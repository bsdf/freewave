#ifndef TRAYICON_HH
#define TRAYICON_HH

#include <QObject>

class QWidget;
class QSystemTrayIcon;
class QAction;

// System tray icon that shows/hides its owning window. A no-op everywhere
// set_enabled(true) is called without a tray-capable desktop (is_supported()
// is false) — the QSystemTrayIcon simply never becomes visible.
class TrayIcon : public QObject {
  Q_OBJECT
public:
  explicit TrayIcon(QWidget *window, QObject *parent = nullptr);

  static auto is_supported() -> bool;

  auto set_enabled(bool enabled) -> void;

signals:
  // Quitting from the tray menu needs the same teardown as a normal window
  // close, so this is a request rather than a direct qApp->quit() — the owner
  // decides how to get there.
  void quit_requested();
  void play_pause_requested();

private:
  auto toggle_window() -> void;
  auto update_toggle_text() -> void;

  QWidget *window;
  QSystemTrayIcon *tray;
  QAction *toggle_action;
};

#endif // TRAYICON_HH
