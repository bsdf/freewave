#include "playlistitemmodel.hh"

#include <algorithm>
#include <utility>

#include <QDataStream>
#include <QIODevice>
#include <QMimeData>

#include "controller/favoritesmanager.hh"
#include "mime.hh"

PlaylistItemModel::PlaylistItemModel(const LibraryManager *lib, QObject *parent)
  : QAbstractListModel{parent}
  , lib{lib}
{
}

int
PlaylistItemModel::rowCount(const QModelIndex &parent) const
{
  if (parent.isValid())
    return 0;
  return rows.size();
}

auto
PlaylistItemModel::flat_index(int row) const -> int
{
  if (row < 0 || row >= rows.size())
    return -1;
  return rows[row].first;
}

void
PlaylistItemModel::set_data(const QList<song> &songs)
{
  // Skip the reset when nothing that affects rendering changed (uri = identity,
  // album_hash = cover/coalescing key). The favorites list is re-fetched on
  // every library reload / periodic poll to re-resolve album hashes; without
  // this guard each poll would rebuild the model and jump the scroll position.
  // Caveat: identical flat songs whose album *tracks* got cached since the last
  // build won't re-coalesce — unreachable via normal flows (content changes move
  // a uri/hash) and immaterial to favorites, which don't coalesce.
  // as_const: `flat` shares its buffer with the list it was assigned from, so
  // iterating it mutably would detach — a full copy inside the guard that exists
  // to avoid exactly that work.
  if (std::ranges::equal(songs, std::as_const(flat), [](const song &a, const song &b) {
        return a.uri == b.uri && a.album_hash == b.album_hash;
      }))
    return;

  beginResetModel();
  flat = songs;
  rebuild();
  endResetModel();
}

void
PlaylistItemModel::set_current_uri(const QString &uri)
{
  if (current_uri == uri)
    return;
  current_uri = uri;
  // Recompute per-row playing flags in place; the flat list is unchanged.
  for (int i = 0; i < rows.size(); ++i)
    {
      bool playing = false;
      for (int k = 0; k < rows[i].count; ++k)
        if (flat[rows[i].first + k].uri == current_uri)
          {
            playing = true;
            break;
          }
      if (playing != rows[i].playing)
        {
          rows[i].playing = playing;
          emit dataChanged(index(i), index(i), {UserRoleCurrentlyPlaying});
        }
    }
}

void
PlaylistItemModel::set_favorites(const FavoritesManager *f)
{
  favs = f;
  if (!favs)
    return;
  connect(favs, &FavoritesManager::favorite_changed, this,
      [this](const QString &uri, bool) {
        for (int i = 0; i < rows.size(); ++i)
          if (rows[i].kind == Track && rows[i].track.uri == uri)
            emit dataChanged(index(i), index(i), {UserRoleFavorited});
      });
  connect(favs, &FavoritesManager::favorites_reset, this,
      [this] { refresh_favorites(); });
}

void
PlaylistItemModel::refresh_favorites()
{
  if (!rows.isEmpty())
    emit dataChanged(index(0), index(rows.size() - 1), {UserRoleFavorited});
}

void
PlaylistItemModel::rebuild()
{
  rows.clear();

  auto resolve = [this](const QString &hash) -> playlistrows::album_ref {
    if (!lib)
      return {};
    // Cached tracks (MPD, opened Subsonic albums) drive the strict match;
    // song_count (from getAlbumList2) lets un-fetched Subsonic playlist albums
    // still coalesce without a getAlbum per album.
    return {lib->get_songs(hash), lib->get_album(hash).song_count};
  };
  auto coalesced = playlistrows::coalesce_playlist_rows(flat, resolve);

  for (const auto &r : coalesced)
    {
      DisplayRow dr;
      dr.kind = (r.kind == playlistrows::playlist_row::Kind::Album) ? Album : Track;
      dr.first = r.first;
      dr.count = r.count;

      qint64 dur = 0;
      bool playing = false;
      for (int k = 0; k < r.count; ++k)
        {
          const auto &s = flat[r.first + k];
          dur += s.duration; // song::duration is ms
          if (!current_uri.isEmpty() && s.uri == current_uri)
            playing = true;
        }
      dr.duration_ms = dur;
      dr.playing = playing;

      const auto &head = flat[r.first];
      dr.album_hash = head.album_hash;

      if (dr.kind == Album)
        {
          auto a = lib ? lib->get_album(head.album_hash) : album{};
          dr.primary = a.name.isEmpty() ? head.title : a.name;
          auto artist = a.artist.isEmpty() ? head.artist : a.artist;
          dr.secondary = QString("%1  ～  %2 %3")
                             .arg(artist)
                             .arg(r.count)
                             .arg(r.count == 1 ? "track" : "tracks");
        }
      else
        {
          dr.track = head;
          dr.primary = head.title;
          auto a = lib ? lib->get_album(head.album_hash) : album{};
          dr.secondary = a.name.isEmpty()
                             ? head.artist
                             : QString("%1  ～  %2").arg(head.artist, a.name);
        }
      rows.push_back(dr);
    }
}

Qt::ItemFlags
PlaylistItemModel::flags(const QModelIndex &index) const
{
  auto f = QAbstractListModel::flags(index);
  if (index.isValid())
    return f | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
  return f | Qt::ItemIsDropEnabled; // allow dropping past the last row
}

Qt::DropActions
PlaylistItemModel::supportedDropActions() const
{
  return Qt::MoveAction;
}

QStringList
PlaylistItemModel::mimeTypes() const
{
  return {MimeTypes::PlaylistItem};
}

QMimeData *
PlaylistItemModel::mimeData(const QModelIndexList &indexes) const
{
  QByteArray encoded;
  QDataStream stream(&encoded, QIODevice::WriteOnly);
  for (const auto &i : indexes)
    if (i.isValid())
      stream << i.row();

  auto *mime = new QMimeData;
  mime->setData(MimeTypes::PlaylistItem, encoded);
  return mime;
}

bool
PlaylistItemModel::dropMimeData(const QMimeData *data, Qt::DropAction /*action*/,
    int row, int /*column*/, const QModelIndex &parent)
{
  if (!data->hasFormat(MimeTypes::PlaylistItem))
    return false;

  // A drop onto a row (not between rows) reports row == -1 with a valid parent.
  int drop_row = row >= 0 ? row : (parent.isValid() ? parent.row() : rows.size());

  // Target flat index = where the drop-target display row begins, or the end of
  // the flat list when dropping past the last row.
  int target = (drop_row >= 0 && drop_row < rows.size()) ? rows[drop_row].first
                                                         : flat.size();

  QByteArray encoded = data->data(MimeTypes::PlaylistItem);
  QDataStream stream(&encoded, QIODevice::ReadOnly);
  QList<int> moved;
  while (!stream.atEnd())
    {
      int r;
      stream >> r;
      if (r < 0 || r >= rows.size())
        continue;
      for (int k = 0; k < rows[r].count; ++k) // expand a coalesced span
        moved.push_back(rows[r].first + k);
    }

  if (!moved.isEmpty())
    emit rearrange_rows(target, moved);

  // Return false: don't let the view perform its default row removal — the
  // manager's optimistic reorder + refetch rebuilds the model authoritatively.
  return false;
}

QVariant
PlaylistItemModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || index.row() >= rows.size())
    return {};
  const auto &r = rows[index.row()];
  switch (role)
    {
    case UserRoleKind:
      return int(r.kind);
    case UserRoleSong:
      return QVariant::fromValue(r.track);
    case UserRoleAlbumHash:
      return r.album_hash;
    case UserRolePrimary:
      return r.primary;
    case UserRoleSecondary:
      return r.secondary;
    case UserRoleFirstIndex:
      return r.first;
    case UserRoleSpan:
      return r.count;
    case UserRoleDurationMs:
      return QVariant::fromValue<qint64>(r.duration_ms);
    case UserRoleCurrentlyPlaying:
      return r.playing;
    case UserRoleFavorited:
      return r.kind == Track && favs && favs->is_favorite(r.track.uri);
    case UserRoleFavoritesAvailable:
      return favs && favs->available();
    }
  return {};
}
