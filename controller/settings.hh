#ifndef SETTINGS_HH
#define SETTINGS_HH

#include <QByteArray>
#include <QString>

// Typed facade over the global QSettings store for non-connection app/UI prefs.
// Connection config (MPD host/port, Subsonic URL/API key) lives elsewhere (ProfileStore).
// Defaults match the previous inline literals exactly.
class Settings {
public:
  auto has_geometry() const -> bool;
  auto geometry() const -> QByteArray;
  void set_geometry(const QByteArray &value);

  auto rounded_corners() const -> bool;
  void set_rounded_corners(bool value);

  auto drop_shadow() const -> bool;
  void set_drop_shadow(bool value);

  auto poll_interval() const -> int;
  void set_poll_interval(int value);

  auto queue_display() const -> QString;
  void set_queue_display(const QString &value);

  auto queue_style() const -> QString;
  void set_queue_style(const QString &value);

  auto queue_color_band() const -> bool;
  void set_queue_color_band(bool value);

  auto ui_queue_visible() const -> bool;
  void set_ui_queue_visible(bool value);

  auto queue_width() const -> int;
  void set_queue_width(int value);

  auto ui_recent() const -> bool;
  void set_ui_recent(bool value);

  auto bootleg_stamp() const -> bool;
  void set_bootleg_stamp(bool value);

  // On-disk cache of played tracks. On by default: a seek inside a cached track
  // is a local seek, which is the difference between instant and ten seconds on
  // a remote FLAC, and every failure mode of it degrades to streaming.
  auto audio_cache_enabled() const -> bool;
  void set_audio_cache_enabled(bool value);

  // Budget in MiB — the unit the settings spin box speaks. Default 2048.
  auto audio_cache_budget_mb() const -> int;
  void set_audio_cache_budget_mb(int value);

  auto visualizer_enabled() const -> bool;
  void set_visualizer_enabled(bool value);

  // Chosen visualization (string id matching a NowPlaying preset, e.g.
  // "albumwash"). Default is the album wash.
  auto visualizer_preset() const -> QString;
  void set_visualizer_preset(const QString &id);

  auto tray_enabled() const -> bool;
  void set_tray_enabled(bool value);

  auto close_to_tray() const -> bool;
  void set_close_to_tray(bool value);
};

#endif // SETTINGS_HH
