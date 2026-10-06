#include <gtest/gtest.h>
#include <QSignalSpy>

#include <memory>

#include "controller/favoritesmanager.hh"
#include "controller/backend.hh"

// ---------------------------------------------------------------------------
// Minimal stub backend: records set_favorite calls and exposes helpers to emit
// the favorites signals FavoritesManager reconciles from.
// ---------------------------------------------------------------------------

class FavStubBackend : public Backend {
  Q_OBJECT
public:
  bool supports_favorites = true;
  int set_favorite_calls = 0;
  int fetch_calls = 0;
  QString last_uri;
  bool last_fav = false;
  QSet<QString> server_set; // authoritative set returned by fetch_favorites()

  void emit_favorites_loaded(const QSet<QString> &uris) { emit favorites_loaded(uris); }
  void emit_favorite_changed(const QString &uri, bool fav)
  {
    emit favorite_changed(uri, fav);
  }
  void emit_error() { emit error("boom"); }
  void emit_favorite_songs_loaded(const QList<song> &songs)
  {
    emit favorite_songs_loaded(songs);
  }
  void emit_album_mapping_changed() { emit album_mapping_changed(); }
  void emit_connection_update(bool up) { emit connection_update(up); }
  void emit_favorites_stale() { emit favorites_stale(); }
  void emit_capabilities_changed() { emit capabilities_changed(); }

  // Stands in for the library index: native album id → album_hash. Unmapped ids
  // resolve empty, which the re-resolve treats as "leave this song alone".
  QHash<QString, QString> resolved;
  auto resolve_album_hash(const QString &native_album_id) const -> QString override
  {
    return resolved.value(native_album_id);
  }

  // Backend interface — only favorites + supports carry behaviour; the rest are
  // inert stubs so the abstract class is satisfiable.
  auto get_albums() -> QList<album> override { return {}; }
  void fetch_songs(const album &, std::function<void(const QList<song> &)> cb) override
  {
    if (cb) cb({});
  }
  bool supports(Feature f) const override
  {
    return f == Feature::Favorites && supports_favorites;
  }

  void set_favorite(const QString &uri, bool fav) override
  {
    set_favorite_calls++;
    last_uri = uri;
    last_fav = fav;
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
  void fetch_favorites() override
  {
    fetch_calls++;
    emit favorites_loaded(server_set);
  }
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class FavoritesManagerTest : public ::testing::Test {
protected:
  std::shared_ptr<FavStubBackend> stub;
  std::unique_ptr<FavoritesManager> favs;

  void SetUp() override
  {
    stub = std::make_shared<FavStubBackend>();
    favs = std::make_unique<FavoritesManager>(stub);
  }
};

// ---------------------------------------------------------------------------
// Initial state / availability
// ---------------------------------------------------------------------------

TEST_F(FavoritesManagerTest, IsFavorite_FalseInitially)
{
  EXPECT_FALSE(favs->is_favorite("s1"));
  EXPECT_TRUE(favs->all().isEmpty());
}

TEST_F(FavoritesManagerTest, Available_ReflectsBackendSupport)
{
  EXPECT_TRUE(favs->available());
  stub->supports_favorites = false;
  EXPECT_FALSE(favs->available());
}

TEST_F(FavoritesManagerTest, NullBackend_AvailableFalse_NoCrash)
{
  FavoritesManager fm{nullptr};
  EXPECT_FALSE(fm.available());
  fm.set_favorite("s1", true);       // must not crash
  EXPECT_TRUE(fm.is_favorite("s1")); // still optimistically tracked
}

// ---------------------------------------------------------------------------
// Optimistic set / toggle
// ---------------------------------------------------------------------------

TEST_F(FavoritesManagerTest, SetFavorite_OptimisticallyUpdatesAndPersists)
{
  QSignalSpy spy(favs.get(), &FavoritesManager::favorite_changed);

  favs->set_favorite("s1", true);

  EXPECT_TRUE(favs->is_favorite("s1"));
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().at(0).toString(), "s1");
  EXPECT_TRUE(spy.first().at(1).toBool());

  // Pushed down to the backend.
  EXPECT_EQ(stub->set_favorite_calls, 1);
  EXPECT_EQ(stub->last_uri, "s1");
  EXPECT_TRUE(stub->last_fav);
}

TEST_F(FavoritesManagerTest, SetFavorite_Redundant_NoOp)
{
  favs->set_favorite("s1", true);
  QSignalSpy spy(favs.get(), &FavoritesManager::favorite_changed);

  favs->set_favorite("s1", true); // already favorited

  EXPECT_EQ(spy.count(), 0);
  EXPECT_EQ(stub->set_favorite_calls, 1); // no second backend call
}

TEST_F(FavoritesManagerTest, Toggle_FlipsState)
{
  favs->toggle("s1");
  EXPECT_TRUE(favs->is_favorite("s1"));
  EXPECT_TRUE(stub->last_fav);

  favs->toggle("s1");
  EXPECT_FALSE(favs->is_favorite("s1"));
  EXPECT_FALSE(stub->last_fav);
  EXPECT_EQ(stub->set_favorite_calls, 2);
}

// ---------------------------------------------------------------------------
// Reconciliation from backend signals
// ---------------------------------------------------------------------------

TEST_F(FavoritesManagerTest, FavoritesLoaded_ReplacesSetAndEmitsReset)
{
  favs->set_favorite("old", true); // will be wiped by the bulk load
  QSignalSpy reset_spy(favs.get(), &FavoritesManager::favorites_reset);

  stub->emit_favorites_loaded({"s1", "s2"});

  ASSERT_EQ(reset_spy.count(), 1);
  EXPECT_TRUE(favs->is_favorite("s1"));
  EXPECT_TRUE(favs->is_favorite("s2"));
  EXPECT_FALSE(favs->is_favorite("old"));
  EXPECT_EQ(favs->all().size(), 2);
}

TEST_F(FavoritesManagerTest, BackendFavoriteChanged_UpdatesStateAndEmits)
{
  QSignalSpy spy(favs.get(), &FavoritesManager::favorite_changed);

  stub->emit_favorite_changed("s1", true); // external change

  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(favs->is_favorite("s1"));
}

// An optimistic toggle already updated our state; the backend's matching
// confirmation must not re-emit favorite_changed (no double repaint / no loop).
TEST_F(FavoritesManagerTest, OptimisticThenBackendConfirm_NoDoubleEmit)
{
  favs->set_favorite("s1", true); // optimistic — emits once
  QSignalSpy spy(favs.get(), &FavoritesManager::favorite_changed);

  stub->emit_favorite_changed("s1", true); // backend confirmation

  EXPECT_EQ(spy.count(), 0);
  EXPECT_TRUE(favs->is_favorite("s1"));
}

// ---------------------------------------------------------------------------
// Optimistic revert on backend error (Phase 6)
// ---------------------------------------------------------------------------

// A failed toggle (backend errors before confirming) re-fetches the authoritative
// set, snapping the optimistic change back to truth.
TEST_F(FavoritesManagerTest, ErrorAfterToggle_RevertsViaRefetch)
{
  stub->server_set = {};          // the server never persisted the star
  favs->set_favorite("s1", true); // optimistic on
  EXPECT_TRUE(favs->is_favorite("s1"));

  QSignalSpy reset_spy(favs.get(), &FavoritesManager::favorites_reset);
  stub->emit_error();

  EXPECT_EQ(stub->fetch_calls, 1);
  ASSERT_EQ(reset_spy.count(), 1);
  EXPECT_FALSE(favs->is_favorite("s1")); // reverted
}

// An error unrelated to a toggle (nothing pending) must not trigger a re-fetch.
TEST_F(FavoritesManagerTest, ErrorWithoutPendingToggle_NoRefetch)
{
  stub->emit_error();
  EXPECT_EQ(stub->fetch_calls, 0);
}

// Once a toggle is confirmed, a later unrelated error doesn't re-fetch.
TEST_F(FavoritesManagerTest, ConfirmedToggleThenError_NoRefetch)
{
  favs->set_favorite("s1", true);
  stub->emit_favorite_changed("s1", true); // confirmation clears pending
  stub->emit_error();
  EXPECT_EQ(stub->fetch_calls, 0);
}

// ---------------------------------------------------------------------------
// Refresh ownership — every favorites read starts here
// ---------------------------------------------------------------------------

// Connecting is the initial read. Nothing else in the app issues it, so if this
// stops firing the hearts and the auto list stay empty for the session.
TEST_F(FavoritesManagerTest, ConnectionUp_Reads)
{
  stub->emit_connection_update(true);
  EXPECT_EQ(stub->fetch_calls, 1);
}

TEST_F(FavoritesManagerTest, ConnectionDown_DoesNotRead)
{
  stub->emit_connection_update(false);
  EXPECT_EQ(stub->fetch_calls, 0);
}

// The backend reporting that the server may have diverged — a poll tick, an MPD
// sticker event — is a reason to read, and the manager is what turns it into one.
TEST_F(FavoritesManagerTest, FavoritesStale_Reads)
{
  stub->emit_favorites_stale();
  EXPECT_EQ(stub->fetch_calls, 1);
}

// A backend without favorites support is never asked for them.
TEST_F(FavoritesManagerTest, UnsupportedBackend_NeverReads)
{
  stub->supports_favorites = false;

  favs->request_refresh();
  stub->emit_connection_update(true);
  stub->emit_favorites_stale();

  EXPECT_EQ(stub->fetch_calls, 0);
}

// A server that withdraws favorites mid-session leaves the manager holding a set
// it can neither trust nor write back. Dropping it is only half the job: the
// hearts are already painted, and favorites_reset is what takes them down.
TEST_F(FavoritesManagerTest, CapabilityWithdrawn_ClearsAndRepaints)
{
  stub->emit_favorites_loaded({"s1", "s2"});
  stub->emit_favorite_songs_loaded({song{"s1", "One", "A", 1, 1, 1000, ":al1"}});
  ASSERT_TRUE(favs->is_favorite("s1"));
  ASSERT_FALSE(favs->songs().isEmpty());

  QSignalSpy reset(favs.get(), &FavoritesManager::favorites_reset);
  QSignalSpy songs(favs.get(), &FavoritesManager::songs_changed);
  stub->supports_favorites = false;
  stub->emit_capabilities_changed();

  EXPECT_EQ(reset.count(), 1);
  EXPECT_EQ(songs.count(), 1);
  EXPECT_FALSE(favs->is_favorite("s1"));
  EXPECT_TRUE(favs->songs().isEmpty());
}

// A capability change that doesn't touch favorites (a playlist downgrade) must
// leave the set alone — the signal names no feature, so the manager re-queries.
TEST_F(FavoritesManagerTest, UnrelatedCapabilityChange_KeepsFavorites)
{
  stub->emit_favorites_loaded({"s1"});

  QSignalSpy reset(favs.get(), &FavoritesManager::favorites_reset);
  stub->emit_capabilities_changed(); // supports_favorites still true

  EXPECT_EQ(reset.count(), 0);
  EXPECT_TRUE(favs->is_favorite("s1"));
}

// ---------------------------------------------------------------------------
// Re-resolving the auto list against a library index that arrived late
// ---------------------------------------------------------------------------

// Favorites are read before the library finishes paginating, so their album
// hashes start as fallbacks. When the index lands, the songs already held are
// corrected in place — no second read of the same payload.
TEST_F(FavoritesManagerTest, AlbumMappingChanged_ReresolvesHeldSongs)
{
  song s{"s1", "One", "A", 1, 1, 1000, ":al1"};
  s.native_album_id = "al1";
  stub->emit_favorite_songs_loaded({s});

  stub->resolved["al1"] = "mbid-1"; // the library landed

  QSignalSpy changed(favs.get(), &FavoritesManager::songs_changed);
  stub->emit_album_mapping_changed();

  ASSERT_EQ(favs->songs().size(), 1);
  EXPECT_EQ(favs->songs().at(0).album_hash, "mbid-1");
  EXPECT_EQ(stub->fetch_calls, 0); // corrected locally, not re-fetched
  EXPECT_EQ(changed.count(), 1);
}

// A poll that reloads an identical library resolves every id to the hash the
// song already carries: no repaint, no signal.
TEST_F(FavoritesManagerTest, AlbumMappingChanged_NoOpWhenHashesAreUnchanged)
{
  song s{"s1", "One", "A", 1, 1, 1000, "mbid-1"};
  s.native_album_id = "al1";
  stub->resolved["al1"] = "mbid-1";
  stub->emit_favorite_songs_loaded({s});

  QSignalSpy changed(favs.get(), &FavoritesManager::songs_changed);
  stub->emit_album_mapping_changed();

  EXPECT_EQ(changed.count(), 0);
  EXPECT_EQ(stub->fetch_calls, 0);
}

// MPD resolves nothing (its hashes come from each song's own tags), so the
// mapping signal must leave its songs exactly as they are.
TEST_F(FavoritesManagerTest, AlbumMappingChanged_LeavesUnresolvableSongsAlone)
{
  song s{"file:///a.flac", "One", "A", 1, 1, 1000, "tag-hash"};
  stub->emit_favorite_songs_loaded({s});

  QSignalSpy changed(favs.get(), &FavoritesManager::songs_changed);
  stub->emit_album_mapping_changed();

  ASSERT_EQ(favs->songs().size(), 1);
  EXPECT_EQ(favs->songs().at(0).album_hash, "tag-hash");
  EXPECT_EQ(changed.count(), 0);
}

#include "test_favoritesmanager.moc"
