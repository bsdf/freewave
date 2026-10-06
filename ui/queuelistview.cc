#include "queuelistview.hh"

#include <QContextMenuEvent>
#include <QDrag>
#include <QEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

#include <algorithm>

#include "theme.hh"
#include "queuelistmodel.hh"
#include "model/song.hh"

QueueListView::QueueListView(QWidget *parent)
  : QListView{parent}
{
  setDragEnabled(true);
  setAcceptDrops(true);
  setDropIndicatorShown(true);
  setSelectionMode(SelectionMode::ExtendedSelection);
  setMouseTracking(true);

  list_item = std::make_unique<QueueListItem>();
  setItemDelegate(list_item.get());

  connect(list_item.get(), &QueueListItem::needs_repaint, this,
      [this] { viewport()->update(); });
}

auto
QueueListView::pop_favorite(const QString &uri) -> void
{
  list_item->pop(uri);
}

auto
QueueListView::changeEvent(QEvent *event) -> void
{
  QListView::changeEvent(event);
  if (event->type() == QEvent::ApplicationFontChange)
    scheduleDelayedItemsLayout();
}

auto
QueueListView::keyPressEvent(QKeyEvent *event) -> void
{
  switch (event->key())
    {
    case Qt::Key_Delete:
      emit delete_items(selectedIndexes());
      break;

    default:
      break;
    }
  QListView::keyPressEvent(event);
}

auto
QueueListView::contextMenuEvent(QContextMenuEvent *event) -> void
{
  auto idx = indexAt(event->pos());
  if (!idx.isValid() || in_group_header(event->pos()))
    return;

  // Right-clicking a row outside the current selection retargets to just that
  // row, matching the album-grid menu behavior.
  if (!selectionModel()->isSelected(idx))
    {
      selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect);
      setCurrentIndex(idx);
    }

  auto selected = selectionModel()->selectedIndexes();
  if (selected.isEmpty())
    return;
  std::sort(selected.begin(), selected.end(),
      [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });

  QMenu menu(this);
  auto *play = menu.addAction(QIcon::fromTheme("media-playback-start"), "Play");
  auto *remove = menu.addAction(QIcon::fromTheme("list-remove"), "Remove from Queue");

  int play_pos = selected.first().row();
  connect(play, &QAction::triggered, this, [this, play_pos] { emit play_item(play_pos); });
  connect(remove, &QAction::triggered, this,
      [this, selected] { emit delete_items(selected); });

  if (playlists_available)
    {
      menu.addSeparator();
      auto *add = menu.addAction(QIcon::fromTheme("list-add"), "Add to playlist…");
      auto s = selected.first().data(QueueListModel::UserRoleSong).value<song>();
      connect(add, &QAction::triggered, this, [this, s] { emit add_to_playlist(s); });
    }

  menu.exec(event->globalPos());
}

auto
QueueListView::set_display_mode(QueueListItem::QueueDisplayMode mode) -> void
{
  list_item->set_display_mode(mode);
  scheduleDelayedItemsLayout();
  viewport()->update();
}

auto
QueueListView::refresh_layout() -> void
{
  scheduleDelayedItemsLayout();
  viewport()->update();
}

auto
QueueListView::set_color_band(bool enabled) -> void
{
  list_item->set_color_band(enabled);
  viewport()->update();
}

auto
QueueListView::set_queue_dark(bool dark) -> void
{
  auto pal = theme::queue_palette(dark);
  list_item->set_dark(dark);
  setPalette(pal);
  viewport()->setPalette(pal);
  scheduleDelayedItemsLayout();
  viewport()->update();
}

auto
QueueListView::in_group_header(const QPoint &pos) const -> bool
{
  auto idx = indexAt(pos);
  if (!idx.isValid())
    return false;

  int hh = list_item->header_height_at(idx);
  if (hh <= 0)
    return false;

  // The header band occupies the top `hh` pixels of a group-start row.
  return (pos.y() - visualRect(idx).top()) < hh;
}

auto
QueueListView::mousePressEvent(QMouseEvent *event) -> void
{
  if (in_group_header(event->pos()))
    return;

  if (event->button() == Qt::LeftButton)
    {
      auto idx = indexAt(event->pos());
      if (idx.isValid())
        {
          QStyleOptionViewItem opt;
          initViewItemOption(&opt);
          opt.rect = visualRect(idx);
          if (list_item->heart_rect(opt, idx).contains(event->pos()))
            {
              auto s = idx.data(QueueListModel::UserRoleSong).value<song>();
              emit favorite_toggled(s.uri);
              event->accept();
              return; // swallow — don't select or start a drag
            }
        }
    }

  press_pos = event->pos();
  QListView::mousePressEvent(event);
}

auto
QueueListView::startDrag(Qt::DropActions supportedActions) -> void
{
  auto indexes = selectedIndexes();
  indexes.erase(std::remove_if(indexes.begin(), indexes.end(),
                    [](const QModelIndex &idx) { return !(idx.flags() & Qt::ItemIsDragEnabled); }),
      indexes.end());
  if (indexes.isEmpty())
    return;

  auto *data = model()->mimeData(indexes);
  if (!data)
    return;

  // Build the drag pixmap ourselves: QAbstractItemView's default renders each
  // dragged row's full delegate rect, which would drag a group-start row's
  // album header band along with the first track of the group. Clip every
  // row to its content sub-rect (below any header band) so only the track
  // itself shows under the cursor.
  QRect bounds;
  for (const auto &idx : indexes)
    bounds |= visualRect(idx).adjusted(0, list_item->header_height_at(idx), 0, 0);

  QPixmap pixmap(bounds.size());
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  QStyleOptionViewItem option;
  initViewItemOption(&option);
  option.state |= QStyle::State_Selected;
  for (const auto &idx : indexes)
    {
      auto vr = visualRect(idx);
      auto content_vr = vr.adjusted(0, list_item->header_height_at(idx), 0, 0);
      painter.save();
      painter.setClipRect(content_vr.translated(-bounds.topLeft()));
      option.rect = vr.translated(-bounds.topLeft());
      itemDelegate()->paint(&painter, option, idx);
      painter.restore();
    }
  painter.end();

  auto *drag = new QDrag(this);
  drag->setPixmap(pixmap);
  drag->setMimeData(data);
  auto hotspot = press_pos - bounds.topLeft();
  hotspot.setY(std::max(0, hotspot.y()));
  drag->setHotSpot(hotspot);

  auto default_action = Qt::IgnoreAction;
  if (defaultDropAction() != Qt::IgnoreAction && (supportedActions & defaultDropAction()))
    default_action = defaultDropAction();
  else if ((supportedActions & Qt::CopyAction) && dragDropMode() != QAbstractItemView::InternalMove)
    default_action = Qt::CopyAction;

  drag->exec(supportedActions, default_action);
}

auto
QueueListView::mouseDoubleClickEvent(QMouseEvent *event) -> void
{
  if (in_group_header(event->pos()))
    return;
  QListView::mouseDoubleClickEvent(event);
}

auto
QueueListView::mouseMoveEvent(QMouseEvent *event) -> void
{
  // Suppress the row hover fill while the cursor is over a (non-interactive)
  // group-header band so the first track of the group doesn't light up.
  list_item->set_hover_in_header(in_group_header(event->pos()));
  viewport()->update();
  QListView::mouseMoveEvent(event);
}

auto
QueueListView::leaveEvent(QEvent *event) -> void
{
  list_item->set_hover_in_header(false);
  QListView::leaveEvent(event);
}

// auto
// QueueListView::dragMoveEvent(QDragMoveEvent *event) -> void
// {
//   QListView::dragMoveEvent(event);

//   //   event->accept();
// }
