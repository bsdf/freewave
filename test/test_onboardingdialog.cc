// Drives the onboarding connection probe through its MPD path, which needs no
// external network: a refused port must warn (and reveal "Finish anyway"), and
// a socket that speaks the MPD greeting must be accepted and advance the wizard.

#include "ui/onboardingdialog.hh"

#include "controller/credentialstore.hh"
#include "controller/profilestore.hh"

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {

template<class Pred>
bool
wait_until(Pred pred, int timeout_ms = 3000)
{
  QElapsedTimer t;
  t.start();
  while (!pred() && t.elapsed() < timeout_ms)
    QTest::qWait(20);
  return pred();
}

// Pick the MPD backend, fill host/port, and trigger the connect step. The slots
// are private, so reach them by name (widgets carry object names for the same
// reason).
void
probe_mpd(OnboardingDialog &dlg, const QString &host, quint16 port)
{
  QMetaObject::invokeMethod(&dlg, "on_type_selected", Q_ARG(QString, QString("mpd")));
  dlg.findChild<QLineEdit *>("mpd_host")->setText(host);
  dlg.findChild<QLineEdit *>("mpd_port")->setText(QString::number(port));
  QMetaObject::invokeMethod(&dlg, "on_connect");
}

} // namespace

TEST(OnboardingProbeTest, MpdUnreachableWarnsButLetsUserFinish)
{
  OnboardingDialog dlg;
  dlg.show();
  probe_mpd(dlg, "127.0.0.1", 1); // port 1 is not connectable → refused

  auto *conn_label = dlg.findChild<QLabel *>("conn_label");
  auto *finish = dlg.findChild<QPushButton *>("finish_anyway_btn");
  ASSERT_TRUE(conn_label && finish);

  ASSERT_TRUE(wait_until([&] { return conn_label->text().startsWith("Couldn't connect"); }));
  EXPECT_TRUE(finish->isVisible());
}

TEST(OnboardingProbeTest, MpdGreetingIsAccepted)
{
  QTcpServer server;
  ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
  QObject::connect(&server, &QTcpServer::newConnection, &server, [&server] {
    auto *c = server.nextPendingConnection();
    c->write("OK MPD 0.23.0\n");
    c->flush();
  });

  OnboardingDialog dlg;
  dlg.show();
  probe_mpd(dlg, "127.0.0.1", server.serverPort());

  auto *conn_detail = dlg.findChild<QLabel *>("conn_detail");
  auto *finish = dlg.findChild<QPushButton *>("finish_anyway_btn");
  ASSERT_TRUE(conn_detail && finish);

  EXPECT_TRUE(wait_until([&] { return conn_detail->text() == "Connected"; }));
  EXPECT_FALSE(finish->isVisible());
}

// Real CredentialStore::write_blocking, real ProfileStore/QSettings — this
// branches on whether a Secret Service is reachable (none in this sandbox/CI).
// QSettings is redirected to a temp dir so neither branch touches the real
// user's profile list.
class OnboardingKeychainTest : public ::testing::Test {
protected:
  QTemporaryDir tmp;
  void SetUp() override
  {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    QCoreApplication::setOrganizationName("freewave_test");
    QCoreApplication::setApplicationName("onboardingdialog_test");
    QSettings().clear();
  }
};

// Regression test for the same bug as ProfileEditDialogKeychainTest, in the
// onboarding wizard's own "Open" handler: a failed keychain write was
// swallowed by a bare `return;` with zero user feedback.
TEST_F(OnboardingKeychainTest, WriteFailureWarnsAndDoesNotFinishSetup)
{
  OnboardingDialog dlg;
  dlg.show();

  QMetaObject::invokeMethod(&dlg, "on_type_selected", Q_ARG(QString, QString("subsonic")));
  dlg.findChild<QLineEdit *>("ss_url")->setText("https://example.org");
  dlg.findChild<QLineEdit *>("ss_api_key")->setText("test-secret-value");

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

  QMetaObject::invokeMethod(&dlg, "on_open");
  poll.stop();

  ProfileStore ps;
  auto profiles = ps.load();
  if (profiles.isEmpty())
    {
      // No Secret Service reachable: write failed, warning shown, wizard
      // did not mark setup complete.
      EXPECT_TRUE(warned);
      EXPECT_FALSE(QSettings().value("setup_complete").toBool());
      EXPECT_TRUE(dlg.isVisible());
    }
  else
    {
      // Real keyring present: normal open path, no warning.
      EXPECT_FALSE(warned);
      std::ignore = CredentialStore().remove_blocking(profiles.first().id, "api_key");
    }
}
