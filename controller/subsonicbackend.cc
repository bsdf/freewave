#include "subsonicbackend.hh"
#include "controller/gstaudioengine.hh"
#include "controller/settings.hh"
#include "reorder.hh"

#include <algorithm>
#include <random>
#include <set>

#include <QCoreApplication>
#include <QSettings>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QCryptographicHash>

// SHA-256 hashes of known per-server "no cover" placeholder images. Neither
// Navidrome nor Ampache has an explicit no-art response — both silently serve
// a bundled placeholder instead of 404ing when an album has no art. See
// fetch_album_art() below for why an exact hash match, not a content-type/size
// guess, is what tells "no art" apart from a real cover.
//
// Navidrome: consts.PlaceholderAlbumArt = "album-placeholder.webp", streamed
// verbatim by GetOrPlaceholder in server/subsonic/media_retrieval.go.
// Verified against a live Navidrome 0.63.0 server.
//
// Ampache: a 1400x1400 PNG served by every getCoverArt call for an album it
// has gathered no art for (has_art: false). Verified against a live Ampache
// 8.0.0 instance across five distinct albums (with and without songs) —
// byte-identical every time.
//
// The positive path (these hashes actually matching a placeholder) is
// verified manually against a live server, not by CI — the fixtures would be
// tens of KB of binary checked into the test tree, not worth it for two
// constants. test_subsonicbackend.cc covers the negative path: a real,
// same-size, same-content-type cover that isn't byte-identical must still
// come through as art.
static const QSet<QByteArray> KNOWN_PLACEHOLDER_HASHES = {
    "273a4dbd61dfbb0a12d5d8ffe780eb7a3d4d000bc9771c5411cc70ae4dfa8a1f", // Navidrome
    "ae2a1f8906adf867451aa2b62c3484a0267c0b9d996b9004dc0b64a2eb5b2104", // Ampache
};

// Fill a song from a Subsonic song/entry JSON object. `duration` is seconds in
// the API; song::duration is ms. `album_hash` is supplied by the caller: the
// album's real hash for getAlbum, a profile-scoped fallback for standalone
// songs (playlist entries / starred), which lack the album's release MBID.
static auto
parse_subsonic_song(const QJsonObject &obj, const QString &album_hash) -> song
{
  song s;
  s.uri = obj["id"].toString();
  s.title = obj["title"].toString();
  s.artist = obj["artist"].toString();
  s.track_number = static_cast<uint32_t>(obj["track"].toInt());
  s.disc_number = static_cast<uint32_t>(obj["discNumber"].toInt(1));
  s.duration = static_cast<uint32_t>(obj["duration"].toInt()) * 1000;
  s.album_hash = album_hash;
  s.native_album_id = obj["albumId"].toString();
  return s;
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

SubsonicBackend::SubsonicBackend(QString server_url, SubsonicAuth auth,
    std::shared_ptr<LibraryManager> libman, QString profile_id)
  : SubsonicBackend{
        std::move(server_url),
        std::move(auth),
        new GstAudioEngine(),
        std::move(libman),
        std::move(profile_id)}
{
}

SubsonicBackend::SubsonicBackend(QString server_url, SubsonicAuth auth,
    AudioEngine *audio_engine, std::shared_ptr<LibraryManager> libman,
    QString profile_id)
  : client{new SubsonicClient{std::move(server_url), std::move(auth), this}}
  , profile_id{std::move(profile_id)}
  , libman{libman ? std::move(libman) : std::make_shared<LibraryManager>()}
{
  engine = audio_engine;
  if (!engine->parent()) engine->setParent(this);
  QObject::connect(client, &SubsonicClient::certificate_untrusted,
      this, &Backend::certificate_untrusted);

  poll_timer = new QTimer(this);
  Settings settings_facade;
  auto saved_interval = settings_facade.poll_interval();
  poll_timer->setInterval(saved_interval * 60 * 1000);
  QObject::connect(poll_timer, &QTimer::timeout, this, &SubsonicBackend::poll_library);

  QObject::connect(engine, &AudioEngine::state_changed,
      this, &SubsonicBackend::engine_state_changed);
  QObject::connect(engine, &AudioEngine::busy_changed,
      this, [this](AudioEngine::Busy busy) {
        // Anything the listener is waiting on — a stream opening, a seek
        // landing — gets the link to itself. A prefetch running alongside a
        // deep seek was measured tripling how long that seek takes.
        if (cache) cache->set_held(busy != AudioEngine::Busy::None);
        emit_playback_state();
      });
  QObject::connect(engine, &AudioEngine::position_changed,
      this, [this](uint32_t) { emit_playback_state(); });
  QObject::connect(engine, &AudioEngine::duration_changed,
      this, [this](uint32_t) { emit_playback_state(); });
  QObject::connect(engine, &AudioEngine::track_changed,
      this, &SubsonicBackend::engine_track_changed);
  QObject::connect(engine, &AudioEngine::track_finished,
      this, &SubsonicBackend::engine_track_finished);
  QObject::connect(engine, &AudioEngine::error,
      this, [this](const QString &msg) { emit error(msg); });

#ifdef ENABLE_VISUALIZER
  // Surface the engine's local PCM tap to the visualizer. The engine already
  // marshals frames onto this thread, so a plain forward keeps them ordered.
  QObject::connect(engine, &AudioEngine::viz_pcm,
      this, &SubsonicBackend::viz_pcm);
#endif
}

SubsonicBackend::~SubsonicBackend()
{
  save_state();

  // Before any member state goes: abort() emits finished() synchronously, and
  // our handlers emit signals (e.g. library_refresh_active) into a UI that is
  // currently being torn down — re-entering a half-destroyed MainWindow
  // segfaults.
  client->abort_all();
}

auto
SubsonicBackend::set_track_cache(TrackCache *track_cache) -> void
{
  delete cache;
  cache = track_cache;
  if (!cache) return;

  cache->setParent(this);
  cache->set_scope(profile_id);

  QObject::connect(cache, &TrackCache::warmed, this,
      [this](const QString &track_id, const QUrl &local) {
        // The successor was armed as a stream URL because it wasn't on disk
        // yet. Now it is, so the gapless handoff can be a local one. Losing
        // this race costs nothing — the stream URL still plays.
        if (track_id.isEmpty() || track_id != armed_next_id) return;
        engine->set_next_url(local);
        armed_next_id.clear();
      });
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------

auto
SubsonicBackend::save_state() const -> void
{
  auto serialize_songs = [](const QList<song> &list) {
    QJsonArray arr;
    for (const auto &s : list)
      {
        QJsonObject o;
        o["uri"] = s.uri;
        o["title"] = s.title;
        o["artist"] = s.artist;
        o["track_number"] = static_cast<int>(s.track_number);
        o["disc_number"] = static_cast<int>(s.disc_number);
        o["duration"] = static_cast<int>(s.duration);
        o["album_hash"] = s.album_hash;
        o["native_album_id"] = s.native_album_id;
        arr.append(o);
      }
    return arr;
  };

  QJsonObject root;
  root["version"] = 1;
  root["queue_pos"] = queue_pos;
  root["position_ms"] = static_cast<int>(engine->position_ms());
  root["volume"] = engine->volume();
  root["repeat"] = repeat;
  root["single"] = single;
  root["shuffle"] = shuffle;
  root["queue"] = serialize_songs(queue);
  if (!unshuffled_queue.isEmpty())
    root["unshuffled_queue"] = serialize_songs(unshuffled_queue);

  QSettings().setValue(QString("subsonic_state/%1").arg(profile_id),
      QJsonDocument(root).toJson(QJsonDocument::Compact));
}

auto
SubsonicBackend::restore_state() -> void
{
  auto raw = QSettings().value(QString("subsonic_state/%1").arg(profile_id)).toByteArray();
  if (raw.isEmpty()) return;

  auto doc = QJsonDocument::fromJson(raw);
  if (!doc.isObject()) return;

  auto root = doc.object();

  auto deserialize_songs = [](const QJsonArray &arr) {
    QList<song> list;
    for (const auto &v : arr)
      {
        auto o = v.toObject();
        song s;
        s.uri = o["uri"].toString();
        s.title = o["title"].toString();
        s.artist = o["artist"].toString();
        s.track_number = static_cast<uint32_t>(o["track_number"].toInt());
        s.disc_number = static_cast<uint32_t>(o["disc_number"].toInt());
        s.duration = static_cast<uint32_t>(o["duration"].toInt());
        s.album_hash = o["album_hash"].toString();
        s.native_album_id = o["native_album_id"].toString(); // absent in states written before it existed
        list.append(s);
      }
    return list;
  };

  queue = deserialize_songs(root["queue"].toArray());
  unshuffled_queue = deserialize_songs(root["unshuffled_queue"].toArray());

  auto saved_pos = root["queue_pos"].toInt(0);
  queue_pos = (saved_pos < queue.size()) ? saved_pos : 0;

  restore_position_ms = static_cast<uint32_t>(root["position_ms"].toInt(0));
  repeat = root["repeat"].toBool(false);
  single = root["single"].toBool(false);
  shuffle = root["shuffle"].toBool(false);

  engine->set_volume(root["volume"].toInt(100));
  update_engine_next_url();

  emit queue_changed(queue);

  if (queue_pos >= 0 && queue_pos < queue.size())
    emit current_song_changed(queue[queue_pos]);

  // Synthetic stopped state so UI shows correct position before play is pressed
  PlaybackState ps;
  ps.state = PlayState::Stopped;
  ps.elapsed_ms = restore_position_ms;
  ps.total_ms = (queue_pos >= 0 && queue_pos < queue.size())
                    ? queue[queue_pos].duration
                    : 0;
  ps.volume = engine->volume();
  ps.repeat = repeat;
  ps.shuffle = shuffle;
  ps.single = single;
  ps.queue_pos = queue_pos;
  emit playback_state_changed(ps);
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

namespace {

// Subsonic reserves a block of error codes for credential rejections: 40 wrong
// username or password, 41/42/43 an auth mechanism the server won't take, 44
// invalid API key, 50 authenticated but not permitted. Anything else — a
// transport failure, a body that was never a Subsonic envelope, a code the spec
// spends on other things — is not something the user fixes in Settings.
auto
classify_connect_failure(const SubsonicReply &r) -> Backend::ConnectError
{
  if (!r.transport_ok)
    return Backend::ConnectError::Unreachable;
  switch (r.error_code)
    {
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 50:
      return Backend::ConnectError::Auth;
    default:
      return Backend::ConnectError::Unreachable;
    }
}

} // namespace

auto
SubsonicBackend::connect_to_server() -> bool
{
  if (connected) return true;

  fetch_extensions();

  return true;
}

void
SubsonicBackend::fetch_extensions()
{
  // A reconnect re-runs this: drop the previous answer so a failed or
  // inconclusive re-fetch can't leave the last one standing as confirmed.
  extensions.clear();
  extensions_known = false;
  starred_needs_v1 = false;
  cap_favorites = true;
  cap_playlists = true;

  client->get("getOpenSubsonicExtensions", [this](const SubsonicReply &r) {
    // Only a genuine OpenSubsonic server (openSubsonic:true) enumerates
    // extensions at all; a plain Subsonic server's "ok" ping-like reply
    // (and the test fixture's default stub) omits the field entirely and
    // must NOT read as "confirmed empty" — extensions_known stays false.
    if (r.ok && r.payload("openSubsonic").toBool())
      {
        extensions_known = true;
        for (const auto &ext : r.payload("openSubsonicExtensions").toArray())
          {
            auto obj = ext.toObject();
            QList<int> versions;
            for (const auto &v : obj["versions"].toArray())
              versions.append(v.toInt());
            extensions.insert(obj["name"].toString(), versions);
          }
      }

    // apiKey mode needs the server to confirm apiKeyAuthentication: the ping
    // that would otherwise catch this can't tell "server doesn't support
    // apiKey" apart from "wrong apiKey" (both fail identically), and by the
    // time ping runs the connection is already declared bad either way — a
    // clear message up front is strictly better. Skipped when extensions
    // couldn't be confirmed (non-OpenSubsonic server, network error) so
    // absence-of-evidence never blocks a connection ping itself would allow.
    if (client->uses_api_key() && extensions_known && !has_extension("apiKeyAuthentication"))
      {
        const auto msg = QStringLiteral(
            "This server does not support API key authentication — "
            "use username/password instead.");
        spdlog::error("SubsonicBackend: {}", msg);
        emit error(msg);
        emit connection_failed(ConnectError::Auth, msg);
        emit connection_update(false);
        return;
      }

    send_ping();
  });
}

void
SubsonicBackend::send_ping()
{
  client->get("ping", [this](const SubsonicReply &r) {
    if (!r.ok)
      {
        spdlog::error("SubsonicBackend: ping failed: {}", r.error);
        emit error(r.error);
        emit connection_failed(classify_connect_failure(r), r.error);
        emit connection_update(false);
        return;
      }

    spdlog::info("SubsonicBackend: connected to {}", client->server());
    connected = true;
    emit connection_update(true);

    restore_state();
    emit_playback_state();
    load_library();
    if (poll_timer->interval() > 0)
      poll_timer->start();

    // Stop the audio engine before the event loop exits so GStreamer can
    // tear down while threads are still available.
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, engine,
        &AudioEngine::stop, Qt::UniqueConnection);
  });
}

auto
SubsonicBackend::disconnect_from_server() -> void
{
  if (!connected) return;

  engine->stop();
  poll_timer->stop();
  connected = false;

  emit connection_update(false);
}

// ---------------------------------------------------------------------------
// Library loading
// ---------------------------------------------------------------------------

auto
SubsonicBackend::refresh_library() -> void
{
  poll_library();
}

auto
SubsonicBackend::set_poll_interval(int minutes) -> void
{
  poll_timer->stop();
  poll_timer->setInterval(minutes * 60 * 1000);
  if (connected && minutes > 0)
    poll_timer->start();
}

static constexpr int ALBUM_PAGE_SIZE = 500;

auto
SubsonicBackend::load_library() -> void
{
  if (!connected) return;

  emit library_refresh_active(true);
  pending_albums.clear();
  fetch_album_page(0);
}

auto
SubsonicBackend::poll_library() -> void
{
  load_library();

  // No push channel, so a poll is the only moment a star changed by another
  // client can be noticed. Report that; the favorites owner decides whether to
  // read. Deliberately not reported at connect — the owner reads on
  // connection_update, and reporting it here would make that two reads.
  if (connected)
    emit favorites_stale();
}

auto
SubsonicBackend::fetch_album_page(int offset) -> void
{
  client->get("getAlbumList2",
      {
          {"type", "alphabeticalByName"},
          {"size", QString::number(ALBUM_PAGE_SIZE)},
          {"offset", QString::number(offset)},
      },
      [this, offset](const SubsonicReply &r) {
        // Not transport_ok: a reply that arrived and was refused carries no
        // payload either. Some bridges answer an endpoint they don't implement
        // with HTTP 200 and a body that isn't a Subsonic envelope at all, which
        // parses to zero albums and would otherwise read as a library that
        // finished loading and happens to be empty.
        if (!r.ok)
          {
            spdlog::error("SubsonicBackend: getAlbumList2 failed (offset {}): {}",
                offset, r.error);
            emit library_refresh_active(false);
            emit library_load_failed(r.error);
            return;
          }

        auto albums_json = r.payload("albumList2")["album"].toArray();

        for (const auto &val : albums_json)
          {
            auto obj = val.toObject();
            auto native_id = obj["id"].toString();
            // MusicBrainz release id (OpenSubsonic extension) is a globally unique,
            // server-independent identity — prefer it so art caches and cross-server
            // MPD/Subsonic lookups can share the same key. Lowercased to match
            // MpdManager's normalization. Fall back to a profile-scoped native id,
            // since the raw Subsonic id isn't unique across servers.
            auto mbid = obj["musicBrainzId"].toString();
            auto album_hash = mbid.isEmpty()
                                  ? QString("%1:%2").arg(profile_id, native_id)
                                  : mbid.toLower();
            auto name = obj["name"].toString();
            auto artist = obj["artist"].toString();
            auto sort_artist = obj["artistSort"].toString();
            if (sort_artist.isEmpty()) sort_artist = artist;
            auto year = QString::number(obj["year"].toInt());
            // coverArt is optional in the Subsonic schema and Ampache omits it
            // outright; the album id is a valid getCoverArt id there, so fall back to
            // it rather than requesting an empty one.
            auto uri = obj["coverArt"].toString();
            if (uri.isEmpty()) uri = native_id;
            auto created = QDateTime::fromString(obj["created"].toString(), Qt::ISODate);

            album a{uri, album_hash, name, artist, year, sort_artist, created};
            a.native_id = native_id;
            a.song_count = obj["songCount"].toInt();
            for (const auto &t : obj["releaseTypes"].toArray())
              if (t.toString().compare("live", Qt::CaseInsensitive) == 0)
                {
                  a.is_live = true;
                  break;
                }
            pending_albums.append(a);
          }

        spdlog::debug("SubsonicBackend: fetched {} albums at offset {} ({} pending)",
            albums_json.size(), offset, pending_albums.size());

        if (albums_json.size() == ALBUM_PAGE_SIZE)
          {
            fetch_album_page(offset + ALBUM_PAGE_SIZE);
          }
        else
          {
            auto previous_index = libman->native_index();

            libman->clear();
            for (const auto &a : pending_albums)
              libman->add_album(a.album_hash, a);
            pending_albums.clear();

            const bool mapping_changed = libman->native_index() != previous_index;

            spdlog::info("SubsonicBackend: library refresh complete, loaded {} albums total",
                libman->get_albums().size());
            emit library_changed();
            if (mapping_changed)
              emit album_mapping_changed();
            emit library_refresh_active(false);
          }
      });
}

auto
SubsonicBackend::get_albums() -> QList<album>
{
  return libman->get_albums();
}

// The raw Subsonic id isn't unique across servers, so an id with no library
// album behind it — the library hasn't loaded, or the song's album isn't in it —
// falls back to a profile-scoped key rather than the bare id.
auto
SubsonicBackend::resolve_album_hash(const QString &native_album_id) const -> QString
{
  if (native_album_id.isEmpty())
    return {};
  auto hash = libman->album_hash_for_native(native_album_id);
  return hash.isEmpty() ? QString("%1:%2").arg(profile_id, native_album_id) : hash;
}

auto
SubsonicBackend::fetch_songs(const album &a, std::function<void(const QList<song> &)> cb) -> void
{
  const auto album_hash = a.album_hash;
  const auto native_id = a.native_id;

  // Cache hit — return synchronously, issue no request.
  auto cached = libman->get_songs(album_hash);
  if (!cached.isEmpty())
    {
      if (cb) cb(cached);
      return;
    }

  // UI fetches (no callback) are fire-and-forget — a prefetch on selection and
  // the open-on-activate can both ask for the same album, so dedup in-flight
  // requests. Callback fetches (queueing) always run; they carry their own work.
  if (!cb)
    {
      if (songs_inflight.contains(album_hash)) return;
      songs_inflight.insert(album_hash);
    }

  // native_id (not album_hash) is what the server understands as an album id.
  client->get("getAlbum", {{"id", native_id}}, [this, album_hash, cb](const SubsonicReply &r) {
    songs_inflight.remove(album_hash);

    // A refused reply carries no payload, and taking it for one would cache an
    // empty tracklist against the album — a miss that never retries.
    if (!r.ok)
      {
        spdlog::warn("SubsonicBackend: getAlbum failed for [{}]: {}",
            album_hash, r.error);
        if (cb) cb({});
        return;
      }

    auto songs_json = r.payload("album")["song"].toArray();

    QList<song> songs;
    for (const auto &val : songs_json)
      songs.append(parse_subsonic_song(val.toObject(), album_hash));

    libman->set_songs(album_hash, songs);
    spdlog::debug("SubsonicBackend: loaded {} songs for [{}]",
        songs.size(), album_hash);
    emit songs_changed(album_hash);
    if (cb) cb(songs);
  });
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

auto
SubsonicBackend::play_current() -> void
{
  if (queue_pos < 0 || queue_pos >= queue.size()) return;

  const auto &s = queue[queue_pos];

  engine->play(playback_url(s.uri), restore_position_ms);
  restore_position_ms = 0;

  update_engine_next_url();
  emit current_song_changed(s);
  // Push a fresh PlaybackState now so the queue-list highlight (driven by
  // queue_pos in PlaybackState) moves immediately, rather than waiting for the
  // next position tick ~1 s later. The gapless path already does this.
  emit_playback_state();
}

auto
SubsonicBackend::play() -> void
{
  switch (engine->state())
    {
    case AudioEngine::State::Paused:
      engine->resume();
      break;
    case AudioEngine::State::Playing:
      // Already playing — including the case that matters, a track whose seek
      // has not landed yet. Restarting it here threw the listener back to zero
      // for the sin of pressing play on a player that looked stuck.
      break;
    default:
      play_current();
      break;
    }
}

auto
SubsonicBackend::pause() -> void
{
  engine->pause();
}

auto
SubsonicBackend::stop() -> void
{
  engine->stop();
}

auto
SubsonicBackend::prev() -> void
{
  if (queue.isEmpty()) return;
  restore_position_ms = 0;
  if (queue_pos > 0)
    queue_pos--;
  else if (repeat)
    queue_pos = static_cast<int>(queue.size()) - 1; // wrap to last (Repeat All)
  else
    return; // at the start with repeat off: hard end
  play_current();
}

auto
SubsonicBackend::next() -> void
{
  if (queue.isEmpty()) return;
  restore_position_ms = 0;
  if (queue_pos + 1 < queue.size())
    queue_pos++;
  else if (repeat)
    queue_pos = 0; // wrap to first (Repeat All)
  else
    return; // at the end with repeat off: hard end
  play_current();
}

auto
SubsonicBackend::seek(uint32_t ms) -> void
{
  engine->seek(ms);
}

auto
SubsonicBackend::set_volume(int vol) -> void
{
  engine->set_volume(vol);
}

auto
SubsonicBackend::play_pos(uint32_t pos) -> void
{
  restore_position_ms = 0;
  queue_pos = static_cast<int>(pos);
  play_current();
}

// ---------------------------------------------------------------------------
// Playback options
// ---------------------------------------------------------------------------

auto
SubsonicBackend::set_repeat(bool on_repeat, bool on_single) -> void
{
  repeat = on_repeat;
  single = on_single;
  update_engine_next_url();
  emit_playback_state();
}

auto
SubsonicBackend::set_shuffle(bool on) -> void
{
  shuffle = on;

  if (!queue.isEmpty())
    {
      QString current_uri;
      if (queue_pos >= 0 && queue_pos < queue.size())
        current_uri = queue[queue_pos].uri;

      if (on)
        {
          if (unshuffled_queue.isEmpty())
            unshuffled_queue = queue;
          std::shuffle(queue.begin(), queue.end(),
              std::mt19937{std::random_device{}()});

          // Pin the currently-playing track to the top so the shuffled order
          // reads as a forward "up next" list rather than scattering it mid-queue.
          if (!current_uri.isEmpty())
            {
              auto it = std::find_if(queue.begin(), queue.end(),
                  [&](const song &s) { return s.uri == current_uri; });
              if (it != queue.end())
                std::rotate(queue.begin(), it, it + 1);
            }
        }
      else if (!unshuffled_queue.isEmpty())
        {
          queue = unshuffled_queue;
          unshuffled_queue.clear();
        }

      // Re-sync queue_pos to wherever the playing track ended up (index 0 after
      // a shuffle; its original slot after restoring the unshuffled order).
      if (!current_uri.isEmpty())
        if (auto it = std::ranges::find(queue, current_uri, &song::uri);
            it != queue.end())
          queue_pos = static_cast<int>(std::ranges::distance(queue.begin(), it));

      update_engine_next_url();
      emit queue_changed(queue);
    }

  emit_playback_state();
}

// ---------------------------------------------------------------------------
// Queue
// ---------------------------------------------------------------------------

auto
SubsonicBackend::stream_url(const QString &track_id) const -> QUrl
{
  return client->url("stream", {{"id", track_id}, {"format", "raw"}});
}

auto
SubsonicBackend::playback_url(const QString &track_id) -> QUrl
{
  if (cache)
    if (auto local = cache->local_for(track_id); !local.isEmpty())
      return local;
  return stream_url(track_id);
}

auto
SubsonicBackend::arm_next(const QString &track_id) -> void
{
  if (cache)
    if (auto local = cache->local_for(track_id); !local.isEmpty())
      {
        engine->set_next_url(local);
        return;
      }

  engine->set_next_url(stream_url(track_id));
  armed_next_id = track_id;

  if (!cache) return;

  // Never the track that is playing: its bytes are already coming down the same
  // link, and a second full-file GET alongside a stream was measured tripling
  // the time a seek in that stream takes. (single + repeat, and a one-track
  // repeating queue, both arm the current track as their own successor.)
  if (queue_pos >= 0 && queue_pos < queue.size() && queue[queue_pos].uri == track_id)
    return;

  // Stopped means a queue restored at startup, arming a successor for a track
  // nobody has pressed play on yet — not something to spend the link on.
  if (engine->state() == AudioEngine::State::Stopped) return;

  cache->warm(track_id, stream_url(track_id));
}

auto
SubsonicBackend::next_track_id() const -> QString
{
  if (queue_pos < 0 || queue.isEmpty()) return {};

  // single + repeat loops the current track; single without repeat plays it
  // once and stops (MPD semantics), so there is no gapless successor.
  if (single) return repeat ? queue[queue_pos].uri : QString{};

  int next_pos = queue_pos + 1;
  if (next_pos >= queue.size())
    {
      if (!repeat) return {};
      next_pos = 0;
    }
  return queue[next_pos].uri;
}

auto
SubsonicBackend::update_engine_next_url() -> void
{
  armed_next_id.clear();

  const QString next_id = next_track_id();
  if (next_id.isEmpty())
    engine->clear_next_url();
  else
    arm_next(next_id);

  if (cache)
    {
      const QString current_id = (queue_pos >= 0 && queue_pos < queue.size())
                                     ? queue[queue_pos].uri
                                     : QString{};
      // Evicting what is playing, or what the engine has already been handed as
      // its successor, would pull a file out from under the pipeline.
      cache->set_pinned({current_id, next_id});
    }
}

auto
SubsonicBackend::insert_queue(const QList<song> &songs, uint32_t pos) -> void
{
  auto idx = static_cast<int>(pos);
  for (int i = 0; i < songs.size(); ++i)
    queue.insert(idx + i, songs[i]);

  update_engine_next_url();
  emit queue_changed(queue);
}

auto
SubsonicBackend::append_queue(const QList<song> &songs) -> void
{
  queue.append(songs);
  update_engine_next_url();
  emit queue_changed(queue);
}

auto
SubsonicBackend::replace_queue(const QList<song> &songs, uint32_t playpos) -> void
{
  restore_position_ms = 0;
  unshuffled_queue.clear();
  queue = songs;
  queue_pos = static_cast<int>(playpos);
  if (shuffle)
    {
      unshuffled_queue = queue;
      std::shuffle(queue.begin(), queue.end(),
          std::mt19937{std::random_device{}()});
      // Pin the track the user started from to the top of the shuffled order.
      const auto &start_uri = songs.value(static_cast<int>(playpos)).uri;
      auto it = std::find_if(queue.begin(), queue.end(),
          [&](const song &s) { return s.uri == start_uri; });
      if (it != queue.end())
        std::rotate(queue.begin(), it, it + 1);
      queue_pos = 0;
    }
  emit queue_changed(queue);
  play_current();
}

auto
SubsonicBackend::remove_from_queue(const QList<QModelIndex> &indexes) -> void
{
  if (indexes.isEmpty()) return;

  QSet<int> deleted;
  for (const auto &idx : indexes)
    if (idx.row() >= 0 && idx.row() < queue.size())
      deleted.insert(idx.row());

  bool current_deleted = deleted.contains(queue_pos);

  int rows_before = 0;
  for (int r : deleted)
    if (r < queue_pos) ++rows_before;

  // Count removals per URI so a duplicated track only drops the same number of
  // copies from unshuffled_queue below. A plain URI set would erase every copy,
  // desyncing the two queues and losing tracks on un-shuffle.
  QHash<QString, int> deleted_uri_counts;
  for (int r : deleted)
    deleted_uri_counts[queue[r].uri]++;

  auto rows = indexes;
  std::sort(rows.begin(), rows.end(),
      [](const QModelIndex &a, const QModelIndex &b) { return a.row() > b.row(); });
  for (const auto &idx : rows)
    if (idx.row() >= 0 && idx.row() < queue.size())
      queue.removeAt(idx.row());

  if (!unshuffled_queue.isEmpty())
    {
      auto it = unshuffled_queue.begin();
      while (it != unshuffled_queue.end())
        {
          auto cnt = deleted_uri_counts.find(it->uri);
          if (cnt != deleted_uri_counts.end() && cnt.value() > 0)
            {
              --cnt.value();
              it = unshuffled_queue.erase(it);
            }
          else
            ++it;
        }
    }

  queue_pos -= rows_before;

  if (current_deleted)
    {
      bool was_playing = (engine->state() == AudioEngine::State::Playing);

      if (queue.isEmpty())
        {
          queue_pos = 0;
          engine->stop();
          update_engine_next_url();
          emit queue_changed(queue);
          emit_playback_state();
          return;
        }

      // Deleted the last song: wrap to first (matches MPD behaviour)
      if (queue_pos >= queue.size())
        queue_pos = 0;

      update_engine_next_url();
      emit queue_changed(queue);

      if (was_playing)
        play_current(); // plays new song and emits current_song_changed
      else
        {
          emit current_song_changed(queue[queue_pos]);
          emit_playback_state();
        }
      return;
    }

  update_engine_next_url();
  emit queue_changed(queue);
  emit_playback_state();
}

auto
SubsonicBackend::rearrange_queue(uint32_t target, std::vector<uint32_t> indexes) -> void
{
  if (indexes.empty()) return;

  std::sort(indexes.begin(), indexes.end());

  QList<song> moving;
  for (auto idx : indexes)
    if (static_cast<int>(idx) < queue.size())
      moving.append(queue[static_cast<int>(idx)]);

  std::set<uint32_t> idx_set(indexes.begin(), indexes.end());
  QList<song> remaining;
  for (int i = 0; i < queue.size(); ++i)
    if (!idx_set.count(static_cast<uint32_t>(i)))
      remaining.append(queue[i]);

  auto before = static_cast<int>(
      std::count_if(indexes.begin(), indexes.end(),
          [target](uint32_t i) { return i < target; }));
  auto insert_pos = std::min(
      static_cast<int>(target) - before, static_cast<int>(remaining.size()));

  for (int i = moving.size() - 1; i >= 0; --i)
    remaining.insert(insert_pos, moving[i]);

  if (queue_pos >= 0 && queue_pos < queue.size())
    {
      auto current_uri = queue[queue_pos].uri;
      if (auto it = std::ranges::find(remaining, current_uri, &song::uri);
          it != remaining.end())
        queue_pos = static_cast<int>(std::ranges::distance(remaining.begin(), it));
    }

  queue = remaining;
  update_engine_next_url();
  emit queue_changed(queue);
}

// ---------------------------------------------------------------------------
// Art
// ---------------------------------------------------------------------------

auto
SubsonicBackend::fetch_album_art(const QString &uri) -> void
{
  if (!connected)
    {
      emit album_art_received(uri, {});
      return;
    }

  client->get("getCoverArt", {{"id", uri}}, [this, uri](const SubsonicReply &r) {
    if (!r.transport_ok)
      {
        spdlog::trace("SubsonicBackend: getCoverArt failed for [{}]: {}",
            uri, r.error);
        emit album_art_received(uri, {});
        return;
      }

    auto hash = QCryptographicHash::hash(r.body, QCryptographicHash::Sha256).toHex();
    if (KNOWN_PLACEHOLDER_HASHES.contains(hash))
      {
        spdlog::trace("SubsonicBackend: [{}] is a known server placeholder cover, "
                      "treating as no art",
            uri);
        emit album_art_received(uri, {});
        return;
      }

    emit album_art_received(uri, r.body); }, /*binary_reply=*/true);
}

// ---------------------------------------------------------------------------
// Favorites (star / unstar)
// ---------------------------------------------------------------------------

auto
SubsonicBackend::set_favorite(const QString &uri, bool fav) -> void
{
  if (!connected) return;

  client->get(fav ? "star" : "unstar", {{"id", uri}},
      [this, uri, fav](const SubsonicReply &r) {
        if (!r.ok)
          {
            spdlog::error("SubsonicBackend: {} failed for [{}]: {}",
                fav ? "star" : "unstar", uri, r.error);
            emit error(r.error);
            return;
          }

        emit favorite_changed(uri, fav);
      });
}

void
SubsonicBackend::fetch_starred_endpoint(const QString &endpoint, const QString &key,
    std::function<void(const QJsonArray &)> on_songs)
{
  if (endpoint == QLatin1String("getStarred2") && starred_needs_v1)
    {
      fetch_starred_endpoint("getStarred", "starred", std::move(on_songs));
      return;
    }

  client->get(endpoint, [this, endpoint, key, on_songs](const SubsonicReply &r) {
    if (r.ok)
      {
        on_songs(r.payload(key)["song"].toArray());
        return;
      }

    if (endpoint == "getStarred2")
      {
        spdlog::info("SubsonicBackend: getStarred2 unusable, falling back to getStarred");
        fetch_starred_endpoint("getStarred", "starred",
            [this, on_songs](const QJsonArray &songs) {
              starred_needs_v1 = true;
              on_songs(songs);
            });
        return;
      }

    // Both endpoints are out. Only a server that answered and refused proves it
    // has no favorites read; an unreachable server proves nothing, and silently
    // retrying it each poll tick is the right behaviour there.
    if (r.transport_ok)
      {
        downgrade_favorites();
        return;
      }
    spdlog::warn("SubsonicBackend: favorites read failed on both endpoints: {}",
        r.error);
  });
}

auto
SubsonicBackend::fetch_favorites() -> void
{
  if (!connected) return;

  fetch_starred_endpoint("getStarred2", "starred2", [this](const QJsonArray &arr) {
    QSet<QString> uris;
    QList<song> songs;
    uris.reserve(arr.size());
    songs.reserve(arr.size());

    for (const auto &v : arr)
      {
        auto obj = v.toObject();
        uris.insert(obj["id"].toString());
        // Standalone songs carry no release MBID, so they resolve through the
        // library index. Before the library lands that index is empty and the
        // fallback hash leaves the row without a cover or album name — the song
        // keeps its native album id so that can be corrected in place.
        songs.append(
            parse_subsonic_song(obj, resolve_album_hash(obj["albumId"].toString())));
      }

    emit favorites_loaded(uris);
    emit favorite_songs_loaded(songs);
  });
}

// ---------------------------------------------------------------------------
// Stored playlists
// ---------------------------------------------------------------------------

auto
SubsonicBackend::fetch_playlists() -> void
{
  if (!connected) return;

  client->get("getPlaylists", [this](const SubsonicReply &r) {
    if (!r.ok)
      {
        spdlog::warn("SubsonicBackend: getPlaylists failed: {}", r.error);
        emit playlists_loaded({});
        return;
      }

    QList<playlist_info> lists;
    for (const auto &v : r.payload("playlists")["playlist"].toArray())
      {
        auto obj = v.toObject();
        playlist_info pl;
        pl.id = obj["id"].toString();
        pl.name = obj["name"].toString();
        pl.song_count = obj["songCount"].toInt(-1);
        pl.duration_ms = obj.contains("duration")
                             ? qint64(obj["duration"].toInt()) * 1000
                             : -1;
        pl.last_modified = QDateTime::fromString(obj["changed"].toString(), Qt::ISODate);
        lists.append(pl);
      }

    emit playlists_loaded(lists);
  });
}

auto
SubsonicBackend::fetch_playlist_songs(const QString &id) -> void
{
  if (!connected) return;

  client->get("getPlaylist", {{"id", id}}, [this, id](const SubsonicReply &r) {
    if (!r.ok)
      {
        spdlog::warn("SubsonicBackend: getPlaylist failed for [{}]: {}", id, r.error);
        emit playlist_songs_loaded(id, {});
        return;
      }

    QList<song> songs;
    for (const auto &v : r.payload("playlist")["entry"].toArray())
      {
        auto obj = v.toObject();
        songs.append(
            parse_subsonic_song(obj, resolve_album_hash(obj["albumId"].toString())));
      }

    // Cache so rearrange_playlist can rebuild the full ordered id list.
    playlist_contents.insert(id, songs);
    emit playlist_songs_loaded(id, songs);
  });
}

// Reported once per connection: the poll tick and every later mutation would
// otherwise repeat the same message for a server that is never going to change
// its mind mid-session.
void
SubsonicBackend::downgrade_favorites()
{
  if (!cap_favorites)
    return;
  cap_favorites = false;
  spdlog::warn("SubsonicBackend: server has no usable favorites read — "
               "disabling favorites for this connection");
  emit error(QStringLiteral("This server does not support favorites."));
  emit capabilities_changed();
}

void
SubsonicBackend::downgrade_playlists()
{
  if (!cap_playlists)
    return;
  cap_playlists = false;
  spdlog::warn("SubsonicBackend: server refused to create a playlist — "
               "disabling playlists for this connection");
  emit capabilities_changed();
}

// Shared success/error handling for a mutation that, on ok, invalidates the
// affected playlist's contents cache and reports playlists_changed.
auto
SubsonicBackend::run_playlist_mutation(const QUrl &url, const QString &context,
    const QString &invalidate_id, RefusalMeaning refusal) -> void
{
  client->get_url(url, [this, context, invalidate_id, refusal](const SubsonicReply &r) {
    if (!r.transport_ok)
      {
        spdlog::error("SubsonicBackend: {} failed: {}", context, r.error);
        emit error(r.error);
        return;
      }

    if (!r.ok)
      {
        // The reply arrived intact and was refused on its merits — the one
        // signal that distinguishes "this server can't do playlists" from a
        // request that merely didn't get through (handled above).
        if (refusal == RefusalMeaning::NoPlaylistSupport)
          {
            downgrade_playlists();
            emit error(QStringLiteral(
                "This server does not support creating playlists."));
            return;
          }
        spdlog::error("SubsonicBackend: {} rejected: {}", context, r.error);
        emit error(r.error);
        return;
      }

    if (!invalidate_id.isEmpty())
      playlist_contents.remove(invalidate_id);
    emit playlists_changed(invalidate_id);
  });
}

auto
SubsonicBackend::create_playlist(const QString &name, const QList<song> &songs) -> void
{
  if (!connected) return;

  QList<QPair<QString, QString>> params{{"name", name}};
  for (const auto &s : songs)
    params.append({"songId", s.uri});

  // A new id we don't know yet — nothing to invalidate; the manager refetches.
  run_playlist_mutation(client->url("createPlaylist", params), "createPlaylist", {},
      RefusalMeaning::NoPlaylistSupport);
}

auto
SubsonicBackend::rename_playlist(const QString &id, const QString &new_name) -> void
{
  if (!connected) return;
  run_playlist_mutation(
      client->url("updatePlaylist", {{"playlistId", id}, {"name", new_name}}),
      "updatePlaylist(rename)", id);
}

auto
SubsonicBackend::delete_playlist(const QString &id) -> void
{
  if (!connected) return;
  run_playlist_mutation(client->url("deletePlaylist", {{"id", id}}),
      "deletePlaylist", id);
}

auto
SubsonicBackend::add_to_playlist(const QString &id, const QList<song> &songs) -> void
{
  if (!connected) return;

  QList<QPair<QString, QString>> params{{"playlistId", id}};
  for (const auto &s : songs)
    params.append({"songIdToAdd", s.uri});

  run_playlist_mutation(client->url("updatePlaylist", params),
      "updatePlaylist(add)", id);
}

auto
SubsonicBackend::remove_from_playlist(const QString &id, const QList<int> &positions) -> void
{
  if (!connected) return;

  // Indexes are 0-based against the current contents and computed by the
  // manager; one call carries them all (repeated songIndexToRemove).
  QList<QPair<QString, QString>> params{{"playlistId", id}};
  for (int pos : positions)
    params.append({"songIndexToRemove", QString::number(pos)});

  run_playlist_mutation(client->url("updatePlaylist", params),
      "updatePlaylist(remove)", id);
}

auto
SubsonicBackend::rearrange_playlist(const QString &id, int target,
    const QList<int> &moved) -> void
{
  if (!connected) return;

  // Subsonic has no native move: reorder is a full-contents replace via
  // createPlaylist?playlistId. That needs the current ordered id list, which we
  // only have if this playlist's songs were fetched. On a cache miss, do NOT
  // send an empty songId list — that would wipe the playlist server-side.
  auto contents = playlist_contents.value(id);
  const int n = contents.size();
  if (n == 0)
    {
      spdlog::warn("SubsonicBackend: rearrange_playlist [{}] with no cached "
                   "contents — refusing to reorder (would clear the playlist)",
          id);
      return;
    }

  // Build the target permutation of 0..n-1 (extended to include `target` when it
  // is the end-of-list insertion point n, then filtered back out) and apply it.
  const uint32_t hi = std::max<uint32_t>(n - 1, static_cast<uint32_t>(target));
  std::vector<uint32_t> input;
  for (uint32_t i = 0; i <= hi; ++i)
    input.push_back(i);
  std::vector<uint32_t> indexes;
  for (int m : moved)
    if (m >= 0 && m < n)
      indexes.push_back(static_cast<uint32_t>(m));

  auto order = reorder::get_output_order(input, static_cast<uint32_t>(target), indexes);

  QList<QPair<QString, QString>> params{{"playlistId", id}};
  for (uint32_t idx : order)
    if (idx < static_cast<uint32_t>(n)) // drop the virtual end anchor (idx == n)
      params.append({"songId", contents[static_cast<int>(idx)].uri});

  run_playlist_mutation(client->url("createPlaylist", params),
      "createPlaylist(reorder)", id);
}

// ---------------------------------------------------------------------------
// Engine event slots
// ---------------------------------------------------------------------------

auto
SubsonicBackend::engine_state_changed(AudioEngine::State /*state*/) -> void
{
  // The payload is the pipeline's own state; emit_playback_state re-reads
  // engine->state(), which resolves a mid-flight pipeline to what the user
  // asked for rather than to the PAUSED it necessarily sits in. That is the
  // answer we want to show, so the payload is deliberately not used here.
  emit_playback_state();
}

auto
SubsonicBackend::locate_in_queue(const QUrl &url) const -> int
{
  // A local file has no query to carry the id, so the cache is asked which
  // track it handed that path out for. Without this a cached track falls
  // through to the positional advance, which is the weaker answer.
  QString id = QUrlQuery(url).queryItemValue("id");
  if (id.isEmpty() && cache) id = cache->id_for_local(url);

  if (id.isEmpty() || queue.isEmpty())
    return -1;

  // Search forward from the current position, wrapping once, so a duplicate
  // track resolves to the next copy ahead rather than the one we're leaving.
  const int n = queue.size();
  const int base = ((queue_pos % n) + n) % n;
  for (int off = 1; off <= n; ++off)
    {
      const int i = (base + off) % n;
      if (queue[i].uri == id)
        return i;
    }
  return -1;
}

auto
SubsonicBackend::engine_track_changed(const QUrl &uri) -> void
{
  // Gapless transition: the engine is now playing the track committed back at
  // about-to-finish. Identify it by the id the engine reports as now-playing
  // rather than re-incrementing queue_pos — the queue may have been mutated in
  // the ~10 s between about-to-finish and STREAM_START (A3).
  if (single)
    {
      // Looping same track — queue_pos unchanged, next URL already set.
      if (queue_pos >= 0 && queue_pos < queue.size())
        emit current_song_changed(queue[queue_pos]);
      emit_playback_state();
      return;
    }

  const int located = locate_in_queue(uri);
  if (located >= 0)
    queue_pos = located;
  else
    {
      // Nothing identified the URL, or the track it names is gone from the
      // queue — fall back to a positional advance.
      if (queue_pos + 1 < queue.size())
        queue_pos++;
      else if (repeat)
        queue_pos = 0;
    }

  if (queue_pos >= 0 && queue_pos < queue.size())
    emit current_song_changed(queue[queue_pos]);

  update_engine_next_url();
  emit_playback_state();
}

auto
SubsonicBackend::engine_track_finished() -> void
{
  // EOS with no gapless successor.
  if (single)
    {
      // single without repeat: just stop
    }
  else if (queue_pos + 1 < queue.size())
    {
      queue_pos++;
      play_current();
      return;
    }
  else if (repeat && !queue.isEmpty())
    {
      queue_pos = 0;
      play_current();
      return;
    }

  emit_playback_state();
}

// ---------------------------------------------------------------------------
// emit_playback_state
// ---------------------------------------------------------------------------

auto
SubsonicBackend::emit_playback_state() -> void
{
  PlaybackState ps;

  switch (engine->state())
    {
    case AudioEngine::State::Playing:
      ps.state = PlayState::Playing;
      break;
    case AudioEngine::State::Paused:
      ps.state = PlayState::Paused;
      break;
    default:
      ps.state = PlayState::Stopped;
      break;
    }

  switch (engine->busy())
    {
    case AudioEngine::Busy::Preroll:
      ps.busy = PlayBusy::Loading;
      break;
    case AudioEngine::Busy::Seek:
      ps.busy = PlayBusy::Seeking;
      break;
    default:
      ps.busy = PlayBusy::None;
      break;
    }

  // A stopped engine still remembers where it was; reporting that would leave
  // the progress bar pinned at the end of a queue that has played out.
  if (restore_position_ms > 0)
    ps.elapsed_ms = restore_position_ms;
  else if (ps.state == PlayState::Stopped)
    ps.elapsed_ms = 0;
  else
    ps.elapsed_ms = engine->position_ms();
  ps.total_ms = engine->duration_ms() > 0 ? engine->duration_ms()
                                          : (queue_pos >= 0 && queue_pos < queue.size()
                                                    ? queue[queue_pos].duration
                                                    : 0);
  ps.volume = engine->volume();
  ps.repeat = repeat;
  ps.shuffle = shuffle;
  ps.single = single;
  ps.queue_pos = queue_pos;

  emit playback_state_changed(ps);
}
