#include <gtest/gtest.h>
#include <QSignalSpy>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/connectionstatecontroller.hh"
#include "ui/loadingview.hh"
#include "ui/connectionfailedview.hh"
#include "ui/reconnectingview.hh"
#include "ui/midsessionerrorview.hh"
#include "controller/backendcontroller.hh"
#include "controller/backend.hh"

// The controller's only call into the backend is connect_to_server(); a
// BackendController with no active backend makes that a harmless no-op, so we
// don't need a full StubBackend here — overlay state is what we assert.
class ConnectionStateControllerTest : public ::testing::Test {
protected:
  std::unique_ptr<QWidget> overlay_parent;
  std::unique_ptr<QWidget> banner_parent;
  std::unique_ptr<BackendController> backend;
  std::unique_ptr<ConnectionStateController> csc;
  int album_count = 0;

  void SetUp() override
  {
    overlay_parent = std::make_unique<QWidget>();
    banner_parent = std::make_unique<QWidget>();
    banner_parent->setLayout(new QVBoxLayout); // controller inserts the banner here
    backend = std::make_unique<BackendController>(std::shared_ptr<Backend>{});
    csc = std::make_unique<ConnectionStateController>(
        overlay_parent.get(), banner_parent.get(), backend.get(),
        [this] { return album_count; });
  }

  // isVisibleTo() reflects the explicit show/hide flag without needing the
  // top-level window shown — exactly the state-machine bit we want to assert.
  template<class T>
  bool shown()
  {
    auto *w = overlay_parent->findChild<T *>();
    return w && w->isVisibleTo(overlay_parent.get());
  }

  // The overlays expose no accessor for which error they are presenting, so the
  // rendered text is the seam: it is also what actually has to be right.
  bool label_shown(const QString &text)
  {
    for (auto *l : overlay_parent->findChildren<QLabel *>())
      if (l->text() == text)
        return true;
    return false;
  }

  bool label_contains(const QString &text)
  {
    for (auto *l : overlay_parent->findChildren<QLabel *>())
      if (l->text().contains(text))
        return true;
    return false;
  }
};

TEST_F(ConnectionStateControllerTest, Start_ShowsLoading)
{
  csc->start();
  EXPECT_TRUE(shown<LoadingView>());
  EXPECT_FALSE(shown<ConnectionFailedView>());
}

TEST_F(ConnectionStateControllerTest, FailureBeforeLoad_ShowsConnectFailed)
{
  csc->start();
  csc->on_connection_changed(false);
  // The connect-failed overlay is raised over the still-present loading overlay
  // (show_overlay raises rather than hiding the others), so we only assert it shows.
  EXPECT_TRUE(shown<ConnectionFailedView>());
  EXPECT_FALSE(shown<ReconnectingView>());
}

// Which overlay shows is only half the answer: a server that refused the
// credentials must not be reported as one that couldn't be reached. The kind
// isn't exposed, so assert on what the user actually reads.
TEST_F(ConnectionStateControllerTest, AuthFailureBeforeLoad_ReportsAuthNotOffline)
{
  csc->start();
  csc->on_connection_failed(Backend::ConnectError::Auth, "Wrong username or password.");
  csc->on_connection_changed(false);

  ASSERT_TRUE(shown<ConnectionFailedView>());
  EXPECT_TRUE(label_shown("Authentication failed"));
  EXPECT_FALSE(label_shown("Can't reach server"));
  // The server's own words survive into the detail chip.
  EXPECT_TRUE(label_contains("Wrong username or password."));
}

// No reason reported (MPD, which has no credentials to get wrong) keeps the
// unexplained-failure wording.
TEST_F(ConnectionStateControllerTest, UnexplainedFailureBeforeLoad_ReportsOffline)
{
  csc->start();
  csc->on_connection_changed(false);

  ASSERT_TRUE(shown<ConnectionFailedView>());
  EXPECT_TRUE(label_shown("Can't reach server"));
  EXPECT_FALSE(label_shown("Authentication failed"));
}

// A reason belongs only to the failure it arrived with. If it outlived that,
// a later unrelated failure — or a reconnect after the user fixed the password
// — would still be blaming the credentials.
TEST_F(ConnectionStateControllerTest, AuthReasonDoesNotLeakIntoTheNextFailure)
{
  csc->start();
  csc->on_connection_failed(Backend::ConnectError::Auth, "Wrong username or password.");
  csc->on_connection_changed(false);
  ASSERT_TRUE(label_shown("Authentication failed"));

  // Second attempt fails with nothing reported about why.
  csc->start();
  csc->on_connection_changed(false);
  EXPECT_TRUE(label_shown("Can't reach server"));
  EXPECT_FALSE(label_shown("Authentication failed"));
}

TEST_F(ConnectionStateControllerTest, ConnectedBeforeLoad_KeepsLoading)
{
  csc->on_connection_changed(true);
  EXPECT_TRUE(shown<LoadingView>());
  EXPECT_FALSE(shown<ConnectionFailedView>());
}

TEST_F(ConnectionStateControllerTest, LibraryLoaded_FirstLoadEmitsReadyAndHidesLoading)
{
  QSignalSpy spy(csc.get(), &ConnectionStateController::library_ready);

  csc->on_connection_changed(true); // -> Loading, overlay shown
  ASSERT_TRUE(shown<LoadingView>());

  EXPECT_TRUE(csc->notify_library_loaded()); // first load
  EXPECT_FALSE(shown<LoadingView>());
  EXPECT_EQ(spy.count(), 1);

  // A subsequent load is no longer the first, and doesn't re-fire library_ready
  // (already in Ready).
  EXPECT_FALSE(csc->notify_library_loaded());
  EXPECT_EQ(spy.count(), 1);
}

TEST_F(ConnectionStateControllerTest, MidSessionDrop_ShowsReconnecting)
{
  csc->on_connection_changed(true);
  csc->notify_library_loaded(); // ever_loaded -> true, state Ready

  csc->on_connection_changed(false); // mid-session drop -> reconnect countdown
  EXPECT_TRUE(shown<ReconnectingView>());
  EXPECT_FALSE(shown<ConnectionFailedView>());
}

TEST_F(ConnectionStateControllerTest, BackendSwitching_ResetsToLoading)
{
  csc->on_connection_changed(true);
  csc->notify_library_loaded();

  csc->on_backend_switching();
  EXPECT_TRUE(shown<LoadingView>());
  EXPECT_FALSE(shown<MidSessionErrorView>());

  // ever_loaded was reset, so the next library load counts as a first load again.
  EXPECT_TRUE(csc->notify_library_loaded());
}

TEST_F(ConnectionStateControllerTest, Cancel_HidesLoadingAndRequestsSettings)
{
  QSignalSpy settings_spy(csc.get(), &ConnectionStateController::open_settings_requested);

  csc->start();
  ASSERT_TRUE(shown<LoadingView>());

  emit overlay_parent->findChild<LoadingView *>()->cancel();

  EXPECT_FALSE(shown<LoadingView>());
  EXPECT_EQ(settings_spy.count(), 1);
}

// A cancelled attempt may still be in flight (neither backend can truly abort
// a not-yet-connected client); its eventual result must not resurrect an
// overlay over the settings page the user backed out to.
TEST_F(ConnectionStateControllerTest, CancelThenStaleResult_NoOverlayReappears)
{
  csc->start();
  emit overlay_parent->findChild<LoadingView *>()->cancel();
  ASSERT_FALSE(shown<LoadingView>());

  csc->on_connection_changed(true);
  EXPECT_FALSE(shown<LoadingView>());

  csc->on_connection_changed(false);
  EXPECT_FALSE(shown<ConnectionFailedView>());
}

// Re-triggering a connect attempt from Settings must clear the cancelled flag,
// or this (and every future) attempt's result would be silently swallowed too.
TEST_F(ConnectionStateControllerTest, StartAfterCancel_ResumesNormalHandling)
{
  csc->start();
  emit overlay_parent->findChild<LoadingView *>()->cancel();

  csc->start();
  csc->on_connection_changed(false);
  EXPECT_TRUE(shown<ConnectionFailedView>());
}

TEST_F(ConnectionStateControllerTest, HideOverlays_HidesAll)
{
  csc->start();
  ASSERT_TRUE(shown<LoadingView>());

  csc->hide_overlays();
  EXPECT_FALSE(shown<LoadingView>());
  EXPECT_FALSE(shown<ConnectionFailedView>());
  EXPECT_FALSE(shown<ReconnectingView>());
  EXPECT_FALSE(shown<MidSessionErrorView>());
}
