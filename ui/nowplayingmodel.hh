#ifndef NOWPLAYINGMODEL_HH
#define NOWPLAYINGMODEL_HH

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>

// DTO / view-model bridging the app's playback state to the QML Now Playing
// scene. Properties are the stable contract QML binds to — additive by design:
// new fields (e.g. spectrum/level bands, lyrics, queue context) get a new
// Q_PROPERTY + setter here without disturbing existing screens. Nothing in this
// class knows about a specific QML layout, so alternative screens can bind the
// same data differently.
class NowPlayingModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString title READ title NOTIFY trackChanged)
  Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
  Q_PROPERTY(QString album READ album NOTIFY albumChanged)
  Q_PROPERTY(QString year READ year NOTIFY albumChanged)
  Q_PROPERTY(QString audioFormat READ audio_format NOTIFY audioFormatChanged)
  Q_PROPERTY(QString playState READ play_state NOTIFY playStateChanged)
  // Empty when idle. Set while a seek/preroll hasn't settled yet, so the scene
  // can show that instead of a position that has stopped advancing.
  Q_PROPERTY(QString busyText READ busy_text NOTIFY busyTextChanged)
  Q_PROPERTY(int position READ position NOTIFY progressChanged)   // seconds
  Q_PROPERTY(int duration READ duration NOTIFY progressChanged)   // seconds
  Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged) // 0.0–1.0
  Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)
  Q_PROPERTY(QString coverId READ cover_id NOTIFY coverChanged) // cache-busting key
  // Visualizer: a fixed number of spectrum bands (each 0.0–1.0) plus an overall
  // level. bandCount is set once before the scene loads so a Repeater can size
  // itself without rebuilding delegates each frame; it stays 0 when the
  // visualizer is unavailable, so the QML hides the bars.
  Q_PROPERTY(QList<qreal> bands READ bands NOTIFY bandsChanged)
  Q_PROPERTY(qreal level READ level NOTIFY levelChanged)
  Q_PROPERTY(int bandCount READ band_count NOTIFY bandCountChanged)
  // Row count of the history ring-buffer texture (its height). Set once before the
  // scene loads so history shaders read the ring modulo from a uniform instead of
  // hardcoding it. Stays 0 when no visualizer is available.
  Q_PROPERTY(int historyRows READ history_rows NOTIFY historyRowsChanged)
  // Slow loudness/bass envelopes for ambient visualizations (e.g. the album
  // wash): EMAs of level and of the low bands, derived in the setters below.
  // They breathe with the music instead of twitching on transients.
  Q_PROPERTY(qreal washEnergy READ wash_energy NOTIFY washEnergyChanged)
  Q_PROPERTY(qreal washBass READ wash_bass NOTIFY washBassChanged)
  // Scrolling-spectrogram history for visualizers that draw past frames (e.g.
  // the ridgeline). The texture is a ring buffer: `historyRev` bumps whenever a
  // new row is committed (a cache-buster for the QML Image that uploads it),
  // `vizHead` is the number of rows committed (newest row = (head-1) mod rows),
  // and `vizFrac` is the sub-row scroll position 0..1 that drives smooth motion
  // between commits. `vizFrac` updates every frame; `vizHead`/`historyRev` only on
  // commit. All stay 0 when no visualizer is running.
  Q_PROPERTY(int historyRev READ history_rev NOTIFY historyRevChanged)
  Q_PROPERTY(int vizHead READ viz_head NOTIFY vizHeadChanged)
  Q_PROPERTY(qreal vizFrac READ viz_frac NOTIFY vizFracChanged)
  // User toggle for the visualizer backdrop. Writable from QML (the in-scene
  // toggle assigns it); NowPlayingView persists changes to QSettings and gates
  // audio capture on it. Off by default.
  Q_PROPERTY(bool visualizerEnabled READ visualizer_enabled WRITE
          set_visualizer_enabled NOTIFY visualizerEnabledChanged)
  // Chosen visualization (string id; the QML scene maps it to a shader + layout).
  // Writable from QML (the in-scene picker cycles it); NowPlayingView persists it.
  Q_PROPERTY(QString vizPreset READ viz_preset WRITE set_viz_preset NOTIFY
          vizPresetChanged)
  // Whether the window is in fullscreen Now Playing. Read-only: the QML button
  // reads it to pick its glyph but requests a change via toggleFullscreen() (the
  // actual window state lives in MainWindow, which drives this back via
  // set_fullscreen). Keeping the intent (toggleFullscreen) separate from the
  // reflected state (this property) avoids a set→notify→request feedback loop.
  Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY fullscreenChanged)

public:
  explicit NowPlayingModel(QObject *parent = nullptr);

  auto title() const -> QString { return title_; }
  auto artist() const -> QString { return artist_; }
  auto album() const -> QString { return album_; }
  auto year() const -> QString { return year_; }
  auto audio_format() const -> QString { return audio_format_; }
  auto play_state() const -> QString { return play_state_; }
  auto busy_text() const -> QString { return busy_text_; }
  auto position() const -> int { return position_; }
  auto duration() const -> int { return duration_; }
  // ms-precise for a smooth bar; fed by ClockMan ticks via set_progress, which
  // already carries the interpolated position. Falls back to 0 when no
  // duration is known.
  auto progress() const -> qreal
  {
    if (progress_total_ms_ <= 0)
      return 0.0;
    return qreal(progress_elapsed_ms_) / progress_total_ms_;
  }
  auto accent() const -> QColor { return accent_; }
  auto cover_id() const -> QString { return cover_id_; }
  auto bands() const -> QList<qreal> { return bands_; }
  auto level() const -> qreal { return level_; }
  auto band_count() const -> int { return band_count_; }
  auto history_rows() const -> int { return history_rows_; }
  auto wash_energy() const -> qreal { return wash_energy_; }
  auto wash_bass() const -> qreal { return wash_bass_; }
  auto history_rev() const -> int { return history_rev_; }
  auto viz_head() const -> int { return viz_head_; }
  auto viz_frac() const -> qreal { return viz_frac_; }
  auto visualizer_enabled() const -> bool { return visualizer_enabled_; }
  auto viz_preset() const -> QString { return viz_preset_; }
  auto fullscreen() const -> bool { return fullscreen_; }

  // Setters used by NowPlayingView; emit only on change.
  auto set_track(const QString &title, const QString &artist) -> void;
  auto set_album(const QString &album, const QString &year) -> void;
  auto set_audio_format(const QString &fmt) -> void;
  auto set_play_state(const QString &state) -> void;
  auto set_busy_text(const QString &text) -> void;
  auto set_progress(qint64 elapsed_ms, qint64 total_ms) -> void;
  auto set_accent(const QColor &accent) -> void;
  auto set_cover_id(const QString &id) -> void;
  auto set_bands(const QList<qreal> &bands) -> void;
  auto set_level(qreal level) -> void;
  auto set_band_count(int n) -> void;
  auto set_history_rows(int n) -> void;
  auto set_history(int revision, int head, qreal frac) -> void;
  auto set_visualizer_enabled(bool value) -> void;
  auto set_viz_preset(const QString &id) -> void;
  // Called by NowPlayingView to reflect the window's actual fullscreen state back
  // into the scene (so the button glyph stays in sync when toggled by keyboard).
  auto set_fullscreen(bool value) -> void;

  // Invoked from QML to request leaving the Now Playing screen.
  // Conventional return type: moc cannot parse trailing-return on invokables.
  Q_INVOKABLE void requestClose() { emit closeRequested(); }

  // Invoked from QML when the user scrubs the progress bar; ms is the absolute
  // target position. NowPlayingView forwards it to the backend's seek.
  Q_INVOKABLE void seek(int ms) { emit seekRequested(ms); }

  // Invoked from QML (the corner button) to request entering/leaving fullscreen.
  // MainWindow performs the window-state change, then reflects it via set_fullscreen.
  Q_INVOKABLE void toggleFullscreen() { emit fullscreenToggleRequested(); }

  // Invoked from QML on mouse movement over the scene; NowPlayingView uses it to
  // reset the fullscreen cursor-hide timer (and un-hide the cursor).
  Q_INVOKABLE void pokeActivity() { emit activity(); }

signals:
  void trackChanged();
  void albumChanged();
  void audioFormatChanged();
  void playStateChanged();
  void busyTextChanged();
  void progressChanged();
  void accentChanged();
  void coverChanged();
  void bandsChanged();
  void levelChanged();
  void bandCountChanged();
  void historyRowsChanged();
  void washEnergyChanged();
  void washBassChanged();
  void historyRevChanged();
  void vizHeadChanged();
  void vizFracChanged();
  void visualizerEnabledChanged();
  void vizPresetChanged();
  void fullscreenChanged();
  void fullscreenToggleRequested();
  void activity();
  void closeRequested();
  void seekRequested(int ms);

private:
  qint64 progress_elapsed_ms_ = 0;
  qint64 progress_total_ms_ = 0;

  QString title_;
  QString artist_;
  QString album_;
  QString year_;
  QString audio_format_;
  QString play_state_{"stopped"};
  QString busy_text_;
  int position_ = 0;
  int duration_ = 0;
  QColor accent_;
  QString cover_id_{"0"};
  QList<qreal> bands_;
  qreal level_ = 0.0;
  qreal wash_energy_ = 0.0;
  qreal wash_bass_ = 0.0;
  int band_count_ = 0;
  int history_rows_ = 0;
  int history_rev_ = 0;
  int viz_head_ = 0;
  qreal viz_frac_ = 0.0;
  bool visualizer_enabled_ = false;
  QString viz_preset_{"aurora"};
  bool fullscreen_ = false;
};

#endif // NOWPLAYINGMODEL_HH
