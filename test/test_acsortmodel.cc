#include <gtest/gtest.h>

#include <QDateTime>
#include <QStandardItemModel>
#include <QStringList>
#include <QTimeZone>

#include "ui/aclistmodel.hh"
#include "ui/acsortmodel.hh"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void
add_album(QStandardItemModel &model, const QString &name,
    const QDateTime &last_modified)
{
  auto *item = new QStandardItem(name);
  item->setData(last_modified, AlbumCoverListModel::UserRoleLastModified);
  item->setData(name, AlbumCoverListModel::UserRoleSortKey);
  item->setData(name, AlbumCoverListModel::UserRoleSearchKey);
  model.appendRow(item);
}

static QStringList
visible_names(const AlbumCoverSortModel &proxy)
{
  QStringList out;
  for (int i = 0; i < proxy.rowCount(); i++)
    out << proxy.index(i, 0).data(Qt::DisplayRole).toString();
  return out;
}

static QDateTime
at(int year)
{
  return QDateTime(QDate(year, 1, 1), QTime(0, 0), QTimeZone::utc());
}

// ---------------------------------------------------------------------------
// Date filter
// ---------------------------------------------------------------------------

TEST(AlbumCoverSortModelDateFilter, DropsAlbumsOlderThanCutoff)
{
  QStandardItemModel src;
  add_album(src, "new", at(2025));
  add_album(src, "old", at(2010));

  AlbumCoverSortModel proxy;
  proxy.setSourceModel(&src);
  proxy.set_date_filter(at(2020));

  auto names = visible_names(proxy);
  EXPECT_TRUE(names.contains("new"));
  EXPECT_FALSE(names.contains("old"));
}

// Regression (premerge review M2): an album with no last-modified date yields
// an invalid QDateTime, which compares less-than every valid date — it used to
// be silently dropped from every date-filtered view.
TEST(AlbumCoverSortModelDateFilter, UndatedAlbumNotDropped)
{
  QStandardItemModel src;
  add_album(src, "new", at(2025));
  add_album(src, "undated", QDateTime{});
  add_album(src, "old", at(2010));

  AlbumCoverSortModel proxy;
  proxy.setSourceModel(&src);
  proxy.set_date_filter(at(2020));

  auto names = visible_names(proxy);
  EXPECT_TRUE(names.contains("undated"));
  EXPECT_TRUE(names.contains("new"));
  EXPECT_FALSE(names.contains("old"));
}

TEST(AlbumCoverSortModelDateFilter, ClearRestoresAllRows)
{
  QStandardItemModel src;
  add_album(src, "new", at(2025));
  add_album(src, "undated", QDateTime{});
  add_album(src, "old", at(2010));

  AlbumCoverSortModel proxy;
  proxy.setSourceModel(&src);
  proxy.set_date_filter(at(2020));
  proxy.clear_date_filter();

  EXPECT_EQ(proxy.rowCount(), 3);
}

// ---------------------------------------------------------------------------
// Most-recent filter — pins the existing treatment of undated albums: they
// sort to the bottom of the recency ranking, so they only appear when the
// requested count reaches past every dated album.
// ---------------------------------------------------------------------------

TEST(AlbumCoverSortModelMostRecent, UndatedRanksLast)
{
  QStandardItemModel src;
  add_album(src, "newest", at(2025));
  add_album(src, "older", at(2015));
  add_album(src, "undated", QDateTime{});

  AlbumCoverSortModel proxy;
  proxy.setSourceModel(&src);

  proxy.set_most_recent(2);
  auto top2 = visible_names(proxy);
  EXPECT_TRUE(top2.contains("newest"));
  EXPECT_TRUE(top2.contains("older"));
  EXPECT_FALSE(top2.contains("undated"));

  proxy.set_most_recent(3);
  EXPECT_TRUE(visible_names(proxy).contains("undated"));
}
