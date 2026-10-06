#include "screensaverinhibitor.hh"

#ifdef ENABLE_DBUS_INHIBIT
#include <QDBusConnection>
#include <QDBusMessage>
#include <QString>

#include <spdlog/spdlog.h>

namespace {
constexpr auto SERVICE = "org.freedesktop.ScreenSaver";
constexpr auto PATH = "/org/freedesktop/ScreenSaver";
constexpr auto IFACE = "org.freedesktop.ScreenSaver";
} // namespace

auto
ScreenSaverInhibitor::inhibit() -> void
{
  if (active)
    return;
  auto msg = QDBusMessage::createMethodCall(
      SERVICE, PATH, IFACE, QStringLiteral("Inhibit"));
  msg << QStringLiteral("freewave") << QStringLiteral("Fullscreen Now Playing");
  const auto reply = QDBusConnection::sessionBus().call(msg);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
    {
      // No screensaver service on the bus (or it refused) — leave it be.
      spdlog::debug("ScreenSaver: Inhibit unavailable ({})",
          reply.errorMessage().toStdString());
      return;
    }
  cookie = reply.arguments().constFirst().toUInt();
  active = true;
}

auto
ScreenSaverInhibitor::uninhibit() -> void
{
  if (!active)
    return;
  auto msg = QDBusMessage::createMethodCall(
      SERVICE, PATH, IFACE, QStringLiteral("UnInhibit"));
  msg << cookie;
  QDBusConnection::sessionBus().send(msg); // fire-and-forget; no reply expected
  cookie = 0;
  active = false;
}

ScreenSaverInhibitor::~ScreenSaverInhibitor() { uninhibit(); }

#else // built without D-Bus — inhibition is a no-op

auto
ScreenSaverInhibitor::inhibit() -> void
{
}
auto
ScreenSaverInhibitor::uninhibit() -> void
{
}
ScreenSaverInhibitor::~ScreenSaverInhibitor() = default;

#endif // ENABLE_DBUS_INHIBIT
