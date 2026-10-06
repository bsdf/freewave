#include "controller/backendfactory.hh"
#include "controller/backend.hh"
#include "controller/librarymanager.hh"
#include "controller/profilestore.hh"
#include <gtest/gtest.h>
#include <memory>

TEST(BackendFactoryTest, UnknownTypeReturnsNull)
{
  auto libman = std::make_shared<LibraryManager>();
  BackendProfile p{.id = "x", .type = "bogus", .name = "Bogus"};
  auto be = BackendFactory::create(p, libman);
  EXPECT_EQ(be, nullptr);
}

#ifdef ENABLE_MPD
TEST(BackendFactoryTest, MpdProfileBuildsBackend)
{
  auto libman = std::make_shared<LibraryManager>();
  BackendProfile p{.id = "m", .type = "mpd", .name = "MPD"};
  auto be = BackendFactory::create(p, libman);
  EXPECT_NE(be, nullptr);
}

TEST(BackendFactoryTest, MpdProfileWithParams_BuildsBackend)
{
  auto libman = std::make_shared<LibraryManager>();
  BackendProfile p{.id = "m2",
      .type = "mpd",
      .name = "MPD",
      .params = {{"host", "myserver.local"}, {"port", 9090u}}};
  auto be = BackendFactory::create(p, libman);
  EXPECT_NE(be, nullptr);
}

TEST(BackendFactoryTest, MpdProfileWithoutParams_UsesDefaults)
{
  auto libman = std::make_shared<LibraryManager>();
  // No host/port params — factory must use defaults, not crash
  BackendProfile p{.id = "m3", .type = "mpd", .name = "MPD"};
  auto be = BackendFactory::create(p, libman);
  EXPECT_NE(be, nullptr);
}
#endif
