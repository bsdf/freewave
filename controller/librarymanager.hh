#ifndef LIBRARYMANAGER_HH
#define LIBRARYMANAGER_HH

#include <QHash>
#include <QMap>

#include "model/album.hh"
#include "model/song.hh"

// Plain in-memory cache of albums and songs keyed by album hash. Held by
// shared_ptr; uses no QObject facilities (library_changed lives on Backend).
class LibraryManager {
public:
  LibraryManager() = default;

  auto clear() -> void
  {
    album_map.clear();
    song_map.clear();
    hash_by_native.clear();
  }

  auto add_album(const QString &hash, album a) -> void
  {
    if (!a.native_id.isEmpty())
      hash_by_native.insert(a.native_id, hash);
    album_map[hash] = std::move(a);
  }

  auto get_album(const QString &hash) const -> album
  {
    return album_map.value(hash);
  }

  auto has_album(const QString &hash) const -> bool
  {
    return album_map.contains(hash);
  }

  auto get_albums() const -> QList<album>
  {
    return album_map.values();
  }

  auto get_songs(const QString &hash) const -> QList<song>
  {
    return song_map.value(hash);
  }

  auto set_songs(const QString &hash, QList<song> songs) -> void
  {
    song_map[hash] = songs;
  }

  auto add_song(const QString &hash, song s) -> void
  {
    song_map[hash] << s;
  }

  // Backend-native album id → album_hash, maintained alongside the album set.
  // A standalone song (favorite, playlist entry) knows only its native album
  // id, while the library keys albums by release MBID where one is tagged, so
  // this index is what connects the two. Empty result means "no album under
  // that id" — the caller decides the fallback, which is backend-specific.
  auto album_hash_for_native(const QString &native_id) const -> QString
  {
    return hash_by_native.value(native_id);
  }

  // The whole index, for callers that need to detect it moving (a library
  // reload that resolves every id to the same hash leaves resolved data valid).
  auto native_index() const -> QHash<QString, QString>
  {
    return hash_by_native;
  }

private:
  QMap<QString, album> album_map;
  QMap<QString, QList<song>> song_map;
  QHash<QString, QString> hash_by_native;
};

#endif /* LIBRARYMANAGER_HH */
