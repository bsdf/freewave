#include "songlistview.hh"
#include "songlistitem.hh"
#include "songlistmodel.hh"
#include "model/song.hh"

#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionViewItem>

#include <algorithm>

SongListView::SongListView(QWidget *parent)
  : QListView(parent)
{
  setMouseTracking(true);
}

auto
SongListView::mousePressEvent(QMouseEvent *event) -> void
{
  if (event->button() == Qt::LeftButton)
    {
      auto idx = indexAt(event->pos());
      auto *delegate = qobject_cast<SongListItem *>(itemDelegate());
      if (idx.isValid() && delegate)
        {
          QStyleOptionViewItem opt;
          initViewItemOption(&opt);
          opt.rect = visualRect(idx);
          if (delegate->add_rect(opt, idx).contains(event->pos()))
            {
              auto s = idx.data(SongListModel::UserRoleSong).value<song>();
              emit add_to_playlist_requested(s);
              event->accept();
              return; // swallow — don't select or activate the row
            }
          if (delegate->heart_rect(opt, idx).contains(event->pos()))
            {
              auto s = idx.data(SongListModel::UserRoleSong).value<song>();
              emit favorite_toggled(s.uri);
              event->accept();
              return; // swallow — don't select or activate the row
            }
        }
    }
  press_pos = event->pos();
  QListView::mousePressEvent(event);
}

auto
SongListView::startDrag(Qt::DropActions supportedActions) -> void
{
  auto *delegate = qobject_cast<SongListItem *>(itemDelegate());

  auto indexes = selectedIndexes();
  indexes.erase(std::remove_if(indexes.begin(), indexes.end(),
                    [](const QModelIndex &idx) { return !(idx.flags() & Qt::ItemIsDragEnabled); }),
      indexes.end());
  if (indexes.isEmpty())
    return;

  auto *data = model()->mimeData(indexes);
  if (!data)
    return;

  auto header_h = [delegate](const QModelIndex &idx) {
    return delegate ? delegate->disc_header_height_at(idx) : 0;
  };

  // Build the drag pixmap ourselves: QAbstractItemView's default renders each
  // dragged row's full delegate rect, which would drag a disc separator's
  // header band along with the first track of a disc. Clip every row to its
  // content sub-rect (below any header band) so only the track itself shows
  // under the cursor.
  QRect bounds;
  for (const auto &idx : indexes)
    bounds |= visualRect(idx).adjusted(0, header_h(idx), 0, 0);

  QPixmap pixmap(bounds.size());
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  QStyleOptionViewItem option;
  initViewItemOption(&option);
  option.state |= QStyle::State_Selected;
  for (const auto &idx : indexes)
    {
      auto vr = visualRect(idx);
      auto content_vr = vr.adjusted(0, header_h(idx), 0, 0);
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
