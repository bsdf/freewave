// Tests for AlbumArtManager that do not require a live MPD instance.
//
// These tests focus on:
//  - Construction safety with a temporary cache directory
//  - get_art / get_queue_thumb cache hit/miss behaviour
//  - rebuild_thumbnails synchronous signal emission (null mpdman path)
//  - clean_thumbnails file management
//
// QPixmapCache and QPixmap work in the headless test environment because
// test_main.cc sets QT_QPA_PLATFORM=offscreen before constructing QApplication.

#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QPixmapCache>
#include <QImage>
#include <QBuffer>
#include <QModelIndex>

#include "controller/albumartmanager.hh"
#include "controller/backend.hh"

// Encode a solid-colour 4×4 image as JPEG bytes in memory.
static QByteArray
make_jpeg_bytes(QColor colour = Qt::blue)
{
  QImage img(4, 4, QImage::Format_RGB888);
  img.fill(colour);
  QByteArray ba;
  QBuffer buf(&ba);
  buf.open(QIODevice::WriteOnly);
  img.save(&buf, "JPEG");
  return ba;
}

// ---------------------------------------------------------------------------
// Minimal Backend stub — lets tests drive album_art_received manually.
// ---------------------------------------------------------------------------

class MockBackend : public Backend {
  Q_OBJECT
public:
  explicit MockBackend(QObject *parent = nullptr)
    : Backend{parent}
  {
  }

  QStringList fetched_uris;

  void deliver(const QString &uri, const QByteArray &bytes)
  {
    emit album_art_received(uri, bytes);
  }

  // ---- pure-virtual stubs ----
  QList<album> get_albums() override { return {}; }
  void fetch_songs(const album &, std::function<void(const QList<song> &)> cb) override
  {
    if (cb) cb({});
  }
  bool connect_to_server() override { return true; }
  void disconnect_from_server() override {}
  void play() override {}
  void pause() override {}
  void stop() override {}
  void prev() override {}
  void next() override {}
  void seek(uint32_t) override {}
  void set_volume(int) override {}
  void play_pos(uint32_t) override {}
  void set_repeat(bool, bool) override {}
  void set_shuffle(bool) override {}
  void insert_queue(const QList<song> &, uint32_t) override {}
  void append_queue(const QList<song> &) override {}
  void replace_queue(const QList<song> &, uint32_t) override {}
  void remove_from_queue(const QList<QModelIndex> &) override {}
  void rearrange_queue(uint32_t, std::vector<uint32_t>) override {}
  void fetch_album_art(const QString &uri) override { fetched_uris.append(uri); }
};

static void
write_test_jpeg(const QDir &dir, const QString &hash)
{
  QImage img(4, 4, QImage::Format_RGB888);
  img.fill(Qt::red);
  img.save(dir.filePath(hash + ".jpg"));
}

// ---------------------------------------------------------------------------
// Fixture — a fresh temp dir and AlbumArtManager for each test.
// ---------------------------------------------------------------------------

class AlbumArtManagerTest : public ::testing::Test {
protected:
  QTemporaryDir tmpdir;
  std::unique_ptr<AlbumArtManager> mgr;

  void SetUp() override
  {
    ASSERT_TRUE(tmpdir.isValid());
    QPixmapCache::clear(); // isolate from other tests
    mgr = std::make_unique<AlbumArtManager>(QDir{tmpdir.path()});
  }
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_F(AlbumArtManagerTest, DefaultConstructed_NoCrash)
{
  SUCCEED();
}

// Constructing with a null MpdManager does not crash.
TEST_F(AlbumArtManagerTest, ConstructWithNullMpdman_NoCrash)
{
  AlbumArtManager m2(QDir{tmpdir.path()}, nullptr);
  SUCCEED();
}

// ---------------------------------------------------------------------------
// get_art — cache miss then cache hit
// ---------------------------------------------------------------------------

// get_art on an unknown hash must not crash and must return a (possibly null)
// pixmap.  The important property is that no exception is thrown and no crash
// occurs.
TEST_F(AlbumArtManagerTest, GetArt_UnknownHash_NoCrash)
{
  auto px = mgr->get_art("nonexistent_hash_abc123");
  // Just reaching here without a crash is the pass condition.
  SUCCEED();
}

// A second call for the same hash must be served from QPixmapCache.
TEST_F(AlbumArtManagerTest, GetArt_SecondCall_CacheHit)
{
  const QString hash{"test_cache_key_xyz"};
  write_test_jpeg(QDir{tmpdir.path()}, hash);

  // First call — populates the cache.
  mgr->get_art(hash);

  // Cache key is size-qualified: hash + "-WxH" (default target is 200x200).
  QPixmap cached;
  EXPECT_TRUE(QPixmapCache::find(hash + "-200x200", &cached));
}

// Calling get_art with two different hashes produces two cache entries.
TEST_F(AlbumArtManagerTest, GetArt_TwoDistinctHashes_TwoCacheEntries)
{
  write_test_jpeg(QDir{tmpdir.path()}, "hash_one");
  write_test_jpeg(QDir{tmpdir.path()}, "hash_two");
  mgr->get_art("hash_one");
  mgr->get_art("hash_two");

  QPixmap px;
  EXPECT_TRUE(QPixmapCache::find("hash_one-200x200", &px));
  EXPECT_TRUE(QPixmapCache::find("hash_two-200x200", &px));
}

// ---------------------------------------------------------------------------
// get_queue_thumb — cache miss then cache hit
// ---------------------------------------------------------------------------

TEST_F(AlbumArtManagerTest, GetQueueThumb_UnknownHash_NoCrash)
{
  mgr->get_queue_thumb("nonexistent_thumb_hash");
  SUCCEED();
}

TEST_F(AlbumArtManagerTest, GetQueueThumb_SecondCall_CacheHit)
{
  const QString hash{"thumb_key_abc"};
  const QString thumb_key = hash + "-thumb";

  mgr->get_queue_thumb(hash);

  QPixmap cached;
  EXPECT_TRUE(QPixmapCache::find(thumb_key, &cached));
}

// ---------------------------------------------------------------------------
// rebuild_thumbnails — signal emission (null mpdman path)
//
// With no MpdManager, rebuild_thumbnails is a synchronous no-op:
//   emit thumbnail_updating(true)   — always first
//   emit thumbnail_updating(false)  — immediately after (no MPD fetching)
// ---------------------------------------------------------------------------

// rebuild_thumbnails must emit thumbnail_updating(true) as the first signal.
TEST_F(AlbumArtManagerTest, RebuildThumbnails_Empty_EmitsThumbnailUpdatingTrue)
{
  QSignalSpy spy(mgr.get(), &AlbumArtManager::thumbnail_updating);

  mgr->rebuild_thumbnails({});

  ASSERT_GE(spy.count(), 1);
  EXPECT_TRUE(spy[0][0].toBool());
}

// With null MpdManager, both true then false are emitted synchronously.
TEST_F(AlbumArtManagerTest, RebuildThumbnails_NullMpdman_EmitsTrueAndFalse)
{
  QSignalSpy spy(mgr.get(), &AlbumArtManager::thumbnail_updating);

  mgr->rebuild_thumbnails({});

  ASSERT_EQ(spy.count(), 2);
  EXPECT_TRUE(spy[0][0].toBool());
  EXPECT_FALSE(spy[1][0].toBool());
}

// rebuild_thumbnails on a non-empty album list with null MpdManager emits
// both signals and does not crash — no MPD I/O is attempted.
TEST_F(AlbumArtManagerTest, RebuildThumbnails_WithAlbums_NullMpdman_EmitsBothSignals)
{
  QSignalSpy spy(mgr.get(), &AlbumArtManager::thumbnail_updating);

  album a;
  a.album_hash = "deadbeef";
  a.uri = "artist/album/track.flac";

  mgr->rebuild_thumbnails({a});

  ASSERT_EQ(spy.count(), 2);
  EXPECT_TRUE(spy[0][0].toBool());
  EXPECT_FALSE(spy[1][0].toBool());
}

// A second rebuild_thumbnails call resets the pipeline and emits the
// complete true/false pair again.
TEST_F(AlbumArtManagerTest, RebuildThumbnails_CalledTwice_EmitsBothPairs)
{
  QSignalSpy spy(mgr.get(), &AlbumArtManager::thumbnail_updating);

  mgr->rebuild_thumbnails({});
  mgr->rebuild_thumbnails({});

  ASSERT_EQ(spy.count(), 4);
  EXPECT_TRUE(spy[0][0].toBool());
  EXPECT_FALSE(spy[1][0].toBool());
  EXPECT_TRUE(spy[2][0].toBool());
  EXPECT_FALSE(spy[3][0].toBool());
}

// ---------------------------------------------------------------------------
// clean_thumbnails — file management
// ---------------------------------------------------------------------------

// clean_thumbnails on an empty album list with an empty cache dir must not crash.
TEST_F(AlbumArtManagerTest, CleanThumbnails_Empty_NoCrash)
{
  mgr->clean_thumbnails({});
  SUCCEED();
}

// After clean_thumbnails, .jpg files NOT in the album list are removed and
// files IN the list are preserved.
TEST_F(AlbumArtManagerTest, CleanThumbnails_RemovesOrphans_KeepsListed)
{
  // Plant two thumbnail files in the cache dir.
  QDir dir{tmpdir.path()};
  auto write_jpg = [&](const QString &name) {
    QFile f{dir.filePath(name)};
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("fake");
  };

  write_jpg("keepme.jpg");
  write_jpg("orphan.jpg");

  // Only "keepme" is in the album list.
  album a;
  a.album_hash = "keepme";
  a.uri = "some/uri";

  mgr->clean_thumbnails({a});

  EXPECT_TRUE(dir.exists("keepme.jpg"));
  EXPECT_FALSE(dir.exists("orphan.jpg"));
}

// ---------------------------------------------------------------------------
// Race: rebuild called while a fetch is in flight
//
// Sequence that used to corrupt the cache:
//   1. rebuild({A, B})  → starts fetching A
//   2. rebuild({B})     → resets pipeline, starts fetching B  (A's request still in-flight)
//   3. A's response arrives late → must be IGNORED, not written to B's file
//   4. B's response arrives     → must be written to B's file
// ---------------------------------------------------------------------------

TEST(AlbumArtManagerRaceTest, RebuildMidFlight_StaleResponseDropped)
{
  QTemporaryDir tmpdir;
  ASSERT_TRUE(tmpdir.isValid());
  QPixmapCache::clear();

  MockBackend backend;
  AlbumArtManager mgr(QDir{tmpdir.path()}, &backend);

  auto make_album = [](const QString &hash, const QString &uri) {
    album a;
    a.album_hash = hash;
    a.uri = uri;
    return a;
  };

  album album_a = make_album("hash_a", "uri_a");
  album album_b = make_album("hash_b", "uri_b");

  // Step 1: start rebuild for [A, B] — pipeline begins fetching A.
  mgr.rebuild_thumbnails({album_a, album_b});
  ASSERT_EQ(backend.fetched_uris.size(), 1);
  EXPECT_EQ(backend.fetched_uris[0], "uri_a");

  // Step 2: library refresh mid-flight — new rebuild with just B.
  //         This must disconnect A's in-flight connection.
  mgr.rebuild_thumbnails({album_b});
  ASSERT_EQ(backend.fetched_uris.size(), 2);
  EXPECT_EQ(backend.fetched_uris[1], "uri_b");

  QDir dir{tmpdir.path()};

  // Step 3: A's stale response arrives — must not touch B's file.
  backend.deliver("uri_a", make_jpeg_bytes(Qt::red));
  EXPECT_FALSE(dir.exists("hash_b.jpg")) << "stale A response must not write to B's cache file";

  // Step 4: B's real response arrives — must be written correctly.
  backend.deliver("uri_b", make_jpeg_bytes(Qt::blue));
  EXPECT_TRUE(dir.exists("hash_b.jpg")) << "B's response must be cached";

  // A was never completed — its file must not exist either.
  EXPECT_FALSE(dir.exists("hash_a.jpg"));
}

// ---------------------------------------------------------------------------
// Playlist cover mosaic — render, persist to disk keyed by id+stamp+size,
// and skip re-rendering / re-fetching on a warm cache.
// ---------------------------------------------------------------------------

TEST_F(AlbumArtManagerTest, PlaylistMosaic_NoScope_NoPersistAndNullOnColdMiss)
{
  // Without a cache scope the mosaic never persists, and a cold miss with no
  // hashes returns null (the caller draws a placeholder + fetches contents).
  EXPECT_TRUE(mgr->get_playlist_mosaic("id", "s1", {}, QSize(34, 34)).isNull());
  EXPECT_FALSE(mgr->has_playlist_mosaic("id", "s1", QSize(34, 34)));
}

TEST_F(AlbumArtManagerTest, PlaylistMosaic_RendersPersistsAndWarmHits)
{
  write_test_jpeg(QDir{tmpdir.path()}, "hA");
  write_test_jpeg(QDir{tmpdir.path()}, "hB");
  mgr->set_cache_scope("profile1");

  EXPECT_FALSE(mgr->has_playlist_mosaic("pl1", "s1", QSize(34, 34)));

  auto pm = mgr->get_playlist_mosaic("pl1", "s1", {"hA", "hB"}, QSize(34, 34));
  ASSERT_FALSE(pm.isNull());
  EXPECT_TRUE(mgr->has_playlist_mosaic("pl1", "s1", QSize(34, 34))); // persisted to disk

  // Warm hit: a fresh manager on the same dir+scope draws it with NO hashes.
  QPixmapCache::clear();
  AlbumArtManager warm{QDir{tmpdir.path()}};
  warm.set_cache_scope("profile1");
  EXPECT_TRUE(warm.has_playlist_mosaic("pl1", "s1", QSize(34, 34)));
  EXPECT_FALSE(warm.get_playlist_mosaic("pl1", "s1", {}, QSize(34, 34)).isNull());
}

TEST_F(AlbumArtManagerTest, PlaylistMosaic_KeyedByStampScopeAndSize)
{
  write_test_jpeg(QDir{tmpdir.path()}, "hA");
  mgr->set_cache_scope("profile1");
  mgr->get_playlist_mosaic("pl1", "s1", {"hA"}, QSize(34, 34));

  // A changed stamp, a different profile, and a different size are all misses.
  EXPECT_TRUE(mgr->has_playlist_mosaic("pl1", "s1", QSize(34, 34)));
  EXPECT_FALSE(mgr->has_playlist_mosaic("pl1", "s2", QSize(34, 34)));   // edited playlist
  EXPECT_FALSE(mgr->has_playlist_mosaic("pl1", "s1", QSize(132, 132))); // header size
  mgr->set_cache_scope("profile2");
  EXPECT_FALSE(mgr->has_playlist_mosaic("pl1", "s1", QSize(34, 34))); // other server
}

// ---------------------------------------------------------------------------
// Fetch-queue ordering — newest additions are fetched first.
// ---------------------------------------------------------------------------

TEST(AlbumArtManagerQueueOrderTest, RebuildFetchesNewestAlbumsFirst)
{
  QTemporaryDir tmpdir;
  ASSERT_TRUE(tmpdir.isValid());
  QPixmapCache::clear();

  MockBackend backend;
  AlbumArtManager mgr(QDir{tmpdir.path()}, &backend);

  auto make_album = [](const QString &hash, const QString &uri, int day) {
    album a;
    a.album_hash = hash;
    a.uri = uri;
    a.last_modified = QDateTime(QDate(2026, 7, day), QTime(0, 0));
    return a;
  };

  // Passed oldest-first; the fetch order must be newest-first.
  mgr.rebuild_thumbnails({
      make_album("h_old", "uri_old", 1),
      make_album("h_new", "uri_new", 15),
      make_album("h_mid", "uri_mid", 8),
  });

  ASSERT_EQ(backend.fetched_uris.size(), 1);
  EXPECT_EQ(backend.fetched_uris[0], "uri_new");
  backend.deliver("uri_new", QByteArray{});
  backend.deliver("uri_mid", QByteArray{});
  backend.deliver("uri_old", QByteArray{});

  EXPECT_EQ(backend.fetched_uris,
      (QStringList{"uri_new", "uri_mid", "uri_old"}));
}

// ---------------------------------------------------------------------------
// Negative art cache — a no-art response writes a `<hash>.noart` marker so
// later rebuilds skip the album instead of re-issuing the fetch forever.
// ---------------------------------------------------------------------------

TEST(AlbumArtManagerNoArtTest, EmptyResponse_WritesMarker_SkipsRefetch)
{
  QTemporaryDir tmpdir;
  ASSERT_TRUE(tmpdir.isValid());
  QPixmapCache::clear();

  MockBackend backend;
  AlbumArtManager mgr(QDir{tmpdir.path()}, &backend);

  album a;
  a.album_hash = "hash_a";
  a.uri = "uri_a";

  mgr.rebuild_thumbnails({a});
  ASSERT_EQ(backend.fetched_uris.size(), 1);
  backend.deliver("uri_a", QByteArray{});

  QDir dir{tmpdir.path()};
  EXPECT_TRUE(dir.exists("hash_a.noart"));

  // Second rebuild: the marker suppresses the fetch entirely.
  mgr.rebuild_thumbnails({a});
  EXPECT_EQ(backend.fetched_uris.size(), 1);
}

TEST(AlbumArtManagerNoArtTest, ChangedUri_InvalidatesMarker_RefetchesAndClears)
{
  QTemporaryDir tmpdir;
  ASSERT_TRUE(tmpdir.isValid());
  QPixmapCache::clear();

  MockBackend backend;
  AlbumArtManager mgr(QDir{tmpdir.path()}, &backend);

  album a;
  a.album_hash = "hash_a";
  a.uri = "uri_old";

  mgr.rebuild_thumbnails({a});
  backend.deliver("uri_old", QByteArray{});
  QDir dir{tmpdir.path()};
  ASSERT_TRUE(dir.exists("hash_a.noart"));

  // The server grew art: the coverArt id (fetch uri) changed, so the stale
  // marker must not suppress the fetch, and success must remove it.
  a.uri = "uri_new";
  mgr.rebuild_thumbnails({a});
  ASSERT_EQ(backend.fetched_uris.size(), 2);
  EXPECT_EQ(backend.fetched_uris[1], "uri_new");

  backend.deliver("uri_new", make_jpeg_bytes());
  EXPECT_TRUE(dir.exists("hash_a.jpg"));
  EXPECT_FALSE(dir.exists("hash_a.noart"));
}

TEST(AlbumArtManagerNoArtTest, UndecodableResponse_AlsoWritesMarker)
{
  QTemporaryDir tmpdir;
  ASSERT_TRUE(tmpdir.isValid());
  QPixmapCache::clear();

  MockBackend backend;
  AlbumArtManager mgr(QDir{tmpdir.path()}, &backend);

  album a;
  a.album_hash = "hash_a";
  a.uri = "uri_a";

  mgr.rebuild_thumbnails({a});
  backend.deliver("uri_a", QByteArray{"not an image"});

  EXPECT_TRUE(QDir{tmpdir.path()}.exists("hash_a.noart"));
}

// Manual refresh recovery: clear_noart_markers drops every marker so the next
// rebuild refetches — the only recovery path when the fetch uri is stable (MPD).
TEST(AlbumArtManagerNoArtTest, ClearNoartMarkers_NextRebuildRefetches)
{
  QTemporaryDir tmpdir;
  ASSERT_TRUE(tmpdir.isValid());
  QPixmapCache::clear();

  MockBackend backend;
  AlbumArtManager mgr(QDir{tmpdir.path()}, &backend);

  album a;
  a.album_hash = "hash_a";
  a.uri = "uri_a";

  mgr.rebuild_thumbnails({a});
  backend.deliver("uri_a", QByteArray{});
  mgr.rebuild_thumbnails({a});
  ASSERT_EQ(backend.fetched_uris.size(), 1); // marker suppressed the second fetch

  mgr.clear_noart_markers();
  EXPECT_FALSE(QDir{tmpdir.path()}.exists("hash_a.noart"));

  mgr.rebuild_thumbnails({a});
  EXPECT_EQ(backend.fetched_uris.size(), 2);
}

TEST_F(AlbumArtManagerTest, CleanThumbnails_PrunesOrphanMarkers_KeepsListed)
{
  QDir dir{tmpdir.path()};
  auto write_file = [&](const QString &name) {
    QFile f{dir.filePath(name)};
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("uri");
  };
  write_file("keepme.noart");
  write_file("orphan.noart");

  album a;
  a.album_hash = "keepme";
  a.uri = "some/uri";

  mgr->clean_thumbnails({a});

  EXPECT_TRUE(dir.exists("keepme.noart"));
  EXPECT_FALSE(dir.exists("orphan.noart"));
}

#include "test_albumartmanager.moc"
