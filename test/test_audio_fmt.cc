#include <gtest/gtest.h>

#include <mpd/client.h>

#include "mpdlib/song.hh"
#include "mpdlib/status.hh"
#include "mpdlib/types.hh"

// Compile-time guarantee: audio_fmt is a value type, not a pointer.
static_assert(!std::is_pointer_v<mpd::audio_fmt>,
    "audio_fmt must not be a raw pointer");
static_assert(std::is_same_v<mpd::audio_fmt, std::optional<mpd_audio_format>>,
    "audio_fmt must be std::optional<mpd_audio_format>");

// ── mpd::song ────────────────────────────────────────────────────────────────

static auto
make_raw_song(const char *uri,
    std::initializer_list<std::pair<const char *, const char *>> pairs = {})
    -> mpd_song *
{
  mpd_pair fp{"file", uri};
  mpd_song *s = mpd_song_begin(&fp);
  if (!s) return nullptr;
  for (auto &[name, value] : pairs)
    {
      mpd_pair p{name, value};
      mpd_song_feed(s, &p);
    }
  return s;
}

// Songs with no format pair must produce nullopt — not a dangling pointer.
TEST(AudioFmt, SongFormatNulloptWhenAbsent)
{
  mpd_song *raw = make_raw_song("song.flac");
  ASSERT_NE(raw, nullptr);

  mpd::song song{raw};
  mpd_song_free(raw);

  EXPECT_FALSE(song.format.has_value());
}

// When a Format pair is present the fields must be copied by value.
TEST(AudioFmt, SongFormatCopiedByValue)
{
  mpd_song *raw = make_raw_song("song.flac", {{"Format", "44100:16:2"}});
  ASSERT_NE(raw, nullptr);

  mpd::song song{raw};
  mpd_song_free(raw); // original freed — value copy must survive

  ASSERT_TRUE(song.format.has_value());
  EXPECT_EQ(song.format->sample_rate, 44100u);
  EXPECT_EQ(song.format->bits, 16u);
  EXPECT_EQ(song.format->channels, 2u);
}

// Accessing format fields after free must not corrupt (verified by ASAN).
TEST(AudioFmt, SongFormatValidAfterRawFree)
{
  mpd_song *raw = make_raw_song("song.flac", {{"Format", "48000:24:2"}});
  ASSERT_NE(raw, nullptr);

  mpd::song song{raw};
  mpd_song_free(raw);

  ASSERT_TRUE(song.format.has_value());
  EXPECT_EQ(song.format->sample_rate, 48000u);
  EXPECT_EQ(song.format->bits, 24u);
  EXPECT_EQ(song.format->channels, 2u);
}

// ── mpd::status ──────────────────────────────────────────────────────────────

// The default-constructed status (used as a fallback when mpd_run_status
// returns null) must have audio_format as nullopt, not an uninitialised ptr.
TEST(AudioFmt, StatusDefaultAudioFormatIsNullopt)
{
  mpd::status s;
  EXPECT_FALSE(s.audio_format.has_value());
}
