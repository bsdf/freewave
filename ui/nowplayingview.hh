#ifndef NOWPLAYINGVIEW_HH
#define NOWPLAYINGVIEW_HH

#include <QColor>
#include <QWidget>

#include <memory>

#include "model/song.hh"
#include "controller/playbackstate.hh"

#ifdef ENABLE_VISUALIZER
#include <QElapsedTimer>
#endif

class Backend;
class NowPlayingModel;
class NowPlayingImageProvider;
class QQuickWidget;
class QTimer;
class VisualizerController;
class VizHistory;

// Thin QWidget host for the QML-defined Now Playing scene. Public slots match
// the old widget-based view exactly, so MainWindow wiring is unchanged; they
// feed a NowPlayingModel that QML binds to. The current scene is loaded from the
// qrc (qrc:/qml/NowPlaying.qml); user-supplied screens are a later phase.
class NowPlayingView : public QWidget {
  Q_OBJECT
public:
  // `backend` (the stable BackendController façade) is queried for the local-PCM
  // capability so the visualizer can tap the engine directly when available;
  // may be null (visualizer falls back to the PipeWire monitor).
  explicit NowPlayingView(Backend *backend, QWidget *parent = nullptr);
  ~NowPlayingView() override;

public slots:
  void set_current_song(const song &s);
  void set_album_art(const QPixmap &px);
  void set_album_info(const QString &album_name, const QString &year);
  void set_accent(const QColor &accent);
  void set_progress(qint64 elapsed_ms, qint64 total_ms);
  void set_audio_format(const PlaybackState &state);
  // Reflect the window's actual fullscreen state into the scene (button glyph).
  void set_fullscreen(bool on);

signals:
  void close_requested();
  void seek(uint32_t ms);
  // The in-scene fullscreen button (or its bound state) was toggled; MainWindow
  // owns the window-state change and calls set_fullscreen back to confirm.
  void fullscreen_toggle_requested();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  // Mirrors PlaybackView's busy handling: delay showing "seeking"/"loading" so
  // a wait that resolves quickly doesn't flicker the display.
  void set_busy(PlayBusy state);

#ifdef ENABLE_VISUALIZER
  // Run the capture only while the scene is on screen *and* playing — a
  // default-sink tap would otherwise monitor all system audio in the background.
  void update_visualizer();
#endif

  // Fullscreen cursor auto-hide: blank the pointer after inactivity, restore it on
  // the next movement. Only armed while fullscreen (see set_fullscreen).
  void set_cursor_hidden(bool hidden);

  Backend *backend = nullptr;
  NowPlayingModel *model;
  NowPlayingImageProvider *image_provider; // owned by the QML engine
  QQuickWidget *quick;
  quint64 cover_seq = 0;
  QTimer *idle_timer = nullptr; // fullscreen cursor-hide countdown
  bool np_fullscreen = false;
  bool cursor_hidden = false;
  QTimer *busy_delay_timer = nullptr;
  PlayBusy busy = PlayBusy::None;
  bool busy_visible = false;
#ifdef ENABLE_VISUALIZER
  VisualizerController *visualizer = nullptr;
  bool playing = false;
  bool viz_active = false;
  std::unique_ptr<VizHistory> history;
  QElapsedTimer viz_clock;
#endif
};

#endif // NOWPLAYINGVIEW_HH
