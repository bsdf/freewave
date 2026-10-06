#include <gtest/gtest.h>

#include <mpd/client.h>

#include "mpdlib/song.hh"

// Build an mpd_song from a URI and optional tag pairs using the public
// mpd_song_begin / mpd_song_feed API so no MPD connection is required.
static auto
make_raw_song(const char *uri,
    std::initializer_list<std::pair<const char *, const char *>> pairs = {})
    -> mpd_song *
{
  mpd_pair file_pair{"file", uri};
  mpd_song *s = mpd_song_begin(&file_pair);
  if (!s) return nullptr;
  for (auto &[name, value] : pairs)
    {
      mpd_pair p{name, value};
      mpd_song_feed(s, &p);
    }
  return s;
}

// URI is stored by value in mpd::song — must outlive the raw pointer.
TEST(MpdSong, UriIsCopied)
{
  mpd_song *raw = make_raw_song("artist/album/track.flac");
  ASSERT_NE(raw, nullptr);

  mpd::song song{raw};
  mpd_song_free(raw);

  EXPECT_EQ(song.uri, "artist/album/track.flac");
}

// Tags are extracted into a std::map — must not reference the raw pointer.
TEST(MpdSong, TagsAreCopied)
{
  mpd_song *raw = make_raw_song("song.flac", {
                                                 {"Title", "My Song"},
                                                 {"Artist", "My Artist"},
                                                 {"Album", "My Album"},
                                             });
  ASSERT_NE(raw, nullptr);

  mpd::song song{raw};
  mpd_song_free(raw);

  EXPECT_EQ(song.tags.at(MPD_TAG_TITLE), "My Song");
  EXPECT_EQ(song.tags.at(MPD_TAG_ARTIST), "My Artist");
  EXPECT_EQ(song.tags.at(MPD_TAG_ALBUM), "My Album");
}

// Missing tags must not appear in the map — no garbage entries.
TEST(MpdSong, AbsentTagsNotPresent)
{
  mpd_song *raw = make_raw_song("song.flac", {{"Title", "Only Title"}});
  ASSERT_NE(raw, nullptr);

  mpd::song song{raw};
  mpd_song_free(raw);

  EXPECT_EQ(song.tags.count(MPD_TAG_TITLE), 1u);
  EXPECT_EQ(song.tags.count(MPD_TAG_ARTIST), 0u);
}

// Integer fields are copied by value and remain valid after mpd_song_free.
// This mirrors what get_current_song now does after the fix for issue #2.
TEST(MpdSong, IntegerFieldsValidAfterFree)
{
  mpd_song *raw = make_raw_song("song.flac");
  ASSERT_NE(raw, nullptr);

  mpd::song song{raw};
  mpd_song_free(raw);

  // Songs constructed without queue context have pos/id/prio = 0.
  EXPECT_EQ(song.duration, 0u);
  EXPECT_EQ(song.duration_ms, 0u);
  EXPECT_EQ(song.pos, 0u);
  EXPECT_EQ(song.id, 0u);
}

// Directly exercises the pattern introduced by the get_current_song fix:
// construct mpd::song, free the raw pointer, then access the song.
// Running under ASAN will catch any use-after-free or heap-use-after-free.
TEST(MpdSong, DataRemainsValidAfterRawFree)
{
  mpd_song *raw = make_raw_song("deep/path/to/song.flac", {
                                                              {"Title", "Post-Free Song"},
                                                              {"Artist", "Safe Artist"},
                                                          });
  ASSERT_NE(raw, nullptr);

  mpd::song song{raw};
  mpd_song_free(raw); // mirrors the fix: free immediately after wrapping

  EXPECT_EQ(song.uri, "deep/path/to/song.flac");
  EXPECT_EQ(song.tags.at(MPD_TAG_TITLE), "Post-Free Song");
  EXPECT_EQ(song.tags.at(MPD_TAG_ARTIST), "Safe Artist");
}
