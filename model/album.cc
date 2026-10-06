#include "album.hh"

album::album(
    QString uri, QString album_hash, QString name, QString artist, QString date,
    QString sort_artist, QDateTime last_modified)
  : name{name}
  , artist{artist}
  , sort_artist{sort_artist}
  , date{date}
  , last_modified{last_modified}
  , uri{uri}
  , album_hash{album_hash}
{
}
