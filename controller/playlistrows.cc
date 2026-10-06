#include "playlistrows.hh"

namespace {

// Do songs [begin, end) strictly ascend in (disc, track) order? A strictly
// increasing run of exactly song_count entries covers the whole album in order.
auto
ascends_by_disc_track(const QList<song> &songs, int begin, int end) -> bool
{
  for (int j = begin + 1; j < end; ++j)
    {
      const auto &prev = songs[j - 1];
      const auto &cur = songs[j];
      bool asc = cur.disc_number > prev.disc_number
                 || (cur.disc_number == prev.disc_number
                     && cur.track_number > prev.track_number);
      if (!asc)
        return false;
    }
  return true;
}

} // namespace

auto
playlistrows::coalesce_playlist_rows(const QList<song> &songs,
    const std::function<playlistrows::album_ref(const QString &album_hash)> &resolve)
    -> QList<playlist_row>
{
  QList<playlist_row> rows;

  int i = 0;
  while (i < songs.size())
    {
      const QString hash = songs[i].album_hash;

      int run_end = i + 1;
      while (run_end < songs.size() && songs[run_end].album_hash == hash)
        ++run_end;
      int run_len = run_end - i;

      if (!hash.isEmpty())
        {
          const playlistrows::album_ref ref = resolve(hash);
          bool full = false;

          if (!ref.tracks.isEmpty())
            {
              // Cached tracks → strict uri-exact match against the full album.
              if (ref.tracks.size() == run_len)
                {
                  full = true;
                  for (int j = 0; j < run_len; ++j)
                    if (songs[i + j].uri != ref.tracks[j].uri)
                      {
                        full = false;
                        break;
                      }
                }
            }
          else if (ref.song_count > 0 && ref.song_count == run_len)
            {
              // No cached tracks → the album is full iff the run is exactly
              // song_count songs strictly ascending in (disc, track) order.
              full = ascends_by_disc_track(songs, i, run_end);
            }

          if (full)
            {
              rows.append({playlist_row::Kind::Album, i, run_len});
              i = run_end;
              continue;
            }
        }

      for (int j = i; j < run_end; ++j)
        rows.append({playlist_row::Kind::Track, j, 1});
      i = run_end;
    }

  return rows;
}
