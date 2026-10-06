#include "mainwindow.hh"
#include "./ui_mainwindow.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QFrame>
#include <QIcon>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QShortcut>
#include <QStandardPaths>
#include <QSettings>
#include <QStyle>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/qt_sinks.h>
#include "controller/settings.hh"
#include "controller/backendcontroller.hh"
#include "controller/profilestore.hh"
#include "fwmark.hh"
#include "searchbox.hh"
#include "theme.hh"
#include "ui/theme/components.hh"
#include "connectionstatecontroller.hh"
#include "profileeditdialog.hh"
#include "addtoplaylistdialog.hh"
#include "trayicon.hh"
#ifdef ENABLE_MPRIS
#include "controller/mprisbridge.hh"
#endif

namespace {
// Recolor a monochrome bundled SVG to `color` (its currentColor doesn't follow
// the palette). Tinted once at construction, matching the icon buttons in
// playback.cc — the app doesn't live-retint icons on a palette change.
auto
tinted_icon(const QString &res, const QColor &color, int px, qreal dpr) -> QIcon
{
  QPixmap base = QIcon(res).pixmap(QSize(px, px), dpr);
  if (base.isNull())
    return QIcon(res);
  QPixmap out(base.size());
  out.setDevicePixelRatio(dpr);
  out.fill(Qt::transparent);
  QPainter p(&out);
  p.drawPixmap(0, 0, base);
  p.setCompositionMode(QPainter::CompositionMode_SourceIn);
  p.fillRect(out.rect(), color);
  p.end();
  return QIcon(out);
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , ui(std::make_unique<Ui::MainWindow>())
{
  log_window = new LogWindow(this);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto qt_sink = std::make_shared<spdlog::sinks::qt_sink_mt>(
      log_window->text_edit(), "appendPlainText");
  auto logger = std::make_shared<spdlog::logger>(
      "freewave", spdlog::sinks_init_list{console_sink, qt_sink});
  logger->set_level(spdlog::level::debug);
  spdlog::set_default_logger(logger);
  spdlog::cfg::load_env_levels();

  setup_widgets();
  setup_connections();
  setup_actions();

  QSettings settings;
  Settings settings_facade;
  if (settings_facade.has_geometry())
    restoreGeometry(settings_facade.geometry());
  const bool queue_visible = settings_facade.ui_queue_visible();
  queue_panel_ctrl->set_initial_visible(queue_visible);
  // Drive the button from the setting, not the panel's isVisible(): the window
  // isn't shown yet here, so isVisible() returns false even after setVisible(true).
  playback_view->set_queue_active(queue_visible);
  ui->albumlist->set_rounded_corners(settings_facade.rounded_corners());
  ui->albumlist->set_drop_shadow(settings_facade.drop_shadow());
  tray_icon->set_enabled(settings_facade.tray_enabled());

  // Show the loading overlay up front, then kick off the initial connection.
  conn_states->start();
}

MainWindow::~MainWindow() = default;

auto
MainWindow::setup_widgets() -> void
{
  ui->setupUi(this);

  // Fonts for .ui-authored widgets (kept out of the .ui QSS so they flow
  // through the theme layer like the rest of the app).
  {
    auto logo_font = theme::ui_sans(13);
    logo_font.setWeight(QFont::DemiBold);
    ui->logo_label->setFont(logo_font);
    auto title_font = theme::display_serif(22);
    title_font.setWeight(QFont::DemiBold);
    title_font.setLetterSpacing(QFont::AbsoluteSpacing, -0.4);
    ui->library_title->setFont(title_font);
    ui->filter_frame->setFont(theme::ui_sans(11)); // propagates to all/recent buttons
    ui->album_count_label->setFont(theme::mono(11));
  }

  libman = std::make_shared<LibraryManager>();

  backend = std::make_shared<BackendController>(libman);
  favman = std::make_shared<FavoritesManager>(backend);
  plman = std::make_shared<PlaylistsManager>(backend);
  artman = std::make_shared<AlbumArtManager>(
      QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)),
      backend.get());

#ifdef ENABLE_MPRIS
  mpris_bridge = std::make_unique<MprisBridge>(
      backend.get(),
      QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)),
      libman.get(), this);
  if (!mpris_bridge->register_on_bus())
    spdlog::warn("MPRIS: could not register on the session bus");
  connect(artman.get(), &AlbumArtManager::art_ready,
      mpris_bridge.get(), &MprisBridge::on_art_ready);
#endif

  albumview = std::make_unique<AlbumView>(artman, favman, ui->library_content);
  ui->library_content->layout()->addWidget(albumview.get());
  albumview->setVisible(false);

  empty_library_view = std::make_unique<EmptyLibraryView>(ui->library_content);
  ui->library_content->layout()->addWidget(empty_library_view.get());
  empty_library_view->setVisible(false);

  lib_grid_ctrl = std::make_unique<LibraryGridController>(
      ui->albumlist, ui->recentbutton, ui->allbutton, ui->searchbox,
      ui->library_title, ui->album_count_label, ui->library_toolbar,
      ui->library_content, albumview.get(), empty_library_view.get(),
      backend.get(), artman);

  nowplaying_view = std::make_unique<NowPlayingView>(backend.get(), ui->mainarea_frame);
  ui->mainarea_frame->layout()->addWidget(nowplaying_view.get());
  nowplaying_view->setVisible(false);

  settings_view = std::make_unique<SettingsView>(ui->mainarea_frame);
  ui->mainarea_frame->layout()->addWidget(settings_view.get());
  settings_view->setVisible(false);
  settings_view->set_poll_supported(
      backend->supports(Backend::Feature::PollInterval));
  settings_view->set_audio_cache_supported(
      backend->supports(Backend::Feature::AudioCache));
  settings_view->set_tray_supported(TrayIcon::is_supported());

  tray_icon = std::make_unique<TrayIcon>(this, this);
  connect(tray_icon.get(), &TrayIcon::quit_requested, this, [this] {
    // close() alone isn't enough: with the window already hidden (or another
    // top-level like LogWindow still around), Qt's quit-on-last-window-closed
    // never fires. Run the normal teardown via close()/closeEvent, then quit
    // explicitly rather than relying on that heuristic.
    quitting = true;
    close();
    qApp->quit();
  });

  playlists_view = std::make_unique<PlaylistsView>(
      plman, favman, artman, libman, backend.get(), ui->mainarea_frame);
  ui->mainarea_frame->layout()->addWidget(playlists_view.get());
  playlists_view->setVisible(false);

  ui->playlists_button->setIcon(tinted_icon(":/icons/fw-playlist.svg",
      palette().color(QPalette::Dark), 16, devicePixelRatioF()));
  update_playlists_availability();

  ui->settings_gear_button->setText(QString{});
  ui->settings_gear_button->setIcon(tinted_icon(":/icons/fw-settings.svg",
      palette().color(QPalette::Dark), 16, devicePixelRatioF()));

  queue_panel_ctrl = std::make_unique<QueuePanelController>(
      ui->queuelist, ui->mainarea_frame, ui->central_frame,
      backend.get(), artman, libman, favman);

  playback_view = std::make_unique<PlaybackView>(favman);
  playback_view->setEnabled(false);
  player_separator = theme::ui::separator(Qt::Horizontal, this);
  ui->verticalLayout->addWidget(player_separator);
  ui->verticalLayout->addWidget(playback_view.get());

  clockman = std::make_unique<ClockMan>();

  // The legacy toolbar (refresh / status / queue buttons) is retired: the
  // library refreshes on its own and the playback bar toggles the queue.
  ui->toolbar->setVisible(false);

  // Baseline-align the album count to the big library title. Qt layouts have no
  // text-baseline alignment, so bottom-align both items (the count's stylesheet
  // adds a small bottom pad to drop its baseline onto the title's).
  auto *lib_toolbar_layout = qobject_cast<QHBoxLayout *>(ui->library_toolbar->layout());
  lib_toolbar_layout->setAlignment(ui->library_title, Qt::AlignBottom);
  lib_toolbar_layout->setAlignment(ui->album_count_label, Qt::AlignBottom);

  // Refresh button at the right edge of the library toolbar.
  auto *refresh_btn = theme::ui::outline_pill_button("Refresh", ui->library_toolbar);
  refresh_btn->setFont(theme::ui_sans(11));
  lib_toolbar_layout->insertWidget(lib_toolbar_layout->count() - 1, refresh_btn);
  connect(refresh_btn, &QPushButton::clicked, this, [this] {
    // An explicit refresh means "look again": forget cached no-art verdicts
    // so newly added covers are picked up even where the fetch uri is stable.
    artman->clear_noart_markers();
    backend->refresh_library();
  });

  // Replace the text logo with the FW mark
  auto *logo_layout = qobject_cast<QHBoxLayout *>(ui->logo_container->layout());
  ui->logo_label->hide();
  logo_layout->addWidget(new FWMark(22, ui->logo_container));
  logo_layout->addStretch();

  qApp->installEventFilter(this);

  // The search field owns its own chrome and focus/escape behavior (see SearchBox);
  // Escape clears it, then we move focus back to the album grid.
  connect(ui->searchbox, &SearchBox::escaped, this,
      [this] { ui->albumlist->setFocus(); });

  setup_state_overlays();
}

auto
MainWindow::setup_state_overlays() -> void
{
  conn_states = std::make_unique<ConnectionStateController>(
      ui->centralwidget, ui->mainarea_frame, backend.get(),
      [this] { return lib_grid_ctrl->album_count(); });
  connect(conn_states.get(), &ConnectionStateController::open_settings_requested,
      this, &MainWindow::show_settings);
  connect(conn_states.get(), &ConnectionStateController::library_ready, this, [this] {
    // A backend switch finished loading its library — navigate there if the user
    // is still sitting in Settings.
    if (settings_view->isVisible())
      show_library();
  });
}

auto
MainWindow::setup_connections() -> void
{
  // backend → mainwindow
  connect(backend.get(), &BackendController::backend_switching,
      this, &MainWindow::on_backend_switching);
  connect(backend.get(), &BackendController::backend_switched,
      this, [this] {
        settings_view->set_poll_supported(
            backend->supports(Backend::Feature::PollInterval));
        settings_view->set_audio_cache_supported(
            backend->supports(Backend::Feature::AudioCache));
      });
  // Before connection_update, matching the order the backends emit them in, so
  // the state machine has the reason in hand when the failure arrives.
  connect(backend.get(), &Backend::connection_failed,
      conn_states.get(), &ConnectionStateController::on_connection_failed);
  connect(backend.get(), &Backend::connection_update,
      this, &MainWindow::connection_updated);
  // A server disproved one of supports()' optimistic answers mid-session; the
  // controls it was backing have to come down now, not at the next reconnect.
  connect(backend.get(), &Backend::capabilities_changed,
      this, &MainWindow::capabilities_updated);
  connect(backend.get(), &Backend::library_load_failed,
      conn_states.get(), &ConnectionStateController::on_library_load_failed);
  // Queued deliberately: the signal is emitted from inside the backend's own
  // sslErrors handler, and trusting the certificate reconnects — which destroys
  // that very backend. Running the slot after the emission unwinds keeps us from
  // deleting the sender mid-signal.
  connect(backend.get(), &Backend::certificate_untrusted,
      this, &MainWindow::prompt_untrusted_certificate, Qt::QueuedConnection);
  connect(backend.get(), &Backend::playback_state_changed,
      this, &MainWindow::playback_state_updated);
  // backend -> shared clock -> playbackview / nowplayingview
  connect(backend.get(), &Backend::playback_state_changed,
      clockman.get(), &ClockMan::sync);
  connect(clockman.get(), &ClockMan::tick,
      playback_view.get(), &PlaybackView::clock_tick);
  connect(clockman.get(), &ClockMan::tick,
      nowplaying_view.get(), &NowPlayingView::set_progress);
  // backend -> queue panel
  connect(queue_panel_ctrl.get(), &QueuePanelController::visibility_changed,
      playback_view.get(), &PlaybackView::set_queue_active);
  connect(backend.get(), &Backend::library_refresh_active,
      this, &MainWindow::library_refresh_updated);
  connect(backend.get(), &Backend::current_song_changed,
      [&](const song &s) { setWindowTitle(
                               QString("%1 ～ %2 ～ freewave").arg(s.artist, s.title)); });

  // backend → playbackview
  connect(backend.get(), &Backend::playback_state_changed,
      playback_view.get(), &PlaybackView::set_status);
  connect(backend.get(), &Backend::current_song_changed,
      playback_view.get(), &PlaybackView::set_current_song);
  connect(backend.get(), &Backend::current_song_changed,
      this, [this](const song &s) {
        current_album_hash = s.album_hash;
        playback_view->set_album_art(artman->get_art(s.album_hash, theme::tok::art::mini));
        lib_grid_ctrl->set_current_album(s.album_hash);
        playlists_view->set_current_uri(s.uri);
      });

  // playbackview → backend
  connect(playback_view.get(), &PlaybackView::seek,
      [&](uint64_t pos_ms) { backend->seek(static_cast<uint32_t>(pos_ms)); });
  connect(playback_view.get(), &PlaybackView::seek,
      clockman.get(), &ClockMan::seek_to);
  connect(playback_view.get(), &PlaybackView::play,
      backend.get(), &Backend::play);
  connect(playback_view.get(), &PlaybackView::pause,
      backend.get(), &Backend::pause);
  connect(playback_view.get(), &PlaybackView::stop,
      backend.get(), &Backend::stop);
  connect(playback_view.get(), &PlaybackView::prev,
      backend.get(), &Backend::prev);
  connect(playback_view.get(), &PlaybackView::next,
      backend.get(), &Backend::next);
  connect(playback_view.get(), &PlaybackView::repeat_state,
      backend.get(), &Backend::set_repeat);
  connect(playback_view.get(), &PlaybackView::shuffle_state,
      backend.get(), &Backend::set_shuffle);
  connect(playback_view.get(), &PlaybackView::update_volume,
      [&](int vol) { backend->set_volume(vol); });

  // Library grid slice (grid + open-album view + All/Recent + search filter +
  // empty-state + scroll-to-playing) lives in LibraryGridController.
  connect(lib_grid_ctrl.get(), &LibraryGridController::library_loaded, this, [this] {
    // Sole art-rebuild trigger: the library cache is the only thing art depends
    // on, so every load refreshes it. Redundant calls are cheap — albums with
    // cached art or a no-art marker issue no request.
    conn_states->notify_library_loaded();
    emit rebuild_thumbnails(lib_grid_ctrl->albums());
    refresh_now_playing_art();
  });
  connect(lib_grid_ctrl.get(), &LibraryGridController::show_library_requested,
      this, &MainWindow::show_library);

  // queuelist + queue_model -> backend wiring lives in QueuePanelController.

  connect(this, &MainWindow::rebuild_thumbnails,
      artman.get(), &AlbumArtManager::rebuild_thumbnails);

  connect(ui->queuebutton, &QPushButton::clicked,
      queue_panel_ctrl.get(), &QueuePanelController::toggle_visible);

  connect(playback_view.get(), &PlaybackView::toggle_queue,
      queue_panel_ctrl.get(), &QueuePanelController::toggle_visible);
  connect(playback_view.get(), &PlaybackView::open_now_playing,
      this, &MainWindow::toggle_nowplaying);
  connect(nowplaying_view.get(), &NowPlayingView::close_requested,
      this, &MainWindow::show_library);
  connect(nowplaying_view.get(), &NowPlayingView::seek,
      [&](uint32_t pos_ms) { backend->seek(pos_ms); });
  connect(nowplaying_view.get(), &NowPlayingView::seek,
      clockman.get(), &ClockMan::seek_to);
  connect(nowplaying_view.get(), &NowPlayingView::fullscreen_toggle_requested,
      this, &MainWindow::toggle_fullscreen);

  // `F` toggles fullscreen Now Playing. Scoped to NP (like `L` below) so a bare
  // letter can't hijack the album grid's type-ahead; `F11` is the global variant.
  // `Esc` leaves fullscreen (no-op otherwise).
  auto *fullscreen_shortcut = new QShortcut(QKeySequence(Qt::Key_F), nowplaying_view.get());
  fullscreen_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(fullscreen_shortcut, &QShortcut::activated,
      this, &MainWindow::toggle_fullscreen);
  auto *fullscreen_escape = new QShortcut(QKeySequence(Qt::Key_Escape), nowplaying_view.get());
  fullscreen_escape->setContext(Qt::WidgetWithChildrenShortcut);
  connect(fullscreen_escape, &QShortcut::activated, this, [this] {
    if (fullscreen_active)
      toggle_fullscreen();
  });

  // `L` ("Love this track") toggles the current song's favorite. Scoped to the
  // Now Playing view (not a global action) so a bare letter can't hijack the
  // album grid's type-ahead search; Ctrl+L is already scroll-to-playing.
  // show_nowplaying() gives NP focus so the shortcut is live while it's open.
  nowplaying_view->setFocusPolicy(Qt::StrongFocus);
  auto *love_shortcut = new QShortcut(QKeySequence(Qt::Key_L), nowplaying_view.get());
  love_shortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(love_shortcut, &QShortcut::activated,
      playback_view.get(), &PlaybackView::toggle_current_favorite);

  connect(ui->update_library_button, &QPushButton::clicked, this, [this] {
    artman->clear_noart_markers();
    backend->refresh_library();
  });

  // Navigation: the gear opens Settings; each non-library view's back button
  // (and Now Playing's minimize) returns to the library. There are no nav tabs —
  // the top bar is logo ～ search ～ gear, matching the design.
  connect(ui->settings_gear_button, &QPushButton::clicked,
      this, &MainWindow::show_settings);
  connect(settings_view.get(), &SettingsView::close_requested,
      this, &MainWindow::show_library);

  // playlists_view: top-bar button opens it; its signals drive queue ops.
  connect(ui->playlists_button, &QPushButton::clicked,
      this, &MainWindow::show_playlists);
  connect(playlists_view.get(), &PlaylistsView::close_requested,
      this, &MainWindow::show_library);
  connect(playlists_view.get(), &PlaylistsView::play_songs,
      this, [this](uint32_t pos, const QList<song> &songs) {
        backend->replace_queue(songs, pos);
      });
  connect(playlists_view.get(), &PlaylistsView::play_songs_next,
      this, [this](const QList<song> &songs) {
        backend->insert_queue(songs, current_queue_pos + 1);
      });
  connect(playlists_view.get(), &PlaylistsView::queue_songs,
      this, [this](const QList<song> &songs) { backend->append_queue(songs); });

  // Add-to-playlist entry points → the modal picker.
  connect(albumview.get(), &AlbumView::add_album_to_playlist,
      this, [this](const album &a) {
        open_add_to_playlist({playlist_subject::Kind::Album, a, {}});
      });
  connect(albumview.get(), &AlbumView::add_song_to_playlist,
      this, [this](const song &s) {
        open_add_to_playlist({playlist_subject::Kind::Track, {}, s});
      });
  connect(ui->albumlist, &AlbumCoverListView::add_to_playlist,
      this, [this](const album &a) {
        open_add_to_playlist({playlist_subject::Kind::Album, a, {}});
      });
  connect(ui->queuelist, &QueueListView::add_to_playlist,
      this, [this](const song &s) {
        open_add_to_playlist({playlist_subject::Kind::Track, {}, s});
      });

  // nowplaying_view data
  connect(backend.get(), &Backend::current_song_changed,
      nowplaying_view.get(), &NowPlayingView::set_current_song);
  connect(backend.get(), &Backend::current_song_changed,
      this, [this](const song &s) {
        nowplaying_view->set_album_art(artman->get_art(s.album_hash, theme::tok::art::now_playing));
        nowplaying_view->set_accent(artman->get_accent(s.album_hash));
        if (libman->has_album(s.album_hash))
          {
            auto a = libman->get_album(s.album_hash);
            nowplaying_view->set_album_info(a.name, a.date);
          }
      });

  // settings_view signals
  connect(settings_view.get(), &SettingsView::rounded_corners_changed,
      this, [this](bool v) {
        ui->albumlist->set_rounded_corners(v);
        Settings().set_rounded_corners(v);
      });
  connect(settings_view.get(), &SettingsView::drop_shadow_changed,
      this, [this](bool v) {
        ui->albumlist->set_drop_shadow(v);
        Settings().set_drop_shadow(v);
      });
  connect(settings_view.get(), &SettingsView::queue_display_changed,
      queue_panel_ctrl.get(), &QueuePanelController::set_display_mode);
  connect(settings_view.get(), &SettingsView::color_band_changed,
      queue_panel_ctrl.get(), &QueuePanelController::set_color_band);
  connect(settings_view.get(), &SettingsView::bootleg_stamp_changed,
      this, [this](bool v) {
        Settings().set_bootleg_stamp(v);
        artman->clear_render_cache();
        ui->albumlist->viewport()->update();
      });
  connect(settings_view.get(), &SettingsView::queue_style_changed,
      queue_panel_ctrl.get(), &QueuePanelController::set_style);
  connect(settings_view.get(), &SettingsView::poll_interval_changed,
      this, [this](int minutes) {
        Settings().set_poll_interval(minutes);
        backend->set_poll_interval(minutes);
      });
  connect(settings_view.get(), &SettingsView::audio_cache_enabled_changed,
      this, [this](bool v) {
        Settings s;
        s.set_audio_cache_enabled(v);
        backend->configure_audio_cache(v,
            static_cast<qint64>(s.audio_cache_budget_mb()) * 1024 * 1024);
      });
  connect(settings_view.get(), &SettingsView::audio_cache_budget_changed,
      this, [this](int mib) {
        Settings s;
        s.set_audio_cache_budget_mb(mib);
        backend->configure_audio_cache(s.audio_cache_enabled(),
            static_cast<qint64>(mib) * 1024 * 1024);
        settings_view->set_audio_cache_usage(backend->audio_cache_bytes());
      });
  connect(settings_view.get(), &SettingsView::clear_audio_cache_requested,
      this, [this] {
        backend->clear_audio_cache();
        settings_view->set_audio_cache_usage(backend->audio_cache_bytes());
      });
  connect(settings_view.get(), &SettingsView::open_log_requested,
      this, [this] {
        log_window->show();
        log_window->raise();
        log_window->activateWindow();
      });
  connect(settings_view.get(), &SettingsView::tray_enabled_changed,
      this, [this](bool v) {
        Settings().set_tray_enabled(v);
        tray_icon->set_enabled(v);
      });
  connect(settings_view.get(), &SettingsView::close_to_tray_changed,
      this, [](bool v) { Settings().set_close_to_tray(v); });

  // Servers section
  connect(settings_view.get(), &SettingsView::switch_backend_requested,
      backend.get(), &BackendController::switch_to);
  connect(settings_view.get(), &SettingsView::add_profile_requested,
      this, &MainWindow::on_add_profile_requested);
  connect(settings_view.get(), &SettingsView::edit_profile_requested,
      this, &MainWindow::on_edit_profile_requested);
  connect(settings_view.get(), &SettingsView::remove_profile_requested,
      this, [this](const QString &id) {
        ProfileStore().remove(id);
        refresh_settings_profiles();
      });

  // actions
  connect(&search_action, &QAction::triggered, [&] {
    // The search field lives in the hidden header while fullscreen — focusing it
    // would steal focus from NP (disarming Esc/F) and reveal nothing.
    if (fullscreen_active)
      return;
    ui->searchbox->setFocus();
    ui->searchbox->selectAll();
  });
  connect(&playpause_action, &QAction::triggered,
      playback_view.get(), &PlaybackView::playpause);
  connect(tray_icon.get(), &TrayIcon::play_pause_requested,
      playback_view.get(), &PlaybackView::playpause);
  connect(&prev_action, &QAction::triggered,
      playback_view.get(), &PlaybackView::prev);
  connect(&next_action, &QAction::triggered,
      playback_view.get(), &PlaybackView::next);
  connect(&stop_action, &QAction::triggered,
      playback_view.get(), &PlaybackView::stop);
  connect(&scroll_to_playing_action, &QAction::triggered,
      lib_grid_ctrl.get(), &LibraryGridController::scroll_to_playing);
  connect(&fullscreen_action, &QAction::triggered,
      this, &MainWindow::toggle_fullscreen);
}

inline constexpr int
operator|(Qt::Modifier m, Qt::KeyboardModifier k)
{
  return (int)m | (int)k;
}

auto
MainWindow::setup_actions() -> void
{
  auto setup_action = [&](QAction *a, const QKeySequence &s) {
    a->setShortcut(s);
    addAction(a);
  };
  setup_action(&playpause_action, Qt::Key_Space);
  setup_action(&stop_action, Qt::CTRL | Qt::Key_Space);
  // Search: Ctrl+F (legacy) and Ctrl+K (matches the field's hint badge).
  search_action.setShortcuts(
      {QKeySequence(Qt::CTRL | Qt::Key_F), QKeySequence(Qt::CTRL | Qt::Key_K)});
  addAction(&search_action);
  setup_action(&prev_action, Qt::CTRL | Qt::Key_Left);
  setup_action(&next_action, Qt::CTRL | Qt::Key_Right);
  setup_action(&scroll_to_playing_action, Qt::CTRL | Qt::Key_L);
  setup_action(&fullscreen_action, Qt::Key_F11);
}

auto
MainWindow::eventFilter(QObject *obj, QEvent *event) -> bool
{
  if (event->type() == QEvent::MouseButtonPress)
    {
      auto *me = static_cast<QMouseEvent *>(event);
      if (me->button() == Qt::BackButton)
        {
          // In fullscreen the Back button leaves fullscreen rather than navigating
          // away — the chrome is hidden, so any other screen would be unusable.
          if (fullscreen_active)
            toggle_fullscreen();
          else if (albumview->isVisible())
            lib_grid_ctrl->albumview_hide();
          else if (nowplaying_view->isVisible() || settings_view->isVisible())
            show_library();
          return true;
        }
    }

  return QMainWindow::eventFilter(obj, event);
}

auto
MainWindow::resizeEvent(QResizeEvent *event) -> void
{
  QMainWindow::resizeEvent(event);
  conn_states->position_overlays();
}

auto
MainWindow::connection_updated(bool connected) -> void
{
  playback_view->setEnabled(connected);
  if (!connected)
    setWindowTitle("freewave");
  conn_states->on_connection_changed(connected);
  // Keeps the Servers list honest while it is open: a connection that drops or
  // fails must surface Retry there without the user reopening Settings.
  refresh_settings_profiles();
  update_playlists_availability();
  if (connected)
    artman->set_cache_scope(ProfileStore().active_id()); // per-server mosaic cache
}

auto
MainWindow::on_backend_switching() -> void
{
  playback_view->setEnabled(false);
  setWindowTitle("freewave");
  conn_states->on_backend_switching();
  refresh_settings_profiles();
  update_playlists_availability();
}

auto
MainWindow::library_refresh_updated(bool active) -> void
{
  spdlog::debug("library refresh active = {}", active);

  if (active)
    {
      ui->status_label->setPixmap(QIcon::fromTheme("view-refresh").pixmap(QSize(24, 24)));
    }
  else
    {
      ui->status_label->setPixmap(QIcon::fromTheme("process-stop").pixmap(QSize(24, 24)));

      queue_panel_ctrl->repaint_list();
    }
}

auto
MainWindow::playback_state_updated(PlaybackState state) -> void
{
  if (state.state == PlayState::Stopped)
    setWindowTitle("freewave");

  nowplaying_view->set_audio_format(state);
  // Progress itself comes from ClockMan's tick (fed by the same
  // playback_state_changed signal), shared with playback_view so both labels
  // move in lockstep.

  queue_panel_ctrl->set_current_index(state.queue_pos);
  // Track the current position for the album/song "insert after current" ops.
  lib_grid_ctrl->set_current_queue_pos(state.queue_pos);
  current_queue_pos = state.queue_pos;
}

// Re-apply the now-playing badge/view art for the current track. Needed after a
// thumbnail rebuild: text-cover (artless) albums are generated from album_meta,
// which is empty when the current song first arrives on connect, so the badge
// would otherwise hold the generic placeholder until the next song change.
auto
MainWindow::refresh_now_playing_art() -> void
{
  if (current_album_hash.isEmpty())
    return;
  playback_view->set_album_art(artman->get_art(current_album_hash, theme::tok::art::mini));
  nowplaying_view->set_album_art(artman->get_art(current_album_hash, theme::tok::art::now_playing));
  nowplaying_view->set_accent(artman->get_accent(current_album_hash));
}

auto
MainWindow::closeEvent(QCloseEvent *event) -> void
{
  Settings settings_facade;
  if (!quitting && settings_facade.tray_enabled() && settings_facade.close_to_tray()
      && TrayIcon::is_supported())
    {
      event->ignore();
      hide();
      return;
    }

  // Swap out the qt_sink before any widgets are destroyed. The worker thread
  // may still log during teardown; redirect it to console only.
  spdlog::set_default_logger(std::make_shared<spdlog::logger>("freewave"));

  QSettings settings;
  // Don't persist the fullscreen flag: next launch's restoreGeometry() would
  // otherwise reopen a chrome-less window (fullscreen_active would be false, so
  // the chrome shows but the frame is gone) recoverable only via F11.
  if (isFullScreen())
    setWindowState(pre_fullscreen_state);
  settings_facade.set_geometry(saveGeometry());
  queue_panel_ctrl->save_width();
  QMainWindow::closeEvent(event);
}

auto
MainWindow::show_library() -> void
{
  nowplaying_view->setVisible(false);
  settings_view->setVisible(false);
  playlists_view->setVisible(false);
  ui->library_content->setVisible(true);
  if (!albumview->isVisible())
    ui->library_toolbar->setVisible(true);
}

auto
MainWindow::show_nowplaying() -> void
{
  ui->library_toolbar->setVisible(false);
  ui->library_content->setVisible(false);
  settings_view->setVisible(false);
  playlists_view->setVisible(false);
  nowplaying_view->setVisible(true);
  nowplaying_view->setFocus(); // arm the `L` favorite shortcut (WidgetWithChildren)
}

auto
MainWindow::toggle_nowplaying() -> void
{
  if (nowplaying_view->isVisible())
    show_library();
  else
    show_nowplaying();
}

auto
MainWindow::toggle_fullscreen() -> void
{
  if (!fullscreen_active)
    {
      fullscreen_active = true;
      // Fullscreen always shows Now Playing; remember whether we have to navigate
      // back on exit (entered from the library/settings rather than from NP).
      fs_return_to_library = !nowplaying_view->isVisible();
      if (!nowplaying_view->isVisible())
        show_nowplaying();

      // Hide all app chrome so the QML scene bleeds edge-to-edge.
      fs_queue_was_visible = queue_panel_ctrl->panel()->isVisible();
      queue_panel_ctrl->panel()->setVisible(false);
      ui->app_header->setVisible(false);
      player_separator->setVisible(false);
      playback_view->setVisible(false);

      pre_fullscreen_state = windowState();
      showFullScreen();
      screensaver.inhibit(); // keep the display awake for the immersive scene
      nowplaying_view->set_fullscreen(true);
      nowplaying_view->setFocus(); // keep the F/Esc shortcuts armed
    }
  else
    {
      fullscreen_active = false;
      ui->app_header->setVisible(true);
      player_separator->setVisible(true);
      playback_view->setVisible(true);
      queue_panel_ctrl->panel()->setVisible(fs_queue_was_visible);

      setWindowState(pre_fullscreen_state); // restore normal/maximized
      screensaver.uninhibit();
      nowplaying_view->set_fullscreen(false);
      if (fs_return_to_library)
        show_library();
    }
}

auto
MainWindow::show_settings() -> void
{
  refresh_settings_profiles();
  settings_view->set_audio_cache_usage(backend->audio_cache_bytes());
  // Hide all overlays — they float above centralwidget and would block settings.
  conn_states->hide_overlays();
  ui->library_toolbar->setVisible(false);
  ui->library_content->setVisible(false);
  nowplaying_view->setVisible(false);
  playlists_view->setVisible(false);
  settings_view->setVisible(true);
}

auto
MainWindow::show_playlists() -> void
{
  // Mirror show_settings for the view toggles, but leave the connection overlays
  // to the state machine (playlists content resets to empty on disconnect, so a
  // reconnect overlay floating over it is correct — matching show_nowplaying).
  ui->library_toolbar->setVisible(false);
  ui->library_content->setVisible(false);
  nowplaying_view->setVisible(false);
  settings_view->setVisible(false);
  playlists_view->setVisible(true);
  playlists_view->setFocus(); // arm the Esc-to-close handler on fresh open
}

auto
MainWindow::update_playlists_availability() -> void
{
  bool ok = backend->supports(Backend::Feature::Playlists);
  ui->playlists_button->setVisible(ok);
  albumview->set_playlists_available(ok);
  ui->albumlist->set_playlists_available(ok);
  ui->queuelist->set_playlists_available(ok);
  if (!ok && playlists_view->isVisible())
    show_library();
}

// Favorites reach their own widgets through FavoritesManager's favorites_reset;
// the playlist controls have no such broadcast and are re-evaluated here.
auto
MainWindow::capabilities_updated() -> void
{
  update_playlists_availability();
}

auto
MainWindow::open_add_to_playlist(const playlist_subject &subject) -> void
{
  if (!backend->supports(Backend::Feature::Playlists))
    return;
  auto *dlg = new AddToPlaylistDialog(plman, artman, backend.get(), subject, this);
  dlg->open();
}

auto
MainWindow::refresh_settings_profiles() -> void
{
  ProfileStore ps;
  settings_view->set_profiles(ps.load(), ps.active_id(), backend->is_connected());
}

auto
MainWindow::prompt_untrusted_certificate(const QString &host, const QString &fingerprint,
    const QString &issuer, const QString &reason) -> void
{
  // Group the hex into colon-separated bytes so it can be read off against the
  // server's own output.
  QStringList pairs;
  for (int i = 0; i + 1 < fingerprint.size(); i += 2)
    pairs << fingerprint.mid(i, 2).toUpper();

  QMessageBox box{this};
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle("Certificate not trusted");
  box.setText(QString("freewave cannot verify the certificate for <b>%1</b>.").arg(host));
  box.setInformativeText(
      QString("Issued by: %1\n\nSHA-256:\n%2\n\n%3\n\n"
              "This is expected if you run your own certificate authority. Trust it only "
              "if that fingerprint matches your server's.")
          .arg(issuer, pairs.join(':'), reason));

  auto *trust = box.addButton("Trust this server", QMessageBox::AcceptRole);
  box.addButton(QMessageBox::Cancel);
  box.exec();

  if (box.clickedButton() != trust)
    return;

  ProfileStore ps;
  auto profile = ps.profile_by_id(ps.active_id());
  if (!profile)
    return;
  profile->params["cert_sha256"] = fingerprint;
  ps.update(*profile);

  // Rebuilding the backend is what makes the pin take effect: the new instance
  // reads it before its first request.
  backend->switch_to(profile->id);
}

auto
MainWindow::on_add_profile_requested() -> void
{
  auto *dlg = new ProfileEditDialog(ProfileEditDialog::Mode::Add, {}, this);
  connect(dlg, &ProfileEditDialog::profiles_changed,
      this, &MainWindow::refresh_settings_profiles);
  dlg->open();
}

auto
MainWindow::on_edit_profile_requested(const QString &id) -> void
{
  auto profile_opt = ProfileStore().profile_by_id(id);
  if (!profile_opt)
    return;
  auto *dlg = new ProfileEditDialog(ProfileEditDialog::Mode::Edit, *profile_opt, this);
  connect(dlg, &ProfileEditDialog::profiles_changed,
      this, &MainWindow::refresh_settings_profiles);
  dlg->open();
}
