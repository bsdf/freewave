#include "playlistsmanager.hh"

#include <algorithm>
#include <ranges>
#include <vector>

#include "reorder.hh"

namespace {

// First `max` distinct non-empty album hashes, in playlist order.
auto
first_album_hashes(const QList<song> &songs, int max) -> QStringList
{
  QStringList out;
  for (const auto &s : songs)
    {
      if (s.album_hash.isEmpty() || out.contains(s.album_hash))
        continue;
      out << s.album_hash;
      if (out.size() == max)
        break;
    }
  return out;
}

} // namespace

PlaylistsManager::PlaylistsManager(std::shared_ptr<Backend> backend, QObject *parent)
  : QObject{parent}
  , backend{std::move(backend)} // qualify: the ctor param backend shadows the member here
{
  if (!this->backend)
    return;

  connect(this->backend.get(), &Backend::playlists_loaded, this,
      [this](const QList<playlist_info> &incoming) {
        pending = false;
        lists = incoming;
        emit playlists_reset();
      });

  connect(this->backend.get(), &Backend::playlist_songs_loaded, this,
      [this](const QString &id, const QList<song> &incoming) {
        song_cache[id] = incoming;
        update_summary(id);
        emit playlist_updated(id);
      });

  // A mutation completed server-side (ours or, for MPD, another client's via
  // idle events): the playlist list is always re-fetched; cached contents are
  // dropped only for the playlist that changed. An unnamed change (a create, or
  // an MPD idle event) can have touched anything, so it drops everything.
  connect(this->backend.get(), &Backend::playlists_changed, this,
      [this](const QString &changed_id) {
        pending = false;
        if (changed_id.isEmpty())
          song_cache.clear();
        else
          song_cache.remove(changed_id);
        this->backend->fetch_playlists();
      });

  connect(this->backend.get(), &Backend::connection_update, this,
      [this](bool connected) {
        if (connected)
          {
            reload();
            return;
          }
        // Playlists are per-server state and must not leak across a profile
        // switch or a drop.
        lists.clear();
        song_cache.clear();
        emit playlists_reset();
      });

  // A playlist entry is a standalone song: its album_hash is resolved through
  // the library index, which finishes loading (getAlbumList2 paginates) after
  // the first playlist prefetch on a big library, so early contents carry
  // fallback hashes and the wrong covers. Correct them where they sit when the
  // index moves — the cached contents stay valid, so nothing is re-fetched.
  connect(this->backend.get(), &Backend::album_mapping_changed, this, [this] {
    bool any_moved = false;
    for (auto it = song_cache.begin(); it != song_cache.end(); ++it)
      if (reresolve_album_hashes(*this->backend, it.value()))
        {
          any_moved = true;
          update_summary(it.key());
          emit playlist_updated(it.key());
        }
    if (any_moved)
      emit playlists_reset(); // sidebar mosaics are built from these hashes
  });

  connect(this->backend.get(), &Backend::error, this, [this] {
    if (!pending)
      return;
    pending = false;
    song_cache.clear();
    this->backend->fetch_playlists();
  });

  reload();
}

auto
PlaylistsManager::available() const -> bool
{
  return backend && backend->supports(Backend::Feature::Playlists);
}

auto
PlaylistsManager::playlists() const -> QList<playlist_info>
{
  return lists;
}

auto
PlaylistsManager::info(const QString &id) const -> std::optional<playlist_info>
{
  for (const auto &pl : lists)
    if (pl.id == id)
      return pl;
  return std::nullopt;
}

auto
PlaylistsManager::songs(const QString &id) const -> std::optional<QList<song>>
{
  auto it = song_cache.constFind(id);
  if (it == song_cache.constEnd())
    return std::nullopt;
  return it.value();
}

void
PlaylistsManager::ensure_songs(const QString &id)
{
  if (song_cache.contains(id))
    return;
  if (backend)
    backend->fetch_playlist_songs(id);
}

auto
PlaylistsManager::mosaic_hashes(const QString &id) const -> QStringList
{
  auto it = song_cache.constFind(id);
  return it != song_cache.constEnd() ? first_album_hashes(it.value(), 4)
                                     : QStringList{};
}

void
PlaylistsManager::reload()
{
  song_cache.clear();
  if (backend)
    backend->fetch_playlists();
}

void
PlaylistsManager::create(const QString &name, const QList<song> &initial)
{
  if (!backend)
    return;
  pending = true;
  backend->create_playlist(name, initial);
}

void
PlaylistsManager::rename(const QString &id, const QString &new_name)
{
  if (!backend)
    return;
  pending = true;
  backend->rename_playlist(id, new_name);
}

void
PlaylistsManager::remove(const QString &id)
{
  if (!backend)
    return;
  pending = true;
  backend->delete_playlist(id);
}

void
PlaylistsManager::add_songs(const QString &id, const QList<song> &songs)
{
  auto it = song_cache.find(id);
  if (it != song_cache.end())
    {
      it.value() += songs;
      update_summary(id);
      emit playlist_updated(id);
    }
  if (!backend)
    return;
  pending = true;
  backend->add_to_playlist(id, songs);
}

void
PlaylistsManager::remove_at(const QString &id, QList<int> positions)
{
  auto it = song_cache.find(id);
  if (it != song_cache.end())
    {
      // Descending so earlier removals don't shift later indexes.
      std::sort(positions.begin(), positions.end(), [](int a, int b) { return a > b; });
      for (int pos : positions)
        if (pos >= 0 && pos < it.value().size())
          it.value().removeAt(pos);
      update_summary(id);
      emit playlist_updated(id);
    }
  if (!backend)
    return;
  pending = true;
  backend->remove_from_playlist(id, positions);
}

void
PlaylistsManager::rearrange(const QString &id, int target, QList<int> moved)
{
  auto it = song_cache.find(id);
  if (it != song_cache.end())
    {
      const int n = it.value().size();
      const uint32_t hi = std::max<uint32_t>(n - 1, static_cast<uint32_t>(target));
      auto input = std::views::iota(uint32_t{0}, hi + 1) | std::ranges::to<std::vector>();
      std::vector<uint32_t> indexes;
      for (int m : moved)
        if (m >= 0 && m < n)
          indexes.push_back(static_cast<uint32_t>(m));

      auto order = reorder::get_output_order(input, static_cast<uint32_t>(target), indexes);
      QList<song> reordered;
      for (uint32_t idx : order)
        if (idx < static_cast<uint32_t>(n)) // skip the virtual end anchor
          reordered.append(it.value()[static_cast<int>(idx)]);
      it.value() = reordered;
      emit playlist_updated(id);
    }
  if (!backend)
    return;
  pending = true;
  backend->rearrange_playlist(id, target, moved);
}

void
PlaylistsManager::update_summary(const QString &id)
{
  auto it = song_cache.constFind(id);
  if (it == song_cache.constEnd())
    return;
  qint64 total = 0;
  for (const auto &s : it.value())
    total += s.duration;
  for (auto &pl : lists)
    {
      if (pl.id == id)
        {
          pl.song_count = it.value().size();
          pl.duration_ms = total;
          break;
        }
    }
}
