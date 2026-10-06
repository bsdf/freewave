#include "vizlib/spectrumanalyzer.hh"

#include <kiss_fftr.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace viz {

namespace {
// ── Tuning surface ──────────────────────────────────────────────────────────
// Frequency span the bands cover, log-distributed.
constexpr float FREQ_MIN = 35.0f;
constexpr float FREQ_MAX = 17000.0f;
// Magnitude → display mapping, in dB after window normalization. Everything at
// or below DB_FLOOR reads as silence; DB_CEIL saturates to a full bar.
constexpr float DB_FLOOR = -62.0f;
constexpr float DB_CEIL = -8.0f;
// Spectral tilt: music energy falls ~3-4.5 dB/octave, so without compensation
// the bass bands tower and the treble bands flat-line. A symmetric tilt about
// the band range's geometric centre cuts lows and lifts highs by this much per
// octave — bass still reads, treble comes alive. 0 disables.
constexpr float SLOPE_DB_PER_OCT = 3.0f;
// Gravity: bars jump up instantly, then fall by at most this much per frame.
constexpr float GRAVITY = 0.05f;
// Overall-level gain applied to window RMS before clamping to 0..1.
constexpr float LEVEL_GAIN = 4.0f;
} // namespace

SpectrumAnalyzer::SpectrumAnalyzer(
    std::uint32_t sample_rate, std::size_t fft_size, std::size_t band_count)
  : rate(sample_rate)
  , nfft(fft_size)
  , bands_n(band_count)
  , window(fft_size, 0.0f)
  , hann(fft_size)
  , smoothed(band_count, 0.0f)
  , tilt(band_count, 0.0f)
  , band_lo(band_count)
  , band_hi(band_count)
  , fft_in(fft_size, 0.0f)
  , fft_out(2 * (fft_size / 2 + 1), 0.0f)
{
  cfg = kiss_fftr_alloc(static_cast<int>(nfft), 0, nullptr, nullptr);

  for (std::size_t i = 0; i < nfft; ++i)
    hann[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * i / (nfft - 1)));

  // Log-spaced band edges → FFT bin ranges. Adjacent bands share no bins; a
  // band that would be empty (low frequencies, coarse bin resolution) is given
  // its single nearest bin so it still responds.
  const int max_bin = static_cast<int>(nfft / 2);
  const float ratio = FREQ_MAX / FREQ_MIN;
  const float f_pivot = FREQ_MIN * std::sqrt(ratio); // geometric centre of the range
  for (std::size_t b = 0; b < bands_n; ++b)
    {
      const float f_lo = FREQ_MIN * std::pow(ratio, float(b) / bands_n);
      const float f_hi = FREQ_MIN * std::pow(ratio, float(b + 1) / bands_n);
      int lo = static_cast<int>(std::floor(f_lo * nfft / rate));
      int hi = static_cast<int>(std::ceil(f_hi * nfft / rate));
      lo = std::clamp(lo, 1, max_bin);
      hi = std::clamp(hi, 1, max_bin);
      if (hi < lo)
        hi = lo;
      band_lo[b] = lo;
      band_hi[b] = hi;

      const float f_centre = std::sqrt(f_lo * f_hi);
      tilt[b] = SLOPE_DB_PER_OCT * std::log2(f_centre / f_pivot);
    }
}

SpectrumAnalyzer::~SpectrumAnalyzer()
{
  if (cfg)
    kiss_fftr_free(cfg);
}

auto
SpectrumAnalyzer::feed(const float *samples, std::size_t n) -> void
{
  if (n >= nfft)
    {
      std::memcpy(window.data(), samples + (n - nfft), nfft * sizeof(float));
      filled = nfft;
      return;
    }
  // Slide the existing tail left and append the newcomers.
  const std::size_t keep = nfft - n;
  std::memmove(window.data(), window.data() + n, keep * sizeof(float));
  std::memcpy(window.data() + keep, samples, n * sizeof(float));
  filled = std::min(nfft, filled + n);
}

auto
SpectrumAnalyzer::compute(std::vector<float> &bands, float &level) -> bool
{
  if (filled < nfft || !cfg)
    return false;

  double sq = 0.0;
  for (std::size_t i = 0; i < nfft; ++i)
    {
      fft_in[i] = window[i] * hann[i];
      sq += double(window[i]) * window[i];
    }

  kiss_fftr(cfg, fft_in.data(), reinterpret_cast<kiss_fft_cpx *>(fft_out.data()));

  // Hann coherent gain is 0.5, so a full-scale bin peaks near nfft/4; normalize
  // so the dB window constants are independent of the FFT size.
  const float inv = 2.0f / nfft;

  bands.resize(bands_n);
  for (std::size_t b = 0; b < bands_n; ++b)
    {
      float mag = 0.0f;
      for (int bin = band_lo[b]; bin <= band_hi[b]; ++bin)
        {
          const float re = fft_out[2 * bin];
          const float im = fft_out[2 * bin + 1];
          mag = std::max(mag, std::sqrt(re * re + im * im));
        }
      const float db = 20.0f * std::log10(mag * inv + 1e-9f) + tilt[b];
      const float d = std::clamp((db - DB_FLOOR) / (DB_CEIL - DB_FLOOR), 0.0f, 1.0f);

      const float prev = smoothed[b];
      const float out = d >= prev ? d : std::max(d, prev - GRAVITY);
      smoothed[b] = out;
      bands[b] = out;
    }

  const float rms = std::sqrt(static_cast<float>(sq / nfft));
  level = std::clamp(rms * LEVEL_GAIN, 0.0f, 1.0f);
  return true;
}

} // namespace viz
