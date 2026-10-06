#include <gtest/gtest.h>

#include <mpd/client.h>

#include "mpdlib/stats.hh"

static auto
make_raw_stats(
    std::initializer_list<std::pair<const char *, const char *>> pairs = {})
    -> mpd_stats *
{
  mpd_stats *s = mpd_stats_begin();
  if (!s) return nullptr;
  for (auto &[name, value] : pairs)
    {
      mpd_pair p{name, value};
      mpd_stats_feed(s, &p);
    }
  return s;
}

// Default constructor must produce zero-valued fields — used as the fallback
// when mpd_run_stats returns null.
TEST(MpdStats, DefaultConstructorZeroInitialised)
{
  mpd::stats s;
  EXPECT_EQ(s.num_artists, 0u);
  EXPECT_EQ(s.num_albums, 0u);
  EXPECT_EQ(s.num_songs, 0u);
  EXPECT_EQ(s.uptime, 0ul);
  EXPECT_EQ(s.db_update_time, 0ul);
  EXPECT_EQ(s.play_time, 0ul);
  EXPECT_EQ(s.db_play_time, 0ul);
}

// All fields are copied correctly from an mpd_stats object.
TEST(MpdStats, FieldsCopiedFromRaw)
{
  mpd_stats *raw = make_raw_stats({
      {"artists", "42"},
      {"albums", "10"},
      {"songs", "200"},
      {"uptime", "3600"},
      {"db_update", "1700000000"},
      {"playtime", "7200"},
      {"db_playtime", "99999"},
  });
  ASSERT_NE(raw, nullptr);

  mpd::stats s{raw};
  mpd_stats_free(raw);

  EXPECT_EQ(s.num_artists, 42u);
  EXPECT_EQ(s.num_albums, 10u);
  EXPECT_EQ(s.num_songs, 200u);
  EXPECT_EQ(s.uptime, 3600ul);
  EXPECT_EQ(s.db_update_time, 1700000000ul);
  EXPECT_EQ(s.play_time, 7200ul);
  EXPECT_EQ(s.db_play_time, 99999ul);
}

// Fields remain valid after the raw pointer is freed (ASAN-verified).
TEST(MpdStats, FieldsValidAfterRawFree)
{
  mpd_stats *raw = make_raw_stats({{"artists", "7"}, {"songs", "333"}});
  ASSERT_NE(raw, nullptr);

  mpd::stats s{raw};
  mpd_stats_free(raw);

  EXPECT_EQ(s.num_artists, 7u);
  EXPECT_EQ(s.num_songs, 333u);
}
