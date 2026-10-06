#ifndef VIZLIB_PIPEWIRECAPTURE_HH
#define VIZLIB_PIPEWIRECAPTURE_HH

#include <cstddef>
#include <cstdint>

#include "vizlib/ringbuffer.hh"

struct pw_thread_loop;
struct pw_stream;

namespace viz {

// Captures the default PipeWire sink's monitor (via PW_KEY_STREAM_CAPTURE_SINK),
// downmixed to mono float at a fixed rate, into the lock-free ring buffer. The
// PipeWire realtime thread is the sole producer; drain with read() from one
// consumer thread. Knows nothing about Qt or Freewave.
//
// A default-sink tap captures *all* system audio, so callers must gate start()/
// stop() on "the visualizer is actually on screen and playing" — see
// VisualizerController.
class PipeWireCapture {
public:
  static constexpr std::uint32_t RATE = 48000;

  explicit PipeWireCapture(std::size_t ring_capacity = RATE); // ~1 s
  ~PipeWireCapture();

  PipeWireCapture(const PipeWireCapture &) = delete;
  auto operator=(const PipeWireCapture &) -> PipeWireCapture & = delete;

  // Spin up the capture thread + stream. Returns false if PipeWire is
  // unavailable or the stream could not connect (the visualizer stays dark).
  auto start() -> bool;
  auto stop() -> void;
  auto running() const -> bool { return active; }

  // Consumer side: drain up to n mono samples; returns the count read.
  auto read(float *dst, std::size_t n) -> std::size_t { return ring.read(dst, n); }
  auto available() const -> std::size_t { return ring.available(); }

private:
  static void on_process(void *userdata);

  RingBuffer ring;
  pw_thread_loop *loop = nullptr;
  pw_stream *stream = nullptr;
  bool active = false;
  bool pw_inited = false;
};

} // namespace viz

#endif // VIZLIB_PIPEWIRECAPTURE_HH
