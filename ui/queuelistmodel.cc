#include "queuelistmodel.hh"

#include "mime.hh"
#include "controller/favoritesmanager.hh"

QueueListModel::QueueListModel(std::shared_ptr<AlbumArtManager> artman, std::shared_ptr<LibraryManager> libman, QObject *parent)
  : QAbstractListModel(parent)
  , default_cover{QPixmap(QSize(50, 50))}
  , artman{artman}
  , libman{libman}
{
  default_cover.fill(Qt::black);
}

auto
QueueListModel::rowCount(const QModelIndex &) const -> int
{
  return songs.size();
}

auto
QueueListModel::data(const QModelIndex &index, int role) const -> QVariant
{
  if (!index.isValid())
    return {};

  auto song = songs[index.row()];

  switch (role)
    {
    case Qt::DecorationRole:
      {
        auto a = artman->get_queue_thumb(song.album_hash);
        if (a.isNull())
          return default_cover;

        return a;
      }

    case Qt::DisplayRole:
      return song.title;

    case Qt::ToolTipRole:
      return song.artist.isEmpty() ? song.title
                                   : QString("%1 ～ %2").arg(song.title, song.artist);

    case QueueListModel::UserRoleSong:
      return QVariant::fromValue(song);

    case QueueListModel::UserRoleCurrentlyPlaying:
      return index.row() == static_cast<int>(idx);

    case QueueListModel::UserRoleAlbumHash:
      return song.album_hash;

    case QueueListModel::UserRoleAlbumName:
      return libman ? libman->get_album(song.album_hash).name : QString();

    case QueueListModel::UserRoleAlbumArtist:
      return libman ? libman->get_album(song.album_hash).artist : QString();

    case QueueListModel::UserRoleAccent:
      return artman ? QVariant::fromValue(artman->get_accent(song.album_hash)) : QVariant();

    case QueueListModel::UserRoleGroupStart:
      return index.row() == 0 || songs[index.row() - 1].album_hash != song.album_hash;

    case QueueListModel::UserRoleLocalNumber:
      {
        int local_idx = 0;
        for (int i = index.row() - 1; i >= 0; --i)
          {
            if (songs[i].album_hash == song.album_hash)
              ++local_idx;
            else
              break;
          }
        return local_idx;
      }

    case QueueListModel::UserRolePlayingSameAlbum:
      return idx < (uint32_t)songs.size() && songs[idx].album_hash == song.album_hash;

    case QueueListModel::UserRoleFavorited:
      return favs && favs->is_favorite(song.uri);

    case QueueListModel::UserRoleFavoritesAvailable:
      return favs && favs->available();

    case QueueListModel::UserRoleShowArtist:
      {
        // Various-artists album: any track in this contiguous group whose
        // artist differs from the album artist means the whole group shows
        // per-track artist, mirroring AlbumView's is_va check.
        auto album_artist = libman ? libman->get_album(song.album_hash).artist : QString();
        int start = index.row();
        while (start > 0 && songs[start - 1].album_hash == song.album_hash)
          --start;
        int end = index.row();
        while (end + 1 < songs.size() && songs[end + 1].album_hash == song.album_hash)
          ++end;
        for (int i = start; i <= end; ++i)
          if (!songs[i].artist.isEmpty() && songs[i].artist != album_artist)
            return true;
        return false;
      }

    default:
      return {};
    }
}

auto
QueueListModel::set_favorites(const FavoritesManager *favs) -> void
{
  this->favs = favs;
  if (!favs)
    return;

  // One song flipped — repaint just its row(s) (a uri can appear more than once).
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
QueueListModel::set_data(const QList<song> &songs) -> void
{
  beginResetModel();
  this->songs = songs;
  endResetModel();
}

auto
QueueListModel::set_current_index(uint32_t current) -> void
{
  if (idx == current) return;

  dataChanged(index(idx), index(current));
  idx = current;
}

auto
QueueListModel::current_index() const -> uint32_t
{
  return idx;
}

auto
QueueListModel::mimeData(const QModelIndexList &indexes) const -> QMimeData *
{
  if (indexes.isEmpty()) return nullptr;

  QMimeData *mime_data = new QMimeData;
  QByteArray encoded_data;
  QDataStream stream(&encoded_data, QIODevice::WriteOnly);

  for (const auto &i : indexes)
    {
      if (!i.isValid()) continue;
      stream << i.row();
    }

  mime_data->setData(MimeTypes::QueueListItem, encoded_data);
  return mime_data;
}

auto
QueueListModel::canDropMimeData(const QMimeData *data, Qt::DropAction,
    int, int column, const QModelIndex &) const -> bool
{
  if (column > 0)
    return false;

  if (data->hasFormat(MimeTypes::Song))
    return true;

  if (data->hasFormat(MimeTypes::Album))
    return true;

  if (data->hasFormat(MimeTypes::QueueListItem))
    return true;

  return false;
}

auto
QueueListModel::dropMimeData(const QMimeData *data, Qt::DropAction,
    int row, int column, const QModelIndex &parent) -> bool
{
  spdlog::trace("dropMimeData row = {} col = {}, parent row = {} col = {}", row, column, parent.row(), parent.column());

  if (data->hasFormat(MimeTypes::Song))
    {
      auto encoded = data->data(MimeTypes::Song);
      auto stream = QDataStream(&encoded, QIODevice::ReadOnly);

      QList<song> dropped_songs;
      while (!stream.atEnd())
        {
          song s;
          stream >> s;
          dropped_songs.append(s);
        }

      spdlog::trace("dropped on row = {} songs = {}", row, dropped_songs.size());
      emit insert_queue_at(row == -1 ? songs.size() : row, dropped_songs);
    }
  else if (data->hasFormat(MimeTypes::Album))
    {
      auto encoded = data->data(MimeTypes::Album);
      auto stream = QDataStream(&encoded, QIODevice::ReadOnly);

      QList<album> dropped_albums;
      while (!stream.atEnd())
        {
          album a;
          stream >> a;
          dropped_albums.append(a);
        }

      spdlog::trace("dropped albums on row = {} count = {}", row, dropped_albums.size());
      emit insert_albums_at(row == -1 ? songs.size() : row, dropped_albums);
    }
  else if (data->hasFormat(MimeTypes::QueueListItem))
    {
      auto encoded = data->data(MimeTypes::QueueListItem);
      auto stream = QDataStream(&encoded, QIODevice::ReadOnly);

      std::vector<uint32_t> moved_indexes;

      while (!stream.atEnd())
        {
          uint32_t i;
          stream >> i;
          moved_indexes.emplace_back(i);
        }

      emit rearrange_queue(row, moved_indexes);
    }
  else
    {
      spdlog::warn("somehow dropped invalid mime type");
    }

  return false;
}

auto
QueueListModel::mimeTypes() const -> QStringList
{
  return QStringList() << MimeTypes::QueueListItem << MimeTypes::Song << MimeTypes::Album;
}

auto
QueueListModel::supportedDropActions() const -> Qt::DropActions
{
  return Qt::CopyAction | Qt::MoveAction;
}

auto
QueueListModel::flags(const QModelIndex &index) const -> Qt::ItemFlags
{
  auto default_flags = QAbstractListModel::flags(index);
  if (index.isValid())
    return Qt::ItemIsDragEnabled | default_flags;
  return Qt::ItemIsDropEnabled | default_flags;
}
