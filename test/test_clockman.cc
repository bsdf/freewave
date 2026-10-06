#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QTest>

#include "controller/clockman.hh"

namespace {

auto
make_state(PlayState ps, qint64 elapsed_ms, qint64 total_ms) -> PlaybackState
{
  PlaybackState s;
  s.state = ps;
  s.elapsed_ms = static_cast<uint32_t>(elapsed_ms);
  s.total_ms = static_cast<uint32_t>(total_ms);
  return s;
}

} // namespace

TEST(ClockMan, SyncEmitsImmediateTickWithBackendValues)
{
  ClockMan clock;
  QSignalSpy spy(&clock, &ClockMan::tick);

  clock.sync(make_state(PlayState::Playing, 1000, 10000));

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.constFirst().at(0).toLongLong(), 1000);
  EXPECT_EQ(spy.constFirst().at(1).toLongLong(), 10000);
}

TEST(ClockMan, PausedStateDoesNotAdvance)
{
  ClockMan clock;
  QSignalSpy spy(&clock, &ClockMan::tick);

  clock.sync(make_state(PlayState::Paused, 4000, 10000));
  ASSERT_EQ(spy.count(), 1);

  QTest::qWait(150); // several tick intervals' worth
  EXPECT_EQ(spy.count(), 1) << "paused clock must not keep ticking";
}

TEST(ClockMan, StoppedStateDoesNotAdvance)
{
  ClockMan clock;
  QSignalSpy spy(&clock, &ClockMan::tick);

  clock.sync(make_state(PlayState::Stopped, 0, 0));
  ASSERT_EQ(spy.count(), 1);

  QTest::qWait(150);
  EXPECT_EQ(spy.count(), 1) << "stopped clock must not keep ticking";
}

TEST(ClockMan, PlayingStateAdvancesOverTime)
{
  ClockMan clock;
  QSignalSpy spy(&clock, &ClockMan::tick);

  clock.sync(make_state(PlayState::Playing, 0, 60000));
  ASSERT_TRUE(QTest::qWaitFor([&] { return spy.count() >= 3; }, 2000));

  const qint64 last_elapsed = spy.constLast().at(0).toLongLong();
  EXPECT_GT(last_elapsed, 0);
  EXPECT_LE(last_elapsed, 60000);
}

TEST(ClockMan, PlayingClampsAtTotalDuration)
{
  ClockMan clock;
  QSignalSpy spy(&clock, &ClockMan::tick);

  // Anchored just before the end: unclamped extrapolation would sail past
  // total_ms within a couple of ticks.
  clock.sync(make_state(PlayState::Playing, 9950, 10000));
  QTest::qWait(200);

  ASSERT_GE(spy.count(), 1);
  for (int i = 0; i < spy.count(); ++i)
    EXPECT_LE(spy.at(i).at(0).toLongLong(), 10000);
  EXPECT_EQ(spy.constLast().at(0).toLongLong(), 10000);
}

TEST(ClockMan, SeekToReanchorsImmediatelyAheadOfBackendConfirm)
{
  ClockMan clock;
  clock.sync(make_state(PlayState::Playing, 1000, 60000));

  QSignalSpy spy(&clock, &ClockMan::tick);
  clock.seek_to(30000);

  ASSERT_GE(spy.count(), 1);
  EXPECT_EQ(spy.constFirst().at(0).toLongLong(), 30000);
  EXPECT_EQ(spy.constFirst().at(1).toLongLong(), 60000);
}

TEST(ClockMan, SeekToClampsToKnownDuration)
{
  ClockMan clock;
  clock.sync(make_state(PlayState::Playing, 1000, 10000));

  QSignalSpy spy(&clock, &ClockMan::tick);
  clock.seek_to(999999);

  ASSERT_GE(spy.count(), 1);
  EXPECT_EQ(spy.constFirst().at(0).toLongLong(), 10000);
}

TEST(ClockMan, SeekToNegativeClampsToZero)
{
  ClockMan clock;
  clock.sync(make_state(PlayState::Playing, 1000, 10000));

  QSignalSpy spy(&clock, &ClockMan::tick);
  clock.seek_to(-500);

  ASSERT_GE(spy.count(), 1);
  EXPECT_EQ(spy.constFirst().at(0).toLongLong(), 0);
}

TEST(ClockMan, ResumingAfterPauseContinuesFromBackendAnchorNotStalePosition)
{
  ClockMan clock;
  clock.sync(make_state(PlayState::Playing, 1000, 60000));
  QTest::qWait(80); // let it drift forward while playing

  QSignalSpy spy(&clock, &ClockMan::tick);
  clock.sync(make_state(PlayState::Paused, 1080, 60000));
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.constFirst().at(0).toLongLong(), 1080);

  QTest::qWait(150);
  EXPECT_EQ(spy.count(), 1) << "must stay frozen at the paused anchor";

  clock.sync(make_state(PlayState::Playing, 1080, 60000));
  ASSERT_TRUE(QTest::qWaitFor([&] { return spy.count() >= 3; }, 2000));
  EXPECT_GE(spy.at(1).at(0).toLongLong(), 1080);
}
