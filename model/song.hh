#ifndef MODELS_SONG_HH
#define MODELS_SONG_HH

#include <QString>
#include <QDataStream>
#include <QMetaType>

class song {
public:
  song() = default;
  ~song() = default;
  song(const song &) = default;
  song &operator=(const song &) = default;

  song(QString uri, QString title, QString artist, uint32_t track_number, uint32_t disc_number, uint32_t duration, QString album_hash)
    : uri{uri}
    , title{title}
    , artist{artist}
    , track_number{track_number}
    , disc_number{disc_number}
    , duration{duration}
    , album_hash{album_hash} {};

  QString uri;
  QString title;
  QString artist;
  uint32_t track_number;
  uint32_t disc_number;
  uint32_t duration;
  QString album_hash;

  // Backend-native id of the song's album, kept so album_hash can be resolved
  // again later. A standalone song (a favorite, a playlist entry) arrives with
  // only this id, and the library index that turns it into the album's identity
  // key may not be populated yet — keeping it makes that a re-resolvable local
  // operation instead of a reason to re-request the song. Empty for MPD, whose
  // hash comes from the song's own tags and is never provisional.
  QString native_album_id;
};

Q_DECLARE_METATYPE(song);

inline QDataStream &
operator<<(QDataStream &out, const song &s)
{
  return out << s.uri << s.title << s.artist << s.track_number << s.disc_number << s.duration << s.album_hash << s.native_album_id;
}

inline QDataStream &
operator>>(QDataStream &in, song &s)
{
  return in >> s.uri >> s.title >> s.artist >> s.track_number >> s.disc_number >> s.duration >> s.album_hash >> s.native_album_id;
}

#endif /* MODELS_SONG_HH */
