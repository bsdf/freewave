#include "vizlib/pipewirecapture.hh"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include <array>

namespace viz {

PipeWireCapture::PipeWireCapture(std::size_t ring_capacity)
  : ring(ring_capacity)
{
}

PipeWireCapture::~PipeWireCapture()
{
  stop();
  if (pw_inited)
    pw_deinit();
}

void
PipeWireCapture::on_process(void *userdata)
{
  auto *self = static_cast<PipeWireCapture *>(userdata);
  pw_buffer *b = pw_stream_dequeue_buffer(self->stream);
  if (!b)
    return;

  spa_buffer *buf = b->buffer;
  spa_data &d = buf->datas[0];
  if (d.data && d.chunk->size > 0)
    {
      const auto *samples = reinterpret_cast<const float *>(
          static_cast<const std::uint8_t *>(d.data) + d.chunk->offset);
      const std::size_t n = d.chunk->size / sizeof(float);
      self->ring.write(samples, n); // wait-free; drops on overrun
    }

  pw_stream_queue_buffer(self->stream, b);
}

auto
PipeWireCapture::start() -> bool
{
  if (active)
    return true;

  if (!pw_inited)
    {
      pw_init(nullptr, nullptr);
      pw_inited = true;
    }

  loop = pw_thread_loop_new("freewave-viz", nullptr);
  if (!loop)
    return false;

  static const pw_stream_events events = {
      .version = PW_VERSION_STREAM_EVENTS,
      .process = on_process,
  };

  auto *props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio",
      PW_KEY_MEDIA_CATEGORY, "Capture",
      PW_KEY_MEDIA_ROLE, "Music",
      PW_KEY_STREAM_CAPTURE_SINK, "true", // tap the default sink's monitor
      PW_KEY_NODE_NAME, "freewave-visualizer",
      nullptr);

  stream = pw_stream_new_simple(
      pw_thread_loop_get_loop(loop), "freewave-visualizer", props, &events, this);
  if (!stream)
    {
      stop();
      return false;
    }

  // Fixed mono F32 at RATE — PipeWire converts/downmixes for us, so the
  // analyzer always sees one known format.
  std::array<std::uint8_t, 1024> pod_buf;
  spa_pod_builder pb = SPA_POD_BUILDER_INIT(pod_buf.data(), pod_buf.size());
  spa_audio_info_raw info = {};
  info.format = SPA_AUDIO_FORMAT_F32;
  info.rate = RATE;
  info.channels = 1;
  const spa_pod *params[1] = {
      spa_format_audio_raw_build(&pb, SPA_PARAM_EnumFormat, &info),
  };

  const int res = pw_stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY,
      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT
                                   | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
      params, 1);
  if (res < 0)
    {
      stop();
      return false;
    }

  if (pw_thread_loop_start(loop) < 0)
    {
      stop();
      return false;
    }

  active = true;
  return true;
}

auto
PipeWireCapture::stop() -> void
{
  if (loop)
    pw_thread_loop_stop(loop);
  if (stream)
    {
      pw_stream_destroy(stream);
      stream = nullptr;
    }
  if (loop)
    {
      pw_thread_loop_destroy(loop);
      loop = nullptr;
    }
  active = false;
}

} // namespace viz
