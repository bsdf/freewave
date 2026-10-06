#include "playback.hh"
#include "timeutil.hh"
#include "theme.hh"
#include "dotslider.hh"
#include "nowplayingbadge.hh"
#include "controller/favoritesmanager.hh"
#include "ui/theme/components.hh"
#include <QEvent>
#include <QTime>
#include <QIcon>
#include <QFrame>
#include <QApplication>
#include <QPainter>
#include <QSizePolicy>

// Below this elapsed position (ms), the prev button skips track; above, restarts.
constexpr auto SKIP_BACK_THRESH = 5000;

auto
set_button_icon(QPushButton *butt, const QString &icon) -> void
{
  butt->setIcon(QIcon::fromTheme(icon, QIcon(QString(":/icons/%1.svg").arg(icon))));
}

// Render a monochrome bundled SVG recolored to `color` (currentColor in the
// SVG doesn't follow the widget palette, so we tint the alpha mask ourselves).
// Rasterise at the icon's true device resolution (logical px × dpr) so the
// glyph is never resampled by the button — a downscale smears thin strokes.
auto
tinted_icon(const QString &res, const QColor &color, int px, qreal dpr) -> QIcon
{
  QPixmap base = QIcon(res).pixmap(QSize(px, px), dpr);
  if (base.isNull())
    return QIcon(res);
  QPixmap out(base.size());
  out.setDevicePixelRatio(dpr);
  out.fill(Qt::transparent);
  QPainter p(&out);
  p.drawPixmap(0, 0, base);
  p.setCompositionMode(QPainter::CompositionMode_SourceIn);
  p.fillRect(out.rect(), color);
  p.end();
  return QIcon(out);
}

auto
make_icon_button(const QString &icon, int size = 30) -> QPushButton *
{
  auto *butt = new QPushButton;
  set_button_icon(butt, icon);
  butt->setFixedSize(size, size);
  butt->setFlat(true);
  return butt;
}

// Render the shared favorite-heart glyph to a button icon: filled `on` (accent)
// when favorited, else an outline in `off`. `scale` drives the pop bump.
auto
render_heart_icon(bool filled, const QColor &on, const QColor &off, qreal scale, qreal dpr) -> QIcon
{
  constexpr int PX = 26;
  QPixmap pm(QSize(PX, PX) * dpr);
  pm.setDevicePixelRatio(dpr);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  favheart::paint(&p, favheart::box_in(QRectF(0, 0, PX, PX), 15.0), filled, on, off, scale);
  p.end();
  return QIcon(pm);
}

PlaybackView::PlaybackView(std::shared_ptr<FavoritesManager> favman, QWidget *parent)
  : QWidget{parent}
  , favman{std::move(favman)}
{
  create_widgets();
  setup_connections();
  update_fonts();
}

auto
PlaybackView::create_widgets() -> void
{
  setFixedHeight(76);
  setAutoFillBackground(true);
  setBackgroundRole(QPalette::Base);
  setStyleSheet("PlaybackView { border-top: 1px solid palette(midlight); }");

  auto main_layout = new QHBoxLayout(this);
  // top margin gives the transport breathing room below the border; bottom
  // margin lifts the whole bar (incl. the wave) off the window edge
  main_layout->setContentsMargins(16, 8, 16, 8);
  main_layout->setSpacing(16);

  // ── Left: mini now-playing badge (cover + title/artist) ───────────────────
  badge = new NowPlayingBadge;

  // ── Center: transport + progress bar ───────────────────────────────────────
  const auto transport_btn_style = "QPushButton { border-radius: 4px; }"
                                   "QPushButton:hover { background: palette(link); }";

  shuffle_button = make_icon_button("media-playlist-shuffle", 26);
  shuffle_button->setCheckable(true);
  shuffle_button->setStyleSheet(
      QString(transport_btn_style) + "QPushButton:checked { background: palette(highlight); }"
                                     "QPushButton:checked:hover { background: palette(highlight); }");
  prev_button = make_icon_button("media-skip-backward", 26);
  prev_button->setObjectName("prev_button"); // test hook: no distinguishing text/tooltip otherwise
  prev_button->setStyleSheet(transport_btn_style);
  playpause_button = new QPushButton("▶");
  playpause_button->setFixedSize(32, 32);
  playpause_button->setFlat(true);
  {
    auto f = theme::ui_sans(14);
    playpause_button->setFont(f);
  }
  playpause_button->setStyleSheet(
      QString("QPushButton { background: palette(window-text); color: palette(window); border-radius: 16px; border: none; }"
              "QPushButton:hover { background: %1; }")
          .arg(theme::tok::play_hover.name()));
  next_button = make_icon_button("media-skip-forward", 26);
  next_button->setStyleSheet(transport_btn_style);
  repeat_button = make_icon_button("media-repeat-none", 26);
  repeat_button->setObjectName("repeat_button"); // test hook: no distinguishing text/tooltip otherwise
  repeat_button->setStyleSheet(transport_btn_style);

  // Favorite heart — a peer transport control after the loop button, with a thin
  // divider before it. The glyph is rendered from the shared favheart module
  // (palette-tinted, no QSS color), so its filled/outline state and "pop" reuse
  // the same code as the list delegates. Hidden until a song loads / when the
  // backend has no favorites support.
  heart_button = new QPushButton;
  heart_button->setFixedSize(26, 26);
  heart_button->setFlat(true);
  heart_button->setIconSize(QSize(26, 26));
  heart_button->setStyleSheet(transport_btn_style);
  heart_button->setCursor(Qt::PointingHandCursor);
  heart_button->setToolTip("Love this track (L)");
  heart_button->setVisible(false);

  heart_divider = theme::ui::separator(Qt::Vertical);
  heart_divider->setFixedHeight(18);
  heart_divider->setVisible(false);

  // keep stop_button alive but don't show it (signals still needed); parented
  // to this since no layout ever takes it
  stop_button = new QPushButton(this);
  stop_button->setVisible(false);

  auto *transport_layout = new QHBoxLayout;
  transport_layout->setContentsMargins(0, 0, 0, 0);
  transport_layout->setSpacing(6);
  transport_layout->addStretch();
  transport_layout->addWidget(shuffle_button);
  transport_layout->addWidget(prev_button);
  transport_layout->addWidget(playpause_button);
  transport_layout->addWidget(next_button);
  transport_layout->addWidget(repeat_button);
  transport_layout->addWidget(heart_divider);
  transport_layout->addWidget(heart_button);
  transport_layout->addStretch();

  time_label = new QLabel("0:00");
  time_label->setObjectName("time_label"); // test hook: identical styling to time_total_label otherwise
  time_label->setFont(theme::mono(10));
  time_label->setForegroundRole(QPalette::Mid);
  // Wide enough to hold the longest thing it ever says, so swapping the elapsed
  // time for "seeking" mid-playback doesn't shove the slider sideways.
  time_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  time_total_label = new QLabel("0:00");
  time_total_label->setFont(theme::mono(10));
  time_total_label->setForegroundRole(QPalette::Mid);
  time_total_label->setMinimumWidth(32);

  playback_slider = new WaveSlider(Qt::Horizontal);
  playback_slider->setObjectName("playback_slider"); // test hook: WaveSlider has no Q_OBJECT of its own to findChild<> by type
  playback_slider->setMaximum(100);
  // Slider tracks position in milliseconds; keep keyboard seek steps usable.
  playback_slider->setSingleStep(5000);
  playback_slider->setPageStep(10000);
  playback_slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  auto *progress_layout = new QHBoxLayout;
  progress_layout->setContentsMargins(0, 0, 0, 0);
  progress_layout->setSpacing(8);
  progress_layout->addWidget(time_label);
  progress_layout->addWidget(playback_slider);
  progress_layout->addWidget(time_total_label);

  auto *center_layout = new QVBoxLayout;
  center_layout->setContentsMargins(0, 0, 0, 0);
  center_layout->setSpacing(0);
  center_layout->addStretch(1);
  center_layout->addLayout(transport_layout);
  center_layout->addSpacing(5);
  center_layout->addLayout(progress_layout);
  center_layout->addStretch(1);

  auto *center_widget = new QWidget;
  center_widget->setLayout(center_layout);
  // Minimum keeps the transport wave from collapsing to a sliver while still
  // letting the window shrink (the min is the bar's width floor); it shares slack
  // with the side stretches (see Assemble) and grows up to a 560 cap, past which
  // the gaps take the rest.
  center_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  center_widget->setMinimumWidth(300);
  center_widget->setMaximumWidth(560);

  // ── Right: volume + queue button ───────────────────────────────────────────
  queue_button = new QPushButton("Queue");
  {
    auto f = theme::mono(10);
    f.setWeight(QFont::Medium);
    queue_button->setFont(f);
  }
  queue_button->setIcon(
      tinted_icon(":/icons/queue.svg", palette().color(QPalette::Mid), 12, devicePixelRatioF()));
  queue_button->setIconSize(QSize(12, 12));
  queue_button->setStyleSheet(
      QString(
          "QPushButton { border: 1px solid palette(midlight); border-radius: 4px; padding: 5px 10px;"
          " background: transparent; color: palette(mid); }"
          "QPushButton:hover { border-color: palette(highlight); }"
          "QPushButton:checked { background: rgba(%1,%2,%3,26); color: palette(highlight); }"
          "QPushButton:checked:hover { border-color: palette(midlight); }")
          .arg(theme::tok::accent.red())
          .arg(theme::tok::accent.green())
          .arg(theme::tok::accent.blue()));
  queue_button->setCheckable(true);
  queue_button->setCursor(Qt::PointingHandCursor);

  auto *volume_icon = new QPushButton;
  volume_icon->setIcon(tinted_icon(":/icons/audio-volume-medium.svg",
      palette().color(QPalette::Mid), 17, devicePixelRatioF()));
  volume_icon->setFixedSize(20, 20);
  volume_icon->setFlat(true);
  volume_icon->setIconSize(QSize(17, 17));
  volume_icon->setEnabled(false);

  volume_slider = new DotSlider(Qt::Horizontal);
  volume_slider->setMaximum(100);
  volume_slider->setSliderPosition(100);
  volume_slider->setFixedWidth(70);
  volume_slider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  auto *right_layout = new QHBoxLayout;
  right_layout->setContentsMargins(0, 0, 0, 0);
  right_layout->setSpacing(0);
  right_layout->addStretch();
  right_layout->addWidget(volume_icon, 0, Qt::AlignVCenter);
  right_layout->addSpacing(6);
  right_layout->addWidget(volume_slider, 0, Qt::AlignVCenter);
  right_layout->addSpacing(8);
  right_layout->addWidget(queue_button);

  auto *right_widget = new QWidget;
  right_widget->setLayout(right_layout);
  // Match the badge's width so the equal side stretches center the transport
  // truly in the window (content stays edge-pinned via the leading stretch).
  right_widget->setFixedWidth(220);

  // ── Assemble ───────────────────────────────────────────────────────────────
  // Badge anchored hard-left, volume/queue hard-right, transport centered between
  // them. The center takes the lion's share of the slack (stretch 4 vs the side
  // stretches' 1) so the wave is large by default and grows toward its 560 cap,
  // while the side stretches still open modest gaps that pin the badge/controls to
  // the edges and keep the transport centered.
  main_layout->addWidget(badge, 0);
  main_layout->addStretch(1);
  main_layout->addWidget(center_widget, 4);
  main_layout->addStretch(1);
  main_layout->addWidget(right_widget, 0);

  // Default the keyboard focus to play/pause rather than the leftmost button
  // (shuffle), so it isn't highlighted on startup. Done here, after the buttons
  // are parented into this window — setTabOrder requires a shared window.
  setTabOrder(playpause_button, shuffle_button);
  setTabOrder(shuffle_button, prev_button);
  setTabOrder(prev_button, next_button);
  setTabOrder(next_button, repeat_button);
  setTabOrder(repeat_button, heart_button);

  // misc timers
  seek_settle_timer = new QTimer(this);
  seek_settle_timer->setSingleShot(true);
  // Fallback only — `seeking` normally clears when the backend position
  // reconciles (see update_clock). This covers ~2 backend ticks (1 s each) so
  // a stale-position window can't outlive it; a track change mid-seek relies
  // on it to resume.
  seek_settle_timer->setInterval(2000);

  seek_debounce_timer = new QTimer(this);
  seek_debounce_timer->setSingleShot(true);
  seek_debounce_timer->setInterval(150);

  // A seek that settles quickly should say nothing: flashing "seeking" for two
  // frames on every scrub of a local file is noise. Only a wait long enough to
  // be mistaken for a hang earns the announcement.
  busy_delay_timer = new QTimer(this);
  busy_delay_timer->setSingleShot(true);
  busy_delay_timer->setInterval(400);
}

auto
PlaybackView::update_fonts() -> void
{
  badge->update_fonts();
  time_label->setFont(theme::mono(10));
  time_total_label->setFont(theme::mono(10));

  // The elapsed time shares its label with the word that replaces it while the
  // player is still catching up, so reserve the wider of the two — measured
  // through the label itself, since its own margins are part of the width.
  const QString shown = time_label->text();
  time_label->setText(tr("seeking"));
  time_label->setMinimumWidth(std::max(32, time_label->sizeHint().width()));
  time_label->setText(shown);
}

auto
PlaybackView::setup_connections() -> void
{
  connect(playpause_button, &QPushButton::clicked,
      this, &PlaybackView::playpause);
  connect(stop_button, &QPushButton::clicked,
      this, &PlaybackView::stop_clicked);
  connect(next_button, &QPushButton::clicked,
      [&] { emit next(); });
  connect(repeat_button, &QPushButton::clicked,
      this, &PlaybackView::repeat_clicked);
  connect(shuffle_button, &QPushButton::clicked,
      [&] { emit shuffle_state(!shuffle); });

  connect(prev_button, &QPushButton::clicked,
      [&] { clock_position < SKIP_BACK_THRESH ? emit prev() : emit seek(0); });

  connect(badge, &NowPlayingBadge::clicked,
      this, &PlaybackView::open_now_playing);

  // Favorite heart: click toggles; the pop runs on the confirming favorite_changed
  // (so it fires for external stars too); favorites_reset re-reads the icon.
  connect(heart_button, &QPushButton::clicked,
      this, &PlaybackView::toggle_current_favorite);
  if (favman)
    {
      connect(favman.get(), &FavoritesManager::favorite_changed, this,
          [this](const QString &uri, bool fav) {
            if (uri != current_uri)
              return;
            if (fav)
              hearts.pop(uri);
            update_heart_icon();
          });
      connect(favman.get(), &FavoritesManager::favorites_reset, this, [this] {
        update_heart_availability();
        update_heart_icon();
      });
      connect(&hearts, &HeartPopAnimator::needs_repaint,
          this, &PlaybackView::update_heart_icon);
    }

  // The scrubber has no handle to grab, so it drives the slider-down state from
  // its own press/release (see WaveSlider); those moments arm/disarm the seek
  // state machine. During a drag `seeking` keeps the backend from yanking the
  // stale clock; release commits the final position.
  connect(playback_slider, &QAbstractSlider::sliderPressed, this, [this] {
    dragging = true;
    seeking = true;
  });
  connect(playback_slider, &QAbstractSlider::sliderReleased, this, [this] {
    dragging = false;
    playbackslider_seek();
  });

  // Keyboard seek (arrows, page up/down) — debounced to handle key-hold
  connect(playback_slider, &QAbstractSlider::actionTriggered,
      this, [this](int action) {
        if (!seeking && action != QAbstractSlider::SliderMove)
          seek_debounce_timer->start();
      });

  connect(queue_button, &QPushButton::clicked,
      this, [this] { emit toggle_queue(); });

  connect(volume_slider, &QSlider::valueChanged,
      this, &PlaybackView::volumeslider_seek);

  connect(seek_settle_timer, &QTimer::timeout,
      this, [this] { seeking = false; });

  connect(busy_delay_timer, &QTimer::timeout, this, [this] {
    if (busy == PlayBusy::None) return;
    busy_visible = true;
    update_timelabel();
  });
  connect(seek_debounce_timer, &QTimer::timeout,
      this, &PlaybackView::playbackslider_seek);
}

auto
PlaybackView::changeEvent(QEvent *event) -> void
{
  if (event->type() == QEvent::ApplicationFontChange)
    update_fonts();
  QWidget::changeEvent(event);
}

auto
PlaybackView::clock_tick(qint64 elapsed_ms, qint64 /*total_ms*/) -> void
{
  // ClockMan re-anchors from every backend event, including the stale
  // pre-seek position it reports for a tick or two after a seek (see
  // update_clock(const PlaybackState&)). Trusting that here would clobber
  // clock_position before the reconciliation check gets to compare against
  // it, defeating it. So: while a seek is settling, ignore ClockMan entirely
  // (mirrors `dragging`) and let update_clock(const PlaybackState&) alone
  // decide when to trust the backend again.
  if (seeking)
    return;
  update_clock(static_cast<uint64_t>(elapsed_ms));
}

auto
PlaybackView::update_clock(const PlaybackState &state) -> void
{
  if (state.total_ms > 0)
    max_position = state.total_ms;

  // After a seek, the backend keeps reporting the pre-seek position for a tick
  // or two while the flush-seek settles (GStreamer polls position every ~1 s).
  // Trusting those samples yanks the slider backwards, so ignore them until one
  // reconciles with our locally-advanced clock. The fallback timer force-clears
  // `seeking` if the backend never lands close (e.g. a track change mid-seek).
  if (seeking)
    {
      if (dragging) return;                      // clock is intentionally stale during a drag
      constexpr int64_t RECONCILE_TOL_MS = 1200; // ~one position tick
      const int64_t diff = static_cast<int64_t>(state.elapsed_ms)
                           - static_cast<int64_t>(clock_position);
      if (diff > RECONCILE_TOL_MS || diff < -RECONCILE_TOL_MS) return;
      seeking = false;
      seek_settle_timer->stop();
    }

  update_clock(static_cast<uint64_t>(state.elapsed_ms));
}

auto
PlaybackView::update_clock(uint64_t pos) -> void
{
  clock_position = pos;

  update_timelabel();
  update_playbackslider();
}

auto
PlaybackView::update_timelabel() -> void
{
  // While the player is still getting there, the elapsed time is a request, not
  // a measurement — so say what is happening instead of showing a number that
  // is standing still.
  if (busy_visible)
    time_label->setText(busy == PlayBusy::Seeking ? tr("seeking") : tr("loading"));
  else
    time_label->setText(timeutil::ms_to_text(clock_position));
  time_total_label->setText(timeutil::ms_to_text(max_position));
}

auto
PlaybackView::set_busy(PlayBusy state) -> void
{
  if (busy == state) return;
  busy = state;

  if (busy == PlayBusy::None)
    {
      busy_delay_timer->stop();
      if (!busy_visible) return;
      busy_visible = false;
      update_timelabel();
      return;
    }

  if (!busy_visible)
    busy_delay_timer->start();
}

auto
PlaybackView::playbackslider_seek() -> void
{
  auto pos = playback_slider->sliderPosition();
  seeking = true; // covers keyboard seeks too (mouse sets it on press)
  update_clock(pos);
  seek_settle_timer->start();
  emit seek(pos);
}

auto
PlaybackView::volumeslider_seek() -> void
{
  auto pos = volume_slider->sliderPosition();
  emit update_volume(pos);
}

auto
PlaybackView::update_playbackslider() -> void
{
  // While a keyboard seek is pending (debounce armed), the slider holds the
  // user's nudged position; resetting it to the live clock here would erase the
  // arrow-key step before playbackslider_seek() reads it (the clock ticks every
  // 33 ms, well inside the 150 ms debounce).
  if (!seeking && !seek_debounce_timer->isActive())
    {
      playback_slider->setMaximum(max_position);
      playback_slider->setSliderPosition(clock_position);
    }
}

auto
PlaybackView::update_playstate(bool playing) -> void
{
  this->playing = playing;

  playpause_button->setText(playing ? "⏸" : "▶");
}

auto
PlaybackView::playpause() -> void
{
  playing ? emit pause() : emit play();
}

auto
PlaybackView::stop_clicked() -> void
{
  emit stop();
}

auto
PlaybackView::repeat_clicked() -> void
{
  spdlog::debug("repeat button clicked. repeat = {} single = {}", repeat, single);

  if (repeat && single)
    emit repeat_state(false, false);
  else if (repeat)
    emit repeat_state(true, true);
  else
    emit repeat_state(true, false);
}

auto
PlaybackView::set_status(PlaybackState state) -> void
{
  set_busy(state.busy);
  update_clock(state);
  update_playstate(state.state == PlayState::Playing);

  // update volume
  auto vol = state.volume;
  if (vol == -1)
    {
      volume_slider->setEnabled(false);
    }
  else
    {
      // HACK to prevent infinite volume loop
      volume_slider->blockSignals(true);
      volume_slider->setSliderPosition(vol);
      volume_slider->blockSignals(false);
    }

  // update repeat/single/shuffle status
  repeat = state.repeat;
  single = state.single;
  shuffle = state.shuffle;

  if (single)
    set_button_icon(repeat_button, "media-repeat-single");
  else if (repeat)
    set_button_icon(repeat_button, "media-repeat-all");
  else
    set_button_icon(repeat_button, "media-repeat-none");

  shuffle_button->setChecked(shuffle);
}

auto
PlaybackView::set_queue_active(bool active) -> void
{
  queue_button->setChecked(active);
  queue_button->setIcon(tinted_icon(
      ":/icons/queue.svg", palette().color(active ? QPalette::Highlight : QPalette::Mid),
      12, devicePixelRatioF()));
}

auto
PlaybackView::set_album_art(const QPixmap &pixmap) -> void
{
  badge->set_album_art(pixmap);
}

auto
PlaybackView::set_current_song(const song &song) -> void
{
  spdlog::debug("playbackview got current song = {}", song.uri);
  badge->set_song(song);

  current_uri = song.uri;
  update_heart_availability();
  update_heart_icon();
}

auto
PlaybackView::update_heart_availability() -> void
{
  const bool avail = favman && favman->available();
  heart_button->setVisible(avail);
  heart_divider->setVisible(avail);
}

auto
PlaybackView::toggle_current_favorite() -> void
{
  if (favman && !current_uri.isEmpty())
    favman->toggle(current_uri);
}

auto
PlaybackView::update_heart_icon() -> void
{
  bool fav = favman && favman->is_favorite(current_uri);
  auto on = palette().color(QPalette::Highlight);
  auto off = palette().color(QPalette::Mid);
  qreal scale = fav ? hearts.scale_for(current_uri) : 1.0;
  heart_button->setIcon(render_heart_icon(fav, on, off, scale, devicePixelRatioF()));
}
