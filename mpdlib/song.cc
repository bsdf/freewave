#include "song.hh"

using namespace mpd;

auto
song::extract_tags(mpd_song *s) -> tags_t
{
  tags_t tt;
  for (int i = tag_type::MPD_TAG_ARTIST; i != tag_type::MPD_TAG_COUNT; i++)
    {
      auto t = static_cast<tag_type>(i);
      auto v = mpd_song_get_tag(s, t, 0);

      if (v != nullptr)
        tt[t] = v;
    }
  return tt;
}
