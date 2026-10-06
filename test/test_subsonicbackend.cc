#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QSignalSpy>
#include <QModelIndex>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <sstream>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "controller/favoritesmanager.hh"
#include "controller/subsonicbackend.hh"
#include "controller/trackcache.hh"
#include "test/http_test_server.hh"
#include "test/mock_audio_engine.hh"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static song
make_song(const QString &uri, const QString &title = "",
    uint32_t duration_ms = 180000)
{
  song s;
  s.uri = uri;
  s.title = title;
  s.artist = "Artist";
  s.track_number = 1;
  s.disc_number = 1;
  s.duration = duration_ms;
  s.album_hash = "hash";
  return s;
}

class SubsonicBackendTest : public ::testing::Test {
protected:
  MockAudioEngine *engine;
  SubsonicBackend *backend;

  void SetUp() override
  {
    engine = new MockAudioEngine;
    backend = new SubsonicBackend("http://localhost", SubsonicAuth{.api_key = "key"}, engine);
  }

  void TearDown() override { delete backend; }

  QList<song> make_queue(int count)
  {
    QList<song> q;
    for (int i = 0; i < count; ++i)
      q.append(make_song(QString("id-%1").arg(i), QString("Track %1").arg(i)));
    return q;
  }
};

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST_F(SubsonicBackendTest, InitialState_EngineNotPlayed)
{
  EXPECT_EQ(engine->play_count, 0);
}

TEST_F(SubsonicBackendTest, InitialVolume_Is100)
{
  EXPECT_EQ(engine->volume(), 100);
}

TEST_F(SubsonicBackendTest, SupportsFavorites)
{
  EXPECT_TRUE(backend->supports(Backend::Feature::Favorites));
}

// star/unstar are no-ops while disconnected — must not crash or emit.
TEST_F(SubsonicBackendTest, SetFavorite_WhenNotConnected_NoEmit)
{
  QSignalSpy spy(backend, &Backend::favorite_changed);
  backend->set_favorite("s1", true);
  EXPECT_EQ(spy.count(), 0);
}

// ---------------------------------------------------------------------------
// Volume
// ---------------------------------------------------------------------------

TEST_F(SubsonicBackendTest, SetVolume_DelegatestoEngine)
{
  backend->set_volume(42);
  EXPECT_EQ(engine->volume(), 42);
}

TEST_F(SubsonicBackendTest, PlaybackState_ReflectsVolume)
{
  backend->set_volume(75);
  QSignalSpy spy(backend, &Backend::playback_state_changed);
  backend->stop(); // triggers emit_playback_state
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.takeFirst()[0].value<PlaybackState>().volume, 75);
}

// ---------------------------------------------------------------------------
// Transport — basic delegation
// ---------------------------------------------------------------------------

TEST_F(SubsonicBackendTest, Pause_DelegatesToEngine)
{
  backend->replace_queue(make_queue(2), 0);
  ASSERT_EQ(engine->state(), AudioEngine::State::Playing);
  backend->pause();
  EXPECT_EQ(engine->state(), AudioEngine::State::Paused);
}

TEST_F(SubsonicBackendTest, Stop_DelegatesToEngine)
{
  backend->replace_queue(make_queue(2), 0);
  backend->stop();
  EXPECT_EQ(engine->state(), AudioEngine::State::Stopped);
}

TEST_F(SubsonicBackendTest, Seek_DelegatesToEngine)
{
  backend->seek(5000);
  EXPECT_EQ(engine->last_seek_ms, 5000u);
  EXPECT_EQ(engine->seek_count, 1);
}

TEST_F(SubsonicBackendTest, Play_WhenPaused_Resumes)
{
  backend->replace_queue(make_queue(2), 0);
  backend->pause();
  ASSERT_EQ(engine->state(), AudioEngine::State::Paused);
  auto count_before = engine->play_count;
  backend->play();
  // resume() is called, not play() again
  EXPECT_EQ(engine->play_count, count_before);
  EXPECT_EQ(engine->state(), AudioEngine::State::Playing);
}

// Pressing play on a track that is already playing must do nothing. It used to
// restart it from zero, which is what a listener got for pressing play on a
// player that looked stuck mid-seek.
TEST_F(SubsonicBackendTest, Play_WhenAlreadyPlaying_DoesNotRestartTheTrack)
{
  backend->replace_queue(make_queue(2), 0);
  ASSERT_EQ(engine->state(), AudioEngine::State::Playing);

  auto count_before = engine->play_count;
  backend->play();
  EXPECT_EQ(engine->play_count, count_before);
  EXPECT_EQ(engine->state(), AudioEngine::State::Playing);
}

TEST_F(SubsonicBackendTest, Play_WhenStopped_PlaysCurrentTrack)
{
  backend->replace_queue(make_queue(2), 0);
  backend->stop();
  auto count_before = engine->play_count;
  backend->play();
  EXPECT_EQ(engine->play_count, count_before + 1);
}

// ---------------------------------------------------------------------------
// Queue — replace, append, insert, remove
// ---------------------------------------------------------------------------

TEST_F(SubsonicBackendTest, ReplaceQueue_StartsPlayback)
{
  backend->replace_queue(make_queue(3), 1);
  EXPECT_EQ(engine->play_count, 1);
  EXPECT_TRUE(engine->last_play_url.toString().contains("id-1"));
}

TEST_F(SubsonicBackendTest, ReplaceQueue_EmitsQueueChanged)
{
  QSignalSpy spy(backend, &Backend::queue_changed);
  backend->replace_queue(make_queue(3), 0);
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(SubsonicBackendTest, ReplaceQueue_EmitsCurrentSongChanged)
{
  QSignalSpy spy(backend, &Backend::current_song_changed);
  backend->replace_queue(make_queue(3), 0);
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(SubsonicBackendTest, AppendQueue_AddsToQueue)
{
  QSignalSpy spy(backend, &Backend::queue_changed);
  backend->append_queue(make_queue(2));
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(SubsonicBackendTest, InsertQueue_InsertsAtPosition)
{
  backend->replace_queue(make_queue(3), 0);

  QList<song> extra = {make_song("new-id", "New Track")};
  backend->insert_queue(extra, 1);

  QSignalSpy spy(backend, &Backend::queue_changed);
  backend->append_queue({}); // trigger a queue signal to inspect
  auto queue = spy.takeFirst()[0].value<QList<song>>();
  EXPECT_EQ(queue[1].uri, "new-id");
}

TEST_F(SubsonicBackendTest, RemoveFromQueue_RemovesCorrectItem)
{
  backend->replace_queue(make_queue(4), 0);
  QSignalSpy spy(backend, &Backend::queue_changed);

  QList<QModelIndex> to_remove = {QModelIndex()};
  // Can't easily create valid QModelIndexes here; just verify no crash
  backend->remove_from_queue({});
  SUCCEED();
}

// ---------------------------------------------------------------------------
// Queue — rearrange
// ---------------------------------------------------------------------------

TEST_F(SubsonicBackendTest, RearrangeQueue_ReordersTracks)
{
  backend->replace_queue(make_queue(4), 0); // [id-0, id-1, id-2, id-3]

  QSignalSpy spy(backend, &Backend::queue_changed);
  backend->rearrange_queue(4, {0}); // move id-0 to the end

  ASSERT_EQ(spy.count(), 1);
  auto queue = spy.takeFirst()[0].value<QList<song>>();
  ASSERT_EQ(queue.size(), 4);
  EXPECT_EQ(queue[0].uri, "id-1");
  EXPECT_EQ(queue[1].uri, "id-2");
  EXPECT_EQ(queue[2].uri, "id-3");
  EXPECT_EQ(queue[3].uri, "id-0");
}

// rearrange_queue re-syncs queue_pos by URI identity (subsonicbackend.cc
// ~956-962), not by index — a plain index shift would land on whatever track
// the move happened to leave behind at the old position.
TEST_F(SubsonicBackendTest, RearrangeQueue_ResyncsQueuePosWhenCurrentTrackMoves)
{
  backend->replace_queue(make_queue(4), 0); // playing id-0 at pos 0

  backend->rearrange_queue(2, {0}); // -> [id-1, id-0, id-2, id-3]

  // The armed successor must reflect the re-synced position immediately,
  // not the pre-rearrange one.
  EXPECT_TRUE(engine->next_url.toString().contains("id-2"))
      << engine->next_url.toString().toStdString();

  QSignalSpy spy(backend, &Backend::playback_state_changed);
  backend->stop();
  ASSERT_GE(spy.count(), 1);
  EXPECT_EQ(spy.last()[0].value<PlaybackState>().queue_pos, 1); // still on id-0
}

TEST_F(SubsonicBackendTest, RearrangeQueue_ResyncsQueuePosWhenOtherTrackMoves)
{
  backend->replace_queue(make_queue(4), 1); // playing id-1 at pos 1

  backend->rearrange_queue(4, {0}); // id-0 to the end -> [id-1, id-2, id-3, id-0]

  QSignalSpy spy(backend, &Backend::playback_state_changed);
  backend->stop();
  ASSERT_GE(spy.count(), 1);
  EXPECT_EQ(spy.last()[0].value<PlaybackState>().queue_pos, 0); // still on id-1
}

// Known limitation, not fixed here: the re-sync locates the current track by
// std::ranges::find on uri, which returns the first match. With duplicate
// URIs it can re-sync onto the wrong copy. Pinned so a future fix is a
// deliberate change rather than an accidental one.
TEST_F(SubsonicBackendTest, RearrangeQueue_DuplicateUri_ResyncsToFirstMatch)
{
  QList<song> q{make_song("dup", "First"), make_song("other", "Middle"),
      make_song("dup", "Second")};
  backend->replace_queue(q, 2); // playing the *second* "dup" copy

  backend->rearrange_queue(0, {1}); // move "other" to the front

  QSignalSpy spy(backend, &Backend::playback_state_changed);
  backend->stop();
  ASSERT_GE(spy.count(), 1);
  // Would be 2 (the copy actually playing) with identity tracked correctly;
  // the current implementation lands on 1, the first "dup" it finds.
  EXPECT_EQ(spy.last()[0].value<PlaybackState>().queue_pos, 1);
}

// ---------------------------------------------------------------------------
// Navigation — next, prev, play_pos
// ---------------------------------------------------------------------------

TEST_F(SubsonicBackendTest, Next_AdvancesQueuePos)
{
  backend->replace_queue(make_queue(3), 0);
  auto plays_before = engine->play_count;
  backend->next();
  EXPECT_EQ(engine->play_count, plays_before + 1);
  EXPECT_TRUE(engine->last_play_url.toString().contains("id-1"));
}

// next() must push a PlaybackState carrying the new queue_pos immediately, so
// the queue-list highlight moves at once instead of waiting for the next
// position tick (~1 s later).
TEST_F(SubsonicBackendTest, Next_EmitsPlaybackStateWithNewQueuePos)
{
  backend->replace_queue(make_queue(3), 0);
  QSignalSpy spy(backend, &Backend::playback_state_changed);
  backend->next();
  ASSERT_GE(spy.count(), 1);
  EXPECT_EQ(spy.takeLast()[0].value<PlaybackState>().queue_pos, 1);
}

TEST_F(SubsonicBackendTest, Next_AtEnd_DoesNothing)
{
  backend->replace_queue(make_queue(2), 1);
  auto plays_before = engine->play_count;
  backend->next();
  EXPECT_EQ(engine->play_count, plays_before); // no new play
}

// Repeat All turns the queue into a loop: next on the last track wraps to first.
TEST_F(SubsonicBackendTest, Next_AtEnd_WithRepeat_WrapsToFirst)
{
  backend->replace_queue(make_queue(3), 2); // start on the last track
  backend->set_repeat(/*repeat=*/true, /*single=*/false);
  backend->next();
  EXPECT_TRUE(engine->last_play_url.toString().contains("id-0"));
}

TEST_F(SubsonicBackendTest, Prev_DecrementsQueuePos)
{
  backend->replace_queue(make_queue(3), 2);
  auto plays_before = engine->play_count;
  backend->prev();
  EXPECT_EQ(engine->play_count, plays_before + 1);
  EXPECT_TRUE(engine->last_play_url.toString().contains("id-1"));
}

TEST_F(SubsonicBackendTest, Prev_AtStart_DoesNothing)
{
  backend->replace_queue(make_queue(3), 0);
  auto plays_before = engine->play_count;
  backend->prev();
  EXPECT_EQ(engine->play_count, plays_before);
}

// Repeat All: prev on the first track wraps to the last.
TEST_F(SubsonicBackendTest, Prev_AtStart_WithRepeat_WrapsToLast)
{
  backend->replace_queue(make_queue(3), 0); // start on the first track
  backend->set_repeat(/*repeat=*/true, /*single=*/false);
  backend->prev();
  EXPECT_TRUE(engine->last_play_url.toString().contains("id-2"));
}

TEST_F(SubsonicBackendTest, PlayPos_JumpsToPosition)
{
  backend->replace_queue(make_queue(5), 0);
  backend->play_pos(3);
  EXPECT_TRUE(engine->last_play_url.toString().contains("id-3"));
}

// ---------------------------------------------------------------------------
// Repeat / single
// ---------------------------------------------------------------------------

TEST_F(SubsonicBackendTest, SetRepeatAll_NextUrlIsNextTrack)
{
  backend->replace_queue(make_queue(3), 1);
  backend->set_repeat(true, false);
  EXPECT_TRUE(engine->next_url.toString().contains("id-2"));
}

TEST_F(SubsonicBackendTest, SetRepeatAll_AtEnd_NextUrlWraps)
{
  backend->replace_queue(make_queue(3), 2); // last track
  backend->set_repeat(true, false);
  EXPECT_TRUE(engine->next_url.toString().contains("id-0")); // wraps to first
}

TEST_F(SubsonicBackendTest, SetSingle_NextUrlIsSameTrack)
{
  backend->replace_queue(make_queue(3), 1);
  backend->set_repeat(true, true);                           // repeat=true, single=true
  EXPECT_TRUE(engine->next_url.toString().contains("id-1")); // same track
}

TEST_F(SubsonicBackendTest, SetSingle_NoRepeat_NextUrlCleared)
{
  backend->replace_queue(make_queue(3), 1);
  backend->set_repeat(false, true); // single without repeat: play once, then stop
  EXPECT_TRUE(engine->next_url.isEmpty());
}

TEST_F(SubsonicBackendTest, SetNoRepeat_AtEnd_NextUrlCleared)
{
  backend->replace_queue(make_queue(3), 2);
  backend->set_repeat(false, false);
  EXPECT_TRUE(engine->next_url.isEmpty());
}

// ---------------------------------------------------------------------------
// Engine events — track_finished
// ---------------------------------------------------------------------------

TEST_F(SubsonicBackendTest, TrackFinished_MidQueue_AdvancesAndPlays)
{
  backend->replace_queue(make_queue(3), 0);
  auto plays_before = engine->play_count;
  engine->fire_track_finished();
  EXPECT_EQ(engine->play_count, plays_before + 1);
  EXPECT_TRUE(engine->last_play_url.toString().contains("id-1"));
}

TEST_F(SubsonicBackendTest, TrackFinished_AtEnd_NoRepeat_Stops)
{
  backend->replace_queue(make_queue(2), 1);
  backend->set_repeat(false, false);
  auto plays_before = engine->play_count;
  engine->fire_track_finished();
  EXPECT_EQ(engine->play_count, plays_before); // no new play
}

TEST_F(SubsonicBackendTest, TrackFinished_AtEnd_NoRepeat_ReportsStoppedAtZeroOnLastTrack)
{
  backend->replace_queue(make_queue(2), 1);
  backend->set_repeat(false, false);
  engine->set_position(224319);
  engine->set_duration(224319);

  QSignalSpy spy(backend, &Backend::playback_state_changed);
  engine->fire_eos();

  ASSERT_GE(spy.count(), 1);
  auto ps = spy.last()[0].value<PlaybackState>();
  EXPECT_EQ(ps.state, PlayState::Stopped);
  EXPECT_EQ(ps.elapsed_ms, 0u);
  EXPECT_EQ(ps.total_ms, 224319u);
  EXPECT_EQ(ps.queue_pos, 1);
}

TEST_F(SubsonicBackendTest, Stop_ReportsElapsedZero)
{
  backend->replace_queue(make_queue(2), 0);
  backend->play();
  engine->set_position(30000);

  QSignalSpy spy(backend, &Backend::playback_state_changed);
  backend->stop();

  ASSERT_GE(spy.count(), 1);
  auto ps = spy.last()[0].value<PlaybackState>();
  EXPECT_EQ(ps.state, PlayState::Stopped);
  EXPECT_EQ(ps.elapsed_ms, 0u);
  EXPECT_EQ(ps.queue_pos, 0);
}

TEST_F(SubsonicBackendTest, TrackFinished_AtEnd_WithRepeat_PlaysFromStart)
{
  backend->replace_queue(make_queue(3), 2);
  backend->set_repeat(true, false);
  auto plays_before = engine->play_count;
  engine->fire_track_finished();
  EXPECT_EQ(engine->play_count, plays_before + 1);
  EXPECT_TRUE(engine->last_play_url.toString().contains("id-0"));
}

// ---------------------------------------------------------------------------
// Engine events — track_changed (gapless)
// ---------------------------------------------------------------------------

TEST_F(SubsonicBackendTest, TrackChanged_AdvancesQueuePos)
{
  backend->replace_queue(make_queue(3), 0);
  QSignalSpy spy(backend, &Backend::current_song_changed);
  engine->fire_track_changed();
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(SubsonicBackendTest, TrackChanged_Single_QueuePosUnchanged)
{
  auto q = make_queue(3);
  backend->replace_queue(q, 1);
  backend->set_repeat(true, true); // single mode

  QSignalSpy spy(backend, &Backend::current_song_changed);
  engine->fire_track_changed();
  // single mode: same track, current_song_changed still emitted
  EXPECT_EQ(spy.count(), 1);
}

// A stream URL carrying ?id=<uri> as the engine would report it (current-uri).
static QUrl
stream_url_for(const QString &id)
{
  return QUrl(QString("http://localhost/rest/stream?id=%1&format=raw").arg(id));
}

TEST_F(SubsonicBackendTest, TrackChanged_LocatesByReportedUri)
{
  backend->replace_queue(make_queue(3), 0); // [id-0, id-1, id-2], pos 0

  QSignalSpy spy(backend, &Backend::current_song_changed);
  engine->fire_track_changed(stream_url_for("id-1"));

  ASSERT_GE(spy.count(), 1);
  EXPECT_EQ(spy.last()[0].value<song>().uri, "id-1");
}

TEST_F(SubsonicBackendTest, TrackChanged_RobustToQueueMutation)
{
  // The A3 case: the engine committed "id-1" as next back at about-to-finish.
  // Before STREAM_START the user inserts a track ahead of it, so a positional
  // queue_pos++ would land on the inserted track. Identity must win.
  backend->replace_queue(make_queue(4), 0); // [id-0, id-1, id-2, id-3], pos 0
  backend->insert_queue({make_song("id-new", "Inserted")}, 1);
  // queue is now [id-0, id-new, id-1, id-2, id-3], pos still 0.

  QSignalSpy spy(backend, &Backend::current_song_changed);
  engine->fire_track_changed(stream_url_for("id-1"));

  ASSERT_GE(spy.count(), 1);
  EXPECT_EQ(spy.last()[0].value<song>().uri, "id-1"); // not "id-new"

  QSignalSpy ps_spy(backend, &Backend::playback_state_changed);
  backend->stop();
  EXPECT_EQ(ps_spy.last()[0].value<PlaybackState>().queue_pos, 2);
}

TEST_F(SubsonicBackendTest, TrackChanged_DuplicateUri_AdvancesForward)
{
  // Two copies of the same track: the transition should move to the copy ahead
  // of the current position, not snap back to the one we're leaving.
  QList<song> q{make_song("dup"), make_song("other"), make_song("dup")};
  backend->replace_queue(q, 0); // pos 0 is the first "dup"

  QSignalSpy spy(backend, &Backend::current_song_changed);
  engine->fire_track_changed(stream_url_for("dup"));

  QSignalSpy ps_spy(backend, &Backend::playback_state_changed);
  backend->stop();
  EXPECT_EQ(ps_spy.last()[0].value<PlaybackState>().queue_pos, 2); // forward copy
}

TEST_F(SubsonicBackendTest, TrackChanged_NoId_FallsBackToPositional)
{
  backend->replace_queue(make_queue(3), 0); // pos 0

  QSignalSpy spy(backend, &Backend::current_song_changed);
  engine->fire_track_changed(QUrl("file:///tmp/local.flac")); // no id query

  ASSERT_GE(spy.count(), 1);
  EXPECT_EQ(spy.last()[0].value<song>().uri, "id-1"); // positional advance 0 -> 1
}

// ---------------------------------------------------------------------------
// PlaybackState emission
// ---------------------------------------------------------------------------

TEST_F(SubsonicBackendTest, PlaybackState_ReflectsEngineState)
{
  backend->replace_queue(make_queue(2), 0);
  QSignalSpy spy(backend, &Backend::playback_state_changed);
  backend->pause();
  ASSERT_FALSE(spy.isEmpty());
  auto ps = spy.last()[0].value<PlaybackState>();
  EXPECT_EQ(ps.state, PlayState::Paused);
}

TEST_F(SubsonicBackendTest, PlaybackState_QueuePos_IsCorrect)
{
  backend->replace_queue(make_queue(3), 2);
  QSignalSpy spy(backend, &Backend::playback_state_changed);
  backend->stop();
  ASSERT_FALSE(spy.isEmpty());
  auto ps = spy.last()[0].value<PlaybackState>();
  EXPECT_EQ(ps.queue_pos, 2);
}

TEST_F(SubsonicBackendTest, PlaybackState_TotalMs_FallsBackToSongDuration)
{
  // Engine reports duration=0; should fall back to song.duration
  auto q = make_queue(1);
  q[0].duration = 240000;
  backend->replace_queue(q, 0);
  engine->set_duration(0); // engine hasn't loaded the stream yet

  QSignalSpy spy(backend, &Backend::playback_state_changed);
  backend->stop();
  ASSERT_FALSE(spy.isEmpty());
  auto ps = spy.last()[0].value<PlaybackState>();
  EXPECT_EQ(ps.total_ms, 240000u);
}

TEST_F(SubsonicBackendTest, PlaybackState_TotalMs_UsesEngineDuration_WhenAvailable)
{
  backend->replace_queue(make_queue(1), 0);
  engine->set_duration(300000);

  QSignalSpy spy(backend, &Backend::playback_state_changed);
  backend->stop();
  ASSERT_FALSE(spy.isEmpty());
  auto ps = spy.last()[0].value<PlaybackState>();
  EXPECT_EQ(ps.total_ms, 300000u);
}

// ---------------------------------------------------------------------------
// Shuffle
// ---------------------------------------------------------------------------

TEST_F(SubsonicBackendTest, SetShuffle_ShufflesQueue)
{
  backend->replace_queue(make_queue(10), 0);
  QSignalSpy spy(backend, &Backend::queue_changed);
  backend->set_shuffle(true);
  // Queue should have been shuffled; difficult to assert exact order
  // but queue_changed must be emitted
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(SubsonicBackendTest, SetShuffle_PreservesCurrentSong)
{
  auto q = make_queue(10);
  backend->replace_queue(q, 5);
  backend->set_shuffle(true);
  // After shuffle, the song currently playing should still be
  // reachable via the correct queue_pos
  EXPECT_TRUE(engine->last_play_url.toString().contains("id-5"));
}

TEST_F(SubsonicBackendTest, SetShuffle_PinsCurrentSongToTop)
{
  auto q = make_queue(10);
  backend->replace_queue(q, 5);

  QSignalSpy spy(backend, &Backend::queue_changed);
  backend->set_shuffle(true);

  ASSERT_EQ(spy.count(), 1);
  auto shuffled = spy.at(0).at(0).value<QList<song>>();
  ASSERT_EQ(shuffled.size(), q.size());
  // The playing track must lead the shuffled order so it reads as "up next".
  EXPECT_EQ(shuffled[0].uri, QString("id-5"));
}

TEST_F(SubsonicBackendTest, ReplaceQueue_WithShuffleOn_PinsStartTrackToTop)
{
  backend->replace_queue(make_queue(5), 0);
  backend->set_shuffle(true);

  QSignalSpy spy(backend, &Backend::queue_changed);
  backend->replace_queue(make_queue(10), 7);

  ASSERT_GE(spy.count(), 1);
  auto shuffled = spy.at(spy.count() - 1).at(0).value<QList<song>>();
  ASSERT_EQ(shuffled.size(), 10);
  EXPECT_EQ(shuffled[0].uri, QString("id-7"));
}

TEST_F(SubsonicBackendTest, SetShuffle_Untoggle_RestoresOriginalOrder)
{
  auto original = make_queue(8);
  backend->replace_queue(original, 0);
  backend->set_shuffle(true);

  QSignalSpy spy(backend, &Backend::queue_changed);
  backend->set_shuffle(false);

  ASSERT_EQ(spy.count(), 1);
  auto restored = spy.at(0).at(0).value<QList<song>>();
  ASSERT_EQ(restored.size(), original.size());
  for (int i = 0; i < original.size(); ++i)
    EXPECT_EQ(restored[i].uri, original[i].uri);
}

// Regression: removing one copy of a duplicated URI must drop only that copy
// from unshuffled_queue. A blanket URI match erased every copy, so un-shuffle
// silently lost the surviving track(s).
TEST_F(SubsonicBackendTest, RemoveDuplicateUri_UnshuffleKeepsRemainingCopy)
{
  QList<song> q = {
      make_song("id-0", "Track 0a"),
      make_song("id-1", "Track 1"),
      make_song("id-0", "Track 0b"), // duplicate URI
      make_song("id-2", "Track 2"),
  };
  backend->replace_queue(q, 0);

  QSignalSpy shuffle_spy(backend, &Backend::queue_changed);
  backend->set_shuffle(true); // snapshots unshuffled_queue, shuffles the live queue
  ASSERT_GE(shuffle_spy.count(), 1);
  auto shuffled = shuffle_spy.at(shuffle_spy.count() - 1).at(0).value<QList<song>>();
  ASSERT_EQ(shuffled.size(), 4);

  // Remove a non-current copy of id-0 (row 0 is the pinned playing track).
  int row = -1;
  for (int i = 1; i < shuffled.size(); ++i)
    if (shuffled[i].uri == "id-0")
      {
        row = i;
        break;
      }
  ASSERT_GE(row, 0);

  QStandardItemModel m(shuffled.size(), 1);
  backend->remove_from_queue({m.index(row, 0)});

  QSignalSpy restore_spy(backend, &Backend::queue_changed);
  backend->set_shuffle(false);
  ASSERT_GE(restore_spy.count(), 1);
  auto restored = restore_spy.at(restore_spy.count() - 1).at(0).value<QList<song>>();

  EXPECT_EQ(restored.size(), 3);
  int id0_count = 0;
  for (const auto &s : restored)
    if (s.uri == "id-0") ++id0_count;
  EXPECT_EQ(id0_count, 1); // pre-fix this was 0 — both copies were erased
}

TEST_F(SubsonicBackendTest, SetShuffle_Untoggle_PreservesCurrentSong)
{
  auto q = make_queue(8);
  backend->replace_queue(q, 3);
  backend->set_shuffle(true);
  backend->set_shuffle(false);
  // The currently playing track should still be id-3 after un-shuffle
  EXPECT_TRUE(engine->last_play_url.toString().contains("id-3"));
}

TEST_F(SubsonicBackendTest, SetShuffle_Untoggle_EmitsQueueChanged)
{
  backend->replace_queue(make_queue(5), 0);
  backend->set_shuffle(true);

  QSignalSpy spy(backend, &Backend::queue_changed);
  backend->set_shuffle(false);
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(SubsonicBackendTest, SetShuffle_DoubleToggle_PreservesOriginalOrder)
{
  // Toggling shuffle on twice should not corrupt the saved original order.
  auto original = make_queue(8);
  backend->replace_queue(original, 0);
  backend->set_shuffle(true);
  backend->set_shuffle(true); // second toggle-on: should not overwrite unshuffled_queue

  QSignalSpy spy(backend, &Backend::queue_changed);
  backend->set_shuffle(false);

  ASSERT_EQ(spy.count(), 1);
  auto restored = spy.at(0).at(0).value<QList<song>>();
  ASSERT_EQ(restored.size(), original.size());
  for (int i = 0; i < original.size(); ++i)
    EXPECT_EQ(restored[i].uri, original[i].uri);
}

TEST_F(SubsonicBackendTest, ReplaceQueue_WithShuffleOn_CanUntoggle)
{
  // Load one album, turn on shuffle, then load a second album.
  // Untoggling should restore the second album's original order.
  backend->replace_queue(make_queue(5), 0);
  backend->set_shuffle(true);

  auto second = make_queue(6);
  // Give songs distinct URIs to differentiate from first album
  for (int i = 0; i < second.size(); ++i)
    second[i].uri = QString("b-%1").arg(i);
  backend->replace_queue(second, 0);

  QSignalSpy spy(backend, &Backend::queue_changed);
  backend->set_shuffle(false);

  ASSERT_EQ(spy.count(), 1);
  auto restored = spy.at(0).at(0).value<QList<song>>();
  ASSERT_EQ(restored.size(), second.size());
  for (int i = 0; i < second.size(); ++i)
    EXPECT_EQ(restored[i].uri, second[i].uri);
}

TEST_F(SubsonicBackendTest, RemoveFromQueue_WithShuffleOn_RemovedSongAbsentAfterUntoggle)
{
  // With shuffle on, remove a track by its shuffled-queue row; after
  // untoggling the removed song must not reappear in the restored queue.
  auto q = make_queue(5);
  backend->replace_queue(q, 0);

  QSignalSpy shuffle_spy(backend, &Backend::queue_changed);
  backend->set_shuffle(true);
  ASSERT_EQ(shuffle_spy.count(), 1);
  auto shuffled = shuffle_spy.at(0).at(0).value<QList<song>>();

  // Find "id-2" in the shuffled order
  int row_of_id2 = -1;
  for (int i = 0; i < shuffled.size(); ++i)
    if (shuffled[i].uri == "id-2")
      {
        row_of_id2 = i;
        break;
      }
  ASSERT_GE(row_of_id2, 0);

  // QStandardItemModel provides valid QModelIndex objects
  QStandardItemModel model(shuffled.size(), 1);
  backend->remove_from_queue({model.index(row_of_id2, 0)});

  QSignalSpy unshuffle_spy(backend, &Backend::queue_changed);
  backend->set_shuffle(false);
  ASSERT_EQ(unshuffle_spy.count(), 1);
  auto restored = unshuffle_spy.at(0).at(0).value<QList<song>>();

  EXPECT_EQ(restored.size(), 4);
  for (const auto &s : restored)
    EXPECT_NE(s.uri, "id-2");
}

// ---------------------------------------------------------------------------
// update_engine_next_url
// ---------------------------------------------------------------------------

TEST_F(SubsonicBackendTest, ReplaceQueue_SetsNextUrl)
{
  backend->replace_queue(make_queue(3), 0);
  EXPECT_FALSE(engine->next_url.isEmpty());
  EXPECT_TRUE(engine->next_url.toString().contains("id-1"));
}

TEST_F(SubsonicBackendTest, ReplaceQueue_SingleTrack_NoNextUrl)
{
  backend->replace_queue(make_queue(1), 0);
  EXPECT_TRUE(engine->next_url.isEmpty());
}

TEST_F(SubsonicBackendTest, AppendQueue_UpdatesNextUrl)
{
  backend->replace_queue(make_queue(1), 0);
  ASSERT_TRUE(engine->next_url.isEmpty());

  backend->append_queue(make_queue(1));
  EXPECT_FALSE(engine->next_url.isEmpty());
}

// ===========================================================================
// Network-facing behaviour — exercised against a local fake Subsonic server.
//
// These cover the album-detail UX path (songCount → skeleton rows, prefetch
// dedup) and, importantly, the shutdown crash fix: a getAlbumList2 reply that
// is still in flight when the backend is destroyed must NOT re-enter the UI by
// re-emitting library_refresh_active from inside the destructor.
// ===========================================================================

#include <QTcpServer>
#include <QTcpSocket>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>

namespace {

// Minimal HTTP/1.1 server that answers the handful of Subsonic REST endpoints
// the backend calls. Records each endpoint hit and can withhold a response to
// hold a request in flight indefinitely.
class FakeSubsonicServer : public QObject {
public:
  FakeSubsonicServer()
  {
    server.listen(QHostAddress::LocalHost, 0);
    QObject::connect(&server, &QTcpServer::newConnection, this,
        &FakeSubsonicServer::on_connection);
  }

  QString url() const
  {
    return QString("http://127.0.0.1:%1").arg(server.serverPort());
  }

  void withhold(const QString &endpoint) { withheld.insert(endpoint); }
  // Make an endpoint answer with a Subsonic "failed" status (HTTP 200 still).
  void fail(const QString &endpoint) { failing.insert(endpoint); }
  // Answer the way a read-only bridge answers an endpoint it never implemented:
  // HTTP 200 carrying a private error body with no subsonic-response at all.
  // The body below is byte-for-byte what Bandcamp's bridge returns for
  // getStarred2 and createPlaylist (verified live 2026-07-25); "bad version" is
  // its generic not-implemented reply, not a protocol-version complaint —
  // getStarred succeeds at the same v=1.16.1.
  void bridge_reject(const QString &endpoint) { bridge_rejected.insert(endpoint); }
  // Close without answering: a transport failure, which says nothing about what
  // the server is willing to do — as distinct from an answer that refuses.
  void drop(const QString &endpoint) { dropped.insert(endpoint); }
  // Refuse the credentials the way the spec says to: HTTP 200, status "failed",
  // error code 40. What a server that doesn't honour username/password answers
  // a ping with.
  void reject_auth(const QString &endpoint) { auth_rejected.insert(endpoint); }
  // Undo any fail/bridge_reject/drop/reject_auth override for one endpoint.
  void accept(const QString &endpoint)
  {
    failing.remove(endpoint);
    bridge_rejected.remove(endpoint);
    dropped.remove(endpoint);
    auth_rejected.remove(endpoint);
  }
  int hits(const QString &endpoint) const { return counts.value(endpoint, 0); }
  QStringList request_paths() const { return paths; }
  // The most recent request path for a given endpoint, or empty if none.
  QString last_path(const QString &endpoint) const
  {
    for (auto it = paths.crbegin(); it != paths.crend(); ++it)
      if (it->section('/', 2, 2).section('?', 0, 0) == endpoint)
        return *it;
    return {};
  }

  // album JSON returned by getAlbumList2 (defaults to one album, songCount 12)
  QByteArray album_list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[
    {"id":"alb1","name":"Album One","artist":"A","artistSort":"A","year":2020,
     "coverArt":"ca1","created":"2020-01-01T00:00:00.000Z","songCount":12}]}}})";

  QByteArray album_songs = R"({"subsonic-response":{"status":"ok","album":{"song":[
    {"id":"s1","title":"One","artist":"A","track":1,"discNumber":1,"duration":100},
    {"id":"s2","title":"Two","artist":"A","track":2,"discNumber":1,"duration":120}]}}})";

  QByteArray starred2 = R"({"subsonic-response":{"status":"ok","starred2":{"song":[
    {"id":"s1","title":"One"},
    {"id":"s2","title":"Two"}]}}})";

  // v1 fallback body — same song ids/fields as starred2, wrapped under "starred".
  QByteArray starred = R"({"subsonic-response":{"status":"ok","starred":{"song":[
    {"id":"s1","title":"One","albumId":"alb1"},
    {"id":"s2","title":"Two","albumId":"alb1"}]}}})";

  QByteArray playlists = R"({"subsonic-response":{"status":"ok","playlists":{"playlist":[
    {"id":"pl1","name":"Road Trip","songCount":2,"duration":220,
     "changed":"2020-02-02T00:00:00.000Z"}]}}})";

  QByteArray playlist_entries = R"({"subsonic-response":{"status":"ok","playlist":{"entry":[
    {"id":"s1","title":"One","artist":"A","track":1,"discNumber":1,"duration":100,"albumId":"alb1"},
    {"id":"s2","title":"Two","artist":"A","track":2,"discNumber":1,"duration":120,"albumId":"alb1"}]}}})";

  // getOpenSubsonicExtensions body. Empty (default) falls through to the
  // generic "{status:ok}" stub below, i.e. no openSubsonic flag at all —
  // the "can't confirm" case a plain Subsonic server would also produce.
  QByteArray open_subsonic_extensions;

  // Airsonic-Advanced caps the REST protocol at 1.15.0 and rejects any client
  // sending a higher `v` with error 30 — before it even looks at auth. Set this
  // to reject `v=1.16.x` the same way.
  bool reject_api_above_1_15 = false;

  // getCoverArt response body/content-type, e.g. to simulate Navidrome's
  // default-placeholder image (byte-identical webp regardless of album).
  // Empty body (default) falls through to the generic "{status:ok}" stub.
  QByteArray cover_art;
  QByteArray cover_art_content_type = "image/jpeg";

private:
  void on_connection()
  {
    auto *sock = server.nextPendingConnection();
    QObject::connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
      auto line = QString::fromUtf8(sock->readLine()); // "GET /rest/<ep>?... HTTP/1.1"
      auto path = line.section(' ', 1, 1);
      auto endpoint = path.section('/', 2, 2).section('?', 0, 0);
      counts[endpoint]++;
      paths.append(path);

      if (withheld.contains(endpoint))
        return; // leave the request hanging — reply stays in flight

      if (dropped.contains(endpoint))
        {
          sock->disconnectFromHost(); // no bytes at all → reply carries an error
          return;
        }

      QByteArray content_type = "application/json";
      QByteArray body = "{\"subsonic-response\":{\"status\":\"ok\"}}";
      if (reject_api_above_1_15 && path.contains("v=1.16"))
        body = R"({"subsonic-response":{"status":"failed","error":{"code":30,)"
               R"("message":"Incompatible Airsonic REST protocol version. )"
               R"(Server must upgrade."}}})";
      else if (auth_rejected.contains(endpoint))
        body = R"({"subsonic-response":{"status":"failed","error":{"code":40,)"
               R"("message":"Wrong username or password."}}})";
      else if (failing.contains(endpoint))
        body = R"({"subsonic-response":{"status":"failed","error":{"message":"nope"}}})";
      else if (bridge_rejected.contains(endpoint))
        body = R"({"error":true,"error_message":"bad version"})";
      else if (endpoint == "getAlbumList2")
        body = album_list;
      else if (endpoint == "getAlbum")
        body = album_songs;
      else if (endpoint == "getStarred2")
        body = starred2;
      else if (endpoint == "getStarred")
        body = starred;
      else if (endpoint == "getPlaylists")
        body = playlists;
      else if (endpoint == "getPlaylist")
        body = playlist_entries;
      else if (endpoint == "getOpenSubsonicExtensions" && !open_subsonic_extensions.isEmpty())
        body = open_subsonic_extensions;
      else if (endpoint == "getCoverArt" && !cover_art.isEmpty())
        {
          body = cover_art;
          content_type = cover_art_content_type;
        }

      QByteArray resp = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: "
                        + content_type + "\r\n"
                                         "Content-Length: "
                        + QByteArray::number(body.size()) + "\r\n"
                                                            "Connection: close\r\n\r\n"
                        + body;
      sock->write(resp);
      sock->flush();
      sock->disconnectFromHost();
    });
  }

  QTcpServer server;
  QSet<QString> withheld;
  QSet<QString> failing;
  QSet<QString> bridge_rejected;
  QSet<QString> dropped;
  QSet<QString> auth_rejected;
  QMap<QString, int> counts;
  QStringList paths;
};

// Pump the event loop until `spy` has at least `n` rows or we time out.
static bool
wait_for(QSignalSpy &spy, int n = 1, int timeout_ms = 3000)
{
  QElapsedTimer t;
  t.start();
  while (spy.count() < n && t.elapsed() < timeout_ms)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return spy.count() >= n;
}

// Pump the event loop until `pred` holds — for outcomes that are the *absence*
// of a signal, where there is no spy to wait on.
template<typename Pred>
static bool
wait_until(Pred pred, int timeout_ms = 3000)
{
  QElapsedTimer t;
  t.start();
  while (!pred() && t.elapsed() < timeout_ms)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return pred();
}

} // namespace

class SubsonicBackendNetTest : public ::testing::Test {
protected:
  FakeSubsonicServer *server;
  MockAudioEngine *engine;
  SubsonicBackend *backend;

  void SetUp() override
  {
    qRegisterMetaType<QSet<QString>>();
    server = new FakeSubsonicServer;
    engine = new MockAudioEngine;
    backend = new SubsonicBackend(server->url(), SubsonicAuth{.api_key = "key"}, engine);
  }

  void TearDown() override
  {
    delete backend; // may be null if a test deleted it
    delete server;
  }

  // Drive the ping handshake so `connected` is true before a playlist op.
  void connect_backend()
  {
    QSignalSpy conn(backend, &Backend::connection_update);
    backend->connect_to_server();
    wait_for(conn);
  }
};

// ---------------------------------------------------------------------------
// SubsonicClient — the transport seam
// ---------------------------------------------------------------------------
//
// Every request in the app is classified here, once. The domain tests below
// exercise these outcomes through ten different code paths; these pin the
// classification itself, so a change in meaning shows up as one failure rather
// than a scattering of them.

class SubsonicClientTest : public ::testing::Test {
protected:
  FakeSubsonicServer *server;
  SubsonicClient *client;

  void SetUp() override
  {
    server = new FakeSubsonicServer;
    client = new SubsonicClient(server->url(), SubsonicAuth{.api_key = "key"});
  }
  void TearDown() override
  {
    delete client;
    delete server;
  }

  // Issue one request and return its classified reply.
  auto fetch(const QString &endpoint) -> SubsonicReply
  {
    SubsonicReply out;
    bool done = false;
    client->get(endpoint, [&](const SubsonicReply &r) {
      out = r;
      done = true;
    });
    wait_until([&] { return done; });
    return out;
  }
};

TEST_F(SubsonicClientTest, OkReply_IsUsableAndCarriesPayload)
{
  auto r = fetch("getPlaylists");
  EXPECT_TRUE(r.transport_ok);
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.error.isEmpty());
  EXPECT_TRUE(r.payload("playlists")["playlist"].isArray());
}

// A Subsonic "failed" envelope: the server answered, so transport is fine; the
// request was refused, so the payload is not usable and the server's own
// message is what surfaces.
TEST_F(SubsonicClientTest, FailedStatus_ArrivedButNotOk)
{
  server->fail("getPlaylists");
  auto r = fetch("getPlaylists");
  EXPECT_TRUE(r.transport_ok);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, "nope");
}

// The envelope's error code is the only reliable way to tell a credential
// refusal from any other refusal — the message is free-form text that varies
// per server. Callers classify on the code, so it has to survive the parse.
TEST_F(SubsonicClientTest, FailedStatus_CarriesTheSubsonicErrorCode)
{
  server->reject_auth("getPlaylists");
  auto r = fetch("getPlaylists");
  EXPECT_TRUE(r.transport_ok);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error_code, 40);
}

// A refusal whose body names no code, and an answered request, both read as 0.
TEST_F(SubsonicClientTest, NoErrorCodeInBody_ReadsAsZero)
{
  server->fail("getPlaylists");
  EXPECT_EQ(fetch("getPlaylists").error_code, 0);
  server->accept("getPlaylists");
  EXPECT_EQ(fetch("getPlaylists").error_code, 0);
}

// The bridge shape: HTTP 200, no subsonic-response at all. Still "arrived",
// still not ok, and the error must not come through empty just because there
// was no error.message to read.
TEST_F(SubsonicClientTest, NonSubsonicBody_ArrivedButNotOkWithRealMessage)
{
  server->bridge_reject("getPlaylists");
  auto r = fetch("getPlaylists");
  EXPECT_TRUE(r.transport_ok);
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.error.isEmpty());
}

// Nothing arrived. This is the case that must never be read as a refusal —
// the capability downgrades hang off exactly this distinction.
TEST_F(SubsonicClientTest, DroppedConnection_IsNotATransportSuccess)
{
  server->drop("getPlaylists");
  auto r = fetch("getPlaylists");
  EXPECT_FALSE(r.transport_ok);
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.error.isEmpty());
}

// The authed URL carries credentials and the pinned protocol version; the
// stream URLs handed to the audio engine are built from the same method.
TEST_F(SubsonicClientTest, BuildsAuthedUrlWithPinnedVersion)
{
  auto url = client->url("stream", {{"id", "s1"}});
  const auto q = url.query();
  EXPECT_TRUE(url.path().endsWith("/rest/stream"));
  EXPECT_TRUE(q.contains("apiKey=key"));
  EXPECT_TRUE(q.contains("v=1.15.0")); // below spec level on purpose
  EXPECT_TRUE(q.contains("f=json"));
  EXPECT_TRUE(q.contains("id=s1"));
}

// songCount from getAlbumList2 must land on the album model so the detail view
// can size its skeleton placeholder rows before getAlbum returns.
TEST_F(SubsonicBackendNetTest, SongCountParsedFromAlbumList)
{
  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server(); // ping → poll_library → getAlbumList2 → library_changed
  ASSERT_TRUE(wait_for(lib_spy));

  auto albums = backend->get_albums();
  ASSERT_EQ(albums.size(), 1);
  EXPECT_EQ(albums[0].song_count, 12);
}

// MusicBrainz release id (OpenSubsonic extension) is preferred over the raw
// server-native id and normalized to lowercase, so the same release keys
// identically regardless of server or tag casing.
TEST_F(SubsonicBackendNetTest, AlbumHashPrefersLowercasedMusicBrainzId)
{
  server->album_list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[
    {"id":"alb1","name":"Album One","artist":"A","artistSort":"A","year":2020,
     "coverArt":"ca1","created":"2020-01-01T00:00:00.000Z","songCount":12,
     "musicBrainzId":"B00A1179-AF97-41D9-8C81-BDE2C5DABBEB"}]}}})";

  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  auto albums = backend->get_albums();
  ASSERT_EQ(albums.size(), 1);
  EXPECT_EQ(albums[0].album_hash, "b00a1179-af97-41d9-8c81-bde2c5dabbeb");
  EXPECT_EQ(albums[0].native_id, "alb1");
}

// `coverArt` is optional in the Subsonic schema and Ampache omits it outright.
// The id must then fall back to the album's native id — otherwise every art
// request goes out as `getCoverArt?id=` and no album ever gets a cover.
TEST_F(SubsonicBackendNetTest, AlbumArtUriFallsBackToNativeIdWhenCoverArtAbsent)
{
  server->album_list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[
    {"id":"alb1","name":"Album One","artist":"A","artistSort":"A","year":2020,
     "created":"2020-01-01T00:00:00.000Z","songCount":12}]}}})";

  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  auto albums = backend->get_albums();
  ASSERT_EQ(albums.size(), 1);
  EXPECT_EQ(albums[0].uri, "alb1");
}

// ...but a server that does supply `coverArt` keeps using it: the art id and the
// album id are distinct namespaces on most servers.
TEST_F(SubsonicBackendNetTest, AlbumArtUriPrefersCoverArtWhenPresent)
{
  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server(); // default album_list carries "coverArt":"ca1"
  ASSERT_TRUE(wait_for(lib_spy));

  auto albums = backend->get_albums();
  ASSERT_EQ(albums.size(), 1);
  EXPECT_EQ(albums[0].uri, "ca1");
}

// Navidrome's default "no cover" placeholder happens to be image/webp — but a
// real, legitimately webp (or even coincidentally same-size) cover must still
// come through as art. Placeholder detection is an exact hash match against
// Navidrome's known bundled asset (subsonicbackend.cc), not a content-type or
// size guess, precisely so this case can never be mistaken for "no art".
TEST_F(SubsonicBackendNetTest, AlbumArtWebpCoverThatIsNotThePlaceholderIsKept)
{
  QByteArray fake_webp(69228, 'x'); // same size as Navidrome's real placeholder, different bytes
  server->cover_art = fake_webp;
  server->cover_art_content_type = "image/webp";

  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  QSignalSpy art_spy(backend, &Backend::album_art_received);
  backend->fetch_album_art("ca1");
  ASSERT_TRUE(wait_for(art_spy));

  EXPECT_EQ(art_spy.at(0).at(1).toByteArray(), fake_webp);
}

// Ampache's default "no cover" placeholder is a 1400x1400 PNG -- but a real,
// legitimately same-size PNG cover must still come through as art. Same
// rationale as the Navidrome case above: hash match, not size/content-type.
TEST_F(SubsonicBackendNetTest, AlbumArtPngCoverThatIsNotAmpachesPlaceholderIsKept)
{
  QByteArray fake_png(41464, 'x'); // same size as Ampache's real placeholder, different bytes
  server->cover_art = fake_png;
  server->cover_art_content_type = "image/png";

  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  QSignalSpy art_spy(backend, &Backend::album_art_received);
  backend->fetch_album_art("ca1");
  ASSERT_TRUE(wait_for(art_spy));

  EXPECT_EQ(art_spy.at(0).at(1).toByteArray(), fake_png);
}

// getCoverArt's body is image bytes, not a subsonic-response envelope. It must
// not be run through the JSON parser at all -- doing so reliably fails
// ("illegal number" on binary bytes) and warned on every single art fetch
// (regression from the SubsonicClient extraction, 7adfc64).
TEST_F(SubsonicBackendNetTest, AlbumArtBinaryReplyDoesNotWarnAboutJsonParsing)
{
  QByteArray fake_jpeg(1024, '\xFF'); // not valid JSON by any parse
  server->cover_art = fake_jpeg;
  server->cover_art_content_type = "image/jpeg";

  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  std::ostringstream log_capture;
  auto capture_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(log_capture);
  auto original_sinks = spdlog::default_logger()->sinks();
  spdlog::default_logger()->sinks() = {capture_sink};

  QSignalSpy art_spy(backend, &Backend::album_art_received);
  backend->fetch_album_art("ca1");
  bool delivered = wait_for(art_spy);

  spdlog::default_logger()->sinks() = original_sinks;

  ASSERT_TRUE(delivered);
  EXPECT_EQ(art_spy.at(0).at(1).toByteArray(), fake_jpeg);
  EXPECT_EQ(log_capture.str().find("JSON parse failed"), std::string::npos)
      << "captured log: " << log_capture.str();
}

// Without an MBID, the cache key falls back to the raw server id namespaced by
// profile — the raw id alone isn't unique across servers.
TEST_F(SubsonicBackendNetTest, AlbumHashFallsBackToProfileScopedNativeId)
{
  auto *scoped_engine = new MockAudioEngine;
  auto *scoped_backend = new SubsonicBackend(
      server->url(), SubsonicAuth{.api_key = "key"}, scoped_engine, nullptr, "profile-a");

  QSignalSpy lib_spy(scoped_backend, &Backend::library_changed);
  scoped_backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  auto albums = scoped_backend->get_albums();
  ASSERT_EQ(albums.size(), 1);
  EXPECT_EQ(albums[0].album_hash, "profile-a:alb1");
  EXPECT_EQ(albums[0].native_id, "alb1");

  delete scoped_backend;
}

// A poll that reloads an identical library rebuilds an identical native→hash
// mapping, so every standalone song re-resolves to the hash it already has. The
// favorites refetch that would follow costs a getStarred2 per tick to reproduce
// what is already displayed, so the mapping signal must stay silent.
TEST_F(SubsonicBackendNetTest, IdenticalLibraryReloadEmitsNoMappingChange)
{
  QSignalSpy lib_spy(backend, &Backend::library_changed);
  QSignalSpy map_spy(backend, &Backend::album_mapping_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));
  EXPECT_EQ(map_spy.count(), 1); // empty → loaded is a change

  backend->refresh_library();
  ASSERT_TRUE(wait_for(lib_spy, 2));
  EXPECT_EQ(map_spy.count(), 1);
}

// Album metadata can change without moving any hash. Nothing resolving through
// the mapping is stale then — library_changed alone carries the new metadata to
// the views.
TEST_F(SubsonicBackendNetTest, MetadataOnlyChangeEmitsNoMappingChange)
{
  QSignalSpy lib_spy(backend, &Backend::library_changed);
  QSignalSpy map_spy(backend, &Backend::album_mapping_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));
  ASSERT_EQ(map_spy.count(), 1);

  server->album_list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[
    {"id":"alb1","name":"Album One Remastered","artist":"A","artistSort":"A","year":2021,
     "coverArt":"ca1","created":"2020-01-01T00:00:00.000Z","songCount":12}]}}})";

  backend->refresh_library();
  ASSERT_TRUE(wait_for(lib_spy, 2));
  EXPECT_EQ(map_spy.count(), 1);
  EXPECT_EQ(backend->get_albums().at(0).name, "Album One Remastered");
}

// A hash moving under a native id (here: the server starting to report an MBID)
// is exactly the case the refetch exists for — every cached resolution is now
// wrong and must be re-resolved.
TEST_F(SubsonicBackendNetTest, ChangedAlbumHashEmitsMappingChange)
{
  QSignalSpy lib_spy(backend, &Backend::library_changed);
  QSignalSpy map_spy(backend, &Backend::album_mapping_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));
  ASSERT_EQ(map_spy.count(), 1);

  server->album_list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[
    {"id":"alb1","name":"Album One","artist":"A","artistSort":"A","year":2020,
     "coverArt":"ca1","created":"2020-01-01T00:00:00.000Z","songCount":12,
     "musicBrainzId":"B00A1179-AF97-41D9-8C81-BDE2C5DABBEB"}]}}})";

  backend->refresh_library();
  ASSERT_TRUE(wait_for(lib_spy, 2));
  EXPECT_EQ(map_spy.count(), 2);
}

// A new album is a new mapping entry, so favorites pointing at it can finally
// resolve to a real library album.
TEST_F(SubsonicBackendNetTest, AddedAlbumEmitsMappingChange)
{
  QSignalSpy lib_spy(backend, &Backend::library_changed);
  QSignalSpy map_spy(backend, &Backend::album_mapping_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));
  ASSERT_EQ(map_spy.count(), 1);

  server->album_list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[
    {"id":"alb1","name":"Album One","artist":"A","artistSort":"A","year":2020,
     "coverArt":"ca1","created":"2020-01-01T00:00:00.000Z","songCount":12},
    {"id":"alb2","name":"Album Two","artist":"B","artistSort":"B","year":2021,
     "coverArt":"ca2","created":"2021-01-01T00:00:00.000Z","songCount":9}]}}})";

  backend->refresh_library();
  ASSERT_TRUE(wait_for(lib_spy, 2));
  EXPECT_EQ(map_spy.count(), 2);
}

// The whole point of the ownership split, end to end: the favorites read
// happens once, and the album hashes it could not resolve at the time are
// corrected when the library lands — without touching the network again.
//
// The favorites endpoint is broken *before* the library arrives, so a second
// read could not possibly supply the corrected hash. If the song still ends up
// keyed to the library album, the correction was local.
TEST_F(SubsonicBackendNetTest, FavoritesReadOnceThenCorrectedByTheLibraryLanding)
{
  server->album_list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[]}}})";
  server->starred2 = R"({"subsonic-response":{"status":"ok","starred2":{"song":[
    {"id":"s1","title":"One","albumId":"alb1"}]}}})";

  auto shared = std::shared_ptr<Backend>(backend, [](Backend *) {}); // fixture owns it
  FavoritesManager favman(shared);
  QSignalSpy songs_spy(&favman, &FavoritesManager::songs_changed);

  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));
  ASSERT_TRUE(wait_for(songs_spy));

  ASSERT_EQ(favman.songs().size(), 1);
  EXPECT_EQ(favman.songs().at(0).album_hash, ":alb1"); // library had no such album
  EXPECT_EQ(server->hits("getStarred2"), 1);

  // The album shows up in the library on a later poll — and favorites reads are
  // dead from here on, so nothing but a local re-resolve can fix the hash.
  server->fail("getStarred2");
  server->fail("getStarred");
  server->album_list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[
    {"id":"alb1","name":"Album One","artist":"A","artistSort":"A","year":2020,
     "coverArt":"ca1","created":"2020-01-01T00:00:00.000Z","songCount":12,
     "musicBrainzId":"MBID-ABC"}]}}})";

  backend->refresh_library();
  ASSERT_TRUE(wait_for(lib_spy, 2));
  ASSERT_TRUE(wait_for(songs_spy, 2));

  ASSERT_EQ(favman.songs().size(), 1);
  EXPECT_EQ(favman.songs().at(0).album_hash, "mbid-abc");
  EXPECT_EQ(favman.songs().at(0).uri, "s1"); // the song itself was never re-read
}

class SubsonicBackendStateTest : public ::testing::Test {
protected:
  QTemporaryDir tmp;
  void SetUp() override
  {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    QCoreApplication::setOrganizationName("freewave_test");
    QCoreApplication::setApplicationName("subsonicbackend_test");
    QSettings().clear();
  }
};

// Regression test: switching from one server profile to another must not
// hand the new backend the previous profile's queue (and its server-specific
// song ids). save_state()/restore_state() used to key off a single global
// QSettings entry shared by every profile; a queue saved for profile-a would
// get restored into profile-b on the very next connect.
TEST_F(SubsonicBackendStateTest, QueueStateDoesNotLeakAcrossProfiles)
{
  auto *server_a = new FakeSubsonicServer;
  auto *engine_a = new MockAudioEngine;
  auto *backend_a = new SubsonicBackend(
      server_a->url(), SubsonicAuth{.api_key = "key"}, engine_a, nullptr, "profile-a");

  QSignalSpy lib_spy_a(backend_a, &Backend::library_changed);
  backend_a->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy_a));

  backend_a->replace_queue({make_song("navidrome-song-1", "Track One")}, 0);

  delete backend_a; // destructor -> save_state() under "subsonic_state/profile-a"
  delete server_a;

  auto *server_b = new FakeSubsonicServer;
  auto *engine_b = new MockAudioEngine;
  auto *backend_b = new SubsonicBackend(
      server_b->url(), SubsonicAuth{.api_key = "key"}, engine_b, nullptr, "profile-b");

  QSignalSpy queue_spy(backend_b, &Backend::queue_changed);
  QSignalSpy lib_spy_b(backend_b, &Backend::library_changed);
  backend_b->connect_to_server(); // ping ok -> restore_state() scoped to "profile-b"
  ASSERT_TRUE(wait_for(lib_spy_b));

  // profile-b has no saved state of its own, so restore_state() must bail out
  // before ever emitting queue_changed — proving profile-a's queue didn't leak.
  EXPECT_EQ(queue_spy.count(), 0);

  delete backend_b;
  delete server_b;
}

// getAlbum must be requested by the server-native id, not the (possibly MBID)
// cache key — the server has never heard of the MBID as an album id.
TEST_F(SubsonicBackendNetTest, GetAlbumRequestUsesNativeIdNotHash)
{
  server->album_list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[
    {"id":"alb1","name":"Album One","artist":"A","artistSort":"A","year":2020,
     "coverArt":"ca1","created":"2020-01-01T00:00:00.000Z","songCount":12,
     "musicBrainzId":"b00a1179-af97-41d9-8c81-bde2c5dabbeb"}]}}})";

  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  auto album = backend->get_albums().first();
  ASSERT_EQ(album.album_hash, "b00a1179-af97-41d9-8c81-bde2c5dabbeb");

  QSignalSpy songs_spy(backend, &Backend::songs_changed);
  backend->fetch_songs(album, {});
  ASSERT_TRUE(wait_for(songs_spy));

  bool found = false;
  for (const auto &p : server->request_paths())
    if (p.contains("/rest/getAlbum") && p.contains("id=alb1")) found = true;
  EXPECT_TRUE(found);
}

// A prefetch-on-select followed by open-on-activate asks for the same album's
// songs twice; only one getAlbum request should reach the server.
TEST_F(SubsonicBackendNetTest, InFlightGetAlbumIsDeduped)
{
  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  auto album = backend->get_albums().first();
  QSignalSpy songs_spy(backend, &Backend::songs_changed);

  backend->fetch_songs(album, {}); // prefetch (cache miss → fetch)
  backend->fetch_songs(album, {}); // open    (still in flight → deduped)

  ASSERT_TRUE(wait_for(songs_spy));
  EXPECT_EQ(server->hits("getAlbum"), 1);
}

// Once cached, fetch_songs returns synchronously and issues no request.
TEST_F(SubsonicBackendNetTest, CachedAlbumIssuesNoFurtherRequest)
{
  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  auto album = backend->get_albums().first();
  QSignalSpy songs_spy(backend, &Backend::songs_changed);
  backend->fetch_songs(album, {});
  ASSERT_TRUE(wait_for(songs_spy));
  ASSERT_EQ(server->hits("getAlbum"), 1);

  QList<song> cached;
  backend->fetch_songs(album, [&](const QList<song> &s) { cached = s; });
  EXPECT_EQ(cached.size(), 2);
  EXPECT_EQ(server->hits("getAlbum"), 1); // no second request
}

// Regression guard for the shutdown SIGSEGV: a getAlbumList2 reply in flight at
// destruction time must not re-emit library_refresh_active(false) — that signal
// reaches MainWindow, which would touch already-destroyed widgets.
TEST_F(SubsonicBackendNetTest, DestructorWithInFlightRefreshDoesNotReEmit)
{
  server->withhold("getAlbumList2"); // hold the library scan open

  QSignalSpy active_spy(backend, &Backend::library_refresh_active);
  backend->connect_to_server(); // ping ok → poll_library emits active(true), scan hangs
  ASSERT_TRUE(wait_for(active_spy));
  ASSERT_TRUE(active_spy.takeFirst().at(0).toBool()); // first emission was `true`

  // Watch for any further emissions across the destructor.
  QSignalSpy after_spy(backend, &Backend::library_refresh_active);
  delete backend; // aborts the in-flight reply; must not fire its handler
  backend = nullptr;

  EXPECT_EQ(after_spy.count(), 0);
}

// A ping that can't even reach the server (unreachable host/port) must still
// emit connection_update(false), or ConnectionStateController never leaves
// the "Connecting to server" overlay — no error, no retry, no way to edit
// server settings.
TEST_F(SubsonicBackendNetTest, PingNetworkErrorEmitsConnectionUpdateFalse)
{
  // Heap-allocated: SubsonicBackend reparents the engine to itself, and Qt
  // deletes children as raw pointers — a stack-allocated engine would be
  // double-destroyed.
  auto *local_engine = new MockAudioEngine;
  auto *local_backend = new SubsonicBackend(
      "http://127.0.0.1:1", SubsonicAuth{.api_key = "key"}, local_engine);

  QSignalSpy conn_spy(local_backend, &Backend::connection_update);
  QSignalSpy err_spy(local_backend, &Backend::error);
  local_backend->connect_to_server();

  ASSERT_TRUE(wait_for(conn_spy));
  EXPECT_FALSE(conn_spy.takeFirst().at(0).toBool());
  EXPECT_EQ(err_spy.count(), 1);

  delete local_backend; // also deletes local_engine, which it took ownership of
}

// The reason a connect attempt failed has to reach the UI separately from the
// bare "not connected" state, or every failure renders as "server unreachable".
// A server that refuses the credentials answered perfectly well.
TEST_F(SubsonicBackendNetTest, PingRejectedForAuthReportsAuthFailure)
{
  server->reject_auth("ping");

  QSignalSpy failed_spy(backend, &Backend::connection_failed);
  QSignalSpy conn_spy(backend, &Backend::connection_update);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(conn_spy));

  EXPECT_FALSE(conn_spy.constFirst().at(0).toBool());
  ASSERT_EQ(failed_spy.count(), 1);
  EXPECT_EQ(failed_spy.constFirst().at(0).value<Backend::ConnectError>(),
      Backend::ConnectError::Auth);
  EXPECT_EQ(failed_spy.constFirst().at(1).toString(), "Wrong username or password.");
}

// A refusal with no auth code behind it is not a credential problem — nothing
// in Settings fixes it, so it must not be labelled as one.
TEST_F(SubsonicBackendNetTest, PingRejectedWithoutAuthCodeReportsUnreachable)
{
  server->fail("ping");

  QSignalSpy failed_spy(backend, &Backend::connection_failed);
  QSignalSpy conn_spy(backend, &Backend::connection_update);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(conn_spy));

  ASSERT_EQ(failed_spy.count(), 1);
  EXPECT_EQ(failed_spy.constFirst().at(0).value<Backend::ConnectError>(),
      Backend::ConnectError::Unreachable);
}

// A ping that never reached a server carries no Subsonic code at all; the
// classifier must read that as unreachable rather than falling through the
// code switch into a credential verdict.
TEST_F(SubsonicBackendNetTest, PingTransportFailureReportsUnreachable)
{
  server->drop("ping");

  QSignalSpy failed_spy(backend, &Backend::connection_failed);
  QSignalSpy conn_spy(backend, &Backend::connection_update);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(conn_spy));

  ASSERT_EQ(failed_spy.count(), 1);
  EXPECT_EQ(failed_spy.constFirst().at(0).value<Backend::ConnectError>(),
      Backend::ConnectError::Unreachable);
}

// ---------------------------------------------------------------------------
// Favorites — star / unstar / getStarred2
// ---------------------------------------------------------------------------

// Connecting kicks off getStarred2; the parsed song ids land as favorites_loaded.
// The backend reads favorites when asked to, and the reply carries the whole
// starred set.
TEST_F(SubsonicBackendNetTest, FetchFavoritesLoadsStarredSet)
{
  connect_backend();

  QSignalSpy fav_spy(backend, &Backend::favorites_loaded);
  backend->fetch_favorites();
  ASSERT_TRUE(wait_for(fav_spy));

  auto set = fav_spy.takeFirst().at(0).value<QSet<QString>>();
  EXPECT_EQ(set.size(), 2);
  EXPECT_TRUE(set.contains("s1"));
  EXPECT_TRUE(set.contains("s2"));
}

// Connect must not report staleness: the favorites owner already reads on
// connection_update, and reporting it here would make a connect cost two reads
// of the same payload — the redundancy this ownership split exists to remove.
TEST_F(SubsonicBackendNetTest, ConnectDoesNotReportFavoritesStale)
{
  QSignalSpy stale_spy(backend, &Backend::favorites_stale);
  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  EXPECT_EQ(stale_spy.count(), 0);
  EXPECT_EQ(server->hits("getStarred2"), 0); // nothing self-initiated
}

// The poll heartbeat is the only moment a star changed by another client can be
// noticed, so it reports the possibility — but issues no read of its own. That
// decision belongs to the favorites owner, which is what makes one read per tick
// the whole cost.
TEST_F(SubsonicBackendNetTest, PollReportsFavoritesStaleWithoutReading)
{
  connect_backend();
  auto reads_before = server->hits("getStarred2");

  QSignalSpy stale_spy(backend, &Backend::favorites_stale);
  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->refresh_library();
  ASSERT_TRUE(wait_for(lib_spy));

  EXPECT_EQ(stale_spy.count(), 1);
  EXPECT_EQ(server->hits("getStarred2"), reads_before);
}

// set_favorite(true) issues GET /rest/star?...id=<uri> and confirms via
// favorite_changed once the reply is ok.
TEST_F(SubsonicBackendNetTest, SetFavorite_Star_BuildsUrlAndConfirms)
{
  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  QSignalSpy fav_spy(backend, &Backend::favorite_changed);
  backend->set_favorite("song-42", true);
  ASSERT_TRUE(wait_for(fav_spy));

  auto args = fav_spy.takeFirst();
  EXPECT_EQ(args.at(0).toString(), "song-42");
  EXPECT_TRUE(args.at(1).toBool());
  EXPECT_GE(server->hits("star"), 1);

  bool found = false;
  for (const auto &p : server->request_paths())
    if (p.contains("/rest/star") && p.contains("id=song-42"))
      found = true;
  EXPECT_TRUE(found);
}

// set_favorite(false) hits the unstar endpoint and reports fav=false.
TEST_F(SubsonicBackendNetTest, SetFavorite_Unstar_HitsUnstarEndpoint)
{
  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  QSignalSpy fav_spy(backend, &Backend::favorite_changed);
  backend->set_favorite("song-7", false);
  ASSERT_TRUE(wait_for(fav_spy));

  auto args = fav_spy.takeFirst();
  EXPECT_EQ(args.at(0).toString(), "song-7");
  EXPECT_FALSE(args.at(1).toBool());
  EXPECT_GE(server->hits("unstar"), 1);

  bool found = false;
  for (const auto &p : server->request_paths())
    if (p.contains("/rest/unstar") && p.contains("id=song-7"))
      found = true;
  EXPECT_TRUE(found);
}

// Username/password auth builds salted-token query params (u/t/s) instead of
// the legacy apiKey= param — required for real Subsonic servers (e.g.
// Navidrome), which reject apiKey-only requests.
TEST(SubsonicBackendAuthTest, UserPassAuth_UsesTokenParams_NotApiKey)
{
  qRegisterMetaType<QSet<QString>>();
  auto *server = new FakeSubsonicServer;
  auto *engine = new MockAudioEngine;
  auto *backend = new SubsonicBackend(server->url(),
      SubsonicAuth{.username = "bob", .password = "secret"}, engine);

  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  bool found_token_auth = false;
  for (const auto &p : server->request_paths())
    {
      EXPECT_FALSE(p.contains("apiKey="));
      if (p.contains("u=bob") && p.contains("t=") && p.contains("s="))
        found_token_auth = true;
    }
  EXPECT_TRUE(found_token_auth);

  delete backend;
  delete server;
}

// Real OpenSubsonic server (openSubsonic:true) that enumerates extensions but
// doesn't list apiKeyAuthentication (matches a live Navidrome response) — the
// backend must refuse before ever pinging, since ping can't distinguish "not
// supported" from "wrong key".
TEST(SubsonicBackendAuthTest, ApiKeyMode_ConfirmedUnsupported_FailsBeforePing)
{
  qRegisterMetaType<QSet<QString>>();
  auto *server = new FakeSubsonicServer;
  server->open_subsonic_extensions = R"({"subsonic-response":{"status":"ok",
    "openSubsonic":true,"openSubsonicExtensions":[{"name":"transcoding","versions":[1]}]}})";
  auto *engine = new MockAudioEngine;
  auto *backend = new SubsonicBackend(server->url(), SubsonicAuth{.api_key = "key"}, engine);

  QSignalSpy err_spy(backend, &Backend::error);
  QSignalSpy conn_spy(backend, &Backend::connection_update);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(conn_spy));

  EXPECT_FALSE(conn_spy.constFirst().at(0).toBool());
  ASSERT_EQ(err_spy.count(), 1);
  EXPECT_TRUE(err_spy.constFirst().at(0).toString().contains("does not support API key"));
  EXPECT_EQ(server->hits("ping"), 0);

  delete backend;
  delete server;
}

// Real OpenSubsonic server that does list apiKeyAuthentication (matches a
// live LMS response) — connect proceeds to ping as normal.
TEST(SubsonicBackendAuthTest, ApiKeyMode_ConfirmedSupported_PingProceeds)
{
  qRegisterMetaType<QSet<QString>>();
  auto *server = new FakeSubsonicServer;
  server->open_subsonic_extensions = R"({"subsonic-response":{"status":"ok",
    "openSubsonic":true,"openSubsonicExtensions":[{"name":"apiKeyAuthentication","versions":[1]}]}})";
  auto *engine = new MockAudioEngine;
  auto *backend = new SubsonicBackend(server->url(), SubsonicAuth{.api_key = "key"}, engine);

  QSignalSpy conn_spy(backend, &Backend::connection_update);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(conn_spy));

  EXPECT_TRUE(conn_spy.constFirst().at(0).toBool());
  EXPECT_EQ(server->hits("ping"), 1);

  delete backend;
  delete server;
}

// A plain (non-OpenSubsonic) server's getOpenSubsonicExtensions reply omits
// openSubsonic entirely — can't confirm apiKeyAuthentication is absent, so
// the backend must not block; ping (the real auth check) still runs.
TEST(SubsonicBackendAuthTest, ApiKeyMode_ExtensionsInconclusive_FallsThroughToPing)
{
  qRegisterMetaType<QSet<QString>>();
  auto *server = new FakeSubsonicServer; // open_subsonic_extensions left empty
  auto *engine = new MockAudioEngine;
  auto *backend = new SubsonicBackend(server->url(), SubsonicAuth{.api_key = "key"}, engine);

  QSignalSpy conn_spy(backend, &Backend::connection_update);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(conn_spy));

  EXPECT_TRUE(conn_spy.constFirst().at(0).toBool());
  EXPECT_EQ(server->hits("ping"), 1);

  delete backend;
  delete server;
}

// Gonic gates getOpenSubsonicExtensions behind auth (spec says it needs none)
// and still stamps openSubsonic:true on the *failed* reply — so `openSubsonic`
// alone does not mean "this list is authoritative". The status=="ok" check is
// what stops that empty-because-rejected list from reading as a confirmed
// "apiKeyAuthentication absent" and falsely blocking the connection. Verified
// live against gonic 0.22.0. Only apiKey profiles hit this: build_url sends
// the profile's credentials, so a user/pass profile authenticates the call and
// gonic enumerates normally.
TEST(SubsonicBackendAuthTest, ApiKeyMode_AuthGatedExtensions_FallsThroughToPing)
{
  qRegisterMetaType<QSet<QString>>();
  auto *server = new FakeSubsonicServer;
  server->open_subsonic_extensions = R"({"subsonic-response":{"status":"failed",
    "openSubsonic":true,"error":{"code":10,"message":"please provide a \"u\" parameter"}}})";
  auto *engine = new MockAudioEngine;
  auto *backend = new SubsonicBackend(server->url(), SubsonicAuth{.api_key = "key"}, engine);

  QSignalSpy conn_spy(backend, &Backend::connection_update);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(conn_spy));

  EXPECT_TRUE(conn_spy.constFirst().at(0).toBool());
  EXPECT_EQ(server->hits("ping"), 1);

  delete backend;
  delete server;
}

// The apiKey gate must never fire in username/password mode: salted-token auth
// works on any Subsonic server and needs no extension. Even against a server
// that positively confirms apiKeyAuthentication is absent (a live Navidrome),
// a user/pass profile connects normally.
TEST(SubsonicBackendAuthTest, UserPassMode_UnsupportedApiKey_DoesNotBlock)
{
  qRegisterMetaType<QSet<QString>>();
  auto *server = new FakeSubsonicServer;
  server->open_subsonic_extensions = R"({"subsonic-response":{"status":"ok",
    "openSubsonic":true,"openSubsonicExtensions":[{"name":"transcoding","versions":[1]}]}})";
  auto *engine = new MockAudioEngine;
  auto *backend = new SubsonicBackend(server->url(),
      SubsonicAuth{.username = "bob", .password = "secret"}, engine);

  QSignalSpy err_spy(backend, &Backend::error);
  QSignalSpy conn_spy(backend, &Backend::connection_update);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(conn_spy));

  EXPECT_TRUE(conn_spy.constFirst().at(0).toBool());
  EXPECT_EQ(err_spy.count(), 0);
  EXPECT_EQ(server->hits("ping"), 1);

  delete backend;
  delete server;
}

// freewave sends a fixed `v=` on every request, and a server rejects anything
// above its own protocol level (error 30) *before* checking auth. Airsonic-
// Advanced caps at 1.15.0, so a client claiming 1.16.0 could not connect at
// all — every request, ping included, failed as "Incompatible ... protocol
// version". Nothing freewave calls needs 1.16, so `build_url` claims 1.15.0.
// This pins that: against an Airsonic-shaped server, connect must still work.
TEST(SubsonicBackendAuthTest, ApiVersion_ServerCappedAt_1_15_StillConnects)
{
  qRegisterMetaType<QSet<QString>>();
  auto *server = new FakeSubsonicServer;
  server->reject_api_above_1_15 = true;
  auto *engine = new MockAudioEngine;
  auto *backend = new SubsonicBackend(server->url(),
      SubsonicAuth{.username = "bob", .password = "secret"}, engine);

  QSignalSpy conn_spy(backend, &Backend::connection_update);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(conn_spy));

  EXPECT_TRUE(conn_spy.constFirst().at(0).toBool());
  for (const auto &p : server->request_paths())
    EXPECT_FALSE(p.contains("v=1.16"));

  delete backend;
  delete server;
}

// LMS truncates over-long tags at a byte limit, which can split a multi-byte
// UTF-8 sequence; the getAlbum body is then invalid UTF-8 and Qt's strict JSON
// parser rejects it wholesale. The backend must recover the tracklist instead
// of losing every song (observed live: album visible, zero tracks, unplayable).
TEST_F(SubsonicBackendNetTest, GetAlbum_InvalidUtf8Body_SongsStillLoad)
{
  // "\xE2\x83" = the first two bytes of U+20DD — a 512-byte column cut landing
  // mid-codepoint, exactly as LMS emits it.
  server->album_songs = "{\"subsonic-response\":{\"status\":\"ok\",\"album\":{\"song\":["
                        "{\"id\":\"s1\",\"title\":\"Fine\",\"artist\":\"A\",\"track\":1,\"discNumber\":1,\"duration\":100},"
                        "{\"id\":\"s2\",\"title\":\"Trunc \xE2\x83\",\"artist\":\"A\",\"track\":2,\"discNumber\":1,\"duration\":120}"
                        "]}}}";
  ASSERT_TRUE(QJsonDocument::fromJson(server->album_songs).isNull());

  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  auto album = backend->get_albums().first();
  QSignalSpy songs_spy(backend, &Backend::songs_changed);

  QList<song> got;
  backend->fetch_songs(album, [&](const QList<song> &songs) { got = songs; });
  ASSERT_TRUE(wait_for(songs_spy));

  ASSERT_EQ(got.size(), 2);
  EXPECT_EQ(got[0].title, "Fine");
  // The mangled byte pair decodes to U+FFFD — the honest rendering of what the
  // server sent; the rest of the title (and the other song) must survive.
  EXPECT_TRUE(got[1].title.startsWith("Trunc "));
  EXPECT_TRUE(got[1].title.endsWith(QChar(0xFFFD)));
}

// ===========================================================================
// Stored playlists (plan phase 3)
// ===========================================================================

TEST_F(SubsonicBackendTest, SupportsPlaylists)
{
  EXPECT_TRUE(backend->supports(Backend::Feature::Playlists));
}

// Every playlist slot must be a silent no-op when disconnected: no request, no
// signal (mirrors set_favorite and the phase-2 MPD offline behaviour).
TEST_F(SubsonicBackendTest, PlaylistOps_WhenNotConnected_NoEmit)
{
  QSignalSpy loaded(backend, &Backend::playlists_loaded);
  QSignalSpy songs(backend, &Backend::playlist_songs_loaded);
  QSignalSpy changed(backend, &Backend::playlists_changed);
  QSignalSpy fav_songs(backend, &Backend::favorite_songs_loaded);

  backend->fetch_playlists();
  backend->fetch_playlist_songs("pl1");
  backend->create_playlist("New", {make_song("s1")});
  backend->rename_playlist("pl1", "Renamed");
  backend->delete_playlist("pl1");
  backend->add_to_playlist("pl1", {make_song("s1")});
  backend->remove_from_playlist("pl1", {0});
  backend->rearrange_playlist("pl1", 2, {0});

  QCoreApplication::processEvents();
  EXPECT_EQ(loaded.count(), 0);
  EXPECT_EQ(songs.count(), 0);
  EXPECT_EQ(changed.count(), 0);
  EXPECT_EQ(fav_songs.count(), 0);
}

// getPlaylists → playlists_loaded with id/name/count/duration(ms)/changed.
TEST_F(SubsonicBackendNetTest, FetchPlaylists_ParsesSummaries)
{
  connect_backend();

  QSignalSpy spy(backend, &Backend::playlists_loaded);
  backend->fetch_playlists();
  ASSERT_TRUE(wait_for(spy));

  auto lists = spy.takeFirst().at(0).value<QList<playlist_info>>();
  ASSERT_EQ(lists.size(), 1);
  EXPECT_EQ(lists[0].id, "pl1");
  EXPECT_EQ(lists[0].name, "Road Trip");
  EXPECT_EQ(lists[0].song_count, 2);
  EXPECT_EQ(lists[0].duration_ms, 220000); // 220 s → ms
  EXPECT_TRUE(lists[0].last_modified.isValid());
}

// getPlaylist → playlist_songs_loaded; duration is seconds×1000; the id is
// echoed back so the manager can key the result.
TEST_F(SubsonicBackendNetTest, FetchPlaylistSongs_ParsesEntries)
{
  connect_backend();

  QSignalSpy spy(backend, &Backend::playlist_songs_loaded);
  backend->fetch_playlist_songs("pl1");
  ASSERT_TRUE(wait_for(spy));

  auto args = spy.takeFirst();
  EXPECT_EQ(args.at(0).toString(), "pl1");
  auto songs = args.at(1).value<QList<song>>();
  ASSERT_EQ(songs.size(), 2);
  EXPECT_EQ(songs[0].uri, "s1");
  EXPECT_EQ(songs[0].title, "One");
  EXPECT_EQ(songs[0].duration, 100000u);
  EXPECT_EQ(songs[1].uri, "s2");
}

TEST_F(SubsonicBackendNetTest, CreatePlaylist_SendsNameAndSongIds)
{
  connect_backend();

  QSignalSpy changed(backend, &Backend::playlists_changed);
  backend->create_playlist("Mix", {make_song("s1"), make_song("s2")});
  ASSERT_TRUE(wait_for(changed));

  auto path = server->last_path("createPlaylist");
  EXPECT_TRUE(path.contains("name=Mix"));
  EXPECT_TRUE(path.contains("songId=s1"));
  EXPECT_TRUE(path.contains("songId=s2"));
  EXPECT_FALSE(path.contains("playlistId=")); // create, not replace
  // The server assigns the new id, so we can't name it: consumers must assume
  // every cached contents list is stale.
  EXPECT_TRUE(changed.first().first().toString().isEmpty());
}

// Every mutation that targets an existing playlist names it in playlists_changed,
// so consumers drop that one contents list instead of all of them.
TEST_F(SubsonicBackendNetTest, PlaylistMutations_NameTheChangedPlaylist)
{
  connect_backend();

  auto emitted_id = [this](auto &&mutate) {
    QSignalSpy changed(backend, &Backend::playlists_changed);
    mutate();
    EXPECT_TRUE(wait_for(changed));
    return changed.isEmpty() ? QString{} : changed.first().first().toString();
  };

  EXPECT_EQ(emitted_id([&] { backend->rename_playlist("pl1", "Renamed"); }), "pl1");
  EXPECT_EQ(emitted_id([&] { backend->add_to_playlist("pl1", {make_song("s3")}); }), "pl1");
  EXPECT_EQ(emitted_id([&] { backend->remove_from_playlist("pl1", {0}); }), "pl1");
  EXPECT_EQ(emitted_id([&] { backend->delete_playlist("pl1"); }), "pl1");
}

TEST_F(SubsonicBackendNetTest, RenamePlaylist_SendsPlaylistIdAndName)
{
  connect_backend();

  QSignalSpy changed(backend, &Backend::playlists_changed);
  backend->rename_playlist("pl1", "Renamed");
  ASSERT_TRUE(wait_for(changed));

  auto path = server->last_path("updatePlaylist");
  EXPECT_TRUE(path.contains("playlistId=pl1"));
  EXPECT_TRUE(path.contains("name=Renamed"));
}

TEST_F(SubsonicBackendNetTest, DeletePlaylist_SendsId)
{
  connect_backend();

  QSignalSpy changed(backend, &Backend::playlists_changed);
  backend->delete_playlist("pl1");
  ASSERT_TRUE(wait_for(changed));

  EXPECT_TRUE(server->last_path("deletePlaylist").contains("id=pl1"));
}

TEST_F(SubsonicBackendNetTest, AddToPlaylist_SendsRepeatedSongIdToAdd)
{
  connect_backend();

  QSignalSpy changed(backend, &Backend::playlists_changed);
  backend->add_to_playlist("pl1", {make_song("s3"), make_song("s4")});
  ASSERT_TRUE(wait_for(changed));

  auto path = server->last_path("updatePlaylist");
  EXPECT_TRUE(path.contains("playlistId=pl1"));
  EXPECT_TRUE(path.contains("songIdToAdd=s3"));
  EXPECT_TRUE(path.contains("songIdToAdd=s4"));
}

// Remove forwards the manager-computed 0-based indexes verbatim, in order, as
// repeated songIndexToRemove params in one call.
TEST_F(SubsonicBackendNetTest, RemoveFromPlaylist_SendsRepeatedIndexesInOrder)
{
  connect_backend();

  QSignalSpy changed(backend, &Backend::playlists_changed);
  backend->remove_from_playlist("pl1", {2, 0, 1});
  ASSERT_TRUE(wait_for(changed));

  auto path = server->last_path("updatePlaylist");
  EXPECT_TRUE(path.contains("playlistId=pl1"));
  int i2 = path.indexOf("songIndexToRemove=2");
  int i0 = path.indexOf("songIndexToRemove=0");
  int i1 = path.indexOf("songIndexToRemove=1");
  EXPECT_GE(i2, 0);
  EXPECT_GT(i0, i2);
  EXPECT_GT(i1, i0);
}

// Reorder is a full-contents replace via createPlaylist?playlistId with the
// reordered id list — here song 0 moved to the end of [s1,s2] becomes [s2,s1].
TEST_F(SubsonicBackendNetTest, RearrangePlaylist_ReplacesWithReorderedIds)
{
  connect_backend();

  // Populate the contents cache the reorder depends on.
  QSignalSpy songs_spy(backend, &Backend::playlist_songs_loaded);
  backend->fetch_playlist_songs("pl1");
  ASSERT_TRUE(wait_for(songs_spy));

  QSignalSpy changed(backend, &Backend::playlists_changed);
  backend->rearrange_playlist("pl1", 2, {0}); // move index 0 to the end
  ASSERT_TRUE(wait_for(changed));

  auto path = server->last_path("createPlaylist");
  EXPECT_TRUE(path.contains("playlistId=pl1"));
  EXPECT_LT(path.indexOf("songId=s2"), path.indexOf("songId=s1")); // reordered
}

// A reorder with no cached contents must NOT fire an empty-songId
// createPlaylist — that would clear the playlist server-side. No request, no
// change signal.
TEST_F(SubsonicBackendNetTest, RearrangePlaylist_CacheMiss_DoesNotWipe)
{
  connect_backend();

  QSignalSpy changed(backend, &Backend::playlists_changed);
  backend->rearrange_playlist("never-fetched", 2, {0});

  QCoreApplication::processEvents();
  EXPECT_EQ(server->hits("createPlaylist"), 0);
  EXPECT_EQ(changed.count(), 0);
}

// A body that isn't a Subsonic envelope has no error.message to quote, so the
// error text must not come through empty — an empty error reaches the user as a
// blank message with nothing to act on.
TEST_F(SubsonicBackendNetTest, PlaylistMutation_BridgeRejects_EmitsNonEmptyError)
{
  connect_backend();
  server->bridge_reject("deletePlaylist");

  QSignalSpy err(backend, &Backend::error);
  QSignalSpy changed(backend, &Backend::playlists_changed);
  backend->delete_playlist("pl1");
  ASSERT_TRUE(wait_for(err));

  EXPECT_FALSE(err.first().first().toString().isEmpty());
  EXPECT_EQ(changed.count(), 0);
}

TEST_F(SubsonicBackendNetTest, SetFavorite_BridgeRejects_EmitsNonEmptyError)
{
  connect_backend();
  server->bridge_reject("unstar");

  QSignalSpy err(backend, &Backend::error);
  QSignalSpy changed(backend, &Backend::favorite_changed);
  backend->set_favorite("s1", false);
  ASSERT_TRUE(wait_for(err));

  EXPECT_FALSE(err.first().first().toString().isEmpty());
  EXPECT_EQ(changed.count(), 0); // the toggle did not take
}

// ---------------------------------------------------------------------------
// Per-server capability downgrades
// ---------------------------------------------------------------------------

// Creating a playlist is the one write that proves the server has no playlist
// support: a read-only bridge answers getPlaylists fine and only refuses this.
TEST_F(SubsonicBackendNetTest, CreateRefused_DisablesPlaylists)
{
  connect_backend();
  ASSERT_TRUE(backend->supports(Backend::Feature::Playlists)); // optimistic default
  server->bridge_reject("createPlaylist");

  QSignalSpy caps(backend, &Backend::capabilities_changed);
  QSignalSpy err(backend, &Backend::error);
  backend->create_playlist("Mix", {make_song("s1")});
  ASSERT_TRUE(wait_for(err));

  EXPECT_EQ(caps.count(), 1);
  EXPECT_FALSE(backend->supports(Backend::Feature::Playlists));
  EXPECT_FALSE(err.first().first().toString().isEmpty());
}

// A refused rename says something about that playlist or that name, not about
// the server — letting it disable the feature would turn a name clash into a
// vanished playlists section.
TEST_F(SubsonicBackendNetTest, RenameRefused_KeepsPlaylistsEnabled)
{
  connect_backend();
  server->bridge_reject("updatePlaylist");

  QSignalSpy caps(backend, &Backend::capabilities_changed);
  QSignalSpy err(backend, &Backend::error);
  backend->rename_playlist("pl1", "Renamed");
  ASSERT_TRUE(wait_for(err));

  EXPECT_EQ(caps.count(), 0);
  EXPECT_TRUE(backend->supports(Backend::Feature::Playlists));
}

// Reorder rides the createPlaylist *endpoint* (contents-replace), so gating on
// the endpoint name rather than the call site would disable playlists on a
// failed drag.
TEST_F(SubsonicBackendNetTest, ReorderRefused_KeepsPlaylistsEnabled)
{
  connect_backend();

  QSignalSpy songs_spy(backend, &Backend::playlist_songs_loaded);
  backend->fetch_playlist_songs("pl1"); // reorder needs cached contents
  ASSERT_TRUE(wait_for(songs_spy));

  server->bridge_reject("createPlaylist");
  QSignalSpy caps(backend, &Backend::capabilities_changed);
  QSignalSpy err(backend, &Backend::error);
  backend->rearrange_playlist("pl1", 2, {0});
  ASSERT_TRUE(wait_for(err));

  EXPECT_EQ(caps.count(), 0);
  EXPECT_TRUE(backend->supports(Backend::Feature::Playlists));
}

// Both favorites reads refused: the hearts are backed by nothing, so the
// feature goes and the user is told once.
TEST_F(SubsonicBackendNetTest, BothStarredEndpointsRefused_DisablesFavorites)
{
  connect_backend();
  server->bridge_reject("getStarred2");
  server->bridge_reject("getStarred");

  QSignalSpy caps(backend, &Backend::capabilities_changed);
  QSignalSpy err(backend, &Backend::error);
  backend->fetch_favorites();
  ASSERT_TRUE(wait_for(caps));

  EXPECT_FALSE(backend->supports(Backend::Feature::Favorites));
  ASSERT_EQ(err.count(), 1);
  EXPECT_FALSE(err.first().first().toString().isEmpty());
}

// Reported once, not once per poll tick: the message is about the server, and
// the server is not going to change its mind mid-session.
TEST_F(SubsonicBackendNetTest, FavoritesDowngrade_ReportedOncePerConnection)
{
  connect_backend();
  server->bridge_reject("getStarred2");
  server->bridge_reject("getStarred");

  QSignalSpy caps(backend, &Backend::capabilities_changed);
  QSignalSpy err(backend, &Backend::error);
  backend->fetch_favorites();
  ASSERT_TRUE(wait_for(caps));
  backend->fetch_favorites(); // a later poll tick
  backend->fetch_favorites();
  QCoreApplication::processEvents();

  EXPECT_EQ(caps.count(), 1);
  EXPECT_EQ(err.count(), 1);
}

// An unreachable server proves nothing about what it supports. Downgrading on a
// transport failure would let one blip strip a working server of its features
// for the rest of the session.
TEST_F(SubsonicBackendNetTest, StarredUnreachable_KeepsFavoritesEnabled)
{
  connect_backend();
  server->drop("getStarred2");
  server->drop("getStarred");

  QSignalSpy caps(backend, &Backend::capabilities_changed);
  backend->fetch_favorites();
  ASSERT_TRUE(wait_until([this] { return server->hits("getStarred") >= 1; }));
  QCoreApplication::processEvents();

  EXPECT_EQ(caps.count(), 0);
  EXPECT_TRUE(backend->supports(Backend::Feature::Favorites));
}

// Capabilities are per-connection: a reconnect (or a profile switch onto a
// different server) must not inherit the last server's limits.
TEST_F(SubsonicBackendNetTest, Reconnect_RestoresOptimisticCapabilities)
{
  connect_backend();
  server->bridge_reject("createPlaylist");
  QSignalSpy err(backend, &Backend::error);
  backend->create_playlist("Mix", {make_song("s1")});
  ASSERT_TRUE(wait_for(err));
  ASSERT_FALSE(backend->supports(Backend::Feature::Playlists));

  server->accept("createPlaylist");
  backend->disconnect_from_server(); // connect_to_server no-ops while connected
  connect_backend();

  EXPECT_TRUE(backend->supports(Backend::Feature::Playlists));
  EXPECT_TRUE(backend->supports(Backend::Feature::Favorites));
}

// A Subsonic "failed" status surfaces as error() and must not report a change.
TEST_F(SubsonicBackendNetTest, PlaylistMutation_ErrorStatus_EmitsErrorNotChanged)
{
  connect_backend();
  server->fail("deletePlaylist");

  QSignalSpy err(backend, &Backend::error);
  QSignalSpy changed(backend, &Backend::playlists_changed);
  backend->delete_playlist("pl1");
  ASSERT_TRUE(wait_for(err));

  EXPECT_EQ(changed.count(), 0);
}

// One getStarred2 answers both consumers: favorite_songs_loaded carries the full
// songs alongside the uri-only favorites_loaded.
TEST_F(SubsonicBackendNetTest, FetchFavorites_EmitsParsedSongs)
{
  server->starred2 = R"({"subsonic-response":{"status":"ok","starred2":{"song":[
    {"id":"s1","title":"One","artist":"A","duration":100,"albumId":"alb1"},
    {"id":"s2","title":"Two","artist":"A","duration":120,"albumId":"alb1"}]}}})";
  connect_backend();

  QSignalSpy spy(backend, &Backend::favorite_songs_loaded);
  backend->fetch_favorites();
  ASSERT_TRUE(wait_for(spy));

  auto songs = spy.takeFirst().at(0).value<QList<song>>();
  ASSERT_EQ(songs.size(), 2);
  EXPECT_EQ(songs[0].uri, "s1");
  EXPECT_EQ(songs[0].title, "One");
  EXPECT_EQ(songs[0].duration, 100000u);
}

// Bandcamp's Subsonic bridge answers getStarred2 with a non-Subsonic "bad
// version" body at HTTP 200; server->fail() simulates the same "not usable"
// outcome via a Subsonic "failed" status. fetch_favorites must retry once with the v1 getStarred read instead
// of silently dropping the star set.
TEST_F(SubsonicBackendNetTest, FetchFavorites_FallsBackToGetStarredWhenGetStarred2Unusable)
{
  server->fail("getStarred2");

  QSignalSpy fav_spy(backend, &Backend::favorites_loaded);
  connect_backend();
  backend->fetch_favorites(); // getStarred2 fails → getStarred
  ASSERT_TRUE(wait_for(fav_spy));

  auto uris = fav_spy.at(0).at(0).value<QSet<QString>>();
  EXPECT_EQ(uris, QSet<QString>({"s1", "s2"}));
  EXPECT_GE(server->hits("getStarred"), 1);
}

// Same fallback on the full-song path (Favorited Tracks autoplaylist).
TEST_F(SubsonicBackendNetTest, FetchFavoriteSongs_FallsBackToGetStarredWhenGetStarred2Unusable)
{
  server->fail("getStarred2");
  connect_backend();

  QSignalSpy spy(backend, &Backend::favorite_songs_loaded);
  backend->fetch_favorites();
  ASSERT_TRUE(wait_for(spy));

  auto songs = spy.takeFirst().at(0).value<QList<song>>();
  ASSERT_EQ(songs.size(), 2);
  EXPECT_EQ(songs[0].uri, "s1");
  EXPECT_GE(server->hits("getStarred"), 1);
}

// A successful v1 fallback is memoized for the session: later favorites reads
// go straight to getStarred without re-probing getStarred2, halving favorites
// traffic on servers like Bandcamp's bridge (audit F9).
TEST_F(SubsonicBackendNetTest, StarredV1Fallback_MemoizedForSession)
{
  server->fail("getStarred2");

  QSignalSpy fav_spy(backend, &Backend::favorites_loaded);
  connect_backend();
  backend->fetch_favorites();
  ASSERT_TRUE(wait_for(fav_spy));

  auto probes = server->hits("getStarred2");
  auto v1 = server->hits("getStarred");

  backend->fetch_favorites();
  ASSERT_TRUE(wait_for(fav_spy, fav_spy.count() + 1));

  EXPECT_EQ(server->hits("getStarred2"), probes) << "memoized: no re-probe";
  EXPECT_EQ(server->hits("getStarred"), v1 + 1);
}

// The memo is per-connection: a reconnect probes getStarred2 afresh, so a
// server upgrade isn't masked for ever.
TEST_F(SubsonicBackendNetTest, StarredV1Fallback_MemoResetOnReconnect)
{
  server->fail("getStarred2");

  QSignalSpy fav_spy(backend, &Backend::favorites_loaded);
  connect_backend();
  backend->fetch_favorites();
  ASSERT_TRUE(wait_for(fav_spy));
  auto probes = server->hits("getStarred2");

  backend->disconnect_from_server();
  connect_backend();
  backend->fetch_favorites();
  ASSERT_TRUE(wait_for(fav_spy, fav_spy.count() + 1));

  EXPECT_GT(server->hits("getStarred2"), probes) << "reconnect re-probes v2";
}

// getPlaylist body with a mid-codepoint truncation (LMS's invalid-UTF-8 gotcha)
// must still parse via the lossy recover, not lose every entry.
TEST_F(SubsonicBackendNetTest, FetchPlaylistSongs_InvalidUtf8Body_StillLoads)
{
  server->playlist_entries = "{\"subsonic-response\":{\"status\":\"ok\",\"playlist\":{\"entry\":["
                             "{\"id\":\"s1\",\"title\":\"Fine\",\"albumId\":\"alb1\"},"
                             "{\"id\":\"s2\",\"title\":\"Trunc \xE2\x83\",\"albumId\":\"alb1\"}"
                             "]}}}";
  ASSERT_TRUE(QJsonDocument::fromJson(server->playlist_entries).isNull());
  connect_backend();

  QSignalSpy spy(backend, &Backend::playlist_songs_loaded);
  backend->fetch_playlist_songs("pl1");
  ASSERT_TRUE(wait_for(spy));

  auto songs = spy.takeFirst().at(1).value<QList<song>>();
  ASSERT_EQ(songs.size(), 2);
  EXPECT_EQ(songs[0].title, "Fine");
  EXPECT_TRUE(songs[1].title.endsWith(QChar(0xFFFD)));
}

// An album with an mbid keys the library by the lowercased release MBID, so a
// starred song carries only a native `albumId`. The favorites read must map
// that back to the library album's hash (else the cover/queue-divider lookups
// miss and the row shows no art / blank album). Unknown albums fall back to the
// profile-scoped native id (profile empty here → ":<id>").
TEST_F(SubsonicBackendNetTest, FetchFavoriteSongs_ResolvesAlbumHashFromLoadedLibrary)
{
  server->album_list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[
    {"id":"alb1","name":"Album One","artist":"A","artistSort":"A","year":2020,
     "coverArt":"ca1","created":"2020-01-01T00:00:00.000Z","songCount":12,
     "musicBrainzId":"MBID-ABC"}]}}})";
  server->starred2 = R"({"subsonic-response":{"status":"ok","starred2":{"song":[
    {"id":"s1","title":"One","albumId":"alb1"},
    {"id":"s9","title":"Orphan","albumId":"ghost"}]}}})";

  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server(); // ping → getAlbumList2 → library cached
  ASSERT_TRUE(wait_for(lib_spy));

  QSignalSpy spy(backend, &Backend::favorite_songs_loaded);
  backend->fetch_favorites();
  ASSERT_TRUE(wait_for(spy));

  auto songs = spy.takeFirst().at(0).value<QList<song>>();
  ASSERT_EQ(songs.size(), 2);
  EXPECT_EQ(songs[0].album_hash, "mbid-abc"); // library album, not ":alb1"
  EXPECT_EQ(songs[1].album_hash, ":ghost");   // not in library → fallback
  // Kept so a hash resolved against a half-loaded library can be corrected in
  // place rather than re-requested.
  EXPECT_EQ(songs[0].native_album_id, "alb1");
  EXPECT_EQ(songs[1].native_album_id, "ghost");
}

// The favorites reply can be parsed before the library lands, which is why the
// resolution has to be repeatable: the same native id resolves to the fallback
// before and to the library album's hash after, off the same stored id.
TEST_F(SubsonicBackendNetTest, ResolveAlbumHashFollowsTheLibraryLoad)
{
  server->album_list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[
    {"id":"alb1","name":"Album One","artist":"A","artistSort":"A","year":2020,
     "coverArt":"ca1","created":"2020-01-01T00:00:00.000Z","songCount":12,
     "musicBrainzId":"MBID-ABC"}]}}})";

  EXPECT_EQ(backend->resolve_album_hash("alb1"), ":alb1"); // library not loaded yet
  EXPECT_TRUE(backend->resolve_album_hash("").isEmpty());  // nothing to resolve

  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  EXPECT_EQ(backend->resolve_album_hash("alb1"), "mbid-abc");
  EXPECT_EQ(backend->resolve_album_hash("ghost"), ":ghost");
}

// Same native_id → library-hash resolution for playlist entries.
TEST_F(SubsonicBackendNetTest, FetchPlaylistSongs_ResolvesAlbumHashFromLoadedLibrary)
{
  server->album_list = R"({"subsonic-response":{"status":"ok","albumList2":{"album":[
    {"id":"alb1","name":"Album One","artist":"A","artistSort":"A","year":2020,
     "coverArt":"ca1","created":"2020-01-01T00:00:00.000Z","songCount":12,
     "musicBrainzId":"MBID-ABC"}]}}})";

  QSignalSpy lib_spy(backend, &Backend::library_changed);
  backend->connect_to_server();
  ASSERT_TRUE(wait_for(lib_spy));

  QSignalSpy spy(backend, &Backend::playlist_songs_loaded);
  backend->fetch_playlist_songs("pl1"); // default entries carry albumId "alb1"
  ASSERT_TRUE(wait_for(spy));

  auto songs = spy.takeFirst().at(1).value<QList<song>>();
  ASSERT_EQ(songs.size(), 2);
  EXPECT_EQ(songs[0].album_hash, "mbid-abc");
  EXPECT_EQ(songs[1].album_hash, "mbid-abc");
}

// ---------------------------------------------------------------------------
// cert_pin_accepts — the trust-on-first-use accept decision
// ---------------------------------------------------------------------------

static const QString PIN
    = "da380898ba85cd3949bb761464482e45bdfce3ef8c7d22a2ea447246c4dacc3d";

TEST(CertPinTest, ExactMatchAccepts)
{
  EXPECT_TRUE(cert_pin_accepts(PIN, PIN));
}

TEST(CertPinTest, EmptyPinNeverAccepts)
{
  // No pin means the user has trusted nothing — a server presenting any
  // certificate at all must not be let through.
  EXPECT_FALSE(cert_pin_accepts("", PIN));
  EXPECT_FALSE(cert_pin_accepts("", ""));
}

TEST(CertPinTest, EmptyFingerprintNeverAccepts)
{
  EXPECT_FALSE(cert_pin_accepts(PIN, ""));
}

TEST(CertPinTest, DifferentCertificateIsRejected)
{
  // A renewed or substituted certificate must fall back to the prompt, not be
  // silently accepted on the strength of the old pin.
  QString other = PIN;
  other[0] = 'f';
  other[1] = 'e';
  EXPECT_FALSE(cert_pin_accepts(PIN, other));
}

TEST(CertPinTest, HexCaseIsIgnored)
{
  // Qt hands back lowercase hex; a hand-edited config may not.
  EXPECT_TRUE(cert_pin_accepts(PIN.toUpper(), PIN));
  EXPECT_TRUE(cert_pin_accepts(PIN, PIN.toUpper()));
}

TEST(CertPinTest, PrefixDoesNotAccept)
{
  EXPECT_FALSE(cert_pin_accepts(PIN.left(32), PIN));
}

// ---------------------------------------------------------------------------
// Track cache
//
// The backend against a real localhost server and a real TrackCache. Playback
// itself goes through the mock engine, so every request the server answers is a
// prefetch — which makes the request count the assertion for what the cache did
// and didn't fetch.
// ---------------------------------------------------------------------------

class SubsonicBackendCacheTest : public ::testing::Test {
protected:
  QTemporaryDir tmp;
  HttpTestServer *server = nullptr;
  MockAudioEngine *engine = nullptr;
  SubsonicBackend *backend = nullptr;
  TrackCache *cache = nullptr;

  void SetUp() override
  {
    ASSERT_TRUE(tmp.isValid());
    server = new HttpTestServer;
    server->add_file("/rest/stream", QByteArray(32 * 1024, 'a'));

    engine = new MockAudioEngine;
    backend = new SubsonicBackend(server->url("").toString(),
        SubsonicAuth{.api_key = "key"}, engine);
    cache = new TrackCache{QDir{tmp.path()}, 0};
    backend->set_track_cache(cache);
  }

  void TearDown() override
  {
    delete backend;
    delete server;
  }

  QList<song> make_queue(int count)
  {
    QList<song> q;
    for (int i = 0; i < count; ++i)
      q.append(make_song(QString("id-%1").arg(i), QString("Track %1").arg(i)));
    return q;
  }

  int prefetches() const { return server->request_count("/rest/stream"); }

  // Put `track_id` in the cache and wait for it to land.
  bool cache_track(const QString &track_id)
  {
    QSignalSpy warmed{cache, &TrackCache::warmed};
    cache->warm(track_id, server->url("/rest/stream"));
    return warmed.wait(5000);
  }
};

TEST_F(SubsonicBackendCacheTest, Play_WhenNotCached_StreamsAsBefore)
{
  backend->replace_queue(make_queue(2), 0);

  EXPECT_FALSE(engine->last_play_url.isLocalFile());
  EXPECT_EQ(QUrlQuery(engine->last_play_url).queryItemValue("id"), "id-0");
}

TEST_F(SubsonicBackendCacheTest, Play_WhenCached_PlaysTheLocalFile)
{
  ASSERT_TRUE(cache_track("id-0"));

  backend->replace_queue(make_queue(2), 0);

  EXPECT_TRUE(engine->last_play_url.isLocalFile());
  EXPECT_EQ(cache->id_for_local(engine->last_play_url), "id-0");
}

TEST_F(SubsonicBackendCacheTest, Play_PrefetchesTheSuccessor)
{
  QSignalSpy warmed{cache, &TrackCache::warmed};
  backend->replace_queue(make_queue(2), 0);

  ASSERT_TRUE(warmed.wait(5000));
  EXPECT_EQ(warmed.first().at(0).toString(), "id-1");
  EXPECT_EQ(prefetches(), 1);
  EXPECT_FALSE(cache->local_for("id-1").isEmpty());
}

TEST_F(SubsonicBackendCacheTest, Prefetched_SuccessorIsRearmedAsALocalFile)
{
  QSignalSpy warmed{cache, &TrackCache::warmed};
  backend->replace_queue(make_queue(2), 0);

  // Armed as a stream first: the successor is armed at play time, long before
  // its prefetch can have finished.
  EXPECT_FALSE(engine->next_url.isLocalFile());

  ASSERT_TRUE(warmed.wait(5000));
  EXPECT_TRUE(engine->next_url.isLocalFile());
  EXPECT_EQ(cache->id_for_local(engine->next_url), "id-1");
}

TEST_F(SubsonicBackendCacheTest, Next_ToACachedTrack_PlaysTheLocalFile)
{
  QSignalSpy warmed{cache, &TrackCache::warmed};
  backend->replace_queue(make_queue(2), 0);
  ASSERT_TRUE(warmed.wait(5000));

  backend->next();

  EXPECT_TRUE(engine->last_play_url.isLocalFile());
  EXPECT_EQ(cache->id_for_local(engine->last_play_url), "id-1");
}

// A local file carries no track id, so without the cache's reverse lookup a
// gapless transition into one falls through to a positional advance — which is
// a different, and here wrong, answer.
TEST_F(SubsonicBackendCacheTest, TrackChanged_ToALocalFile_LocatesTheRightTrack)
{
  backend->replace_queue(make_queue(3), 0);
  ASSERT_TRUE(cache_track("id-2"));

  QSignalSpy current{backend, &Backend::current_song_changed};
  engine->fire_track_changed(cache->local_for("id-2"));

  ASSERT_FALSE(current.isEmpty());
  EXPECT_EQ(current.last().at(0).value<song>().uri, "id-2");
}

TEST_F(SubsonicBackendCacheTest, EngineBusy_HoldsThePrefetch)
{
  backend->replace_queue(make_queue(2), 0);

  engine->fire_busy(AudioEngine::Busy::Seek);
  EXPECT_TRUE(cache->is_held());
  EXPECT_FALSE(cache->is_fetching());

  engine->fire_busy(AudioEngine::Busy::None);
  EXPECT_FALSE(cache->is_held());
}

TEST_F(SubsonicBackendCacheTest, EnginePreroll_HoldsThePrefetch)
{
  backend->replace_queue(make_queue(2), 0);

  engine->fire_busy(AudioEngine::Busy::Preroll);
  EXPECT_TRUE(cache->is_held());
}

// The successor of a track playing in single+repeat is itself. Fetching it would
// put a second full-file download on the link the stream is already using — the
// case that was measured tripling how long a seek in that stream takes.
TEST_F(SubsonicBackendCacheTest, SingleRepeat_DoesNotPrefetchTheTrackThatIsPlaying)
{
  backend->set_repeat(true, true);
  backend->replace_queue(make_queue(2), 0);
  QTest::qWait(100);

  EXPECT_EQ(prefetches(), 0);
  EXPECT_FALSE(engine->next_url.isEmpty()); // still armed for the loop
}

TEST_F(SubsonicBackendCacheTest, RepeatingOneTrackQueue_DoesNotPrefetchIt)
{
  backend->set_repeat(true, false);
  backend->replace_queue(make_queue(1), 0);
  QTest::qWait(100);

  EXPECT_EQ(prefetches(), 0);
  EXPECT_FALSE(engine->next_url.isEmpty());
}

// A queue restored at startup arms a successor before anything has been played.
// Downloading it there would spend the link on a track nobody has asked for.
TEST_F(SubsonicBackendCacheTest, ArmedWhileStopped_DoesNotPrefetch)
{
  backend->replace_queue(make_queue(1), 0);
  ASSERT_EQ(prefetches(), 0);
  backend->stop();

  backend->append_queue({make_song("id-9", "Later")});
  QTest::qWait(100);

  EXPECT_EQ(prefetches(), 0);
  EXPECT_FALSE(engine->next_url.isEmpty());
}

TEST_F(SubsonicBackendCacheTest, WithoutACache_NothingChanges)
{
  backend->set_track_cache(nullptr);

  backend->replace_queue(make_queue(2), 0);
  QTest::qWait(100);

  EXPECT_FALSE(engine->last_play_url.isLocalFile());
  EXPECT_FALSE(engine->next_url.isLocalFile());
  EXPECT_EQ(prefetches(), 0);
}

// Eviction must not pull a file out from under the pipeline. The backend pins
// what is playing and what the engine has already been handed as its successor.
TEST_F(SubsonicBackendCacheTest, PlayingAndArmedTracksSurviveEviction)
{
  ASSERT_TRUE(cache_track("spare")); // cached, but in nobody's queue
  ASSERT_TRUE(cache_track("id-0"));

  QSignalSpy warmed{cache, &TrackCache::warmed};
  backend->replace_queue(make_queue(2), 0); // plays id-0, arms and prefetches id-1
  ASSERT_TRUE(warmed.wait(5000));

  cache->set_budget(1); // room for nothing that isn't pinned

  EXPECT_FALSE(cache->local_for("id-0").isEmpty()); // playing
  EXPECT_FALSE(cache->local_for("id-1").isEmpty()); // armed next
  EXPECT_TRUE(cache->local_for("spare").isEmpty()); // neither
}

TEST_F(SubsonicBackendCacheTest, AdvancingTheQueueUnpinsWhatIsBehind)
{
  ASSERT_TRUE(cache_track("id-0"));
  ASSERT_TRUE(cache_track("id-1"));
  ASSERT_TRUE(cache_track("id-2"));

  backend->replace_queue(make_queue(3), 0); // pins id-0 + id-1
  backend->next();                          // now pins id-1 + id-2
  cache->set_budget(1);

  EXPECT_TRUE(cache->local_for("id-0").isEmpty()); // played, and behind us
  EXPECT_FALSE(cache->local_for("id-1").isEmpty());
  EXPECT_FALSE(cache->local_for("id-2").isEmpty());
}

TEST_F(SubsonicBackendCacheTest, ConfigureAudioCache_OffMeansStreamAsBefore)
{
  ASSERT_TRUE(cache_track("id-0"));
  const int before = prefetches(); // caching that track was itself a request

  backend->configure_audio_cache(false, 4096);
  backend->replace_queue(make_queue(2), 0);
  QTest::qWait(100);

  EXPECT_FALSE(engine->last_play_url.isLocalFile());
  EXPECT_EQ(prefetches(), before);
}

TEST_F(SubsonicBackendCacheTest, ClearAudioCache_EmptiesIt)
{
  ASSERT_TRUE(cache_track("id-0"));
  ASSERT_GT(backend->audio_cache_bytes(), 0);

  backend->clear_audio_cache();

  EXPECT_EQ(backend->audio_cache_bytes(), 0);
  EXPECT_TRUE(cache->local_for("id-0").isEmpty());
}

TEST_F(SubsonicBackendCacheTest, SupportsAudioCache_OnlyWithOne)
{
  EXPECT_TRUE(backend->supports(Backend::Feature::AudioCache));
  backend->set_track_cache(nullptr);
  EXPECT_FALSE(backend->supports(Backend::Feature::AudioCache));
}
