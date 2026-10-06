// LibraryManager's native-id index: the link between a standalone song (which
// knows only its server-native album id) and the album_hash the library keys
// that album under.

#include <gtest/gtest.h>

#include "controller/librarymanager.hh"

namespace {

auto
make_album(const QString &hash, const QString &native_id) -> album
{
  album a;
  a.album_hash = hash;
  a.native_id = native_id;
  return a;
}

} // namespace

TEST(LibraryManagerIndex, ResolvesNativeIdToAlbumHash)
{
  LibraryManager libman;
  libman.add_album("mbid-1", make_album("mbid-1", "alb1"));

  EXPECT_EQ(libman.album_hash_for_native("alb1"), "mbid-1");
}

// An unknown id resolves to nothing rather than to a guess: the fallback is
// backend-specific (profile-scoped on Subsonic), so the caller applies it.
TEST(LibraryManagerIndex, UnknownNativeIdResolvesEmpty)
{
  LibraryManager libman;
  libman.add_album("mbid-1", make_album("mbid-1", "alb1"));

  EXPECT_TRUE(libman.album_hash_for_native("alb2").isEmpty());
}

// MPD albums have no native id — they must not enter the index under an empty
// key, or every unresolvable lookup would answer with whichever landed last.
TEST(LibraryManagerIndex, AlbumsWithoutNativeIdAreNotIndexed)
{
  LibraryManager libman;
  libman.add_album("hash-a", make_album("hash-a", ""));
  libman.add_album("hash-b", make_album("hash-b", ""));

  EXPECT_TRUE(libman.album_hash_for_native("").isEmpty());
  EXPECT_TRUE(libman.native_index().isEmpty());
}

TEST(LibraryManagerIndex, ClearDropsTheIndex)
{
  LibraryManager libman;
  libman.add_album("mbid-1", make_album("mbid-1", "alb1"));
  libman.clear();

  EXPECT_TRUE(libman.album_hash_for_native("alb1").isEmpty());
  EXPECT_TRUE(libman.native_index().isEmpty());
}

// A poll that reloads the same albums must produce an index that compares equal
// to the previous one — that comparison is what tells holders of resolved songs
// they have nothing to re-resolve.
TEST(LibraryManagerIndex, IdenticalReloadProducesEqualIndex)
{
  LibraryManager libman;
  libman.add_album("mbid-1", make_album("mbid-1", "alb1"));
  libman.add_album("mbid-2", make_album("mbid-2", "alb2"));

  auto before = libman.native_index();

  libman.clear();
  libman.add_album("mbid-2", make_album("mbid-2", "alb2")); // insertion order differs
  libman.add_album("mbid-1", make_album("mbid-1", "alb1"));

  EXPECT_EQ(libman.native_index(), before);
}

// The hash under a native id moves when the server starts reporting an MBID for
// an album it previously had none for. Everything resolved through the old hash
// is stale at that point.
TEST(LibraryManagerIndex, HashMovingUnderANativeIdChangesTheIndex)
{
  LibraryManager libman;
  libman.add_album("profile:alb1", make_album("profile:alb1", "alb1"));

  auto before = libman.native_index();

  libman.clear();
  libman.add_album("mbid-1", make_album("mbid-1", "alb1"));

  EXPECT_NE(libman.native_index(), before);
  EXPECT_EQ(libman.album_hash_for_native("alb1"), "mbid-1");
}
