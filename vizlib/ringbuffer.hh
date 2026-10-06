#ifndef VIZLIB_RINGBUFFER_HH
#define VIZLIB_RINGBUFFER_HH

#include <atomic>
#include <cstddef>
#include <vector>

namespace viz {

// Single-producer / single-consumer lock-free float ring buffer. The producer is
// PipeWire's realtime thread (one writer); the consumer is the analyzer on the
// GUI thread (one reader). On overrun the producer drops the *newest* samples
// (write returns short) rather than blocking — a visualizer tolerates dropped
// frames, and the audio thread must never wait. Sized for ~1 s by default and
// drained every frame, so occupancy normally stays around one PipeWire quantum.
class RingBuffer {
public:
  explicit RingBuffer(std::size_t capacity)
    : buf(capacity + 1) // one reserved slot distinguishes full from empty
  {
  }

  // Producer side. Returns the number of samples actually written.
  auto write(const float *src, std::size_t n) -> std::size_t
  {
    const std::size_t cap = buf.size();
    std::size_t head = wr.load(std::memory_order_relaxed);
    const std::size_t tail = rd.load(std::memory_order_acquire);
    std::size_t written = 0;
    for (; written < n; ++written)
      {
        const std::size_t next = head + 1 == cap ? 0 : head + 1;
        if (next == tail)
          break; // full
        buf[head] = src[written];
        head = next;
      }
    wr.store(head, std::memory_order_release);
    return written;
  }

  // Consumer side. Returns the number of samples actually read.
  auto read(float *dst, std::size_t n) -> std::size_t
  {
    const std::size_t cap = buf.size();
    std::size_t tail = rd.load(std::memory_order_relaxed);
    const std::size_t head = wr.load(std::memory_order_acquire);
    std::size_t got = 0;
    for (; got < n; ++got)
      {
        if (tail == head)
          break; // empty
        dst[got] = buf[tail];
        tail = tail + 1 == cap ? 0 : tail + 1;
      }
    rd.store(tail, std::memory_order_release);
    return got;
  }

  auto available() const -> std::size_t
  {
    const std::size_t cap = buf.size();
    const std::size_t head = wr.load(std::memory_order_acquire);
    const std::size_t tail = rd.load(std::memory_order_acquire);
    return head >= tail ? head - tail : cap - tail + head;
  }

private:
  std::vector<float> buf;
  std::atomic<std::size_t> wr{0};
  std::atomic<std::size_t> rd{0};
};

} // namespace viz

#endif // VIZLIB_RINGBUFFER_HH
