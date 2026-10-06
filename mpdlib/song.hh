#ifndef SONG_HH
#define SONG_HH

#include <map>

#include "mpd.hh"

namespace mpd {
using tags_t = std::map<tag_type, std::string>;

class song {
private:
  auto extract_tags(mpd_song *) -> tags_t;

public:
  std::string uri;
  uint duration;
  uint duration_ms;
  uint start;
  uint end;
  std::time_t last_modified;
  std::time_t added; // MPD 0.24+ "Added" tag; 0 if the server predates it
  uint pos;
  uint id;
  uint prio;
  audio_fmt format;
  tags_t tags;

  explicit song(mpd_song *s)
  {
    uri = mpd_song_get_uri(s);
    duration = mpd_song_get_duration(s);
    duration_ms = mpd_song_get_duration_ms(s);
    start = mpd_song_get_start(s);
    end = mpd_song_get_end(s);
    last_modified = mpd_song_get_last_modified(s);
    added = mpd_song_get_added(s);
    pos = mpd_song_get_pos(s);
    id = mpd_song_get_id(s);
    prio = mpd_song_get_prio(s);
    if (auto *f = mpd_song_get_audio_format(s)) format = *f;
    tags = extract_tags(s);
  }
};
}
#endif // SONG_HH
