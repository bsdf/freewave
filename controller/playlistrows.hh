#ifndef PLAYLISTROWS_HH
#define PLAYLISTROWS_HH

#include <functional>

#include <QList>
#include <QString>

#include "model/song.hh"

namespace playlistrows {

struct playlist_row {
  enum class Kind { Track,
    Album };
  Kind kind;
  int first; // index into the flat song list
  int count; // 1 for Track; span length for Album
};

// What the library knows about an album_hash, for coalescing.
struct album_ref {
  QList<song> tracks; // full ordered track list, empty if songs aren't cached
  int song_count = 0; // album's total track count from the library list, 0 if unknown
};

// Collapse maximal consecutive runs that reproduce a full album into Album rows.
// A run of songs sharing album_hash becomes an Album row iff either:
//   - the album's tracks are cached and the run's uris equal the album's full
//     ordered track uris (strict uri-exact — MPD always, opened Subsonic albums); or
//   - the tracks aren't cached but the album's song_count is known, the run has
//     exactly that many songs, and they strictly ascend in (disc, track) order.
// The second branch lets a full album coalesce from just the library summary,
// so Subsonic playlists don't have to fetch every album's tracks. Partial runs,
// wrong/duplicate order, or unknown albums (no tracks and no count) stay Track rows.
auto coalesce_playlist_rows(const QList<song> &songs,
    const std::function<album_ref(const QString &album_hash)> &resolve)
    -> QList<playlist_row>;

}

#endif // PLAYLISTROWS_HH
