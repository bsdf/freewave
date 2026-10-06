#ifndef PLAYLISTSMANAGER_HH
#define PLAYLISTSMANAGER_HH

#include <memory>
#include <optional>

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include "controller/backend.hh"
#include "model/playlist.hh"
#include "model/song.hh"

// In-memory cache of server-native stored playlists — the UI's single source
// of truth for the playlists sidebar/detail view. Parallels FavoritesManager:
// the backend (the BackendController façade) remains the persistent source of
// truth; this reconciles from its playlists_loaded / playlist_songs_loaded /
// playlists_changed signals and pushes mutations down.
class PlaylistsManager : public QObject {
  Q_OBJECT
public:
  explicit PlaylistsManager(std::shared_ptr<Backend> backend,
      QObject *parent = nullptr);

  auto available() const -> bool;                 // backend supports playlists
  auto playlists() const -> QList<playlist_info>; // server order
  auto info(const QString &id) const -> std::optional<playlist_info>;
  auto songs(const QString &id) const -> std::optional<QList<song>>; // cached only
  void ensure_songs(const QString &id);                              // fetch if not cached

  // First few distinct album hashes of a playlist's loaded songs, feeding the
  // sidebar's cover mosaic. Empty until the contents are fetched — the rendered
  // mosaic itself (and its persistence across restarts) is AlbumArtManager's job.
  auto mosaic_hashes(const QString &id) const -> QStringList;

public slots:
  void reload();
  void create(const QString &name, const QList<song> &initial);
  void rename(const QString &id, const QString &new_name);
  void remove(const QString &id);
  void add_songs(const QString &id, const QList<song> &songs);
  void remove_at(const QString &id, QList<int> positions);
  // Reorder: move the songs at flat indexes `moved` to flat position `target`
  // (insertion point in the pre-move index space, as reorder::get_output_order
  // defines it). Optimistically reorders the cached song list, then forwards.
  void rearrange(const QString &id, int target, QList<int> moved);

signals:
  void playlists_reset();                   // list changed — rebuild sidebar
  void playlist_updated(const QString &id); // one playlist's songs arrived/changed

private:
  void update_summary(const QString &id);

  QList<playlist_info> lists;
  QMap<QString, QList<song>> song_cache;
  std::shared_ptr<Backend> backend;
  // A mutation was pushed to the backend and not yet reconciled by
  // playlists_changed. If the backend then reports an error, the optimistic
  // song_cache edit may not reflect what actually persisted, so we drop it
  // and re-fetch authoritative state.
  bool pending = false;
};

#endif // PLAYLISTSMANAGER_HH
