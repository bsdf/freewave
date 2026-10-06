#ifndef SETTINGSVIEW_HH
#define SETTINGSVIEW_HH

#include "controller/profilestore.hh"
#include "toggle.hh"

#include <QList>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

class QLabel;

class SettingsView : public QWidget {
  Q_OBJECT
public:
  explicit SettingsView(QWidget *parent = nullptr);

  auto rounded_corners() const -> bool;
  auto drop_shadow() const -> bool;
  auto poll_interval_minutes() const -> int;

  // connected reports whether the active profile actually reached its server;
  // when it did not, its row offers Retry instead of claiming a live connection.
  auto set_profiles(const QList<BackendProfile> &profiles, const QString &active_id,
      bool connected = true) -> void;
  auto set_poll_supported(bool supported) -> void;
  // The whole Storage section is hidden for a backend that streams server-side,
  // where there is nothing for a client cache to hold.
  auto set_audio_cache_supported(bool supported) -> void;
  // How much the cache is currently holding, for the row's hint line.
  auto set_audio_cache_usage(qint64 bytes) -> void;
  // The whole System Tray section is hidden on a desktop with no tray to show
  // an icon on.
  auto set_tray_supported(bool supported) -> void;

signals:
  void rounded_corners_changed(bool value);
  void drop_shadow_changed(bool value);
  void poll_interval_changed(int minutes);
  void open_log_requested();
  void close_requested();
  void queue_display_changed(QString mode);
  void queue_style_changed(QString style);
  void color_band_changed(bool value);
  void bootleg_stamp_changed(bool value);
  void audio_cache_enabled_changed(bool value);
  void audio_cache_budget_changed(int mib);
  void clear_audio_cache_requested();
  void tray_enabled_changed(bool value);
  void close_to_tray_changed(bool value);

  void switch_backend_requested(QString id);
  void add_profile_requested();
  void edit_profile_requested(QString id);
  void remove_profile_requested(QString id);

private:
  Toggle *rounded_cb;
  Toggle *shadow_cb;
  Toggle *color_band_cb;
  Toggle *bootleg_cb;
  QSpinBox *poll_spin;
  QWidget *poll_row = nullptr;
  QVBoxLayout *servers_layout = nullptr;

  Toggle *audio_cache_cb = nullptr;
  QSpinBox *audio_cache_spin = nullptr;
  QLabel *audio_cache_usage_lbl = nullptr;
  QWidget *storage_section = nullptr;

  Toggle *tray_enabled_cb = nullptr;
  Toggle *close_to_tray_cb = nullptr;
  QWidget *tray_section = nullptr;
};

#endif // SETTINGSVIEW_HH
