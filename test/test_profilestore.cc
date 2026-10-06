#include "controller/profilestore.hh"
#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

class ProfileStoreTest : public ::testing::Test {
protected:
  QTemporaryDir tmp;
  void SetUp() override
  {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());
    QCoreApplication::setOrganizationName("freewave_test");
    QCoreApplication::setApplicationName("profilestore_test");
    QSettings().clear();
  }
};

TEST_F(ProfileStoreTest, AddRemoveRoundTrip)
{
  ProfileStore ps;

  BackendProfile p1{.id = "profile-1", .type = "mpd", .name = "First", .params = {{"host", "host1"}}};
  BackendProfile p2{.id = "profile-2", .type = "mpd", .name = "Second", .params = {{"host", "host2"}}};

  ps.add(p1);
  ps.add(p2);

  auto profiles = ps.load();
  EXPECT_EQ(profiles.size(), 2);

  ps.remove("profile-1");

  profiles = ps.load();
  EXPECT_EQ(profiles.size(), 1);
  EXPECT_EQ(profiles[0].id, "profile-2");
}

TEST_F(ProfileStoreTest, RemoveActiveProfile)
{
  ProfileStore ps;

  BackendProfile p1{.id = "id1", .type = "mpd", .name = "P1"};
  BackendProfile p2{.id = "id2", .type = "mpd", .name = "P2"};

  ps.add(p1);
  ps.add(p2);
  ps.set_active_id("id1");

  ps.remove("id1");

  auto profiles = ps.load();
  EXPECT_EQ(profiles.size(), 1);
  EXPECT_EQ(ps.active_id(), "id2");
}

TEST_F(ProfileStoreTest, ActiveProfileWithOneProfile)
{
  ProfileStore ps;

  BackendProfile p{.id = "only-one", .type = "mpd", .name = "Only", .params = {{"host", "localhost"}}};

  ps.add(p);

  auto active = ps.active_profile();
  EXPECT_TRUE(active.has_value());
  EXPECT_EQ(active->id, "only-one");
}

TEST_F(ProfileStoreTest, UpdateProfile)
{
  ProfileStore ps;

  BackendProfile p{.id = "update-me", .type = "mpd", .name = "Original", .params = {{"host", "original-host"}}};

  ps.add(p);

  p.name = "Updated";
  p.params["host"] = "updated-host";
  ps.update(p);

  auto profiles = ps.load();
  EXPECT_EQ(profiles.size(), 1);
  EXPECT_EQ(profiles[0].name, "Updated");
  EXPECT_EQ(profiles[0].params["host"].toString(), "updated-host");
}
