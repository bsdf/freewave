#include <gtest/gtest.h>
#include <QSignalSpy>

#include <memory>

#include "controller/playlistsmanager.hh"
#include "controller/backend.hh"

// ---------------------------------------------------------------------------
// Minimal stub backend: records playlist mutation calls and exposes helpers
// to emit the playlist signals PlaylistsManager reconciles from.
// ---------------------------------------------------------------------------

class PlaylistsStubBackend : public Backend {
  Q_OBJECT
public:
  bool supports_playlists = true;

  int fetch_playlists_calls = 0;
  int fetch_playlist_songs_calls = 0;
  int create_calls = 0;
  int rename_calls = 0;
  int delete_calls = 0;
  int add_calls = 0;
  int remove_calls = 0;
  int rearrange_calls = 0;

  QString last_create_name;
  QList<song> last_create_songs;
  QString last_rename_id;
  QString last_rename_name;
  QString last_delete_id;
  QString last_add_id;
  QList<song> last_add_songs;
  QString last_remove_id;
  QList<int> last_remove_positions;
  QString last_rearrange_id;
  int last_rearrange_target = -1;
  QList<int> last_rearrange_moved;

  QList<playlist_info> server_lists; // returned by fetch_playlists()

  void emit_playlists_loaded(const QList<playlist_info> &lists) { emit playlists_loaded(lists); }
  void emit_playlist_songs_loaded(const QString &id, const QList<song> &songs)
  {
    emit playlist_songs_loaded(id, songs);
  }
  void emit_playlists_changed(const QString &id = {}) { emit playlists_changed(id); }
  void emit_error() { emit error("boom"); }
  void emit_connection_update(bool connected) { emit connection_update(connected); }
  void emit_library_changed() { emit library_changed(); }
  void emit_album_mapping_changed() { emit album_mapping_changed(); }

  // Stands in for the library index: native album id → album_hash. Unmapped ids
  // resolve empty, which the re-resolve treats as "leave this song alone".
  QHash<QString, QString> resolved;
  auto resolve_album_hash(const QString &native_album_id) const -> QString override
  {
    return resolved.value(native_album_id);
  }

  // Backend interface — only playlists + supports carry behaviour; the rest
  // are inert stubs so the abstract class is satisfiable.
  auto get_albums() -> QList<album> override { return {}; }
  void fetch_songs(const album &, std::function<void(const QList<song> &)> cb) override
  {
    if (cb) cb({});
  }
  bool supports(Feature f) const override
  {
    return f == Feature::Playlists && supports_playlists;
  }

  void fetch_playlists() override
  {
    fetch_playlists_calls++;
    emit playlists_loaded(server_lists);
  }
  void fetch_playlist_songs(const QString &) override { fetch_playlist_songs_calls++; }
  void create_playlist(const QString &name, const QList<song> &songs) override
  {
    create_calls++;
    last_create_name = name;
    last_create_songs = songs;
  }
  void rename_playlist(const QString &id, const QString &new_name) override
  {
    rename_calls++;
    last_rename_id = id;
    last_rename_name = new_name;
  }
  void delete_playlist(const QString &id) override
  {
    delete_calls++;
    last_delete_id = id;
  }
  void add_to_playlist(const QString &id, const QList<song> &songs) override
  {
    add_calls++;
    last_add_id = id;
    last_add_songs = songs;
  }
  void remove_from_playlist(const QString &id, const QList<int> &positions) override
  {
    remove_calls++;
    last_remove_id = id;
    last_remove_positions = positions;
  }
  void rearrange_playlist(const QString &id, int target,
      const QList<int> &moved) override
  {
    rearrange_calls++;
    last_rearrange_id = id;
    last_rearrange_target = target;
    last_rearrange_moved = moved;
  }

public slots:
  bool connect_to_server() override { return true; }
  void disconnect_from_server() override {}
  void play() override {}
  void pause() override {}
  void stop() override {}
  void prev() override {}
  void next() override {}
  void seek(uint32_t) override {}
  void set_volume(int) override {}
  void play_pos(uint32_t) override {}
  void set_repeat(bool, bool) override {}
  void set_shuffle(bool) override {}
  void insert_queue(const QList<song> &, uint32_t) override {}
  void append_queue(const QList<song> &) override {}
  void replace_queue(const QList<song> &, uint32_t) override {}
  void remove_from_queue(const QList<QModelIndex> &) override {}
  void rearrange_queue(uint32_t, std::vector<uint32_t>) override {}
  void fetch_album_art(const QString &) override {}
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class PlaylistsManagerTest : public ::testing::Test {
protected:
  std::shared_ptr<PlaylistsStubBackend> stub;
  std::unique_ptr<PlaylistsManager> pm;

  void SetUp() override
  {
    stub = std::make_shared<PlaylistsStubBackend>();
    pm = std::make_unique<PlaylistsManager>(stub); // ctor calls reload()
  }
};

// ---------------------------------------------------------------------------
// Initial state / availability
// ---------------------------------------------------------------------------

TEST_F(PlaylistsManagerTest, InitialState_EmptyList)
{
  EXPECT_TRUE(pm->playlists().isEmpty());
  EXPECT_FALSE(pm->info("id1").has_value());
}

TEST_F(PlaylistsManagerTest, Available_ReflectsBackendSupport)
{
  EXPECT_TRUE(pm->available());
  stub->supports_playlists = false;
  EXPECT_FALSE(pm->available());
}

TEST_F(PlaylistsManagerTest, NullBackend_AvailableFalse_NoCrash)
{
  PlaylistsManager mgr{nullptr};
  EXPECT_FALSE(mgr.available());
  mgr.create("List", {}); // must not crash
  mgr.rename("id1", "x");
  mgr.remove("id1");
  mgr.add_songs("id1", {});
  mgr.remove_at("id1", {0});
  mgr.rearrange("id1", 0, {1});
  mgr.ensure_songs("id1");
}

// ---------------------------------------------------------------------------
// Reconciliation from backend signals
// ---------------------------------------------------------------------------

TEST_F(PlaylistsManagerTest, PlaylistsLoaded_PopulatesListAndEmitsReset)
{
  QSignalSpy spy(pm.get(), &PlaylistsManager::playlists_reset);

  stub->emit_playlists_loaded({playlist_info{"id1", "Chill", 3, 500000, {}}});

  ASSERT_EQ(spy.count(), 1);
  ASSERT_EQ(pm->playlists().size(), 1);
  EXPECT_EQ(pm->playlists()[0].id, "id1");

  auto info = pm->info("id1");
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->name, "Chill");
}

TEST_F(PlaylistsManagerTest, EnsureSongs_FetchesWhenNotCached)
{
  int before = stub->fetch_playlist_songs_calls;
  pm->ensure_songs("id1");
  EXPECT_EQ(stub->fetch_playlist_songs_calls, before + 1);
}

TEST_F(PlaylistsManagerTest, EnsureSongs_SkipsWhenAlreadyCached)
{
  stub->emit_playlist_songs_loaded("id1", {});
  int before = stub->fetch_playlist_songs_calls;
  pm->ensure_songs("id1");
  EXPECT_EQ(stub->fetch_playlist_songs_calls, before);
}

TEST_F(PlaylistsManagerTest, PlaylistSongsLoaded_BackfillsSummaryAndEmitsUpdated)
{
  stub->emit_playlists_loaded({playlist_info{"id1", "Chill", -1, -1, {}}});
  QSignalSpy spy(pm.get(), &PlaylistsManager::playlist_updated);

  song s1{"u1", "t1", "a1", 1, 1, 180000, "h1"};
  song s2{"u2", "t2", "a1", 2, 1, 220000, "h1"};
  stub->emit_playlist_songs_loaded("id1", {s1, s2});

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().at(0).toString(), "id1");

  auto info = pm->info("id1");
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->song_count, 2);
  EXPECT_EQ(info->duration_ms, 400000);

  auto cached = pm->songs("id1");
  ASSERT_TRUE(cached.has_value());
  EXPECT_EQ(cached->size(), 2);
}

// An unnamed change (a create, or an MPD idle event) could have touched any
// playlist, so every cached contents list goes.
TEST_F(PlaylistsManagerTest, PlaylistsChanged_ClearsSongCacheAndRefetches)
{
  stub->emit_playlists_loaded({playlist_info{"id1", "Chill", -1, -1, {}}});
  stub->emit_playlist_songs_loaded("id1", {});
  ASSERT_TRUE(pm->songs("id1").has_value());

  int before = stub->fetch_playlists_calls;
  stub->emit_playlists_changed();

  EXPECT_EQ(stub->fetch_playlists_calls, before + 1);
  EXPECT_FALSE(pm->songs("id1").has_value());
}

// A named change drops only that playlist's contents: the others stay cached,
// so nothing refetches them.
TEST_F(PlaylistsManagerTest, PlaylistsChanged_WithId_DropsOnlyThatPlaylist)
{
  stub->emit_playlists_loaded({playlist_info{"id1", "Chill", -1, -1, {}},
      playlist_info{"id2", "Focus", -1, -1, {}}});
  stub->emit_playlist_songs_loaded("id1", {});
  stub->emit_playlist_songs_loaded("id2", {});
  ASSERT_TRUE(pm->songs("id1").has_value());
  ASSERT_TRUE(pm->songs("id2").has_value());

  int before = stub->fetch_playlists_calls;
  stub->emit_playlists_changed("id1");

  EXPECT_EQ(stub->fetch_playlists_calls, before + 1);
  EXPECT_FALSE(pm->songs("id1").has_value());
  EXPECT_TRUE(pm->songs("id2").has_value());
}

// The payoff, measured the way AddToPlaylistDialog spends it: that dialog caches
// every playlist's contents on open, then re-requests them all after a mutation
// (reload_contents is a loop of ensure_songs). A named change costs one refetch;
// an unnamed one costs the whole list.
TEST_F(PlaylistsManagerTest, NamedChange_CostsOneRefetchNotOnePerPlaylist)
{
  QList<playlist_info> lists;
  for (int i = 1; i <= 5; ++i)
    lists.append(playlist_info{QString("id%1").arg(i), QString("PL %1").arg(i), -1, -1, {}});

  auto reload_contents = [this] { // what the dialog does on playlists_reset
    for (const auto &pl : pm->playlists())
      pm->ensure_songs(pl.id);
  };
  // The mutation's own refetch re-delivers server_lists, so the list must
  // survive it — otherwise reload_contents would loop over nothing.
  stub->server_lists = lists;
  auto warm_all = [&] {
    stub->emit_playlists_loaded(lists);
    for (const auto &pl : lists)
      stub->emit_playlist_songs_loaded(pl.id, {});
  };

  warm_all();
  int before = stub->fetch_playlist_songs_calls;
  stub->emit_playlists_changed("id3");
  reload_contents();
  EXPECT_EQ(stub->fetch_playlist_songs_calls, before + 1);

  warm_all();
  before = stub->fetch_playlist_songs_calls;
  stub->emit_playlists_changed();
  reload_contents();
  EXPECT_EQ(stub->fetch_playlist_songs_calls, before + 5);
}

TEST_F(PlaylistsManagerTest, ConnectionLost_ClearsStateAndEmitsReset)
{
  stub->emit_playlists_loaded({playlist_info{"id1", "Chill", -1, -1, {}}});
  stub->emit_playlist_songs_loaded("id1", {});
  ASSERT_FALSE(pm->playlists().isEmpty());

  QSignalSpy spy(pm.get(), &PlaylistsManager::playlists_reset);
  stub->emit_connection_update(false);

  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(pm->playlists().isEmpty());
  EXPECT_FALSE(pm->songs("id1").has_value());
}

TEST_F(PlaylistsManagerTest, ConnectionRestored_Reloads)
{
  int before = stub->fetch_playlists_calls;
  stub->emit_connection_update(true);
  EXPECT_EQ(stub->fetch_playlists_calls, before + 1);
}

// ---------------------------------------------------------------------------
// Mutations — pessimistic forwarding
// ---------------------------------------------------------------------------

TEST_F(PlaylistsManagerTest, Create_ForwardsToBackend)
{
  song s1{"u1", "t1", "a1", 1, 1, 1000, "h1"};
  pm->create("New List", {s1});

  EXPECT_EQ(stub->create_calls, 1);
  EXPECT_EQ(stub->last_create_name, "New List");
  ASSERT_EQ(stub->last_create_songs.size(), 1);
}

TEST_F(PlaylistsManagerTest, Rename_ForwardsToBackend)
{
  pm->rename("id1", "Renamed");
  EXPECT_EQ(stub->rename_calls, 1);
  EXPECT_EQ(stub->last_rename_id, "id1");
  EXPECT_EQ(stub->last_rename_name, "Renamed");
}

TEST_F(PlaylistsManagerTest, Remove_ForwardsToBackend)
{
  pm->remove("id1");
  EXPECT_EQ(stub->delete_calls, 1);
  EXPECT_EQ(stub->last_delete_id, "id1");
}

TEST_F(PlaylistsManagerTest, Rearrange_ForwardsToBackend)
{
  pm->rearrange("id1", 3, {0, 1});
  EXPECT_EQ(stub->rearrange_calls, 1);
  EXPECT_EQ(stub->last_rearrange_id, "id1");
  EXPECT_EQ(stub->last_rearrange_target, 3);
  EXPECT_EQ(stub->last_rearrange_moved, (QList<int>{0, 1}));
}

// Reorder optimistically permutes the cached song list (move song 0 to the end
// of [u1,u2,u3] → [u2,u3,u1]) and emits playlist_updated for instant repaint.
TEST_F(PlaylistsManagerTest, Rearrange_OptimisticWhenCached_PermutesCacheAndEmits)
{
  stub->emit_playlists_loaded({playlist_info{"id1", "Chill", -1, -1, {}}});
  song s1{"u1", "t1", "a1", 1, 1, 100000, "h1"};
  song s2{"u2", "t2", "a1", 2, 1, 100000, "h1"};
  song s3{"u3", "t3", "a1", 3, 1, 100000, "h1"};
  stub->emit_playlist_songs_loaded("id1", {s1, s2, s3});

  QSignalSpy spy(pm.get(), &PlaylistsManager::playlist_updated);
  pm->rearrange("id1", 3, {0}); // move index 0 to the end

  ASSERT_EQ(spy.count(), 1);
  auto cached = pm->songs("id1");
  ASSERT_TRUE(cached.has_value());
  ASSERT_EQ(cached->size(), 3);
  EXPECT_EQ((*cached)[0].uri, "u2");
  EXPECT_EQ((*cached)[1].uri, "u3");
  EXPECT_EQ((*cached)[2].uri, "u1");

  EXPECT_EQ(stub->rearrange_calls, 1);
  EXPECT_EQ(stub->last_rearrange_target, 3);
}

// ---------------------------------------------------------------------------
// Mutations — optimistic song-cache updates (add_songs / remove_at)
// ---------------------------------------------------------------------------

TEST_F(PlaylistsManagerTest, AddSongs_OptimisticWhenCached_UpdatesCacheAndEmits)
{
  stub->emit_playlists_loaded({playlist_info{"id1", "Chill", -1, -1, {}}});
  song s1{"u1", "t1", "a1", 1, 1, 100000, "h1"};
  stub->emit_playlist_songs_loaded("id1", {s1});

  QSignalSpy spy(pm.get(), &PlaylistsManager::playlist_updated);
  song s2{"u2", "t2", "a1", 2, 1, 150000, "h1"};
  pm->add_songs("id1", {s2});

  ASSERT_EQ(spy.count(), 1);
  auto cached = pm->songs("id1");
  ASSERT_TRUE(cached.has_value());
  EXPECT_EQ(cached->size(), 2);

  auto info = pm->info("id1");
  ASSERT_TRUE(info.has_value());
  EXPECT_EQ(info->song_count, 2);
  EXPECT_EQ(info->duration_ms, 250000);

  EXPECT_EQ(stub->add_calls, 1);
  EXPECT_EQ(stub->last_add_id, "id1");
}

TEST_F(PlaylistsManagerTest, AddSongs_NotCached_NoLocalUpdate_StillForwardsToBackend)
{
  QSignalSpy spy(pm.get(), &PlaylistsManager::playlist_updated);
  song s1{"u1", "t1", "a1", 1, 1, 100000, "h1"};
  pm->add_songs("id1", {s1});

  EXPECT_EQ(spy.count(), 0); // nothing cached to update optimistically
  EXPECT_FALSE(pm->songs("id1").has_value());
  EXPECT_EQ(stub->add_calls, 1);
}

TEST_F(PlaylistsManagerTest, RemoveAt_OptimisticWhenCached_UpdatesCacheAndEmits)
{
  stub->emit_playlists_loaded({playlist_info{"id1", "Chill", -1, -1, {}}});
  song s1{"u1", "t1", "a1", 1, 1, 100000, "h1"};
  song s2{"u2", "t2", "a1", 2, 1, 150000, "h1"};
  stub->emit_playlist_songs_loaded("id1", {s1, s2});

  QSignalSpy spy(pm.get(), &PlaylistsManager::playlist_updated);
  pm->remove_at("id1", {0});

  ASSERT_EQ(spy.count(), 1);
  auto cached = pm->songs("id1");
  ASSERT_TRUE(cached.has_value());
  ASSERT_EQ(cached->size(), 1);
  EXPECT_EQ((*cached)[0].uri, "u2");

  EXPECT_EQ(stub->remove_calls, 1);
  EXPECT_EQ(stub->last_remove_id, "id1");
  ASSERT_EQ(stub->last_remove_positions.size(), 1);
  EXPECT_EQ(stub->last_remove_positions[0], 0);
}

// ---------------------------------------------------------------------------
// Optimistic revert on backend error
// ---------------------------------------------------------------------------

TEST_F(PlaylistsManagerTest, ErrorAfterMutation_RefetchesAndClearsCache)
{
  stub->emit_playlists_loaded({playlist_info{"id1", "Chill", -1, -1, {}}});
  stub->emit_playlist_songs_loaded("id1", {});
  pm->rename("id1", "New Name"); // sets pending

  int before = stub->fetch_playlists_calls;
  stub->emit_error();

  EXPECT_EQ(stub->fetch_playlists_calls, before + 1);
  EXPECT_FALSE(pm->songs("id1").has_value());
}

TEST_F(PlaylistsManagerTest, ErrorWithoutPendingMutation_NoRefetch)
{
  int before = stub->fetch_playlists_calls;
  stub->emit_error();
  EXPECT_EQ(stub->fetch_playlists_calls, before);
}

TEST_F(PlaylistsManagerTest, ConfirmedMutationThenError_NoRefetch)
{
  pm->rename("id1", "New Name");  // sets pending
  stub->emit_playlists_changed(); // clears pending (and itself calls fetch_playlists)

  int before = stub->fetch_playlists_calls;
  stub->emit_error();
  EXPECT_EQ(stub->fetch_playlists_calls, before); // already reconciled
}

// ---------------------------------------------------------------------------
// Cover mosaic hashes — the manager only derives the album hashes from loaded
// songs; rendering + persistence of the tile itself is AlbumArtManager's job.
// ---------------------------------------------------------------------------

TEST_F(PlaylistsManagerTest, MosaicHashes_FromLoadedSongs_FirstFourDistinct)
{
  stub->emit_playlists_loaded({playlist_info{"id1", "Mix", -1, -1, {}}});
  QList<song> songs{
      song{"u1", "t1", "a", 1, 1, 1000, "hA"},
      song{"u2", "t2", "a", 2, 1, 1000, "hA"}, // duplicate album → collapsed
      song{"u3", "t3", "a", 1, 1, 1000, "hB"},
      song{"u4", "t4", "a", 1, 1, 1000, ""}, // empty hash → skipped
      song{"u5", "t5", "a", 1, 1, 1000, "hC"},
      song{"u6", "t6", "a", 1, 1, 1000, "hD"},
      song{"u7", "t7", "a", 1, 1, 1000, "hE"}, // 5th distinct → dropped (max 4)
  };
  stub->emit_playlist_songs_loaded("id1", songs);

  EXPECT_EQ(pm->mosaic_hashes("id1"), (QStringList{"hA", "hB", "hC", "hD"}));
}

// Contents fetched before the library index loads carry fallback album hashes
// (wrong covers). When the index moves, the cached contents are corrected where
// they sit — no re-fetch — and the views that draw from those hashes are told.
TEST_F(PlaylistsManagerTest, AlbumMappingChanged_ReresolvesCachedContents)
{
  stub->emit_playlists_loaded({playlist_info{"id1", "Mix", -1, -1, {}}});
  song s{"u1", "t1", "a", 1, 1, 1000, "fallback:al1"};
  s.native_album_id = "al1";
  stub->emit_playlist_songs_loaded("id1", {s});
  ASSERT_TRUE(pm->songs("id1").has_value());

  stub->resolved["al1"] = "mbid-1"; // the library landed

  QSignalSpy reset(pm.get(), &PlaylistsManager::playlists_reset);
  QSignalSpy updated(pm.get(), &PlaylistsManager::playlist_updated);
  stub->emit_album_mapping_changed();

  ASSERT_TRUE(pm->songs("id1").has_value());
  EXPECT_EQ(pm->songs("id1")->at(0).album_hash, "mbid-1");
  EXPECT_EQ(stub->fetch_playlist_songs_calls, 0); // corrected in place, not re-fetched
  EXPECT_EQ(updated.count(), 1);
  EXPECT_EQ(reset.count(), 1); // sidebar mosaics key off these hashes
}

// A poll that reloads an identical library resolves every id to the hash it
// already had: nothing moved, so nothing is re-emitted and no view churns.
TEST_F(PlaylistsManagerTest, AlbumMappingChanged_NoOpWhenHashesAreUnchanged)
{
  stub->emit_playlists_loaded({playlist_info{"id1", "Mix", -1, -1, {}}});
  song s{"u1", "t1", "a", 1, 1, 1000, "mbid-1"};
  s.native_album_id = "al1";
  stub->resolved["al1"] = "mbid-1";
  stub->emit_playlist_songs_loaded("id1", {s});

  QSignalSpy reset(pm.get(), &PlaylistsManager::playlists_reset);
  QSignalSpy updated(pm.get(), &PlaylistsManager::playlist_updated);
  stub->emit_album_mapping_changed();

  EXPECT_EQ(updated.count(), 0);
  EXPECT_EQ(reset.count(), 0);
}

TEST_F(PlaylistsManagerTest, MosaicHashes_EmptyUntilSongsLoaded)
{
  stub->emit_playlists_loaded({playlist_info{"id1", "Mix", -1, -1, {}}});
  EXPECT_TRUE(pm->mosaic_hashes("id1").isEmpty()); // no contents yet

  stub->emit_playlist_songs_loaded("id1", {song{"u1", "t1", "a", 1, 1, 1000, "hA"}});
  EXPECT_EQ(pm->mosaic_hashes("id1"), (QStringList{"hA"}));
}

#include "test_playlistsmanager.moc"
