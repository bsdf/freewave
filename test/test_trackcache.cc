#include <gtest/gtest.h>

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "controller/trackcache.hh"
#include "http_test_server.hh"

// TrackCache against a real localhost HTTP server: the cache speaks HTTP and the
// filesystem and nothing else, so everything here is observable as files on disk
// and requests the server did or didn't answer.

static QByteArray
blob(int size, char fill)
{
  return QByteArray(size, fill);
}

class TrackCacheTest : public ::testing::Test {
protected:
  QTemporaryDir tmp;
  HttpTestServer *server = nullptr;
  TrackCache *cache = nullptr;

  void SetUp() override
  {
    ASSERT_TRUE(tmp.isValid());
    server = new HttpTestServer;
    cache = new TrackCache{QDir{tmp.path()}, 0};
    cache->set_scope("profile-a");
  }

  void TearDown() override
  {
    delete cache;
    delete server;
  }

  QDir tracks_dir() const { return QDir{QDir{tmp.path()}.filePath("tracks")}; }

  int complete_files() const
  {
    return static_cast<int>(tracks_dir().entryList({"*.trk"}, QDir::Files).size());
  }

  QString part_file() const
  {
    const auto parts = tracks_dir().entryList({"*.part"}, QDir::Files);
    return parts.isEmpty() ? QString{} : tracks_dir().filePath(parts.first());
  }

  QByteArray read_local(const QUrl &url) const
  {
    QFile f{url.toLocalFile()};
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
  }

  // Spin the event loop until `pred` holds, or give up.
  template<typename Pred>
  bool wait_until(Pred pred, int timeout_ms = 5000) const
  {
    QDeadlineTimer deadline{timeout_ms};
    while (!deadline.hasExpired())
      {
        if (pred()) return true;
        QTest::qWait(10);
      }
    return pred();
  }

  // warm() and wait for it to land.
  bool warm_and_wait(const QString &id, const QString &path)
  {
    QSignalSpy spy{cache, &TrackCache::warmed};
    cache->warm(id, server->url(path));
    return spy.wait(5000);
  }
};

// ---------------------------------------------------------------------------
// Hit and miss
// ---------------------------------------------------------------------------

TEST_F(TrackCacheTest, LocalFor_UnknownTrack_IsEmpty)
{
  EXPECT_TRUE(cache->local_for("t1").isEmpty());
}

TEST_F(TrackCacheTest, Warm_LandsCompleteFileAndAnnouncesIt)
{
  const auto data = blob(64 * 1024, 'a');
  server->add_file("/t1", data);

  QSignalSpy spy{cache, &TrackCache::warmed};
  cache->warm("t1", server->url("/t1"));
  ASSERT_TRUE(spy.wait(5000));

  EXPECT_EQ(spy.first().at(0).toString(), "t1");

  const QUrl local = cache->local_for("t1");
  ASSERT_FALSE(local.isEmpty());
  EXPECT_TRUE(local.isLocalFile());
  EXPECT_EQ(read_local(local), data);
  EXPECT_EQ(spy.first().at(1).toUrl(), local);
}

TEST_F(TrackCacheTest, Warm_LeavesNoPartialBehind)
{
  server->add_file("/t1", blob(16 * 1024, 'a'));
  ASSERT_TRUE(warm_and_wait("t1", "/t1"));
  EXPECT_TRUE(part_file().isEmpty());
}

// The point of the cache: a track played twice is read from disk. The request
// count is the assertion — a hit that still talked to the server is not a hit.
TEST_F(TrackCacheTest, Warm_WhenAlreadyCached_IssuesNoRequest)
{
  server->add_file("/t1", blob(16 * 1024, 'a'));
  ASSERT_TRUE(warm_and_wait("t1", "/t1"));
  ASSERT_EQ(server->request_count("/t1"), 1);

  cache->warm("t1", server->url("/t1"));
  QTest::qWait(100);

  EXPECT_EQ(server->request_count("/t1"), 1);
  EXPECT_FALSE(cache->local_for("t1").isEmpty());
}

TEST_F(TrackCacheTest, Warm_SameTrackTwiceInFlight_IssuesOneRequest)
{
  server->add_file("/t1", blob(256 * 1024, 'a'), HttpTestServer::Stall, 1024);

  cache->warm("t1", server->url("/t1"));
  ASSERT_TRUE(wait_until([&] { return server->request_count("/t1") == 1; }));
  cache->warm("t1", server->url("/t1"));
  QTest::qWait(100);

  EXPECT_EQ(server->request_count("/t1"), 1);
}

// ---------------------------------------------------------------------------
// Failure never produces a hit
// ---------------------------------------------------------------------------

TEST_F(TrackCacheTest, Warm_InterruptedDownload_IsNotAHit)
{
  // Declares the full length, sends 1 KB, closes.
  server->add_file("/t1", blob(64 * 1024, 'a'), HttpTestServer::DropOnce, 1024);

  QSignalSpy spy{cache, &TrackCache::warmed};
  cache->warm("t1", server->url("/t1"));
  ASSERT_TRUE(wait_until([&] { return !cache->is_fetching(); }));

  EXPECT_EQ(spy.count(), 0);
  EXPECT_TRUE(cache->local_for("t1").isEmpty());
  EXPECT_EQ(complete_files(), 0);
}

TEST_F(TrackCacheTest, Warm_RefusedRequest_LeavesNothingOnDisk)
{
  // Nothing registered at this path: the server answers 404.
  QSignalSpy spy{cache, &TrackCache::warmed};
  cache->warm("t1", server->url("/missing"));
  ASSERT_TRUE(wait_until([&] { return !cache->is_fetching(); }));

  EXPECT_EQ(spy.count(), 0);
  EXPECT_TRUE(cache->local_for("t1").isEmpty());
  EXPECT_EQ(complete_files(), 0);
  EXPECT_TRUE(part_file().isEmpty()); // an error body is not audio to resume
}

// ---------------------------------------------------------------------------
// Yielding to playback
// ---------------------------------------------------------------------------

TEST_F(TrackCacheTest, Held_Warm_IssuesNoRequestUntilReleased)
{
  server->add_file("/t1", blob(16 * 1024, 'a'));

  cache->set_held(true);
  cache->warm("t1", server->url("/t1"));
  QTest::qWait(100);
  EXPECT_EQ(server->request_count("/t1"), 0);

  QSignalSpy spy{cache, &TrackCache::warmed};
  cache->set_held(false);
  ASSERT_TRUE(spy.wait(5000));
  EXPECT_EQ(server->request_count("/t1"), 1);
  EXPECT_FALSE(cache->local_for("t1").isEmpty());
}

// A fetch in flight when the player needs the link is dropped, then picked up
// again from the bytes already on disk rather than started over.
TEST_F(TrackCacheTest, Held_MidDownload_AbandonsThenResumesByRange)
{
  const auto data = blob(64 * 1024, 'a');
  server->add_file("/t1", data, HttpTestServer::Stall, 4096);

  cache->warm("t1", server->url("/t1"));
  ASSERT_TRUE(wait_until([&] {
    return !part_file().isEmpty() && QFileInfo{part_file()}.size() == 4096;
  }));

  cache->set_held(true);
  EXPECT_FALSE(cache->is_fetching());
  EXPECT_EQ(complete_files(), 0);
  EXPECT_EQ(QFileInfo{part_file()}.size(), 4096); // the bytes are kept

  server->release_stalled(); // let the abandoned connection go
  server->set_mode("/t1", HttpTestServer::Normal);

  QSignalSpy spy{cache, &TrackCache::warmed};
  cache->set_held(false);
  ASSERT_TRUE(spy.wait(5000));

  EXPECT_EQ(server->request_count("/t1"), 2);
  EXPECT_EQ(server->last_range_start("/t1"), 4096); // resumed, not restarted
  EXPECT_EQ(read_local(cache->local_for("t1")), data);
}

TEST_F(TrackCacheTest, Held_ThenWarmedAgain_KeepsOnlyTheLatestRequest)
{
  server->add_file("/t1", blob(16 * 1024, 'a'));
  server->add_file("/t2", blob(16 * 1024, 'b'));

  cache->set_held(true);
  cache->warm("t1", server->url("/t1"));
  cache->warm("t2", server->url("/t2"));

  QSignalSpy spy{cache, &TrackCache::warmed};
  cache->set_held(false);
  ASSERT_TRUE(spy.wait(5000));

  EXPECT_EQ(spy.first().at(0).toString(), "t2");
  EXPECT_EQ(server->request_count("/t1"), 0);
}

// A track that is already cached needs nothing from the link, so being held
// says nothing about whether it can be played.
TEST_F(TrackCacheTest, Held_StillServesHits)
{
  server->add_file("/t1", blob(16 * 1024, 'a'));
  ASSERT_TRUE(warm_and_wait("t1", "/t1"));

  cache->set_held(true);
  EXPECT_FALSE(cache->local_for("t1").isEmpty());
}

// ---------------------------------------------------------------------------
// Scope
// ---------------------------------------------------------------------------

TEST_F(TrackCacheTest, Scope_NeverServesAnotherProfilesBytes)
{
  server->add_file("/t1", blob(16 * 1024, 'a'));
  ASSERT_TRUE(warm_and_wait("t1", "/t1"));
  ASSERT_FALSE(cache->local_for("t1").isEmpty());

  cache->set_scope("profile-b");
  EXPECT_TRUE(cache->local_for("t1").isEmpty());

  cache->set_scope("profile-a");
  EXPECT_FALSE(cache->local_for("t1").isEmpty());
}

TEST_F(TrackCacheTest, Scope_Change_StopsAnInFlightFetch)
{
  server->add_file("/t1", blob(256 * 1024, 'a'), HttpTestServer::Stall, 1024);

  cache->warm("t1", server->url("/t1"));
  ASSERT_TRUE(wait_until([&] { return cache->is_fetching(); }));

  cache->set_scope("profile-b");
  EXPECT_FALSE(cache->is_fetching());
}

// ---------------------------------------------------------------------------
// Reverse lookup — a local path carries no track id of its own
// ---------------------------------------------------------------------------

TEST_F(TrackCacheTest, IdForLocal_RoundTripsWhatLocalForHandedOut)
{
  server->add_file("/t1", blob(16 * 1024, 'a'));
  ASSERT_TRUE(warm_and_wait("t1", "/t1"));

  const QUrl local = cache->local_for("t1");
  ASSERT_FALSE(local.isEmpty());
  EXPECT_EQ(cache->id_for_local(local), "t1");
}

TEST_F(TrackCacheTest, IdForLocal_RoundTripsWhatWarmedAnnounced)
{
  server->add_file("/t1", blob(16 * 1024, 'a'));
  QSignalSpy spy{cache, &TrackCache::warmed};
  cache->warm("t1", server->url("/t1"));
  ASSERT_TRUE(spy.wait(5000));

  EXPECT_EQ(cache->id_for_local(spy.first().at(1).toUrl()), "t1");
}

TEST_F(TrackCacheTest, IdForLocal_UnknownPath_IsEmpty)
{
  EXPECT_TRUE(cache->id_for_local(QUrl::fromLocalFile("/nowhere/x.trk")).isEmpty());
}

TEST_F(TrackCacheTest, IdForLocal_StreamUrl_IsEmpty)
{
  EXPECT_TRUE(cache->id_for_local(QUrl{"http://host/rest/stream?id=t1"}).isEmpty());
}

// ---------------------------------------------------------------------------
// A cache that can't write is a cache that isn't there
// ---------------------------------------------------------------------------

TEST_F(TrackCacheTest, UnwritableRoot_DegradesToNoCache)
{
  TrackCache broken{QDir{"/proc/freewave-cannot-exist"}, 0};
  broken.set_scope("profile-a");
  server->add_file("/t1", blob(4096, 'a'));

  broken.warm("t1", server->url("/t1"));
  QTest::qWait(100);

  EXPECT_TRUE(broken.local_for("t1").isEmpty());
  EXPECT_EQ(server->request_count("/t1"), 0);
}

// ---------------------------------------------------------------------------
// Budget and eviction
// ---------------------------------------------------------------------------

class TrackCacheBudgetTest : public TrackCacheTest {
protected:
  // Small blobs and a small budget: the arithmetic is the point, not the size.
  static constexpr qint64 TRACK = 16 * 1024;
  static constexpr qint64 BUDGET = 3 * TRACK;

  void SetUp() override
  {
    TrackCacheTest::SetUp();
    delete cache;
    cache = new TrackCache{QDir{tmp.path()}, BUDGET};
    cache->set_scope("profile-a");
    for (int i = 0; i < 6; ++i)
      server->add_file(QString("/t%1").arg(i), blob(TRACK, 'a' + i));
  }

  // Cache tracks in order, oldest use first.
  bool fill(const QStringList &ids)
  {
    for (const auto &id : ids)
      if (!warm_and_wait(id, "/" + id)) return false;
    return true;
  }
};

TEST_F(TrackCacheBudgetTest, StaysUnderTheBudget)
{
  ASSERT_TRUE(fill({"t0", "t1", "t2", "t3", "t4"}));

  EXPECT_LE(cache->bytes_used(), BUDGET);
  EXPECT_EQ(complete_files(), 3);
}

TEST_F(TrackCacheBudgetTest, EvictsTheLeastRecentlyUsed)
{
  ASSERT_TRUE(fill({"t0", "t1", "t2"}));
  ASSERT_EQ(complete_files(), 3);

  ASSERT_TRUE(fill({"t3"}));

  EXPECT_TRUE(cache->local_for("t0").isEmpty()); // the oldest went
  EXPECT_FALSE(cache->local_for("t1").isEmpty());
  EXPECT_FALSE(cache->local_for("t2").isEmpty());
  EXPECT_FALSE(cache->local_for("t3").isEmpty());
}

// Playing a track is a use. Without that, the track you just listened to is the
// first one thrown away.
TEST_F(TrackCacheBudgetTest, AHitCountsAsAUse)
{
  ASSERT_TRUE(fill({"t0", "t1", "t2"}));

  ASSERT_FALSE(cache->local_for("t0").isEmpty()); // t0 is now the newest use
  ASSERT_TRUE(fill({"t3"}));

  EXPECT_FALSE(cache->local_for("t0").isEmpty());
  EXPECT_TRUE(cache->local_for("t1").isEmpty()); // t1 is now the oldest
}

TEST_F(TrackCacheBudgetTest, NeverEvictsWhatIsPinned)
{
  ASSERT_TRUE(fill({"t0", "t1", "t2"}));
  cache->set_pinned({"t0", "t1"}); // playing, and armed to play next

  ASSERT_TRUE(fill({"t3", "t4"}));

  EXPECT_FALSE(cache->local_for("t0").isEmpty());
  EXPECT_FALSE(cache->local_for("t1").isEmpty());
  EXPECT_TRUE(cache->local_for("t2").isEmpty()); // the only evictable one
}

// A budget that can't fit what is playing is not a licence to delete it.
TEST_F(TrackCacheBudgetTest, PinnedEntriesMayExceedTheBudget)
{
  ASSERT_TRUE(fill({"t0", "t1", "t2"}));
  cache->set_pinned({"t0", "t1", "t2"});

  cache->set_budget(TRACK);

  EXPECT_EQ(complete_files(), 3);
  EXPECT_GT(cache->bytes_used(), cache->budget());
}

TEST_F(TrackCacheBudgetTest, LoweringTheBudgetEvictsImmediately)
{
  ASSERT_TRUE(fill({"t0", "t1", "t2"}));
  ASSERT_EQ(complete_files(), 3);

  cache->set_budget(TRACK);

  EXPECT_EQ(complete_files(), 1);
  EXPECT_LE(cache->bytes_used(), TRACK);
}

TEST_F(TrackCacheBudgetTest, ATrackTooBigForTheBudgetIsNotAnnounced)
{
  cache->set_budget(TRACK / 2);

  QSignalSpy spy{cache, &TrackCache::warmed};
  cache->warm("t0", server->url("/t0"));
  ASSERT_TRUE(wait_until([&] { return !cache->is_fetching(); }));
  QTest::qWait(50);

  EXPECT_EQ(spy.count(), 0);
  EXPECT_EQ(complete_files(), 0);
  EXPECT_TRUE(cache->local_for("t0").isEmpty());
}

TEST_F(TrackCacheBudgetTest, ClearEmptiesEverything)
{
  ASSERT_TRUE(fill({"t0", "t1"}));
  cache->set_pinned({"t0"}); // an explicit clear is not eviction

  cache->clear();

  EXPECT_EQ(complete_files(), 0);
  EXPECT_EQ(cache->bytes_used(), 0);
  EXPECT_TRUE(cache->local_for("t0").isEmpty());
}

// ---------------------------------------------------------------------------
// The index survives, and is never the authority on what exists
// ---------------------------------------------------------------------------

TEST_F(TrackCacheBudgetTest, EvictionOrderSurvivesARestart)
{
  ASSERT_TRUE(fill({"t0", "t1", "t2"}));
  ASSERT_FALSE(cache->local_for("t0").isEmpty()); // t0 becomes the newest use
  delete cache;                                   // flushes the index

  cache = new TrackCache{QDir{tmp.path()}, BUDGET};
  cache->set_scope("profile-a");
  EXPECT_EQ(cache->bytes_used(), 3 * TRACK);

  ASSERT_TRUE(fill({"t3"}));
  EXPECT_FALSE(cache->local_for("t0").isEmpty()); // still the newest
  EXPECT_TRUE(cache->local_for("t1").isEmpty());  // still the oldest
}

TEST_F(TrackCacheBudgetTest, ALostIndexIsRebuiltByScanning)
{
  ASSERT_TRUE(fill({"t0", "t1", "t2"}));
  delete cache;
  ASSERT_TRUE(QFile::remove(tracks_dir().filePath("index.json")));

  cache = new TrackCache{QDir{tmp.path()}, BUDGET};
  cache->set_scope("profile-a");

  EXPECT_EQ(cache->bytes_used(), 3 * TRACK);
  EXPECT_FALSE(cache->local_for("t1").isEmpty());
}

TEST_F(TrackCacheBudgetTest, ACorruptIndexCostsNothingButOrder)
{
  ASSERT_TRUE(fill({"t0", "t1"}));
  delete cache;
  {
    QFile f{tracks_dir().filePath("index.json")};
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("{not json at all");
  }

  cache = new TrackCache{QDir{tmp.path()}, BUDGET};
  cache->set_scope("profile-a");

  EXPECT_EQ(cache->bytes_used(), 2 * TRACK);
  EXPECT_FALSE(cache->local_for("t0").isEmpty());
}

// The files are the truth: an entry the index claims but the filesystem has lost
// must not be served, or counted against the budget.
TEST_F(TrackCacheBudgetTest, AFileDeletedBehindOurBackIsNotServed)
{
  ASSERT_TRUE(fill({"t0", "t1"}));
  const QString path = cache->local_for("t0").toLocalFile();
  delete cache;
  ASSERT_TRUE(QFile::remove(path));

  cache = new TrackCache{QDir{tmp.path()}, BUDGET};
  cache->set_scope("profile-a");

  EXPECT_TRUE(cache->local_for("t0").isEmpty());
  EXPECT_EQ(cache->bytes_used(), TRACK);
}

TEST_F(TrackCacheBudgetTest, PartialsDoNotSurviveARestart)
{
  server->add_file("/big", blob(256 * 1024, 'z'), HttpTestServer::Stall, 4096);
  cache->warm("big", server->url("/big"));
  ASSERT_TRUE(wait_until([&] {
    return !part_file().isEmpty() && QFileInfo{part_file()}.size() == 4096;
  }));
  cache->set_held(true); // abandons it, keeping the bytes
  ASSERT_FALSE(part_file().isEmpty());
  delete cache;

  cache = new TrackCache{QDir{tmp.path()}, BUDGET};
  cache->set_scope("profile-a");

  EXPECT_TRUE(part_file().isEmpty());
}

// ---------------------------------------------------------------------------
// Off
// ---------------------------------------------------------------------------

TEST_F(TrackCacheBudgetTest, Disabled_IsAMissAndFetchesNothing)
{
  ASSERT_TRUE(fill({"t0"}));

  cache->set_enabled(false);
  cache->warm("t1", server->url("/t1"));
  QTest::qWait(100);

  EXPECT_TRUE(cache->local_for("t0").isEmpty());
  EXPECT_EQ(server->request_count("/t1"), 0);
}

// Turning the cache off is not a request to delete anything.
TEST_F(TrackCacheBudgetTest, Disabled_KeepsWhatIsAlreadyThere)
{
  ASSERT_TRUE(fill({"t0"}));

  cache->set_enabled(false);
  EXPECT_EQ(complete_files(), 1);

  cache->set_enabled(true);
  EXPECT_FALSE(cache->local_for("t0").isEmpty());
}
