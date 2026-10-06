#include "ui/profileeditdialog.hh"
#include "ui/segmentedcontrol.hh"

#include "controller/credentialstore.hh"
#include "controller/profilestore.hh"

#include <gtest/gtest.h>
#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

// profileedit::make_profile is the pure Add-form mapping factored out of the
// dialog: raw field strings -> a new BackendProfile (identity + params), with the
// MPD/Subsonic + name/host/port fallback rules but no UUID generation or secret
// store. These pin those rules without constructing a widget.

using profileedit::make_profile;

TEST(ProfileEditMakeProfile, MpdBasic)
{
  auto p = make_profile({.is_mpd = true,
      .id = "id-1",
      .name = "Home",
      .fallback_name = "MPD",
      .host = "musicbox",
      .port = "6601"});
  EXPECT_EQ(p.id, "id-1");
  EXPECT_EQ(p.type, "mpd");
  EXPECT_EQ(p.name, "Home");
  EXPECT_EQ(p.params.value("host").toString(), "musicbox");
  EXPECT_EQ(p.params.value("port").toUInt(), 6601u);
  EXPECT_FALSE(p.params.contains("url"));
}

TEST(ProfileEditMakeProfile, MpdEmptyHostFallsBackToLocalhost)
{
  auto p = make_profile({.is_mpd = true, .id = "x", .fallback_name = "MPD", .host = "   "});
  EXPECT_EQ(p.params.value("host").toString(), "localhost");
}

TEST(ProfileEditMakeProfile, MpdEmptyPortFallsBackTo6600)
{
  auto p = make_profile({.is_mpd = true, .id = "x", .fallback_name = "MPD", .host = "h", .port = ""});
  EXPECT_EQ(p.params.value("port").toUInt(), 6600u);
}

TEST(ProfileEditMakeProfile, EmptyNameUsesFallback)
{
  auto p = make_profile({.is_mpd = true, .id = "x", .name = "   ", .fallback_name = "MPD", .host = "h"});
  EXPECT_EQ(p.name, "MPD");
}

TEST(ProfileEditMakeProfile, NameIsTrimmed)
{
  auto p = make_profile({.is_mpd = true, .id = "x", .name = "  Den  ", .fallback_name = "MPD", .host = "h"});
  EXPECT_EQ(p.name, "Den");
}

TEST(ProfileEditMakeProfile, SubsonicSetsUrlNotHostPort)
{
  auto p = make_profile({.is_mpd = false,
      .id = "id-2",
      .name = "LMS",
      .fallback_name = "Subsonic",
      .url = "  https://example.org  "});
  EXPECT_EQ(p.type, "subsonic");
  EXPECT_EQ(p.name, "LMS");
  EXPECT_EQ(p.params.value("url").toString(), "https://example.org"); // trimmed
  EXPECT_FALSE(p.params.contains("host"));
  EXPECT_FALSE(p.params.contains("port"));
}

TEST(ProfileEditMakeProfile, SubsonicEmptyNameUsesFallback)
{
  auto p = make_profile({.is_mpd = false, .id = "x", .name = "", .fallback_name = "Subsonic", .url = "u"});
  EXPECT_EQ(p.name, "Subsonic");
}

TEST(ProfileEditMakeProfile, SubsonicDefaultsToApiKeyAuthMode)
{
  auto p = make_profile({.is_mpd = false, .id = "x", .fallback_name = "Subsonic", .url = "u"});
  EXPECT_EQ(p.params.value("auth_mode").toString(), "apikey");
  EXPECT_FALSE(p.params.contains("username"));
}

TEST(ProfileEditMakeProfile, SubsonicUserpassAuthSetsUsername)
{
  auto p = make_profile({.is_mpd = false,
      .id = "x",
      .fallback_name = "Subsonic",
      .url = "https://example.org",
      .userpass_auth = true,
      .username = "  bob  "});
  EXPECT_EQ(p.params.value("auth_mode").toString(), "userpass");
  EXPECT_EQ(p.params.value("username").toString(), "bob"); // trimmed
}

// find_segment finds the checkable QPushButton with the given label inside a
// SegmentedControl and clicks it (a real click, so QButtonGroup::idClicked
// fires and drives the dialog's visibility-toggle slots — set_current()
// alone would not).
static void
click_segment(SegmentedControl *control, const QString &label)
{
  for (auto *btn : control->findChildren<QPushButton *>())
    if (btn->text() == label)
      {
        btn->click();
        return;
      }
  FAIL() << "no segment labeled " << label.toStdString();
}

// QFormLayout::addRow(QLabel*, QWidget*) sets the label as the field's buddy,
// so this finds the QLineEdit for a given label text without needing a
// pointer into the dialog's private locals.
static QLineEdit *
find_field(const QWidget &dlg, const QString &label_text)
{
  for (auto *lbl : dlg.findChildren<QLabel *>())
    if (lbl->text() == label_text)
      return qobject_cast<QLineEdit *>(lbl->buddy());
  return nullptr;
}

// Regression test for a real bug: QFormLayout::setRowVisible() doesn't
// synchronously recompute geometry (the form's own sub-layout needs
// reactivating, not just the dialog's outer layout), and QLayout::activate()
// bakes the layout's minimum size in as a floor — both left the Add Server
// dialog stuck at its tallest-ever height after toggling Subsonic auth mode
// back and forth.
TEST(ProfileEditDialogSizing, AddDialog_AuthModeToggle_RoundTripRestoresHeight)
{
  ProfileEditDialog dlg(ProfileEditDialog::Mode::Add, BackendProfile{});
  dlg.show();
  qApp->processEvents();

  auto controls = dlg.findChildren<SegmentedControl *>();
  ASSERT_EQ(controls.size(), 2);
  auto *type_control = controls[0];
  auto *auth_control = controls[1];

  click_segment(type_control, "Subsonic");
  qApp->processEvents();
  const int apikey_height = dlg.height();
  const int field_x = find_field(dlg, "API Key")->x();

  click_segment(auth_control, "Username/Password");
  qApp->processEvents();
  EXPECT_GT(dlg.height(), apikey_height); // extra username+password rows
  // "Username"/"Password" are wider than "API Key" — a non-fixed label
  // column would shift the field column here.
  EXPECT_EQ(find_field(dlg, "Username")->x(), field_x);
  EXPECT_EQ(find_field(dlg, "Password")->x(), field_x);

  click_segment(auth_control, "API Key");
  qApp->processEvents();
  EXPECT_EQ(dlg.height(), apikey_height);
  EXPECT_EQ(find_field(dlg, "API Key")->x(), field_x);
}

// Real CredentialStore::write_blocking, real ProfileStore — this branches on
// whether a Secret Service is reachable (none in this sandbox/CI, matching
// CredentialStoreTest's env-gated skip). QSettings is redirected to a temp
// dir so neither branch touches the real user's profile list.
class ProfileEditDialogKeychainTest : public ::testing::Test {
protected:
  QTemporaryDir tmp;
  void SetUp() override
  {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    QCoreApplication::setOrganizationName("freewave_test");
    QCoreApplication::setApplicationName("profileeditdialog_test");
    QSettings().clear();
  }
};

// Regression test for a real bug: a failed keychain write was swallowed by a
// bare `return;` in the Save handler — no message, dialog just appeared to do
// nothing. On a fresh machine with no Secret Service running, this made it
// impossible to add a Subsonic profile with zero feedback as to why.
TEST_F(ProfileEditDialogKeychainTest, WriteFailureWarnsAndKeepsDialogOpen)
{
  // WA_DeleteOnClose self-deletes the dialog on accept(); heap-allocate so a
  // successful accept (real-keyring branch) doesn't double-free a stack local.
  auto *dlg = new ProfileEditDialog(ProfileEditDialog::Mode::Add, BackendProfile{});
  dlg->show();
  qApp->processEvents();

  auto controls = dlg->findChildren<SegmentedControl *>();
  ASSERT_EQ(controls.size(), 2);
  click_segment(controls[0], "Subsonic");
  qApp->processEvents();

  find_field(*dlg, "URL")->setText("https://example.org");
  find_field(*dlg, "API Key")->setText("test-secret-value");

  bool warned = false;
  QTimer poll;
  poll.setInterval(5);
  QObject::connect(&poll, &QTimer::timeout, [&] {
    for (auto *w : qApp->topLevelWidgets())
      if (auto *box = qobject_cast<QMessageBox *>(w); box && box->isVisible())
        {
          warned = true;
          box->accept();
          poll.stop();
        }
  });
  poll.start();

  auto *btn_box = dlg->findChild<QDialogButtonBox *>();
  btn_box->button(QDialogButtonBox::Ok)->click();
  poll.stop();

  ProfileStore ps;
  auto profiles = ps.load();
  if (profiles.isEmpty())
    {
      // No Secret Service reachable: write failed, warning shown, nothing
      // persisted, dialog stayed open for the user to see the message.
      EXPECT_TRUE(warned);
      EXPECT_TRUE(dlg->isVisible());
      delete dlg;
    }
  else
    {
      // Real keyring present: normal save path, no warning.
      EXPECT_FALSE(warned);
      std::ignore = CredentialStore().remove_blocking(profiles.first().id, "api_key");
      qApp->processEvents(); // let WA_DeleteOnClose's deleteLater run
    }
}

TEST(ProfileEditDialogSizing, EditDialog_AuthModeToggle_RoundTripRestoresHeight)
{
  BackendProfile existing{.id = "x", .type = "subsonic", .name = "Test"};
  existing.params["url"] = "https://example.org";
  existing.params["auth_mode"] = "apikey";

  ProfileEditDialog dlg(ProfileEditDialog::Mode::Edit, existing);
  dlg.show();
  qApp->processEvents();

  auto controls = dlg.findChildren<SegmentedControl *>();
  ASSERT_EQ(controls.size(), 1);
  auto *auth_control = controls[0];
  const int apikey_height = dlg.height();
  const int field_x = find_field(dlg, "API Key")->x();

  click_segment(auth_control, "Username/Password");
  qApp->processEvents();
  EXPECT_GT(dlg.height(), apikey_height);
  EXPECT_EQ(find_field(dlg, "Username")->x(), field_x);
  EXPECT_EQ(find_field(dlg, "Password")->x(), field_x);

  click_segment(auth_control, "API Key");
  qApp->processEvents();
  EXPECT_EQ(dlg.height(), apikey_height);
  EXPECT_EQ(find_field(dlg, "API Key")->x(), field_x);
}
