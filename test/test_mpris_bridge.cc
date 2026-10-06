#include <gtest/gtest.h>

#include "controller/mprisbridge.hh"
#include "controller/backend.hh"
#include "controller/librarymanager.hh"
#include "mprislib/qmprisserver.hh"
#include "model/song.hh"
#include "model/album.hh"

#include <QDir>
#include <QDBusObjectPath>
#include <QFile>
#include <QUrl>
#include <limits>
#include <memory>

namespace {

// Minimal Backend that records the transport commands the bridge issues and lets
// a test push state out via the inherited Backend signals.
class StubBackend : public Backend {
public:
  // Recorded request side.
  int play_calls = 0, pause_calls = 0, stop_calls = 0, next_calls = 0, prev_calls = 0;
  uint32_t last_seek = std::numeric_limits<uint32_t>::max();
  int last_volume = -999;
  bool repeat = false, single = false, shuffle = false;

  auto get_albums() -> QList<album> override { return {}; }
  void fetch_songs(const album &, std::function<void(const QList<song> &)>) override {}

  bool connect_to_server() override { return true; }
  void disconnect_from_server() override {}
  void play() override { ++play_calls; }
  void pause() override { ++pause_calls; }
  void stop() override { ++stop_calls; }
  void prev() override { ++prev_calls; }
  void next() override { ++next_calls; }
  void seek(uint32_t ms) override { last_seek = ms; }
  void set_volume(int v) override { last_volume = v; }
  void play_pos(uint32_t) override {}
  void set_repeat(bool r, bool s) override
  {
    repeat = r;
    single = s;
  }
  void set_shuffle(bool s) override { shuffle = s; }
  void insert_queue(const QList<song> &, uint32_t) override {}
  void append_queue(const QList<song> &) override {}
  void replace_queue(const QList<song> &, uint32_t) override {}
  void remove_from_queue(const QList<QModelIndex> &) override {}
  void rearrange_queue(uint32_t, std::vector<uint32_t>) override {}
  void fetch_album_art(const QString &) override {}
};

struct BridgeFixture {
  StubBackend backend;
  std::shared_ptr<LibraryManager> libman = std::make_shared<LibraryManager>();
  std::unique_ptr<MprisBridge> bridge;
  mpris::QMprisServer *srv = nullptr;

  BridgeFixture()
  {
    bridge = std::make_unique<MprisBridge>(
        &backend, QDir(QDir::tempPath()), libman.get(), nullptr);
    srv = bridge->server_object();
  }

  auto state(PlayState st, int vol, bool repeat, bool single, bool shuffle,
      uint32_t elapsed_ms, uint32_t total_ms, int queue_pos) -> PlaybackState
  {
    PlaybackState ps;
    ps.state = st;
    ps.volume = vol;
    ps.repeat = repeat;
    ps.single = single;
    ps.shuffle = shuffle;
    ps.elapsed_ms = elapsed_ms;
    ps.total_ms = total_ms;
    ps.queue_pos = queue_pos;
    return ps;
  }
};

} // namespace

// --- Backend → server (state push) ------------------------------------------

TEST(MprisBridge, PlaybackStateMapsToServer)
{
  BridgeFixture f;
  emit f.backend.playback_state_changed(
      f.state(PlayState::Playing, 80, true, false, true, 30'000, 180'000, 0));

  EXPECT_EQ(f.srv->playbackStatusString(), "Playing");
  EXPECT_DOUBLE_EQ(f.srv->volume(), 0.8);
  EXPECT_EQ(f.srv->loopStatusString(), "Playlist"); // repeat && !single
  EXPECT_TRUE(f.srv->shuffle());
  EXPECT_EQ(f.srv->position(), 30'000LL * 1000); // ms → µs
}

TEST(MprisBridge, SingleMapsToTrackLoop)
{
  BridgeFixture f;
  emit f.backend.playback_state_changed(
      f.state(PlayState::Paused, 50, false, true, false, 0, 0, 0));
  EXPECT_EQ(f.srv->playbackStatusString(), "Paused");
  EXPECT_EQ(f.srv->loopStatusString(), "Track"); // single wins
}

TEST(MprisBridge, ConnectionGatesCanFlags)
{
  BridgeFixture f;
  emit f.backend.connection_update(true);
  EXPECT_TRUE(f.srv->canControl());
  EXPECT_TRUE(f.srv->canPlay());
  EXPECT_TRUE(f.srv->canPause());
  EXPECT_TRUE(f.srv->canGoNext());
  EXPECT_TRUE(f.srv->canGoPrevious());
  EXPECT_TRUE(f.srv->canSeek());

  emit f.backend.connection_update(false);
  EXPECT_FALSE(f.srv->canControl());
  EXPECT_FALSE(f.srv->canPlay());
  EXPECT_EQ(f.srv->playbackStatusString(), "Stopped");
}

TEST(MprisBridge, CurrentSongBuildsMetadata)
{
  BridgeFixture f;
  album a("uri", "h1", "AlbumName", "AlbumArtist", "2020", "AlbumArtist", {});
  f.libman->add_album("h1", a);

  song s("file:///x.flac", "The Title", "The Artist",
      /*track*/ 4, /*disc*/ 2, /*duration ms*/ 200'000, "h1");
  emit f.backend.current_song_changed(s);

  const QVariantMap m = f.srv->metadataMap();
  EXPECT_EQ(m.value("xesam:title").toString(), "The Title");
  EXPECT_EQ(m.value("xesam:artist").toStringList(), (QStringList{"The Artist"}));
  EXPECT_EQ(m.value("mpris:length").toLongLong(), 200'000LL * 1000);
  EXPECT_EQ(m.value("xesam:trackNumber").toInt(), 4);
  EXPECT_EQ(m.value("xesam:discNumber").toInt(), 2);
  EXPECT_EQ(m.value("xesam:album").toString(), "AlbumName");
  EXPECT_EQ(m.value("xesam:albumArtist").toStringList(), (QStringList{"AlbumArtist"}));

  const auto path = m.value("mpris:trackid").value<QDBusObjectPath>().path();
  EXPECT_TRUE(path.startsWith("/org/mpris/freewave/track/"));
}

TEST(MprisBridge, TrackIdChangesPerSong)
{
  BridgeFixture f;
  song s1("a", "T1", "A", 1, 1, 1000, "h");
  song s2("b", "T2", "A", 2, 1, 1000, "h");
  emit f.backend.current_song_changed(s1);
  const auto id1 = f.srv->metadataMap().value("mpris:trackid").value<QDBusObjectPath>().path();
  emit f.backend.current_song_changed(s2);
  const auto id2 = f.srv->metadataMap().value("mpris:trackid").value<QDBusObjectPath>().path();
  EXPECT_NE(id1, id2);
}

TEST(MprisBridge, ArtReadyRePushesCurrentTrackCover)
{
  BridgeFixture f;
  const QString hash = "mpris_art_ready_test_hash";
  const QString jpg = QDir(QDir::tempPath()).filePath(hash + ".jpg");
  QFile::remove(jpg); // ensure a clean slate

  song s("a", "T", "A", 1, 1, 1000, hash);
  emit f.backend.current_song_changed(s);

  // No cover on disk yet → no artUrl advertised.
  EXPECT_FALSE(f.srv->metadataMap().contains("mpris:artUrl"));
  const auto id_before = f.srv->metadataMap().value("mpris:trackid").value<QDBusObjectPath>().path();

  // Cover lands for the current album → re-push fills in artUrl, same trackid.
  QFile out(jpg);
  ASSERT_TRUE(out.open(QIODevice::WriteOnly));
  out.write("not-a-real-jpeg-but-existence-is-all-that-matters");
  out.close();

  f.bridge->on_art_ready(hash);

  const QVariantMap m = f.srv->metadataMap();
  EXPECT_EQ(m.value("mpris:artUrl").toString(), QUrl::fromLocalFile(jpg).toString());
  EXPECT_EQ(m.value("mpris:trackid").value<QDBusObjectPath>().path(), id_before);

  QFile::remove(jpg);
}

TEST(MprisBridge, ArtReadyIgnoresOtherAlbum)
{
  BridgeFixture f;
  const QString hash = "mpris_art_ready_other_hash";
  const QString jpg = QDir(QDir::tempPath()).filePath(hash + ".jpg");
  QFile out(jpg);
  ASSERT_TRUE(out.open(QIODevice::WriteOnly));
  out.write("x");
  out.close();

  song s("a", "T", "A", 1, 1, 1000, "the_current_album");
  emit f.backend.current_song_changed(s);

  f.bridge->on_art_ready(hash); // a different album's art arrived
  EXPECT_FALSE(f.srv->metadataMap().contains("mpris:artUrl"));

  QFile::remove(jpg);
}

// --- Server → backend (request translation) ---------------------------------

TEST(MprisBridge, TransportRequestsForwarded)
{
  BridgeFixture f;
  emit f.srv->playRequested();
  emit f.srv->pauseRequested();
  emit f.srv->stopRequested();
  emit f.srv->nextRequested();
  emit f.srv->previousRequested();
  EXPECT_EQ(f.backend.play_calls, 1);
  EXPECT_EQ(f.backend.pause_calls, 1);
  EXPECT_EQ(f.backend.stop_calls, 1);
  EXPECT_EQ(f.backend.next_calls, 1);
  EXPECT_EQ(f.backend.prev_calls, 1);
}

TEST(MprisBridge, PlayPauseTogglesOnCachedState)
{
  BridgeFixture f;
  emit f.backend.playback_state_changed(
      f.state(PlayState::Playing, 50, false, false, false, 0, 1000, 0));
  emit f.srv->playPauseRequested();
  EXPECT_EQ(f.backend.pause_calls, 1);
  EXPECT_EQ(f.backend.play_calls, 0);

  emit f.backend.playback_state_changed(
      f.state(PlayState::Paused, 50, false, false, false, 0, 1000, 0));
  emit f.srv->playPauseRequested();
  EXPECT_EQ(f.backend.play_calls, 1);
}

TEST(MprisBridge, VolumeRequestScaledToPercent)
{
  BridgeFixture f;
  emit f.srv->volumeRequested(0.25);
  EXPECT_EQ(f.backend.last_volume, 25);
  emit f.srv->volumeRequested(1.0);
  EXPECT_EQ(f.backend.last_volume, 100);
}

TEST(MprisBridge, RelativeSeekClamped)
{
  BridgeFixture f;
  emit f.backend.playback_state_changed(
      f.state(PlayState::Playing, 50, false, false, false, 30'000, 180'000, 0));

  emit f.srv->seekRequested(10'000'000); // +10 s
  EXPECT_EQ(f.backend.last_seek, 40'000u);

  emit f.srv->seekRequested(-100'000'000); // way before start → clamp to 0
  EXPECT_EQ(f.backend.last_seek, 0u);

  emit f.srv->seekRequested(1'000'000'000); // past end → clamp to total
  EXPECT_EQ(f.backend.last_seek, 180'000u);
}

TEST(MprisBridge, SetPositionGuardsOnTrackId)
{
  BridgeFixture f;
  emit f.backend.playback_state_changed(
      f.state(PlayState::Playing, 50, false, false, false, 0, 180'000, 0));
  song s("a", "T", "A", 1, 1, 180'000, "h");
  emit f.backend.current_song_changed(s);
  const QDBusObjectPath id(
      f.srv->metadataMap().value("mpris:trackid").value<QDBusObjectPath>());

  emit f.srv->setPositionRequested(id, 60'000'000); // 60 s, matching track
  EXPECT_EQ(f.backend.last_seek, 60'000u);

  emit f.srv->setPositionRequested(
      QDBusObjectPath("/org/mpris/freewave/track/999"), 5'000'000); // stale id
  EXPECT_EQ(f.backend.last_seek, 60'000u);                          // unchanged
}

TEST(MprisBridge, LoopStatusRequestMapsToRepeatSingle)
{
  BridgeFixture f;
  emit f.srv->loopStatusRequested(mpris::LoopStatus::Track);
  EXPECT_TRUE(f.backend.repeat);
  EXPECT_TRUE(f.backend.single);

  emit f.srv->loopStatusRequested(mpris::LoopStatus::Playlist);
  EXPECT_TRUE(f.backend.repeat);
  EXPECT_FALSE(f.backend.single);

  emit f.srv->loopStatusRequested(mpris::LoopStatus::None);
  EXPECT_FALSE(f.backend.repeat);
  EXPECT_FALSE(f.backend.single);
}

TEST(MprisBridge, ShuffleRequestForwarded)
{
  BridgeFixture f;
  emit f.srv->shuffleRequested(true);
  EXPECT_TRUE(f.backend.shuffle);
  emit f.srv->shuffleRequested(false);
  EXPECT_FALSE(f.backend.shuffle);
}
