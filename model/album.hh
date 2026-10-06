#ifndef MODELS_ALBUM_HH
#define MODELS_ALBUM_HH

#include <QString>
#include <QDataStream>
#include <QDateTime>
#include <QMetaType>

class album {
public:
  album() = default;
  ~album() = default;
  album(const album &) = default;
  album &operator=(const album &) = default;

  // album() {};
  album(
      QString uri, QString album_hash, QString name, QString artist, QString date,
      QString sort_artist, QDateTime last_modified);

  QString name;
  QString sort_name; // album_sort || album || unknown
  QString artist;
  QString sort_artist; // album_artist_sort || artist_sort || album_artist || artist || unknown
  QString date;
  QDateTime last_modified; // "date added" surrogate: MPD Added tag (falls back to
                           // Last-Modified pre-0.24) or Subsonic's "created"

  QString filename;
  QString uri;
  QString album_hash;

  // Backend-native id for server requests (e.g. Subsonic getAlbum). Distinct
  // from album_hash, which is an identity key (MBID or profile-scoped
  // fallback) shared across servers and used for caching. Unused by MPD,
  // which never needs to hand its hash back to the server.
  QString native_id;

  int song_count = 0;   // from getAlbumList2; drives skeleton rows on a cache miss
  bool is_live = false; // MusicBrainz release type == live; drives the bootleg stamp
};

Q_DECLARE_METATYPE(album);

inline QDataStream &
operator<<(QDataStream &out, const album &a)
{
  return out << a.uri << a.album_hash << a.name << a.sort_name << a.artist
             << a.sort_artist << a.date << a.last_modified << a.filename
             << a.native_id;
}

inline QDataStream &
operator>>(QDataStream &in, album &a)
{
  return in >> a.uri >> a.album_hash >> a.name >> a.sort_name >> a.artist
         >> a.sort_artist >> a.date >> a.last_modified >> a.filename
         >> a.native_id;
}

#endif // MODELS_ALBUM_HH
