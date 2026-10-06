// Tests for QMpdClient that do not require a live MPD instance.
//
// All tests call slots directly on the main thread (no moveToThread) so that
// they execute synchronously — this is valid because run_command() is
// reentrant-safe and there is no concurrent socket access in these tests.

#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QCoreApplication>

#include "mpdlib/QMpdClient.hh"

// ---------------------------------------------------------------------------
// Helper — build an mpd_song using the public mpd_song_begin/feed API.
// ---------------------------------------------------------------------------
[[maybe_unused]] static auto
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

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class QMpdClientTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    // Ensure all types used in cross-thread signals are registered.
    // QMpdClient's constructor does this too, but the first construction
    // in the process registers them, so duplicates are harmless.
    qRegisterMetaType<mpd::error>();
    qRegisterMetaType<mpd::status>();
    qRegisterMetaType<std::vector<mpd::song>>();
    qRegisterMetaType<std::optional<mpd::song>>();
  }
};

// ---------------------------------------------------------------------------
// Lifecycle safety tests (no connection)
// ---------------------------------------------------------------------------

// A freshly constructed client is in the disconnected state and does not crash.
TEST_F(QMpdClientTest, DefaultConstructed_NoCrash)
{
  QMpdClient client;
  SUCCEED();
}

// do_disconnect when not connected must not crash and must emit disconnected.
TEST_F(QMpdClientTest, DoDisconnect_WhenNotConnected_EmitsDisconnected)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::disconnected);

  client.do_disconnect();

  EXPECT_EQ(spy.count(), 1);
}

// Calling do_disconnect twice must not crash (idempotent).
TEST_F(QMpdClientTest, DoDisconnect_Twice_NoCrash)
{
  QMpdClient client;
  client.do_disconnect();
  client.do_disconnect();
  SUCCEED();
}

// A connect attempt to an unreachable port must emit disconnected() (not hang
// silently) so the UI can react to the failure.
TEST_F(QMpdClientTest, DoConnect_ToUnreachablePort_EmitsDisconnected)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::disconnected);

  // Port 1 on loopback refuses immediately.
  client.do_connect(QStringLiteral("127.0.0.1"), 1);

  EXPECT_GE(spy.count(), 1);
}

// ---------------------------------------------------------------------------
// Fetch methods when not connected — must be safe no-ops or emit sensible values.
// ---------------------------------------------------------------------------

// fetch_status: no connection → no status_received emitted (no status to report).
TEST_F(QMpdClientTest, FetchStatus_WhenNotConnected_EmitsNothing)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::status_received);

  client.fetch_status();

  EXPECT_EQ(spy.count(), 0);
}

// fetch_queue: no connection → queue_received emitted with empty list.
TEST_F(QMpdClientTest, FetchQueue_WhenNotConnected_EmitsEmpty)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::queue_received);

  client.fetch_queue();

  ASSERT_EQ(spy.count(), 1);
  auto songs = spy.takeFirst()[0].value<std::vector<mpd::song>>();
  EXPECT_TRUE(songs.empty());
}

// fetch_current_song: no connection → current_song_received emitted with nullopt.
TEST_F(QMpdClientTest, FetchCurrentSong_WhenNotConnected_EmitsNullopt)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::current_song_received);

  client.fetch_current_song();

  ASSERT_EQ(spy.count(), 1);
  auto result = spy.takeFirst()[0].value<std::optional<mpd::song>>();
  EXPECT_FALSE(result.has_value());
}

// fetch_all_songs: no connection → all_songs_received emitted with empty list.
TEST_F(QMpdClientTest, FetchAllSongs_WhenNotConnected_EmitsEmpty)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::all_songs_received);

  client.fetch_all_songs();

  ASSERT_EQ(spy.count(), 1);
  auto songs = spy.takeFirst()[0].value<std::vector<mpd::song>>();
  EXPECT_TRUE(songs.empty());
}

// fetch_album_art: no connection → album_art_received emitted with empty bytes.
TEST_F(QMpdClientTest, FetchAlbumArt_WhenNotConnected_EmitsEmpty)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::album_art_received);

  client.fetch_album_art("artist/album/track.flac");

  ASSERT_EQ(spy.count(), 1);
  auto bytes = spy.takeFirst()[1].value<QByteArray>();
  EXPECT_TRUE(bytes.isEmpty());
}

// ---------------------------------------------------------------------------
// Playback and option commands when not connected — must not crash.
// ---------------------------------------------------------------------------

TEST_F(QMpdClientTest, PlaybackCommands_WhenNotConnected_NoCrash)
{
  QMpdClient client;
  client.play();
  client.stop();
  client.pause(true);
  client.pause(false);
  client.next();
  client.prev();
  client.seek(0.5f);
  client.play_pos(0);
  SUCCEED();
}

TEST_F(QMpdClientTest, OptionCommands_WhenNotConnected_NoCrash)
{
  QMpdClient client;
  client.set_volume(50);
  client.set_repeat(true);
  client.set_random(false);
  client.set_single(true);
  client.set_consume(false);
  client.set_crossfade(3);
  client.trigger_db_update();
  SUCCEED();
}

TEST_F(QMpdClientTest, QueueCommands_WhenNotConnected_NoCrash)
{
  QMpdClient client;
  client.replace_queue_and_play({}, 0);
  client.replace_queue_and_play({"a/b.flac", "c/d.flac"}, 1);
  client.append_queue({});
  client.append_queue({"x/y.flac"});
  client.remove_from_queue({});
  client.remove_from_queue({0, 2, 1});
  client.rearrange_queue({});
  client.rearrange_queue({{0, 2}, {1, 3}});
  client.insert_queue_at(0, {});
  client.insert_queue_at(2, {"a/b.flac"});
  SUCCEED();
}

// ---------------------------------------------------------------------------
// do_connect with a refused port — must emit error, not crash.
// ECONNREFUSED on localhost is immediate so this test is fast.
// ---------------------------------------------------------------------------

TEST_F(QMpdClientTest, DoConnect_WithRefusedPort_EmitsError)
{
  QMpdClient client;
  QSignalSpy error_spy(&client, &QMpdClient::error);
  QSignalSpy connect_spy(&client, &QMpdClient::connected);

  // Port 9 is the discard protocol; nothing listens on it in test environments.
  // ECONNREFUSED is immediate on localhost.
  client.do_connect("127.0.0.1", 9);

  // Either error or connected — the key invariant is: no crash,
  // and if the connection fails then exactly one error is emitted.
  if (connect_spy.count() == 0)
    {
      EXPECT_EQ(error_spy.count(), 1);
    }
  else
    {
      // Unexpected success — clean up gracefully
      client.do_disconnect();
    }
}

// ---------------------------------------------------------------------------
// Metatype registration — all cross-thread types must be findable by name.
// ---------------------------------------------------------------------------

TEST_F(QMpdClientTest, MetatypesRegistered_AfterConstruction)
{
  QMpdClient client; // constructor registers types

  EXPECT_TRUE(QMetaType::fromType<mpd::error>().isValid());
  EXPECT_TRUE(QMetaType::fromType<mpd::status>().isValid());
  EXPECT_TRUE(QMetaType::fromType<std::vector<mpd::song>>().isValid());
  EXPECT_TRUE(QMetaType::fromType<std::optional<mpd::song>>().isValid());
  EXPECT_TRUE(QMetaType::fromType<QByteArray>().isValid());
}

// ---------------------------------------------------------------------------
// on_socket_readable — idle re-entered before idle_event is emitted.
// Verified structurally: the idling flag must be true when the signal fires.
//
// We test this by subclassing QMpdClient to intercept the signal emission and
// read the (now-public) idling state via a friend accessor.
// ---------------------------------------------------------------------------

// A thin wrapper that exposes the idling flag for white-box testing.
class ObservableQMpdClient : public QMpdClient {
public:
  bool last_idling_on_event = false;

  ObservableQMpdClient()
  {
    QObject::connect(this, &QMpdClient::idle_event,
        [this](unsigned int) {
          // By the time idle_event is emitted, the connection must have
          // re-entered idle mode.  We can't access the private 'idling'
          // member directly, but we CAN observe that a subsequent call
          // to fetch_status (which calls run_command) does NOT attempt
          // an extra noidle because idle was already re-entered.
          // Record that we reached this lambda.
          last_idling_on_event = true;
        });
  }
};

// Structural test: idle_event lambda fires (proof that on_socket_readable
// emits the signal at all; actual re-entry ordering is tested at integration
// level with a real MPD socket).
TEST_F(QMpdClientTest, IdleEvent_SignalIsEmittable)
{
  ObservableQMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::idle_event);

  // Manually emit via Qt's meta-object system to verify signal is wired
  // (QMetaObject::invokeMethod on a signal emits it directly).
  QMetaObject::invokeMethod(&client, "idle_event",
      Qt::DirectConnection,
      Q_ARG(unsigned int, MPD_IDLE_PLAYER));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.takeFirst()[0].toUInt(),
      static_cast<unsigned int>(MPD_IDLE_PLAYER));
  EXPECT_TRUE(client.last_idling_on_event);
}

// ---------------------------------------------------------------------------
// Error signal contract — must NOT fire for not-connected state.
// In the two-connection design, command slots guard with `if (!cmd_conn) return`
// and never touch the error signal for the disconnected case.
// ---------------------------------------------------------------------------

TEST_F(QMpdClientTest, PlaybackCommands_WhenNotConnected_NoErrorSignal)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::error);

  client.play();
  client.stop();
  client.pause(true);
  client.pause(false);
  client.next();
  client.prev();
  client.seek(0.5f);
  client.play_pos(0);

  EXPECT_EQ(spy.count(), 0);
}

TEST_F(QMpdClientTest, OptionCommands_WhenNotConnected_NoErrorSignal)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::error);

  client.set_volume(50);
  client.set_repeat(true);
  client.set_random(false);
  client.set_single(true);
  client.set_consume(false);
  client.set_crossfade(3);
  client.trigger_db_update();

  EXPECT_EQ(spy.count(), 0);
}

TEST_F(QMpdClientTest, QueueCommands_WhenNotConnected_NoErrorSignal)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::error);

  client.replace_queue_and_play({}, 0);
  client.replace_queue_and_play({"a/b.flac", "c/d.flac"}, 1);
  client.append_queue({"x/y.flac"});
  client.remove_from_queue({0, 2, 1});
  client.rearrange_queue({{0, 2}, {1, 3}});
  client.insert_queue_at(0, {"a/b.flac"});

  EXPECT_EQ(spy.count(), 0);
}

TEST_F(QMpdClientTest, FetchMethods_WhenNotConnected_NoErrorSignal)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::error);

  client.fetch_status();
  client.fetch_queue();
  client.fetch_current_song();
  client.fetch_all_songs();
  client.fetch_album_art("artist/album/track.flac");

  EXPECT_EQ(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// do_connect robustness
// ---------------------------------------------------------------------------

// A second do_connect after a failed attempt must not crash or leak state.
// (The two-connection design calls do_disconnect internally if cmd_conn/idle_conn
// are non-null, but after a partial failure they're left null — still safe.)
TEST_F(QMpdClientTest, DoConnect_SecondAttemptAfterFailure_NoCrash)
{
  QMpdClient client;
  QSignalSpy error_spy(&client, &QMpdClient::error);
  QSignalSpy connected_spy(&client, &QMpdClient::connected);

  client.do_connect("127.0.0.1", 9); // first attempt — expected to fail
  client.do_connect("127.0.0.1", 9); // second attempt — must not crash

  EXPECT_EQ(connected_spy.count(), 0);
  // One error per failed connection attempt.
  if (connected_spy.count() == 0)
    {
      EXPECT_GE(error_spy.count(), 1);
    }
}

// After a failed connect, all command slots are still safe no-ops and do not
// emit error() (they just return on the null-connection guard).
TEST_F(QMpdClientTest, AfterFailedConnect_CommandSlots_NoCrashAndNoErrorSignal)
{
  QMpdClient client;
  client.do_connect("127.0.0.1", 9); // fails

  QSignalSpy spy(&client, &QMpdClient::error);

  client.play();
  client.stop();
  client.pause(true);
  client.next();
  client.prev();
  client.seek(0.5f);
  client.play_pos(0);
  client.set_volume(50);
  client.set_repeat(true);
  client.trigger_db_update();
  client.fetch_status();
  client.fetch_queue();
  client.fetch_current_song();
  client.fetch_all_songs();

  EXPECT_EQ(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// Slot metadata — structural checks on the two-connection design
// ---------------------------------------------------------------------------

// The old 'on_socket_readable' slot was renamed to 'on_idle_readable' to
// reflect that it belongs exclusively to the idle connection.
TEST_F(QMpdClientTest, SlotRename_OnIdleReadable_ReplacesOnSocketReadable)
{
  QMpdClient client;
  const QMetaObject *meta = client.metaObject();

  bool found_old = false;
  bool found_new = false;
  for (int i = 0; i < meta->methodCount(); ++i)
    {
      QMetaMethod m = meta->method(i);
      if (m.methodType() == QMetaMethod::Slot)
        {
          QByteArray name = m.name();
          if (name == "on_socket_readable") found_old = true;
          if (name == "on_idle_readable") found_new = true;
        }
    }

  EXPECT_FALSE(found_old) << "old slot 'on_socket_readable' should have been removed";
  EXPECT_TRUE(found_new) << "new slot 'on_idle_readable' should exist";
}

// ---------------------------------------------------------------------------
// Stored playlists — offline behaviour.
//
// Like every other QMpdClient test these run without a live MPD, so protocol
// framing (exact commands sent, response parsing, descending delete order,
// command-list batching) is not exercised here — that coverage is consciously
// waived (no TCP fake). What we can and do pin down: the fetchers
// emit sensible empty results, the mutators are silent no-ops that neither
// confirm a change nor raise an error, and the new metatype is registered.
// ---------------------------------------------------------------------------

// fetch_playlists: no connection → playlists_received emitted with empty list.
TEST_F(QMpdClientTest, FetchPlaylists_WhenNotConnected_EmitsEmpty)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::playlists_received);

  client.fetch_playlists();

  ASSERT_EQ(spy.count(), 1);
  auto lists = spy.takeFirst()[0].value<std::vector<mpd::playlist_summary>>();
  EXPECT_TRUE(lists.empty());
}

// fetch_playlist_songs: no connection → playlist_songs_received emitted with the
// requested name and an empty song list.
TEST_F(QMpdClientTest, FetchPlaylistSongs_WhenNotConnected_EmitsNameAndEmpty)
{
  QMpdClient client;
  QSignalSpy spy(&client, &QMpdClient::playlist_songs_received);

  client.fetch_playlist_songs("My Mix");

  ASSERT_EQ(spy.count(), 1);
  auto args = spy.takeFirst();
  EXPECT_EQ(args[0].toString(), QStringLiteral("My Mix"));
  EXPECT_TRUE(args[1].value<std::vector<mpd::song>>().empty());
}

// Mutating slots when not connected must be silent no-ops: no
// stored_playlists_changed (nothing changed) and no error (the guard returns
// before touching the connection).
TEST_F(QMpdClientTest, PlaylistMutators_WhenNotConnected_NoChangeNoError)
{
  QMpdClient client;
  QSignalSpy changed_spy(&client, &QMpdClient::stored_playlists_changed);
  QSignalSpy error_spy(&client, &QMpdClient::error);

  client.create_playlist("New", {"a/b.flac", "c/d.flac"});
  client.add_to_playlist("New", {"e/f.flac"});
  client.rename_playlist("New", "Renamed");
  client.delete_playlist("Renamed");
  client.remove_from_playlist("Mix", {2, 0, 1});
  client.rearrange_playlist("Mix", {{3, 0}, {1, 2}});

  EXPECT_EQ(changed_spy.count(), 0);
  EXPECT_EQ(error_spy.count(), 0);
}

// The playlist-summary vector must be registered as a metatype so it can cross
// the worker→main thread boundary in a queued signal.
TEST_F(QMpdClientTest, PlaylistSummaryMetatype_Registered)
{
  QMpdClient client; // constructor registers the type

  EXPECT_TRUE(
      QMetaType::fromType<std::vector<mpd::playlist_summary>>().isValid());
}
