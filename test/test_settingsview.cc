// Tests for SettingsView::set_profiles — Servers section dynamic list.

#include <gtest/gtest.h>
#include <QApplication>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QSignalSpy>
#include <QToolButton>

#include "controller/profilestore.hh"
#include "ui/settingsview.hh"
#include "ui/toggle.hh"
#include "version.hh"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static BackendProfile
make_profile(const QString &id, const QString &type, const QString &name)
{
  BackendProfile p{.id = id, .type = type, .name = name};
  if (type == "mpd")
    {
      p.params["host"] = "localhost";
      p.params["port"] = 6600u;
    }
  else
    {
      p.params["url"] = "http://example.com";
    }
  return p;
}

// Find a QPushButton by exact text anywhere in the widget tree.
static QPushButton *
find_button(const QWidget *w, const QString &text)
{
  for (auto *btn : w->findChildren<QPushButton *>())
    if (btn->text() == text)
      return btn;
  return nullptr;
}

// Count QPushButtons with a given text in the widget tree.
static int
count_buttons(const QWidget *w, const QString &text)
{
  int n = 0;
  for (auto *btn : w->findChildren<QPushButton *>())
    if (btn->text() == text)
      ++n;
  return n;
}

// Find an icon QToolButton by its tooltip (Edit / Remove are icon buttons).
static QToolButton *
find_tool_button(const QWidget *w, const QString &tip)
{
  for (auto *btn : w->findChildren<QToolButton *>())
    if (btn->toolTip() == tip)
      return btn;
  return nullptr;
}

// ---------------------------------------------------------------------------
// set_profiles — structural tests
// ---------------------------------------------------------------------------

TEST(SettingsViewTest, SetProfiles_Empty_NoCrash)
{
  SettingsView sv;
  sv.set_profiles({}, "");
  SUCCEED();
}

TEST(SettingsViewTest, SetPollSupported_TogglesPollRowVisibility)
{
  SettingsView sv;
  auto *spin = sv.findChild<QSpinBox *>();
  ASSERT_NE(spin, nullptr);
  EXPECT_TRUE(spin->isVisibleTo(&sv));

  sv.set_poll_supported(false);
  EXPECT_FALSE(spin->isVisibleTo(&sv));

  sv.set_poll_supported(true);
  EXPECT_TRUE(spin->isVisibleTo(&sv));
}

TEST(SettingsViewTest, SetTraySupported_TogglesTraySectionVisibility)
{
  SettingsView sv;
  auto *tray_cb = sv.findChild<Toggle *>("tray_enabled_cb");
  ASSERT_NE(tray_cb, nullptr);
  EXPECT_TRUE(tray_cb->isVisibleTo(&sv));

  sv.set_tray_supported(false);
  EXPECT_FALSE(tray_cb->isVisibleTo(&sv));

  sv.set_tray_supported(true);
  EXPECT_TRUE(tray_cb->isVisibleTo(&sv));
}

TEST(SettingsViewTest, TrayEnabledChanged_EmittedOnToggle)
{
  SettingsView sv;
  auto *tray_cb = sv.findChild<Toggle *>("tray_enabled_cb");
  ASSERT_NE(tray_cb, nullptr);

  QSignalSpy spy(&sv, &SettingsView::tray_enabled_changed);
  tray_cb->setChecked(true);
  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(spy.first().first().toBool());
}

TEST(SettingsViewTest, CloseToTrayCheckbox_DisabledUnlessTrayEnabled)
{
  SettingsView sv;
  auto *tray_cb = sv.findChild<Toggle *>("tray_enabled_cb");
  auto *close_cb = sv.findChild<Toggle *>("close_to_tray_cb");
  ASSERT_NE(tray_cb, nullptr);
  ASSERT_NE(close_cb, nullptr);

  // Tray starts disabled by default, so "close to tray" is a dead control.
  EXPECT_FALSE(tray_cb->isChecked());
  EXPECT_FALSE(close_cb->isEnabled());

  tray_cb->setChecked(true);
  EXPECT_TRUE(close_cb->isEnabled());

  tray_cb->setChecked(false);
  EXPECT_FALSE(close_cb->isEnabled());
}

TEST(SettingsViewTest, CloseToTrayChanged_EmittedOnToggle)
{
  SettingsView sv;
  auto *tray_cb = sv.findChild<Toggle *>("tray_enabled_cb");
  auto *close_cb = sv.findChild<Toggle *>("close_to_tray_cb");
  ASSERT_NE(tray_cb, nullptr);
  ASSERT_NE(close_cb, nullptr);
  tray_cb->setChecked(true);

  QSignalSpy spy(&sv, &SettingsView::close_to_tray_changed);
  close_cb->setChecked(true);
  ASSERT_EQ(spy.count(), 1);
  EXPECT_TRUE(spy.first().first().toBool());
}

TEST(SettingsViewTest, VersionLabelIncludesGitSha)
{
  SettingsView sv;
  bool found = false;
  for (auto *lbl : sv.findChildren<QLabel *>())
    if (lbl->text().contains(FREEWAVE_VERSION) && lbl->text().contains(FREEWAVE_GIT_SHA))
      found = true;
  EXPECT_TRUE(found);
}

TEST(SettingsViewTest, SetProfiles_EmptyList_AddButtonPresent)
{
  SettingsView sv;
  sv.set_profiles({}, "");
  EXPECT_NE(find_button(&sv, "Add Server…"), nullptr);
}

TEST(SettingsViewTest, SetProfiles_SingleProfile_OneConnectOrActiveLabel)
{
  SettingsView sv;
  auto p = make_profile("id1", "mpd", "Home MPD");
  sv.set_profiles({p}, "id1");
  // Active profile shows "Active" text, not a Connect button.
  EXPECT_EQ(count_buttons(&sv, "Connect"), 0);
}

TEST(SettingsViewTest, SetProfiles_TwoProfiles_ConnectButtonForInactive)
{
  SettingsView sv;
  auto active = make_profile("id1", "mpd", "Home MPD");
  auto inactive = make_profile("id2", "subsonic", "LMS");
  sv.set_profiles({active, inactive}, "id1");
  // One Connect button for the inactive profile.
  EXPECT_EQ(count_buttons(&sv, "Connect"), 1);
}

TEST(SettingsViewTest, SetProfiles_SingleProfile_RemoveButtonDisabled)
{
  SettingsView sv;
  auto p = make_profile("id1", "mpd", "Home MPD");
  sv.set_profiles({p}, "id1");
  // Cannot remove the only profile.
  auto *rem = find_tool_button(&sv, "Remove");
  ASSERT_NE(rem, nullptr);
  EXPECT_FALSE(rem->isEnabled());
}

TEST(SettingsViewTest, SetProfiles_TwoProfiles_ActiveRemoveDisabled)
{
  SettingsView sv;
  auto active = make_profile("id1", "mpd", "Home MPD");
  auto inactive = make_profile("id2", "subsonic", "LMS");
  sv.set_profiles({active, inactive}, "id1");

  // Two Remove buttons; the one for the active profile must be disabled.
  int disabled_count = 0;
  int enabled_count = 0;
  for (auto *btn : sv.findChildren<QToolButton *>())
    {
      if (btn->toolTip() == "Remove")
        {
          if (btn->isEnabled())
            ++enabled_count;
          else
            ++disabled_count;
        }
    }
  EXPECT_EQ(disabled_count, 1);
  EXPECT_EQ(enabled_count, 1);
}

TEST(SettingsViewTest, SetProfiles_CalledTwice_ListRefreshed)
{
  SettingsView sv;
  auto p1 = make_profile("id1", "mpd", "Home MPD");
  sv.set_profiles({p1}, "id1");
  EXPECT_EQ(count_buttons(&sv, "Connect"), 0);

  // Add a second profile and refresh.
  auto p2 = make_profile("id2", "subsonic", "LMS");
  sv.set_profiles({p1, p2}, "id1");
  EXPECT_EQ(count_buttons(&sv, "Connect"), 1);
}

// ---------------------------------------------------------------------------
// Signal tests
// ---------------------------------------------------------------------------

TEST(SettingsViewTest, AddProfileRequested_EmittedOnAddButtonClick)
{
  SettingsView sv;
  sv.set_profiles({}, "");

  QSignalSpy spy(&sv, &SettingsView::add_profile_requested);
  auto *btn = find_button(&sv, "Add Server…");
  ASSERT_NE(btn, nullptr);
  btn->click();
  EXPECT_EQ(spy.count(), 1);
}

TEST(SettingsViewTest, SwitchBackendRequested_EmittedWithCorrectId)
{
  SettingsView sv;
  auto active = make_profile("id1", "mpd", "Home MPD");
  auto inactive = make_profile("id2", "subsonic", "LMS");
  sv.set_profiles({active, inactive}, "id1");

  QSignalSpy spy(&sv, &SettingsView::switch_backend_requested);
  auto *conn_btn = find_button(&sv, "Connect");
  ASSERT_NE(conn_btn, nullptr);
  conn_btn->click();

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().first().toString(), "id2");
}

TEST(SettingsViewTest, RemoveProfileRequested_EmittedWithCorrectId)
{
  SettingsView sv;
  auto active = make_profile("id1", "mpd", "Home MPD");
  auto inactive = make_profile("id2", "subsonic", "LMS");
  sv.set_profiles({active, inactive}, "id1");

  QSignalSpy spy(&sv, &SettingsView::remove_profile_requested);

  // Find the enabled Remove button (inactive profile).
  QToolButton *enabled_rem = nullptr;
  for (auto *btn : sv.findChildren<QToolButton *>())
    if (btn->toolTip() == "Remove" && btn->isEnabled())
      {
        enabled_rem = btn;
        break;
      }
  ASSERT_NE(enabled_rem, nullptr);
  enabled_rem->click();

  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().first().toString(), "id2");
}

// ---------------------------------------------------------------------------
// A failed connection must not leave the active row a dead end
// ---------------------------------------------------------------------------

TEST(SettingsViewTest, SetProfiles_ActiveButDisconnected_OffersRetry)
{
  SettingsView sv;
  auto p = make_profile("id1", "subsonic", "Ampache");
  sv.set_profiles({p}, "id1", /*connected=*/false);

  // Selected but never reached its server: the row must offer a way back in.
  EXPECT_EQ(count_buttons(&sv, "Retry"), 1);
}

TEST(SettingsViewTest, SetProfiles_ActiveAndConnected_OffersNoRetry)
{
  SettingsView sv;
  auto p = make_profile("id1", "subsonic", "Ampache");
  sv.set_profiles({p}, "id1", /*connected=*/true);

  EXPECT_EQ(count_buttons(&sv, "Retry"), 0);
  EXPECT_EQ(count_buttons(&sv, "Connect"), 0);
}

TEST(SettingsViewTest, RetryButton_EmitsSwitchBackendForActiveProfile)
{
  SettingsView sv;
  auto p = make_profile("id1", "subsonic", "Ampache");
  sv.set_profiles({p}, "id1", /*connected=*/false);

  QSignalSpy spy(&sv, &SettingsView::switch_backend_requested);
  auto *retry = find_button(&sv, "Retry");
  ASSERT_NE(retry, nullptr);
  retry->click();

  // Retry re-requests the profile that is already active — BackendController
  // must treat that as a reconnect, not as a no-op.
  ASSERT_EQ(spy.count(), 1);
  EXPECT_EQ(spy.first().first().toString(), "id1");
}
