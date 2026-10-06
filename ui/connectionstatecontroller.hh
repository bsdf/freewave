#ifndef CONNECTIONSTATECONTROLLER_HH
#define CONNECTIONSTATECONTROLLER_HH

#include <QObject>
#include <QTimer>

#include <functional>

#include "controller/backend.hh"

class QWidget;
class BackendController;
class StaleLibraryBanner;
class LoadingView;
class ConnectionFailedView;
class ReconnectingView;
class MidSessionErrorView;

// Owns the connection/library lifecycle state machine and its overlay widgets,
// extracted from MainWindow.
// MainWindow forwards backend signals to the public slots; the controller drives
// the loading / connect-failed / reconnecting / mid-session-error overlays and the
// stale-library banner, and emits the two cross-cutting events it can't own.
class ConnectionStateController : public QObject {
  Q_OBJECT

public:
  ConnectionStateController(QWidget *overlay_parent, // central widget
      QWidget *banner_parent,                        // mainarea frame
      BackendController *backend,
      std::function<int()> album_count, // for the stale banner
      QObject *parent = nullptr);

  void start();             // show the loading overlay, then connect to the server
  void position_overlays(); // resize overlays to the parent (from resizeEvent)
  void hide_overlays();     // hide the four full-area overlays (when showing settings)

public slots:
  void on_connection_changed(bool connected);
  // Why the connect attempt that is about to report failure failed. Arrives
  // just before on_connection_changed(false) and only for a connect attempt,
  // so it is recorded rather than acted on: the state change is what decides
  // which overlay to raise, this only decides what that overlay says.
  void on_connection_failed(Backend::ConnectError kind, const QString &detail);
  void on_backend_switching();
  // Called once the library has been (re)populated; transitions Loading→Ready and
  // returns whether this was the first successful load.
  auto notify_library_loaded() -> bool;
  // The backend connected but the library fetch failed. Without this the loading
  // overlay is terminal — notify_library_loaded() (the only Loading→Ready exit)
  // never fires. Surfaces the connect-failed overlay so the user gets Retry /
  // Settings instead of an endless spinner. No-op once a library has ever loaded
  // (a mid-session refresh failure keeps the cached library).
  void on_library_load_failed(const QString &reason);

signals:
  void open_settings_requested();
  void library_ready();

private:
  enum class ConnState { Connecting,
    Loading,
    Ready,
    ConnectFailed,
    Reconnecting,
    Disconnected };

  void setup_overlays();
  void show_overlay(QWidget *overlay);
  void show_loading(int phase);
  void start_reconnect();
  void stop_reconnect(bool reconnected);
  void on_retry_tick();
  void on_loading_timeout();
  void refresh_overlay_labels();
  void clear_failure_reason();
  auto server_label() const -> QString;

  static constexpr int RETRY_INTERVAL = 15;
  static constexpr int MAX_ATTEMPTS = 5;
  // Backstop for a library fetch that neither completes nor errors (a broken
  // callback chain). Comfortably above the backend's 15s per-request transfer
  // timeout, which normally turns a stalled fetch into library_load_failed.
  static constexpr int LOADING_TIMEOUT = 30;

  QWidget *overlay_parent;
  QWidget *banner_parent;
  BackendController *backend;
  std::function<int()> album_count;

  StaleLibraryBanner *stale_banner = nullptr;
  LoadingView *loading_view = nullptr;
  ConnectionFailedView *connfailed_view = nullptr;
  ReconnectingView *reconnecting_view = nullptr;
  MidSessionErrorView *midsession_view = nullptr;

  ConnState conn_state = ConnState::Connecting;
  QTimer retry_timer;      // 1 Hz tick: drives the reconnect countdown + retries
  QTimer loading_watchdog; // single-shot; fires if a library load hangs silently
  int retry_attempt = 0;
  int retry_seconds = 0;
  bool ever_loaded = false;
  // Reason for the failure being reported, valid only between
  // on_connection_failed() and the on_connection_changed(false) it precedes.
  // Cleared on every attempt and on success so a stale reason can never label
  // a later, differently-caused failure.
  bool have_failure_reason = false;
  Backend::ConnectError failure_kind = Backend::ConnectError::Unreachable;
  QString failure_detail;
  // Set when the user cancels out of the initial "Connecting" overlay. The
  // backend's in-flight attempt (MPD's blocking connect, or a Subsonic ping
  // with no way to abort a not-yet-connected client) may still resolve after
  // the user has moved on to Settings; this suppresses that stale result so
  // an overlay doesn't pop back up over the settings page.
  bool cancelled = false;
};

#endif // CONNECTIONSTATECONTROLLER_HH
