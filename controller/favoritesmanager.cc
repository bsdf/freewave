#include "favoritesmanager.hh"

FavoritesManager::FavoritesManager(std::shared_ptr<Backend> backend, QObject *parent)
  : QObject{parent}
  , backend{std::move(backend)}
{
  if (!this->backend)
    return;

  // Full set on connect / reload — replace and repaint everything.
  connect(this->backend.get(), &Backend::favorites_loaded, this,
      [this](const QSet<QString> &uris) {
        pending = false; // authoritative set arrived — nothing left to reconcile
        favs = uris;
        emit favorites_reset();
      });

  // The same fetch delivers the songs behind the auto list.
  connect(this->backend.get(), &Backend::favorite_songs_loaded, this,
      [this](const QList<song> &songs) {
        fav_songs = songs;
        fav_songs_loaded = true;
        emit songs_changed();
      });

  // Single-item confirmation (e.g. Subsonic star reply, or an external change
  // surfaced by a backend). Only emit when it actually changes our state — an
  // optimistic toggle already updated favs, so its own confirmation is a no-op.
  connect(this->backend.get(), &Backend::favorite_changed, this,
      [this](const QString &uri, bool fav) {
        pending = false; // the toggle was confirmed by the backend
        if (favs.contains(uri) != fav)
          {
            if (fav)
              favs.insert(uri);
            else
              favs.remove(uri);
            emit favorite_changed(uri, fav);
          }
      });

  // Favorited songs are standalone: their album_hash is resolved through the
  // library index, which finishes loading after the first favorites read on a
  // paginated library. When that index moves, correct the songs we already hold
  // — the covers and album names appear without another read.
  connect(this->backend.get(), &Backend::album_mapping_changed, this, [this] {
    if (reresolve_album_hashes(*this->backend, fav_songs))
      emit songs_changed();
  });

  // A backend error after an unconfirmed toggle means the optimistic change may
  // not have persisted — re-fetch the authoritative set so the UI snaps back to
  // truth (reverting a failed star/unstar or sticker write).
  connect(this->backend.get(), &Backend::error, this, [this] {
    if (!pending)
      return;
    pending = false;
    request_refresh();
  });

  // Connect is the initial read. Both backends report it after their own state
  // is up, so the fetch this triggers is never issued against a dead connection.
  connect(this->backend.get(), &Backend::connection_update, this,
      [this](bool up) {
        if (up)
          request_refresh();
      });

  // The server may have diverged from what we hold: a Subsonic poll tick, or an
  // MPD sticker idle event. Either way the decision to read is made here.
  connect(this->backend.get(), &Backend::favorites_stale, this,
      &FavoritesManager::request_refresh);

  // The server proved it can't do favorites at all. Whatever we hold can no
  // longer be trusted or written back, and every heart in the UI has to go —
  // which favorites_reset already means to its listeners.
  connect(this->backend.get(), &Backend::capabilities_changed, this, [this] {
    if (available())
      return;
    favs.clear();
    fav_songs.clear();
    fav_songs_loaded = false;
    emit favorites_reset();
    emit songs_changed();
  });
}

void
FavoritesManager::request_refresh()
{
  if (available())
    backend->fetch_favorites();
}

auto
FavoritesManager::is_favorite(const QString &uri) const -> bool
{
  return favs.contains(uri);
}

auto
FavoritesManager::all() const -> QSet<QString>
{
  return favs;
}

auto
FavoritesManager::songs() const -> QList<song>
{
  return fav_songs;
}

auto
FavoritesManager::songs_loaded() const -> bool
{
  return fav_songs_loaded;
}

auto
FavoritesManager::available() const -> bool
{
  return backend && backend->supports(Backend::Feature::Favorites);
}

void
FavoritesManager::toggle(const QString &uri)
{
  set_favorite(uri, !is_favorite(uri));
}

void
FavoritesManager::set_favorite(const QString &uri, bool fav)
{
  if (is_favorite(uri) == fav)
    return;

  // Optimistic: update + repaint immediately, then persist. The backend's own
  // reconciliation (Subsonic favorite_changed reply / MPD MPD_IDLE_STICKER
  // refetch) confirms or corrects this asynchronously.
  if (fav)
    favs.insert(uri);
  else
    favs.remove(uri);
  emit favorite_changed(uri, fav);

  if (backend)
    {
      pending = true; // awaiting confirmation; an error before then triggers a re-fetch
      backend->set_favorite(uri, fav);
    }
}
