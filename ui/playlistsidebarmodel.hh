#ifndef PLAYLISTSIDEBARMODEL_HH
#define PLAYLISTSIDEBARMODEL_HH

#include <functional>

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>

#include "model/playlist.hh"

// Sidebar cover-tile edge in logical px — shared by the delegate (paints it) and
// the view (checks the mosaic cache at this size to decide whether to prefetch).
inline constexpr int PLAYLIST_SIDEBAR_TILE = 34;

// The playlists sidebar list. Row 0 is always the pinned auto "Favorited
// Tracks" entry; rows 1.. mirror PlaylistsManager::playlists() in server order.
class PlaylistSidebarModel : public QAbstractListModel {
  Q_OBJECT
public:
  explicit PlaylistSidebarModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

  void set_playlists(const QList<playlist_info> &lists);
  void set_favorites_count(int n);
  // Resolve a playlist id to up to four distinct album hashes for its mosaic
  // tile. Called lazily from data(); the view repaints when songs/art arrive.
  void set_mosaic_resolver(std::function<QStringList(const QString &id)> resolver);

  auto is_auto_row(int row) const -> bool { return row == 0; }
  // Playlist id for a data row; empty for the auto row (row 0) or out of range.
  auto playlist_id(int row) const -> QString;
  // Model row for a playlist id, or -1; used to keep selection stable by id.
  auto row_for_id(const QString &id) const -> int;

  enum UserRoles {
    UserRoleIsAuto = Qt::UserRole,
    UserRolePlaylistId,
    UserRoleName,
    UserRoleMeta,         // "N items ～ updated X" or the auto row's subtitle
    UserRoleMosaicHashes, // QStringList (empty on the auto row -> heart tile)
    UserRoleMosaicStamp,  // playlist version tag for the mosaic cache key
  };

private:
  QList<playlist_info> lists;
  int fav_count = 0;
  std::function<QStringList(const QString &id)> mosaic;
};

#endif // PLAYLISTSIDEBARMODEL_HH
