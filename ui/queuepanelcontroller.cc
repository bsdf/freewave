#include "queuepanelcontroller.hh"

#include <QBoxLayout>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QSplitter>
#include <QSplitterHandle>
#include <QVariantAnimation>
#include <algorithm>

#include "controller/backendcontroller.hh"
#include "controller/favoritesmanager.hh"
#include "controller/settings.hh"
#include "fwmark.hh"
#include "queuelistview.hh"
#include "theme.hh"
#include "ui/theme/components.hh"

namespace {
// Splitter handle that fills the (Base-colored) gap and paints a centered 1px
// hairline, so a comfortable grab strip reads as the same thin divider the
// queue panel used before it became resizable.
class HairlineHandle : public QSplitterHandle {
public:
  using QSplitterHandle::QSplitterHandle;

protected:
  auto paintEvent(QPaintEvent *) -> void override
  {
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Base));
    p.setPen(palette().color(QPalette::Midlight));
    int x = width() / 2;
    p.drawLine(x, 0, x, height());
  }
};

class HairlineSplitter : public QSplitter {
public:
  using QSplitter::QSplitter;

protected:
  auto createHandle() -> QSplitterHandle * override
  {
    return new HairlineHandle(orientation(), this);
  }
};
} // namespace

auto
queuepanel::display_mode_from_string(const QString &s) -> QueueListItem::QueueDisplayMode
{
  if (s == "compact")
    return QueueListItem::QueueDisplayMode::Compact;
  if (s == "full")
    return QueueListItem::QueueDisplayMode::Full;
  return QueueListItem::QueueDisplayMode::Grouped;
}

namespace {
constexpr int MIN_RUN = 3;
constexpr double ALBUMNESS_THRESHOLD = 0.6;
} // namespace

auto
queuepanel::auto_display_mode(const QList<song> &songs, bool shuffle) -> QueueListItem::QueueDisplayMode
{
  if (shuffle)
    return QueueListItem::QueueDisplayMode::Full;
  if (songs.isEmpty())
    return QueueListItem::QueueDisplayMode::Grouped;

  QList<int> run_lengths;
  int i = 0;
  while (i < songs.size())
    {
      if (songs[i].album_hash.isEmpty())
        {
          run_lengths.append(1);
          ++i;
          continue;
        }
      int j = i + 1;
      while (j < songs.size() && songs[j].album_hash == songs[i].album_hash)
        ++j;
      run_lengths.append(j - i);
      i = j;
    }

  if (run_lengths.size() == 1 && !songs[0].album_hash.isEmpty())
    return QueueListItem::QueueDisplayMode::Grouped;

  int album_tracks = 0;
  for (int len : run_lengths)
    if (len >= MIN_RUN)
      album_tracks += len;

  double albumness = static_cast<double>(album_tracks) / songs.size();
  return albumness >= ALBUMNESS_THRESHOLD ? QueueListItem::QueueDisplayMode::Grouped
                                          : QueueListItem::QueueDisplayMode::Full;
}

QueuePanelController::QueuePanelController(QueueListView *queuelist,
    QWidget *main_area, QWidget *splitter_parent, BackendController *backend,
    std::shared_ptr<AlbumArtManager> artman, std::shared_ptr<LibraryManager> libman,
    std::shared_ptr<FavoritesManager> favman,
    QObject *parent)
  : QObject(parent)
  , queuelist(queuelist)
  , backend(backend)
  , artman(std::move(artman))
  , libman(std::move(libman))
  , favman(std::move(favman))
{
  queue_model = std::make_unique<QueueListModel>(this->artman, this->libman);
  queue_model->set_favorites(this->favman.get());
  queuelist->setModel(queue_model.get());

  // Heart hit-test → toggle favorite; the pop bump runs on the confirming
  // favorite_changed (so external stars animate too).
  if (this->favman)
    {
      connect(queuelist, &QueueListView::favorite_toggled,
          this->favman.get(), &FavoritesManager::toggle);
      connect(this->favman.get(), &FavoritesManager::favorite_changed, this,
          [this](const QString &uri, bool fav) {
            if (fav)
              this->queuelist->pop_favorite(uri);
          });
    }

  build_panel(main_area, splitter_parent);
  apply_saved_settings();

  // queuelist + queue_model -> backend
  connect(queuelist, &QueueListView::delete_items,
      [this](const auto &items) { this->backend->remove_from_queue(items); });
  connect(queuelist, &QueueListView::play_item,
      [this](int pos) { this->backend->play_pos(pos); });
  connect(queuelist, &QAbstractItemView::activated,
      [this](const QModelIndex &index) { this->backend->play_pos(index.row()); });
  connect(queue_model.get(), &QueueListModel::rearrange_queue,
      backend, &Backend::rearrange_queue);
  connect(queue_model.get(), &QueueListModel::insert_queue_at,
      [this](uint32_t idx, const QList<song> &songs) {
        this->backend->insert_queue(songs, idx);
      });
  connect(queue_model.get(), &QueueListModel::insert_albums_at,
      [this](uint32_t idx, const QList<album> &albums) {
        this->backend->insert_albums(albums, idx);
      });

  connect(backend, &Backend::queue_changed, this, &QueuePanelController::set_queue);

  // Both backends hand over the queue before the library (MPD dispatches
  // fetch_queue ahead of fetch_all_songs; Subsonic restores the saved queue at
  // connect and polls the library after), so grouped rows get laid out while
  // get_album() still answers with a blank artist — i.e. at the taller
  // show-the-artist height. Relayout once the library lands, or the rows keep the
  // old spacing and show a band of whitespace under each one.
  connect(backend, &Backend::library_changed, this,
      [this] { this->queuelist->refresh_layout(); });

  // The queue belongs to the server being switched away from, and the switch
  // takes the library it renders against with it. Left in place against an empty
  // library every track reads as various-artists, so the rows grow a subtitle
  // line while the view keeps the positions it cached at the old height and each
  // row paints over the next. A switch that never connects sends no
  // queue_changed to replace them either, so drop them here.
  connect(backend, &BackendController::backend_switching, this,
      [this] { set_queue({}); });

  connect(backend, &Backend::playback_state_changed, this, [this](const PlaybackState &st) {
    if (st.shuffle == shuffle)
      return;
    shuffle = st.shuffle;
    if (display_pref == "auto")
      apply_display_mode();
  });
}

auto
QueuePanelController::build_panel(QWidget *main_area, QWidget *splitter_parent) -> void
{
  // Wrap the queue list in a container panel: [header + list]. All colors are
  // palette-driven (autoFillBackground + backgroundRole, plain QFrame hairlines)
  // so a setPalette() swap cascades for the light/dark toggle.
  auto *host_layout = qobject_cast<QBoxLayout *>(queuelist->parentWidget()->layout());

  queue_panel = new QWidget(queuelist->parentWidget());
  queue_panel->setObjectName("queue_panel");
  queue_panel->setMinimumWidth(260);

  auto *panel_h = new QHBoxLayout(queue_panel);
  panel_h->setContentsMargins(0, 0, 0, 0);
  panel_h->setSpacing(0);

  // The splitter's hairline handle is the divider now (see HairlineSplitter), so
  // the panel no longer carries its own left border.
  auto *inner = new QWidget(queue_panel);
  inner->setAutoFillBackground(true);
  inner->setBackgroundRole(QPalette::Base);
  auto *panel_layout = new QVBoxLayout(inner);
  panel_layout->setContentsMargins(0, 0, 0, 0);
  panel_layout->setSpacing(0);
  panel_h->addWidget(inner, 1);

  // Header (mark + "QUEUE" label + count) followed by a bottom hairline
  auto *header_container = new QWidget(inner);
  auto *header_v = new QVBoxLayout(header_container);
  header_v->setContentsMargins(0, 0, 0, 0);
  header_v->setSpacing(0);

  auto *header = new QWidget(header_container);
  auto *q_header_layout = new QHBoxLayout(header);
  q_header_layout->setContentsMargins(14, 9, 14, 8);
  q_header_layout->setSpacing(0);

  auto *left = new QWidget(header);
  auto *left_layout = new QHBoxLayout(left);
  left_layout->setContentsMargins(0, 0, 0, 0);
  left_layout->setSpacing(10);

  queue_mark = new FWMark(12, left);
  queue_mark->set_cross(false);                // queue header shows the wave only
  queue_mark->set_stroke_role(QPalette::Dark); // dim, not accent
  left_layout->addWidget(queue_mark);

  auto *queue_label = theme::ui::section_header("QUEUE", left);
  queue_label->setStyleSheet("letter-spacing: 2px;");
  queue_label->setForegroundRole(QPalette::Dark);
  left_layout->addWidget(queue_label);

  q_header_layout->addWidget(left);
  q_header_layout->addStretch(1);

  queue_count_label = new QLabel("0 tracks", header);
  queue_count_label->setFont(theme::mono(10));
  queue_count_label->setForegroundRole(QPalette::Dark);
  q_header_layout->addWidget(queue_count_label);

  header_v->addWidget(header);

  auto *header_hair = theme::ui::separator(Qt::Horizontal, header_container);
  header_v->addWidget(header_hair);

  panel_layout->addWidget(header_container);

  // List fills the rest of the inner column; background via viewport autofill
  queuelist->setMinimumWidth(0);
  queuelist->setMaximumWidth(QWIDGETSIZE_MAX);
  queuelist->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  queuelist->setFrameShape(QFrame::NoFrame);
  queuelist->setStyleSheet("");
  queuelist->viewport()->setAutoFillBackground(true);
  queuelist->viewport()->setBackgroundRole(QPalette::Base);
  queuelist->setVisible(true);
  panel_layout->addWidget(queuelist);

  // Make the queue user-resizable: host the main area and the queue panel in a
  // horizontal splitter. 260px stays the queue's minimum (the panel's own left
  // hairline is the visible divide, so the splitter handle is a thin 1px grab
  // strip). The main area takes all the extra space, so the queue opens at its
  // minimum and only grows when the user drags the divider; setChildrenCollapsible
  // keeps a drag from collapsing either side (the queue still hides via setVisible).
  content_splitter = new HairlineSplitter(Qt::Horizontal, splitter_parent);
  content_splitter->setObjectName("content_splitter");
  content_splitter->setChildrenCollapsible(false);
  content_splitter->setHandleWidth(9);
  host_layout->removeWidget(main_area);
  content_splitter->addWidget(main_area);
  content_splitter->addWidget(queue_panel);
  content_splitter->setStretchFactor(0, 1);
  content_splitter->setStretchFactor(1, 0);
  // main_area was the only remaining child; the splitter now holds both.
  host_layout->addWidget(content_splitter);

  // Remember the width the user drags the queue to so a hide/show slides back to
  // it; the value is persisted on close (not per drag-pixel) and restored later.
  queue_open_width = std::max(260, Settings().queue_width());
  connect(content_splitter, &QSplitter::splitterMoved, this, [this] {
    if (queue_panel->isVisible())
      queue_open_width = queue_panel->width();
  });
  // Watch the splitter's own resizes to restore the saved width once it's laid
  // out (see eventFilter); the window's resizeEvent fires before the child
  // splitter has real geometry, so it can't drive this.
  content_splitter->installEventFilter(this);

  queue_panel->setVisible(false);
}

auto
QueuePanelController::apply_saved_settings() -> void
{
  Settings sf;
  auto qd = sf.queue_display();
  bool band = sf.queue_color_band();
  display_pref = qd;
  apply_display_mode();
  queuelist->set_color_band(band);
  apply_style(sf.queue_style() == "dark");
}

auto
QueuePanelController::apply_display_mode() -> void
{
  auto mode = display_pref == "auto"
                  ? queuepanel::auto_display_mode(queue_model->get_data(), shuffle)
                  : queuepanel::display_mode_from_string(display_pref);
  if (mode == applied_mode)
    return;
  applied_mode = mode;
  queuelist->set_display_mode(mode);
}

auto
QueuePanelController::apply_style(bool dark) -> void
{
  // Palette cascades to all panel descendants (inner column, hairlines, labels,
  // wave mark) which paint via autoFillBackground/foregroundRole. The list owns
  // its own palette for the item delegate.
  queuelist->set_queue_dark(dark);
  if (queue_panel)
    {
      queue_panel->setPalette(theme::queue_palette(dark));
      queue_panel->update();
    }
}

auto
QueuePanelController::set_initial_visible(bool visible) -> void
{
  if (visible)
    {
      queue_panel->setVisible(true);
      queue_width_restore_pending = true;
    }
}

auto
QueuePanelController::set_queue(const QList<song> &songs) -> void
{
  spdlog::trace("queue changed handler, songs = {}", songs.size());
  queue_model->set_data(songs);
  if (display_pref == "auto")
    apply_display_mode();

  if (queue_count_label)
    queue_count_label->setText(QString("%1 tracks").arg(songs.size()));
}

auto
QueuePanelController::set_current_index(int pos) -> void
{
  queue_model->set_current_index(pos);

  // Only scroll when the playing track actually changes, not on every position
  // update (which would prevent the user from scrolling the queue).
  if (pos != last_scrolled_pos)
    {
      last_scrolled_pos = pos;
      auto idx = queue_model->index(pos, 0);
      queuelist->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    }
}

auto
QueuePanelController::repaint_list() -> void
{
  // The queue may have rendered before libman was populated (album names, group
  // headers, and the various-artists check behind UserRoleShowArtist all resolve
  // from libman) — refresh it now that the scan is done.
  //
  // A repaint is not enough. With no album artist to compare against, every track's
  // artist looked "different", so grouped rows were laid out at the taller
  // show-the-artist height. Once libman answers, QListView re-queries each item's
  // size but reuses the positions it cached at layout time: the rows shrink while
  // the spacing does not, leaving whitespace under every row. The positions have to
  // be recomputed.
  queuelist->refresh_layout();
}

auto
QueuePanelController::eventFilter(QObject *obj, QEvent *event) -> bool
{
  // The window's resizeEvent fires before its child splitter is laid out, so the
  // splitter's own resize is the moment its width becomes real. Restore the saved
  // queue width there, and stop watching once it sticks (a resize through a width
  // too narrow to hold both the saved width and the main area's minimum clamps the
  // queue to its minimum — wait for a wider one). If the queue is hidden at startup
  // there's nothing to restore (toggling open already opens to queue_open_width).
  if (obj == content_splitter && event->type() == QEvent::Resize
      && queue_width_restore_pending)
    {
      int avail = content_splitter->width() - content_splitter->handleWidth();
      if (avail > queue_open_width)
        {
          content_splitter->setSizes({avail - queue_open_width, queue_open_width});
          if (queue_panel->width() >= queue_open_width - 4)
            {
              queue_width_restore_pending = false;
              content_splitter->removeEventFilter(this);
            }
        }
    }
  return QObject::eventFilter(obj, event);
}

auto
QueuePanelController::save_width() -> void
{
  Settings().set_queue_width(queue_open_width);
}

auto
QueuePanelController::toggle_visible() -> void
{
  auto q = queue_panel;
  bool show = !q->isVisible();

  // Slide the queue in/out by animating the splitter's section widths. The
  // splitter floors each section at qSmartMinSize, which ignores a 0 minimum and
  // falls back to the panel's content min hint (~its open width) — so a 0-min
  // close stalls there then snaps shut. Setting the minimum to 1 IS honored
  // (qSmartMinSize only overrides when minWidth > 0), letting the slide reach a
  // 1px sliver before it hides. childrenCollapsible is also relaxed for the
  // slide. Both are restored on finish (min to 260 only when shown).
  q->setMinimumWidth(1);
  content_splitter->setChildrenCollapsible(true);
  int start = show ? 0 : q->width();
  int end = show ? queue_open_width : 0;
  if (show)
    {
      q->setVisible(true);
      // Collapse to zero before the first frame so it doesn't flash at its
      // hint width, then slide open.
      int avail = content_splitter->width() - content_splitter->handleWidth();
      content_splitter->setSizes({avail, 0});
    }

  auto *anim = new QVariantAnimation(this);
  anim->setDuration(200);
  anim->setEasingCurve(QEasingCurve::OutCubic);
  anim->setStartValue(start);
  anim->setEndValue(end);
  connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
    int qw = v.toInt();
    int avail = content_splitter->width() - content_splitter->handleWidth();
    content_splitter->setSizes({std::max(0, avail - qw), qw});
  });
  connect(anim, &QVariantAnimation::finished, this, [this, show] {
    if (!show)
      queue_panel->setVisible(false);
    queue_panel->setMinimumWidth(show ? 260 : 0);
    content_splitter->setChildrenCollapsible(false);
  });
  anim->start(QAbstractAnimation::DeleteWhenStopped);

  Settings().set_ui_queue_visible(show);
  emit visibility_changed(show);
}

auto
QueuePanelController::set_display_mode(const QString &mode) -> void
{
  Settings().set_queue_display(mode);
  display_pref = mode;
  apply_display_mode();
}

auto
QueuePanelController::set_color_band(bool enabled) -> void
{
  Settings().set_queue_color_band(enabled);
  queuelist->set_color_band(enabled);
}

auto
QueuePanelController::set_style(const QString &style) -> void
{
  Settings().set_queue_style(style);
  apply_style(style == "dark");
}
