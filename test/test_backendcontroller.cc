#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QMetaMethod>
#include <QSignalSpy>
#include <QModelIndex>
#include <QSettings>
#include <QTemporaryDir>

#include "controller/backendcontroller.hh"
#include "controller/backend.hh"
#include "controller/librarymanager.hh"
#include "controller/profilestore.hh"
#include "model/album.hh"
#include "model/playlist.hh"
#include "model/song.hh"

// ---------------------------------------------------------------------------
// Mock backend: tracks slot invocations and exposes signal emission helpers.
// ---------------------------------------------------------------------------

class StubBackend : public Backend {
  Q_OBJECT
public:
  explicit StubBackend(QObject *parent = nullptr)
    : Backend{parent}
  {
  }

  // Configurable return values
  bool connect_result = true;
  bool supports_result = false;
  QList<album> albums_result;
  QList<song> songs_result;

  // Call counters
  int connect_calls = 0;
  int disconnect_calls = 0;
  int play_calls = 0;
  int pause_calls = 0;
  int stop_calls = 0;
  int prev_calls = 0;
  int next_calls = 0;
  int refresh_calls = 0;
  int fetch_art_calls = 0;
  int insert_queue_calls = 0;
  int append_queue_calls = 0;
  int replace_queue_calls = 0;
  int remove_queue_calls = 0;
  int rearrange_calls = 0;
  int set_favorite_calls = 0;
  int fetch_favorites_calls = 0;
  int fetch_playlists_calls = 0;
  int fetch_playlist_songs_calls = 0;
  int create_playlist_calls = 0;
  int rename_playlist_calls = 0;
  int delete_playlist_calls = 0;
  int add_to_playlist_calls = 0;
  int remove_from_playlist_calls = 0;
  int rearrange_playlist_calls = 0;

  // Last-argument captures
  uint32_t last_seek = 0;
  int last_volume = -1;
  uint32_t last_play_pos = 0;
  bool last_repeat = false;
  bool last_single = false;
  bool last_shuffle = false;
  int last_poll_interval = -1;
  QString last_art_uri;
  QString last_fav_uri;
  bool last_fav_state = false;
  Feature last_supports_feature{};
  QString last_playlist_id;
  QString last_playlist_name;
  QList<song> last_playlist_songs;
  QList<int> last_playlist_positions;
  int last_rearrange_target = -1;
  QList<int> last_rearrange_moved;

  // Whether anything is listening to one of this backend's signals — used to
  // check the façade wired up every signal on the interface, without having to
  // synthesise arguments for each one.
  bool signal_has_receivers(const QMetaMethod &m) const { return isSignalConnected(m); }

  // Signal emission helpers
  void emit_connection_update(bool c) { emit connection_update(c); }
  void emit_playback_state(PlaybackState s) { emit playback_state_changed(s); }
  void emit_current_song(const song &s) { emit current_song_changed(s); }
  void emit_queue(const QList<song> &songs) { emit queue_changed(songs); }
  void emit_library_changed() { emit library_changed(); }
  void emit_album_mapping_changed() { emit album_mapping_changed(); }
  void emit_library_refresh(bool active) { emit library_refresh_active(active); }
  void emit_songs_changed(const QString &hash) { emit songs_changed(hash); }
  void emit_album_art(const QString &uri, QByteArray bytes)
  {
    emit album_art_received(uri, std::move(bytes));
  }
  void emit_error(const QString &msg) { emit error(msg); }
  void emit_favorites_loaded(const QSet<QString> &uris) { emit favorites_loaded(uris); }
  void emit_favorite_changed(const QString &uri, bool fav)
  {
    emit favorite_changed(uri, fav);
  }
  void emit_playlists_loaded(const QList<playlist_info> &lists)
  {
    emit playlists_loaded(lists);
  }
  void emit_playlist_songs_loaded(const QString &id, const QList<song> &songs)
  {
    emit playlist_songs_loaded(id, songs);
  }
  void emit_playlists_changed(const QString &id = {}) { emit playlists_changed(id); }
  void emit_favorite_songs_loaded(const QList<song> &songs)
  {
    emit favorite_songs_loaded(songs);
  }

  // Backend interface
  auto get_albums() -> QList<album> override { return albums_result; }
  void fetch_songs(const album &, std::function<void(const QList<song> &)> cb) override
  {
    if (cb) cb(songs_result);
  }
  bool supports(Feature f) const override
  {
    const_cast<StubBackend *>(this)->last_supports_feature = f;
    return supports_result;
  }

public slots:
  bool connect_to_server() override
  {
    connect_calls++;
    return connect_result;
  }
  void disconnect_from_server() override { disconnect_calls++; }
  void play() override { play_calls++; }
  void pause() override { pause_calls++; }
  void stop() override { stop_calls++; }
  void prev() override { prev_calls++; }
  void next() override { next_calls++; }
  void seek(uint32_t ms) override { last_seek = ms; }
  void set_volume(int v) override { last_volume = v; }
  void play_pos(uint32_t p) override { last_play_pos = p; }
  void set_repeat(bool r, bool s) override
  {
    last_repeat = r;
    last_single = s;
  }
  void set_shuffle(bool s) override { last_shuffle = s; }
  void insert_queue(const QList<song> &, uint32_t) override { insert_queue_calls++; }
  void append_queue(const QList<song> &) override { append_queue_calls++; }
  void replace_queue(const QList<song> &, uint32_t) override { replace_queue_calls++; }
  void remove_from_queue(const QList<QModelIndex> &) override { remove_queue_calls++; }
  void rearrange_queue(uint32_t, std::vector<uint32_t>) override { rearrange_calls++; }
  void refresh_library() override { refresh_calls++; }
  void set_poll_interval(int m) override { last_poll_interval = m; }
  void fetch_album_art(const QString &uri) override
  {
    fetch_art_calls++;
    last_art_uri = uri;
  }
  void set_favorite(const QString &uri, bool fav) override
  {
    set_favorite_calls++;
    last_fav_uri = uri;
    last_fav_state = fav;
  }
  void fetch_favorites() override { fetch_favorites_calls++; }
  void fetch_playlists() override { fetch_playlists_calls++; }
  void fetch_playlist_songs(const QString &id) override
  {
    fetch_playlist_songs_calls++;
    last_playlist_id = id;
  }
  void create_playlist(const QString &name, const QList<song> &songs) override
  {
    create_playlist_calls++;
    last_playlist_name = name;
    last_playlist_songs = songs;
  }
  void rename_playlist(const QString &id, const QString &new_name) override
  {
    rename_playlist_calls++;
    last_playlist_id = id;
    last_playlist_name = new_name;
  }
  void delete_playlist(const QString &id) override
  {
    delete_playlist_calls++;
    last_playlist_id = id;
  }
  void add_to_playlist(const QString &id, const QList<song> &songs) override
  {
    add_to_playlist_calls++;
    last_playlist_id = id;
    last_playlist_songs = songs;
  }
  void remove_from_playlist(const QString &id, const QList<int> &positions) override
  {
    remove_from_playlist_calls++;
    last_playlist_id = id;
    last_playlist_positions = positions;
  }
  void rearrange_playlist(const QString &id, int target,
      const QList<int> &moved) override
  {
    rearrange_playlist_calls++;
    last_playlist_id = id;
    last_rearrange_target = target;
    last_rearrange_moved = moved;
  }
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class BackendControllerTest : public ::testing::Test {
protected:
  std::shared_ptr<StubBackend> mock;
  std::unique_ptr<BackendController> ctrl;

  void SetUp() override
  {
    mock = std::make_shared<StubBackend>();
    ctrl = std::make_unique<BackendController>(mock);
  }
};

// ---------------------------------------------------------------------------
// Null-active guards (no backend injected)
// ---------------------------------------------------------------------------

TEST(BackendControllerNullTest, Connect_ReturnsFalse)
{
  BackendController ctrl{std::shared_ptr<Backend>{}};
  EXPECT_FALSE(ctrl.connect_to_server());
}

TEST(BackendControllerNullTest, FetchSongs_CallsCallbackWithEmpty)
{
  BackendController ctrl{std::shared_ptr<Backend>{}};
  bool called = false;
  QList<song> result;
  ctrl.fetch_songs(album{}, [&](const QList<song> &s) {
    called = true;
    result = s;
  });
  EXPECT_TRUE(called);
  EXPECT_TRUE(result.isEmpty());
}

TEST(BackendControllerNullTest, Supports_ReturnsFalse)
{
  BackendController ctrl{std::shared_ptr<Backend>{}};
  EXPECT_FALSE(ctrl.supports(Backend::Feature::PollInterval));
  EXPECT_FALSE(ctrl.supports(Backend::Feature::ServerScan));
}

TEST(BackendControllerNullTest, GetAlbums_ReturnsEmpty)
{
  BackendController ctrl{std::shared_ptr<Backend>{}};
  EXPECT_TRUE(ctrl.get_albums().isEmpty());
}

TEST(BackendControllerNullTest, CommandsDoNotCrash)
{
  BackendController ctrl{std::shared_ptr<Backend>{}};
  ctrl.disconnect_from_server();
  ctrl.play();
  ctrl.pause();
  ctrl.stop();
  ctrl.prev();
  ctrl.next();
  ctrl.seek(0);
  ctrl.set_volume(50);
  ctrl.play_pos(0);
  ctrl.set_repeat(false, false);
  ctrl.set_shuffle(false);
  ctrl.insert_queue({}, 0);
  ctrl.append_queue({});
  ctrl.replace_queue({}, 0);
  ctrl.remove_from_queue({});
  ctrl.rearrange_queue(0, {});
  ctrl.refresh_library();
  ctrl.set_poll_interval(5);
  ctrl.set_favorite("s1", true);
  ctrl.fetch_favorites();
  ctrl.fetch_album_art("uri");
  ctrl.fetch_playlists();
  ctrl.fetch_playlist_songs("pl1");
  ctrl.create_playlist("New", {});
  ctrl.rename_playlist("pl1", "Renamed");
  ctrl.delete_playlist("pl1");
  ctrl.add_to_playlist("pl1", {});
  ctrl.remove_from_playlist("pl1", {0});
  ctrl.rearrange_playlist("pl1", 2, {0});
  ctrl.fetch_favorites();
  // Should reach here without crash or assertion.
}

// ---------------------------------------------------------------------------
// Signal forwarding — inner emits, controller re-emits
// ---------------------------------------------------------------------------

TEST_F(BackendControllerTest, Signal_ConnectionUpdate)
{
  QSignalSpy spy(ctrl.get(), &Backend::connection_update);
  mock->emit_connection_update(true);
  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(spy.first().first().toBool());
}

TEST_F(BackendControllerTest, Signal_ConnectionUpdate_False)
{
  QSignalSpy spy(ctrl.get(), &Backend::connection_update);
  mock->emit_connection_update(false);
  ASSERT_EQ(spy.count(), 1);
  EXPECT_FALSE(spy.first().first().toBool());
}

TEST_F(BackendControllerTest, Signal_LibraryChanged)
{
  QSignalSpy spy(ctrl.get(), &Backend::library_changed);
  mock->emit_library_changed();
  EXPECT_EQ(spy.count(), 1);
}

// Everything above the backend layer connects to the façade, not to the active
// backend, so a signal the façade forgets to relay is not a partial failure —
// it never arrives, silently, and the per-signal tests below only cover the
// signals somebody remembered to write a test for. favorites_stale shipped
// unrelayed exactly that way: the poll tick stopped refreshing favorites and
// the whole suite stayed green. Enumerating the interface is what catches it.
TEST_F(BackendControllerTest, RelaysEverySignalOnTheBackendInterface)
{
  const auto &mo = Backend::staticMetaObject;
  QStringList unrelayed;
  for (int i = mo.methodOffset(); i < mo.methodCount(); ++i)
    {
      auto method = mo.method(i);
      if (method.methodType() == QMetaMethod::Signal
          && !mock->signal_has_receivers(method))
        unrelayed << QString::fromLatin1(method.methodSignature());
    }

  EXPECT_TRUE(unrelayed.isEmpty())
      << "not relayed by BackendController: " << unrelayed.join(", ").toStdString();
}

TEST_F(BackendControllerTest, Signal_AlbumMappingChanged)
{
  QSignalSpy spy(ctrl.get(), &Backend::album_mapping_changed);
  mock->emit_album_mapping_changed();
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(BackendControllerTest, Signal_LibraryRefreshActive)
{
  QSignalSpy spy(ctrl.get(), &Backend::library_refresh_active);
  mock->emit_library_refresh(true);
  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(spy.first().first().toBool());
}

TEST_F(BackendControllerTest, Signal_PlaybackStateChanged)
{
  QSignalSpy spy(ctrl.get(), &Backend::playback_state_changed);
  PlaybackState s;
  s.state = PlayState::Playing;
  mock->emit_playback_state(s);
  ASSERT_EQ(spy.count(), 1);
}

TEST_F(BackendControllerTest, Signal_CurrentSongChanged)
{
  QSignalSpy spy(ctrl.get(), &Backend::current_song_changed);
  song s;
  s.title = "Test Track";
  mock->emit_current_song(s);
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().first().value<song>().title, "Test Track");
}

TEST_F(BackendControllerTest, Signal_QueueChanged)
{
  QSignalSpy spy(ctrl.get(), &Backend::queue_changed);
  mock->emit_queue({song{}, song{}});
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().first().value<QList<song>>().size(), 2);
}

TEST_F(BackendControllerTest, Signal_SongsChanged)
{
  QSignalSpy spy(ctrl.get(), &Backend::songs_changed);
  mock->emit_songs_changed("abc123");
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().first().toString(), "abc123");
}

TEST_F(BackendControllerTest, Signal_AlbumArtReceived)
{
  QSignalSpy spy(ctrl.get(), &Backend::album_art_received);
  mock->emit_album_art("file:///cover.jpg", QByteArray("\x01\x02\x03", 3));
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().first().toString(), "file:///cover.jpg");
}

TEST_F(BackendControllerTest, Signal_Error)
{
  QSignalSpy spy(ctrl.get(), &Backend::error);
  mock->emit_error("Connection lost");
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().first().toString(), "Connection lost");
}

TEST_F(BackendControllerTest, Signal_FavoritesLoaded)
{
  qRegisterMetaType<QSet<QString>>();
  QSignalSpy spy(ctrl.get(), &Backend::favorites_loaded);
  mock->emit_favorites_loaded({"s1", "s2"});
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().first().value<QSet<QString>>().size(), 2);
}

TEST_F(BackendControllerTest, Signal_FavoriteChanged)
{
  QSignalSpy spy(ctrl.get(), &Backend::favorite_changed);
  mock->emit_favorite_changed("s1", true);
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().at(0).toString(), "s1");
  EXPECT_TRUE(spy.first().at(1).toBool());
}

TEST_F(BackendControllerTest, SetFavorite_ForwardsToActive)
{
  ctrl->set_favorite("s1", true);
  EXPECT_EQ(mock->set_favorite_calls, 1);
  EXPECT_EQ(mock->last_fav_uri, "s1");
  EXPECT_TRUE(mock->last_fav_state);
}

TEST_F(BackendControllerTest, FetchFavorites_ForwardsToActive)
{
  ctrl->fetch_favorites();
  EXPECT_EQ(mock->fetch_favorites_calls, 1);
}

// ---------------------------------------------------------------------------
// Playlists — signal re-emission and command forwarding
// ---------------------------------------------------------------------------

TEST_F(BackendControllerTest, Signal_PlaylistsLoaded)
{
  qRegisterMetaType<QList<playlist_info>>();
  QSignalSpy spy(ctrl.get(), &Backend::playlists_loaded);
  playlist_info pl;
  pl.id = "pl1";
  pl.name = "Road Trip";
  pl.song_count = 12;
  mock->emit_playlists_loaded({pl});
  ASSERT_EQ(spy.count(), 1);
  auto lists = spy.first().first().value<QList<playlist_info>>();
  ASSERT_EQ(lists.size(), 1);
  EXPECT_EQ(lists.first().id, "pl1");
  EXPECT_EQ(lists.first().name, "Road Trip");
  EXPECT_EQ(lists.first().song_count, 12);
}

TEST_F(BackendControllerTest, Signal_PlaylistSongsLoaded)
{
  QSignalSpy spy(ctrl.get(), &Backend::playlist_songs_loaded);
  song s;
  s.title = "Track 1";
  mock->emit_playlist_songs_loaded("pl1", {s});
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().at(0).toString(), "pl1");
  auto songs = spy.first().at(1).value<QList<song>>();
  ASSERT_EQ(songs.size(), 1);
  EXPECT_EQ(songs.first().title, "Track 1");
}

TEST_F(BackendControllerTest, Signal_PlaylistsChanged)
{
  QSignalSpy spy(ctrl.get(), &Backend::playlists_changed);
  mock->emit_playlists_changed("id1");
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().first().toString(), "id1"); // the id must survive the relay
}

TEST_F(BackendControllerTest, Signal_FavoriteSongsLoaded)
{
  QSignalSpy spy(ctrl.get(), &Backend::favorite_songs_loaded);
  song s;
  s.title = "Fav Track";
  mock->emit_favorite_songs_loaded({s});
  ASSERT_EQ(spy.count(), 1);
  auto songs = spy.first().first().value<QList<song>>();
  ASSERT_EQ(songs.size(), 1);
  EXPECT_EQ(songs.first().title, "Fav Track");
}

TEST_F(BackendControllerTest, FetchPlaylists_ForwardsToActive)
{
  ctrl->fetch_playlists();
  EXPECT_EQ(mock->fetch_playlists_calls, 1);
}

TEST_F(BackendControllerTest, FetchPlaylistSongs_ForwardsToActive)
{
  ctrl->fetch_playlist_songs("pl1");
  EXPECT_EQ(mock->fetch_playlist_songs_calls, 1);
  EXPECT_EQ(mock->last_playlist_id, "pl1");
}

TEST_F(BackendControllerTest, CreatePlaylist_ForwardsToActive)
{
  song s;
  s.title = "Track 1";
  ctrl->create_playlist("Road Trip", {s});
  EXPECT_EQ(mock->create_playlist_calls, 1);
  EXPECT_EQ(mock->last_playlist_name, "Road Trip");
  ASSERT_EQ(mock->last_playlist_songs.size(), 1);
  EXPECT_EQ(mock->last_playlist_songs.first().title, "Track 1");
}

TEST_F(BackendControllerTest, RenamePlaylist_ForwardsToActive)
{
  ctrl->rename_playlist("pl1", "New Name");
  EXPECT_EQ(mock->rename_playlist_calls, 1);
  EXPECT_EQ(mock->last_playlist_id, "pl1");
  EXPECT_EQ(mock->last_playlist_name, "New Name");
}

TEST_F(BackendControllerTest, DeletePlaylist_ForwardsToActive)
{
  ctrl->delete_playlist("pl1");
  EXPECT_EQ(mock->delete_playlist_calls, 1);
  EXPECT_EQ(mock->last_playlist_id, "pl1");
}

TEST_F(BackendControllerTest, AddToPlaylist_ForwardsToActive)
{
  song s;
  s.title = "Track 2";
  ctrl->add_to_playlist("pl1", {s});
  EXPECT_EQ(mock->add_to_playlist_calls, 1);
  EXPECT_EQ(mock->last_playlist_id, "pl1");
  ASSERT_EQ(mock->last_playlist_songs.size(), 1);
  EXPECT_EQ(mock->last_playlist_songs.first().title, "Track 2");
}

TEST_F(BackendControllerTest, RemoveFromPlaylist_ForwardsToActive)
{
  ctrl->remove_from_playlist("pl1", {3, 1, 0});
  EXPECT_EQ(mock->remove_from_playlist_calls, 1);
  EXPECT_EQ(mock->last_playlist_id, "pl1");
  EXPECT_EQ(mock->last_playlist_positions, (QList<int>{3, 1, 0}));
}

TEST_F(BackendControllerTest, RearrangePlaylist_ForwardsToActive)
{
  ctrl->rearrange_playlist("pl1", 5, {2, 3});
  EXPECT_EQ(mock->rearrange_playlist_calls, 1);
  EXPECT_EQ(mock->last_playlist_id, "pl1");
  EXPECT_EQ(mock->last_rearrange_target, 5);
  EXPECT_EQ(mock->last_rearrange_moved, (QList<int>{2, 3}));
}

TEST_F(BackendControllerTest, Supports_Playlists_DelegatesToActive)
{
  mock->supports_result = true;
  EXPECT_TRUE(ctrl->supports(Backend::Feature::Playlists));
  EXPECT_EQ(mock->last_supports_feature, Backend::Feature::Playlists);
}

// ---------------------------------------------------------------------------
// Forwarding: get_albums, fetch_songs, supports
// ---------------------------------------------------------------------------

TEST_F(BackendControllerTest, GetAlbums_ForwardsToActive)
{
  album a;
  a.name = "Test Album";
  mock->albums_result = {a};
  auto result = ctrl->get_albums();
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(result.first().name, "Test Album");
}

TEST_F(BackendControllerTest, FetchSongs_ForwardsCallback)
{
  song s;
  s.title = "Track 1";
  mock->songs_result = {s};
  bool called = false;
  QList<song> received;
  ctrl->fetch_songs(album{}, [&](const QList<song> &songs) {
    called = true;
    received = songs;
  });
  EXPECT_TRUE(called);
  ASSERT_EQ(received.size(), 1);
  EXPECT_EQ(received.first().title, "Track 1");
}

TEST_F(BackendControllerTest, Supports_DelegatesToActive_True)
{
  mock->supports_result = true;
  EXPECT_TRUE(ctrl->supports(Backend::Feature::PollInterval));
  EXPECT_EQ(mock->last_supports_feature, Backend::Feature::PollInterval);
}

TEST_F(BackendControllerTest, Supports_DelegatesToActive_False)
{
  mock->supports_result = false;
  EXPECT_FALSE(ctrl->supports(Backend::Feature::ServerScan));
}

// ---------------------------------------------------------------------------
// Command forwarding
// ---------------------------------------------------------------------------

TEST_F(BackendControllerTest, Connect_ForwardsToActive)
{
  mock->connect_result = true;
  EXPECT_TRUE(ctrl->connect_to_server());
  EXPECT_EQ(mock->connect_calls, 1);
}

TEST_F(BackendControllerTest, Disconnect_ForwardsToActive)
{
  ctrl->disconnect_from_server();
  EXPECT_EQ(mock->disconnect_calls, 1);
}

TEST_F(BackendControllerTest, Play_ForwardsToActive)
{
  ctrl->play();
  EXPECT_EQ(mock->play_calls, 1);
}

TEST_F(BackendControllerTest, Pause_ForwardsToActive)
{
  ctrl->pause();
  EXPECT_EQ(mock->pause_calls, 1);
}

TEST_F(BackendControllerTest, Stop_ForwardsToActive)
{
  ctrl->stop();
  EXPECT_EQ(mock->stop_calls, 1);
}

TEST_F(BackendControllerTest, Seek_ForwardsWithCorrectArg)
{
  ctrl->seek(42000);
  EXPECT_EQ(mock->last_seek, 42000u);
}

TEST_F(BackendControllerTest, SetVolume_ForwardsToActive)
{
  ctrl->set_volume(73);
  EXPECT_EQ(mock->last_volume, 73);
}

TEST_F(BackendControllerTest, SetRepeat_ForwardsToActive)
{
  ctrl->set_repeat(true, false);
  EXPECT_TRUE(mock->last_repeat);
  EXPECT_FALSE(mock->last_single);
}

TEST_F(BackendControllerTest, SetShuffle_ForwardsToActive)
{
  ctrl->set_shuffle(true);
  EXPECT_TRUE(mock->last_shuffle);
}

TEST_F(BackendControllerTest, RefreshLibrary_ForwardsToActive)
{
  ctrl->refresh_library();
  EXPECT_EQ(mock->refresh_calls, 1);
}

TEST_F(BackendControllerTest, SetPollInterval_ForwardsToActive)
{
  ctrl->set_poll_interval(15);
  EXPECT_EQ(mock->last_poll_interval, 15);
}

TEST_F(BackendControllerTest, FetchAlbumArt_ForwardsToActive)
{
  ctrl->fetch_album_art("some/uri");
  EXPECT_EQ(mock->fetch_art_calls, 1);
  EXPECT_EQ(mock->last_art_uri, "some/uri");
}

// ---------------------------------------------------------------------------
// do_switch: switching backends
// ---------------------------------------------------------------------------

class BackendControllerSwitchTest : public ::testing::Test {
protected:
  std::shared_ptr<StubBackend> stub1;
  std::shared_ptr<StubBackend> stub2;
  std::shared_ptr<LibraryManager> libman;
  std::unique_ptr<BackendController> ctrl;

  void SetUp() override
  {
    stub1 = std::make_shared<StubBackend>();
    stub2 = std::make_shared<StubBackend>();
    libman = std::make_shared<LibraryManager>();
    ctrl = std::make_unique<BackendController>(stub1, libman);
  }
};

TEST_F(BackendControllerSwitchTest, DoSwitch_EmitsBackendSwitching)
{
  QSignalSpy spy(ctrl.get(), &BackendController::backend_switching);
  ctrl->do_switch(stub2, "new-id");
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(BackendControllerSwitchTest, DoSwitch_BackendSwitchedFiresAfterAttach)
{
  // supports() at backend_switched time must query the NEW backend — this is
  // what distinguishes it from backend_switching (emitted before teardown).
  stub1->supports_result = false;
  stub2->supports_result = true;

  int emissions = 0;
  bool supports_at_emit = false;
  QObject::connect(ctrl.get(), &BackendController::backend_switched,
      [&] {
        ++emissions;
        supports_at_emit = ctrl->supports(Backend::Feature::PollInterval);
      });

  ctrl->do_switch(stub2, "new-id");
  EXPECT_EQ(emissions, 1);
  EXPECT_TRUE(supports_at_emit);
}

TEST_F(BackendControllerSwitchTest, DoSwitch_OldSignalsAreSevered)
{
  // Attach stub2, then verify stub1 signals no longer propagate.
  ctrl->do_switch(stub2, "new-id");

  QSignalSpy spy(ctrl.get(), &Backend::library_changed);
  stub1->emit_library_changed();
  EXPECT_EQ(spy.count(), 0);
}

TEST_F(BackendControllerSwitchTest, DoSwitch_NewSignalsAreForwarded)
{
  ctrl->do_switch(stub2, "new-id");

  QSignalSpy spy(ctrl.get(), &Backend::library_changed);
  stub2->emit_library_changed();
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(BackendControllerSwitchTest, DoSwitch_CallsConnectOnNewBackend)
{
  ctrl->do_switch(stub2, "new-id");
  EXPECT_EQ(stub2->connect_calls, 1);
}

TEST_F(BackendControllerSwitchTest, DoSwitch_DoesNotCallConnectOnOldBackend)
{
  stub1->connect_calls = 0;
  ctrl->do_switch(stub2, "new-id");
  EXPECT_EQ(stub1->connect_calls, 0);
}

TEST_F(BackendControllerSwitchTest, DoSwitch_ClearsLibman)
{
  // Pre-populate libman with an album.
  album a;
  a.album_hash = "abc";
  a.name = "Test Album";
  libman->add_album("abc", a);
  ASSERT_FALSE(libman->get_albums().isEmpty());

  ctrl->do_switch(stub2, "new-id");
  EXPECT_TRUE(libman->get_albums().isEmpty());
}

TEST_F(BackendControllerSwitchTest, DoSwitch_GetAlbumsReturnNewBackend)
{
  album a;
  a.name = "New Album";
  stub2->albums_result = {a};
  ctrl->do_switch(stub2, "new-id");
  EXPECT_EQ(ctrl->get_albums().first().name, "New Album");
}

TEST_F(BackendControllerSwitchTest, DoSwitch_ForwardsSongsChangedFromNew)
{
  ctrl->do_switch(stub2, "new-id");

  QSignalSpy spy(ctrl.get(), &Backend::songs_changed);
  stub2->emit_songs_changed("hash123");
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().first().toString(), "hash123");
}

TEST_F(BackendControllerSwitchTest, DoSwitch_ForwardsConnectionUpdateFromNew)
{
  ctrl->do_switch(stub2, "new-id");

  QSignalSpy spy(ctrl.get(), &Backend::connection_update);
  stub2->emit_connection_update(true);
  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(spy.first().first().toBool());
}

TEST_F(BackendControllerSwitchTest, DoSwitch_OldBackendDisconnectCalled)
{
  ctrl->do_switch(stub2, "new-id");
  // disconnect_from_server() slot on stub1 must have been called to close the connection.
  EXPECT_EQ(stub1->disconnect_calls, 1);
}

TEST_F(BackendControllerSwitchTest, DoSwitch_NoSpuriousConnectionUpdateFromOld)
{
  // After detach, signals from stub1 must not pass through — even if it emits
  // connection_update(false) during teardown.
  QSignalSpy spy(ctrl.get(), &Backend::connection_update);
  ctrl->do_switch(stub2, "new-id");
  // stub1's disconnect_from_server() was called; if signals leaked that would appear here.
  // Only the connect_to_server() call on stub2 should be able to add entries.
  // stub2.connect_result defaults true but emits no connection_update by itself.
  EXPECT_EQ(spy.count(), 0);
}

TEST_F(BackendControllerSwitchTest, DoSwitch_PlaylistCommandsForwardToNew)
{
  ctrl->do_switch(stub2, "new-id");

  ctrl->fetch_playlists();
  ctrl->fetch_playlist_songs("pl1");
  ctrl->fetch_favorites();
  EXPECT_EQ(stub2->fetch_playlists_calls, 1);
  EXPECT_EQ(stub2->fetch_playlist_songs_calls, 1);
  EXPECT_EQ(stub2->last_playlist_id, "pl1");
  EXPECT_EQ(stub2->fetch_favorites_calls, 1);
  EXPECT_EQ(stub1->fetch_playlists_calls, 0);
  EXPECT_EQ(stub1->fetch_playlist_songs_calls, 0);
  EXPECT_EQ(stub1->fetch_favorites_calls, 0);
}

TEST_F(BackendControllerSwitchTest, DoSwitch_PlaylistSignalsForwardedFromNew)
{
  ctrl->do_switch(stub2, "new-id");

  QSignalSpy loaded_spy(ctrl.get(), &Backend::playlists_loaded);
  QSignalSpy changed_spy(ctrl.get(), &Backend::playlists_changed);
  QSignalSpy songs_spy(ctrl.get(), &Backend::playlist_songs_loaded);
  QSignalSpy fav_spy(ctrl.get(), &Backend::favorite_songs_loaded);
  stub2->emit_playlists_loaded({playlist_info{}});
  stub2->emit_playlists_changed();
  stub2->emit_playlist_songs_loaded("pl1", {});
  stub2->emit_favorite_songs_loaded({});
  EXPECT_EQ(loaded_spy.count(), 1);
  EXPECT_EQ(changed_spy.count(), 1);
  EXPECT_EQ(songs_spy.count(), 1);
  EXPECT_EQ(fav_spy.count(), 1);
}

TEST_F(BackendControllerSwitchTest, DoSwitch_OldPlaylistSignalsAreSevered)
{
  ctrl->do_switch(stub2, "new-id");

  QSignalSpy loaded_spy(ctrl.get(), &Backend::playlists_loaded);
  QSignalSpy changed_spy(ctrl.get(), &Backend::playlists_changed);
  QSignalSpy songs_spy(ctrl.get(), &Backend::playlist_songs_loaded);
  QSignalSpy fav_spy(ctrl.get(), &Backend::favorite_songs_loaded);
  stub1->emit_playlists_loaded({playlist_info{}});
  stub1->emit_playlists_changed();
  stub1->emit_playlist_songs_loaded("pl1", {});
  stub1->emit_favorite_songs_loaded({});
  EXPECT_EQ(loaded_spy.count(), 0);
  EXPECT_EQ(changed_spy.count(), 0);
  EXPECT_EQ(songs_spy.count(), 0);
  EXPECT_EQ(fav_spy.count(), 0);
}

TEST_F(BackendControllerSwitchTest, DoSwitch_WithNullNewActive_NoCrash)
{
  // Switching to a null backend (failed factory) must not crash.
  ctrl->do_switch(nullptr, "bad-id");
  // Commands on null active should be no-ops.
  ctrl->play();
  EXPECT_EQ(stub2->play_calls, 0);
}

TEST_F(BackendControllerSwitchTest, SwitchTo_SameId_IsNoOp)
{
  // do_switch was used to set active_id = "orig"; a switch_to that id is a no-op.
  ctrl->do_switch(stub1, "orig");
  stub1->connect_calls = 0;

  QSignalSpy spy(ctrl.get(), &BackendController::backend_switching);
  ctrl->switch_to("orig");
  EXPECT_EQ(spy.count(), 0);
  EXPECT_EQ(stub1->connect_calls, 0);
}

TEST_F(BackendControllerSwitchTest, SwitchTo_UnknownId_LeavesActiveIntact)
{
  // switch_to with a non-existent id must leave the current backend running.
  QSignalSpy spy(ctrl.get(), &BackendController::backend_switching);
  ctrl->switch_to("does-not-exist");
  EXPECT_EQ(spy.count(), 0);

  // Verify stub1 signals still reach ctrl.
  QSignalSpy lib_spy(ctrl.get(), &Backend::library_changed);
  stub1->emit_library_changed();
  EXPECT_EQ(lib_spy.count(), 1);
}

// ---------------------------------------------------------------------------
// revert_to_previous: backing out of a switch
// ---------------------------------------------------------------------------

TEST_F(BackendControllerSwitchTest, RevertToPrevious_WithNothingToGoBackTo_ReturnsFalse)
{
  // Nothing has ever connected, so cancelling the first connect has no earlier
  // session to restore — the caller falls back to Settings on this answer.
  QSignalSpy spy(ctrl.get(), &BackendController::backend_switching);
  EXPECT_FALSE(ctrl->revert_to_previous());
  EXPECT_EQ(spy.count(), 0);
}

TEST_F(BackendControllerSwitchTest, RevertToPrevious_SkipsProfilesThatNeverConnected)
{
  // A switch away from a backend that never came up must not record it as the
  // session worth returning to: cancelling out of a second failed attempt would
  // otherwise land back on the first failure.
  ctrl->do_switch(stub2, "never-connected");
  QSignalSpy spy(ctrl.get(), &BackendController::backend_switching);
  EXPECT_FALSE(ctrl->revert_to_previous());
  EXPECT_EQ(spy.count(), 0);
}

#ifdef ENABLE_MPD
TEST_F(BackendControllerSwitchTest, RevertToPrevious_RestoresTheLastConnectedProfile)
{
  QTemporaryDir tmp;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
  QCoreApplication::setOrganizationName("freewave_test");
  QCoreApplication::setApplicationName("backendcontroller_revert_test");
  ProfileStore().save({{.id = "orig", .type = "mpd", .name = "Original"}});

  // "orig" reached a connected state, so switching away from it is what gets
  // recorded as the session worth returning to.
  ctrl->do_switch(stub1, "orig");
  stub1->emit_connection_update(true);
  ASSERT_TRUE(ctrl->is_connected());

  ctrl->do_switch(stub2, "new-id");
  ASSERT_FALSE(ctrl->is_connected());

  QSignalSpy spy(ctrl.get(), &BackendController::backend_switching);
  EXPECT_TRUE(ctrl->revert_to_previous());
  EXPECT_EQ(spy.count(), 1);
  EXPECT_EQ(ProfileStore().active_id(), "orig");
}
#endif

// ---------------------------------------------------------------------------
// is_connected: "active" is not "connected"
// ---------------------------------------------------------------------------

TEST_F(BackendControllerSwitchTest, IsConnected_FalseBeforeAnyConnectionUpdate)
{
  // Attaching a backend selects it; it says nothing about reaching the server.
  EXPECT_FALSE(ctrl->is_connected());
}

TEST_F(BackendControllerSwitchTest, IsConnected_TracksConnectionUpdate)
{
  stub1->emit_connection_update(true);
  EXPECT_TRUE(ctrl->is_connected());

  stub1->emit_connection_update(false);
  EXPECT_FALSE(ctrl->is_connected());
}

TEST_F(BackendControllerSwitchTest, IsConnected_FreshAtConnectionUpdateEmission)
{
  // The Servers list rebuilds from connection_update and reads is_connected()
  // in that same slot, so the flag must already be updated when it fires.
  bool seen = false;
  QObject::connect(ctrl.get(), &Backend::connection_update,
      [&](bool) { seen = ctrl->is_connected(); });

  stub1->emit_connection_update(true);
  EXPECT_TRUE(seen);
}

TEST_F(BackendControllerSwitchTest, DoSwitch_ClearsConnectedForNewBackend)
{
  stub1->emit_connection_update(true);
  ASSERT_TRUE(ctrl->is_connected());

  // The new backend has not connected yet — a stale true here is what made a
  // failed profile look live and hid the Retry button.
  ctrl->do_switch(stub2, "new-id");
  EXPECT_FALSE(ctrl->is_connected());

  stub2->emit_connection_update(true);
  EXPECT_TRUE(ctrl->is_connected());
}

#include "test_backendcontroller.moc"
