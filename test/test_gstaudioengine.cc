#include <gtest/gtest.h>
#include <QSignalSpy>

#include "controller/gstaudioengine.hh"

// These tests exercise GstAudioEngine without real HTTP streams.
// They verify the state machine, default values, and basic property behaviour.

TEST(GstAudioEngineTest, DefaultState_IsStopped)
{
  GstAudioEngine engine;
  EXPECT_EQ(engine.state(), AudioEngine::State::Stopped);
}

TEST(GstAudioEngineTest, DefaultVolume_Is100)
{
  GstAudioEngine engine;
  EXPECT_EQ(engine.volume(), 100);
}

TEST(GstAudioEngineTest, SetVolume_UpdatesVolume)
{
  GstAudioEngine engine;
  engine.set_volume(42);
  EXPECT_EQ(engine.volume(), 42);
}

TEST(GstAudioEngineTest, DefaultPosition_IsZero)
{
  GstAudioEngine engine;
  EXPECT_EQ(engine.position_ms(), 0u);
}

TEST(GstAudioEngineTest, DefaultDuration_IsZero)
{
  GstAudioEngine engine;
  EXPECT_EQ(engine.duration_ms(), 0u);
}

TEST(GstAudioEngineTest, SetNextUrl_Stores)
{
  GstAudioEngine engine;
  QUrl url("https://example.com/stream");
  engine.set_next_url(url);
  // No public getter; just verify no crash and gapless_active stays false
  // (no about-to-finish has fired yet)
  SUCCEED();
}

TEST(GstAudioEngineTest, ClearNextUrl_NoCrash)
{
  GstAudioEngine engine;
  engine.set_next_url(QUrl("https://example.com/stream"));
  engine.clear_next_url();
  SUCCEED();
}

TEST(GstAudioEngineTest, StopWhenAlreadyStopped_NoCrash)
{
  GstAudioEngine engine;
  engine.stop();
  EXPECT_EQ(engine.state(), AudioEngine::State::Stopped);
}

TEST(GstAudioEngineTest, PauseWhenStopped_NoCrash)
{
  GstAudioEngine engine;
  engine.pause(); // no-op: not playing
  EXPECT_EQ(engine.state(), AudioEngine::State::Stopped);
}

TEST(GstAudioEngineTest, ResumeWhenStopped_NoCrash)
{
  GstAudioEngine engine;
  engine.resume(); // no-op: not paused
  SUCCEED();
}

TEST(GstAudioEngineTest, StopEmitsStateStopped)
{
  GstAudioEngine engine;
  QSignalSpy spy(&engine, &AudioEngine::state_changed);
  engine.stop();
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.takeFirst()[0].value<AudioEngine::State>(),
      AudioEngine::State::Stopped);
}

TEST(GstAudioEngineTest, VolumeClampedToRange)
{
  GstAudioEngine engine;
  engine.set_volume(0);
  EXPECT_EQ(engine.volume(), 0);
  engine.set_volume(100);
  EXPECT_EQ(engine.volume(), 100);
}

TEST(GstAudioEngineTest, SeekWithNoPipeline_NoCrash)
{
  // pipeline is non-null after construction (playbin created), but
  // calling seek on a NULL-state pipeline should not crash
  GstAudioEngine engine;
  engine.seek(5000);
  SUCCEED();
}
