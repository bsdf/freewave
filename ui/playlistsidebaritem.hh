#ifndef PLAYLISTSIDEBARITEM_HH
#define PLAYLISTSIDEBARITEM_HH

#include <QAbstractItemDelegate>
#include <QFont>

class AlbumArtManager;

// Sidebar row delegate for PlaylistSidebarModel: a 34px tile (2x2 cover mosaic
// for playlists, accent heart for the pinned auto row), the playlist name, and
// a mono meta line. Reads colors from option.palette only.
class PlaylistSidebarItem : public QAbstractItemDelegate {
  Q_OBJECT
public:
  explicit PlaylistSidebarItem(AlbumArtManager *art, QObject *parent = nullptr);

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
      const QModelIndex &index) const override;
  QSize sizeHint(const QStyleOptionViewItem &option,
      const QModelIndex &index) const override;

private:
  AlbumArtManager *art;
  QFont name_font;
  QFont meta_font;
};

#endif // PLAYLISTSIDEBARITEM_HH
