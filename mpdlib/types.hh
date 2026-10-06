#ifndef TYPES_H
#define TYPES_H

#include <ctime>
#include <optional>
#include <string>

#include <mpd/client.h>

namespace mpd {
struct error {
  mpd_error code;
  std::string msg;
  bool fatal;
};

// Copies data out of libmpdclient's mpd_playlist so it can be used after the
// C struct is freed. Contents come separately (fetch_playlist_songs).
struct playlist_summary {
  std::string name;
  std::time_t last_modified;

  explicit playlist_summary(mpd_playlist *p)
    : name{mpd_playlist_get_path(p)}
    , last_modified{mpd_playlist_get_last_modified(p)}
  {
  }

  playlist_summary(std::string name, std::time_t last_modified)
    : name{std::move(name)}
    , last_modified{last_modified}
  {
  }
};

using state_t = mpd_state;
using single_state_t = mpd_single_state;
using audio_fmt = std::optional<mpd_audio_format>;
using tag_type = mpd_tag_type;
}

#endif /* TYPES_H */
