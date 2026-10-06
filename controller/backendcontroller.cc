#include "backendcontroller.hh"

#include "controller/backendfactory.hh"
#include "controller/profilestore.hh"
#include "controller/settings.hh"

#include <QPixmapCache>
#include <spdlog/spdlog.h>

BackendController::BackendController(std::shared_ptr<LibraryManager> libman)
  : libman{std::move(libman)}
{
  ProfileStore ps;
  auto profile = ps.active_profile();
  if (profile)
    {
      auto b = BackendFactory::create(*profile, this->libman);
      if (b)
        {
          active_id = profile->id;
          attach(std::move(b));
          return;
        }
      spdlog::warn("BackendController: active profile type '{}' not available; trying other profiles",
          profile->type.toStdString());
    }

  // Active profile unavailable (type not compiled in, or no active set).
  // Fall back to the first profile whose type is supported in this build.
  for (const BackendProfile &p : ps.load())
    {
      if (profile && p.id == profile->id)
        continue;
      auto b = BackendFactory::create(p, this->libman);
      if (b)
        {
          spdlog::info("BackendController: falling back to profile '{}' (type '{}')",
              p.name.toStdString(), p.type.toStdString());
          ps.set_active_id(p.id);
          active_id = p.id;
          attach(std::move(b));
          return;
        }
    }

  spdlog::error("BackendController: no usable backend profile found in this build");
}

BackendController::BackendController(std::shared_ptr<Backend> initial,
    std::shared_ptr<LibraryManager> libman)
  : libman{std::move(libman)}
{
  attach(std::move(initial));
}

void
BackendController::attach(std::shared_ptr<Backend> b)
{
  active = std::move(b);
  if (!active)
    return;
  // Track the real connection state before forwarding, so a slot reacting to
  // connection_update already sees the fresh value from is_connected().
  QObject::connect(active.get(), &Backend::connection_update,
      this, [this](bool up) { connected = up; });
  QObject::connect(active.get(), &Backend::connection_update,
      this, &Backend::connection_update);
  QObject::connect(active.get(), &Backend::connection_failed,
      this, &Backend::connection_failed);
  QObject::connect(active.get(), &Backend::certificate_untrusted,
      this, &Backend::certificate_untrusted);
  QObject::connect(active.get(), &Backend::playback_state_changed,
      this, &Backend::playback_state_changed);
  QObject::connect(active.get(), &Backend::current_song_changed,
      this, &Backend::current_song_changed);
  QObject::connect(active.get(), &Backend::queue_changed,
      this, &Backend::queue_changed);
  QObject::connect(active.get(), &Backend::library_changed,
      this, &Backend::library_changed);
  QObject::connect(active.get(), &Backend::album_mapping_changed,
      this, &Backend::album_mapping_changed);
  QObject::connect(active.get(), &Backend::library_refresh_active,
      this, &Backend::library_refresh_active);
  QObject::connect(active.get(), &Backend::library_load_failed,
      this, &Backend::library_load_failed);
  QObject::connect(active.get(), &Backend::songs_changed,
      this, &Backend::songs_changed);
  QObject::connect(active.get(), &Backend::album_art_received,
      this, &Backend::album_art_received);
  QObject::connect(active.get(), &Backend::error,
      this, &Backend::error);
  QObject::connect(active.get(), &Backend::viz_pcm,
      this, &Backend::viz_pcm);
  QObject::connect(active.get(), &Backend::favorites_stale,
      this, &Backend::favorites_stale);
  QObject::connect(active.get(), &Backend::favorites_loaded,
      this, &Backend::favorites_loaded);
  QObject::connect(active.get(), &Backend::favorite_changed,
      this, &Backend::favorite_changed);
  QObject::connect(active.get(), &Backend::playlists_loaded,
      this, &Backend::playlists_loaded);
  QObject::connect(active.get(), &Backend::playlist_songs_loaded,
      this, &Backend::playlist_songs_loaded);
  QObject::connect(active.get(), &Backend::playlists_changed,
      this, &Backend::playlists_changed);
  QObject::connect(active.get(), &Backend::capabilities_changed,
      this, &Backend::capabilities_changed);
  QObject::connect(active.get(), &Backend::favorite_songs_loaded,
      this, &Backend::favorite_songs_loaded);
}

void
BackendController::detach()
{
  if (!active)
    return;
  // Sever signals first so disconnect_from_server() can't emit
  // connection_update(false) through this controller to the UI.
  QObject::disconnect(active.get(), nullptr, this, nullptr);
  active->disconnect_from_server();
  active.reset();
  active_id.clear();
  connected = false;
}

auto
BackendController::switch_to(const QString &id) -> bool
{
  // Only a *connected* active profile is a no-op. If the active profile failed
  // to connect, re-selecting it is a retry: rebuilding the backend re-reads the
  // profile, so a credential fix in between takes effect.
  if (id == active_id && connected)
    return false;
  auto profile = ProfileStore().profile_by_id(id);
  if (!profile)
    {
      spdlog::error("BackendController::switch_to: no profile '{}'", id.toStdString());
      return false;
    }
  auto new_backend = BackendFactory::create(*profile, libman);
  if (!new_backend)
    {
      spdlog::error("BackendController::switch_to: backend type '{}' not available for profile '{}'",
          profile->type.toStdString(), id.toStdString());
      return false;
    }
  do_switch(std::move(new_backend), id);
  return true;
}

auto
BackendController::revert_to_previous() -> bool
{
  if (previous_id.isEmpty() || previous_id == active_id)
    return false;
  return switch_to(previous_id);
}

void
BackendController::do_switch(std::shared_ptr<Backend> new_active, const QString &new_id)
{
  ProfileStore().set_active_id(new_id);
  // Only a session that was actually up is worth returning to, so switching away
  // from a backend that never connected leaves the last good id standing —
  // otherwise backing out of a second failed attempt would land on the first one.
  if (connected)
    previous_id = active_id;
  connected = false;
  emit backend_switching();
  detach();
  if (libman)
    libman->clear();
  QPixmapCache::clear();
  // After detach(), which clears it — assigning before would be undone.
  active_id = new_id;
  attach(std::move(new_active));
  if (active)
    {
      int poll = Settings().poll_interval();
      if (poll > 0)
        active->set_poll_interval(poll);
      active->connect_to_server();
    }
  emit backend_switched();
}

auto
BackendController::get_albums() -> QList<album>
{
  return active ? active->get_albums() : QList<album>{};
}

void
BackendController::fetch_songs(const album &a,
    std::function<void(const QList<song> &)> cb)
{
  if (active)
    active->fetch_songs(a, std::move(cb));
  else if (cb)
    cb({});
}

bool
BackendController::supports(Feature f) const
{
  return active && active->supports(f);
}

auto
BackendController::resolve_album_hash(const QString &native_album_id) const -> QString
{
  return active ? active->resolve_album_hash(native_album_id) : QString{};
}

void
BackendController::set_viz_pcm_enabled(bool on)
{
  if (active)
    active->set_viz_pcm_enabled(on);
}

void
BackendController::configure_audio_cache(bool enabled, qint64 budget_bytes)
{
  if (active)
    active->configure_audio_cache(enabled, budget_bytes);
}

void
BackendController::clear_audio_cache()
{
  if (active)
    active->clear_audio_cache();
}

auto
BackendController::audio_cache_bytes() const -> qint64
{
  return active ? active->audio_cache_bytes() : 0;
}

bool
BackendController::connect_to_server()
{
  return active && active->connect_to_server();
}

void
BackendController::disconnect_from_server()
{
  if (active)
    active->disconnect_from_server();
}

void
BackendController::play()
{
  if (active) active->play();
}
void
BackendController::pause()
{
  if (active) active->pause();
}
void
BackendController::stop()
{
  if (active) active->stop();
}
void
BackendController::prev()
{
  if (active) active->prev();
}
void
BackendController::next()
{
  if (active) active->next();
}
void
BackendController::seek(uint32_t ms)
{
  if (active) active->seek(ms);
}
void
BackendController::set_volume(int vol)
{
  if (active) active->set_volume(vol);
}
void
BackendController::play_pos(uint32_t pos)
{
  if (active) active->play_pos(pos);
}

void
BackendController::set_repeat(bool repeat, bool single)
{
  if (active)
    active->set_repeat(repeat, single);
}

void
BackendController::set_shuffle(bool shuffle)
{
  if (active)
    active->set_shuffle(shuffle);
}

void
BackendController::insert_queue(const QList<song> &songs, uint32_t pos)
{
  if (active)
    active->insert_queue(songs, pos);
}

void
BackendController::append_queue(const QList<song> &songs)
{
  if (active)
    active->append_queue(songs);
}

void
BackendController::replace_queue(const QList<song> &songs, uint32_t playpos)
{
  if (active)
    active->replace_queue(songs, playpos);
}

void
BackendController::remove_from_queue(const QList<QModelIndex> &indexes)
{
  if (active)
    active->remove_from_queue(indexes);
}

void
BackendController::rearrange_queue(uint32_t target, std::vector<uint32_t> indexes)
{
  if (active)
    active->rearrange_queue(target, std::move(indexes));
}

void
BackendController::refresh_library()
{
  if (active)
    active->refresh_library();
}

void
BackendController::set_poll_interval(int minutes)
{
  if (active)
    active->set_poll_interval(minutes);
}

void
BackendController::fetch_album_art(const QString &uri)
{
  if (active)
    active->fetch_album_art(uri);
}

void
BackendController::set_favorite(const QString &uri, bool fav)
{
  if (active)
    active->set_favorite(uri, fav);
}

void
BackendController::fetch_favorites()
{
  if (active)
    active->fetch_favorites();
}

void
BackendController::fetch_playlists()
{
  if (active)
    active->fetch_playlists();
}

void
BackendController::fetch_playlist_songs(const QString &id)
{
  if (active)
    active->fetch_playlist_songs(id);
}

void
BackendController::create_playlist(const QString &name, const QList<song> &songs)
{
  if (active)
    active->create_playlist(name, songs);
}

void
BackendController::rename_playlist(const QString &id, const QString &new_name)
{
  if (active)
    active->rename_playlist(id, new_name);
}

void
BackendController::delete_playlist(const QString &id)
{
  if (active)
    active->delete_playlist(id);
}

void
BackendController::add_to_playlist(const QString &id, const QList<song> &songs)
{
  if (active)
    active->add_to_playlist(id, songs);
}

void
BackendController::remove_from_playlist(const QString &id, const QList<int> &positions)
{
  if (active)
    active->remove_from_playlist(id, positions);
}

void
BackendController::rearrange_playlist(const QString &id, int target,
    const QList<int> &moved)
{
  if (active)
    active->rearrange_playlist(id, target, moved);
}
