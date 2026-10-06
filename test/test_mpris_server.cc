#include <gtest/gtest.h>

#include "mprislib/qmprisserver.hh"

using mpris::LoopStatus;
using mpris::PlaybackStatus;
using mpris::QMprisServer;

namespace {
auto
make_server() -> QMprisServer *
{
  QMprisServer::Config cfg;
  cfg.serviceSuffix = "freewavetest";
  cfg.identity = "Freewave Test";
  return new QMprisServer(cfg);
}
} // namespace

TEST(MprisServer, PlaybackStatusStrings)
{
  auto *s = make_server();
  EXPECT_EQ(s->playbackStatusString(), "Stopped"); // default

  s->setPlaybackStatus(PlaybackStatus::Playing);
  EXPECT_EQ(s->playbackStatusString(), "Playing");
  s->setPlaybackStatus(PlaybackStatus::Paused);
  EXPECT_EQ(s->playbackStatusString(), "Paused");
  s->setPlaybackStatus(PlaybackStatus::Stopped);
  EXPECT_EQ(s->playbackStatusString(), "Stopped");
  delete s;
}

TEST(MprisServer, LoopStatusStrings)
{
  auto *s = make_server();
  EXPECT_EQ(s->loopStatusString(), "None");

  s->setLoopStatus(LoopStatus::Track);
  EXPECT_EQ(s->loopStatusString(), "Track");
  s->setLoopStatus(LoopStatus::Playlist);
  EXPECT_EQ(s->loopStatusString(), "Playlist");
  s->setLoopStatus(LoopStatus::None);
  EXPECT_EQ(s->loopStatusString(), "None");
  delete s;
}

// The LoopStatus property is writable on the bus; the string→enum parse must
// round-trip and treat anything unrecognised as None.
TEST(MprisServer, LoopStatusFromString)
{
  EXPECT_EQ(QMprisServer::loopStatusFromString("Track"), LoopStatus::Track);
  EXPECT_EQ(QMprisServer::loopStatusFromString("Playlist"), LoopStatus::Playlist);
  EXPECT_EQ(QMprisServer::loopStatusFromString("None"), LoopStatus::None);
  EXPECT_EQ(QMprisServer::loopStatusFromString("garbage"), LoopStatus::None);
  EXPECT_EQ(QMprisServer::loopStatusFromString(""), LoopStatus::None);
}

TEST(MprisServer, ScalarSettersReflectInGetters)
{
  auto *s = make_server();
  s->setShuffle(true);
  EXPECT_TRUE(s->shuffle());
  s->setVolume(0.5);
  EXPECT_DOUBLE_EQ(s->volume(), 0.5);
  s->setCanGoNext(true);
  s->setCanGoPrevious(true);
  s->setCanPlay(true);
  s->setCanPause(true);
  s->setCanSeek(true);
  s->setCanControl(true);
  EXPECT_TRUE(s->canGoNext());
  EXPECT_TRUE(s->canGoPrevious());
  EXPECT_TRUE(s->canPlay());
  EXPECT_TRUE(s->canPause());
  EXPECT_TRUE(s->canSeek());
  EXPECT_TRUE(s->canControl());
  delete s;
}

TEST(MprisServer, MetadataPushedThrough)
{
  auto *s = make_server();
  mpris::Metadata m;
  m.title("Track").trackId(QDBusObjectPath("/org/mpris/freewave/track/0"));
  s->setMetadata(m);
  EXPECT_EQ(s->metadataMap().value("xesam:title").toString(), "Track");
  delete s;
}

// Position is a cached value, not a PropertiesChanged property. updatePosition
// stores it; emitSeeked also updates the cache (the Seeked D-Bus signal needs a
// live bus, exercised in the round-trip test).
TEST(MprisServer, PositionCaching)
{
  auto *s = make_server();
  s->updatePosition(42'000'000);
  EXPECT_EQ(s->position(), 42'000'000);
  s->emitSeeked(7'000'000);
  EXPECT_EQ(s->position(), 7'000'000);
  delete s;
}

TEST(MprisServer, ConfigExposed)
{
  auto *s = make_server();
  EXPECT_EQ(s->config().identity, "Freewave Test");
  EXPECT_EQ(s->config().serviceSuffix, "freewavetest");
  delete s;
}
