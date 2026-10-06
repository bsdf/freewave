#include "connectionstatecontroller.hh"

#include <QBoxLayout>
#include <QWidget>
#include <spdlog/spdlog.h>

#include "controller/backendcontroller.hh"
#include "controller/profilestore.hh"
#include "stalelibrarybanner.hh"
#include "loadingview.hh"
#include "connectionfailedview.hh"
#include "reconnectingview.hh"
#include "midsessionerrorview.hh"

ConnectionStateController::ConnectionStateController(QWidget *overlay_parent,
    QWidget *banner_parent, BackendController *backend,
    std::function<int()> album_count, QObject *parent)
  : QObject(parent)
  , overlay_parent(overlay_parent)
  , banner_parent(banner_parent)
  , backend(backend)
  , album_count(std::move(album_count))
{
  setup_overlays();
}

auto
ConnectionStateController::setup_overlays() -> void
{
  const QString srv = server_label();

  // Stale-library banner: thin bar above the library grid, shown when the user
  // keeps browsing the cached library after giving up on reconnecting.
  stale_banner = new StaleLibraryBanner(banner_parent);
  auto *main_area_layout = qobject_cast<QVBoxLayout *>(banner_parent->layout());
  main_area_layout->insertWidget(0, stale_banner);
  stale_banner->setVisible(false);
  connect(stale_banner, &StaleLibraryBanner::retry, this, [this] { backend->connect_to_server(); });
  connect(stale_banner, &StaleLibraryBanner::dismissed,
      this, [this] { stale_banner->setVisible(false); });

  // Full-area state overlays, parented to the central widget and resized in
  // position_overlays(). All hidden until a connection event shows one.
  loading_view = new LoadingView(overlay_parent);
  connfailed_view = new ConnectionFailedView(overlay_parent);
  reconnecting_view = new ReconnectingView(overlay_parent);
  midsession_view = new MidSessionErrorView(overlay_parent);

  for (QWidget *o : {static_cast<QWidget *>(loading_view),
           static_cast<QWidget *>(connfailed_view),
           static_cast<QWidget *>(reconnecting_view),
           static_cast<QWidget *>(midsession_view)})
    o->setVisible(false);

  loading_view->setServerLabel(srv);
  connfailed_view->setServerLabel(srv);
  reconnecting_view->setServerLabel(srv);
  midsession_view->setServerLabel(srv);

  connect(connfailed_view, &ConnectionFailedView::retry, this, [this] {
    show_loading(static_cast<int>(LoadingView::Phase::Connecting));
    backend->connect_to_server();
  });
  connect(connfailed_view, &ConnectionFailedView::openSettings,
      this, &ConnectionStateController::open_settings_requested);

  connect(reconnecting_view, &ReconnectingView::retryNow, this, [this] {
    retry_seconds = 0;
    on_retry_tick();
  });
  connect(reconnecting_view, &ReconnectingView::giveUp, this, [this] {
    stop_reconnect(false);
    conn_state = ConnState::Disconnected;
    midsession_view->setError(MidSessionErrorView::Kind::Gone);
    midsession_view->setWasPlaying(false);
    show_overlay(midsession_view);
  });

  connect(midsession_view, &MidSessionErrorView::reconnect, this, [this] {
    midsession_view->setVisible(false);
    show_loading(static_cast<int>(LoadingView::Phase::Connecting));
    backend->connect_to_server();
  });
  connect(midsession_view, &MidSessionErrorView::dismiss, this, [this] {
    midsession_view->setVisible(false);
    // Keep browsing the cached library; surface the non-blocking stale banner.
    stale_banner->setAlbumCount(album_count ? album_count() : 0);
    stale_banner->setVisible(true);
  });

  connect(loading_view, &LoadingView::cancel, this, [this] {
    cancelled = true;
    loading_watchdog.stop();
    // Backing out of a switch returns to the server that was working, rather
    // than leaving the user in Settings on top of a session already torn down.
    // The revert is itself a switch, so it re-shows this overlay for the old
    // server; Settings is only where there is nothing to go back to.
    if (backend->revert_to_previous())
      return;
    loading_view->setVisible(false);
    emit open_settings_requested();
  });

  connect(&retry_timer, &QTimer::timeout, this, &ConnectionStateController::on_retry_tick);

  loading_watchdog.setSingleShot(true);
  connect(&loading_watchdog, &QTimer::timeout,
      this, &ConnectionStateController::on_loading_timeout);
}

auto
ConnectionStateController::start() -> void
{
  cancelled = false;
  clear_failure_reason();
  show_loading(static_cast<int>(LoadingView::Phase::Connecting));
  backend->connect_to_server();
}

auto
ConnectionStateController::on_connection_failed(Backend::ConnectError kind,
    const QString &detail) -> void
{
  have_failure_reason = true;
  failure_kind = kind;
  failure_detail = detail;
}

auto
ConnectionStateController::clear_failure_reason() -> void
{
  have_failure_reason = false;
  failure_kind = Backend::ConnectError::Unreachable;
  failure_detail.clear();
}

auto
ConnectionStateController::on_connection_changed(bool connected) -> void
{
  spdlog::debug("received connection_updated({})", connected);

  // The user backed out of the "Connecting" overlay into Settings; a stray
  // result from that abandoned attempt must not pop an overlay back up.
  if (cancelled)
    return;

  if (connected)
    {
      clear_failure_reason();
      refresh_overlay_labels();

      // A reconnect succeeded (or the initial connect landed): clear any
      // reconnect machinery and error overlays.
      if (conn_state == ConnState::Reconnecting || conn_state == ConnState::Disconnected)
        stop_reconnect(true);
      connfailed_view->setVisible(false);
      midsession_view->setVisible(false);

      if (ever_loaded)
        {
          conn_state = ConnState::Ready;
        }
      else
        {
          // Wait on the library; keep showing the loading overlay (index phase).
          conn_state = ConnState::Loading;
          show_loading(static_cast<int>(LoadingView::Phase::Index));
          loading_watchdog.start(LOADING_TIMEOUT * 1000);
        }
    }
  else
    {
      // While actively reconnecting, the retry timer owns the lifecycle —
      // a failed attempt just loops; don't restart anything here.
      if (conn_state == ConnState::Reconnecting || conn_state == ConnState::Disconnected)
        return;

      if (ever_loaded)
        {
          // Mid-session drop with a library in hand → count down and retry.
          start_reconnect();
        }
      else
        {
          // Never reached a usable library → startup connection failure.
          loading_watchdog.stop();
          conn_state = ConnState::ConnectFailed;
          const bool auth = have_failure_reason
                            && failure_kind == Backend::ConnectError::Auth;
          // With no reason reported, the failure is genuinely unexplained —
          // "Connection refused" is the honest stand-in, and the only backend
          // that reports nothing (MPD) has no credentials to get wrong.
          connfailed_view->setError(
              auth ? ConnectionFailedView::Kind::Auth
                   : ConnectionFailedView::Kind::Offline,
              have_failure_reason ? failure_detail : QStringLiteral("Connection refused"));
          clear_failure_reason();
          show_overlay(connfailed_view);
        }
    }
}

auto
ConnectionStateController::notify_library_loaded() -> bool
{
  // First library load completed — tear down the startup loading overlay.
  loading_watchdog.stop();
  const bool first_load = !ever_loaded;
  ever_loaded = true;
  loading_view->setVisible(false);
  if (conn_state == ConnState::Loading || conn_state == ConnState::Connecting)
    {
      conn_state = ConnState::Ready;
      // If the user triggered a backend switch, let MainWindow navigate to the
      // library now that the new backend's library is ready.
      emit library_ready();
    }
  return first_load;
}

auto
ConnectionStateController::on_library_load_failed(const QString &reason) -> void
{
  if (cancelled)
    return;

  // Only the *initial* load failing strands the app; a refresh failure with a
  // library already in hand is handled by keeping the cached library.
  if (ever_loaded)
    return;
  if (conn_state != ConnState::Connecting && conn_state != ConnState::Loading)
    return;

  // The connection came up but the library request failed — most often a
  // rejected credential on a server whose ping doesn't validate auth. Present
  // it as a library/auth failure so the user gets Retry + Settings; the detail
  // chip carries the concrete reason (e.g. "HTTP 500").
  loading_watchdog.stop();
  spdlog::warn("initial library load failed ({}); showing connect-failed overlay", reason);
  conn_state = ConnState::ConnectFailed;
  connfailed_view->setError(ConnectionFailedView::Kind::Library, reason);
  show_overlay(connfailed_view);
}

auto
ConnectionStateController::on_loading_timeout() -> void
{
  // The library fetch neither completed nor errored within LOADING_TIMEOUT —
  // don't leave the user on an endless spinner. Same escape as an explicit
  // failure: connect-failed overlay with Retry + Settings.
  if (cancelled || conn_state != ConnState::Loading)
    return;

  spdlog::warn("library load timed out after {}s; showing connect-failed overlay",
      LOADING_TIMEOUT);
  conn_state = ConnState::ConnectFailed;
  connfailed_view->setError(ConnectionFailedView::Kind::Library, "Timed out");
  show_overlay(connfailed_view);
}

auto
ConnectionStateController::server_label() const -> QString
{
  if (auto p = ProfileStore().active_profile())
    {
      if (p->type == "subsonic")
        return "Subsonic ～ " + p->params.value("url").toString();
      const auto host = p->params.value("host", "localhost").toString();
      const auto port = p->params.value("port", 6600u).toUInt();
      return QString("MPD ～ %1:%2").arg(host).arg(port);
    }
  return "freewave";
}

auto
ConnectionStateController::refresh_overlay_labels() -> void
{
  const QString srv = server_label();
  loading_view->setServerLabel(srv);
  connfailed_view->setServerLabel(srv);
  reconnecting_view->setServerLabel(srv);
  midsession_view->setServerLabel(srv);
}

auto
ConnectionStateController::on_backend_switching() -> void
{
  // Stop any in-progress reconnect so the countdown doesn't interfere with the
  // new backend's initial connection attempt.
  retry_timer.stop();
  loading_watchdog.stop();
  retry_attempt = 0;
  retry_seconds = 0;
  reconnecting_view->setVisible(false);

  cancelled = false;
  ever_loaded = false;
  clear_failure_reason();
  conn_state = ConnState::Connecting;
  refresh_overlay_labels();
  show_loading(static_cast<int>(LoadingView::Phase::Connecting));
}

auto
ConnectionStateController::position_overlays() -> void
{
  if (!loading_view)
    return;
  const QRect r = overlay_parent->rect();
  for (QWidget *o : {static_cast<QWidget *>(loading_view),
           static_cast<QWidget *>(connfailed_view),
           static_cast<QWidget *>(reconnecting_view),
           static_cast<QWidget *>(midsession_view)})
    o->setGeometry(r);
}

auto
ConnectionStateController::show_overlay(QWidget *overlay) -> void
{
  position_overlays();
  overlay->raise();
  overlay->setVisible(true);
}

auto
ConnectionStateController::show_loading(int phase) -> void
{
  connfailed_view->setVisible(false);
  reconnecting_view->setVisible(false);
  midsession_view->setVisible(false);
  loading_view->setPhase(static_cast<LoadingView::Phase>(phase));
  show_overlay(loading_view);
}

auto
ConnectionStateController::hide_overlays() -> void
{
  loading_watchdog.stop();
  loading_view->setVisible(false);
  connfailed_view->setVisible(false);
  reconnecting_view->setVisible(false);
  midsession_view->setVisible(false);
}

auto
ConnectionStateController::start_reconnect() -> void
{
  conn_state = ConnState::Reconnecting;
  retry_attempt = 1;
  retry_seconds = RETRY_INTERVAL;
  reconnecting_view->setTotal(RETRY_INTERVAL);
  reconnecting_view->setAttempt(retry_attempt, MAX_ATTEMPTS);
  reconnecting_view->setSeconds(retry_seconds);
  show_overlay(reconnecting_view);
  retry_timer.start(1000);
}

auto
ConnectionStateController::stop_reconnect(bool reconnected) -> void
{
  retry_timer.stop();
  retry_attempt = 0;
  retry_seconds = 0;
  reconnecting_view->setVisible(false);
  if (reconnected)
    {
      midsession_view->setVisible(false);
      stale_banner->setVisible(false);
    }
}

auto
ConnectionStateController::on_retry_tick() -> void
{
  if (retry_seconds > 0)
    {
      --retry_seconds;
      reconnecting_view->setSeconds(retry_seconds);
      return;
    }

  // Countdown elapsed — attempt a reconnect, or give up if exhausted.
  if (retry_attempt >= MAX_ATTEMPTS)
    {
      stop_reconnect(false);
      conn_state = ConnState::Disconnected;
      midsession_view->setError(MidSessionErrorView::Kind::Gone);
      midsession_view->setWasPlaying(false);
      show_overlay(midsession_view);
      return;
    }

  ++retry_attempt;
  retry_seconds = RETRY_INTERVAL;
  reconnecting_view->setAttempt(retry_attempt, MAX_ATTEMPTS);
  reconnecting_view->setSeconds(retry_seconds);
  backend->connect_to_server();
}
