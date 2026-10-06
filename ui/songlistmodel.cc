#include "songlistmodel.hh"

#include "mime.hh"
#include "controller/favoritesmanager.hh"

#include <algorithm>

SongListModel::SongListModel(QObject *parent)
  : QAbstractListModel(parent)
{
}

auto
SongListModel::rowCount(const QModelIndex &) const -> int
{
  return songs.isEmpty() ? skeleton_count : songs.size();
}

auto
SongListModel::data(const QModelIndex &index, int role) const -> QVariant
{
  if (!index.isValid())
    return {};

  // Skeleton rows: songs is empty but we render placeholders.
  if (songs.isEmpty())
    return role == SongListModel::UserRoleSkeleton ? QVariant(true) : QVariant{};

  auto song = songs[index.row()];

  switch (role)
    {
    case SongListModel::UserRoleSkeleton:
      return false;

    case Qt::DisplayRole:
      return song.title;

    case SongListModel::UserRoleSong:
      return QVariant::fromValue(song);

    case SongListModel::UserRoleCurrentlyPlaying:
      return song.uri == uri;

    case SongListModel::UserRoleAlbumHash:
      return song.album_hash;

    case SongListModel::UserRoleMultiDisc:
      return multi_disc;

    case SongListModel::UserRoleDiscOpener:
      return disc_openers.at(song.disc_number) == song.track_number;

    case SongListModel::UserRoleShowArtist:
      return show_artist;

    case SongListModel::UserRoleFavorited:
      return favs && favs->is_favorite(song.uri);

    case SongListModel::UserRoleFavoritesAvailable:
      return favs && favs->available();

    default:
      return {};
    }
}

auto
SongListModel::set_favorites(const FavoritesManager *favs) -> void
{
  this->favs = favs;
  if (!favs)
    return;

  // One song flipped — repaint just its row.
  connect(favs, &FavoritesManager::favorite_changed, this,
      [this](const QString &uri, bool) {
        for (int row = 0; row < songs.size(); ++row)
          if (songs[row].uri == uri)
            {
              auto idx = index(row);
              emit dataChanged(idx, idx, {UserRoleFavorited});
            }
      });

  // Bulk reload (connect / backend switch) — repaint every heart.
  connect(favs, &FavoritesManager::favorites_reset, this, [this] {
    if (!songs.isEmpty())
      emit dataChanged(index(0), index(songs.size() - 1), {UserRoleFavorited});
  });
}

auto
SongListModel::set_show_artist(bool show) -> void
{
  show_artist = show;
}

auto
SongListModel::set_data(const QList<song> &songs) -> void
{
  beginResetModel();

  skeleton_count = 0;

  // construct map of disc opening tracks
  disc_openers.clear();
  std::ranges::for_each(songs, [&](const song &s) {
    disc_openers.try_emplace(s.disc_number, s.track_number);
  });
  multi_disc = disc_openers.size() > 1;

  this->songs = songs;
  endResetModel();
}

auto
SongListModel::set_skeleton(int count) -> void
{
  beginResetModel();
  songs.clear();
  disc_openers.clear();
  multi_disc = false;
  skeleton_count = std::max(0, count);
  endResetModel();
}

auto
SongListModel::set_current_uri(const QString &current) -> void
{
  if (uri == current) return;

  uri = current;
  dataChanged(index(0), index(songs.size() - 1));
}

auto
SongListModel::mimeData(const QModelIndexList &indexes) const -> QMimeData *
{
  if (indexes.isEmpty()) return nullptr;

  QMimeData *mime_data = new QMimeData;
  QByteArray encoded_data;
  QDataStream stream(&encoded_data, QIODevice::WriteOnly);

  for (const auto &idx : indexes)
    {
      if (!idx.isValid()) continue;

      auto s = songs[idx.row()];
      stream << s;
    }

  mime_data->setData(MimeTypes::Song, encoded_data);
  return mime_data;
}

auto
SongListModel::mimeTypes() const -> QStringList
{
  return QStringList() << MimeTypes::Song;
}

auto
SongListModel::flags(const QModelIndex &index) const -> Qt::ItemFlags
{
  auto default_flags = QAbstractListModel::flags(index);
  if (songs.isEmpty()) // skeleton rows: not selectable or draggable
    return default_flags & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
  if (index.isValid())
    return Qt::ItemIsDragEnabled | default_flags;
  return default_flags;
}
