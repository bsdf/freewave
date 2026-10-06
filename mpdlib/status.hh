#ifndef STATUS_H
#define STATUS_H

#include "mpd.hh"

namespace mpd {
class status {
public:
  int volume;
  bool repeat;
  bool random;
  bool single;
  bool consume;
  uint queue_length;
  uint queue_version;
  state_t state;
  single_state_t single_state;
  uint crossfade;
  float mixrampdb;
  float mixrampdelay;
  int song_pos;
  int song_id;
  int next_song_pos;
  int next_song_id;
  uint elapsed_time;
  uint elapsed_ms;
  uint total_time;
  uint kbit_rate;
  audio_fmt audio_format;
  uint update_id;
  std::string partition;
  std::string error;

  status() {}
  explicit status(mpd_status *s)
  {
    volume = mpd_status_get_volume(s);
    repeat = mpd_status_get_repeat(s);
    random = mpd_status_get_random(s);
    single = mpd_status_get_single(s);
    consume = mpd_status_get_consume(s);
    queue_length = mpd_status_get_queue_length(s);
    queue_version = mpd_status_get_queue_version(s);
    state = mpd_status_get_state(s);
    single_state = mpd_status_get_single_state(s);
    crossfade = mpd_status_get_crossfade(s);
    mixrampdb = mpd_status_get_mixrampdb(s);
    mixrampdelay = mpd_status_get_mixrampdelay(s);
    song_pos = mpd_status_get_song_pos(s);
    song_id = mpd_status_get_song_id(s);
    next_song_pos = mpd_status_get_next_song_pos(s);
    next_song_id = mpd_status_get_next_song_id(s);
    elapsed_time = mpd_status_get_elapsed_time(s);
    elapsed_ms = mpd_status_get_elapsed_ms(s);
    total_time = mpd_status_get_total_time(s);
    kbit_rate = mpd_status_get_kbit_rate(s);
    if (auto *f = mpd_status_get_audio_format(s)) audio_format = *f;
    update_id = mpd_status_get_update_id(s);
    partition = mpd_status_get_partition(s);
    // error         = mpd_status_get_error(s);
  }
};
}

#endif // STATUS_H
