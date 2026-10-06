#include "nowplayingview.hh"
#include "nowplayingmodel.hh"
#include "theme.hh"
#include "themebridge.hh"
#include "controller/backend.hh"
#include "controller/settings.hh"

#include <QCursor>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickWidget>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#ifdef ENABLE_VISUALIZER
#include "controller/visualizercontroller.hh"

#include <QImage>

#include <algorithm>
#include <cmath>
#endif

namespace {
// Fullscreen inactivity before the cursor blanks (video-player convention).
constexpr int CURSOR_IDLE_MS = 2500;
} // namespace

// Serves the current cover pixmap to QML. Pull-based: QML re-requests whenever
// the model's coverId changes (the id itself is just a cache-buster, ignored
// here). Pixmap providers are invoked on the GUI thread, so no locking needed.
class NowPlayingImageProvider : public QQuickImageProvider {
public:
  NowPlayingImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
  {
  }

  auto set_pixmap(const QPixmap &px) -> void { current = px; }

  auto requestPixmap(const QString &, QSize *size, const QSize &) -> QPixmap override
  {
    if (current.isNull())
      {
        // Transparent placeholder until the first cover lands — avoids QML's
        // "failed to get image" warning while showing nothing.
        QPixmap blank(1, 1);
        blank.fill(Qt::transparent);
        if (size)
          *size = blank.size();
        return blank;
      }
    if (size)
      *size = current.size();
    return current;
  }

private:
  QPixmap current;
};

#ifdef ENABLE_VISUALIZER
namespace {
constexpr int HIST_ROWS = 192;        // rows stored in the history texture
constexpr double ROWS_PER_SEC = 22.0; // scroll speed (history rows committed/sec)
} // namespace

// Rolling scrolling-spectrogram for history-based visualizers, stored as a ring
// buffer: each committed row is one past spectrum written at `committed % rows`,
// so a commit changes exactly one texel row (only the newest can be a frame stale
// on the GPU — no whole-field shift). The shader reconstructs row order from
// `head()` (= committed count) and animates smooth sub-row motion from `frac()`
// (wall-clock scroll position, 0..1); both come from one update so geometry and
// data never disagree. No Qt eventing — driven from VisualizerController::bands_ready.
class VizHistory {
public:
  VizHistory(int bands, int rows, double rows_per_sec)
    : img(bands, rows, QImage::Format_Grayscale8)
    , rps(rows_per_sec)
  {
    img.fill(0);
  }

  auto advance(const QList<qreal> &bands, double dt) -> void
  {
    scroll_pos += dt * rps;
    const int target = static_cast<int>(std::floor(scroll_pos));
    while (committed < target)
      {
        write_row(committed % img.height(), bands);
        ++committed;
        ++rev;
      }
  }

  auto reset() -> void
  {
    img.fill(0);
    scroll_pos = 0.0;
    committed = 0;
    ++rev;
  }

  auto image() const -> QImage { return img; }
  auto revision() const -> int { return rev; }
  auto head() const -> int { return committed; }
  auto frac() const -> double { return scroll_pos - committed; }

private:
  auto write_row(int row, const QList<qreal> &bands) -> void
  {
    const int w = img.width();
    uchar *p = img.scanLine(row);
    for (int i = 0; i < w; ++i)
      {
        double v = (i < bands.size()) ? std::clamp(double(bands[i]), 0.0, 1.0) : 0.0;
        p[i] = static_cast<uchar>(v * 255.0 + 0.5);
      }
  }

  QImage img;
  double rps;
  double scroll_pos = 0.0;
  int committed = 0;
  int rev = 0;
};

class VizHistoryImageProvider : public QQuickImageProvider {
public:
  explicit VizHistoryImageProvider(VizHistory *h)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , hist(h)
  {
  }

  auto requestImage(const QString &, QSize *size, const QSize &) -> QImage override
  {
    QImage img = hist->image();
    if (size)
      *size = img.size();
    return img;
  }

private:
  VizHistory *hist;
};
#endif // ENABLE_VISUALIZER

NowPlayingView::NowPlayingView(Backend *backend, QWidget *parent)
  : QWidget(parent)
  , backend(backend)
{
  model = new NowPlayingModel(this);
  image_provider = new NowPlayingImageProvider();

  quick = new QQuickWidget(this);
  quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
  // Match the scene's darkest field tone so there is no white flash before the
  // QML renders its gradient.
  quick->setClearColor(theme::tok::npwash::tint_far);
  quick->engine()->addImageProvider(QStringLiteral("fwnp"), image_provider);
  quick->rootContext()->setContextProperty(QStringLiteral("np"), model);
  // Theme token bridge — QML reads colors/fonts from here instead of hardcoding
  // them. Parented to the model (alive for the scene's lifetime, torn down first).
  quick->rootContext()->setContextProperty(QStringLiteral("Theme"),
      new ThemeBridge(model));

  connect(model, &NowPlayingModel::closeRequested,
      this, &NowPlayingView::close_requested);
  connect(model, &NowPlayingModel::seekRequested, this,
      [this](int ms) { emit seek(static_cast<uint32_t>(ms < 0 ? 0 : ms)); });
  connect(model, &NowPlayingModel::fullscreenToggleRequested,
      this, &NowPlayingView::fullscreen_toggle_requested);

  // Fullscreen cursor auto-hide: the QML scene pokes `activity` on mouse movement,
  // which un-hides the cursor and restarts the countdown; the timeout blanks it.
  idle_timer = new QTimer(this);
  idle_timer->setSingleShot(true);
  idle_timer->setInterval(CURSOR_IDLE_MS);
  connect(idle_timer, &QTimer::timeout, this, [this] {
    if (np_fullscreen)
      set_cursor_hidden(true);
  });
  connect(model, &NowPlayingModel::activity, this, [this] {
    if (!np_fullscreen)
      return;
    set_cursor_hidden(false);
    idle_timer->start();
  });

  // Delay showing "seeking"/"loading" so a wait that resolves quickly (the
  // common case) doesn't flicker the scene.
  busy_delay_timer = new QTimer(this);
  busy_delay_timer->setSingleShot(true);
  busy_delay_timer->setInterval(400);
  connect(busy_delay_timer, &QTimer::timeout, this, [this] {
    if (busy == PlayBusy::None) return;
    busy_visible = true;
    model->set_busy_text(busy == PlayBusy::Seeking ? tr("seeking") : tr("loading"));
  });

  // Seed a valid accent so the field is never pure black before a song's
  // accent arrives (and as the fallback for albums without a derivable one).
  model->set_accent(theme::tok::accent);

  // Restore the persisted visualizer preference (off by default), then persist
  // any in-scene toggle and start/stop capture to match.
  model->set_visualizer_enabled(Settings().visualizer_enabled());
  connect(model, &NowPlayingModel::visualizerEnabledChanged, this, [this]() {
    Settings().set_visualizer_enabled(model->visualizer_enabled());
#ifdef ENABLE_VISUALIZER
    update_visualizer();
#endif
  });

  // Restore the chosen visualization, then persist any in-scene cycle.
  model->set_viz_preset(Settings().visualizer_preset());
  connect(model, &NowPlayingModel::vizPresetChanged, this,
      [this]() { Settings().set_visualizer_preset(model->viz_preset()); });

#ifdef ENABLE_VISUALIZER
  visualizer = new VisualizerController(this);
  // Set before the scene loads: shaders size band-indexed work from bandCount.
  model->set_band_count(visualizer->band_count());
  connect(visualizer, &VisualizerController::bands_ready,
      model, &NowPlayingModel::set_bands);
  connect(visualizer, &VisualizerController::level_ready,
      model, &NowPlayingModel::set_level);

  // Engine PCM tap (Source::External) — fed when the active backend decodes
  // locally; harmless when it doesn't (the signal simply never fires).
  if (backend)
    connect(backend, &Backend::viz_pcm,
        visualizer, &VisualizerController::feed_pcm);

  // Scrolling-spectrogram history: a generic visualizer input (not ridgeline-
  // specific) — any shader can sample the texture or ignore it.
  history = std::make_unique<VizHistory>(visualizer->band_count(), HIST_ROWS, ROWS_PER_SEC);
  model->set_history_rows(HIST_ROWS); // shaders read the ring modulo from a uniform
  quick->engine()->addImageProvider(QStringLiteral("fwvizhist"),
      new VizHistoryImageProvider(history.get()));
  connect(visualizer, &VisualizerController::bands_ready, this,
      [this](const QList<qreal> &bands) {
        const double dt = std::clamp(viz_clock.restart() / 1000.0, 0.0, 0.1);
        history->advance(bands, dt);
        model->set_history(history->revision(), history->head(), history->frac());
      });
#endif

  quick->setSource(QUrl(QStringLiteral("qrc:/qml/NowPlaying.qml")));

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->addWidget(quick);
}

NowPlayingView::~NowPlayingView()
{
  set_cursor_hidden(false); // don't leave a blanked override cursor behind
  // Tear the QML scene down while `model` is still alive. Otherwise child
  // auto-deletion can destroy the model first, and the scene's bindings
  // re-evaluate against a null `np`, logging "Cannot read property … of null".
  quick->setSource(QUrl());
}

auto
NowPlayingView::set_current_song(const song &s) -> void
{
  model->set_track(s.title, s.artist);
}

auto
NowPlayingView::set_album_art(const QPixmap &px) -> void
{
  image_provider->set_pixmap(px);
  model->set_cover_id(QString::number(++cover_seq));
}

auto
NowPlayingView::set_album_info(const QString &album_name, const QString &year) -> void
{
  model->set_album(album_name, year);
}

auto
NowPlayingView::set_accent(const QColor &accent) -> void
{
  model->set_accent(accent.isValid() ? accent : theme::tok::accent);
}

auto
NowPlayingView::set_progress(qint64 elapsed_ms, qint64 total_ms) -> void
{
  model->set_progress(elapsed_ms, total_ms);
}

auto
NowPlayingView::set_fullscreen(bool on) -> void
{
  model->set_fullscreen(on);
  np_fullscreen = on;
  if (on)
    idle_timer->start();
  else
    {
      idle_timer->stop();
      set_cursor_hidden(false);
    }
}

auto
NowPlayingView::set_cursor_hidden(bool hidden) -> void
{
  if (hidden == cursor_hidden)
    return;
  // Application-wide override so it wins over the per-item cursors the QML scene
  // sets (seek bar, corner button). Balanced push/pop, guarded by cursor_hidden.
  if (hidden)
    QGuiApplication::setOverrideCursor(Qt::BlankCursor);
  else
    QGuiApplication::restoreOverrideCursor();
  cursor_hidden = hidden;
}

auto
NowPlayingView::showEvent(QShowEvent *event) -> void
{
  QWidget::showEvent(event);
#ifdef ENABLE_VISUALIZER
  update_visualizer();
#endif
}

auto
NowPlayingView::hideEvent(QHideEvent *event) -> void
{
  QWidget::hideEvent(event);
#ifdef ENABLE_VISUALIZER
  update_visualizer();
#endif
}

#ifdef ENABLE_VISUALIZER
auto
NowPlayingView::update_visualizer() -> void
{
  const bool want = isVisible() && playing && model->visualizer_enabled();

  // Pick the PCM source from the active backend: if it decodes locally, tap its
  // own output (Source::External — only this player's sound, no capture stream,
  // no mic indicator); otherwise fall back to the PipeWire default-sink monitor.
  const bool local_pcm = backend && backend->supports(Backend::Feature::LocalPcm);
  visualizer->set_source(local_pcm ? VisualizerController::Source::External
                                   : VisualizerController::Source::PipeWire);
  if (local_pcm && backend)
    backend->set_viz_pcm_enabled(want); // gate the engine tap to match

  // Clear stale history and the dt clock only on the off→on edge, so periodic
  // status updates while playing don't wipe the scrolling field.
  if (want && !viz_active)
    {
      history->reset();
      viz_clock.restart();
    }
  viz_active = want;
  visualizer->set_active(want);
}
#endif

auto
NowPlayingView::set_busy(PlayBusy state) -> void
{
  if (busy == state) return;
  busy = state;

  if (busy == PlayBusy::None)
    {
      busy_delay_timer->stop();
      if (!busy_visible) return;
      busy_visible = false;
      model->set_busy_text(QString());
      return;
    }

  if (!busy_visible)
    busy_delay_timer->start();
}

auto
NowPlayingView::set_audio_format(const PlaybackState &state) -> void
{
  set_busy(state.busy);

  switch (state.state)
    {
    case PlayState::Playing:
      model->set_play_state(QStringLiteral("playing"));
      break;
    case PlayState::Paused:
      model->set_play_state(QStringLiteral("paused"));
      break;
    case PlayState::Stopped:
      model->set_play_state(QStringLiteral("stopped"));
      break;
    }

#ifdef ENABLE_VISUALIZER
  playing = state.state == PlayState::Playing;
  update_visualizer();
#endif

  if (state.state == PlayState::Stopped || state.bits <= 0 || state.sample_rate_hz <= 0)
    {
      model->set_audio_format(QString());
      return;
    }

  model->set_audio_format(QString("%1-bit ～ %2 kHz")
          .arg(state.bits)
          .arg(state.sample_rate_hz / 1000.0, 0, 'g', 5));
}
