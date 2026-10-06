#include "playlistsidebarmodel.hh"

#include "timeutil.hh"

PlaylistSidebarModel::PlaylistSidebarModel(QObject *parent)
  : QAbstractListModel{parent}
{
}

int
PlaylistSidebarModel::rowCount(const QModelIndex &parent) const
{
  if (parent.isValid())
    return 0;
  return 1 + lists.size(); // row 0 = pinned Favorited Tracks
}

void
PlaylistSidebarModel::set_playlists(const QList<playlist_info> &incoming)
{
  beginResetModel();
  lists = incoming;
  endResetModel();
}

void
PlaylistSidebarModel::set_favorites_count(int n)
{
  if (fav_count == n)
    return;
  fav_count = n;
  emit dataChanged(index(0), index(0), {UserRoleMeta});
}

void
PlaylistSidebarModel::set_mosaic_resolver(std::function<QStringList(const QString &id)> resolver)
{
  mosaic = std::move(resolver);
}

auto
PlaylistSidebarModel::playlist_id(int row) const -> QString
{
  int i = row - 1;
  if (i < 0 || i >= lists.size())
    return {};
  return lists[i].id;
}

auto
PlaylistSidebarModel::row_for_id(const QString &id) const -> int
{
  for (int i = 0; i < lists.size(); ++i)
    if (lists[i].id == id)
      return i + 1;
  return -1;
}

QVariant
PlaylistSidebarModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid())
    return {};
  int row = index.row();

  if (row == 0) // auto Favorited Tracks row
    {
      switch (role)
        {
        case UserRoleIsAuto:
          return true;
        case UserRolePlaylistId:
          return QString{};
        case UserRoleName:
          return QStringLiteral("Favorited Tracks");
        case UserRoleMeta:
          return QString("%1 %2  ～  AUTO")
              .arg(fav_count)
              .arg(fav_count == 1 ? "track" : "tracks");
        case UserRoleMosaicHashes:
          return QStringList{};
        case UserRoleMosaicStamp:
          return QString{};
        }
      return {};
    }

  int i = row - 1;
  if (i < 0 || i >= lists.size())
    return {};
  const auto &pl = lists[i];

  switch (role)
    {
    case UserRoleIsAuto:
      return false;
    case UserRolePlaylistId:
      return pl.id;
    case UserRoleName:
      return pl.name;
    case UserRoleMeta:
      {
        QString items = pl.song_count < 0
                            ? QStringLiteral("— items")
                            : QString("%1 %2").arg(pl.song_count).arg(pl.song_count == 1 ? "item" : "items");
        auto rel = timeutil::relative_label(pl.last_modified);
        return rel.isEmpty() ? items : QString("%1  ～  updated %2").arg(items, rel);
      }
    case UserRoleMosaicHashes:
      return mosaic ? mosaic(pl.id) : QStringList{};
    case UserRoleMosaicStamp:
      return pl.cache_stamp();
    }
  return {};
}
