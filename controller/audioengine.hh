#ifndef AUDIOENGINE_HH
#define AUDIOENGINE_HH

#include <cstdint>
#include <QList>
#include <QObject>
#include <QUrl>

// Abstract audio playback engine. Speaks only in URLs and milliseconds.
// Knows nothing about songs, queues, or backends.
class AudioEngine : public QObject {
  Q_OBJECT
public:
  enum class State { Stopped,
    Playing,
    Paused };

  // Work the engine is doing that has not settled yet. A remote seek can take
  // ten seconds or more, during which State alone says "playing" and the
  // position stands still — indistinguishable, to a listener, from a hang. This
  // is what lets the UI say which of the two it is.
  enum class Busy { None,
    Preroll, // opening a stream: no audio yet
    Seek };  // moving within a stream that is already open

  explicit AudioEngine(QObject *parent = nullptr)
    : QObject{parent}
  {
  }

  // Start playing url from start_ms. The engine handles any seek-before-play
  // internally — callers never see a position-0 flash.
  virtual void play(const QUrl &url, uint32_t start_ms = 0) = 0;

  // Pre-load the next URL for a gapless transition. Call this whenever the
  // next track changes (queue mutations, track advances). The engine reads it
  // synchronously from the GStreamer about-to-finish callback.
  virtual void set_next_url(const QUrl &url) = 0;
  virtual void clear_next_url() = 0;

  virtual void pause() = 0;
  virtual void resume() = 0;
  virtual void stop() = 0;
  virtual void seek(uint32_t ms) = 0;
  virtual void set_volume(int vol) = 0; // 0–100

  virtual State state() const = 0;
  virtual uint32_t position_ms() const = 0; // last known; safe on cold pipeline
  virtual uint32_t duration_ms() const = 0;
  virtual int volume() const = 0;

  // Engines that complete every operation synchronously never report Busy.
  virtual Busy busy() const { return Busy::None; }

  // Visualizer PCM tap. When enabled, an engine that decodes audio locally taps
  // its own output and emits mono float frames via viz_pcm — letting the
  // visualizer analyze exactly this player's sound, with no PipeWire capture
  // stream (so no "microphone in use" indicator). Default no-op: engines that
  // don't decode locally simply never emit. Gated on by the visualizer when the
  // backend advertises Feature::LocalPcm.
  virtual void set_viz_pcm_enabled(bool) {}

  // Stop verifying the stream's TLS certificate. Set only when the user has
  // already accepted this server's certificate through the trust-on-first-use
  // prompt: the streaming stack (souphttpsrc → glib-networking → GnuTLS) has its
  // own trust store, which never sees that pin, so a certificate the API accepts
  // can still be rejected here. Handing GnuTLS the pinned certificate is not an
  // option — a leaf server cert is not a CA, so it cannot serve as a trust anchor.
  // Note the asymmetry this leaves: the API pins an exact fingerprint, while the
  // stream then accepts any certificate from that host. Default off.
  virtual void set_allow_untrusted_tls(bool) {}

signals:
  void state_changed(AudioEngine::State state);
  void busy_changed(AudioEngine::Busy busy);
  void position_changed(uint32_t ms);  // ~1 s interval during playback
  void duration_changed(uint32_t ms);  // when GStreamer reports duration
  void track_changed(const QUrl &uri); // gapless transition; uri = now-playing stream
  void track_finished();               // EOS with no gapless successor
  void error(const QString &message);

  // Mono PCM frames tapped from the engine's own output (the analysis rate is
  // fixed by the tap; see GstAudioEngine). Emitted on the engine's thread only
  // while the tap is enabled.
  void viz_pcm(const QList<float> &samples);
};

#endif // AUDIOENGINE_HH
