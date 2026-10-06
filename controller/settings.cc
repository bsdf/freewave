#include "settings.hh"

#include <QSettings>

namespace {

constexpr auto GEOMETRY = "geometry";
constexpr auto ROUNDED_CORNERS = "rounded_corners";
constexpr auto DROP_SHADOW = "drop_shadow";
constexpr auto POLL_INTERVAL = "poll_interval";
constexpr auto QUEUE_DISPLAY = "queue_display";
constexpr auto QUEUE_STYLE = "queue_style";
constexpr auto QUEUE_COLOR_BAND = "queue_color_band";
constexpr auto UI_QUEUE_VISIBLE = "ui_queue_visible";
constexpr auto QUEUE_WIDTH = "queue_width";
constexpr auto UI_RECENT = "ui_recent";
constexpr auto BOOTLEG_STAMP = "bootleg_stamp";
constexpr auto AUDIO_CACHE_ENABLED = "audio_cache_enabled";
constexpr auto AUDIO_CACHE_BUDGET_MB = "audio_cache_budget_mb";
constexpr auto VISUALIZER_ENABLED = "visualizer_enabled";
constexpr auto VISUALIZER_PRESET = "visualizer_preset";
constexpr auto TRAY_ENABLED = "tray_enabled";
constexpr auto CLOSE_TO_TRAY = "close_to_tray";

}

auto
Settings::has_geometry() const -> bool
{
  QSettings s;
  return s.contains(GEOMETRY);
}

auto
Settings::geometry() const -> QByteArray
{
  QSettings s;
  return s.value(GEOMETRY, QByteArray()).toByteArray();
}

void
Settings::set_geometry(const QByteArray &value)
{
  QSettings s;
  s.setValue(GEOMETRY, value);
}

auto
Settings::rounded_corners() const -> bool
{
  QSettings s;
  return s.value(ROUNDED_CORNERS, false).toBool();
}

void
Settings::set_rounded_corners(bool value)
{
  QSettings s;
  s.setValue(ROUNDED_CORNERS, value);
}

auto
Settings::drop_shadow() const -> bool
{
  QSettings s;
  return s.value(DROP_SHADOW, false).toBool();
}

void
Settings::set_drop_shadow(bool value)
{
  QSettings s;
  s.setValue(DROP_SHADOW, value);
}

auto
Settings::poll_interval() const -> int
{
  QSettings s;
  return s.value(POLL_INTERVAL, 5).toInt();
}

void
Settings::set_poll_interval(int value)
{
  QSettings s;
  s.setValue(POLL_INTERVAL, value);
}

auto
Settings::queue_display() const -> QString
{
  QSettings s;
  return s.value(QUEUE_DISPLAY, "grouped").toString();
}

void
Settings::set_queue_display(const QString &value)
{
  QSettings s;
  s.setValue(QUEUE_DISPLAY, value);
}

auto
Settings::queue_style() const -> QString
{
  QSettings s;
  return s.value(QUEUE_STYLE, "light").toString();
}

void
Settings::set_queue_style(const QString &value)
{
  QSettings s;
  s.setValue(QUEUE_STYLE, value);
}

auto
Settings::queue_color_band() const -> bool
{
  QSettings s;
  return s.value(QUEUE_COLOR_BAND, false).toBool();
}

void
Settings::set_queue_color_band(bool value)
{
  QSettings s;
  s.setValue(QUEUE_COLOR_BAND, value);
}

auto
Settings::ui_queue_visible() const -> bool
{
  QSettings s;
  return s.value(UI_QUEUE_VISIBLE, false).toBool();
}

void
Settings::set_ui_queue_visible(bool value)
{
  QSettings s;
  s.setValue(UI_QUEUE_VISIBLE, value);
}

auto
Settings::queue_width() const -> int
{
  QSettings s;
  return s.value(QUEUE_WIDTH, 260).toInt();
}

void
Settings::set_queue_width(int value)
{
  QSettings s;
  s.setValue(QUEUE_WIDTH, value);
}

auto
Settings::ui_recent() const -> bool
{
  QSettings s;
  return s.value(UI_RECENT, false).toBool();
}

void
Settings::set_ui_recent(bool value)
{
  QSettings s;
  s.setValue(UI_RECENT, value);
}

auto
Settings::bootleg_stamp() const -> bool
{
  QSettings s;
  return s.value(BOOTLEG_STAMP, false).toBool();
}

void
Settings::set_bootleg_stamp(bool value)
{
  QSettings s;
  s.setValue(BOOTLEG_STAMP, value);
}

auto
Settings::audio_cache_enabled() const -> bool
{
  QSettings s;
  return s.value(AUDIO_CACHE_ENABLED, true).toBool();
}

void
Settings::set_audio_cache_enabled(bool value)
{
  QSettings s;
  s.setValue(AUDIO_CACHE_ENABLED, value);
}

auto
Settings::audio_cache_budget_mb() const -> int
{
  QSettings s;
  return s.value(AUDIO_CACHE_BUDGET_MB, 2048).toInt();
}

void
Settings::set_audio_cache_budget_mb(int value)
{
  QSettings s;
  s.setValue(AUDIO_CACHE_BUDGET_MB, value);
}

auto
Settings::visualizer_enabled() const -> bool
{
  QSettings s;
  return s.value(VISUALIZER_ENABLED, false).toBool();
}

void
Settings::set_visualizer_enabled(bool value)
{
  QSettings s;
  s.setValue(VISUALIZER_ENABLED, value);
}

auto
Settings::visualizer_preset() const -> QString
{
  QSettings s;
  return s.value(VISUALIZER_PRESET, "aurora").toString();
}

void
Settings::set_visualizer_preset(const QString &id)
{
  QSettings s;
  s.setValue(VISUALIZER_PRESET, id);
}

auto
Settings::tray_enabled() const -> bool
{
  QSettings s;
  return s.value(TRAY_ENABLED, false).toBool();
}

void
Settings::set_tray_enabled(bool value)
{
  QSettings s;
  s.setValue(TRAY_ENABLED, value);
}

auto
Settings::close_to_tray() const -> bool
{
  QSettings s;
  return s.value(CLOSE_TO_TRAY, false).toBool();
}

void
Settings::set_close_to_tray(bool value)
{
  QSettings s;
  s.setValue(CLOSE_TO_TRAY, value);
}
