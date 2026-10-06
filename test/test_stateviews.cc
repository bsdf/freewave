// Tests for state view widgets: ReconnectingView, StaleLibraryBanner,
// ConnectionFailedView, MidSessionErrorView, LoadingView, and state-drawing
// primitives (PulseDot, CountdownRing, ErrorIcon).

#include <gtest/gtest.h>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QString>
#include <QWidget>

#include "ui/statewidgets.hh"
#include "ui/reconnectingview.hh"
#include "ui/stalelibrarybanner.hh"
#include "ui/connectionfailedview.hh"
#include "ui/midsessionerrorview.hh"
#include "ui/loadingview.hh"

// ---------------------------------------------------------------------------
// Helper — search a widget tree for any QLabel containing a substring
// ---------------------------------------------------------------------------
static bool
has_label_containing(const QWidget *w, const QString &sub)
{
  for (QLabel *l : w->findChildren<QLabel *>())
    if (l->text().contains(sub))
      return true;
  return false;
}

// ---------------------------------------------------------------------------
// StateWidgets — primitives
// ---------------------------------------------------------------------------

TEST(StateWidgets, PulseDot_Construct_NoCrash)
{
  PulseDot d;
  SUCCEED();
}

TEST(StateWidgets, CountdownRing_SetValues_NoCrash)
{
  CountdownRing r;
  r.setTotal(30);
  r.setSeconds(15);
  SUCCEED();
}

TEST(StateWidgets, ErrorIcon_AllKinds_NoCrash)
{
  ErrorIcon e1(ErrorIcon::Kind::Offline);
  ErrorIcon e2(ErrorIcon::Kind::Timeout);
  ErrorIcon e3(ErrorIcon::Kind::Auth);
  ErrorIcon e4(ErrorIcon::Kind::Partial);
  ErrorIcon e5(ErrorIcon::Kind::Gone);
  SUCCEED();
}

// ---------------------------------------------------------------------------
// ReconnectingView
// ---------------------------------------------------------------------------

TEST(ReconnectingViewTest, AttemptComposition)
{
  ReconnectingView v;
  v.setServerLabel("MPD ～ host:6600");
  v.setAttempt(2, 5);

  EXPECT_TRUE(has_label_containing(&v, "Attempt 2/5"));
  EXPECT_TRUE(has_label_containing(&v, "MPD ～ host:6600"));
}

TEST(ReconnectingViewTest, ServerLabelBeforeAttempt_NoCrash)
{
  ReconnectingView v;
  v.setServerLabel("MPD ～ x:6600");
  SUCCEED();
}

// ---------------------------------------------------------------------------
// StaleLibraryBanner
// ---------------------------------------------------------------------------

TEST(StaleLibraryBannerTest, AlbumCountShown)
{
  StaleLibraryBanner b;
  b.setAlbumCount(2413);
  EXPECT_TRUE(has_label_containing(&b, "2413"));
}

// ---------------------------------------------------------------------------
// ConnectionFailedView
// ---------------------------------------------------------------------------

TEST(ConnectionFailedViewTest, ErrorKindTitles)
{
  ConnectionFailedView v;

  v.setError(ConnectionFailedView::Kind::Offline, "Connection refused");
  EXPECT_TRUE(has_label_containing(&v, "Can't reach server"));
  EXPECT_TRUE(has_label_containing(&v, "Connection refused"));

  v.setError(ConnectionFailedView::Kind::Timeout, "x");
  EXPECT_TRUE(has_label_containing(&v, "Connection timed out"));

  v.setError(ConnectionFailedView::Kind::Auth, "x");
  EXPECT_TRUE(has_label_containing(&v, "Authentication failed"));

  v.setError(ConnectionFailedView::Kind::Library, "HTTP 500");
  EXPECT_TRUE(has_label_containing(&v, "Couldn't load your library"));
  EXPECT_TRUE(has_label_containing(&v, "HTTP 500"));
}

TEST(ConnectionFailedViewTest, HelpHintIsServerAware)
{
  ConnectionFailedView v;

  // MPD unreachable → the "is MPD running?" tip is useful.
  v.setServerLabel("MPD ～ localhost:6600");
  v.setError(ConnectionFailedView::Kind::Offline, "Connection refused");
  EXPECT_TRUE(has_label_containing(&v, "systemctl status mpd"));

  // A library failure means the server *did* respond — the MPD tip is wrong.
  v.setError(ConnectionFailedView::Kind::Library, "HTTP 500");
  EXPECT_FALSE(has_label_containing(&v, "systemctl status mpd"));

  // Subsonic server → never show the MPD tip, even when unreachable.
  v.setServerLabel("Subsonic ～ https://example.test");
  v.setError(ConnectionFailedView::Kind::Offline, "Connection refused");
  EXPECT_FALSE(has_label_containing(&v, "systemctl status mpd"));
}

// ---------------------------------------------------------------------------
// MidSessionErrorView
// ---------------------------------------------------------------------------

TEST(MidSessionErrorViewTest, ErrorKindAndWasPlaying)
{
  MidSessionErrorView v;

  v.setError(MidSessionErrorView::Kind::Gone);
  EXPECT_TRUE(has_label_containing(&v, "Server disconnected"));

  v.setWasPlaying(true);
  EXPECT_TRUE(has_label_containing(&v, "Playback has been paused"));

  v.setError(MidSessionErrorView::Kind::Offline);
  EXPECT_TRUE(has_label_containing(&v, "Server went offline"));
}

// ---------------------------------------------------------------------------
// LoadingView
// ---------------------------------------------------------------------------

TEST(LoadingViewTest, PhaseAndAlbumCount)
{
  LoadingView v;
  v.setServerLabel("MPD ～ host:6600");

  v.setPhase(LoadingView::Phase::Index);
  v.setAlbumCount(5);
  EXPECT_TRUE(has_label_containing(&v, "5"));

  v.setAlbumCount(-1);
  EXPECT_TRUE(has_label_containing(&v, "Reading"));

  v.setPhase(LoadingView::Phase::Connecting);
  EXPECT_TRUE(has_label_containing(&v, "Connecting"));
}

// The cancel button is the only escape from a startup connect attempt that
// never resolves; it must be offered while Connecting and withdrawn once a
// connection is in hand (Index/Rendering).
TEST(LoadingViewTest, CancelButtonOnlyDuringConnecting)
{
  LoadingView v;
  auto *btn = v.findChild<QPushButton *>();
  ASSERT_NE(btn, nullptr);

  // isVisibleTo(), not isVisible(): `v` is a never-shown top-level widget, and
  // isVisible() folds in that top-level hidden state regardless of the child's
  // own setVisible() calls.
  EXPECT_TRUE(btn->isVisibleTo(&v)); // default phase is Connecting

  v.setPhase(LoadingView::Phase::Index);
  EXPECT_FALSE(btn->isVisibleTo(&v));

  v.setPhase(LoadingView::Phase::Connecting);
  EXPECT_TRUE(btn->isVisibleTo(&v));

  QSignalSpy cancel_spy(&v, &LoadingView::cancel);
  btn->click();
  EXPECT_EQ(cancel_spy.count(), 1);
}
