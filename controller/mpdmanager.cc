#include "mpdmanager.hh"

#include "reorder.hh"

#include <QDateTime>
#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>

#include <optional>

// ---------------------------------------------------------------------------
// dispatch — cross-thread call to QMpdClient via member-function pointer
// ---------------------------------------------------------------------------

template<typename Func, typename... Args>
  requires std::invocable<Func, QMpdClient *, Args...>
auto
MpdManager::dispatch(Func func, Args &&...args) -> void
{
  auto *c = client;
  // Pack init-capture moves rvalue args into the lambda instead of re-copying
  // ([=] would copy even arguments that were moved into dispatch).
  QMetaObject::invokeMethod(c,
      [c, func, ... args = std::forward<Args>(args)] { (c->*func)(args...); });
}

// ---------------------------------------------------------------------------
// File-local tag helpers
// ---------------------------------------------------------------------------

static auto
get_tag(const mpd::tags_t &tags, const mpd::tag_type &type) -> std::optional<std::string>
{
  if (tags.contains(type))
    return tags.at(type);
  return {};
}

static auto
get_preferred_tag_value(
    const mpd::tags_t &tags,
    const std::vector<mpd::tag_type> &preferred_tags,
    const std::string &fallback) -> std::string
{
  for (const auto &tag : preferred_tags)
    if (auto val = get_tag(tags, tag))
      return *val;
  return fallback;
}

static auto
get_canonical_album(const mpd::tags_t &tags) -> QString
{
  return QString::fromStdString(
      get_preferred_tag_value(tags, {MPD_TAG_ALBUM}, "Unknown Album"));
}

static auto
get_canonical_artist(const mpd::tags_t &tags) -> QString
{
  return QString::fromStdString(
      get_preferred_tag_value(tags,
          {MPD_TAG_ALBUM_ARTIST, MPD_TAG_ARTIST}, "Unknown Artist"));
}

static auto
get_sort_artist(const mpd::tags_t &tags) -> QString
{
  auto sort_tags = {
      MPD_TAG_ALBUM_ARTIST_SORT,
      MPD_TAG_ALBUM_ARTIST,
      MPD_TAG_ARTIST_SORT,
      MPD_TAG_ARTIST,
  };
  return QString::fromStdString(
      get_preferred_tag_value(tags, sort_tags, "Artist, Unknown"));
}

static const QRegularExpression year_rx("(\\d{4})");

static auto
extract_year(QString date) -> QString
{
  auto match = year_rx.match(date);
  if (match.hasMatch())
    return match.captured(1);

  spdlog::debug("couldn't parse year from [{}]", date);
  return date;
}

// Display year: prefer the original (recording) date so old material reads as
// its true year. Used only for what the UI shows, never for album identity.
static auto
get_canonical_date(const mpd::tags_t &tags) -> QString
{
  return extract_year(QString::fromStdString(
      get_preferred_tag_value(tags, {MPD_TAG_ORIGINAL_DATE, MPD_TAG_DATE}, "2009")));
}

// Album-identity year: the *release* date only, never ORIGINAL_DATE. Original
// date is per-track recording provenance and varies within a single release
// (compilations, box sets, reissues), which would fragment the album. Tracks
// with no DATE share the constant fallback so they still group together.
static auto
get_key_date(const mpd::tags_t &tags) -> QString
{
  return extract_year(QString::fromStdString(
      get_preferred_tag_value(tags, {MPD_TAG_DATE}, "2009")));
}

template<typename T>
static auto
parse_number(const std::string &tag, T fallback) -> T
{
  if (tag.empty()) return fallback;
  try
    {
      return static_cast<T>(std::stoi(tag));
    }
  catch (const std::invalid_argument &e)
    {
      spdlog::warn("could not parse number from [{}]: {}", tag, e.what());
      return fallback;
    }
}

// ---------------------------------------------------------------------------
// mpd::status → PlaybackState
// ---------------------------------------------------------------------------

static auto
to_playback_state(const mpd::status &s) -> PlaybackState
{
  PlaybackState ps;

  switch (s.state)
    {
    case MPD_STATE_PLAY:
      ps.state = PlayState::Playing;
      break;
    case MPD_STATE_PAUSE:
      ps.state = PlayState::Paused;
      break;
    default:
      ps.state = PlayState::Stopped;
      break;
    }

  ps.elapsed_ms = s.elapsed_ms;
  ps.total_ms = s.total_time * 1000;
  ps.volume = s.volume;
  ps.repeat = s.repeat;
  ps.shuffle = s.random;
  ps.single = s.single;
  ps.queue_pos = s.song_pos;

  if (s.audio_format)
    {
      ps.sample_rate_hz = s.audio_format->sample_rate;
      ps.bits = s.audio_format->bits;
      ps.channels = s.audio_format->channels;
    }
  ps.bitrate_kbps = s.kbit_rate;

  return ps;
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

MpdManager::MpdManager(std::shared_ptr<LibraryManager> libman, QString host, uint16_t port)
  : mpd_host{std::move(host)}
  , mpd_port{port}
  , libman{libman}
{
  client = new QMpdClient;
  client->moveToThread(&worker_thread);

  // QMpdClient is deleted by the worker thread on shutdown
  QObject::connect(&worker_thread, &QThread::finished,
      client, &QObject::deleteLater);

  // Wire signals from QMpdClient (worker) → MpdManager (main thread).
  // Qt::AutoConnection → Qt::QueuedConnection because the objects are in
  // different threads.
  QObject::connect(client, &QMpdClient::connected,
      this, &MpdManager::on_connected);
  QObject::connect(client, &QMpdClient::disconnected,
      this, &MpdManager::on_disconnected);
  QObject::connect(client, &QMpdClient::error,
      this, &MpdManager::on_error);
  QObject::connect(client, &QMpdClient::status_received,
      this, &MpdManager::on_status);
  QObject::connect(client, &QMpdClient::queue_received,
      this, &MpdManager::on_queue);
  QObject::connect(client, &QMpdClient::current_song_received,
      this, &MpdManager::on_current_song);
  QObject::connect(client, &QMpdClient::all_songs_received,
      this, &MpdManager::on_all_songs);
  QObject::connect(client, &QMpdClient::favorites_received,
      this, &MpdManager::on_favorites);
  QObject::connect(client, &QMpdClient::favorites_unsupported,
      this, [this] { stickers_ok = false; });
  QObject::connect(client, &QMpdClient::playlists_received,
      this, &MpdManager::on_playlists);
  QObject::connect(client, &QMpdClient::playlist_songs_received,
      this, &MpdManager::on_playlist_songs);
  QObject::connect(client, &QMpdClient::stored_playlists_changed,
      this, [this] { emit playlists_changed({}); });
  QObject::connect(client, &QMpdClient::idle_event,
      this, &MpdManager::handle_idle);
  // Forward album_art_received from worker to Backend signal
  QObject::connect(client, &QMpdClient::album_art_received,
      this, &Backend::album_art_received);

  fav_refresh_timer.setSingleShot(true);
  fav_refresh_timer.setInterval(250);
  QObject::connect(&fav_refresh_timer, &QTimer::timeout,
      this, &Backend::favorites_stale);

  playlist_refresh_timer.setSingleShot(true);
  playlist_refresh_timer.setInterval(250);
  QObject::connect(&playlist_refresh_timer, &QTimer::timeout,
      this, [this] { emit playlists_changed({}); });

  worker_thread.start();
}

MpdManager::~MpdManager()
{
  worker_thread.quit();
  worker_thread.wait();
}

// ---------------------------------------------------------------------------
// Lifecycle (public slots — main thread)
// ---------------------------------------------------------------------------

auto
MpdManager::connect_to_server() -> bool
{
  if (connected) return true;
  dispatch(&QMpdClient::do_connect, mpd_host, mpd_port);
  return true;
}

auto
MpdManager::disconnect_from_server() -> void
{
  if (!connected) return;
  dispatch(&QMpdClient::do_disconnect);
}

// ---------------------------------------------------------------------------
// Private slots — receive results from QMpdClient (queued, main thread)
// ---------------------------------------------------------------------------

auto
MpdManager::on_connected() -> void
{
  connected = true;
  stickers_ok = true; // re-probed by the favorites read this connection triggers

  // Enable playback controls immediately (before library loads). Also what the
  // favorites owner reads on, so this has to precede the fetches below.
  emit connection_update(true);

  dispatch(&QMpdClient::fetch_status);
  dispatch(&QMpdClient::fetch_queue);
  dispatch(&QMpdClient::fetch_current_song);
  dispatch(&QMpdClient::fetch_all_songs);
}

auto
MpdManager::on_disconnected() -> void
{
  connected = false;
  emit connection_update(false);
}

auto
MpdManager::on_error(mpd::error err) -> void
{
  // Fatal errors also trigger do_disconnect() → on_disconnected() which updates
  // connection state.  Non-fatal errors (ACK responses) are just logged here.
  spdlog::error("MPD error [{}]: {}", static_cast<int>(err.code), err.msg);
  emit error(QString::fromStdString(err.msg));
}

auto
MpdManager::handle_idle(unsigned int idle) -> void
{
  if (idle & MPD_IDLE_QUEUE)
    {
      spdlog::debug("MPD_IDLE_QUEUE");
      dispatch(&QMpdClient::fetch_status);
      dispatch(&QMpdClient::fetch_queue);
    }

  if (idle & MPD_IDLE_PLAYER)
    {
      spdlog::debug("MPD_IDLE_PLAYER");
      dispatch(&QMpdClient::fetch_status);
      dispatch(&QMpdClient::fetch_current_song);
    }

  if (idle & MPD_IDLE_MIXER)
    {
      spdlog::trace("MPD_IDLE_MIXER");
      dispatch(&QMpdClient::fetch_status);
    }

  if (idle & MPD_IDLE_OPTIONS)
    {
      spdlog::debug("MPD_IDLE_OPTIONS");
      dispatch(&QMpdClient::fetch_status);
    }

  if (idle & MPD_IDLE_UPDATE)
    {
      spdlog::debug("MPD_IDLE_UPDATE");
      dispatch(&QMpdClient::fetch_status);
    }

  if (idle & MPD_IDLE_DATABASE)
    {
      spdlog::debug("MPD_IDLE_DATABASE");
      dispatch(&QMpdClient::fetch_all_songs);
    }

  if (idle & MPD_IDLE_STICKER)
    {
      // Sticker idle names no song, so re-pull the whole set (debounced).
      spdlog::debug("MPD_IDLE_STICKER");
      fav_refresh_timer.start();
    }

  if (idle & MPD_IDLE_STORED_PLAYLIST)
    {
      // Names no playlist; consumers refetch on playlists_changed (debounced).
      spdlog::debug("MPD_IDLE_STORED_PLAYLIST");
      playlist_refresh_timer.start();
    }
}

auto
MpdManager::on_status(mpd::status status) -> void
{
  last_status = status;

  bool update_just_finished = db_updating && (status.update_id == 0);

  if (status.update_id != 0)
    {
      db_updating = true;
      emit library_refresh_active(true);
    }
  else if (update_just_finished)
    {
      db_updating = false;
      pending_db_finish = true;
      // Kick off a library refresh. on_all_songs emits library_refresh_active(false)
      // only after LibraryManager is repopulated, so the spinner never clears while
      // the cache still holds pre-scan data.
      dispatch(&QMpdClient::fetch_all_songs);
    }

  emit playback_state_changed(to_playback_state(status));
}

auto
MpdManager::on_queue(std::vector<mpd::song> songs) -> void
{
  QList<song> result;
  result.reserve(static_cast<int>(songs.size()));
  for (const auto &s : songs)
    result.append(mpdsong_to_song(s));
  emit queue_changed(result);
}

auto
MpdManager::on_current_song(std::optional<mpd::song> song) -> void
{
  if (song)
    emit current_song_changed(mpdsong_to_song(*song));
}

auto
MpdManager::on_all_songs(std::vector<mpd::song> songs) -> void
{
  libman->clear();

  for (const auto &mpd_song : songs)
    {
      const auto &tags = mpd_song.tags;
      auto artist = get_canonical_artist(tags);
      auto date = get_canonical_date(tags);
      auto name = get_canonical_album(tags);
      auto sort_artist = get_sort_artist(tags);
      auto album_hash = get_album_hash(mpd_song);

      if (!libman->has_album(album_hash))
        {
          auto uri = QString::fromStdString(mpd_song.uri);
          auto added_ts = mpd_song.added ? mpd_song.added : mpd_song.last_modified;
          auto mod = QDateTime::fromMSecsSinceEpoch(added_ts * 1000);
          libman->add_album(album_hash,
              {uri, album_hash, name, artist, date, sort_artist, mod});
        }

      libman->add_song(album_hash, mpdsong_to_song(mpd_song));
    }

  spdlog::info("MpdManager: library refresh complete, loaded {} albums ({} songs)",
      libman->get_albums().size(), songs.size());

  // Notify UI that the library cache is ready
  emit library_changed();
  // MPD hashes each song's album from its own tags, so a library load always
  // re-establishes the mapping standalone songs resolve through.
  emit album_mapping_changed();

  // If a db update just finished, emit the deferred "update finished" signal
  // now that LibraryManager is fresh.
  if (pending_db_finish)
    {
      pending_db_finish = false;
      emit library_refresh_active(false);
    }
}

auto
MpdManager::on_favorites(QSet<QString> uris) -> void
{
  emit favorites_loaded(uris);

  if (!libman)
    {
      emit favorite_songs_loaded({});
      return;
    }

  QHash<QString, song> by_uri;
  for (const auto &a : libman->get_albums())
    for (const auto &s : libman->get_songs(a.album_hash))
      if (uris.contains(s.uri))
        by_uri.insert(s.uri, s);

  QList<song> favs;
  favs.reserve(by_uri.size());
  for (const auto &uri : uris)
    if (auto it = by_uri.constFind(uri); it != by_uri.constEnd())
      favs.append(*it);

  emit favorite_songs_loaded(favs);
}

auto
MpdManager::on_playlists(std::vector<mpd::playlist_summary> playlists) -> void
{
  QList<playlist_info> lists;
  lists.reserve(static_cast<int>(playlists.size()));
  for (const auto &p : playlists)
    {
      auto name = QString::fromStdString(p.name);
      auto mod = p.last_modified
                     ? QDateTime::fromSecsSinceEpoch(p.last_modified)
                     : QDateTime{};
      lists.append({name, name, -1, -1, mod});
    }
  emit playlists_loaded(lists);
}

auto
MpdManager::on_playlist_songs(QString name, std::vector<mpd::song> songs) -> void
{
  QList<song> result;
  result.reserve(static_cast<int>(songs.size()));
  for (const auto &s : songs)
    result.append(mpdsong_to_song(s));
  emit playlist_songs_loaded(name, result);
}

// ---------------------------------------------------------------------------
// get_album_hash / mpdsong_to_song
// ---------------------------------------------------------------------------

auto
MpdManager::get_album_hash(const mpd::song &song) -> QString
{
  const auto &tags = song.tags;

  // Lowercased so the same release yields the same key via MPD or Subsonic —
  // both may otherwise disagree on case for an identical MBID tag/response.
  if (auto brainz_id = get_tag(tags, MPD_TAG_MUSICBRAINZ_ALBUMID))
    return QString::fromStdString(*brainz_id).toLower();

  spdlog::trace("generating hash, no musicbrainz id: [{}]", song.uri);
  auto artist = get_canonical_artist(tags);
  auto date = get_key_date(tags);
  auto name = get_canonical_album(tags);

  auto key = QString("%1/%2/%3").arg(artist, date, name);
  auto hash = QString(
      QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex());

  spdlog::trace("hash: [{}]", hash);
  return hash;
}

auto
MpdManager::mpdsong_to_song(const mpd::song &mpdsong, QString album_hash) -> song
{
  const auto &tags = mpdsong.tags;
  auto uri = QString::fromStdString(mpdsong.uri);
  auto title = QString::fromStdString(get_tag(tags, MPD_TAG_TITLE).value_or(""));
  auto artist = QString::fromStdString(get_tag(tags, MPD_TAG_ARTIST).value_or(""));

  uint32_t track_number = parse_number<uint32_t>(get_tag(tags, MPD_TAG_TRACK).value_or(""), 0);
  uint32_t disc_number = parse_number<uint32_t>(get_tag(tags, MPD_TAG_DISC).value_or(""), 1);
  uint32_t duration = mpdsong.duration_ms;

  if (album_hash.isEmpty())
    album_hash = get_album_hash(mpdsong);

  return {uri, title, artist, track_number, disc_number, duration, album_hash};
}

// ---------------------------------------------------------------------------
// Public cache reads (no MPD I/O)
// ---------------------------------------------------------------------------

auto
MpdManager::get_albums() -> QList<album>
{
  return libman->get_albums();
}

auto
MpdManager::fetch_songs(const album &a, std::function<void(const QList<song> &)> cb) -> void
{
  if (cb)
    cb(libman->get_songs(a.album_hash));
}

// ---------------------------------------------------------------------------
// Playback commands — dispatch to worker thread
// ---------------------------------------------------------------------------

auto
MpdManager::play() -> void
{
  dispatch(&QMpdClient::play);
}
auto
MpdManager::pause() -> void
{
  dispatch(&QMpdClient::pause, true);
}
auto
MpdManager::stop() -> void
{
  dispatch(&QMpdClient::stop);
}
auto
MpdManager::prev() -> void
{
  dispatch(&QMpdClient::prev);
}
auto
MpdManager::next() -> void
{
  dispatch(&QMpdClient::next);
}
auto
MpdManager::seek(uint32_t ms) -> void
{
  // QMpdClient::seek takes seconds (float), convert from milliseconds
  dispatch(&QMpdClient::seek, static_cast<float>(ms) / 1000.0f);
}
auto
MpdManager::set_volume(int vol) -> void
{
  dispatch(&QMpdClient::set_volume, static_cast<uint>(vol));
}
auto
MpdManager::play_pos(uint32_t pos) -> void
{
  dispatch(&QMpdClient::play_pos, pos);
}
auto
MpdManager::set_repeat(bool repeat_enabled, bool single_enabled) -> void
{
  dispatch(&QMpdClient::set_repeat, repeat_enabled);
  dispatch(&QMpdClient::set_single, single_enabled);
}
auto
MpdManager::set_shuffle(bool shuffle) -> void
{
  dispatch(&QMpdClient::set_random, shuffle);
}
auto
MpdManager::refresh_library() -> void
{
  dispatch(&QMpdClient::trigger_db_update);
}
auto
MpdManager::update_db() -> void
{
  refresh_library();
}
auto
MpdManager::fetch_album_art(const QString &uri) -> void
{
  dispatch(&QMpdClient::fetch_album_art, uri);
}
auto
MpdManager::set_favorite(const QString &uri, bool fav) -> void
{
  dispatch(&QMpdClient::set_favorite, uri, fav);
}
auto
MpdManager::fetch_favorites() -> void
{
  dispatch(&QMpdClient::fetch_favorites);
}

// ---------------------------------------------------------------------------
// Playlist commands
// ---------------------------------------------------------------------------

static auto
song_uris(const QList<song> &songs) -> QList<QString>
{
  QList<QString> uris;
  uris.reserve(songs.size());
  for (const auto &s : songs)
    uris.append(s.uri);
  return uris;
}

auto
MpdManager::fetch_playlists() -> void
{
  dispatch(&QMpdClient::fetch_playlists);
}
auto
MpdManager::fetch_playlist_songs(const QString &id) -> void
{
  dispatch(&QMpdClient::fetch_playlist_songs, id);
}
auto
MpdManager::create_playlist(const QString &name, const QList<song> &songs) -> void
{
  dispatch(&QMpdClient::create_playlist, name, song_uris(songs));
}
auto
MpdManager::rename_playlist(const QString &id, const QString &new_name) -> void
{
  dispatch(&QMpdClient::rename_playlist, id, new_name);
}
auto
MpdManager::delete_playlist(const QString &id) -> void
{
  dispatch(&QMpdClient::delete_playlist, id);
}
auto
MpdManager::add_to_playlist(const QString &id, const QList<song> &songs) -> void
{
  dispatch(&QMpdClient::add_to_playlist, id, song_uris(songs));
}
auto
MpdManager::remove_from_playlist(const QString &id, const QList<int> &positions) -> void
{
  dispatch(&QMpdClient::remove_from_playlist, id, positions);
}
auto
MpdManager::rearrange_playlist(const QString &id, int target,
    const QList<int> &moved) -> void
{
  std::vector<uint32_t> indexes;
  for (int m : moved)
    indexes.push_back(static_cast<uint32_t>(m));

  auto moves = reorder::get_moves(static_cast<uint32_t>(target), indexes);
  if (moves.empty())
    return;
  dispatch(&QMpdClient::rearrange_playlist, id, moves);
}

// ---------------------------------------------------------------------------
// Queue commands
// ---------------------------------------------------------------------------

auto
MpdManager::insert_queue(const QList<song> &songs, uint32_t pos) -> void
{
  QList<QString> uris;
  for (const auto &s : songs)
    uris.append(s.uri);

  insert_queue(uris, pos);
}

auto
MpdManager::insert_queue(const QList<QString> &uris, uint32_t pos) -> void
{
  dispatch(&QMpdClient::insert_queue_at, static_cast<uint32_t>(pos), QStringList{uris});
}

auto
MpdManager::replace_queue(const QList<song> &songs, uint32_t playpos) -> void
{
  QStringList uris;
  for (const auto &s : songs)
    uris.append(s.uri);

  dispatch(&QMpdClient::replace_queue_and_play, uris, playpos);
}

auto
MpdManager::append_queue(const QList<song> &songs) -> void
{
  QStringList uris;
  for (const auto &s : songs)
    uris.append(s.uri);

  dispatch(&QMpdClient::append_queue, uris);
}

auto
MpdManager::remove_from_queue(const QList<QModelIndex> &selected_indexes) -> void
{
  std::vector<uint32_t> indexes;
  for (const auto &idx : selected_indexes)
    indexes.push_back(static_cast<uint32_t>(idx.row()));

  dispatch(&QMpdClient::remove_from_queue, indexes);
}

auto
MpdManager::rearrange_queue(uint32_t target_idx, std::vector<uint32_t> indexes) -> void
{
  spdlog::debug("MpdManager::rearrange_queue({}, {})", target_idx, indexes);
  auto swaps = reorder::get_swaps(target_idx, indexes);
  spdlog::debug("reorder::get_swaps(...) = {}", swaps);
  dispatch(&QMpdClient::rearrange_queue, swaps);
}
