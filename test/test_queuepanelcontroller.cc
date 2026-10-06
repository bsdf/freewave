#include "ui/queuepanelcontroller.hh"

#include "controller/albumartmanager.hh"
#include "controller/librarymanager.hh"
#include "ui/queuelistmodel.hh"
#include "ui/queuelistview.hh"

#include <QApplication>
#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

// queuepanel::display_mode_from_string is the pure mapping factored out of the
// queue panel so the saved-preference -> display-enum rule (including the
// unknown -> Grouped default) is testable without building a panel.

using QM = QueueListItem::QueueDisplayMode;

TEST(QueueDisplayMode, Compact)
{
  EXPECT_EQ(queuepanel::display_mode_from_string("compact"), QM::Compact);
}

TEST(QueueDisplayMode, Full)
{
  EXPECT_EQ(queuepanel::display_mode_from_string("full"), QM::Full);
}

TEST(QueueDisplayMode, Grouped)
{
  EXPECT_EQ(queuepanel::display_mode_from_string("grouped"), QM::Grouped);
}

TEST(QueueDisplayMode, UnknownFallsBackToGrouped)
{
  EXPECT_EQ(queuepanel::display_mode_from_string(""), QM::Grouped);
  EXPECT_EQ(queuepanel::display_mode_from_string("bogus"), QM::Grouped);
}

TEST(QueueDisplayMode, AutoStringFallsBackToGrouped)
{
  // Older builds don't know "auto"; the controller branches on it before
  // reaching display_mode_from_string, so this documents the fallback.
  EXPECT_EQ(queuepanel::display_mode_from_string("auto"), QM::Grouped);
}

// queuepanel::auto_display_mode — Grouped iff the queue reads as album
// listening (consecutive runs from the same albums), Full otherwise.

namespace {
auto
make_song(const QString &hash) -> song
{
  return song("uri", "title", "artist", 1, 1, 1000, hash);
}

auto
make_run(const QString &hash, int count) -> QList<song>
{
  QList<song> out;
  for (int i = 0; i < count; ++i)
    out.append(make_song(hash));
  return out;
}
} // namespace

TEST(AutoDisplayMode, EmptyQueueIsGrouped)
{
  EXPECT_EQ(queuepanel::auto_display_mode({}, false), QM::Grouped);
}

TEST(AutoDisplayMode, OneFullAlbumIsGrouped)
{
  EXPECT_EQ(queuepanel::auto_display_mode(make_run("A", 8), false), QM::Grouped);
}

TEST(AutoDisplayMode, SingleRunShorterThanMinRunIsGrouped)
{
  // A 2-track EP queued alone is unambiguously "an album on the queue" even
  // though it fails the MIN_RUN=3 floor used for the ratio.
  EXPECT_EQ(queuepanel::auto_display_mode(make_run("A", 2), false), QM::Grouped);
}

TEST(AutoDisplayMode, SingleTrackWithRealAlbumIsGrouped)
{
  EXPECT_EQ(queuepanel::auto_display_mode(make_run("A", 1), false), QM::Grouped);
}

TEST(AutoDisplayMode, AlbumPlusAFewSinglesIsGrouped)
{
  auto songs = make_run("A", 10);
  songs += make_run("B", 1) + make_run("C", 1) + make_run("D", 1) + make_run("E", 1);
  // 10/14 ≈ 0.71 >= 0.6
  EXPECT_EQ(queuepanel::auto_display_mode(songs, false), QM::Grouped);
}

TEST(AutoDisplayMode, HalfAndHalfIsFull)
{
  auto songs = make_run("A", 3) + make_run("B", 3);
  for (int i = 0; i < 6; ++i)
    songs += make_run(QString("S%1").arg(i), 1);
  // 6/12 = 0.5 < 0.6
  EXPECT_EQ(queuepanel::auto_display_mode(songs, false), QM::Full);
}

TEST(AutoDisplayMode, ShuffledDumpIsFull)
{
  QList<song> songs;
  for (int i = 0; i < 12; ++i)
    songs += make_run(QString("S%1").arg(i), 1);
  EXPECT_EQ(queuepanel::auto_display_mode(songs, false), QM::Full);
}

TEST(AutoDisplayMode, PairsDoNotCountTowardAlbumness)
{
  QList<song> songs;
  for (int i = 0; i < 6; ++i)
    songs += make_run(QString("P%1").arg(i), 2);
  EXPECT_EQ(queuepanel::auto_display_mode(songs, false), QM::Full);
}

TEST(AutoDisplayMode, ExactThresholdIsGrouped)
{
  auto songs = make_run("A", 3) + make_run("B", 3);
  songs += make_run("C", 1) + make_run("D", 1) + make_run("E", 1) + make_run("F", 1);
  // 6/10 = 0.6, and the comparison is >=.
  EXPECT_EQ(queuepanel::auto_display_mode(songs, false), QM::Grouped);
}

TEST(AutoDisplayMode, ShuffleForcesFull)
{
  EXPECT_EQ(queuepanel::auto_display_mode(make_run("A", 8), true), QM::Full);
}

TEST(AutoDisplayMode, EmptyHashesNeverGroup)
{
  // Untagged singles: each empty-hash track is its own run, never a block.
  EXPECT_EQ(queuepanel::auto_display_mode(make_run("", 8), false), QM::Full);
}

// Grouped-mode row height depends on libman: UserRoleShowArtist compares each
// track's artist against the *album* artist, which only exists once the library
// has loaded. At startup the queue is restored before the library arrives, so
// get_album() returns a default album with an empty artist, every track's artist
// looks "different", and every row is laid out with the taller subtitle height.
//
// When the library lands, QListView re-queries the delegate for each item's *size*
// but keeps the item *positions* it cached at layout time. The rows therefore
// shrink to their correct height while still being spaced at the old, taller
// pitch — leaving a band of dead whitespace under every row. Only a relayout
// fixes the positions; a repaint cannot. Needs the list visible, which is what
// forced the early layout in the first place.
TEST(QueueRowHeights, LibraryArrivingAfterLayoutRelaysOutRows)
{
  QTemporaryDir cache;
  auto artman = std::make_shared<AlbumArtManager>(QDir{cache.path()});
  auto libman = std::make_shared<LibraryManager>();

  // One album's worth of tracks, every track by the album artist — so once the
  // library is known, no per-track artist line is needed.
  QList<song> tracks;
  for (int i = 0; i < 3; ++i)
    tracks.append(song("uri" + QString::number(i), "Title", "A", i + 1, 1, 1000, "hash"));

  QueueListView view;
  QueueListModel model(artman, libman);
  view.setModel(&model);
  view.resize(400, 600);
  model.set_data(tracks); // library still empty, exactly as at restore

  // The view must actually be laid out for it to cache row geometry — which is
  // why the bug only shows when the queue panel was left visible at shutdown.
  // Force the layout to fully settle, so no pending relayout can mask the bug.
  view.show();
  view.refresh_layout();
  qApp->processEvents();

  // Rows 1 and 2 are mid-album (not group starts), so no header band is involved:
  // the gap between them is purely the row pitch.
  const auto row1 = model.index(1);
  const auto row2 = model.index(2);
  const int laid_out_pitch = view.visualRect(row2).y() - view.visualRect(row1).y();
  EXPECT_EQ(view.visualRect(row1).height(), laid_out_pitch); // no gap while consistent

  album a;
  a.album_hash = "hash";
  a.name = "Album";
  a.artist = "A";
  libman->add_album("hash", a);

  // A repaint alone: the row shrinks (size is re-queried from the delegate) but
  // the spacing does not (positions stay cached) — that mismatch IS the whitespace.
  view.viewport()->update();
  qApp->processEvents();
  const int shrunk_h = view.visualRect(row1).height();
  const int stale_pitch = view.visualRect(row2).y() - view.visualRect(row1).y();
  EXPECT_LT(shrunk_h, stale_pitch);
  EXPECT_EQ(stale_pitch, laid_out_pitch);

  // Relayout closes the gap: spacing matches the rows again.
  view.refresh_layout();
  qApp->processEvents();
  EXPECT_EQ(view.visualRect(row2).y() - view.visualRect(row1).y(),
      view.visualRect(row1).height());
}
