#include <gtest/gtest.h>

#include <QList>
#include <QMap>
#include <QModelIndex>

#include "controller/backend.hh"
#include "model/album.hh"
#include "model/song.hh"

// ---------------------------------------------------------------------------
// Fake backend exercising Backend's default album-level queue ops
// (replace_albums / append_albums / insert_albums) and their shared
// fetch_albums_songs helper. fetch_songs serves a configurable per-album track
// list and can defer callbacks so out-of-order completion is testable.
// ---------------------------------------------------------------------------

namespace {

auto
make_song(const QString &title, const QString &hash) -> song
{
  return song{"uri/" + title, title, "artist", 1, 1, 1000, hash};
}

class FakeBackend : public Backend {
public:
  // album_hash -> its tracks
  QMap<QString, QList<song>> tracks;

  // When true, fetch_songs stashes callbacks instead of firing them inline.
  bool defer = false;

  enum class Op { None,
    Insert,
    Append,
    Replace };
  Op last_op = Op::None;
  QList<song> last_songs;
  uint32_t last_pos = 0;
  int queue_calls = 0;

  // Replay deferred callbacks in reverse request order to simulate albums
  // resolving out of order.
  void flush_reverse()
  {
    for (auto it = pending.rbegin(); it != pending.rend(); ++it)
      (*it)();
    pending.clear();
  }

  void fetch_songs(const album &a,
      std::function<void(const QList<song> &)> cb) override
  {
    auto songs = tracks.value(a.album_hash);
    if (defer)
      pending.push_back([cb, songs] { cb(songs); });
    else if (cb)
      cb(songs);
  }

  // --- unused pure virtuals -------------------------------------------------
  auto get_albums() -> QList<album> override { return {}; }
  bool supports(Feature) const override { return false; }
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
  void remove_from_queue(const QList<QModelIndex> &) override {}
  void rearrange_queue(uint32_t, std::vector<uint32_t>) override {}
  void fetch_album_art(const QString &) override {}

  void insert_queue(const QList<song> &songs, uint32_t pos) override
  {
    last_op = Op::Insert;
    last_songs = songs;
    last_pos = pos;
    ++queue_calls;
  }
  void append_queue(const QList<song> &songs) override
  {
    last_op = Op::Append;
    last_songs = songs;
    ++queue_calls;
  }
  void replace_queue(const QList<song> &songs, uint32_t playpos) override
  {
    last_op = Op::Replace;
    last_songs = songs;
    last_pos = playpos;
    ++queue_calls;
  }

private:
  std::vector<std::function<void()>> pending;
};

auto
titles(const QList<song> &songs) -> QStringList
{
  QStringList out;
  for (const auto &s : songs)
    out << s.title;
  return out;
}

album
make_album(const QString &hash)
{
  album a;
  a.album_hash = hash;
  return a;
}

} // namespace

class BackendAlbumOpsTest : public ::testing::Test {
protected:
  FakeBackend backend;

  void SetUp() override
  {
    backend.tracks["A"] = {make_song("a1", "A"), make_song("a2", "A")};
    backend.tracks["B"] = {make_song("b1", "B")};
  }
};

TEST_F(BackendAlbumOpsTest, ReplaceAlbums_ConcatenatesInOrderAndPlaysFromStart)
{
  backend.replace_albums({make_album("A"), make_album("B")});

  EXPECT_EQ(backend.queue_calls, 1);
  EXPECT_EQ(backend.last_op, FakeBackend::Op::Replace);
  EXPECT_EQ(titles(backend.last_songs), (QStringList{"a1", "a2", "b1"}));
  EXPECT_EQ(backend.last_pos, 0u);
}

TEST_F(BackendAlbumOpsTest, AppendAlbums_AppendsConcatenated)
{
  backend.append_albums({make_album("A"), make_album("B")});

  EXPECT_EQ(backend.queue_calls, 1);
  EXPECT_EQ(backend.last_op, FakeBackend::Op::Append);
  EXPECT_EQ(titles(backend.last_songs), (QStringList{"a1", "a2", "b1"}));
}

TEST_F(BackendAlbumOpsTest, InsertAlbums_InsertsAtRequestedPos)
{
  backend.insert_albums({make_album("B"), make_album("A")}, 5);

  EXPECT_EQ(backend.queue_calls, 1);
  EXPECT_EQ(backend.last_op, FakeBackend::Op::Insert);
  EXPECT_EQ(titles(backend.last_songs), (QStringList{"b1", "a1", "a2"}));
  EXPECT_EQ(backend.last_pos, 5u);
}

TEST_F(BackendAlbumOpsTest, EmptyAlbumList_NoQueueCall)
{
  backend.append_albums({});
  EXPECT_EQ(backend.queue_calls, 0);
}

TEST_F(BackendAlbumOpsTest, AlbumsWithNoTracks_NoQueueCall)
{
  // Albums that resolve to zero tracks must not issue an empty queue command.
  backend.replace_albums({make_album("missing1"), make_album("missing2")});
  EXPECT_EQ(backend.queue_calls, 0);
}

TEST_F(BackendAlbumOpsTest, OutOfOrderCallbacks_PreserveAlbumOrder)
{
  backend.defer = true;
  backend.replace_albums({make_album("A"), make_album("B")});
  EXPECT_EQ(backend.queue_calls, 0); // nothing resolved yet

  backend.flush_reverse(); // B's callback fires before A's
  EXPECT_EQ(backend.queue_calls, 1);
  EXPECT_EQ(titles(backend.last_songs), (QStringList{"a1", "a2", "b1"}));
}
