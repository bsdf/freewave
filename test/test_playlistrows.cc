// Correctness tests for playlistrows::coalesce_playlist_rows.
//
// Testing strategy
// ----------------
// Two coalescing paths, one per resolver branch:
//  - uri-exact (cached tracks): `resolver` returns each album's full track list;
//    a run coalesces iff its uris match in order.
//  - count-based (no cached tracks, only song_count from the library summary):
//    `count_resolver` returns just a count; a run coalesces iff it has exactly
//    that many songs strictly ascending in (disc, track).
// Assert the exact row sequence for full/partial/wrong-order/duplicate/unknown.

#include <gtest/gtest.h>

#include <QMap>

#include "controller/playlistrows.hh"

using playlistrows::album_ref;
using playlistrows::coalesce_playlist_rows;
using playlistrows::playlist_row;

namespace {

auto
make_song(const QString &uri, const QString &hash) -> song
{
  return song{uri, uri, "artist", 1, 1, 200000, hash};
}

// track/disc-aware variant for the count-based (no cached tracks) path.
auto
make_track(const QString &uri, const QString &hash, uint32_t track, uint32_t disc = 1) -> song
{
  return song{uri, uri, "artist", track, disc, 200000, hash};
}

// Cached-tracks resolver → exercises the strict uri-exact branch.
auto
resolver(const QMap<QString, QList<song>> &albums)
{
  return [albums](const QString &hash) -> album_ref {
    auto t = albums.value(hash);
    return {t, int(t.size())};
  };
}

// Count-only resolver (no cached tracks) → exercises the count-ascending branch.
auto
count_resolver(const QMap<QString, int> &counts)
{
  return [counts](const QString &hash) -> album_ref {
    return {{}, counts.value(hash, 0)};
  };
}

} // namespace

TEST(PlaylistRows, EmptyInput_NoRows)
{
  QMap<QString, QList<song>> albums;
  EXPECT_TRUE(coalesce_playlist_rows({}, resolver(albums)).isEmpty());
}

TEST(PlaylistRows, FullAlbumInOrder_CollapsesToOneRow)
{
  QList<song> tracks{make_song("a1", "h1"), make_song("a2", "h1"), make_song("a3", "h1")};
  QMap<QString, QList<song>> albums{{"h1", tracks}};

  auto rows = coalesce_playlist_rows(tracks, resolver(albums));

  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(rows[0].kind, playlist_row::Kind::Album);
  EXPECT_EQ(rows[0].first, 0);
  EXPECT_EQ(rows[0].count, 3);
}

TEST(PlaylistRows, PartialAlbum_StaysTrackRows)
{
  QList<song> full{make_song("a1", "h1"), make_song("a2", "h1"), make_song("a3", "h1")};
  QList<song> playlist{make_song("a1", "h1"), make_song("a2", "h1")}; // missing a3
  QMap<QString, QList<song>> albums{{"h1", full}};

  auto rows = coalesce_playlist_rows(playlist, resolver(albums));

  ASSERT_EQ(rows.size(), 2);
  EXPECT_EQ(rows[0].kind, playlist_row::Kind::Track);
  EXPECT_EQ(rows[0].first, 0);
  EXPECT_EQ(rows[1].kind, playlist_row::Kind::Track);
  EXPECT_EQ(rows[1].first, 1);
}

TEST(PlaylistRows, WrongOrder_StaysTrackRows)
{
  QList<song> full{make_song("a1", "h1"), make_song("a2", "h1"), make_song("a3", "h1")};
  QList<song> playlist{make_song("a2", "h1"), make_song("a1", "h1"), make_song("a3", "h1")};
  QMap<QString, QList<song>> albums{{"h1", full}};

  auto rows = coalesce_playlist_rows(playlist, resolver(albums));

  ASSERT_EQ(rows.size(), 3);
  for (const auto &row : rows)
    EXPECT_EQ(row.kind, playlist_row::Kind::Track);
}

TEST(PlaylistRows, DuplicatedTracks_StaysTrackRows)
{
  QList<song> full{make_song("a1", "h1"), make_song("a2", "h1")};
  QList<song> playlist{make_song("a1", "h1"), make_song("a2", "h1"), make_song("a1", "h1")};
  QMap<QString, QList<song>> albums{{"h1", full}};

  auto rows = coalesce_playlist_rows(playlist, resolver(albums));

  ASSERT_EQ(rows.size(), 3);
  for (const auto &row : rows)
    EXPECT_EQ(row.kind, playlist_row::Kind::Track);
}

TEST(PlaylistRows, UnknownAlbumHash_StaysTrackRows)
{
  QList<song> playlist{make_song("a1", "unknown"), make_song("a2", "unknown")};
  QMap<QString, QList<song>> albums; // resolver returns {} for "unknown"

  auto rows = coalesce_playlist_rows(playlist, resolver(albums));

  ASSERT_EQ(rows.size(), 2);
  for (const auto &row : rows)
    EXPECT_EQ(row.kind, playlist_row::Kind::Track);
}

TEST(PlaylistRows, EmptyHash_NeverCoalesces)
{
  QList<song> playlist{make_song("a1", ""), make_song("a2", "")};
  QMap<QString, QList<song>> albums{{"", playlist}}; // even if resolver would match

  auto rows = coalesce_playlist_rows(playlist, resolver(albums));

  ASSERT_EQ(rows.size(), 2);
  for (const auto &row : rows)
    EXPECT_EQ(row.kind, playlist_row::Kind::Track);
}

TEST(PlaylistRows, MixedTrackAndAlbumRuns)
{
  QList<song> album_tracks{make_song("b1", "h2"), make_song("b2", "h2")};
  QMap<QString, QList<song>> albums{{"h2", album_tracks}};

  QList<song> playlist{
      make_song("t1", "loose"),
      make_song("b1", "h2"),
      make_song("b2", "h2"),
      make_song("t2", "loose2"),
  };

  auto rows = coalesce_playlist_rows(playlist, resolver(albums));

  ASSERT_EQ(rows.size(), 3);
  EXPECT_EQ(rows[0].kind, playlist_row::Kind::Track);
  EXPECT_EQ(rows[0].first, 0);

  EXPECT_EQ(rows[1].kind, playlist_row::Kind::Album);
  EXPECT_EQ(rows[1].first, 1);
  EXPECT_EQ(rows[1].count, 2);

  EXPECT_EQ(rows[2].kind, playlist_row::Kind::Track);
  EXPECT_EQ(rows[2].first, 3);
}

TEST(PlaylistRows, TwoConsecutiveFullAlbums_TwoRows)
{
  QList<song> tracks1{make_song("a1", "h1"), make_song("a2", "h1")};
  QList<song> tracks2{make_song("b1", "h2"), make_song("b2", "h2")};
  QMap<QString, QList<song>> albums{{"h1", tracks1}, {"h2", tracks2}};

  QList<song> playlist = tracks1 + tracks2;

  auto rows = coalesce_playlist_rows(playlist, resolver(albums));

  ASSERT_EQ(rows.size(), 2);
  EXPECT_EQ(rows[0].kind, playlist_row::Kind::Album);
  EXPECT_EQ(rows[0].first, 0);
  EXPECT_EQ(rows[0].count, 2);
  EXPECT_EQ(rows[1].kind, playlist_row::Kind::Album);
  EXPECT_EQ(rows[1].first, 2);
  EXPECT_EQ(rows[1].count, 2);
}

// ── Count-based branch (no cached tracks, only song_count) ──────────────────

TEST(PlaylistRows, CountBased_FullAlbumAscending_Collapses)
{
  QList<song> pl{
      make_track("a1", "h1", 1), make_track("a2", "h1", 2), make_track("a3", "h1", 3)};
  QMap<QString, int> counts{{"h1", 3}};

  auto rows = coalesce_playlist_rows(pl, count_resolver(counts));

  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(rows[0].kind, playlist_row::Kind::Album);
  EXPECT_EQ(rows[0].count, 3);
}

TEST(PlaylistRows, CountBased_MultiDiscAscending_Collapses)
{
  // disc 1 tracks 1-2, disc 2 tracks 1-2 — track# resets per disc, so this only
  // works because we compare (disc, track) pairs, not track# alone.
  QList<song> pl{
      make_track("a1", "h1", 1, 1), make_track("a2", "h1", 2, 1),
      make_track("a3", "h1", 1, 2), make_track("a4", "h1", 2, 2)};
  QMap<QString, int> counts{{"h1", 4}};

  auto rows = coalesce_playlist_rows(pl, count_resolver(counts));

  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(rows[0].kind, playlist_row::Kind::Album);
  EXPECT_EQ(rows[0].count, 4);
}

TEST(PlaylistRows, CountBased_ShuffledButCompleteCount_StaysTrackRows)
{
  // All 3 tracks present, count matches, but not in ascending order → the row
  // would misrepresent itself as "the album" while playing shuffled, so keep
  // it as tracks.
  QList<song> pl{
      make_track("a2", "h1", 2), make_track("a1", "h1", 1), make_track("a3", "h1", 3)};
  QMap<QString, int> counts{{"h1", 3}};

  auto rows = coalesce_playlist_rows(pl, count_resolver(counts));

  ASSERT_EQ(rows.size(), 3);
  for (const auto &row : rows)
    EXPECT_EQ(row.kind, playlist_row::Kind::Track);
}

TEST(PlaylistRows, CountBased_PartialRun_StaysTrackRows)
{
  QList<song> pl{make_track("a1", "h1", 1), make_track("a2", "h1", 2)}; // 2 of 3
  QMap<QString, int> counts{{"h1", 3}};

  auto rows = coalesce_playlist_rows(pl, count_resolver(counts));

  ASSERT_EQ(rows.size(), 2);
  for (const auto &row : rows)
    EXPECT_EQ(row.kind, playlist_row::Kind::Track);
}

TEST(PlaylistRows, CountBased_DuplicateTrackWrongCount_StaysTrackRows)
{
  // Same track twice + one more: count matches (3) but not strictly ascending.
  QList<song> pl{
      make_track("a1", "h1", 1), make_track("a1", "h1", 1), make_track("a2", "h1", 2)};
  QMap<QString, int> counts{{"h1", 3}};

  auto rows = coalesce_playlist_rows(pl, count_resolver(counts));

  ASSERT_EQ(rows.size(), 3);
  for (const auto &row : rows)
    EXPECT_EQ(row.kind, playlist_row::Kind::Track);
}

TEST(PlaylistRows, CountBased_NoCountAndNoTracks_StaysTrackRows)
{
  QList<song> pl{make_track("a1", "h1", 1), make_track("a2", "h1", 2)};
  QMap<QString, int> counts; // unknown album → count 0

  auto rows = coalesce_playlist_rows(pl, count_resolver(counts));

  ASSERT_EQ(rows.size(), 2);
  for (const auto &row : rows)
    EXPECT_EQ(row.kind, playlist_row::Kind::Track);
}

// A run that matches the count but is a different-length album than the cached
// track list must not coalesce — cached tracks take precedence over count.
TEST(PlaylistRows, CachedTracksTakePrecedenceOverCount)
{
  // Album truly has 3 tracks (cached), playlist has 2 of them; count happens to
  // equal the run length, but the uri-exact branch (cached tracks present)
  // correctly rejects the partial run.
  QList<song> full{make_song("a1", "h1"), make_song("a2", "h1"), make_song("a3", "h1")};
  QList<song> pl{make_song("a1", "h1"), make_song("a2", "h1")};

  auto rows = coalesce_playlist_rows(pl, [&](const QString &) -> album_ref {
    return {full, 2}; // cached tracks (size 3) win over the misleading count 2
  });

  ASSERT_EQ(rows.size(), 2);
  for (const auto &row : rows)
    EXPECT_EQ(row.kind, playlist_row::Kind::Track);
}
