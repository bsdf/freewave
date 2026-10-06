#include "gstaudioengine.hh"

#include <mutex>

#include <QList>
#include <QSet>
#include <QUrlQuery>

#include <gst/audio/streamvolume.h>

// playbin's GstPlayFlags enum is not exposed in a public header; DOWNLOAD is
// bit 7. Enabling it turns on on-disk download buffering of the encoded source.
static constexpr gint GST_PLAY_FLAG_DOWNLOAD = (1 << 7);

void
gst_about_to_finish_cb(GstElement *, gpointer data)
{
  static_cast<GstAudioEngine *>(data)->on_about_to_finish();
}

void
gst_source_setup_cb(GstElement *, GstElement *source, gpointer data)
{
  static_cast<GstAudioEngine *>(data)->on_source_setup(source);
}

gboolean
gst_bus_watch_cb(GstBus *, GstMessage *msg, gpointer data)
{
  static_cast<GstAudioEngine *>(data)->handle_bus_message(msg);
  return G_SOURCE_CONTINUE;
}

#ifdef ENABLE_VISUALIZER
static void
gst_viz_handoff_cb(GstElement *, GstBuffer *buffer, GstPad *, gpointer data)
{
  static_cast<GstAudioEngine *>(data)->on_viz_handoff(buffer);
}
#endif

// Initialise GStreamer exactly once for the process. Safe to call from any
// engine constructor (including direct construction in tests, which have no
// gst_init in their main). Uses gst_init_check so a failure is logged rather
// than aborting, and no longer swaps the global GLib log handler.
static void
ensure_gst_init()
{
  static std::once_flag flag;
  std::call_once(flag, [] {
    GError *err = nullptr;
    if (!gst_init_check(nullptr, nullptr, &err))
      spdlog::error("GstAudioEngine: gst_init failed: {}",
          err ? err->message : "unknown");
    g_clear_error(&err);
  });
}

// Stream URLs carry Subsonic/LMS auth in the query string (apiKey, or the
// salted token/salt pair) — strip it before this URL goes anywhere near a log
// line. Other query params (id, format, ...) are kept for debuggability.
static auto
redact_stream_url(const QUrl &url) -> QString
{
  static const QSet<QString> sensitive_keys = {"apiKey", "u", "t", "s", "p", "password", "token", "salt"};

  QUrl redacted = url;
  QUrlQuery query(url);
  auto items = query.queryItems();
  for (auto &item : items)
    if (sensitive_keys.contains(item.first))
      item.second = "REDACTED";
  query.setQueryItems(items);
  redacted.setQuery(query);
  return redacted.toString();
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

GstAudioEngine::GstAudioEngine(QObject *parent)
  : AudioEngine{parent}
{
  ensure_gst_init();
  setup_pipeline();

  clock_timer = new QTimer(this);
  clock_timer->setInterval(1000);
  QObject::connect(clock_timer, &QTimer::timeout, this, [this] {
    auto prev_dur = last_dur_ms;
    update_position_cache();
    if (last_dur_ms != prev_dur)
      emit duration_changed(last_dur_ms);
    emit position_changed(last_pos_ms);
  });

  seek_timer = new QTimer(this);
  seek_timer->setSingleShot(false);
  seek_timer->setInterval(50);
  QObject::connect(seek_timer, &QTimer::timeout, this, &GstAudioEngine::do_seek_now);

  seek_backstop_timer = new QTimer(this);
  seek_backstop_timer->setSingleShot(true);
  seek_backstop_timer->setInterval(seek_backstop_ms);
  QObject::connect(seek_backstop_timer, &QTimer::timeout,
      this, &GstAudioEngine::on_seek_backstop);
}

GstAudioEngine::~GstAudioEngine()
{
  clock_timer->stop();
  seek_timer->stop();

  // Remove before the pipeline dies so the callback can't fire into it.
  if (bus_watch_id)
    {
      g_source_remove(bus_watch_id);
      bus_watch_id = 0;
    }

  if (pipeline)
    {
      g_signal_handlers_disconnect_by_func(
          pipeline, reinterpret_cast<gpointer>(gst_about_to_finish_cb), this);
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
      pipeline = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Pipeline setup
// ---------------------------------------------------------------------------

void
GstAudioEngine::on_source_setup(GstElement *source)
{
  if (!allow_untrusted_tls || !source)
    return;

  // source-setup fires for every source playbin plugs, most of which have no TLS
  // at all (file://, and the download buffer's own reads) — setting a property
  // they don't have is a GLib warning.
  if (!g_object_class_find_property(G_OBJECT_GET_CLASS(source), "ssl-strict"))
    return;

  // The user pinned this server's certificate at the API layer, but GnuTLS keeps
  // its own trust store and never sees that pin, so it can still reject a cert
  // the app already accepted. There is no way to hand it the pinned cert: a leaf
  // server certificate is not a CA and cannot be a trust anchor.
  g_object_set(source, "ssl-strict", FALSE, nullptr);
  spdlog::debug("GstAudioEngine: stream TLS verification off (certificate pinned by user)");
}

auto
GstAudioEngine::setup_pipeline() -> void
{
  pipeline = gst_element_factory_make("playbin", "player");
  if (!pipeline)
    {
      spdlog::error("GstAudioEngine: failed to create playbin");
      return;
    }

  g_signal_connect(pipeline, "about-to-finish",
      G_CALLBACK(gst_about_to_finish_cb), this);
  g_signal_connect(pipeline, "source-setup",
      G_CALLBACK(gst_source_setup_cb), this);

  // Buffer the *encoded* source to a temp file (GST_PLAY_FLAG_DOWNLOAD): an
  // in-range seek reads from disk instead of issuing a fresh HTTP range request,
  // so it lands instantly and never trips the intermittent souphttpsrc -5. We use
  // playbin's default sink (autoaudiosink + auto-plugged conversion) as-is;
  // volume still goes through playbin's GstStreamVolume interface. This replaces
  // the old decoded-stream queue2 in a custom sink bin, which only re-buffered
  // *after* the network round-trip.
  gint play_flags = 0;
  g_object_get(pipeline, "flags", &play_flags, nullptr);
  play_flags |= GST_PLAY_FLAG_DOWNLOAD;
  g_object_set(pipeline, "flags", play_flags, nullptr);

#ifdef ENABLE_VISUALIZER
  // Replace playbin's default sink with a tee'd bin so the visualizer can read
  // freewave's *own* decoded output (not a system-wide PipeWire monitor tap).
  // On failure we leave playbin's default sink in place — playback is unaffected.
  if (GstElement *tap_sink = build_viz_tap_sink())
    g_object_set(pipeline, "audio-sink", tap_sink, nullptr);
#endif

  GstBus *bus = gst_element_get_bus(pipeline);
  bus_watch_id = gst_bus_add_watch(bus, gst_bus_watch_cb, this);
  gst_object_unref(bus); // the watch holds its own ref
}

auto
GstAudioEngine::set_viz_pcm_enabled(bool on) -> void
{
#ifdef ENABLE_VISUALIZER
  viz_tap_enabled.store(on, std::memory_order_relaxed);
#else
  Q_UNUSED(on);
#endif
}

#ifdef ENABLE_VISUALIZER
auto
GstAudioEngine::build_viz_tap_sink() -> GstElement *
{
  GstElement *bin = gst_bin_new("viz-sink-bin");
  GstElement *tee = gst_element_factory_make("tee", nullptr);
  GstElement *play_queue = gst_element_factory_make("queue", "viz-play-queue");
  GstElement *real_sink = gst_element_factory_make("autoaudiosink", nullptr);
  GstElement *tap_queue = gst_element_factory_make("queue", "viz-tap-queue");
  GstElement *convert = gst_element_factory_make("audioconvert", nullptr);
  GstElement *resample = gst_element_factory_make("audioresample", nullptr);
  GstElement *capsfilter = gst_element_factory_make("capsfilter", nullptr);
  GstElement *tap_sink = gst_element_factory_make("fakesink", nullptr);

  GstElement *parts[] = {tee, play_queue, real_sink, tap_queue, convert,
      resample, capsfilter, tap_sink};
  bool all = bin != nullptr;
  for (GstElement *e : parts)
    all = all && e != nullptr;
  if (!all)
    {
      spdlog::warn("GstAudioEngine: viz tap elements unavailable; default sink");
      for (GstElement *e : parts)
        if (e)
          gst_object_unref(e);
      if (bin)
        gst_object_unref(bin);
      return nullptr;
    }

  GstCaps *caps = gst_caps_new_simple("audio/x-raw",
      "format", G_TYPE_STRING, "F32LE",
      "channels", G_TYPE_INT, 1,
      "rate", G_TYPE_INT, VIZ_TAP_RATE,
      "layout", G_TYPE_STRING, "interleaved",
      nullptr);
  g_object_set(capsfilter, "caps", caps, nullptr);
  gst_caps_unref(caps);

  // The tap must never back-pressure the real audio path: drop old buffers and
  // don't clock-sync, so a slow/idle consumer can't stall the tee.
  g_object_set(tap_queue, "leaky", 2 /* downstream */, "max-size-buffers", 8,
      "max-size-bytes", 0, "max-size-time", G_GUINT64_CONSTANT(0), nullptr);
  g_object_set(tap_sink, "sync", FALSE, "async", FALSE, "signal-handoffs", TRUE,
      "enable-last-sample", FALSE, nullptr);

  gst_bin_add_many(GST_BIN(bin), tee, play_queue, real_sink, tap_queue, convert,
      resample, capsfilter, tap_sink, nullptr);

  if (!gst_element_link(tee, play_queue)
      || !gst_element_link(play_queue, real_sink)
      || !gst_element_link_many(tee, tap_queue, convert, resample, capsfilter,
          tap_sink, nullptr))
    {
      spdlog::warn("GstAudioEngine: viz tap link failed; default sink");
      gst_object_unref(bin); // owns all added elements
      return nullptr;
    }

  GstPad *tee_sink = gst_element_get_static_pad(tee, "sink");
  gst_element_add_pad(bin, gst_ghost_pad_new("sink", tee_sink));
  gst_object_unref(tee_sink);

  g_signal_connect(tap_sink, "handoff", G_CALLBACK(gst_viz_handoff_cb), this);
  return bin;
}

auto
GstAudioEngine::on_viz_handoff(GstBuffer *buffer) -> void
{
  if (!viz_tap_enabled.load(std::memory_order_relaxed))
    return;

  GstMapInfo info;
  if (!gst_buffer_map(buffer, &info, GST_MAP_READ))
    return;
  const auto *samples = reinterpret_cast<const float *>(info.data);
  const int n = static_cast<int>(info.size / sizeof(float));
  QList<float> frame;
  frame.reserve(n);
  for (int i = 0; i < n; ++i)
    frame.append(samples[i]);
  gst_buffer_unmap(buffer, &info);

  // Handoff runs on the fakesink's streaming thread; viz_pcm consumers live on
  // the engine's (GUI) thread, so hop there before emitting.
  QMetaObject::invokeMethod(
      this, [this, frame = std::move(frame)]() { emit viz_pcm(frame); },
      Qt::QueuedConnection);
}
#endif // ENABLE_VISUALIZER

// playbin implements the GstStreamVolume interface, so apply the perceptual
// (CUBIC) slider value straight through it — no manual cubic→linear conversion.
auto
GstAudioEngine::apply_volume() -> void
{
  if (!pipeline) return;
  gst_stream_volume_set_volume(GST_STREAM_VOLUME(pipeline),
      GST_STREAM_VOLUME_FORMAT_CUBIC, static_cast<gdouble>(vol) / 100.0);
}

// ---------------------------------------------------------------------------
// Playback control
// ---------------------------------------------------------------------------

auto
GstAudioEngine::play(const QUrl &url, uint32_t start_ms) -> void
{
  if (!pipeline) return;

  spdlog::debug("GstAudioEngine: play({}, start={} ms)", redact_stream_url(url), start_ms);

  // A new track chosen by the backend: this is a fresh playback, so reset the
  // recovery bookkeeping (see start_stream for the recovery path itself).
  current_url = url;
  track_started = false;
  seek_recovery_attempts = 0;
  last_error_pos_ms = 0;

  start_stream(url, start_ms);
}

// Bring the pipeline up on `url` at `start_ms`. Shared by play() (new track) and
// the in-place error recovery (same url, current position) — recovery reuses
// this without resetting the recovery counters.
auto
GstAudioEngine::start_stream(const QUrl &url, uint32_t start_ms) -> void
{
  spdlog::debug("GstAudioEngine: start_stream({}, start={} ms)",
      redact_stream_url(url), start_ms);

  playing = true;
  set_busy(Busy::Preroll);
  pending_seek_ms = start_ms;
  // A seek queued against the stream being torn down here must not survive into
  // the new one: its target is a position in a pipeline that no longer exists.
  seek_waiting = false;
  seek_timer->stop();
  restore_play_pending = false;
  restoring = false;
  last_pos_ms = start_ms;
  last_dur_ms = 0;
  // Cancel any gapless commit from about-to-finish: the next STREAM_START
  // belongs to this track, not a queue advance.
  gapless_active.store(false, std::memory_order_release);

  gst_element_set_state(pipeline, GST_STATE_NULL);
  // Record the teardown without announcing it. The pipeline really is stopped
  // here, so the new stream's PLAYING is a genuine change rather than a repeat
  // of the outgoing track's — but nothing needs telling, because the intent has
  // not changed and Preroll already says a stream is opening.
  current_state = State::Stopped;
  g_object_set(pipeline, "uri", url.toString().toUtf8().constData(), nullptr);
  apply_volume();

  if (pending_seek_ms > 0)
    gst_element_set_state(pipeline, GST_STATE_PAUSED);
  else
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
}

auto
GstAudioEngine::pause() -> void
{
  if (!pipeline || !playing) return;
  playing = false;
  clock_timer->stop();
  gst_element_set_state(pipeline, GST_STATE_PAUSED);
  // A pause that lands mid-flight draws no bus transition at all — the pipeline
  // is already PAUSED — so without this the user's pause goes unacknowledged for
  // as long as the seek runs. When nothing is in flight the bus reports it.
  if (busy_now != Busy::None)
    emit state_changed(State::Paused);
}

auto
GstAudioEngine::resume() -> void
{
  if (!pipeline || playing) return;
  playing = true;
  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  if (busy_now != Busy::None)
    emit state_changed(State::Playing);
}

auto
GstAudioEngine::stop() -> void
{
  if (!pipeline) return;
  playing = false;
  pending_seek_ms = 0;
  restore_play_pending = false;
  restoring = false;
  track_started = false;
  seek_recovery_attempts = 0;
  seek_waiting = false;
  gapless_active.store(false, std::memory_order_release);
  clock_timer->stop();
  seek_timer->stop();
  gst_element_set_state(pipeline, GST_STATE_NULL);
  set_busy(Busy::None);
  // Announced unconditionally: stop is a terminal user action, and a caller that
  // asked for it is owed the confirmation whether or not anything was playing.
  current_state = State::Stopped;
  emit state_changed(State::Stopped);
}

// ---------------------------------------------------------------------------
// Seek
// Store the latest target and let the 50 ms timer issue it when the
// pipeline is stable. Only one seek is ever in-flight at a time.
// ---------------------------------------------------------------------------

auto
GstAudioEngine::seek(uint32_t ms) -> void
{
  if (!pipeline) return;
  seek_target_ms = ms;
  last_pos_ms = ms;
  seek_waiting = true;
  // Busy from the moment the user asks, not from when the pipeline accepts:
  // the wait for a stable pipeline is part of what they are waiting through,
  // and it has been measured at five seconds on its own.
  set_busy(Busy::Seek);
  // A fresh user seek is a new wait, not a continuation of a stuck one.
  backstop_target_ms = ms;
  seek_backstop_attempts = 0;
  seek_backstop_timer->start();
  if (!seek_timer->isActive())
    seek_timer->start();
}

auto
GstAudioEngine::do_seek_now() -> void
{
  if (!seek_waiting || !pipeline) return;

  GstState cur = GST_STATE_VOID_PENDING;
  GstState pending = GST_STATE_VOID_PENDING;
  gst_element_get_state(pipeline, &cur, &pending, 0);
  if (pending != GST_STATE_VOID_PENDING)
    {
      spdlog::trace("GstAudioEngine: seek to {} ms held off (state {}, pending {})",
          seek_target_ms, gst_element_state_get_name(cur),
          gst_element_state_get_name(pending));
      return;
    }

  seek_waiting = false;
  seek_timer->stop();

  // Accurate (FLUSH-only) seek: lands exactly at the target, as Strawberry and
  // GstPlay do. KEY_UNIT|SNAP_NEAREST was briefly used here on the theory that an
  // accurate mid-frame seek tripped gst_base_parse's "Internal data stream error"
  // over HTTP, but a live-LMS reproduction disproved that (the souphttpsrc -5 is
  // an intermittent fault that fires under FLUSH-only and KEY_UNIT alike) — so
  // KEY_UNIT bought no robustness and only added a visible backward keyframe snap.
  spdlog::debug("GstAudioEngine: seek to {} ms (flush) from state {}",
      seek_target_ms, gst_element_state_get_name(cur));
  clock_timer->stop(); // the position is about to be fiction until ASYNC_DONE
  if (!gst_element_seek_simple(pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
          static_cast<gint64>(seek_target_ms) * GST_MSECOND))
    {
      // Refused, not slow: nothing will settle and no ASYNC_DONE is coming, so
      // leaving Busy set would strand the UI on "seeking" indefinitely.
      spdlog::warn("GstAudioEngine: pipeline refused the seek to {} ms (state {})",
          seek_target_ms, gst_element_state_get_name(cur));
      update_position_cache();
      set_busy(Busy::None);
      if (playing)
        clock_timer->start();
      emit position_changed(last_pos_ms);
    }
}

// A seek that has not settled after seek_backstop_ms: cold remote seeks run
// 10-15 s legitimately, so this is a backstop for a wedged pipeline, not the
// primary fix for slowness. Re-establishes the stream at the target position;
// gives up after max_seek_backstop_attempts rather than retrying forever.
auto
GstAudioEngine::on_seek_backstop() -> void
{
  if (busy_now != Busy::Seek || !pipeline) return;

  if (current_url.isEmpty() || ++seek_backstop_attempts >= max_seek_backstop_attempts)
    {
      spdlog::error("GstAudioEngine: seek to {} ms did not settle after {} "
                    "attempt(s); giving up",
          backstop_target_ms, seek_backstop_attempts);
      set_busy(Busy::None);
      emit error(QStringLiteral("Seek timed out"));
      return;
    }

  spdlog::warn("GstAudioEngine: seek to {} ms did not settle within {} ms "
               "(attempt {}/{}); re-establishing the stream",
      backstop_target_ms, seek_backstop_ms, seek_backstop_attempts,
      max_seek_backstop_attempts);
  start_stream(current_url, backstop_target_ms);
}

auto
GstAudioEngine::set_volume(int v) -> void
{
  vol = v;
  apply_volume();
}

// ---------------------------------------------------------------------------
// Gapless
// ---------------------------------------------------------------------------

auto
GstAudioEngine::set_next_url(const QUrl &url) -> void
{
  QMutexLocker lock(&next_url_mutex);
  next_url = url;
}

auto
GstAudioEngine::clear_next_url() -> void
{
  QMutexLocker lock(&next_url_mutex);
  next_url.clear();
}

auto
GstAudioEngine::on_about_to_finish() -> void
{
  QMutexLocker lock(&next_url_mutex);
  if (!next_url.isEmpty())
    {
      spdlog::debug("GstAudioEngine: about-to-finish; committing gapless uri {}",
          redact_stream_url(next_url));
      g_object_set(pipeline, "uri",
          next_url.toString().toUtf8().constData(), nullptr);
      gapless_active.store(true, std::memory_order_release);
    }
  else
    spdlog::debug("GstAudioEngine: about-to-finish; no next uri queued (will EOS)");
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

auto
GstAudioEngine::state() const -> State
{
  // Mid-flight the pipeline reads PAUSED no matter what was asked of it, so
  // reporting it would turn every seek into a spurious pause. Report the
  // intent; busy() carries the rest of the truth.
  if (busy_now != Busy::None)
    return playing ? State::Playing : State::Paused;
  return current_state;
}

auto
GstAudioEngine::busy() const -> Busy
{
  return busy_now;
}

auto
GstAudioEngine::pipeline_state() const -> GstState
{
  if (!pipeline) return GST_STATE_NULL;
  GstState cur = GST_STATE_VOID_PENDING;
  gst_element_get_state(pipeline, &cur, nullptr, 0);
  return cur;
}

auto
GstAudioEngine::set_reported_state(State s) -> void
{
  if (current_state == s) return;
  current_state = s;
  emit state_changed(s);
}

auto
GstAudioEngine::set_busy(Busy b) -> void
{
  if (busy_now == b) return;
  // Whatever caused the exit (settle, refusal, error, EOS, a fresh
  // start_stream) the wait this timer was watching is over.
  if (busy_now == Busy::Seek)
    seek_backstop_timer->stop();
  busy_now = b;
  emit busy_changed(b);
}

auto
GstAudioEngine::position_ms() const -> uint32_t
{
  update_position_cache();
  return last_pos_ms;
}

auto
GstAudioEngine::duration_ms() const -> uint32_t
{
  return last_dur_ms;
}

auto
GstAudioEngine::volume() const -> int
{
  return vol;
}

auto
GstAudioEngine::update_position_cache() const -> void
{
  if (!pipeline) return;
  gint64 pos = 0, dur = 0;
  if (gst_element_query_position(pipeline, GST_FORMAT_TIME, &pos))
    last_pos_ms = static_cast<uint32_t>(std::max(gint64(0), pos) / GST_MSECOND);
  if (gst_element_query_duration(pipeline, GST_FORMAT_TIME, &dur) && dur > 0)
    last_dur_ms = static_cast<uint32_t>(dur / GST_MSECOND);
}

// ---------------------------------------------------------------------------
// GStreamer bus
// ---------------------------------------------------------------------------

auto
GstAudioEngine::handle_bus_message(GstMessage *msg) -> void
{
  if (!pipeline) return;

  switch (GST_MESSAGE_TYPE(msg))
    {
    case GST_MESSAGE_EOS:
      spdlog::debug("GstAudioEngine: EOS at {} ms / {} ms", last_pos_ms, last_dur_ms);
      playing = false;
      clock_timer->stop();
      seek_waiting = false;
      seek_timer->stop();
      set_busy(Busy::None);
      // A pipeline left PLAYING at EOS keeps its sound-server stream open and
      // uncorked. pulsesink asks for prebuf=0, and pipewire-pulse (1.6.x) then
      // advances that stream's 32-bit read index through the endless underrun;
      // every 2^31 bytes the index comparison wraps and the server replays its
      // ring buffer — the last seconds of this track — hours later, unprompted.
      // Tearing down releases the stream (and the output device with it). The
      // position is cached first: a pipeline in NULL answers no queries.
      update_position_cache();
      gst_element_set_state(pipeline, GST_STATE_NULL);
      // Recorded, not announced: on a gapless advance the next stream is about
      // to start, and a Stopped in between would read as the player halting.
      current_state = State::Stopped;
      emit track_finished();
      break;

    case GST_MESSAGE_STREAM_START:
      {
        // A stream actually started decoding — the previous track's failure
        // (if any) did not cascade, so clear the brake. The track is now
        // playing, so a later error is an in-track failure (recoverable), not a
        // failed start.
        consecutive_failures = 0;
        track_started = true;

        // Read playbin's current-uri: it's the authoritative now-playing track
        // (the gapless URI was committed ~10 s earlier at about-to-finish; the
        // queue may have moved since). Keep current_url in sync for both the
        // gapless track_changed and the in-place error recovery.
        QUrl now_playing;
        gchar *uri = nullptr;
        g_object_get(pipeline, "current-uri", &uri, nullptr);
        if (uri)
          {
            now_playing = QUrl(QString::fromUtf8(uri));
            g_free(uri);
          }
        if (!now_playing.isEmpty())
          current_url = now_playing;

        bool was_gapless = gapless_active.exchange(false, std::memory_order_acq_rel);
        spdlog::debug("GstAudioEngine: STREAM_START uri={} (gapless={})",
            redact_stream_url(now_playing), was_gapless);
        if (was_gapless)
          emit track_changed(now_playing);
        break;
      }

    case GST_MESSAGE_BUFFERING:
      {
        // Path A': do NOT pause/resume the pipeline on buffering. With
        // GST_PLAY_FLAG_DOWNLOAD the messages come from playbin's internal
        // download queue; let it manage flow. Manually pausing here deadlocked
        // near end-of-track when it collided with the about-to-finish gapless
        // handoff. Trace only.
        int percent = 0;
        gst_message_parse_buffering(msg, &percent);
        spdlog::trace("GstAudioEngine: buffering {}% (pos {} / {} ms)",
            percent, last_pos_ms, last_dur_ms);
        break;
      }

    case GST_MESSAGE_STATE_CHANGED:
      {
        if (GST_MESSAGE_SRC(msg) != GST_OBJECT(pipeline)) break;

        GstState old_state, new_state, pending;
        gst_message_parse_state_changed(msg, &old_state, &new_state, &pending);
        spdlog::debug("GstAudioEngine: pipeline state {} -> {} (pending {})",
            gst_element_state_get_name(old_state),
            gst_element_state_get_name(new_state),
            gst_element_state_get_name(pending));

        if (pending != GST_STATE_VOID_PENDING) break;

        if (new_state == GST_STATE_PAUSED && pending_seek_ms > 0)
          {
            // The pipeline has prerolled to the saved position's track.
            // Issue the flush-seek and wait for ASYNC_DONE to start
            // playback, rather than blocking the UI thread on get_state.
            auto pos = pending_seek_ms;
            pending_seek_ms = 0;
            last_pos_ms = pos;
            restore_play_pending = true;
            restoring = true;
            set_busy(Busy::Seek);
            // Not seek_backstop_attempts = 0 here: this branch is also where a
            // prior backstop's own retry re-enters (start_stream -> preroll ->
            // this flush-seek), so resetting here would erase the retry budget
            // and let a wedged stream loop forever.
            backstop_target_ms = pos;
            seek_backstop_timer->start();
            // Accurate (FLUSH-only) restore — see do_seek_now for why KEY_UNIT
            // was dropped.
            gst_element_seek_simple(pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                static_cast<gint64>(pos) * GST_MSECOND);
            break;
          }

        if (new_state == GST_STATE_PLAYING)
          {
            restoring = false; // restore sequence has reached playback
            update_position_cache();
            clock_timer->start();
            // A genuine settle: give any future freeze a fresh backstop budget
            // rather than carrying forward a count from an unrelated seek.
            seek_backstop_attempts = 0;
            // State first, then busy: state() answers from intent while busy is
            // set, so clearing it first would expose the stale value for the
            // width of a signal emission.
            set_reported_state(State::Playing);
            set_busy(Busy::None);
          }
        else if (new_state == GST_STATE_PAUSED && pending_seek_ms == 0
                 && !restoring)
          {
            update_position_cache();
            clock_timer->stop();
            seek_backstop_attempts = 0;
            set_reported_state(State::Paused);
            set_busy(Busy::None);
          }
        break;
      }

    case GST_MESSAGE_ASYNC_DONE:
      // Query rather than report the cache: until this settles, last_pos_ms is
      // the *requested* seek target, so logging it says nothing about where the
      // pipeline actually landed.
      update_position_cache();
      spdlog::debug("GstAudioEngine: ASYNC_DONE at {} ms (seek/preroll settled)",
          last_pos_ms);
      // The restore flush-seek has settled — start playback now. (FIFO bus
      // order guarantees the initial preroll's ASYNC_DONE was already seen
      // before we set the flag, so this is the seek's completion.) Stay Busy
      // until playback actually starts; the PLAYING transition ends it.
      if (restore_play_pending)
        {
          restore_play_pending = false;
          gst_element_set_state(pipeline, GST_STATE_PLAYING);
          break;
        }
      {
        // A preroll settling on its way to PLAYING is not finished work — the
        // PLAYING transition is, and it clears Busy itself.
        GstState settled = GST_STATE_VOID_PENDING;
        GstState still_pending = GST_STATE_VOID_PENDING;
        gst_element_get_state(pipeline, &settled, &still_pending, 0);
        if (still_pending == GST_STATE_VOID_PENDING)
          {
            set_busy(Busy::None);
            emit position_changed(last_pos_ms);
          }
      }
      break;

    case GST_MESSAGE_WARNING:
      {
        // Non-fatal — gst keeps playing. Logged because these often precede a
        // fatal ERROR (e.g. a parser/decoder complaint just before it gives up).
        GError *err = nullptr;
        gchar *dbg = nullptr;
        gst_message_parse_warning(msg, &err, &dbg);
        spdlog::warn("GstAudioEngine: warning from {}: {} (debug: {})",
            GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)), err->message,
            dbg ? dbg : "none");
        g_clear_error(&err);
        g_free(dbg);
        break;
      }

    case GST_MESSAGE_ERROR:
      {
        GError *err = nullptr;
        gchar *dbg = nullptr;
        gst_message_parse_error(msg, &err, &dbg);
        QString msg_text = QString::fromUtf8(err->message);
        spdlog::error("GstAudioEngine: error from {} at {} ms / {} ms "
                      "(track_started={}): {}",
            GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)), last_pos_ms, last_dur_ms,
            track_started, err->message);
        spdlog::debug("GstAudioEngine: error debug: {}", dbg ? dbg : "none");
        g_clear_error(&err);
        g_free(dbg);

        if (!playing)
          break;
        playing = false;
        clock_timer->stop();
        // Whatever was in flight is not coming back — a recovery below may open
        // new work, but this attempt is over.
        seek_waiting = false;
        seek_timer->stop();
        set_busy(Busy::None);
        // As with EOS: recovery or an advance usually follows, so this is
        // recorded rather than announced. The brake below announces when it
        // decides nothing further will be attempted.
        current_state = State::Stopped;

        if (track_started && !current_url.isEmpty())
          {
            // The track WAS playing and then errored — almost always the
            // intermittent, unpreventable souphttpsrc/baseparse -5 fault on
            // FLAC-over-HTTP seeks (confirmed against live LMS; serializing seeks
            // doesn't stop it). Don't skip the song: re-establish the stream at the
            // current position. The budget resets whenever playback has progressed
            // past the last error, so only a spot the server genuinely can't serve
            // exhausts the retries and falls back to advancing.
            uint32_t pos = last_pos_ms;

            // An error in the final stretch of the track is effectively EOS (a
            // truncated/short final frame): recovering re-seeks into the tail and
            // re-hits the same spot. Just finish and advance.
            if (last_dur_ms > 0 && pos + end_of_track_guard_ms >= last_dur_ms)
              {
                spdlog::warn("GstAudioEngine: stream error at {} ms of {} ms "
                             "(near end); treating as EOS",
                    pos, last_dur_ms);
                // Torn down for the same reason as at EOS.
                gst_element_set_state(pipeline, GST_STATE_NULL);
                emit track_finished();
                break;
              }

            if (pos > last_error_pos_ms + 500)
              seek_recovery_attempts = 0;
            last_error_pos_ms = pos;

            if (seek_recovery_attempts < max_seek_recovery_attempts)
              {
                ++seek_recovery_attempts;
                // Step back rather than re-seeking to the identical position:
                // a bad frame alignment fails the same way every retry.
                uint32_t retry_pos = pos > seek_recovery_step_ms
                                         ? pos - seek_recovery_step_ms
                                         : 0;
                spdlog::warn("GstAudioEngine: stream error mid-track; "
                             "re-establishing at {} ms (attempt {}/{})",
                    retry_pos, seek_recovery_attempts, max_seek_recovery_attempts);
                start_stream(current_url, retry_pos);
                break;
              }

            spdlog::warn("GstAudioEngine: recovery exhausted at {} ms; advancing",
                pos);
            gst_element_set_state(pipeline, GST_STATE_NULL);
            emit error(msg_text);
            emit track_finished();
            break;
          }

        // Never decoded a frame (bad url / dead server): advance to the next
        // track, with the cascade brake so a dead server doesn't march the queue.
        emit error(msg_text);
        if (++consecutive_failures < max_consecutive_failures)
          emit track_finished();
        else
          {
            spdlog::warn("GstAudioEngine: {} consecutive failures, "
                         "halting auto-advance",
                consecutive_failures);
            gst_element_set_state(pipeline, GST_STATE_NULL);
            current_state = State::Stopped;
            emit state_changed(State::Stopped);
          }
        break;
      }

    default:
      break;
    }
}
