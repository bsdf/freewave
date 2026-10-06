#include <gtest/gtest.h>

#include <QStyleOptionViewItem>

#include "ui/songlistitem.hh"
#include "ui/songlistmodel.hh"
#include "model/song.hh"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static song
make_song(const QString &uri, uint32_t duration_ms)
{
  song s;
  s.uri = uri;
  s.title = "Title " + uri;
  s.artist = "Artist";
  s.duration = duration_ms;
  return s;
}

// ---------------------------------------------------------------------------
// Duration column width — heart/add columns must not shift row to row
// ---------------------------------------------------------------------------

TEST(SongListItemTest, AddColumnStableAcrossTenMinuteBoundary)
{
  SongListModel model;
  model.set_data({
      make_song("a", 9 * 60 * 1000 + 59 * 1000), // 9:59
      make_song("b", 10 * 60 * 1000),            // 10:00 — one digit wider
  });

  SongListItem item;
  item.set_playlists_available(true);

  QStyleOptionViewItem option;
  option.rect = QRect(0, 0, 400, 40);

  auto rect_a = item.add_rect(option, model.index(0, 0));
  auto rect_b = item.add_rect(option, model.index(1, 0));

  EXPECT_EQ(rect_a.left(), rect_b.left())
      << "add/heart columns shifted when a row's duration crossed into an extra digit";
}

TEST(SongListItemTest, AddColumnWidensWhenListContainsAnHourLongTrack)
{
  SongListModel short_list;
  short_list.set_data({make_song("a", 9 * 60 * 1000 + 59 * 1000)}); // 9:59

  SongListModel long_list;
  long_list.set_data({
      make_song("a", 9 * 60 * 1000 + 59 * 1000), // 9:59
      make_song("b", 61 * 60 * 1000),            // 1:01:00 — h:mm:ss
  });

  SongListItem item;
  item.set_playlists_available(true);

  QStyleOptionViewItem option;
  option.rect = QRect(0, 0, 400, 40);

  auto rect_short = item.add_rect(option, short_list.index(0, 0));
  auto rect_long = item.add_rect(option, long_list.index(0, 0));

  EXPECT_LT(rect_long.left(), rect_short.left())
      << "the reserved duration column should widen to fit the hour-long track";
}

// ---------------------------------------------------------------------------
// Vertical alignment — number/duration/heart/add sit next to the title line,
// not centered across a two-line (title + artist) row
// ---------------------------------------------------------------------------

TEST(SongListItemTest, AddColumnAlignsToTopLineWhenArtistLineShown)
{
  SongListModel two_line_model;
  auto with_artist = make_song("a", 200000);
  with_artist.artist = "Various";
  two_line_model.set_data({with_artist});
  two_line_model.set_show_artist(true);

  SongListModel one_line_model;
  one_line_model.set_data({make_song("a", 200000)});

  SongListItem item;
  item.set_playlists_available(true);

  QStyleOptionViewItem option;
  option.rect = QRect(0, 0, 400, 60);

  auto rect_two_line = item.add_rect(option, two_line_model.index(0, 0));
  auto rect_one_line = item.add_rect(option, one_line_model.index(0, 0));

  EXPECT_LT(rect_two_line.height(), rect_one_line.height())
      << "the add/heart column should span only the title line, not the whole "
         "two-line row, when an artist line is shown below it";
  EXPECT_EQ(rect_two_line.top(), rect_one_line.top())
      << "both should still start flush at the row's top";
}
