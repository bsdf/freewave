#include "ui/librarygridcontroller.hh"

#include <gtest/gtest.h>

// librarygrid::title_text + count_label are the pure label builders factored out
// of the library grid so the All/Recent wording and the album-count pluralization
// are testable without building the controller (which needs the full grid widget
// tree). The widget-bound logic (empty-state, per-view scroll restore,
// scroll-to-playing index search) is manual/live-verified.

TEST(LibraryGridTitle, AllVsRecent)
{
  EXPECT_EQ(librarygrid::title_text(false), "Library");
  EXPECT_EQ(librarygrid::title_text(true), "Recently added");
}

TEST(LibraryGridCount, PluralizesAndPicksSort)
{
  EXPECT_EQ(librarygrid::count_label(0, false), "0 albums ～ sorted by artist");
  EXPECT_EQ(librarygrid::count_label(1, false), "1 album ～ sorted by artist");
  EXPECT_EQ(librarygrid::count_label(2, false), "2 albums ～ sorted by artist");
}

TEST(LibraryGridCount, RecentUsesAddedSort)
{
  EXPECT_EQ(librarygrid::count_label(1, true), "1 album ～ sorted by added");
  EXPECT_EQ(librarygrid::count_label(5, true), "5 albums ～ sorted by added");
}
