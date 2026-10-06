#ifndef VISUALIZERCONTROLLER_HH
#define VISUALIZERCONTROLLER_HH

#include <QList>
#include <QObject>
#include <QTimer>

#include <memory>
#include <vector>

namespace viz {
class PipeWireCapture;
class SpectrumAnalyzer;
} // namespace viz

// Qt glue over the vizlib core: drives the PipeWire capture + spectrum analysis
// from a ~60 fps timer on the GUI thread and emits normalized bands + level for
// the Now Playing model. Owns lifecycle via set_active() — callers gate it on
// "the visualizer is on screen and playing" so a default-sink tap never runs in
// the background.
class VisualizerController : public QObject {
  Q_OBJECT
public:
  // Where the analyzed PCM comes from. PipeWire taps the default sink's monitor
  // (all system audio — for backends that play remotely, e.g. MPD); it registers
  // a capture stream, so the OS shows a "microphone in use" hint. External takes
  // PCM pushed via feed_pcm() — the active backend's own decoded output (e.g. the
  // GStreamer engine) — so only that player's sound is analyzed and there is no
  // capture stream (no mic indicator).
  enum class Source { PipeWire,
    External };

  explicit VisualizerController(QObject *parent = nullptr);
  ~VisualizerController() override;

  auto band_count() const -> int;

  // Choose the PCM source. If switched while active, the current source is torn
  // down; the caller re-arms with set_active().
  void set_source(Source s);

public slots:
  void set_active(bool on);

  // Push externally-captured mono PCM (Source::External) at the analyzer's rate.
  // Ignored unless active and in External mode.
  void feed_pcm(const QList<float> &samples);

signals:
  void bands_ready(const QList<qreal> &bands);
  void level_ready(qreal level);

private:
  void tick();

  std::unique_ptr<viz::PipeWireCapture> capture;
  std::unique_ptr<viz::SpectrumAnalyzer> analyzer;
  QTimer timer;
  std::vector<float> drain; // scratch: ring → analyzer
  std::vector<float> bands_scratch;
  Source source = Source::PipeWire;
  bool active = false;
};

#endif // VISUALIZERCONTROLLER_HH
