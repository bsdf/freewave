#include "shaderlabwindow.hh"

#include "ui/nowplayingmodel.hh"
#include "ui/theme/tokens.hh"
#include "ui/themebridge.hh"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QLibraryInfo>
#include <QLinearGradient>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickWidget>
#include <QRadialGradient>
#include <QSlider>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef ENABLE_VISUALIZER
#include "controller/visualizercontroller.hh"
#endif

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr int HIST_ROWS = 192;        // mirrors NowPlayingView (ring height)
constexpr double ROWS_PER_SEC = 22.0; // mirrors NowPlayingView (scroll speed)

// Known metadata for the shipped shaders: a nicer label, the background flag,
// and the natural layout (inset = the ridgeline's "Unknown Pleasures" framing).
// The picker is built by scanning the directory, so any *new* .frag is picked up
// automatically and falls through to a generic full-bleed viz entry.
struct Known {
  const char *file;
  const char *label;
  bool is_wash;
  bool inset;
};
const Known KNOWN[] = {
    {"albumwash.frag", "Album Wash", false, false},
    {"coverfield.frag", "Cover Field", false, false},
    {"ridgeline.frag", "Ridgeline", false, true},
    {"waterfall.frag", "Waterfall", false, false},
    {"aurora.frag", "Aurora", false, false},
    {"wash.frag", "Background wash", true, false},
};

auto
known_for(const QString &file) -> const Known *
{
  for (const auto &k : KNOWN)
    if (file == QLatin1String(k.file))
      return &k;
  return nullptr;
}
} // namespace

// Scrolling-spectrogram ring buffer feeding history-based shaders. Mirrors the
// VizHistory in nowplayingview.cc so the harness drives the `history` sampler
// the same way the app does (newest row at (head-1) mod rows; frac slides).
class SynthHistory {
public:
  SynthHistory(int bands, int rows, double rows_per_sec)
    : img(bands, rows, QImage::Format_Grayscale8)
    , rps(rows_per_sec)
  {
    img.fill(0);
  }

  auto advance(const QList<qreal> &bands, double dt) -> void
  {
    scroll_pos += dt * rps;
    const int target = static_cast<int>(std::floor(scroll_pos));
    while (committed < target)
      {
        write_row(committed % img.height(), bands);
        ++committed;
        ++rev;
      }
  }

  auto image() const -> QImage { return img; }
  auto revision() const -> int { return rev; }
  auto head() const -> int { return committed; }
  auto frac() const -> double { return scroll_pos - committed; }

private:
  auto write_row(int row, const QList<qreal> &bands) -> void
  {
    uchar *p = img.scanLine(row);
    for (int i = 0; i < img.width(); ++i)
      {
        double v = (i < bands.size()) ? std::clamp(double(bands[i]), 0.0, 1.0) : 0.0;
        p[i] = static_cast<uchar>(v * 255.0 + 0.5);
      }
  }

  QImage img;
  double rps;
  double scroll_pos = 0.0;
  int committed = 0;
  int rev = 0;
};

// Serves the (synthetic or user-loaded) cover to the QML `fwnp` provider.
class CoverProvider : public QQuickImageProvider {
public:
  CoverProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
  {
  }
  auto set_pixmap(const QPixmap &px) -> void { current = px; }
  auto requestPixmap(const QString &, QSize *size, const QSize &) -> QPixmap override
  {
    if (size)
      *size = current.size();
    return current;
  }

private:
  QPixmap current;
};

// Serves the spectrogram ring buffer to the QML `fwvizhist` provider.
class HistoryProvider : public QQuickImageProvider {
public:
  explicit HistoryProvider(SynthHistory *h)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , hist(h)
  {
  }
  auto requestImage(const QString &, QSize *size, const QSize &) -> QImage override
  {
    QImage img = hist->image();
    if (size)
      *size = img.size();
    return img;
  }

private:
  SynthHistory *hist;
};

ShaderLabWindow::ShaderLabWindow(QWidget *parent)
  : QWidget(parent)
  , accent(theme::tok::accent)
{
  setWindowTitle(QStringLiteral("Freewave Shader Lab"));

  model = new NowPlayingModel(this);
  lab = new LabShader(this);

#ifdef ENABLE_VISUALIZER
  visualizer = new VisualizerController(this);
  band_count = visualizer->band_count();
  connect(visualizer, &VisualizerController::bands_ready, this,
      [this](const QList<qreal> &bands) {
        model->set_bands(bands);
        feed_history(bands);
      });
  connect(visualizer, &VisualizerController::level_ready,
      model, &NowPlayingModel::set_level);
#endif

  history = std::make_unique<SynthHistory>(band_count, HIST_ROWS, ROWS_PER_SEC);

  // Seed the scene state the QML binds to.
  model->set_band_count(band_count);
  model->set_history_rows(HIST_ROWS);
  model->set_visualizer_enabled(true);
  model->set_accent(accent);
  model->set_track(QStringLiteral("Shader Lab"), QStringLiteral("Live Preview"));
  model->set_album(QStringLiteral("res/shaders"), QStringLiteral("2026"));
  model->set_progress(0, 240);

  cover_provider = new CoverProvider();

  quick = new QQuickWidget(this);
  quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
  quick->setClearColor(theme::tok::npwash::tint_far);
  quick->engine()->addImageProvider(QStringLiteral("fwnp"), cover_provider);
  quick->engine()->addImageProvider(QStringLiteral("fwvizhist"),
      new HistoryProvider(history.get()));
  quick->rootContext()->setContextProperty(QStringLiteral("np"), model);
  quick->rootContext()->setContextProperty(QStringLiteral("Theme"),
      new ThemeBridge(model));
  quick->rootContext()->setContextProperty(QStringLiteral("labShader"), lab);

  regenerate_cover();
  quick->setSource(QUrl(QStringLiteral("qrc:/qml/NowPlaying.qml")));

  // Synthetic frame clock (used when the source is "Synthetic", or always when
  // built without the visualizer).
  synth_clock = new QTimer(this);
  synth_clock->setInterval(16);
  connect(synth_clock, &QTimer::timeout, this, &ShaderLabWindow::tick);

  tmp = std::make_unique<QTemporaryDir>();

  watcher = new QFileSystemWatcher(this);
  // Watch the directory too, so .frag files added/removed at runtime are picked
  // up (file watches for the individual shaders are added in rescan_shaders).
  watcher->addPath(QStringLiteral(SHADER_SRC_DIR));
  connect(watcher, &QFileSystemWatcher::fileChanged, this,
      &ShaderLabWindow::on_file_changed);
  // Debounce: atomic-save editors (write-temp + rename, or backup) briefly remove
  // the file from the listing, firing several directoryChanged events. Coalesce
  // them and let the filesystem settle before rescanning.
  connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
    if (rescan_queued)
      return;
    rescan_queued = true;
    QTimer::singleShot(120, this, [this]() {
      rescan_queued = false;
      rescan_shaders();
    });
  });

  auto *split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(quick);
  split->addWidget(build_controls());
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 0);
  split->setSizes({900, 360});

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->addWidget(split);

  // Default to the live tap when available, else synthetic. rescan_shaders()
  // populates the picker and makes the initial selection.
  on_source_changed();
  rescan_shaders();
}

ShaderLabWindow::~ShaderLabWindow()
{
  // Tear the scene down while `model`/`lab` are still alive (bindings reference
  // them); the QQuickWidget would otherwise re-evaluate against freed objects.
  quick->setSource(QUrl());
}

auto
ShaderLabWindow::build_controls() -> QWidget *
{
  auto *panel = new QWidget(this);
  auto *col = new QVBoxLayout(panel);
  col->setContentsMargins(14, 14, 14, 14);
  col->setSpacing(10);

  auto *form = new QFormLayout();
  form->setLabelAlignment(Qt::AlignLeft);

  preset_combo = new QComboBox(panel);
  // Items are filled by rescan_shaders() (scans res/shaders for *.frag).
  connect(preset_combo, &QComboBox::currentIndexChanged, this,
      &ShaderLabWindow::select_shader);
  form->addRow(QStringLiteral("Shader"), preset_combo);

  layout_combo = new QComboBox(panel);
  layout_combo->addItem(QStringLiteral("Fill"));
  layout_combo->addItem(QStringLiteral("Inset"));
  connect(layout_combo, &QComboBox::currentIndexChanged, this,
      [this]() { apply_layout(); });
  form->addRow(QStringLiteral("Layout"), layout_combo);

  source_combo = new QComboBox(panel);
#ifdef ENABLE_VISUALIZER
  source_combo->addItem(QStringLiteral("Live (PipeWire)"));
#endif
  source_combo->addItem(QStringLiteral("Synthetic"));
  connect(source_combo, &QComboBox::currentIndexChanged, this,
      [this]() { on_source_changed(); });
  form->addRow(QStringLiteral("Signal"), source_combo);

  viz_box = new QCheckBox(QStringLiteral("Visualizer enabled"), panel);
  viz_box->setChecked(true);
  connect(viz_box, &QCheckBox::toggled, model,
      &NowPlayingModel::set_visualizer_enabled);
  form->addRow(QString(), viz_box);

  speed_slider = new QSlider(Qt::Horizontal, panel);
  speed_slider->setRange(0, 200);
  speed_slider->setValue(100);
  form->addRow(QStringLiteral("Synth speed"), speed_slider);

  intensity_slider = new QSlider(Qt::Horizontal, panel);
  intensity_slider->setRange(0, 150);
  intensity_slider->setValue(100);
  form->addRow(QStringLiteral("Synth intensity"), intensity_slider);

  col->addLayout(form);

  auto *accent_btn = new QPushButton(QStringLiteral("Accent color…"), panel);
  connect(accent_btn, &QPushButton::clicked, this, &ShaderLabWindow::pick_accent);
  col->addWidget(accent_btn);

  auto *cover_btn = new QPushButton(QStringLiteral("Load cover image…"), panel);
  connect(cover_btn, &QPushButton::clicked, this, &ShaderLabWindow::pick_cover);
  col->addWidget(cover_btn);

  auto *recompile_btn = new QPushButton(QStringLiteral("Recompile now"), panel);
  connect(recompile_btn, &QPushButton::clicked, this,
      &ShaderLabWindow::recompile_active);
  col->addWidget(recompile_btn);

  status = new QLabel(panel);
  status->setWordWrap(true);
  col->addWidget(status);

  log_view = new QPlainTextEdit(panel);
  log_view->setObjectName(QStringLiteral("log_view"));
  log_view->setReadOnly(true);
  col->addWidget(log_view, 1);

  panel->setMinimumWidth(320);
  return panel;
}

auto
ShaderLabWindow::rescan_shaders() -> void
{
  QDir dir(QStringLiteral(SHADER_SRC_DIR));
  const auto files = dir.entryList({QStringLiteral("*.frag")}, QDir::Files, QDir::Name);

  // Ensure every shader file is individually watched (the dir watch only tells
  // us files were added/removed, not edited).
  bool active_rewatched = false;
  for (const auto &f : files)
    {
      const QString p = dir.absoluteFilePath(f);
      if (!watcher->files().contains(p))
        {
          watcher->addPath(p);
          // The active file losing then regaining its watch means an editor
          // saved it via rename without a fileChanged — recompile to apply it.
          if (p == active_frag)
            active_rewatched = true;
        }
    }
  if (active_rewatched)
    QTimer::singleShot(0, this, &ShaderLabWindow::recompile_active);

  // Repopulate, keying off the actually-previewed shader (active_frag) rather
  // than transient combo state, so an atomic save that momentarily hides the
  // file never bumps the preview to another shader.
  const auto label_of = [](const QString &f) {
    const Known *k = known_for(f);
    return k ? QString::fromUtf8(k->label) : f;
  };
  const QString active = active_frag.isEmpty() ? QString() : QFileInfo(active_frag).fileName();

  const QSignalBlocker block(preset_combo);
  preset_combo->clear();
  for (const auto &f : files)
    preset_combo->addItem(label_of(f), f);

  if (!active.isEmpty())
    {
      const int idx = preset_combo->findData(active);
      if (idx >= 0)
        {
          preset_combo->setCurrentIndex(idx); // preserved — no preview change
          return;
        }
      if (QFileInfo::exists(active_frag))
        {
          // Transient race (mid-save): keep the selection so the preview holds;
          // a later rescan re-syncs once the file settles back into the listing.
          preset_combo->addItem(label_of(active), active);
          preset_combo->setCurrentIndex(preset_combo->count() - 1);
          return;
        }
      // active file genuinely deleted — fall through and pick the first shader.
    }

  if (preset_combo->count() > 0)
    {
      preset_combo->setCurrentIndex(0);
      select_shader(0); // first run, or the active shader was deleted
    }
}

auto
ShaderLabWindow::apply_layout() -> void
{
  lab->set_viz_inset(layout_combo->currentIndex() == 1);
}

auto
ShaderLabWindow::select_shader(int idx) -> void
{
  if (idx < 0 || idx >= preset_combo->count())
    return;
  const QString file = preset_combo->itemData(idx).toString();
  if (file.isEmpty())
    return;
  const Known *k = known_for(file);
  active_is_wash = k && k->is_wash;
  active_frag = QDir(QStringLiteral(SHADER_SRC_DIR)).absoluteFilePath(file);

  if (!active_is_wash)
    {
      const QSignalBlocker block(layout_combo);
      layout_combo->setCurrentIndex(k && k->inset ? 1 : 0);
      apply_layout();
      lab->set_wash_path(QString()); // stop overriding the background
      viz_box->setChecked(true);
    }
  else
    {
      // Editing the background — hide the viz overlay so it's actually visible.
      viz_box->setChecked(false);
      lab->set_viz_path(QString());
    }

  recompile_active();
}

auto
ShaderLabWindow::recompile_active() -> void
{
  if (active_frag.isEmpty())
    return;
  const QString name = QFileInfo(active_frag).fileName();
  QString url;
  QString err;
  if (bake(active_frag, url, err))
    {
      if (active_is_wash)
        lab->set_wash_path(url);
      else
        lab->set_viz_path(url);
      status->setText(QStringLiteral("✓ %1 compiled").arg(name));
      log(QStringLiteral("ok: %1").arg(name));
    }
  else
    {
      // Keep the last-good shader bound so the scene keeps rendering.
      status->setText(QStringLiteral("✗ %1 — see log").arg(name));
      log(QStringLiteral("ERROR %1:\n%2").arg(name, err.trimmed()));
    }
}

auto
ShaderLabWindow::bake(const QString &frag, QString &out_url, QString &error) -> bool
{
  QString qsb = QLibraryInfo::path(QLibraryInfo::BinariesPath) + QStringLiteral("/qsb");
  if (!QFileInfo::exists(qsb))
    qsb = QLibraryInfo::path(QLibraryInfo::LibraryExecutablesPath) + QStringLiteral("/qsb");
  if (!QFileInfo::exists(qsb))
    {
      error = QStringLiteral("qsb tool not found near the Qt installation");
      return false;
    }

  const QString out
      = tmp->filePath(QStringLiteral("%1.%2.qsb").arg(QFileInfo(frag).fileName()).arg(++bake_seq));

  // Match the target set qt6_add_shaders bakes in the app build.
  QProcess p;
  p.start(qsb, {QStringLiteral("--glsl"), QStringLiteral("100es,120,150"),
                   QStringLiteral("--hlsl"), QStringLiteral("50"),
                   QStringLiteral("--msl"), QStringLiteral("12"),
                   QStringLiteral("-o"), out, frag});
  if (!p.waitForStarted(3000))
    {
      error = QStringLiteral("could not start qsb: %1").arg(qsb);
      return false;
    }
  p.waitForFinished(15000);
  if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
    {
      error = QString::fromUtf8(p.readAllStandardError())
              + QString::fromUtf8(p.readAllStandardOutput());
      return false;
    }
  out_url = QUrl::fromLocalFile(out).toString();
  return true;
}

auto
ShaderLabWindow::on_file_changed(const QString &path) -> void
{
  // Editors that save atomically (write-temp + rename) drop the inode the
  // watcher held; re-add the path so subsequent saves still fire.
  if (!watcher->files().contains(path) && QFileInfo::exists(path))
    watcher->addPath(path);

  if (QFileInfo(path).absoluteFilePath() != QFileInfo(active_frag).absoluteFilePath())
    return;
  // Debounce: let the write settle before baking.
  QTimer::singleShot(80, this, &ShaderLabWindow::recompile_active);
}

auto
ShaderLabWindow::on_source_changed() -> void
{
  live = source_combo->currentText().startsWith(QStringLiteral("Live"));
#ifdef ENABLE_VISUALIZER
  if (live)
    {
      synth_clock->stop();
      history_clock.restart();
      visualizer->set_source(VisualizerController::Source::PipeWire);
      visualizer->set_active(true);
      log(QStringLiteral("source: live PipeWire tap (plays all system audio)"));
      return;
    }
  visualizer->set_active(false);
#endif
  synth_t = 0.0;
  synth_clock->start();
  log(QStringLiteral("source: synthetic"));
}

auto
ShaderLabWindow::tick() -> void
{
  const double dt = 0.016 * speed_slider->value() / 100.0;
  const double inten = intensity_slider->value() / 100.0;
  synth_t += dt;

  const double two_pi = 2.0 * std::numbers::pi_v<double>;
  const double beat = std::pow(0.5 + 0.5 * std::sin(synth_t * two_pi * 1.7), 6.0);

  QList<qreal> bands;
  bands.reserve(band_count);
  double sum = 0.0;
  for (int i = 0; i < band_count; ++i)
    {
      const double f = band_count > 1 ? double(i) / (band_count - 1) : 0.0;
      const double roll = std::exp(-f * 2.4); // bass-heavy
      const double wob = 0.5 + 0.5 * std::sin(synth_t * 2.0 + i * 0.55) * std::sin(synth_t * 0.7 + i * 0.13);
      double v = roll * (0.35 + 0.65 * wob) + beat * roll * 0.7;
      v = std::clamp(v * inten, 0.0, 1.0);
      bands.push_back(v);
      sum += v;
    }
  model->set_bands(bands);
  model->set_level(std::clamp(sum / std::max(1, band_count) * 1.8, 0.0, 1.0));
  feed_history(bands);

  progress_pos += dt;
  if (progress_pos > 240.0)
    progress_pos = 0.0;
  model->set_progress(int(progress_pos), 240);
}

auto
ShaderLabWindow::feed_history(const QList<qreal> &bands) -> void
{
  const double dt = std::clamp(history_clock.restart() / 1000.0, 0.0, 0.1);
  history->advance(bands, dt);
  model->set_history(history->revision(), history->head(), history->frac());
}

auto
ShaderLabWindow::regenerate_cover() -> void
{
  // Synthetic cover: an accent radial over a complementary diagonal, plus a few
  // discs — enough texture for cover-reactive shaders to chew on.
  QPixmap px(512, 512);
  px.fill(Qt::black);
  QPainter g(&px);
  g.setRenderHint(QPainter::Antialiasing, true);

  const QColor comp = QColor::fromHsv((accent.hue() + 180) % 360,
      accent.saturation(), accent.value());
  QLinearGradient lin(0, 0, 512, 512);
  lin.setColorAt(0.0, comp.darker(160));
  lin.setColorAt(1.0, accent.darker(220));
  g.fillRect(px.rect(), lin);

  QRadialGradient rad(180, 160, 360);
  rad.setColorAt(0.0, accent.lighter(140));
  rad.setColorAt(1.0, Qt::transparent);
  g.fillRect(px.rect(), rad);

  g.setPen(Qt::NoPen);
  g.setBrush(QColor(255, 255, 255, 26));
  g.drawEllipse(QPointF(360, 380), 150, 150);
  g.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 90));
  g.drawEllipse(QPointF(120, 400), 90, 90);
  g.end();

  cover_provider->set_pixmap(px);
  model->set_cover_id(QString::number(++cover_seq));
}

auto
ShaderLabWindow::pick_cover() -> void
{
  const QString file = QFileDialog::getOpenFileName(this,
      QStringLiteral("Load cover image"), QString(),
      QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
  if (file.isEmpty())
    return;
  QPixmap px(file);
  if (px.isNull())
    {
      log(QStringLiteral("could not load image: %1").arg(file));
      return;
    }
  cover_provider->set_pixmap(px.scaled(512, 512, Qt::KeepAspectRatioByExpanding,
      Qt::SmoothTransformation));
  model->set_cover_id(QString::number(++cover_seq));
}

auto
ShaderLabWindow::pick_accent() -> void
{
  const QColor c = QColorDialog::getColor(accent, this, QStringLiteral("Accent color"));
  if (!c.isValid())
    return;
  accent = c;
  model->set_accent(accent);
  regenerate_cover();
}

auto
ShaderLabWindow::log(const QString &msg) -> void
{
  log_view->appendPlainText(msg);
}
