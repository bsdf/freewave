#ifndef SHADERLABWINDOW_HH
#define SHADERLABWINDOW_HH

#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QWidget>

#include <memory>

class NowPlayingModel;
class SynthHistory;
class CoverProvider;
class QQuickWidget;
class QComboBox;
class QPlainTextEdit;
class QLabel;
class QSlider;
class QCheckBox;
class QTimer;
class QFileSystemWatcher;
class QTemporaryDir;

#ifdef ENABLE_VISUALIZER
class VisualizerController;
#endif

// Context object the harness injects into the QML scene as `labShader`. When a
// path is non-empty the scene's matching ShaderEffect uses it in place of the
// baked-in .qsb, so a freshly recompiled shader previews live. Absent in the
// real app (the QML guards on `typeof labShader !== "undefined"`).
class LabShader : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString vizPath READ viz_path NOTIFY vizPathChanged)
  Q_PROPERTY(QString washPath READ wash_path NOTIFY washPathChanged)
  Q_PROPERTY(bool vizInset READ viz_inset NOTIFY vizInsetChanged)

public:
  using QObject::QObject;
  auto viz_path() const -> QString { return viz_path_; }
  auto wash_path() const -> QString { return wash_path_; }
  auto viz_inset() const -> bool { return viz_inset_; }
  auto set_viz_inset(bool inset) -> void
  {
    if (viz_inset_ == inset)
      return;
    viz_inset_ = inset;
    emit vizInsetChanged();
  }
  auto set_viz_path(const QString &p) -> void
  {
    viz_path_ = p;
    emit vizPathChanged();
  }
  auto set_wash_path(const QString &p) -> void
  {
    wash_path_ = p;
    emit washPathChanged();
  }

signals:
  void vizPathChanged();
  void washPathChanged();
  void vizInsetChanged();

private:
  QString viz_path_;
  QString wash_path_;
  bool viz_inset_ = false;
};

// Live shader test harness: hosts the real Now Playing QML scene fed by either
// the live PipeWire tap (when built with ENABLE_VISUALIZER) or a synthetic
// signal, watches res/shaders/*.frag, rebakes the edited shader with `qsb`, and
// hot-swaps it into the scene so edits are visible without rebuilding the app.
class ShaderLabWindow : public QWidget {
  Q_OBJECT

public:
  explicit ShaderLabWindow(QWidget *parent = nullptr);
  ~ShaderLabWindow() override;

private:
  auto build_controls() -> QWidget *;
  auto rescan_shaders() -> void;       // (re)populate the picker from disk
  auto apply_layout() -> void;         // fill vs inset for the active viz shader
  auto select_shader(int idx) -> void; // preset combo changed
  auto recompile_active() -> void;     // bake + apply the current shader
  auto on_file_changed(const QString &path) -> void;
  auto on_source_changed() -> void; // live vs synthetic
  auto tick() -> void;              // synthetic frame
  auto feed_history(const QList<qreal> &bands) -> void;
  auto regenerate_cover() -> void;
  auto pick_cover() -> void;
  auto pick_accent() -> void;
  auto log(const QString &msg) -> void;

  // Bake `frag` to a unique .qsb in the temp dir; on success returns true and
  // fills `out_url` with a file:// URL, else false and fills `error`.
  auto bake(const QString &frag, QString &out_url, QString &error) -> bool;

  NowPlayingModel *model = nullptr;
  LabShader *lab = nullptr;
  QQuickWidget *quick = nullptr;
  CoverProvider *cover_provider = nullptr; // owned by the QML engine
  int cover_seq = 0;
  int band_count = 64;

  std::unique_ptr<SynthHistory> history;
  QElapsedTimer history_clock;

  std::unique_ptr<QTemporaryDir> tmp;
  int bake_seq = 0;
  QFileSystemWatcher *watcher = nullptr;
  QString active_frag; // absolute path of the shader being previewed
  bool active_is_wash = false;
  bool rescan_queued = false; // debounce coalescing for directoryChanged

  QTimer *synth_clock = nullptr;
  double synth_t = 0.0;
  double progress_pos = 0.0;
  bool live = false;

#ifdef ENABLE_VISUALIZER
  VisualizerController *visualizer = nullptr;
#endif

  // Controls.
  QComboBox *preset_combo = nullptr;
  QComboBox *source_combo = nullptr;
  QComboBox *layout_combo = nullptr;
  QLabel *status = nullptr;
  QPlainTextEdit *log_view = nullptr;
  QSlider *speed_slider = nullptr;
  QSlider *intensity_slider = nullptr;
  QCheckBox *viz_box = nullptr;
  QColor accent;
};

#endif // SHADERLABWINDOW_HH
