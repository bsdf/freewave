#include "settingsview.hh"
#include "controller/settings.hh"
#include "segmentedcontrol.hh"
#include "theme.hh"
#include "ui/theme/components.hh"
#include "version.hh"

#include <QApplication>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

// ── helpers ───────────────────────────────────────────────────────────────

namespace {

const auto ISSUE_TRACKER_URL = QStringLiteral("https://github.com/bsdf/freewave/issues");

auto
make_section_header(const QString &title, QWidget *parent) -> QLabel *
{
  auto *lbl = new QLabel(title.toUpper(), parent);
  lbl->setFont(theme::type::mono_meta());
  lbl->setStyleSheet(
      "color: palette(dark); letter-spacing: 2px; "
      "padding-bottom: 8px; border-bottom: 1px solid palette(midlight);");
  lbl->setContentsMargins(0, 0, 0, 0);
  return lbl;
}

// hint_out, when given, receives the hint label so the caller can rewrite it
// later (the cache row's hint carries a number that changes).
auto
make_row(QWidget *parent, const QString &label, const QString &hint, QWidget *control,
    QLabel **hint_out = nullptr) -> QWidget *
{
  auto *row = new QWidget(parent);
  auto *rl = new QHBoxLayout(row);
  rl->setContentsMargins(0, 14, 0, 14);
  rl->setSpacing(24);

  auto *text_col = new QWidget(row);
  auto *tcl = new QVBoxLayout(text_col);
  tcl->setContentsMargins(0, 0, 0, 0);
  tcl->setSpacing(2);

  auto *lbl = new QLabel(label, text_col);
  auto lf = theme::ui_sans(14);
  lf.setWeight(QFont::Medium);
  lbl->setFont(lf);
  tcl->addWidget(lbl);

  if (!hint.isEmpty())
    {
      auto *hint_lbl = new QLabel(hint, text_col);
      hint_lbl->setFont(theme::ui_sans(12));
      hint_lbl->setForegroundRole(QPalette::Dark);
      hint_lbl->setWordWrap(true);
      tcl->addWidget(hint_lbl);
      if (hint_out) *hint_out = hint_lbl;
    }

  rl->addWidget(text_col, 1);
  rl->addWidget(control);

  auto *outer = new QWidget(parent);
  auto *ol = new QVBoxLayout(outer);
  ol->setContentsMargins(0, 0, 0, 0);
  ol->setSpacing(0);
  ol->addWidget(row);

  auto *sep = theme::ui::separator(Qt::Horizontal, outer);
  ol->addWidget(sep);

  return outer;
}

auto
make_icon_btn(const QString &icon, const QString &tip) -> QToolButton *
{
  auto *btn = new QToolButton;
  btn->setIcon(QIcon::fromTheme(icon, QIcon(QString(":/icons/%1.svg").arg(icon))));
  btn->setIconSize(QSize(16, 16));
  btn->setToolTip(tip);
  btn->setCursor(Qt::PointingHandCursor);
  btn->setAutoRaise(true);
  btn->setFixedSize(28, 28);
  btn->setStyleSheet(
      "QToolButton { border: none; border-radius: 4px; background: transparent; }"
      "QToolButton:hover { background: palette(link); }");
  return btn;
}

} // namespace

// ── SettingsView ──────────────────────────────────────────────────────────

SettingsView::SettingsView(QWidget *parent)
  : QWidget(parent)
{
  Settings settings_facade;

  // ── Fixed header bar ───────────────────────────────────────────────────
  auto *header = new QWidget(this);
  header->setAutoFillBackground(true);
  header->setBackgroundRole(QPalette::Window);

  auto *header_layout = new QVBoxLayout(header);
  // Canonical back-button placement: 28px left / 14px top from the view origin,
  // matching AlbumView (albumview.ui), PlaylistsView, and NowPlaying.qml so the
  // chevron lands in the identical spot on every screen. Spacing is 20 (not 14)
  // to hold the title block where it was after the top margin dropped from 20.
  header_layout->setContentsMargins(28, 14, 28, 16);
  header_layout->setSpacing(20);

  // Back button (top-left), consistent with the other views.
  auto *back_btn = theme::ui::back_button("Back", header);
  connect(back_btn, &QPushButton::clicked, this, &SettingsView::close_requested);
  header_layout->addWidget(back_btn, 0, Qt::AlignLeft);

  // Title block
  auto *title_block = new QWidget(header);
  auto *title_block_layout = new QVBoxLayout(title_block);
  title_block_layout->setContentsMargins(0, 0, 0, 0);
  title_block_layout->setSpacing(4);

  auto *prefs_lbl = theme::ui::section_header("PREFERENCES", title_block);
  prefs_lbl->setStyleSheet("letter-spacing: 2px;");
  title_block_layout->addWidget(prefs_lbl);

  auto *title_lbl = new QLabel("Settings", title_block);
  auto title_font = theme::ui_sans(30);
  title_font.setWeight(QFont::DemiBold);
  title_lbl->setFont(title_font);
  title_lbl->setStyleSheet("letter-spacing: -0.5px;");
  title_block_layout->addWidget(title_lbl);

  header_layout->addWidget(title_block);

  // ── Scroll area wraps all content ─────────────────────────────────────
  auto *scroll = new QScrollArea(this);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  auto *content = new QWidget;
  auto *content_layout = new QVBoxLayout(content);
  content_layout->setContentsMargins(36, 30, 36, 40);
  content_layout->setSpacing(0);

  // ── Servers section ───────────────────────────────────────────────────
  content_layout->addWidget(make_section_header("Servers", content));

  auto *servers_container = new QWidget(content);
  servers_layout = new QVBoxLayout(servers_container);
  servers_layout->setContentsMargins(0, 0, 0, 0);
  servers_layout->setSpacing(0);
  content_layout->addWidget(servers_container);

  content_layout->addSpacing(8);
  auto *add_btn = theme::ui::outline_pill_button("Add Server…", content);
  connect(add_btn, &QPushButton::clicked, this, &SettingsView::add_profile_requested);
  content_layout->addWidget(add_btn, 0, Qt::AlignLeft);

  // ── Appearance section ────────────────────────────────────────────────
  content_layout->addSpacing(28);
  content_layout->addWidget(make_section_header("Appearance", content));
  content_layout->addSpacing(0);

  rounded_cb = new Toggle(content);
  rounded_cb->setChecked(settings_facade.rounded_corners());
  connect(rounded_cb, &Toggle::toggled, this, &SettingsView::rounded_corners_changed);
  content_layout->addWidget(make_row(content, "Rounded album corners", "", rounded_cb));

  shadow_cb = new Toggle(content);
  shadow_cb->setChecked(settings_facade.drop_shadow());
  connect(shadow_cb, &Toggle::toggled, this, &SettingsView::drop_shadow_changed);
  content_layout->addWidget(make_row(content, "Album cover drop shadow", "", shadow_cb));

  bootleg_cb = new Toggle(content);
  bootleg_cb->setChecked(settings_facade.bootleg_stamp());
  connect(bootleg_cb, &Toggle::toggled, this, &SettingsView::bootleg_stamp_changed);
  content_layout->addWidget(make_row(content, "Bootleg stamp",
      "Stamp \"BOOTLEG\" on cover-less live releases.", bootleg_cb));

  // Queue display segmented control
  auto *qd_control = new SegmentedControl(content);
  qd_control->add_segment(0, "Compact");
  qd_control->add_segment(1, "Full");
  qd_control->add_segment(2, "Grouped");
  qd_control->add_segment(3, "Auto");
  QString qd_saved = settings_facade.queue_display();
  qd_control->set_current(qd_saved == "full"      ? 1
                          : qd_saved == "grouped" ? 2
                          : qd_saved == "auto"    ? 3
                                                  : 0);
  connect(qd_control, &SegmentedControl::selected, this, [this](int id) {
    static const char *modes[] = {"compact", "full", "grouped", "auto"};
    emit queue_display_changed(modes[id]);
  });
  content_layout->addWidget(make_row(content, "Queue display", "How tracks are listed in the queue panel.", qd_control));

  // Queue panel segmented control
  auto *qs_control = new SegmentedControl(content);
  qs_control->add_segment(0, "Light");
  qs_control->add_segment(1, "Dark");
  qs_control->set_current(settings_facade.queue_style() == "dark" ? 1 : 0);
  connect(qs_control, &SegmentedControl::selected, this, [this](int id) {
    emit queue_style_changed(id == 1 ? "dark" : "light");
  });
  content_layout->addWidget(make_row(content, "Queue panel", "Color style of the queue panel.", qs_control));

  color_band_cb = new Toggle(content);
  color_band_cb->setChecked(settings_facade.queue_color_band());
  connect(color_band_cb, &Toggle::toggled, this, &SettingsView::color_band_changed);
  content_layout->addWidget(make_row(content, "Queue color band", "Accent bar down the left edge of each track.", color_band_cb));

  // ── Library section ───────────────────────────────────────────────────
  content_layout->addSpacing(28);
  content_layout->addWidget(make_section_header("Library", content));

  poll_spin = new QSpinBox(content);
  poll_spin->setRange(0, 60);
  poll_spin->setSuffix(" min");
  poll_spin->setSpecialValueText("Off");
  poll_spin->setValue(settings_facade.poll_interval());
  poll_spin->setFixedWidth(100);
  connect(poll_spin, &QSpinBox::valueChanged, this, &SettingsView::poll_interval_changed);
  poll_row = make_row(content, "Auto-refresh interval", "Rescan library on a timer.", poll_spin);
  content_layout->addWidget(poll_row);

  // ── System Tray section ───────────────────────────────────────────────
  tray_section = new QWidget(content);
  auto *tray_layout = new QVBoxLayout(tray_section);
  tray_layout->setContentsMargins(0, 28, 0, 0);
  tray_layout->setSpacing(0);
  tray_layout->addWidget(make_section_header("System Tray", tray_section));

  tray_enabled_cb = new Toggle(tray_section);
  tray_enabled_cb->setObjectName("tray_enabled_cb"); // test hook: no distinguishing text/tooltip otherwise
  tray_enabled_cb->setChecked(settings_facade.tray_enabled());
  connect(tray_enabled_cb, &Toggle::toggled, this, [this](bool v) {
    close_to_tray_cb->setEnabled(v);
    emit tray_enabled_changed(v);
  });
  tray_layout->addWidget(make_row(tray_section, "Show tray icon",
      "Adds a freewave icon to the system tray.", tray_enabled_cb));

  close_to_tray_cb = new Toggle(tray_section);
  close_to_tray_cb->setObjectName("close_to_tray_cb"); // test hook: no distinguishing text/tooltip otherwise
  close_to_tray_cb->setChecked(settings_facade.close_to_tray());
  close_to_tray_cb->setEnabled(tray_enabled_cb->isChecked());
  connect(close_to_tray_cb, &Toggle::toggled, this, &SettingsView::close_to_tray_changed);
  tray_layout->addWidget(make_row(tray_section, "Close to tray",
      "Closing the window keeps freewave running in the tray instead of quitting.",
      close_to_tray_cb));

  content_layout->addWidget(tray_section);

  // ── Storage section ───────────────────────────────────────────────────
  storage_section = new QWidget(content);
  auto *storage_layout = new QVBoxLayout(storage_section);
  storage_layout->setContentsMargins(0, 28, 0, 0);
  storage_layout->setSpacing(0);
  storage_layout->addWidget(make_section_header("Storage", storage_section));

  audio_cache_cb = new Toggle(storage_section);
  audio_cache_cb->setChecked(settings_facade.audio_cache_enabled());
  connect(audio_cache_cb, &Toggle::toggled, this,
      &SettingsView::audio_cache_enabled_changed);
  storage_layout->addWidget(make_row(storage_section, "Keep played tracks",
      "Play a track you have already heard from disk, so seeking in it is instant.",
      audio_cache_cb));

  audio_cache_spin = new QSpinBox(storage_section);
  audio_cache_spin->setRange(256, 51200);
  audio_cache_spin->setSingleStep(512);
  audio_cache_spin->setSuffix(" MiB");
  audio_cache_spin->setValue(settings_facade.audio_cache_budget_mb());
  audio_cache_spin->setFixedWidth(120);
  connect(audio_cache_spin, &QSpinBox::valueChanged, this,
      &SettingsView::audio_cache_budget_changed);
  storage_layout->addWidget(make_row(storage_section, "Cache limit",
      "Oldest tracks are dropped first.", audio_cache_spin));

  auto *clear_cache_btn = theme::ui::outline_pill_button("Clear Cache", storage_section);
  connect(clear_cache_btn, &QPushButton::clicked, this,
      &SettingsView::clear_audio_cache_requested);
  storage_layout->addWidget(make_row(storage_section, "Cached audio",
      "Nothing cached yet.", clear_cache_btn, &audio_cache_usage_lbl));

  content_layout->addWidget(storage_section);

  // ── Debug section ─────────────────────────────────────────────────────
  content_layout->addSpacing(28);
  content_layout->addWidget(make_section_header("Debug", content));

  auto *log_btn = theme::ui::outline_pill_button("Open Log…", content);
  connect(log_btn, &QPushButton::clicked, this, &SettingsView::open_log_requested);
  content_layout->addWidget(make_row(content, "Application log", "View diagnostic messages.", log_btn));

  auto *bug_btn = theme::ui::outline_pill_button("Report a Bug…", content);
  connect(bug_btn, &QPushButton::clicked, this,
      [] { QDesktopServices::openUrl(QUrl(ISSUE_TRACKER_URL)); });
  content_layout->addWidget(make_row(
      content, "Report a bug",
      "Opens the issue tracker. Include your server and the application log.", bug_btn));

  // Version info
  content_layout->addSpacing(30);
  auto *ver_lbl = new QLabel("freewave ～ " FREEWAVE_VERSION " ～ " FREEWAVE_GIT_SHA, content);
  ver_lbl->setFont(theme::type::mono_meta());
  ver_lbl->setStyleSheet("color: palette(mid); letter-spacing: 1px;");
  content_layout->addWidget(ver_lbl);

  content_layout->addStretch();

  scroll->setWidget(content);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  root->addWidget(header);
  // A selector-less QSS border-bottom on header would cascade to every
  // QFrame-derived descendant (the title labels) — same trap as the
  // LoadingView status bar. The hairline is a real separator widget.
  root->addWidget(theme::ui::separator(Qt::Horizontal, this));
  root->addWidget(scroll);
}

auto
SettingsView::rounded_corners() const -> bool
{
  return rounded_cb->isChecked();
}

auto
SettingsView::drop_shadow() const -> bool
{
  return shadow_cb->isChecked();
}

auto
SettingsView::poll_interval_minutes() const -> int
{
  return poll_spin->value();
}

auto
SettingsView::set_poll_supported(bool supported) -> void
{
  poll_row->setVisible(supported);
}

auto
SettingsView::set_audio_cache_supported(bool supported) -> void
{
  storage_section->setVisible(supported);
}

auto
SettingsView::set_tray_supported(bool supported) -> void
{
  tray_section->setVisible(supported);
}

auto
SettingsView::set_audio_cache_usage(qint64 bytes) -> void
{
  if (!audio_cache_usage_lbl) return;

  if (bytes <= 0)
    {
      audio_cache_usage_lbl->setText("Nothing cached yet.");
      return;
    }

  constexpr double MIB = 1024.0 * 1024.0;
  const double mib = static_cast<double>(bytes) / MIB;
  audio_cache_usage_lbl->setText(mib >= 1024.0
                                     ? QString("Holding %1 GiB.").arg(mib / 1024.0, 0, 'f', 1)
                                     : QString("Holding %1 MiB.").arg(mib, 0, 'f', 0));
}

auto
SettingsView::set_profiles(const QList<BackendProfile> &profiles, const QString &active_id,
    bool connected) -> void
{
  // Clear existing rows
  while (QLayoutItem *item = servers_layout->takeAt(0))
    {
      delete item->widget();
      delete item;
    }

  for (const BackendProfile &p : profiles)
    {
      const bool is_active = (p.id == active_id);
      const bool is_live = is_active && connected;

      auto *row = new QWidget;
      auto *rl = new QHBoxLayout(row);
      rl->setContentsMargins(0, 10, 0, 10);
      rl->setSpacing(10);

      // Filled only for a profile that actually reached its server: an active
      // profile whose connection failed must not read as connected.
      auto *dot = new QLabel(is_live ? "●" : "○");
      dot->setFont(theme::ui_sans(12));
      dot->setForegroundRole(is_live ? QPalette::Highlight : QPalette::Mid);
      dot->setFixedWidth(16);
      rl->addWidget(dot);

      // Name with the type badge attached inline after it
      auto *name_lbl = new QLabel(p.name);
      auto nf = theme::ui_sans(13);
      if (is_active)
        nf.setWeight(QFont::Medium);
      name_lbl->setFont(nf);
      rl->addWidget(name_lbl);

      auto *badge = theme::ui::chip(p.type.toUpper(), nullptr, /*compact=*/true);
      rl->addWidget(badge, 0, Qt::AlignVCenter);

      rl->addStretch(1);

      const QString pid = p.id;

      // Offered unless the profile is already live: an active profile that failed
      // to connect needs a way back in, or the row is a dead end.
      if (!is_live)
        {
          auto *conn_btn = theme::ui::outline_pill_button(is_active ? "Retry" : "Connect");
          connect(conn_btn, &QPushButton::clicked, this, [this, pid] {
            emit switch_backend_requested(pid);
          });
          rl->addWidget(conn_btn);
        }

      // Edit + Remove as standard icon buttons
      auto *edit_btn = make_icon_btn("document-edit", "Edit");
      connect(edit_btn, &QToolButton::clicked, this, [this, pid] {
        emit edit_profile_requested(pid);
      });
      rl->addWidget(edit_btn);

      auto *rem_btn = make_icon_btn("edit-delete", "Remove");
      rem_btn->setEnabled(!is_active && profiles.size() > 1);
      connect(rem_btn, &QToolButton::clicked, this, [this, pid] {
        emit remove_profile_requested(pid);
      });
      rl->addWidget(rem_btn);

      // Separator
      auto *outer = new QWidget;
      auto *ol = new QVBoxLayout(outer);
      ol->setContentsMargins(0, 0, 0, 0);
      ol->setSpacing(0);
      ol->addWidget(row);
      auto *sep = theme::ui::separator(Qt::Horizontal);
      ol->addWidget(sep);

      servers_layout->addWidget(outer);
    }

  if (profiles.isEmpty())
    {
      auto *empty = new QLabel("No servers configured.");
      empty->setFont(theme::ui_sans(12));
      empty->setForegroundRole(QPalette::Mid);
      empty->setContentsMargins(0, 10, 0, 0);
      servers_layout->addWidget(empty);
    }
}
