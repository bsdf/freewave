#include "vizlib/spectrumanalyzer.hh"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using viz::SpectrumAnalyzer;

namespace {
constexpr int RATE = 48000;
constexpr int NFFT = 2048;
constexpr int BANDS = 32;
// Must mirror the log-band edges in spectrumanalyzer.cc.
constexpr float FMIN = 35.0f;
constexpr float FMAX = 17000.0f;

auto
expected_band(float f) -> int
{
  const float ratio = FMAX / FMIN;
  int e = 0;
  for (int b = 0; b < BANDS; ++b)
    {
      const float lo = FMIN * std::pow(ratio, float(b) / BANDS);
      const float hi = FMIN * std::pow(ratio, float(b + 1) / BANDS);
      if (f >= lo && f < hi)
        e = b;
    }
  return e;
}

auto
sine(float freq, float amp, int n) -> std::vector<float>
{
  std::vector<float> v(n);
  for (int i = 0; i < n; ++i)
    v[i] = amp * std::sin(2.0 * M_PI * freq * i / RATE);
  return v;
}

auto
peak_band(const std::vector<float> &b) -> int
{
  int p = 0;
  float pv = -1.0f;
  for (std::size_t i = 0; i < b.size(); ++i)
    if (b[i] > pv)
      {
        pv = b[i];
        p = static_cast<int>(i);
      }
  return p;
}
} // namespace

TEST(SpectrumAnalyzer, FalseBeforeFullWindow)
{
  SpectrumAnalyzer a(RATE, NFFT, BANDS);
  auto s = sine(1000, 0.5f, NFFT / 2); // less than one window
  a.feed(s.data(), s.size());

  std::vector<float> bands;
  float level = 0;
  EXPECT_FALSE(a.compute(bands, level));
}

TEST(SpectrumAnalyzer, TrueAfterFullWindow)
{
  SpectrumAnalyzer a(RATE, NFFT, BANDS);
  auto s = sine(1000, 0.5f, NFFT);
  a.feed(s.data(), s.size());

  std::vector<float> bands;
  float level = 0;
  EXPECT_TRUE(a.compute(bands, level));
  EXPECT_EQ(bands.size(), static_cast<std::size_t>(BANDS));
}

TEST(SpectrumAnalyzer, AllOutputsWithinUnitRange)
{
  SpectrumAnalyzer a(RATE, NFFT, BANDS);
  auto s = sine(1000, 1.0f, NFFT);
  a.feed(s.data(), s.size());

  std::vector<float> bands;
  float level = 0;
  ASSERT_TRUE(a.compute(bands, level));
  for (float v : bands)
    {
      EXPECT_GE(v, 0.0f);
      EXPECT_LE(v, 1.0f);
    }
  EXPECT_GE(level, 0.0f);
  EXPECT_LE(level, 1.0f);
}

class SinePeak : public testing::TestWithParam<float> {};

TEST_P(SinePeak, PeaksAtExpectedBand)
{
  const float freq = GetParam();
  SpectrumAnalyzer a(RATE, NFFT, BANDS);
  auto s = sine(freq, 0.6f, RATE / 4);
  a.feed(s.data(), s.size());

  std::vector<float> bands;
  float level = 0;
  ASSERT_TRUE(a.compute(bands, level));

  const int peak = peak_band(bands);
  const int exp = expected_band(freq);
  EXPECT_NEAR(peak, exp, 1) << "freq=" << freq << " peak=" << peak << " expected=" << exp;
}

INSTANTIATE_TEST_SUITE_P(Frequencies, SinePeak,
    testing::Values(500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 12000.0f));

TEST(SpectrumAnalyzer, SilenceDecaysToZero)
{
  SpectrumAnalyzer a(RATE, NFFT, BANDS);
  auto s = sine(1000, 0.8f, NFFT);
  a.feed(s.data(), s.size());

  std::vector<float> bands;
  float level = 0;
  a.compute(bands, level);

  std::vector<float> z(NFFT, 0.0f);
  for (int i = 0; i < 100; ++i) // many frames so gravity fully unwinds
    {
      a.feed(z.data(), z.size());
      a.compute(bands, level);
    }

  float mx = 0;
  for (float v : bands)
    mx = std::max(mx, v);
  EXPECT_NEAR(mx, 0.0f, 1e-3f);
  EXPECT_NEAR(level, 0.0f, 1e-6f);
}

TEST(SpectrumAnalyzer, GravityFallIsGradualNotInstant)
{
  SpectrumAnalyzer a(RATE, NFFT, BANDS);
  auto s = sine(1000, 0.8f, NFFT);
  a.feed(s.data(), s.size());

  std::vector<float> loud;
  float level = 0;
  a.compute(loud, level);
  const int pb = peak_band(loud);
  const float before = loud[pb];
  ASSERT_GT(before, 0.3f);

  std::vector<float> z(NFFT, 0.0f);
  a.feed(z.data(), z.size());
  std::vector<float> after;
  a.compute(after, level);

  EXPECT_LT(after[pb], before);        // it falls
  EXPECT_GT(after[pb], before - 0.2f); // but gravity caps a single-frame drop
}

TEST(SpectrumAnalyzer, LevelRespondsToAmplitude)
{
  SpectrumAnalyzer quiet(RATE, NFFT, BANDS);
  SpectrumAnalyzer loud(RATE, NFFT, BANDS);
  auto sq = sine(1000, 0.05f, NFFT);
  auto sl = sine(1000, 0.8f, NFFT);
  quiet.feed(sq.data(), sq.size());
  loud.feed(sl.data(), sl.size());

  std::vector<float> b;
  float lq = 0, ll = 0;
  quiet.compute(b, lq);
  loud.compute(b, ll);
  EXPECT_LT(lq, ll);
}

// Feeding in small sub-window chunks must accumulate into a full window.
TEST(SpectrumAnalyzer, SlidingFeedAccumulates)
{
  SpectrumAnalyzer a(RATE, NFFT, BANDS);
  auto s = sine(2000, 0.6f, NFFT);
  for (std::size_t off = 0; off < s.size(); off += 100)
    {
      const std::size_t n = std::min<std::size_t>(100, s.size() - off);
      a.feed(s.data() + off, n);
    }

  std::vector<float> bands;
  float level = 0;
  ASSERT_TRUE(a.compute(bands, level));
  EXPECT_NEAR(peak_band(bands), expected_band(2000), 1);
}

// A feed longer than the window keeps only the most recent samples.
TEST(SpectrumAnalyzer, FeedLargerThanWindowUsesTail)
{
  SpectrumAnalyzer a(RATE, NFFT, BANDS);
  auto lo = sine(500, 0.6f, RATE / 10); // long quiet-frequency lead-in
  auto hi = sine(4000, 0.6f, NFFT);     // recent tail = a full window of 4 kHz
  std::vector<float> buf;
  buf.insert(buf.end(), lo.begin(), lo.end());
  buf.insert(buf.end(), hi.begin(), hi.end());
  a.feed(buf.data(), buf.size());

  std::vector<float> bands;
  float level = 0;
  ASSERT_TRUE(a.compute(bands, level));
  EXPECT_NEAR(peak_band(bands), expected_band(4000), 1); // tail dominates
}

TEST(SpectrumAnalyzer, BandCountConfigurable)
{
  SpectrumAnalyzer a16(RATE, NFFT, 16);
  SpectrumAnalyzer a64(RATE, 1024, 64);
  EXPECT_EQ(a16.band_count(), 16u);
  EXPECT_EQ(a64.band_count(), 64u);

  auto s = sine(1000, 0.5f, 1024);
  a64.feed(s.data(), s.size());
  std::vector<float> bands;
  float level = 0;
  EXPECT_TRUE(a64.compute(bands, level));
  EXPECT_EQ(bands.size(), 64u);
}

TEST(SpectrumAnalyzer, SilenceNeverProducesNaN)
{
  SpectrumAnalyzer a(RATE, NFFT, BANDS);
  std::vector<float> z(NFFT, 0.0f);
  a.feed(z.data(), z.size());

  std::vector<float> bands;
  float level = 0;
  ASSERT_TRUE(a.compute(bands, level));
  for (float v : bands)
    EXPECT_FALSE(std::isnan(v));
  EXPECT_FALSE(std::isnan(level));
}
