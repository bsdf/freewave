#ifndef SCREENSAVERINHIBITOR_HH
#define SCREENSAVERINHIBITOR_HH

// Keeps the desktop screensaver and display-blanking (DPMS) suppressed while
// active, via the org.freedesktop.ScreenSaver session-bus service (KDE, GNOME,
// XFCE, …). Degrades to a no-op when built without D-Bus (ENABLE_DBUS_INHIBIT) or
// when no such service is on the bus. Both calls are idempotent; the destructor
// releases any outstanding inhibition.
class ScreenSaverInhibitor {
public:
  ~ScreenSaverInhibitor();
  auto inhibit() -> void;
  auto uninhibit() -> void;

private:
  unsigned cookie = 0; // org.freedesktop.ScreenSaver Inhibit handle
  bool active = false;
};

#endif // SCREENSAVERINHIBITOR_HH
