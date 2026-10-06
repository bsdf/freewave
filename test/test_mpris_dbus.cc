// Round-trip tests against a real session bus. Skipped when no bus is reachable
// (CI, minimal sandboxes), mirroring the GStreamer integration tests' gating.

#include <gtest/gtest.h>

#include "mprislib/qmprisserver.hh"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QSignalSpy>
#include <QVariantMap>
#include <QDeadlineTimer>
#include <QtTest/QTest>

using mpris::PlaybackStatus;
using mpris::QMprisServer;

namespace {

constexpr auto OBJECT_PATH = "/org/mpris/MediaPlayer2";
constexpr auto PLAYER_IFACE = "org.mpris.MediaPlayer2.Player";

auto
bus_available() -> bool
{
  return QDBusConnection::sessionBus().isConnected();
}

// Per-process-unique service so parallel ctest shards don't collide.
auto
unique_suffix() -> QString
{
  return QStringLiteral("freewavetest_%1").arg(QCoreApplication::applicationPid());
}

// Spin the event loop until pred() holds or the deadline passes. A bare
// processEvents() returns before a signal has round-tripped through the bus
// daemon, so async signal delivery needs an actual wait.
template<class Pred>
auto
wait_until(Pred pred, int ms = 2000) -> void
{
  QDeadlineTimer deadline(ms);
  while (!pred() && !deadline.hasExpired())
    QTest::qWait(10);
}

// Receiver for the raw PropertiesChanged D-Bus signal (QDBusConnection::connect
// needs a real slot, not a functor).
class PropsReceiver : public QObject {
  Q_OBJECT
public:
  QString iface;
  QVariantMap changed;
  QStringList invalidated;
  int count = 0;
public slots:
  void on_changed(QString i, QVariantMap c, QStringList inv)
  {
    iface = i;
    changed = c;
    invalidated = inv;
    ++count;
  }
};

class SeekReceiver : public QObject {
  Q_OBJECT
public:
  int count = 0;
  qlonglong us = 0;
public slots:
  void on_seeked(qlonglong v)
  {
    us = v;
    ++count;
  }
};

} // namespace

TEST(MprisDBus, MethodCallEmitsRequest)
{
  if (!bus_available())
    GTEST_SKIP() << "no session bus";

  QMprisServer::Config cfg;
  cfg.serviceSuffix = unique_suffix();
  QMprisServer server(cfg);
  ASSERT_TRUE(server.registerOnBus());
  const QString service = QStringLiteral("org.mpris.MediaPlayer2.") + cfg.serviceSuffix;

  QSignalSpy next_spy(&server, &QMprisServer::nextRequested);
  QSignalSpy vol_spy(&server, &QMprisServer::volumeRequested);

  QDBusInterface player(service, OBJECT_PATH, PLAYER_IFACE, QDBusConnection::sessionBus());
  player.call("Next");
  player.setProperty("Volume", 0.42); // writable property → volumeRequested
  wait_until([&] { return next_spy.count() > 0 && vol_spy.count() > 0; });

  EXPECT_EQ(next_spy.count(), 1);
  ASSERT_EQ(vol_spy.count(), 1);
  EXPECT_DOUBLE_EQ(vol_spy.at(0).at(0).toDouble(), 0.42);
}

TEST(MprisDBus, PropertyGetReturnsState)
{
  if (!bus_available())
    GTEST_SKIP() << "no session bus";

  QMprisServer::Config cfg;
  cfg.serviceSuffix = unique_suffix();
  cfg.identity = "Freewave RT";
  QMprisServer server(cfg);
  ASSERT_TRUE(server.registerOnBus());
  const QString service = QStringLiteral("org.mpris.MediaPlayer2.") + cfg.serviceSuffix;

  server.setPlaybackStatus(PlaybackStatus::Playing);

  QDBusInterface props(service, OBJECT_PATH, "org.freedesktop.DBus.Properties",
      QDBusConnection::sessionBus());
  QDBusReply<QVariant> status = props.call("Get", PLAYER_IFACE, "PlaybackStatus");
  ASSERT_TRUE(status.isValid());
  EXPECT_EQ(status.value().toString(), "Playing");

  QDBusReply<QVariant> identity = props.call("Get", "org.mpris.MediaPlayer2", "Identity");
  ASSERT_TRUE(identity.isValid());
  EXPECT_EQ(identity.value().toString(), "Freewave RT");
}

// A burst of setters within one event-loop turn must coalesce into a single
// PropertiesChanged, and Position must never appear in it (spec requirement).
TEST(MprisDBus, PropertiesChangedIsBatchedAndExcludesPosition)
{
  if (!bus_available())
    GTEST_SKIP() << "no session bus";

  QMprisServer::Config cfg;
  cfg.serviceSuffix = unique_suffix();
  QMprisServer server(cfg);
  ASSERT_TRUE(server.registerOnBus());
  const QString service = QStringLiteral("org.mpris.MediaPlayer2.") + cfg.serviceSuffix;

  PropsReceiver rx;
  ASSERT_TRUE(QDBusConnection::sessionBus().connect(service, OBJECT_PATH,
      "org.freedesktop.DBus.Properties", "PropertiesChanged", &rx,
      SLOT(on_changed(QString, QVariantMap, QStringList))));

  server.setPlaybackStatus(PlaybackStatus::Playing);
  server.setShuffle(true);
  server.setVolume(0.3);
  server.updatePosition(5'000'000); // must NOT trigger a PropertiesChanged entry
  wait_until([&] { return rx.count > 0; });

  EXPECT_EQ(rx.count, 1) << "all changes should batch into one signal";
  EXPECT_EQ(rx.iface, PLAYER_IFACE);
  EXPECT_TRUE(rx.changed.contains("PlaybackStatus"));
  EXPECT_TRUE(rx.changed.contains("Shuffle"));
  EXPECT_TRUE(rx.changed.contains("Volume"));
  EXPECT_FALSE(rx.changed.contains("Position"));
}

TEST(MprisDBus, SeekedSignalDelivered)
{
  if (!bus_available())
    GTEST_SKIP() << "no session bus";

  QMprisServer::Config cfg;
  cfg.serviceSuffix = unique_suffix();
  QMprisServer server(cfg);
  ASSERT_TRUE(server.registerOnBus());
  const QString service = QStringLiteral("org.mpris.MediaPlayer2.") + cfg.serviceSuffix;

  SeekReceiver rx;
  ASSERT_TRUE(QDBusConnection::sessionBus().connect(service, OBJECT_PATH, PLAYER_IFACE,
      "Seeked", &rx, SLOT(on_seeked(qlonglong))));

  server.emitSeeked(12'345'000);
  wait_until([&] { return rx.count > 0; });

  EXPECT_EQ(rx.count, 1);
  EXPECT_EQ(rx.us, 12'345'000);
  EXPECT_EQ(server.position(), 12'345'000);
}

#include "test_mpris_dbus.moc"
