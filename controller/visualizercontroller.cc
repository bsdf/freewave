#include "controller/visualizercontroller.hh"

#include "vizlib/pipewirecapture.hh"
#include "vizlib/spectrumanalyzer.hh"

#include <ranges>

namespace {
constexpr std::size_t FFT_SIZE = 2048;
constexpr std::size_t BAND_COUNT = 64; // log-binned; mids/highs gain real detail,
                                       // bass stays FFT-bin-limited (23 Hz @ 2048)
constexpr int FRAME_MS = 16;           // ~60 fps
} // namespace

VisualizerController::VisualizerController(QObject *parent)
  : QObject(parent)
  , capture(std::make_unique<viz::PipeWireCapture>())
  , analyzer(std::make_unique<viz::SpectrumAnalyzer>(
        viz::PipeWireCapture::RATE, FFT_SIZE, BAND_COUNT))
  , drain(4096)
{
  timer.setInterval(FRAME_MS);
  connect(&timer, &QTimer::timeout, this, &VisualizerController::tick);
}

VisualizerController::~VisualizerController() = default;

auto
VisualizerController::band_count() const -> int
{
  return static_cast<int>(analyzer->band_count());
}

auto
VisualizerController::set_source(Source s) -> void
{
  if (s == source)
    return;
  if (active)
    set_active(false); // tear down the current source; caller re-arms
  source = s;
}

auto
VisualizerController::set_active(bool on) -> void
{
  if (on == active)
    return;

  if (on)
    {
      // PipeWire taps the system monitor itself; External is fed via feed_pcm,
      // so there's no capture to start — just run the analysis timer.
      if (source == Source::PipeWire && !capture->start())
        return; // PipeWire unavailable — stay dark
      active = true;
      timer.start();
    }
  else
    {
      timer.stop();
      if (source == Source::PipeWire)
        capture->stop();
      active = false;
      // Settle the scene to a flat baseline rather than freezing the last frame.
      emit bands_ready(QList<qreal>(band_count(), 0.0));
      emit level_ready(0.0);
    }
}

auto
VisualizerController::feed_pcm(const QList<float> &samples) -> void
{
  if (!active || source != Source::External || samples.isEmpty())
    return;
  // QList<float> is contiguous (Qt6), so feed its storage straight in.
  analyzer->feed(samples.constData(), static_cast<std::size_t>(samples.size()));
}

auto
VisualizerController::tick() -> void
{
  // PipeWire mode pulls from the capture ring here; External mode's PCM has
  // already been pushed in via feed_pcm. Either way, analyze the current window.
  if (source == Source::PipeWire)
    {
      // Drain everything captured since the last frame into the sliding window,
      // so analysis tracks live audio without latency building up in the ring.
      std::size_t avail = capture->available();
      while (avail > 0)
        {
          const std::size_t got = capture->read(drain.data(),
              std::min(avail, drain.size()));
          if (got == 0)
            break;
          analyzer->feed(drain.data(), got);
          avail -= got;
        }
    }

  float level = 0.0f;
  if (!analyzer->compute(bands_scratch, level))
    return;

  auto out = bands_scratch | std::ranges::to<QList<qreal>>();

  emit bands_ready(out);
  emit level_ready(level);
}
