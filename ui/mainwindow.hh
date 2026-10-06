#ifndef MAINWINDOW_HH
#define MAINWINDOW_HH

#include <QAction>
#include <QMainWindow>
#include <QKeySequence>

#include <memory>

#include "controller/albumartmanager.hh"
#include "controller/backendcontroller.hh"
#include "controller/clockman.hh"
#include "controller/favoritesmanager.hh"
#include "controller/librarymanager.hh"
#include "controller/playlistsmanager.hh"
#include "controller/screensaverinhibitor.hh"
#include "queuepanelcontroller.hh"
#include "librarygridcontroller.hh"
#include "albumview.hh"
#include "emptylibraryview.hh"
#include "nowplayingview.hh"
#include "playlistsview.hh"
#include "settingsview.hh"
#include "playback.hh"
#include "logwindow.hh"

class QFrame;
class QLabel;
class ConnectionStateController;
class MprisBridge;
class TrayIcon;
struct playlist_subject;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

public slots:
  void playback_state_updated(PlaybackState state);
  void connection_updated(bool connected);
  void library_refresh_updated(bool active);

  // Trust-on-first-use prompt for an unverifiable server certificate. Accepting
  // pins the fingerprint on the active profile and reconnects.
  void prompt_untrusted_certificate(const QString &host, const QString &fingerprint,
      const QString &issuer, const QString &reason);

  void show_library();
  void show_nowplaying();
  void toggle_nowplaying();
  void toggle_fullscreen();
  void show_settings();
  void show_playlists();

signals:
  void rebuild_thumbnails(const QList<album> &albums);

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void setup_widgets();
  void setup_connections();
  void setup_actions();
  void setup_state_overlays();
  void refresh_now_playing_art();

  void on_backend_switching();
  void refresh_settings_profiles();
  void on_add_profile_requested();
  void on_edit_profile_requested(const QString &id);
  void update_playlists_availability();
  void capabilities_updated();
  void open_add_to_playlist(const playlist_subject &subject);

  std::unique_ptr<Ui::MainWindow> ui;
  std::unique_ptr<AlbumView> albumview;
  std::unique_ptr<EmptyLibraryView> empty_library_view;
  std::unique_ptr<NowPlayingView> nowplaying_view;
  std::unique_ptr<SettingsView> settings_view;
  std::unique_ptr<PlaylistsView> playlists_view;
  std::unique_ptr<PlaybackView> playback_view;
  // Shared interpolated-position source for playback_view and nowplaying_view,
  // so their time labels tick in lockstep instead of drifting on independent
  // per-view timers.
  std::unique_ptr<ClockMan> clockman;

  // Deliberate shared ownership (not Qt parent-ownership): these QObject managers
  // are co-owned by MainWindow and the list models / controllers / backends that
  // outlive any single owner's teardown order. Constructed parent-less via
  // make_shared so the shared_ptr is the sole owner — never give them a QObject
  // parent, or the parent's delete races the last shared_ptr.
  std::shared_ptr<AlbumArtManager> artman;
  std::shared_ptr<BackendController> backend;
  std::shared_ptr<LibraryManager> libman;
  std::shared_ptr<FavoritesManager> favman;
  std::shared_ptr<PlaylistsManager> plman;

  QString current_album_hash;
  int current_queue_pos = -1; // for playlist "play next" (insert after current)

  QAction search_action;
  QAction playpause_action;
  QAction prev_action;
  QAction next_action;
  QAction stop_action;
  QAction scroll_to_playing_action;
  QAction fullscreen_action;

  // Fullscreen Now Playing: MainWindow owns the OS window state and hides the app
  // chrome (header, player bar, queue) so the QML scene fills the screen. These
  // snapshot what to restore on exit.
  QFrame *player_separator = nullptr; // hairline above the player bar
  bool fullscreen_active = false;
  bool fs_queue_was_visible = false; // queue panel visibility to restore
  bool fs_return_to_library = false; // entered from a non-NP view → go back on exit
  Qt::WindowStates pre_fullscreen_state = Qt::WindowNoState;
  ScreenSaverInhibitor screensaver; // suppresses screen-blank while fullscreen

  LogWindow *log_window;

  std::unique_ptr<TrayIcon> tray_icon;
  // Set by the tray menu's Quit action so closeEvent runs its normal teardown
  // instead of hiding to tray, even when "close to tray" is enabled.
  bool quitting = false;

  // Library content area slice (grid models, open-album view, All/Recent,
  // search filter, empty-state, scroll-to-playing).
  std::unique_ptr<LibraryGridController> lib_grid_ctrl;

  // Resizable queue panel slice (panel/splitter/animation/model + queue settings).
  std::unique_ptr<QueuePanelController> queue_panel_ctrl;

  // Connection/library lifecycle state machine + its overlay widgets.
  std::unique_ptr<ConnectionStateController> conn_states;

#ifdef ENABLE_MPRIS
  std::unique_ptr<MprisBridge> mpris_bridge;
#endif
};
#endif // MAINWINDOW_HH
