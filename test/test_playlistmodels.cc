// Row/role tests for the two playlists view-models:
//   - PlaylistItemModel: flat songs -> coalesced display rows (Track/Album),
//     flat_index() mapping, and per-role payloads.
//   - PlaylistSidebarModel: pinned auto row 0 + playlist rows, id<->row mapping.
//
// The coalescing math itself is covered in test_playlistrows.cc; here we only
// assert PlaylistItemModel drives it correctly and exposes the right roles.

#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QStringList>

#include "controller/librarymanager.hh"
#include "ui/playlistitemmodel.hh"
#include "ui/playlistsidebarmodel.hh"

namespace {

auto
make_song(const QString &uri, const QString &hash) -> song
{
  return song{uri, "T:" + uri, "artist", 1, 1, 200000, hash};
}

auto
make_album(const QString &hash, const QString &name, const QString &artist) -> album
{
  album a;
  a.album_hash = hash;
  a.name = name;
  a.artist = artist;
  return a;
}

} // namespace

TEST(PlaylistItemModel, FullAlbumRun_CollapsesToOneAlbumRow)
{
  LibraryManager lib;
  QList<song> tracks{make_song("a1", "h1"), make_song("a2", "h1"), make_song("a3", "h1")};
  lib.set_songs("h1", tracks);
  lib.add_album("h1", make_album("h1", "Album One", "The Band"));

  PlaylistItemModel model(&lib);
  model.set_data(tracks);

  ASSERT_EQ(model.rowCount(), 1);
  auto idx = model.index(0);
  EXPECT_EQ(idx.data(PlaylistItemModel::UserRoleKind).toInt(), 1); // Album
  EXPECT_EQ(idx.data(PlaylistItemModel::UserRoleSpan).toInt(), 3);
  EXPECT_EQ(idx.data(PlaylistItemModel::UserRoleFirstIndex).toInt(), 0);
  EXPECT_EQ(idx.data(PlaylistItemModel::UserRolePrimary).toString(), "Album One");
  EXPECT_EQ(idx.data(PlaylistItemModel::UserRoleDurationMs).toLongLong(), 600000);
  EXPECT_EQ(model.flat_index(0), 0);
}

TEST(PlaylistItemModel, LooseTracks_StayIndividualTrackRows)
{
  LibraryManager lib; // no albums cached -> nothing coalesces
  QList<song> tracks{make_song("a1", "h1"), make_song("b1", "h2")};

  PlaylistItemModel model(&lib);
  model.set_data(tracks);

  ASSERT_EQ(model.rowCount(), 2);
  for (int i = 0; i < 2; ++i)
    {
      auto idx = model.index(i);
      EXPECT_EQ(idx.data(PlaylistItemModel::UserRoleKind).toInt(), 0); // Track
      EXPECT_EQ(idx.data(PlaylistItemModel::UserRoleSpan).toInt(), 1);
      EXPECT_EQ(idx.data(PlaylistItemModel::UserRoleFirstIndex).toInt(), i);
    }
  EXPECT_EQ(model.index(0).data(PlaylistItemModel::UserRolePrimary).toString(), "T:a1");
}

TEST(PlaylistItemModel, MixedRuns_MapFlatIndexToRowStart)
{
  LibraryManager lib;
  QList<song> album2{make_song("b1", "h2"), make_song("b2", "h2")};
  lib.set_songs("h2", album2);
  lib.add_album("h2", make_album("h2", "Second", "Other"));

  QList<song> playlist{
      make_song("t1", "loose"),
      make_song("b1", "h2"),
      make_song("b2", "h2"),
      make_song("t2", "loose2"),
  };

  PlaylistItemModel model(&lib);
  model.set_data(playlist);

  ASSERT_EQ(model.rowCount(), 3);
  EXPECT_EQ(model.flat_index(0), 0); // loose track
  EXPECT_EQ(model.flat_index(1), 1); // album run starts at flat 1
  EXPECT_EQ(model.index(1).data(PlaylistItemModel::UserRoleKind).toInt(), 1);
  EXPECT_EQ(model.flat_index(2), 3); // trailing loose track
  EXPECT_EQ(model.songs().size(), 4);
}

TEST(PlaylistItemModel, CurrentUri_FlagsContainingRow)
{
  LibraryManager lib;
  QList<song> tracks{make_song("a1", "h1"), make_song("b1", "h2")};

  PlaylistItemModel model(&lib);
  model.set_data(tracks);
  model.set_current_uri("b1");

  EXPECT_FALSE(model.index(0).data(PlaylistItemModel::UserRoleCurrentlyPlaying).toBool());
  EXPECT_TRUE(model.index(1).data(PlaylistItemModel::UserRoleCurrentlyPlaying).toBool());
}

// The favorites list is re-fetched on every library reload / poll to re-resolve
// album hashes. set_data must no-op on identical data (no reset → no scroll
// jump), but still rebuild when a row's album_hash changes (fallback → real,
// which is what makes the covers/album name finally load).
TEST(PlaylistItemModel, SetData_NoResetWhenUnchanged_ResetsOnAlbumHashChange)
{
  LibraryManager lib;
  QList<song> tracks{make_song("a1", "fallback"), make_song("a2", "fallback")};

  PlaylistItemModel model(&lib);
  model.set_data(tracks);

  QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
  model.set_data(tracks); // identical → no reset
  EXPECT_EQ(reset_spy.count(), 0);

  QList<song> resolved{make_song("a1", "realhash"), make_song("a2", "realhash")};
  model.set_data(resolved); // album_hash changed → rebuild
  EXPECT_EQ(reset_spy.count(), 1);
}

TEST(PlaylistSidebarModel, PinnedAutoRowThenPlaylists)
{
  PlaylistSidebarModel model;
  EXPECT_EQ(model.rowCount(), 1); // just the auto row before any playlists
  EXPECT_TRUE(model.is_auto_row(0));
  EXPECT_TRUE(model.index(0).data(PlaylistSidebarModel::UserRoleIsAuto).toBool());

  QList<playlist_info> lists;
  playlist_info a;
  a.id = "id-a";
  a.name = "Roadtrip";
  a.song_count = 12;
  playlist_info b;
  b.id = "id-b";
  b.name = "Focus";
  b.song_count = -1; // unknown count (MPD before contents load)
  lists << a << b;
  model.set_playlists(lists);

  ASSERT_EQ(model.rowCount(), 3);
  EXPECT_FALSE(model.is_auto_row(1));
  EXPECT_EQ(model.playlist_id(0), QString{}); // auto row has no id
  EXPECT_EQ(model.playlist_id(1), "id-a");
  EXPECT_EQ(model.playlist_id(2), "id-b");
  EXPECT_EQ(model.row_for_id("id-b"), 2);
  EXPECT_EQ(model.row_for_id("missing"), -1);

  EXPECT_EQ(model.index(1).data(PlaylistSidebarModel::UserRoleName).toString(), "Roadtrip");
  EXPECT_TRUE(model.index(1).data(PlaylistSidebarModel::UserRoleMeta).toString().contains("12 items"));
  EXPECT_TRUE(model.index(2).data(PlaylistSidebarModel::UserRoleMeta).toString().contains("— items"));
}

TEST(PlaylistSidebarModel, FavoritesCountFeedsAutoRowMeta)
{
  PlaylistSidebarModel model;
  model.set_favorites_count(7);
  auto meta = model.index(0).data(PlaylistSidebarModel::UserRoleMeta).toString();
  EXPECT_TRUE(meta.contains("7 tracks"));
  EXPECT_TRUE(meta.contains("AUTO"));
}
