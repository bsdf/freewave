#ifndef PLAYLIST_HH
#define PLAYLIST_HH

#include <QDateTime>
#include <QMetaType>
#include <QString>

// A server-side playlist's identity + summary. `id` is backend-native:
// the playlist *name* for MPD (names are the id), the playlist id for Subsonic.
struct playlist_info {
  QString id;
  QString name;
  int song_count = -1;     // -1 = unknown (MPD before contents fetched)
  qint64 duration_ms = -1; // -1 = unknown
  QDateTime last_modified; // invalid if server didn't say

  // Version tag for caches keyed to this playlist (e.g. the cover mosaic): the
  // last-modified epoch, or the song count when the server gives no timestamp
  // (so add/remove still busts the cache; reorder-only edits rarely change the
  // representative covers).
  auto cache_stamp() const -> QString
  {
    return last_modified.isValid()
               ? QString::number(last_modified.toMSecsSinceEpoch())
               : QStringLiteral("n%1").arg(song_count);
  }
};

Q_DECLARE_METATYPE(playlist_info);

#endif // PLAYLIST_HH
