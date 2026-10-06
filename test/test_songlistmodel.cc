#include <gtest/gtest.h>

#include <QSignalSpy>

#include "ui/songlistmodel.hh"
#include "controller/favoritesmanager.hh"
#include "model/song.hh"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static song
make_song(const QString &uri, uint32_t track = 1, uint32_t disc = 1)
{
  song s;
  s.uri = uri;
  s.title = "Title " + uri;
  s.artist = "Artist";
  s.track_number = track;
  s.disc_number = disc;
  s.duration = 180000;
  s.album_hash = "hash";
  return s;
}

static QList<song>
make_songs(int n)
{
  QList<song> out;
  for (int i = 0; i < n; ++i)
    out.append(make_song(QString::number(i), i + 1));
  return out;
}

// ---------------------------------------------------------------------------
// Default / real-data state
// ---------------------------------------------------------------------------

TEST(SongListModelSkeleton, EmptyByDefault)
{
  SongListModel model;
  EXPECT_EQ(model.rowCount(), 0);
}

TEST(SongListModelSkeleton, RealData_NotMarkedSkeleton)
{
  SongListModel model;
  model.set_data(make_songs(3));

  ASSERT_EQ(model.rowCount(), 3);
  for (int i = 0; i < 3; ++i)
    {
      auto idx = model.index(i, 0);
      EXPECT_FALSE(idx.data(SongListModel::UserRoleSkeleton).toBool());
      EXPECT_TRUE(idx.data(SongListModel::UserRoleSong).value<song>().uri
                  == QString::number(i));
    }
}

// ---------------------------------------------------------------------------
// Skeleton rows
// ---------------------------------------------------------------------------

TEST(SongListModelSkeleton, SetSkeleton_ShowsPlaceholderRows)
{
  SongListModel model;
  model.set_skeleton(5);

  ASSERT_EQ(model.rowCount(), 5);
  for (int i = 0; i < 5; ++i)
    {
      auto idx = model.index(i, 0);
      EXPECT_TRUE(idx.data(SongListModel::UserRoleSkeleton).toBool());
      // Skeleton rows carry no real song payload.
      EXPECT_FALSE(idx.data(SongListModel::UserRoleSong).isValid());
    }
}

TEST(SongListModelSkeleton, SetSkeleton_NegativeClampedToZero)
{
  SongListModel model;
  model.set_skeleton(-3);
  EXPECT_EQ(model.rowCount(), 0);
}

TEST(SongListModelSkeleton, SetSkeleton_ZeroIsEmpty)
{
  SongListModel model;
  model.set_skeleton(0);
  EXPECT_EQ(model.rowCount(), 0);
}

// ---------------------------------------------------------------------------
// Transitions — the skeleton must give way to real data and vice-versa
// ---------------------------------------------------------------------------

TEST(SongListModelSkeleton, SetData_ClearsSkeleton)
{
  SongListModel model;
  model.set_skeleton(8);
  ASSERT_EQ(model.rowCount(), 8);

  model.set_data(make_songs(3));
  ASSERT_EQ(model.rowCount(), 3); // real count, not the skeleton count
  EXPECT_FALSE(model.index(0, 0).data(SongListModel::UserRoleSkeleton).toBool());
}

TEST(SongListModelSkeleton, SetEmptyData_ClearsSkeleton)
{
  SongListModel model;
  model.set_skeleton(8);
  model.set_data({}); // e.g. a real fetch that returned no songs
  EXPECT_EQ(model.rowCount(), 0);
}

TEST(SongListModelSkeleton, SetSkeleton_ReplacesExistingSongs)
{
  SongListModel model;
  model.set_data(make_songs(4));
  ASSERT_EQ(model.rowCount(), 4);

  model.set_skeleton(2);
  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_TRUE(model.index(0, 0).data(SongListModel::UserRoleSkeleton).toBool());
}

// ---------------------------------------------------------------------------
// Interaction flags — skeleton rows must not be selectable or draggable
// ---------------------------------------------------------------------------

TEST(SongListModelSkeleton, SkeletonRows_NotSelectableOrEnabled)
{
  SongListModel model;
  model.set_skeleton(3);

  auto flags = model.flags(model.index(0, 0));
  EXPECT_FALSE(flags & Qt::ItemIsSelectable);
  EXPECT_FALSE(flags & Qt::ItemIsEnabled);
  EXPECT_FALSE(flags & Qt::ItemIsDragEnabled);
}

TEST(SongListModelSkeleton, RealRows_AreDraggable)
{
  SongListModel model;
  model.set_data(make_songs(2));

  auto flags = model.flags(model.index(0, 0));
  EXPECT_TRUE(flags & Qt::ItemIsDragEnabled);
  EXPECT_TRUE(flags & Qt::ItemIsSelectable);
}

// ---------------------------------------------------------------------------
// Model reset notifications — views must repaint on the skeleton↔data swap
// ---------------------------------------------------------------------------

TEST(SongListModelSkeleton, SetSkeleton_EmitsModelReset)
{
  SongListModel model;
  QSignalSpy spy(&model, &QAbstractItemModel::modelReset);
  model.set_skeleton(4);
  EXPECT_EQ(spy.count(), 1);
}

// ---------------------------------------------------------------------------
// Favorited role — backed by a FavoritesManager (nullptr backend still tracks
// optimistically, which is all the model needs to observe).
// ---------------------------------------------------------------------------

TEST(SongListModelFavorites, FavoritedRole_FalseWithoutManager)
{
  SongListModel model;
  model.set_data(make_songs(2));
  EXPECT_FALSE(model.index(0, 0).data(SongListModel::UserRoleFavorited).toBool());
}

TEST(SongListModelFavorites, FavoritedRole_ReflectsManagerState)
{
  FavoritesManager favs{nullptr};
  SongListModel model;
  model.set_favorites(&favs);
  model.set_data(make_songs(3));

  favs.set_favorite("1", true);

  EXPECT_FALSE(model.index(0, 0).data(SongListModel::UserRoleFavorited).toBool());
  EXPECT_TRUE(model.index(1, 0).data(SongListModel::UserRoleFavorited).toBool());
  EXPECT_FALSE(model.index(2, 0).data(SongListModel::UserRoleFavorited).toBool());
}

TEST(SongListModelFavorites, FavoriteChanged_EmitsTargetedDataChanged)
{
  FavoritesManager favs{nullptr};
  SongListModel model;
  model.set_favorites(&favs);
  model.set_data(make_songs(3));

  QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
  favs.set_favorite("2", true);

  ASSERT_EQ(spy.count(), 1);
  auto top = spy.first().at(0).value<QModelIndex>();
  auto bottom = spy.first().at(1).value<QModelIndex>();
  EXPECT_EQ(top.row(), 2);
  EXPECT_EQ(bottom.row(), 2);
  auto roles = spy.first().at(2).value<QList<int>>();
  EXPECT_TRUE(roles.contains(SongListModel::UserRoleFavorited));
}
