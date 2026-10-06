#ifndef GSTAUDIOENGINE_HH
#define GSTAUDIOENGINE_HH

#include <atomic>

#include <QMutex>
#include <QTimer>
#include <QUrl>

#include <gst/gst.h>

#include "controller/audioengine.hh"

class GstAudioEngine : public AudioEngine {
  Q_OBJECT
public:
  explicit GstAudioEngine(QObject *parent = nullptr);
  ~GstAudioEngine();

  void play(const QUrl &url, uint32_t start_ms = 0) override;
  void set_next_url(const QUrl &url) override;
  void clear_next_url() override;

  void pause() override;
  void resume() override;
  void stop() override;
  void seek(uint32_t ms) override;
  void set_volume(int vol) override;

  State state() const override;
  uint32_t position_ms() const override;
  uint32_t duration_ms() const override;
  int volume() const override;
  Busy busy() const override;

  // The pipeline's settled GStreamer state; exposed for tests.
  auto pipeline_state() const -> GstState;

  void set_viz_pcm_enabled(bool on) override;
  void set_allow_untrusted_tls(bool on) override { allow_untrusted_tls = on; }

  void on_about_to_finish(); // called from GStreamer streaming thread
  void on_source_setup(GstElement *source);
#ifdef ENABLE_VISUALIZER
  // Called from the tap fakesink's streaming thread; marshals the PCM to the
  // engine's thread before emitting viz_pcm.
  void on_viz_handoff(GstBuffer *buffer);
#endif

private slots:
  void do_seek_now();
  void on_seek_backstop();

private:
  void setup_pipeline();
#ifdef ENABLE_VISUALIZER
  // Build the custom playbin audio-sink: tee → real sink + an analysis branch
  // (convert/resample to mono F32 at VIZ_TAP_RATE → fakesink handoff). Returns
  // the bin to hand to playbin's "audio-sink", or nullptr on failure (falls
  // back to the default sink).
  auto build_viz_tap_sink() -> GstElement *;
#endif
  void start_stream(const QUrl &url, uint32_t start_ms);
  void handle_bus_message(GstMessage *msg);
  // Single writer for the reported state / busy pair: assigns, then emits only
  // on an actual change, so a repeated pipeline transition is not news.
  void set_reported_state(State s);
  void set_busy(Busy b);
  void apply_volume();
  void update_position_cache() const;

  GstElement *pipeline = nullptr;

  // What the user asked for. Drives the pause/resume guards, and is what gets
  // reported while the pipeline is mid-flight (see `busy_now`).
  bool playing = false;
  // What the pipeline last reported. Written by the bus, never by a command —
  // the exceptions are stop/EOS/error, which are conclusions the pipeline does
  // not post: a pipeline torn down to NULL flushes its bus on the way.
  State current_state = State::Stopped;
  // Set where async work is issued (start_stream, do_seek_now, the restore
  // seek), cleared on ASYNC_DONE. Between those points the pipeline sits in
  // PAUSED whatever the user asked for, so reporting its state verbatim would
  // show a pause nobody requested — on a cold remote seek, for ten seconds or
  // more. state() reports intent across that window; this reports the fact.
  Busy busy_now = Busy::None;
  uint32_t pending_seek_ms = 0;
  // Restore-from-startup: after the flush-seek to the saved position is issued
  // we wait (non-blocking) for ASYNC_DONE before going PLAYING, so the UI
  // thread never blocks on get_state. `restore_play_pending` fires the PLAYING
  // transition once; `restoring` suppresses the intermediate PAUSED emissions
  // the re-preroll produces until playback actually begins.
  bool restore_play_pending = false;
  bool restoring = false;

  mutable uint32_t last_pos_ms = 0;
  mutable uint32_t last_dur_ms = 0;
  int vol = 100;

  bool seek_waiting = false;
  uint32_t seek_target_ms = 0;
  QTimer *seek_timer;

  // Backstop for a seek the pipeline never settles: a cold remote seek can
  // legitimately run 10-15 s, so this fires well clear of that at 30 s, not
  // near it. Armed explicitly at each site that enters Busy::Seek (seek() and
  // the startup-restore flush-seek) with the target position stashed at arm
  // time, since last_pos_ms/pending_seek_ms are unreliable mid-wait; disarmed
  // centrally in set_busy() on any transition out of Seek. seek_backstop_attempts
  // accumulates across the retries this fires (reset only on a genuine settle),
  // so a permanently wedged stream gives up rather than retrying forever.
  QTimer *seek_backstop_timer;
  uint32_t backstop_target_ms = 0;
  int seek_backstop_attempts = 0;
  static constexpr int seek_backstop_ms = 30000;
  static constexpr int max_seek_backstop_attempts = 3;

  QMutex next_url_mutex;
  QUrl next_url;
  std::atomic<bool> gapless_active{false};

  // Brake for the error -> advance cascade: if a server dies mid-queue every
  // entry fails in turn. Count failures-without-a-successful-stream-start and
  // stop auto-advancing once they pile up. Reset on STREAM_START.
  int consecutive_failures = 0;
  static constexpr int max_consecutive_failures = 3;

  // In-place recovery for a track that WAS playing and then errored — almost
  // always a transient HTTP/seek failure (souphttpsrc -5 after a range request
  // for a manual seek). Such an error must not skip the song: re-establish the
  // stream at the current position instead. `current_url` is the now-playing uri
  // (kept in sync on STREAM_START); `track_started` separates "never decoded a
  // frame" (bad url -> advance) from "was playing" (-> recover). The retry budget
  // resets whenever playback has progressed past the last error point, so a spot
  // the server genuinely can't serve still falls back to advancing.
  QUrl current_url;
  bool track_started = false;
  int seek_recovery_attempts = 0;
  uint32_t last_error_pos_ms = 0;
  static constexpr int max_seek_recovery_attempts = 3;
  // Some seek targets land on a byte/frame alignment flacparse can't resync
  // from (gst_base_parse_finish_frame assertion, reproducible on a local file
  // seek at the exact same ms — not a network fault); retrying the identical
  // position fails identically every time. Step each retry back by this much
  // so it lands on a different frame boundary; last_pos_ms re-anchors after
  // each attempt, so the walk-back is cumulative across the retry budget.
  static constexpr uint32_t seek_recovery_step_ms = 800;
  // An error within this many ms of the track's end is treated as EOS rather
  // than recovered: replaying the tail just re-hits the same near-end fault.
  static constexpr uint32_t end_of_track_guard_ms = 3000;

  // Bus watch (GLib source on the pipeline's bus); relies on Qt's GLib event
  // dispatcher. Removed in the destructor.
  guint bus_watch_id = 0;
  QTimer *clock_timer;

#ifdef ENABLE_VISUALIZER
  // Visualizer PCM tap. The fakesink in the audio-sink bin fires handoff on its
  // streaming thread; we only copy/emit when the consumer has enabled the tap
  // (NowPlaying on screen + playing). The branch always exists during playback;
  // the flag just gates the per-buffer work and the cross-thread emit.
  static constexpr int VIZ_TAP_RATE = 48000;
  std::atomic<bool> viz_tap_enabled{false};
#endif

  friend void gst_about_to_finish_cb(GstElement *, gpointer);
  friend void gst_source_setup_cb(GstElement *, GstElement *, gpointer);

  bool allow_untrusted_tls = false;
  friend gboolean gst_bus_watch_cb(GstBus *, GstMessage *, gpointer);
};

#endif // GSTAUDIOENGINE_HH
