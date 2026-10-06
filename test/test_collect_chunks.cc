#include <gtest/gtest.h>

#include <cstring>
#include <numeric>

#include "mpdlib/chunk_reader.hh"

using mpd::collect_chunks;

// Reader that immediately signals EOF.
TEST(CollectChunks, EmptyRead)
{
  auto result = collect_chunks(
      [](std::size_t, void *, std::size_t) -> int64_t { return 0; }, 1024);
  EXPECT_TRUE(result.empty());
}

// Single read that fits within one chunk.
TEST(CollectChunks, SingleChunk)
{
  const std::vector<uint8_t> data = {1, 2, 3, 4, 5};
  bool done = false;

  auto result = collect_chunks(
      [&](std::size_t offset, void *buf, std::size_t) -> int64_t {
        if (done) return 0;
        done = true;
        EXPECT_EQ(offset, 0u);
        std::memcpy(buf, data.data(), data.size());
        return static_cast<int64_t>(data.size());
      },
      1024);

  EXPECT_EQ(result, data);
}

// Data larger than chunk_size — exercises the accumulation loop.
TEST(CollectChunks, MultipleChunks)
{
  constexpr std::size_t chunk_size = 10;
  constexpr std::size_t total = 25;

  std::vector<uint8_t> data(total);
  std::iota(data.begin(), data.end(), uint8_t{0});

  auto result = collect_chunks(
      [&](std::size_t offset, void *buf, std::size_t size) -> int64_t {
        if (offset >= total) return 0;
        std::size_t n = std::min(size, total - offset);
        std::memcpy(buf, data.data() + offset, n);
        return static_cast<int64_t>(n);
      },
      chunk_size);

  EXPECT_EQ(result, data);
}

// The 512 KB chunk buffer (ALBUM_ART_CHUNK_SZ) must be heap-allocated.
// If it were on the stack this would likely crash or corrupt the stack on
// threads with limited stack space. Running it here proves the allocation
// succeeds and the function returns cleanly with no data.
TEST(CollectChunks, LargeChunkSizeDoesNotOverflowStack)
{
  constexpr std::size_t album_art_chunk_sz = 524288;
  bool called = false;

  auto result = collect_chunks(
      [&](std::size_t, void *, std::size_t) -> int64_t {
        called = true;
        return 0;
      },
      album_art_chunk_sz);

  EXPECT_TRUE(called);
  EXPECT_TRUE(result.empty());
}
