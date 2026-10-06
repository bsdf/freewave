#ifndef STATS_H
#define STATS_H

#include "mpd.hh"

namespace mpd {
class stats {
public:
  uint num_artists;
  uint num_albums;
  uint num_songs;
  ulong uptime;
  ulong db_update_time;
  ulong play_time;
  ulong db_play_time;

  stats()
    : num_artists{}
    , num_albums{}
    , num_songs{}
    , uptime{}
    , db_update_time{}
    , play_time{}
    , db_play_time{}
  {
  }

  explicit stats(mpd_stats *s)
  {
    num_artists = mpd_stats_get_number_of_artists(s);
    num_albums = mpd_stats_get_number_of_albums(s);
    num_songs = mpd_stats_get_number_of_songs(s);
    uptime = mpd_stats_get_uptime(s);
    db_update_time = mpd_stats_get_db_update_time(s);
    play_time = mpd_stats_get_play_time(s);
    db_play_time = mpd_stats_get_db_play_time(s);
  }
};
}

#endif /* STATS_H */
