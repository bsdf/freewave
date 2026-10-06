#include "aclistview.hh"

#include "aclistmodel.hh"
#include "aclistitem.hh"

#include <algorithm>

#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QIcon>
#include <QMenu>

#define COVER_WIDTH  200
#define COVER_HEIGHT 200

AlbumCoverListView::AlbumCoverListView(QWidget *parent)
  : QListView{parent}
{
  setViewMode(ViewMode::IconMode);
  setResizeMode(ResizeMode::Adjust);
  setWrapping(true);
  setUniformItemSizes(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  setVerticalScrollMode(ScrollMode::ScrollPerPixel);

  setSelectionMode(SelectionMode::ExtendedSelection);
  setDragDropMode(QAbstractItemView::DragOnly);
  setMouseTracking(true);
  viewport()->setMouseTracking(true);

  album_item = std::make_unique<AlbumCoverListItem>(this);
  setItemDelegate(album_item.get());
}

auto
AlbumCoverListView::set_cover_size(QSize size) -> void
{
  album_item->set_options({size, QMargins(10, 10, 10, 4), true});
  if (auto *m = qobject_cast<AlbumCoverListModel *>(model()))
    m->set_cover_size(size);
  scheduleDelayedItemsLayout();
  viewport()->update();
}

auto
AlbumCoverListView::set_rounded_corners(bool enabled, int radius) -> void
{
  album_item->set_rounded_corners(enabled, radius);
  viewport()->update();
}

auto
AlbumCoverListView::set_drop_shadow(bool enabled) -> void
{
  album_item->set_drop_shadow(enabled);
  viewport()->update();
}

auto
AlbumCoverListView::changeEvent(QEvent *event) -> void
{
  QListView::changeEvent(event);
  if (event->type() == QEvent::ApplicationFontChange)
    scheduleDelayedItemsLayout();
}

auto
AlbumCoverListView::resizeEvent(QResizeEvent *event) -> void
{
  QListView::resizeEvent(event);

  // Compute the gap between the last tile's right edge and the scrollbar, then
  // apply half of it as left margin so the grid is visually centred.
  // maximumViewportSize() is already reduced by left_margin from a previous call,
  // so add it back to get the true unmaargined width before recalculating.
  auto tile_w = album_item->tile_width();
  auto vp_w = maximumViewportSize().width() + left_margin;
  auto n = (vp_w - 1) / tile_w;
  if (n <= 0) return;

  auto empty = (vp_w - 1) - n * tile_w;
  auto item_w = tile_w + empty / n;
  auto remaining = vp_w - n * item_w;
  auto new_left = remaining / 2;

  if (new_left != left_margin)
    {
      left_margin = new_left;
      setViewportMargins(left_margin, 0, 0, 0);
    }
}

auto
AlbumCoverListView::wheelEvent(QWheelEvent *event) -> void
{
  constexpr int WHEEL_SLOWDOWN = 10;

  QPoint angle_delta = event->angleDelta() / WHEEL_SLOWDOWN;
  QPoint pixel_delta = event->pixelDelta() / WHEEL_SLOWDOWN;
  if (angle_delta.isNull() && pixel_delta.isNull())
    {
      event->accept();
      return;
    }

  QWheelEvent scaled{event->position(), event->globalPosition(), pixel_delta, angle_delta,
      event->buttons(), event->modifiers(), event->phase(), event->inverted(), event->source()};
  QListView::wheelEvent(&scaled);
  event->setAccepted(scaled.isAccepted());
}

auto
AlbumCoverListView::contextMenuEvent(QContextMenuEvent *event) -> void
{
  auto idx = indexAt(event->pos());
  if (!idx.isValid())
    return;

  // Right-clicking a tile outside the current selection retargets to just that
  // tile, matching common file-manager behavior.
  if (!selectionModel()->isSelected(idx))
    {
      selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect);
      setCurrentIndex(idx);
    }

  auto selected = selectionModel()->selectedIndexes();
  std::sort(selected.begin(), selected.end(),
      [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });

  QList<album> albums;
  for (const auto &sel : selected)
    albums.push_back(sel.data(AlbumCoverListModel::UserRoleAlbum).value<album>());
  if (albums.isEmpty())
    return;

  QMenu menu(this);
  auto *play = menu.addAction(QIcon::fromTheme("media-playback-start"), "Play");
  auto *next = menu.addAction(QIcon::fromTheme("media-skip-forward"), "Play Next");
  auto *append = menu.addAction(QIcon::fromTheme("list-add"), "Add to Queue");

  connect(play, &QAction::triggered, this, [this, albums] { emit play_albums(albums); });
  connect(next, &QAction::triggered, this, [this, albums] { emit play_albums_next(albums); });
  connect(append, &QAction::triggered, this, [this, albums] { emit queue_albums(albums); });

  if (playlists_available)
    {
      menu.addSeparator();
      auto *add = menu.addAction(QIcon::fromTheme("list-add"), "Add to playlist…");
      auto primary = albums.first();
      connect(add, &QAction::triggered, this, [this, primary] { emit add_to_playlist(primary); });
    }

  menu.exec(event->globalPos());
}
