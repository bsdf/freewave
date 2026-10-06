#include "trayicon.hh"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QWidget>

TrayIcon::TrayIcon(QWidget *window, QObject *parent)
  : QObject(parent)
  , window(window)
{
  tray = new QSystemTrayIcon(QIcon::fromTheme("org.xeyes.Freewave"), this);
  tray->setToolTip("freewave");

  auto *menu = new QMenu(window);
  toggle_action = menu->addAction("Hide Window");
  connect(toggle_action, &QAction::triggered, this, &TrayIcon::toggle_window);
  connect(menu, &QMenu::aboutToShow, this, &TrayIcon::update_toggle_text);

  menu->addSeparator();
  auto *quit_action = menu->addAction("Quit");
  connect(quit_action, &QAction::triggered, this, &TrayIcon::quit_requested);

  tray->setContextMenu(menu);

  connect(tray, &QSystemTrayIcon::activated, this,
      [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
          toggle_window();
        else if (reason == QSystemTrayIcon::MiddleClick)
          emit play_pause_requested();
      });
}

auto
TrayIcon::is_supported() -> bool
{
  return QSystemTrayIcon::isSystemTrayAvailable();
}

auto
TrayIcon::set_enabled(bool enabled) -> void
{
  tray->setVisible(enabled && is_supported());
}

auto
TrayIcon::toggle_window() -> void
{
  if (window->isVisible() && !window->isMinimized())
    window->hide();
  else
    {
      window->showNormal();
      window->raise();
      window->activateWindow();
    }
  update_toggle_text();
}

auto
TrayIcon::update_toggle_text() -> void
{
  toggle_action->setText(
      window->isVisible() && !window->isMinimized() ? "Hide Window" : "Show Window");
}
