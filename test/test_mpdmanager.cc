// Tests for MpdManager that do not require a live MPD instance.
//
// Private slots are invoked via QMetaObject::invokeMethod() with
// Qt::DirectConnection so they execute synchronously on the test thread.
// Qt ignores C++ access specifiers for meta-object invocation, so private
// slots are reachable this way.

#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>

#include <mpd/client.h>

#include "controller/backend.hh"
#include "controller/mpdmanager.hh"
#include "controller/librarymanager.hh"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build an mpd_song* using the public libmpdclient API.
static auto
make_raw_song(const char *uri,
    std::initializer_list<std::pair<const char *, const char *>> pairs = {})
    -> mpd_song *
{
  mpd_pair fp{"file", uri};
  mpd_song *s = mpd_song_begin(&fp);
  if (!s) return nullptr;
  for (auto &[name, val] : pairs)
    {
      mpd_pair p{name, val};
      mpd_song_feed(s, &p);
    }
  return s;
}

// Build an mpd::song (copies data out of the raw C struct then frees it).
static auto
make_mpd_song(const char *uri,
    std::initializer_list<std::pair<const char *, const char *>> pairs = {})
    -> mpd::song
{
  mpd_song *raw = make_raw_song(uri, pairs);
  mpd::song s(raw);
  mpd_song_free(raw);
  return s;
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class MpdManagerTest : public ::testing::Test {
protected:
  std::shared_ptr<LibraryManager> libman;
  std::unique_ptr<MpdManager> mpdman;

  void SetUp() override
  {
    libman = std::make_shared<LibraryManager>();
    mpdman = std::make_unique<MpdManager>(libman);
  }
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_F(MpdManagerTest, DefaultConstructed_NoCrash)
{
  SUCCEED();
}

TEST_F(MpdManagerTest, ConstructorWithHostPort_NoCrash)
{
  MpdManager m(libman, "myhost.local", 7700);
  SUCCEED();
}

TEST_F(MpdManagerTest, ConstructorWithHostPort_ConnectNoCrash)
{
  // Verifies that connect_to_server() uses stored host/port (dispatches to worker) without crashing.
  // The worker thread has no live connection; the dispatch is a no-op but must not crash.
  MpdManager m(libman, "localhost", 6600);
  m.connect_to_server();
  SUCCEED();
}

TEST_F(MpdManagerTest, GetAlbums_InitiallyEmpty)
{
  EXPECT_TRUE(mpdman->get_albums().isEmpty());
}

// ---------------------------------------------------------------------------
// Connection state signals
// ---------------------------------------------------------------------------

// on_connected() sets connected=true and emits connection_update(true).
TEST_F(MpdManagerTest, OnConnected_EmitsConnectionUpdateTrue)
{
  QSignalSpy spy(mpdman.get(), &MpdManager::connection_update);

  QMetaObject::invokeMethod(mpdman.get(), "on_connected",
      Qt::DirectConnection);

  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(spy.takeFirst()[0].toBool());
}

// on_disconnected() sets connected=false and emits connection_update(false).
TEST_F(MpdManagerTest, OnDisconnected_EmitsConnectionUpdateFalse)
{
  QSignalSpy spy(mpdman.get(), &MpdManager::connection_update);

  QMetaObject::invokeMethod(mpdman.get(), "on_disconnected",
      Qt::DirectConnection);

  ASSERT_EQ(spy.count(), 1);
  EXPECT_FALSE(spy.takeFirst()[0].toBool());
}

// ---------------------------------------------------------------------------
// on_all_songs — LibraryManager population
// ---------------------------------------------------------------------------

/// Empty song list: LibraryManager stays empty, library_changed() fired.
TEST_F(MpdManagerTest, OnAllSongs_Empty_EmitsLibraryChanged)
{
  QSignalSpy spy(mpdman.get(), &Backend::library_changed);

  std::vector<mpd::song> empty;
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, empty));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(mpdman->get_albums().isEmpty());
}

/// Album hashes are computed per song from its own tags, so every library load
/// re-establishes the mapping standalone songs resolve through: MPD reports it
/// unconditionally alongside library_changed.
TEST_F(MpdManagerTest, OnAllSongs_EmitsAlbumMappingChanged)
{
  QSignalSpy spy(mpdman.get(), &Backend::album_mapping_changed);

  std::vector<mpd::song> songs{make_mpd_song("artist/album/01.flac", {
                                                                         {"Title", "Track One"},
                                                                         {"Artist", "My Artist"},
                                                                         {"Album", "My Album"},
                                                                     })};
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, songs));

  EXPECT_EQ(spy.count(), 1);
}

// Two songs from the same album → exactly one album in LibraryManager.
TEST_F(MpdManagerTest, OnAllSongs_WithSongs_PopulatesLibraryManager)
{
  auto s1 = make_mpd_song("artist/album/01.flac", {
                                                      {"Title", "Track One"},
                                                      {"Artist", "My Artist"},
                                                      {"Album", "My Album"},
                                                      {"Date", "2020"},
                                                  });
  auto s2 = make_mpd_song("artist/album/02.flac", {
                                                      {"Title", "Track Two"},
                                                      {"Artist", "My Artist"},
                                                      {"Album", "My Album"},
                                                      {"Date", "2020"},
                                                  });

  std::vector<mpd::song> songs{s1, s2};
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, songs));

  auto albums = mpdman->get_albums();
  ASSERT_EQ(albums.size(), 1);
  EXPECT_EQ(albums.first().name, "My Album");
}

// Songs from two distinct albums → two albums in LibraryManager.
TEST_F(MpdManagerTest, OnAllSongs_MultipleAlbums_PopulatesCorrectly)
{
  auto s1 = make_mpd_song("a/b/01.flac", {
                                             {"Artist", "Artist A"},
                                             {"Album", "Album A"},
                                             {"Date", "2020"},
                                         });
  auto s2 = make_mpd_song("c/d/01.flac", {
                                             {"Artist", "Artist B"},
                                             {"Album", "Album B"},
                                             {"Date", "2021"},
                                         });

  std::vector<mpd::song> songs{s1, s2};
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, songs));

  EXPECT_EQ(mpdman->get_albums().size(), 2);
}

// The MusicBrainz Album Id is lowercased so the same release keys identically
// whether the tag is upper/lower/mixed case, and so it matches whatever case
// Subsonic/LMS happens to report for the same tag.
TEST_F(MpdManagerTest, OnAllSongs_MusicBrainzAlbumId_LowercasedInHash)
{
  auto s1 = make_mpd_song("a/b/01.flac", {
                                             {"Artist", "Artist A"},
                                             {"Album", "Album A"},
                                             {"Date", "2020"},
                                             {"MUSICBRAINZ_ALBUMID", "B00A1179-AF97-41D9-8C81-BDE2C5DABBEB"},
                                         });

  std::vector<mpd::song> songs{s1};
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, songs));

  auto albums = mpdman->get_albums();
  ASSERT_EQ(albums.size(), 1);
  EXPECT_EQ(albums.first().album_hash, "b00a1179-af97-41d9-8c81-bde2c5dabbeb");
}

// The album's "date added" surrogate prefers MPD's Added tag (0.24+) over
// Last-Modified (the file's mtime) when the server supplies both.
TEST_F(MpdManagerTest, OnAllSongs_AddedTag_PreferredOverLastModified)
{
  auto s1 = make_mpd_song("a/b/01.flac", {
                                             {"Artist", "Artist A"},
                                             {"Album", "Album A"},
                                             {"Date", "2020"},
                                             {"Last-Modified", "2020-01-01T00:00:00Z"},
                                             {"Added", "2024-06-15T12:00:00Z"},
                                         });

  std::vector<mpd::song> songs{s1};
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, songs));

  auto albums = mpdman->get_albums();
  ASSERT_EQ(albums.size(), 1);
  EXPECT_EQ(albums.first().last_modified,
      QDateTime::fromString("2024-06-15T12:00:00Z", Qt::ISODate));
}

// A server predating MPD 0.24 sends no Added tag (mpd_song_get_added() == 0);
// the surrogate must fall back to Last-Modified rather than the 1970 epoch.
TEST_F(MpdManagerTest, OnAllSongs_NoAddedTag_FallsBackToLastModified)
{
  auto s1 = make_mpd_song("a/b/01.flac", {
                                             {"Artist", "Artist A"},
                                             {"Album", "Album A"},
                                             {"Date", "2020"},
                                             {"Last-Modified", "2020-01-01T00:00:00Z"},
                                         });

  std::vector<mpd::song> songs{s1};
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, songs));

  auto albums = mpdman->get_albums();
  ASSERT_EQ(albums.size(), 1);
  EXPECT_EQ(albums.first().last_modified,
      QDateTime::fromString("2020-01-01T00:00:00Z", Qt::ISODate));
}

// A compilation whose tracks share album/album-artist/release-date but carry
// differing (or missing) OriginalDate values must stay ONE album. The album
// key uses the release Date only; OriginalDate is per-track recording
// provenance and must not influence album identity.
TEST_F(MpdManagerTest, OnAllSongs_VaryingOriginalDate_GroupsAsOneAlbum)
{
  auto s1 = make_mpd_song("comp/01.flac", {
                                              {"Album", "Singles etc"},
                                              {"AlbumArtist", "David Sylvian"},
                                              {"Date", "2022"},
                                              {"OriginalDate", "1987"},
                                          });
  auto s2 = make_mpd_song("comp/02.flac", {
                                              {"Album", "Singles etc"},
                                              {"AlbumArtist", "David Sylvian"},
                                              {"Date", "2022"},
                                              {"OriginalDate", "1993"},
                                          });
  auto s3 = make_mpd_song("comp/03.flac", {
                                              {"Album", "Singles etc"},
                                              {"AlbumArtist", "David Sylvian"},
                                              {"Date", "2022"},
                                              // no OriginalDate at all
                                          });

  std::vector<mpd::song> songs{s1, s2, s3};
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, songs));

  ASSERT_EQ(mpdman->get_albums().size(), 1);
  EXPECT_EQ(mpdman->get_albums().first().name, "Singles etc");
}

// Disambiguation is preserved: two releases with the same name and album-artist
// but different *release* Date remain two distinct albums.
TEST_F(MpdManagerTest, OnAllSongs_SameNameDifferentReleaseDate_StaysSplit)
{
  auto s1 = make_mpd_song("live/87.flac", {
                                              {"Album", "Live"},
                                              {"AlbumArtist", "Some Artist"},
                                              {"Date", "1987"},
                                          });
  auto s2 = make_mpd_song("live/99.flac", {
                                              {"Album", "Live"},
                                              {"AlbumArtist", "Some Artist"},
                                              {"Date", "1999"},
                                          });

  std::vector<mpd::song> songs{s1, s2};
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, songs));

  EXPECT_EQ(mpdman->get_albums().size(), 2);
}

// Second call to on_all_songs replaces old library data.
TEST_F(MpdManagerTest, OnAllSongs_ClearsOldDataBeforePopulating)
{
  auto s1 = make_mpd_song("a/b/01.flac", {
                                             {"Artist", "Artist A"},
                                             {"Album", "Album A"},
                                             {"Date", "2020"},
                                         });
  std::vector<mpd::song> first{s1};
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, first));
  ASSERT_EQ(mpdman->get_albums().size(), 1);

  auto s2 = make_mpd_song("c/d/01.flac", {
                                             {"Artist", "Artist B"},
                                             {"Album", "Album B"},
                                             {"Date", "2021"},
                                         });
  auto s3 = make_mpd_song("c/d/02.flac", {
                                             {"Artist", "Artist B"},
                                             {"Album", "Album B"},
                                             {"Date", "2021"},
                                         });
  std::vector<mpd::song> second{s2, s3};
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, second));

  // Only the new album remains; the old one is gone.
  EXPECT_EQ(mpdman->get_albums().size(), 1);
}

// ---------------------------------------------------------------------------
// on_queue
// ---------------------------------------------------------------------------

TEST_F(MpdManagerTest, OnQueue_Empty_EmitsQueueModifiedEmpty)
{
  QSignalSpy spy(mpdman.get(), &Backend::queue_changed);

  std::vector<mpd::song> empty;
  QMetaObject::invokeMethod(mpdman.get(), "on_queue",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, empty));

  ASSERT_EQ(spy.count(), 1);
  auto list = spy.takeFirst()[0].value<QList<song>>();
  EXPECT_TRUE(list.isEmpty());
}

TEST_F(MpdManagerTest, OnQueue_WithSongs_EmitsQueueModified)
{
  QSignalSpy spy(mpdman.get(), &Backend::queue_changed);

  auto s = make_mpd_song("a/b.flac", {{"Title", "T"}, {"Artist", "A"}});
  std::vector<mpd::song> songs{s};
  QMetaObject::invokeMethod(mpdman.get(), "on_queue",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, songs));

  ASSERT_EQ(spy.count(), 1);
  auto list = spy.takeFirst()[0].value<QList<song>>();
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list[0].uri, "a/b.flac");
}

// ---------------------------------------------------------------------------
// on_current_song
// ---------------------------------------------------------------------------

TEST_F(MpdManagerTest, OnCurrentSong_WithSong_EmitsCurrentSong)
{
  QSignalSpy spy(mpdman.get(), &Backend::current_song_changed);

  auto s = make_mpd_song("x/y.flac", {{"Title", "MySong"}});
  std::optional<mpd::song> opt{s};
  QMetaObject::invokeMethod(mpdman.get(), "on_current_song",
      Qt::DirectConnection,
      Q_ARG(std::optional<mpd::song>, opt));

  ASSERT_EQ(spy.count(), 1);
  auto result = spy.takeFirst()[0].value<song>();
  EXPECT_EQ(result.uri, "x/y.flac");
}

// nullopt → signal must NOT be emitted.
TEST_F(MpdManagerTest, OnCurrentSong_WithNullopt_DoesNotEmit)
{
  QSignalSpy spy(mpdman.get(), &Backend::current_song_changed);

  std::optional<mpd::song> opt{std::nullopt};
  QMetaObject::invokeMethod(mpdman.get(), "on_current_song",
      Qt::DirectConnection,
      Q_ARG(std::optional<mpd::song>, opt));

  EXPECT_EQ(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// on_status
// ---------------------------------------------------------------------------

// on_status emits playback_state_changed.
TEST_F(MpdManagerTest, OnStatus_EmitsPlaybackStateChanged)
{
  QSignalSpy spy(mpdman.get(), &Backend::playback_state_changed);

  mpd::status s;
  s.update_id = 0;
  QMetaObject::invokeMethod(mpdman.get(), "on_status",
      Qt::DirectConnection,
      Q_ARG(mpd::status, s));

  EXPECT_EQ(spy.count(), 1);
}

// update_id > 0 → DB update started → library_refresh_active(true) emitted.
TEST_F(MpdManagerTest, OnStatus_DbUpdateStart_EmitsLibraryRefreshActive)
{
  QSignalSpy spy(mpdman.get(), &Backend::library_refresh_active);

  mpd::status s;
  s.update_id = 42;
  QMetaObject::invokeMethod(mpdman.get(), "on_status",
      Qt::DirectConnection,
      Q_ARG(mpd::status, s));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.takeFirst()[0].toBool(), true);
}

// update_id drops to 0 after being > 0 → finish is deferred; signal not yet emitted.
TEST_F(MpdManagerTest, OnStatus_DbUpdateFinish_DefersPendingFlag)
{
  // Start an update
  {
    mpd::status start;
    start.update_id = 1;
    QMetaObject::invokeMethod(mpdman.get(), "on_status",
        Qt::DirectConnection,
        Q_ARG(mpd::status, start));
  }

  // Now watch for the deferred finish signal
  QSignalSpy spy(mpdman.get(), &Backend::library_refresh_active);

  // Finish the update (update_id → 0)
  {
    mpd::status finish;
    finish.update_id = 0;
    QMetaObject::invokeMethod(mpdman.get(), "on_status",
        Qt::DirectConnection,
        Q_ARG(mpd::status, finish));
  }

  // Must NOT be emitted yet — deferred until on_all_songs repopulates.
  EXPECT_EQ(spy.count(), 0);
}

// After db update finishes, on_all_songs emits the deferred library_refresh_active(false).
TEST_F(MpdManagerTest, OnAllSongs_AfterDbFinish_EmitsDeferredLibraryRefreshActive)
{
  // Sequence: update start → update finish → on_all_songs
  {
    mpd::status start;
    start.update_id = 7;
    QMetaObject::invokeMethod(mpdman.get(), "on_status",
        Qt::DirectConnection,
        Q_ARG(mpd::status, start));
  }
  {
    mpd::status finish;
    finish.update_id = 0;
    QMetaObject::invokeMethod(mpdman.get(), "on_status",
        Qt::DirectConnection,
        Q_ARG(mpd::status, finish));
  }

  QSignalSpy spy(mpdman.get(), &Backend::library_refresh_active);

  std::vector<mpd::song> empty;
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, empty));

  ASSERT_EQ(spy.count(), 1);
  // active == false means the refresh is complete.
  EXPECT_EQ(spy.takeFirst()[0].toBool(), false);
}

// Calling on_all_songs without a prior db update does NOT emit library_refresh_active.
TEST_F(MpdManagerTest, OnAllSongs_WithoutDbUpdate_DoesNotEmitLibraryRefreshActive)
{
  QSignalSpy spy(mpdman.get(), &Backend::library_refresh_active);

  std::vector<mpd::song> empty;
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, empty));

  EXPECT_EQ(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// handle_idle — no live connection; dispatched lambdas run safely as no-ops.
// ---------------------------------------------------------------------------

TEST_F(MpdManagerTest, HandleIdle_AllKnownBits_NoCrash)
{
  // The dispatched fetch calls hit run_command which guards conn==nullptr.
  // We just verify no crash; async effects are drained when MpdManager
  // destructor calls worker_thread.quit() + wait().
  QMetaObject::invokeMethod(mpdman.get(), "handle_idle",
      Qt::DirectConnection,
      Q_ARG(unsigned int, MPD_IDLE_QUEUE));
  QMetaObject::invokeMethod(mpdman.get(), "handle_idle",
      Qt::DirectConnection,
      Q_ARG(unsigned int, MPD_IDLE_PLAYER));
  QMetaObject::invokeMethod(mpdman.get(), "handle_idle",
      Qt::DirectConnection,
      Q_ARG(unsigned int, MPD_IDLE_MIXER));
  QMetaObject::invokeMethod(mpdman.get(), "handle_idle",
      Qt::DirectConnection,
      Q_ARG(unsigned int, MPD_IDLE_OPTIONS));
  QMetaObject::invokeMethod(mpdman.get(), "handle_idle",
      Qt::DirectConnection,
      Q_ARG(unsigned int, MPD_IDLE_UPDATE));
  QMetaObject::invokeMethod(mpdman.get(), "handle_idle",
      Qt::DirectConnection,
      Q_ARG(unsigned int, MPD_IDLE_DATABASE));
  QMetaObject::invokeMethod(mpdman.get(), "handle_idle",
      Qt::DirectConnection,
      Q_ARG(unsigned int, MPD_IDLE_STICKER));
  SUCCEED();
}

// ---------------------------------------------------------------------------
// Favorites (stickers)
// ---------------------------------------------------------------------------

// Stickers are assumed available until a server tells us otherwise.
TEST_F(MpdManagerTest, SupportsFavorites_TrueByDefault)
{
  EXPECT_TRUE(mpdman->supports(Backend::Feature::Favorites));
}

// on_favorites re-emits the worker's set through the Backend favorites_loaded
// signal verbatim.
TEST_F(MpdManagerTest, OnFavorites_EmitsFavoritesLoaded)
{
  QSignalSpy spy(mpdman.get(), &Backend::favorites_loaded);

  QSet<QString> favs{"a/b.flac", "c/d.flac"};
  QMetaObject::invokeMethod(mpdman.get(), "on_favorites",
      Qt::DirectConnection, Q_ARG(QSet<QString>, favs));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.takeFirst().at(0).value<QSet<QString>>(), favs);
}

// MPD_IDLE_STICKER carries no song id, so the manager schedules a debounced
// full refetch. With no live connection the worker's fetch_favorites returns an
// empty set, but the round trip still surfaces as favorites_loaded — proving
// the idle → debounce → dispatch → re-emit wiring end to end.
// A sticker idle event names no song, so all the backend can say is that the
// server's favorites may have moved — debounced, because a batch of sticker
// writes arrives as a burst. Reading them is the favorites owner's call.
TEST_F(MpdManagerTest, HandleIdleSticker_ReportsFavoritesStale)
{
  QSignalSpy spy(mpdman.get(), &Backend::favorites_stale);

  QMetaObject::invokeMethod(mpdman.get(), "handle_idle",
      Qt::DirectConnection, Q_ARG(unsigned int, MPD_IDLE_STICKER));
  QMetaObject::invokeMethod(mpdman.get(), "handle_idle",
      Qt::DirectConnection, Q_ARG(unsigned int, MPD_IDLE_STICKER));

  QElapsedTimer t;
  t.start();
  while (spy.count() < 1 && t.elapsed() < 3000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);

  EXPECT_EQ(spy.count(), 1); // two events in the window, one report
}

// ---------------------------------------------------------------------------
// Stored playlists
// ---------------------------------------------------------------------------

// Stored playlists are core MPD; support is advertised unconditionally.
TEST_F(MpdManagerTest, SupportsPlaylists_True)
{
  EXPECT_TRUE(mpdman->supports(Backend::Feature::Playlists));
}

// on_playlists bridges the worker's summaries to the Backend interface:
// id == name, counts unknown (-1) until contents load, epoch seconds converted
// to a QDateTime (0 → invalid).
TEST_F(MpdManagerTest, OnPlaylists_BridgesSummariesToPlaylistInfo)
{
  QSignalSpy spy(mpdman.get(), &Backend::playlists_loaded);

  std::vector<mpd::playlist_summary> summaries;
  summaries.emplace_back("Road Trip", 1'700'000'000);
  summaries.emplace_back("Untimed", 0);

  QMetaObject::invokeMethod(mpdman.get(), "on_playlists",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::playlist_summary>, summaries));

  ASSERT_EQ(spy.count(), 1);
  auto lists = spy.takeFirst().at(0).value<QList<playlist_info>>();
  ASSERT_EQ(lists.size(), 2);

  EXPECT_EQ(lists[0].id, QStringLiteral("Road Trip"));
  EXPECT_EQ(lists[0].name, QStringLiteral("Road Trip"));
  EXPECT_EQ(lists[0].song_count, -1);
  EXPECT_EQ(lists[0].duration_ms, -1);
  EXPECT_TRUE(lists[0].last_modified.isValid());
  EXPECT_EQ(lists[0].last_modified.toSecsSinceEpoch(), 1'700'000'000);

  EXPECT_EQ(lists[1].id, QStringLiteral("Untimed"));
  EXPECT_FALSE(lists[1].last_modified.isValid()); // 0 → invalid
}

// on_playlist_songs converts mpd::song → song and re-emits under the playlist
// name via playlist_songs_loaded.
TEST_F(MpdManagerTest, OnPlaylistSongs_ConvertsAndEmitsUnderName)
{
  QSignalSpy spy(mpdman.get(), &Backend::playlist_songs_loaded);

  std::vector<mpd::song> songs{
      make_mpd_song("a/b/01.flac", {{"Title", "One"}, {"Artist", "Band"}}),
      make_mpd_song("a/b/02.flac", {{"Title", "Two"}, {"Artist", "Band"}}),
  };

  QMetaObject::invokeMethod(mpdman.get(), "on_playlist_songs",
      Qt::DirectConnection,
      Q_ARG(QString, "Mix"),
      Q_ARG(std::vector<mpd::song>, songs));

  ASSERT_EQ(spy.count(), 1);
  auto args = spy.takeFirst();
  EXPECT_EQ(args.at(0).toString(), QStringLiteral("Mix"));
  auto result = args.at(1).value<QList<song>>();
  ASSERT_EQ(result.size(), 2);
  EXPECT_EQ(result[0].uri, QStringLiteral("a/b/01.flac"));
  EXPECT_EQ(result[0].title, QStringLiteral("One"));
  EXPECT_EQ(result[1].uri, QStringLiteral("a/b/02.flac"));
}

// MPD_IDLE_STORED_PLAYLIST names no playlist, so bursts are debounced into a
// single playlists_changed. Two rapid idle events must yield exactly one.
TEST_F(MpdManagerTest, HandleIdleStoredPlaylist_DebouncesToSingleChanged)
{
  QSignalSpy spy(mpdman.get(), &Backend::playlists_changed);

  QMetaObject::invokeMethod(mpdman.get(), "handle_idle",
      Qt::DirectConnection, Q_ARG(unsigned int, MPD_IDLE_STORED_PLAYLIST));
  QMetaObject::invokeMethod(mpdman.get(), "handle_idle",
      Qt::DirectConnection, Q_ARG(unsigned int, MPD_IDLE_STORED_PLAYLIST));

  // Pump well past the 250 ms debounce window.
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < 500)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);

  EXPECT_EQ(spy.count(), 1);
}

// The favorites reply resolves its uris against the fully-cached library into
// full song values. Delivered synchronously (direct on_favorites call) so the
// async worker fetch — which offline returns an empty set — can't race it.
TEST_F(MpdManagerTest, FetchFavorites_ResolvesUrisAgainstLibrary)
{
  const QString hash = "album-hash";
  libman->add_album(hash,
      album{"a/b", hash, "Disc", "Band", "2022", "Band", QDateTime{}});
  libman->set_songs(hash, {
                              song{"a/b/01.flac", "One", "Band", 1, 1, 200000, hash},
                              song{"a/b/02.flac", "Two", "Band", 2, 1, 200000, hash},
                              song{"a/b/03.flac", "Three", "Band", 3, 1, 200000, hash},
                          });

  QSignalSpy spy(mpdman.get(), &Backend::favorite_songs_loaded);

  QSet<QString> favs{"a/b/01.flac", "a/b/03.flac", "not/in/library.flac"};
  QMetaObject::invokeMethod(mpdman.get(), "on_favorites",
      Qt::DirectConnection, Q_ARG(QSet<QString>, favs));

  ASSERT_EQ(spy.count(), 1);
  auto resolved = spy.takeFirst().at(0).value<QList<song>>();

  // The unknown uri is skipped; order follows QSet iteration, so compare as a set.
  QSet<QString> got;
  for (const auto &s : resolved)
    got.insert(s.uri);
  EXPECT_EQ(got, (QSet<QString>{"a/b/01.flac", "a/b/03.flac"}));
}

// One sticker reply answers both consumers: the uri set and the resolved songs.
TEST_F(MpdManagerTest, FetchFavorites_EmitsBothLoadedSignals)
{
  const QString hash = "album-hash";
  libman->add_album(hash,
      album{"a/b", hash, "Disc", "Band", "2022", "Band", QDateTime{}});
  libman->set_songs(hash,
      {song{"a/b/01.flac", "One", "Band", 1, 1, 200000, hash}});

  QSignalSpy loaded(mpdman.get(), &Backend::favorites_loaded);
  QSignalSpy songs(mpdman.get(), &Backend::favorite_songs_loaded);

  QMetaObject::invokeMethod(mpdman.get(), "on_favorites",
      Qt::DirectConnection, Q_ARG(QSet<QString>, QSet<QString>{"a/b/01.flac"}));

  EXPECT_EQ(loaded.count(), 1);
  EXPECT_EQ(songs.count(), 1);
}

// With a null library manager, the songs payload must come through empty rather
// than dereference libman.
TEST_F(MpdManagerTest, FetchFavorites_NullLibman_EmitsEmptySongs)
{
  MpdManager m(nullptr);
  QSignalSpy spy(&m, &Backend::favorite_songs_loaded);

  QSet<QString> favs{"a/b/01.flac"};
  QMetaObject::invokeMethod(&m, "on_favorites",
      Qt::DirectConnection, Q_ARG(QSet<QString>, favs));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(spy.takeFirst().at(0).value<QList<song>>().isEmpty());
}

// Playlist command slots dispatch to the worker; with no live connection the
// dispatched lambdas hit QMpdClient's null-connection guard. Must not crash.
TEST_F(MpdManagerTest, PlaylistCommands_WhenNotConnected_NoCrash)
{
  song s;
  s.uri = "a/b/01.flac";

  mpdman->fetch_playlists();
  mpdman->fetch_playlist_songs("Mix");
  mpdman->create_playlist("New", QList<song>{s});
  mpdman->add_to_playlist("New", QList<song>{s});
  mpdman->rename_playlist("New", "Renamed");
  mpdman->delete_playlist("Renamed");
  mpdman->remove_from_playlist("Mix", QList<int>{2, 0, 1});
  mpdman->rearrange_playlist("Mix", 4, QList<int>{1, 2});
  SUCCEED();
}

// ---------------------------------------------------------------------------
// get_songs_for_album — cache read after on_all_songs
// ---------------------------------------------------------------------------

TEST_F(MpdManagerTest, GetSongsForAlbum_AfterPopulation_ReturnsCorrectTrackCount)
{
  auto s1 = make_mpd_song("a/b/01.flac", {
                                             {"Title", "Track 1"},
                                             {"Artist", "Band"},
                                             {"Album", "Disc"},
                                             {"Date", "2022"},
                                             {"Track", "1"},
                                         });
  auto s2 = make_mpd_song("a/b/02.flac", {
                                             {"Title", "Track 2"},
                                             {"Artist", "Band"},
                                             {"Album", "Disc"},
                                             {"Date", "2022"},
                                             {"Track", "2"},
                                         });

  std::vector<mpd::song> songs{s1, s2};
  QMetaObject::invokeMethod(mpdman.get(), "on_all_songs",
      Qt::DirectConnection,
      Q_ARG(std::vector<mpd::song>, songs));

  auto albums = mpdman->get_albums();
  ASSERT_EQ(albums.size(), 1);
  QList<song> tracks;
  mpdman->fetch_songs(albums.first(), [&](const QList<song> &s) { tracks = s; });
  EXPECT_EQ(tracks.size(), 2);
}

// ---------------------------------------------------------------------------
// on_error — new slot wired to QMpdClient::error
// ---------------------------------------------------------------------------

// on_error must not crash for any error code or fatality.
TEST_F(MpdManagerTest, OnError_NoCrash)
{
  mpd::error non_fatal{MPD_ERROR_SERVER, "ACK [50@0] {} Not playing", false};
  QMetaObject::invokeMethod(mpdman.get(), "on_error",
      Qt::DirectConnection,
      Q_ARG(mpd::error, non_fatal));

  mpd::error fatal{MPD_ERROR_CLOSED, "connection closed by server", true};
  QMetaObject::invokeMethod(mpdman.get(), "on_error",
      Qt::DirectConnection,
      Q_ARG(mpd::error, fatal));

  SUCCEED();
}

// on_error must not emit connection_update — that is on_disconnected's job.
// Non-fatal errors (ACK responses) leave the connection alive.
TEST_F(MpdManagerTest, OnError_NonFatal_DoesNotEmitConnectionUpdate)
{
  QSignalSpy spy(mpdman.get(), &MpdManager::connection_update);

  mpd::error err{MPD_ERROR_SERVER, "ACK [50@0] {} Not playing", false};
  QMetaObject::invokeMethod(mpdman.get(), "on_error",
      Qt::DirectConnection,
      Q_ARG(mpd::error, err));

  EXPECT_EQ(spy.count(), 0);
}

// Even a "fatal" error passed to on_error must not directly emit
// connection_update — the signal chain is: check_cmd_error → do_disconnect →
// disconnected() → on_disconnected → connection_update(false).
// on_error itself is just a logging sink.
TEST_F(MpdManagerTest, OnError_Fatal_DoesNotEmitConnectionUpdate)
{
  QSignalSpy spy(mpdman.get(), &MpdManager::connection_update);

  mpd::error err{MPD_ERROR_CLOSED, "connection closed by server", true};
  QMetaObject::invokeMethod(mpdman.get(), "on_error",
      Qt::DirectConnection,
      Q_ARG(mpd::error, err));

  EXPECT_EQ(spy.count(), 0);
}

// on_error while in the connected state must not change the connected flag
// (only on_disconnected does that).
TEST_F(MpdManagerTest, OnError_WhenConnected_DoesNotEmitConnectionUpdateFalse)
{
  QMetaObject::invokeMethod(mpdman.get(), "on_connected",
      Qt::DirectConnection);

  QSignalSpy spy(mpdman.get(), &MpdManager::connection_update);

  mpd::error err{MPD_ERROR_CLOSED, "connection closed by server", true};
  QMetaObject::invokeMethod(mpdman.get(), "on_error",
      Qt::DirectConnection,
      Q_ARG(mpd::error, err));

  EXPECT_EQ(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// connect / disconnect lifecycle guards
// ---------------------------------------------------------------------------

// connect_to_server() when already connected must return true without re-dispatching
// do_connect to the worker thread.
TEST_F(MpdManagerTest, Connect_WhenAlreadyConnected_ReturnsTrueImmediately)
{
  QMetaObject::invokeMethod(mpdman.get(), "on_connected",
      Qt::DirectConnection);

  QSignalSpy spy(mpdman.get(), &MpdManager::connection_update);

  bool result = mpdman->connect_to_server();

  EXPECT_TRUE(result);
  // No additional connection_update from the re-entrant connect_to_server() call.
  EXPECT_EQ(spy.count(), 0);
}

// disconnect_from_server() when not connected must be a no-op — no signal, no crash.
TEST_F(MpdManagerTest, Disconnect_WhenNotConnected_NoCrash)
{
  QSignalSpy spy(mpdman.get(), &MpdManager::connection_update);

  mpdman->disconnect_from_server();

  EXPECT_EQ(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// Public command slots — dispatch safely when not connected.
// The dispatched lambdas are queued on the worker thread and execute
// during MpdManager teardown; they hit QMpdClient's `if (!cmd_conn) return`
// guard and complete cleanly.
// ---------------------------------------------------------------------------

TEST_F(MpdManagerTest, PlaybackCommands_WhenNotConnected_NoCrash)
{
  mpdman->play();
  mpdman->pause();
  mpdman->stop();
  mpdman->prev();
  mpdman->next();
  mpdman->seek(500);
  mpdman->play_pos(0);
  SUCCEED();
}

TEST_F(MpdManagerTest, VolumeAndRepeatCommands_WhenNotConnected_NoCrash)
{
  mpdman->set_volume(75);
  mpdman->set_repeat(true, false);
  mpdman->set_repeat(false, true);
  mpdman->set_repeat(false, false);
  mpdman->update_db();
  SUCCEED();
}

TEST_F(MpdManagerTest, InsertQueue_SongList_WhenNotConnected_NoCrash)
{
  song s;
  s.uri = "artist/album/01.flac";
  mpdman->insert_queue(QList<song>{s}, 0);
  SUCCEED();
}

TEST_F(MpdManagerTest, InsertQueue_UriList_WhenNotConnected_NoCrash)
{
  mpdman->insert_queue(QList<QString>{"a/b.flac", "c/d.flac"}, 0);
  mpdman->insert_queue(QList<QString>{}, 0);
  SUCCEED();
}

TEST_F(MpdManagerTest, ReplaceQueue_WhenNotConnected_NoCrash)
{
  song s;
  s.uri = "a/b.flac";
  mpdman->replace_queue(QList<song>{s}, 0);
  mpdman->replace_queue(QList<song>{}, 0);
  SUCCEED();
}

TEST_F(MpdManagerTest, AppendQueue_WhenNotConnected_NoCrash)
{
  song s;
  s.uri = "a/b.flac";
  mpdman->append_queue(QList<song>{s});
  mpdman->append_queue(QList<song>{});
  SUCCEED();
}

TEST_F(MpdManagerTest, RemoveFromQueue_WhenNotConnected_NoCrash)
{
  mpdman->remove_from_queue(QList<QModelIndex>{});
  SUCCEED();
}

TEST_F(MpdManagerTest, RearrangeQueue_WhenNotConnected_NoCrash)
{
  mpdman->rearrange_queue(2, {0, 1});
  mpdman->rearrange_queue(0, {});
  SUCCEED();
}

// ---------------------------------------------------------------------------
// LibraryManager — read methods must not mutate the cache
// ---------------------------------------------------------------------------

TEST(LibraryManagerTest, ReadsDoNotInsertOnMiss)
{
  LibraryManager libman;

  // QMap::operator[] would default-insert on these misses; value() must not.
  EXPECT_TRUE(libman.get_album("missing").name.isEmpty());
  EXPECT_TRUE(libman.get_songs("missing").isEmpty());

  EXPECT_FALSE(libman.has_album("missing"));
  EXPECT_TRUE(libman.get_albums().isEmpty());
}
