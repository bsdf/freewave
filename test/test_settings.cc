#include "controller/settings.hh"
#include <gtest/gtest.h>
#include <QByteArray>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

class SettingsTest : public ::testing::Test {
protected:
  QTemporaryDir tmp;
  void SetUp() override
  {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    QCoreApplication::setOrganizationName("freewave_test");
    QCoreApplication::setApplicationName("settings_test");
    QSettings().clear();
  }
};

TEST_F(SettingsTest, DefaultsMatchLegacyLiterals)
{
  Settings s;
  EXPECT_FALSE(s.has_geometry());
  EXPECT_TRUE(s.geometry().isEmpty());
  EXPECT_FALSE(s.rounded_corners());
  EXPECT_FALSE(s.drop_shadow());
  EXPECT_EQ(s.poll_interval(), 5);
  EXPECT_EQ(s.queue_display().toStdString(), "grouped");
  EXPECT_EQ(s.queue_style().toStdString(), "light");
  EXPECT_FALSE(s.queue_color_band());
  EXPECT_FALSE(s.ui_queue_visible());
  EXPECT_FALSE(s.ui_recent());
  EXPECT_EQ(s.visualizer_preset().toStdString(), "aurora");
  EXPECT_FALSE(s.tray_enabled());
  EXPECT_FALSE(s.close_to_tray());
}

TEST_F(SettingsTest, RoundTripPersistsAcrossInstances)
{
  {
    Settings s;
    s.set_geometry(QByteArray("geomblob"));
    s.set_rounded_corners(true);
    s.set_drop_shadow(true);
    s.set_poll_interval(11);
    s.set_queue_display("compact");
    s.set_queue_style("dark");
    s.set_queue_color_band(true);
    s.set_ui_queue_visible(true);
    s.set_ui_recent(true);
    s.set_visualizer_preset("ridgeline");
    s.set_tray_enabled(true);
    s.set_close_to_tray(true);
  }
  Settings s2;
  EXPECT_TRUE(s2.has_geometry());
  EXPECT_EQ(s2.geometry(), QByteArray("geomblob"));
  EXPECT_TRUE(s2.rounded_corners());
  EXPECT_TRUE(s2.drop_shadow());
  EXPECT_EQ(s2.poll_interval(), 11);
  EXPECT_EQ(s2.queue_display().toStdString(), "compact");
  EXPECT_EQ(s2.queue_style().toStdString(), "dark");
  EXPECT_TRUE(s2.queue_color_band());
  EXPECT_TRUE(s2.ui_queue_visible());
  EXPECT_TRUE(s2.ui_recent());
  EXPECT_EQ(s2.visualizer_preset().toStdString(), "ridgeline");
  EXPECT_TRUE(s2.tray_enabled());
  EXPECT_TRUE(s2.close_to_tray());
}

TEST_F(SettingsTest, AudioCacheDefaultsToOnAtTwoGiB)
{
  Settings s;
  EXPECT_TRUE(s.audio_cache_enabled());
  EXPECT_EQ(s.audio_cache_budget_mb(), 2048);
}

TEST_F(SettingsTest, AudioCacheSettingsRoundTrip)
{
  {
    Settings s;
    s.set_audio_cache_enabled(false);
    s.set_audio_cache_budget_mb(8192);
  }
  Settings s;
  EXPECT_FALSE(s.audio_cache_enabled());
  EXPECT_EQ(s.audio_cache_budget_mb(), 8192);
}
