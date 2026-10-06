#ifndef PLAYLISTITEMDELEGATE_HH
#define PLAYLISTITEMDELEGATE_HH

#include <QAbstractItemDelegate>
#include <QFont>

#include "ui/favoriteheart.hh"

class AlbumArtManager;
class QAbstractItemModel;

// Detail-pane row delegate for PlaylistItemModel. Paints Track rows (index,
// cover thumb, title, artist～album, favorite heart, duration) and coalesced
// Album rows (ALBUM chip, album title, artist～N tracks, summed duration).
// Reads colors from option.palette only.
class PlaylistItemDelegate : public QAbstractItemDelegate {
  Q_OBJECT
public:
  explicit PlaylistItemDelegate(AlbumArtManager *art, QObject *parent = nullptr);

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
      const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option,
      const QModelIndex &index) const override;

  // Hit rect for the Track-row favorite heart (empty for Album rows or when
  // favorites are unavailable), so the view can turn a click into a toggle.
  auto heart_rect(const QStyleOptionViewItem &option, const QModelIndex &index) const -> QRect;

  // Play the "pop" bump for a track that just became favorited.
  void pop(const QString &uri);

signals:
  void needs_repaint();

private:
  // Width reserved for the duration column: the widest duration string in the
  // whole list, so a row that reaches the hours segment (h:mm:ss) doesn't shove
  // its own duration + heart left of the shorter rows'. Cached; recomputed when
  // the model or its row count changes.
  auto time_col_width(const QAbstractItemModel *model) const -> int;

  AlbumArtManager *art;
  mutable HeartPopAnimator hearts;
  QFont title_font;
  QFont sub_font;
  QFont mono_font;
  QFont chip_font;

  mutable const QAbstractItemModel *cached_model = nullptr;
  mutable int cached_rows = -1;
  mutable int cached_time_w = 0;
};

#endif // PLAYLISTITEMDELEGATE_HH
