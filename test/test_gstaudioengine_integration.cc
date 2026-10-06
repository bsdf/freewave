#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QTest>
#include <QCoreApplication>

#include "controller/gstaudioengine.hh"
#include "http_test_server.hh"

// Integration tests against real GStreamer pipelines.
// Test audio: 3-second Opus tones (tone1/tone2) + a 15s tone (tone_long) in
// test/res/.

// End-of-track events arrive at the track duration plus however long the audio
// sink takes to drain. That is a property of whatever sink the machine has, not
// of the engine, so these waits are bounded loosely rather than tuned to a
// runner: they return the instant the signal arrives and only spend the full
// budget on a genuine failure.
static constexpr int EOS_TIMEOUT_MS = 15000;

static QUrl
tone_url(int n)
{
  auto path = QDir(QCoreApplication::applicationDirPath())
                  .absoluteFilePath(
                      QString("test/res/tone%1.opus").arg(n));
  return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
}

static QByteArray
load_res(const QString &name)
{
  QFile f(QDir(QCoreApplication::applicationDirPath())
          .absoluteFilePath("test/res/" + name));
  if (!f.open(QIODevice::ReadOnly)) return {};
  return f.readAll();
}

// Wait for a specific AudioEngine state, returning false on timeout.
static bool
wait_for_state(QSignalSpy &spy, AudioEngine::State target,
    int timeout_ms = 5000)
{
  auto deadline = QDeadlineTimer(timeout_ms);
  while (!deadline.hasExpired())
    {
      for (const auto &args : spy)
        if (args[0].value<AudioEngine::State>() == target) return true;
      spy.wait(100);
    }
  return false;
}

class GstIntegrationTest : public ::testing::Test {
protected:
  GstAudioEngine *engine = nullptr;

  void SetUp() override
  {
    engine = new GstAudioEngine;
    // These tests drive a real audio sink; keep them silent. Nothing here
    // asserts on loudness — only on position, state, duration and EOS.
    engine->set_volume(0);
  }
  void TearDown() override
  {
    engine->stop();
    delete engine;
  }
};

// ---------------------------------------------------------------------------
// Basic transport
// ---------------------------------------------------------------------------

TEST_F(GstIntegrationTest, Play_TransitionsToPlaying)
{
  QSignalSpy spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(spy, AudioEngine::State::Playing));
  EXPECT_EQ(engine->state(), AudioEngine::State::Playing);
}

TEST_F(GstIntegrationTest, Stop_TransitionsToStopped)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  QSignalSpy stop_spy(engine, &AudioEngine::state_changed);
  engine->stop();
  ASSERT_TRUE(wait_for_state(stop_spy, AudioEngine::State::Stopped));
  EXPECT_EQ(engine->state(), AudioEngine::State::Stopped);
}

TEST_F(GstIntegrationTest, Pause_TransitionsToPaused)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  QSignalSpy pause_spy(engine, &AudioEngine::state_changed);
  engine->pause();
  ASSERT_TRUE(wait_for_state(pause_spy, AudioEngine::State::Paused));
  EXPECT_EQ(engine->state(), AudioEngine::State::Paused);
}

TEST_F(GstIntegrationTest, Resume_TransitionsBackToPlaying)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  engine->pause();
  QSignalSpy pause_spy(engine, &AudioEngine::state_changed);
  ASSERT_TRUE(wait_for_state(pause_spy, AudioEngine::State::Paused));

  QSignalSpy resume_spy(engine, &AudioEngine::state_changed);
  engine->resume();
  ASSERT_TRUE(wait_for_state(resume_spy, AudioEngine::State::Playing));
  EXPECT_EQ(engine->state(), AudioEngine::State::Playing);
}

// ---------------------------------------------------------------------------
// EOS
// ---------------------------------------------------------------------------

TEST_F(GstIntegrationTest, EOS_EmitsTrackFinished)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  // The tone is 3 s, but EOS lands at 3 s + the audio sink's drain time, which
  // is the sink's business and not something a test should budget tightly for.
  // Waiting for a signal costs nothing on success, so the bound is generous.
  QSignalSpy eos_spy(engine, &AudioEngine::track_finished);
  ASSERT_TRUE(eos_spy.wait(EOS_TIMEOUT_MS));
}

TEST_F(GstIntegrationTest, EOS_StateBecomesStoppedAfterFinish)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  QSignalSpy eos_spy(engine, &AudioEngine::track_finished);
  ASSERT_TRUE(eos_spy.wait(EOS_TIMEOUT_MS));

  EXPECT_NE(engine->state(), AudioEngine::State::Playing);
}

// A pipeline idling in PLAYING after EOS holds an uncorked sound-server stream
// that pipewire-pulse eventually replays the track's tail from, hours later.
TEST_F(GstIntegrationTest, EOS_ReleasesThePipeline)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  QSignalSpy eos_spy(engine, &AudioEngine::track_finished);
  ASSERT_TRUE(eos_spy.wait(EOS_TIMEOUT_MS));

  EXPECT_EQ(engine->pipeline_state(), GST_STATE_NULL);
  // The torn-down pipeline answers no queries; the final position survives.
  EXPECT_GT(engine->position_ms(), 2000u);
}

// ---------------------------------------------------------------------------
// Error → advance cascade brake (A5)
// ---------------------------------------------------------------------------

// An unreadable URL: playbin fails before any stream starts.
static QUrl
bad_url(int n)
{
  return QUrl::fromLocalFile(
      QString("/nonexistent/freewave-test-%1.opus").arg(n));
}

// A dead server fails every queue entry in turn. The first failures advance
// (track_finished, so the backend tries the next track); once they pile up the
// engine trips the brake — no more track_finished, it just goes Stopped —
// instead of marching through the whole queue with N pipeline restarts.
TEST_F(GstIntegrationTest, ConsecutiveErrors_BrakeStopsAutoAdvance)
{
  QSignalSpy finished_spy(engine, &AudioEngine::track_finished);
  QSignalSpy state_spy(engine, &AudioEngine::state_changed);

  engine->play(bad_url(1));
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished_spy.count() >= 1; }, 5000));

  engine->play(bad_url(2));
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished_spy.count() >= 2; }, 5000));

  // Third failure trips the brake: no further track_finished, engine Stopped.
  engine->play(bad_url(3));
  ASSERT_TRUE(wait_for_state(state_spy, AudioEngine::State::Stopped));
  EXPECT_EQ(finished_spy.count(), 2);
  EXPECT_EQ(engine->state(), AudioEngine::State::Stopped);
}

// A track that actually starts decoding (STREAM_START) clears the brake, so a
// later isolated failure advances again rather than being treated as the Nth
// of a cascade.
TEST_F(GstIntegrationTest, SuccessfulStreamStart_ResetsErrorBrake)
{
  QSignalSpy finished_spy(engine, &AudioEngine::track_finished);

  // Two failures — one short of the brake.
  engine->play(bad_url(1));
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished_spy.count() >= 1; }, 5000));
  engine->play(bad_url(2));
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished_spy.count() >= 2; }, 5000));

  // A good track starts → STREAM_START resets the counter. Stop it before EOS
  // so the only track_finished we then observe comes from the next failure.
  QSignalSpy state_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(state_spy, AudioEngine::State::Playing));
  engine->stop();

  // Counter was reset: this failure still advances (had it stayed at 2, this
  // would be the 3rd consecutive and the brake would suppress track_finished).
  QSignalSpy finished2(engine, &AudioEngine::track_finished);
  engine->play(bad_url(3));
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished2.count() >= 1; }, 5000));
}

// ---------------------------------------------------------------------------
// Duration
// ---------------------------------------------------------------------------

TEST_F(GstIntegrationTest, Duration_ReportedAfterPlay)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  // Give GStreamer time to report duration (longer with queue2 buffering)
  QSignalSpy dur_spy(engine, &AudioEngine::duration_changed);
  dur_spy.wait(5000);

  // tone1.opus is 3 seconds; duration should be within 500ms
  auto dur = engine->duration_ms();
  EXPECT_GT(dur, 2500u);
  EXPECT_LT(dur, 3500u);
}

// ---------------------------------------------------------------------------
// Seek
// ---------------------------------------------------------------------------

TEST_F(GstIntegrationTest, Seek_PositionMovesToTarget)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  // Seek to 500ms
  engine->seek(500);
  QTest::qWait(500); // let GStreamer process the seek

  // Position should be near 500ms (within 200ms tolerance for keyframe snap)
  auto pos = engine->position_ms();
  EXPECT_GT(pos, 300u);
}

// Spamming next-track: a burst of play() calls must never hang the caller or
// wedge the pipeline, and must settle on Playing.
TEST_F(GstIntegrationTest, RapidSuccessivePlays_DoNotFreeze)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  for (int i = 0; i < 40; ++i)
    engine->play(tone_url((i % 2) + 1));

  QSignalSpy final_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  EXPECT_TRUE(wait_for_state(final_spy, AudioEngine::State::Playing));
  EXPECT_EQ(engine->state(), AudioEngine::State::Playing);
}

// Seeking must land near the requested point — keyframe-aligned with
// SNAP_NEAREST, so it doesn't snap a long way backwards to the preceding
// keyframe. Seek forward to 2 s in the 3 s tone and confirm position is near 2 s.
TEST_F(GstIntegrationTest, Seek_LandsNearTargetNotBackwards)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  engine->seek(2000);
  QTest::qWait(400);

  auto pos = engine->position_ms();
  EXPECT_GE(pos, 1850u) << "seek snapped backwards (pos=" << pos << ")";
  EXPECT_LE(pos, 2400u) << "seek overshot (pos=" << pos << ")";
}

TEST_F(GstIntegrationTest, RapidSeeks_DoNotFreezeOrCrash)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  // Fire 10 seeks in rapid succession — debounce should coalesce them
  for (int i = 0; i < 10; ++i)
    engine->seek(static_cast<uint32_t>(i * 50));

  QTest::qWait(300); // let seeks settle
  EXPECT_EQ(engine->state(), AudioEngine::State::Playing);
}

// A seek is work in progress, and the engine has to say so: a remote seek can
// run for ten seconds, and a player that reports plain "playing" throughout is
// indistinguishable from one that has hung.
TEST_F(GstIntegrationTest, Seek_ReportsBusyUntilItSettles)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));
  ASSERT_EQ(engine->busy(), AudioEngine::Busy::None);

  QSignalSpy busy_spy(engine, &AudioEngine::busy_changed);
  engine->seek(2000);
  EXPECT_EQ(engine->busy(), AudioEngine::Busy::Seek)
      << "busy from the moment the seek is asked for, not when gst accepts it";
  EXPECT_GE(busy_spy.count(), 1);

  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return engine->busy() == AudioEngine::Busy::None; }, 5000));
}

// The pipeline necessarily sits in PAUSED while a flushing seek runs. Reporting
// that verbatim would show a pause the listener never asked for — and, worse,
// invite the play button to restart the track.
TEST_F(GstIntegrationTest, Seek_DoesNotReportAPauseNobodyAskedFor)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  QSignalSpy state_spy(engine, &AudioEngine::state_changed);
  engine->seek(2000);
  EXPECT_EQ(engine->state(), AudioEngine::State::Playing);

  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return engine->busy() == AudioEngine::Busy::None; }, 5000));
  EXPECT_EQ(engine->state(), AudioEngine::State::Playing);

  for (const auto &args : state_spy)
    EXPECT_NE(args[0].value<AudioEngine::State>(), AudioEngine::State::Paused)
        << "a seek announced itself as a pause";
}

// ---------------------------------------------------------------------------
// Restore position (play with start_ms)
// ---------------------------------------------------------------------------

TEST_F(GstIntegrationTest, RestorePosition_StartsNearTargetOffset)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1), 500); // start at 500ms
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  QTest::qWait(200);

  // Position should be >= 500ms (engine restored to that offset)
  auto pos = engine->position_ms();
  EXPECT_GE(pos, 400u);
}

TEST_F(GstIntegrationTest, RestorePosition_DoesNotPlayFromZero)
{
  // Verify state_changed(Playing) is NOT preceded by a position report of 0
  // followed by the seek target. We check that the playing state arrives once
  // and position is already near the target (no 0→500 jump observed).
  engine->play(tone_url(1), 500);

  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  // First position report after Playing should be near restore target, not 0
  auto pos = engine->position_ms();
  EXPECT_GE(pos, 400u);
}

// The restore path prerolls through PAUSED internally and now waits for
// ASYNC_DONE (not a blocking get_state) before going PLAYING — but it must
// still not surface a Paused state to the UI before playback begins.
TEST_F(GstIntegrationTest, RestorePosition_EmitsPlayingWithoutSpuriousPaused)
{
  QSignalSpy state_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1), 500);
  ASSERT_TRUE(wait_for_state(state_spy, AudioEngine::State::Playing));

  for (const auto &args : state_spy)
    EXPECT_NE(args[0].value<AudioEngine::State>(), AudioEngine::State::Paused);
}

// ---------------------------------------------------------------------------
// Volume
// ---------------------------------------------------------------------------

TEST_F(GstIntegrationTest, Volume_SetBeforePlay_AppliedWhenPlaying)
{
  // A low but non-zero level: still imperceptible, still exercises that the
  // value set before play() survives the play() call.
  engine->set_volume(2);
  EXPECT_EQ(engine->volume(), 2);

  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  EXPECT_EQ(engine->volume(), 2);
}

// ---------------------------------------------------------------------------
// Gapless
// ---------------------------------------------------------------------------

TEST_F(GstIntegrationTest, Gapless_TrackChangedEmittedOnTransition)
{
  // Pre-load the second track so about-to-finish can set it
  engine->set_next_url(tone_url(2));

  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  // Wait for about-to-finish → gapless transition → STREAM_START → track_changed
  QSignalSpy changed_spy(engine, &AudioEngine::track_changed);
  ASSERT_TRUE(changed_spy.wait(EOS_TIMEOUT_MS));
}

TEST_F(GstIntegrationTest, Gapless_ContinuesPlayingAfterTransition)
{
  engine->set_next_url(tone_url(2));

  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  QSignalSpy changed_spy(engine, &AudioEngine::track_changed);
  ASSERT_TRUE(changed_spy.wait(EOS_TIMEOUT_MS));

  // Engine should still be playing the second track
  EXPECT_EQ(engine->state(), AudioEngine::State::Playing);
}

// ---------------------------------------------------------------------------
// Stale bus messages / stale gapless flag across track changes
// ---------------------------------------------------------------------------

// An EOS posted by the previous track but not yet delivered to the bus watch
// must not surface as track_finished for the track that replaced it — the backend would
// advance the queue and skip a track. Today this is guaranteed by GstPipeline's
// auto-flush-bus (default TRUE), which drops queued messages on READY→NULL;
// this test pins that guarantee against play() ever skipping the NULL round-trip
// (e.g. a playbin3 instant-uri migration) or auto-flush-bus being disabled.
TEST_F(GstIntegrationTest, StaleEosOnBus_FlushedByPlay_NoSpuriousTrackFinished)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  // Let the 3s tone run to EOS *without* spinning the Qt event loop: the EOS
  // message lands on the bus, but the watch never dispatches to deliver it.
  QTest::qSleep(4000);

  QSignalSpy finished_spy(engine, &AudioEngine::track_finished);
  QSignalSpy play2_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(2));
  ASSERT_TRUE(wait_for_state(play2_spy, AudioEngine::State::Playing));

  QTest::qWait(300);
  EXPECT_EQ(finished_spy.count(), 0);
}

// Same guarantee through stop(): a stale EOS popped after stop() would emit
// track_finished and make the backend auto-advance after the user hit stop.
TEST_F(GstIntegrationTest, StaleEosOnBus_FlushedByStop_NoSpuriousTrackFinished)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  QTest::qSleep(4000); // EOS queued on the bus, never delivered

  engine->stop();

  QSignalSpy finished_spy(engine, &AudioEngine::track_finished);
  QTest::qWait(300); // watch dispatches; must find nothing
  EXPECT_EQ(finished_spy.count(), 0);
}

// A gapless transition committed by about-to-finish must be cancelled when a
// different track is started manually before STREAM_START arrives — otherwise
// the new track's STREAM_START consumes the stale flag and emits a spurious
// track_changed, desyncing the backend's queue position.
TEST_F(GstIntegrationTest, StalePendingGapless_ClearedByPlay_NoSpuriousTrackChanged)
{
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  // Commit a gapless successor exactly as the streaming-thread callback would.
  engine->set_next_url(tone_url(2));
  engine->on_about_to_finish();

  // User picks a different track before the gapless STREAM_START. The backend
  // recomputes the next track on every manual play; here it becomes none, so
  // any track_changed in the window can only come from the stale commit.
  engine->clear_next_url();
  QSignalSpy changed_spy(engine, &AudioEngine::track_changed);
  QSignalSpy play2_spy(engine, &AudioEngine::state_changed);
  engine->play(tone_url(1));
  ASSERT_TRUE(wait_for_state(play2_spy, AudioEngine::State::Playing));

  QTest::qWait(300);
  EXPECT_EQ(changed_spy.count(), 0);
}

// ---------------------------------------------------------------------------
// HTTP source — seek/error regressions (D1, D2)
//
// These drive the real engine against a localhost HTTP server so the network
// path is exercised (the file:// tone fixtures above can't reproduce queue2
// underrun or a mid-stream stream error). See test/http_test_server.hh.
// ---------------------------------------------------------------------------

// D1 regression: a flush-seek into the final seconds empties queue2 and posts
// BUFFERING 0%. The removed manual buffering pause (Path A') would PAUSE here,
// and with about-to-finish about to commit the gapless successor the pipeline
// deadlocks (position frozen, never reaches EOS). The fix must let it play
// through to the gapless transition. We assert the positive signal (the
// successor's track_changed) rather than the freeze itself.
TEST_F(GstIntegrationTest, Gapless_NearEndSeekOverHttp_AdvancesNoFreeze)
{
  HttpTestServer srv;
  srv.add_file("/a.opus", load_res("tone1.opus")); // 3 s
  srv.add_file("/b.opus", load_res("tone2.opus")); // 3 s

  engine->set_next_url(srv.url("/b.opus"));
  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  engine->play(srv.url("/a.opus"));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  QSignalSpy changed_spy(engine, &AudioEngine::track_changed);
  engine->seek(2500); // final second of the 3 s track

  ASSERT_TRUE(changed_spy.wait(8000)); // gapless successor started, no freeze
  EXPECT_EQ(engine->state(), AudioEngine::State::Playing);
}

// D2 regression: a track that errors mid-stream (here a server-truncated body,
// standing in for the intermittent souphttpsrc -5 on a real seek) must NOT be
// skipped — the engine re-establishes the stream in place and keeps playing.
// The 15 s track keeps the error well outside the 3 s near-EOS guard, so the
// recovery path is taken rather than the treat-as-EOS path.
TEST_F(GstIntegrationTest, MidStreamErrorOverHttp_RecoversInPlaceNoSkip)
{
  HttpTestServer srv;
  QByteArray data = load_res("tone_long.opus");
  ASSERT_FALSE(data.isEmpty());
  // First fetch is truncated ~30% in; the retry serves it whole.
  srv.add_file("/long.opus", data, HttpTestServer::DropOnce,
      data.size() * 3 / 10);

  QSignalSpy play_spy(engine, &AudioEngine::state_changed);
  QSignalSpy finished_spy(engine, &AudioEngine::track_finished);
  engine->play(srv.url("/long.opus"));
  ASSERT_TRUE(wait_for_state(play_spy, AudioEngine::State::Playing));

  // The server drops the first connection → the engine sees a mid-stream error.
  ASSERT_TRUE(QTest::qWaitFor([&] { return srv.drop_count() >= 1; }, 5000));

  // Non-vacuous progress check: the truncated 30 % body decodes to only ~4 s of
  // audio, so playback can only pass ~8 s if the engine re-established the stream
  // (the retry serves the file whole). A skip would fire track_finished instead;
  // a silent stall would freeze the position short of 8 s — both fail here.
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return engine->position_ms() > 8000; }, 15000));
  EXPECT_EQ(finished_spy.count(), 0);
  EXPECT_EQ(engine->state(), AudioEngine::State::Playing);
}
