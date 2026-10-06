#include "vizlib/ringbuffer.hh"

#include <gtest/gtest.h>

#include <algorithm>
#include <thread>
#include <vector>

using viz::RingBuffer;

TEST(RingBuffer, EmptyReadReturnsZero)
{
  RingBuffer rb(8);
  float dst[4];
  EXPECT_EQ(rb.read(dst, 4), 0u);
  EXPECT_EQ(rb.available(), 0u);
}

TEST(RingBuffer, WriteReadRoundtrip)
{
  RingBuffer rb(8);
  float src[4] = {1, 2, 3, 4};
  EXPECT_EQ(rb.write(src, 4), 4u);
  EXPECT_EQ(rb.available(), 4u);

  float dst[4] = {};
  EXPECT_EQ(rb.read(dst, 4), 4u);
  for (int i = 0; i < 4; ++i)
    EXPECT_FLOAT_EQ(dst[i], src[i]);
  EXPECT_EQ(rb.available(), 0u);
}

TEST(RingBuffer, PartialReadWhenLessAvailable)
{
  RingBuffer rb(8);
  float src[2] = {5, 6};
  rb.write(src, 2);

  float dst[4] = {};
  EXPECT_EQ(rb.read(dst, 4), 2u); // only 2 available
  EXPECT_FLOAT_EQ(dst[0], 5);
  EXPECT_FLOAT_EQ(dst[1], 6);
}

TEST(RingBuffer, OverrunDropsNewestKeepsOldest)
{
  RingBuffer rb(4); // 4 usable slots
  float src[10];
  for (int i = 0; i < 10; ++i)
    src[i] = static_cast<float>(i);

  EXPECT_EQ(rb.write(src, 10), 4u); // only capacity accepted
  EXPECT_EQ(rb.available(), 4u);

  float dst[4] = {};
  EXPECT_EQ(rb.read(dst, 4), 4u);
  for (int i = 0; i < 4; ++i)
    EXPECT_FLOAT_EQ(dst[i], static_cast<float>(i)); // oldest retained
}

TEST(RingBuffer, WraparoundManyCycles)
{
  RingBuffer rb(4);
  float v = 0;
  for (int cycle = 0; cycle < 100; ++cycle)
    {
      float src[3] = {v, v + 1, v + 2};
      v += 3;
      ASSERT_EQ(rb.write(src, 3), 3u);

      float dst[3] = {};
      ASSERT_EQ(rb.read(dst, 3), 3u);
      EXPECT_FLOAT_EQ(dst[0], src[0]);
      EXPECT_FLOAT_EQ(dst[1], src[1]);
      EXPECT_FLOAT_EQ(dst[2], src[2]);
    }
}

TEST(RingBuffer, AvailableTracksOccupancy)
{
  RingBuffer rb(16);
  float s[5] = {1, 2, 3, 4, 5};
  rb.write(s, 5);
  EXPECT_EQ(rb.available(), 5u);

  float d[2];
  rb.read(d, 2);
  EXPECT_EQ(rb.available(), 3u);

  rb.write(s, 4);
  EXPECT_EQ(rb.available(), 7u);
}

TEST(RingBuffer, WriteToFullReturnsZero)
{
  RingBuffer rb(4);
  float s[4] = {1, 2, 3, 4};
  EXPECT_EQ(rb.write(s, 4), 4u);
  EXPECT_EQ(rb.write(s, 4), 0u); // full
}

// SPSC correctness: a producer thread streams a strict ramp (retrying the
// remainder on a short write, so nothing is dropped); the consumer must observe
// every value exactly once, in order — no tearing, duplication, or reordering.
TEST(RingBuffer, SpscThreadedNoCorruption)
{
  RingBuffer rb(1024);
  constexpr std::size_t N = 200000;

  std::thread producer([&] {
    std::size_t i = 0;
    while (i < N)
      {
        float chunk[64];
        const std::size_t n = std::min<std::size_t>(64, N - i);
        for (std::size_t k = 0; k < n; ++k)
          chunk[k] = static_cast<float>(i + k);
        i += rb.write(chunk, n); // advance only by what was accepted
      }
  });

  float last = -1.0f;
  std::size_t got = 0;
  std::vector<float> dst(256);
  while (got < N)
    {
      const std::size_t r = rb.read(dst.data(), dst.size());
      for (std::size_t k = 0; k < r; ++k)
        {
          ASSERT_GT(dst[k], last); // strictly increasing
          last = dst[k];
        }
      got += r;
    }

  producer.join();
  EXPECT_EQ(last, static_cast<float>(N - 1));
}
