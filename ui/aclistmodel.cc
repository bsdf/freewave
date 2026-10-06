#include "aclistmodel.hh"

#include "mime.hh"

#include <QDir>
#include <QIcon>
#include <QPixmap>
#include <QPixmapCache>
#include <QCryptographicHash>

AlbumCoverListModel::AlbumCoverListModel(
    std::shared_ptr<AlbumArtManager> artman, QObject *parent)
  : QAbstractListModel(parent)
  , default_cover{QPixmap(QSize(200, 200))}
  , artman{artman}
{
  default_cover.fill(Qt::black);
}

auto
AlbumCoverListModel::rowCount(const QModelIndex &) const -> int
{
  return albums.size();
}

auto
AlbumCoverListModel::data(const QModelIndex &index, int role) const -> QVariant
{
  if (!index.isValid())
    return {};

  auto album = albums[index.row()];

  switch (role)
    {
    case Qt::DecorationRole:
      {
        auto a = artman->get_art(album.album_hash, cover_size);
        if (a.isNull())
          return default_cover;

        return a;
      }

    case Qt::DisplayRole:
      return album.sort_artist;

    case AlbumCoverListModel::UserRoleAlbum:
      return QVariant::fromValue(album);

    case AlbumCoverListModel::UserRoleLastModified:
      return album.last_modified;

    case AlbumCoverListModel::UserRoleSortKey:
      return QString("%1/%2/%3").arg(album.sort_artist, album.date, album.name);

    case AlbumCoverListModel::UserRoleSearchKey:
      return QString("%1\n%2\n%3").arg(album.artist, album.sort_artist, album.name);

    default:
      return {};
    }
}

auto
AlbumCoverListModel::set_data(const QList<album> &albums) -> void
{
  beginResetModel();
  this->albums = albums;
  endResetModel();
}

auto
AlbumCoverListModel::flags(const QModelIndex &index) const -> Qt::ItemFlags
{
  auto default_flags = QAbstractListModel::flags(index);
  return Qt::ItemIsDragEnabled | default_flags;
}

auto
AlbumCoverListModel::mimeTypes() const -> QStringList
{
  return QStringList() << MimeTypes::Album;
}

auto
AlbumCoverListModel::set_cover_size(QSize size) -> void
{
  cover_size = size;
  default_cover = QPixmap(size);
  default_cover.fill(Qt::black);
}

auto
AlbumCoverListModel::mimeData(const QModelIndexList &indexes) const -> QMimeData *
{
  if (indexes.isEmpty()) return nullptr;

  QMimeData *mime_data = new QMimeData;
  QByteArray encoded_data;
  QDataStream stream(&encoded_data, QIODevice::WriteOnly);

  for (const auto &idx : indexes)
    {
      if (!idx.isValid()) continue;
      auto a = data(idx, AlbumCoverListModel::UserRoleAlbum).value<album>();
      spdlog::trace("dragged album = {}", a.name);
      stream << a;
    }

  mime_data->setData(MimeTypes::Album, encoded_data);
  return mime_data;
}
