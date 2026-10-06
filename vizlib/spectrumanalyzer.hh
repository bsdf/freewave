#ifndef VIZLIB_SPECTRUMANALYZER_HH
#define VIZLIB_SPECTRUMANALYZER_HH

#include <cstddef>
#include <cstdint>
#include <vector>

struct kiss_fftr_state;
using kiss_fftr_cfg = kiss_fftr_state *;

namespace viz {

// Turns a sliding window of mono PCM into normalized spectrum bands. Own DSP:
// Hann window → real FFT (KissFFT) → log-frequency binning → dB mapping →
// gravity smoothing (fast attack, slow fall). No Qt. The mapping constants in
// the .cc are the tuning surface for how the visualizer looks.
class SpectrumAnalyzer {
public:
  SpectrumAnalyzer(std::uint32_t sample_rate, std::size_t fft_size, std::size_t band_count);
  ~SpectrumAnalyzer();

  SpectrumAnalyzer(const SpectrumAnalyzer &) = delete;
  auto operator=(const SpectrumAnalyzer &) -> SpectrumAnalyzer & = delete;

  // Append mono samples to the sliding analysis window (keeps the most recent
  // fft_size samples).
  auto feed(const float *samples, std::size_t n) -> void;

  // Run the FFT on the current window. Fills `bands` (band_count values in
  // 0..1, gravity-smoothed) and `level` (overall 0..1). Returns false until the
  // first full window has accumulated.
  auto compute(std::vector<float> &bands, float &level) -> bool;

  auto band_count() const -> std::size_t { return bands_n; }

private:
  std::uint32_t rate;
  std::size_t nfft;
  std::size_t bands_n;
  std::size_t filled = 0; // accumulated samples, capped at nfft

  std::vector<float> window;   // sliding PCM, size nfft
  std::vector<float> hann;     // precomputed window function
  std::vector<float> smoothed; // previous band values (gravity state)
  std::vector<float> tilt;     // per-band dB offset (spectral tilt, lifts highs)
  std::vector<int> band_lo;    // first FFT bin per band
  std::vector<int> band_hi;    // last FFT bin per band (inclusive)

  kiss_fftr_cfg cfg = nullptr;
  std::vector<float> fft_in;  // nfft windowed samples
  std::vector<float> fft_out; // 2*(nfft/2+1) floats, viewed as kiss_fft_cpx
};

} // namespace viz

#endif // VIZLIB_SPECTRUMANALYZER_HH
