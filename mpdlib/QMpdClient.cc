#include "QMpdClient.hh"
#include "chunk_reader.hh"

#include <algorithm>
#include <spdlog/spdlog.h>

#define ALBUM_ART_CHUNK_SZ 524288

// libmpdclient's timeout covers the connect as well as every blocking read that
// follows, and its default is 30s. do_connect opens two connections back to
// back, so a host that swallows SYNs costs a full minute before anything reports
// back — long enough that the UI's connecting state looks hung. Connect under a
// tighter bound, then hand each connection the library default for the reads.
static constexpr unsigned CONNECT_TIMEOUT_MS = 10'000;
static constexpr unsigned SESSION_TIMEOUT_MS = 30'000;

QMpdClient::QMpdClient(QObject *parent)
  : QObject{parent}
{
  // Register custom types used in cross-thread queued connections.
  qRegisterMetaType<mpd::error>();
  qRegisterMetaType<mpd::status>();
  qRegisterMetaType<std::vector<mpd::song>>();
  qRegisterMetaType<std::optional<mpd::song>>();
  qRegisterMetaType<QSet<QString>>();
  qRegisterMetaType<std::vector<mpd::playlist_summary>>();
}

QMpdClient::~QMpdClient()
{
  if (cmd_conn || idle_conn)
    do_disconnect();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

auto
QMpdClient::do_connect(QString host, uint16_t port) -> void
{
  if (cmd_conn || idle_conn)
    do_disconnect();

  // Keep the UTF-8 bytes alive for the duration of do_connect: a temporary from
  // host.toUtf8() would be freed at the end of the statement, leaving addr
  // dangling when mpd_connection_new() reads it (intermittent connect failures).
  const QByteArray host_bytes = host.toUtf8();
  const char *addr = host.isEmpty() ? nullptr : host_bytes.constData();

  // --- command connection ---
  cmd_conn = mpd_connection_new(addr, port, CONNECT_TIMEOUT_MS);
  if (!cmd_conn)
    {
      spdlog::error("QMpdClient: mpd_connection_new returned null (cmd)");
      emit error(mpd::error{MPD_ERROR_OOM, "mpd_connection_new returned null", true});
      emit disconnected();
      return;
    }

  if (mpd_connection_get_error(cmd_conn) != MPD_ERROR_SUCCESS)
    {
      auto msg = std::string(mpd_connection_get_error_message(cmd_conn));
      auto code = mpd_connection_get_error(cmd_conn);
      auto fatal = !mpd_connection_clear_error(cmd_conn);
      spdlog::error("QMpdClient: command connection error: {}", msg);
      emit error(mpd::error{code, msg, fatal});

      mpd_connection_free(cmd_conn);
      cmd_conn = nullptr;
      emit disconnected();
      return;
    }

  mpd_connection_set_timeout(cmd_conn, SESSION_TIMEOUT_MS);
  mpd_run_binarylimit(cmd_conn, ALBUM_ART_CHUNK_SZ);
  mpd_connection_set_keepalive(cmd_conn, true);

  // --- idle connection ---
  idle_conn = mpd_connection_new(addr, port, CONNECT_TIMEOUT_MS);
  if (!idle_conn)
    {
      spdlog::error("QMpdClient: mpd_connection_new returned null (idle)");
      emit error(mpd::error{MPD_ERROR_OOM, "mpd_connection_new returned null", true});

      mpd_connection_free(cmd_conn);
      cmd_conn = nullptr;
      emit disconnected();
      return;
    }

  if (mpd_connection_get_error(idle_conn) != MPD_ERROR_SUCCESS)
    {
      auto msg = std::string(mpd_connection_get_error_message(idle_conn));
      auto code = mpd_connection_get_error(idle_conn);
      auto fatal = !mpd_connection_clear_error(idle_conn);
      spdlog::error("QMpdClient: idle connection error: {}", msg);
      emit error(mpd::error{code, msg, fatal});

      mpd_connection_free(idle_conn);
      idle_conn = nullptr;

      mpd_connection_free(cmd_conn);
      cmd_conn = nullptr;
      emit disconnected();
      return;
    }

  mpd_connection_set_timeout(idle_conn, SESSION_TIMEOUT_MS);
  mpd_send_idle(idle_conn);

  int fd = mpd_connection_get_fd(idle_conn);
  idle_notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
  connect(idle_notifier.get(), &QSocketNotifier::activated,
      this, &QMpdClient::on_idle_readable);
  idle_notifier->setEnabled(true);

  // Create timer here (on the worker thread) so its thread affinity is correct.
  if (!keepalive_timer)
    {
      keepalive_timer = std::make_unique<QTimer>();
      keepalive_timer->setInterval(45'000);
      connect(keepalive_timer.get(), &QTimer::timeout, this, &QMpdClient::on_keepalive);
    }
  keepalive_timer->start();

  spdlog::info("QMpdClient: connected to {}:{}", host.isEmpty() ? "localhost" : host.toStdString(), port);
  emit connected();
}

auto
QMpdClient::do_disconnect() -> void
{
  if (keepalive_timer) keepalive_timer->stop();

  if (idle_notifier)
    {
      idle_notifier->setEnabled(false);
      idle_notifier.reset();
    }

  if (idle_conn)
    {
      mpd_send_noidle(idle_conn);
      mpd_recv_idle(idle_conn, false);
      mpd_connection_free(idle_conn);
      idle_conn = nullptr;
    }

  if (cmd_conn)
    {
      mpd_connection_free(cmd_conn);
      cmd_conn = nullptr;
    }

  spdlog::info("QMpdClient: disconnected");
  emit disconnected();
}

// ---------------------------------------------------------------------------
// Idle socket — event from MPD
// ---------------------------------------------------------------------------

auto
QMpdClient::on_idle_readable() -> void
{
  auto idle = mpd_recv_idle(idle_conn, false);

  // Re-enter idle immediately so next events aren't missed while we process this one.
  mpd_send_idle(idle_conn);

  if (idle != 0)
    {
      spdlog::debug("QMpdClient: idle_event 0x{:x}", static_cast<unsigned>(idle));
      emit idle_event(static_cast<unsigned int>(idle));
    }
  else
    {
      spdlog::debug("QMpdClient: idle was 0 (socket noise or noidle drain)");
    }
}

auto
QMpdClient::on_keepalive() -> void
{
  if (!cmd_conn) return;

  auto *s = mpd_run_status(cmd_conn);
  if (s) mpd_status_free(s);
  check_cmd_error("keepalive");
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Returns true if no error on cmd_conn.
// On error: logs, emits error(), calls do_disconnect() if fatal.
auto
QMpdClient::check_cmd_error(const char *context) -> bool
{
  if (!cmd_conn) return false;

  auto err = mpd_connection_get_error(cmd_conn);
  if (err == MPD_ERROR_SUCCESS) return true;

  auto msg = std::string(mpd_connection_get_error_message(cmd_conn));
  bool fatal = !mpd_connection_clear_error(cmd_conn);
  spdlog::error("QMpdClient::{}: {} (fatal={})", context, msg, fatal);
  emit error(mpd::error{err, msg, fatal});

  if (fatal) do_disconnect();
  return false;
}

auto
QMpdClient::receive_songs() -> std::vector<mpd::song>
{
  std::vector<mpd::song> songs;
  mpd_song *s = nullptr;

  while ((s = mpd_recv_song(cmd_conn)) != nullptr)
    {
      songs.emplace_back(mpd::song{s});
      mpd_song_free(s);
    }

  return songs;
}

auto
QMpdClient::fetch_binary(decltype(mpd_run_readpicture) *f,
    const std::string &uri) -> QByteArray
{
  if (!cmd_conn) return {};

  auto chunks = mpd::collect_chunks(
      [&](std::size_t offset, void *buf, std::size_t size) -> int64_t {
        return f(cmd_conn, uri.c_str(), offset, buf, size);
      },
      ALBUM_ART_CHUNK_SZ);

  QByteArray bytes(reinterpret_cast<const char *>(chunks.data()),
      static_cast<int>(chunks.size()));
  spdlog::trace("QMpdClient::fetch_binary: {} bytes for [{}]", bytes.size(), uri);
  return bytes;
}

// ---------------------------------------------------------------------------
// Fetch requests
// ---------------------------------------------------------------------------

auto
QMpdClient::fetch_status() -> void
{
  if (!cmd_conn) return;

  auto *s = mpd_run_status(cmd_conn);
  if (!check_cmd_error("fetch_status") || !s) return;

  mpd::status status{s};
  mpd_status_free(s);
  emit status_received(status);
}

auto
QMpdClient::fetch_queue() -> void
{
  if (!cmd_conn)
    {
      emit queue_received({});
      return;
    }

  mpd_send_list_queue_meta(cmd_conn);
  auto songs = receive_songs();

  check_cmd_error("fetch_queue");
  emit queue_received(songs);
}

auto
QMpdClient::fetch_current_song() -> void
{
  if (!cmd_conn)
    {
      emit current_song_received(std::nullopt);
      return;
    }

  auto *s = mpd_run_current_song(cmd_conn);

  if (!check_cmd_error("fetch_current_song")) return;

  if (!s)
    {
      emit current_song_received(std::nullopt);
      return;
    }

  mpd::song song{s};
  mpd_song_free(s);
  emit current_song_received(song);
}

auto
QMpdClient::fetch_all_songs() -> void
{
  std::vector<mpd::song> songs;
  if (cmd_conn
      && mpd_search_db_songs(cmd_conn, false)
      && mpd_search_add_tag_constraint(cmd_conn, MPD_OPERATOR_DEFAULT, MPD_TAG_ALBUM, "")
      && mpd_search_commit(cmd_conn))
    {
      songs = receive_songs();
    }

  check_cmd_error("fetch_all_songs");
  emit all_songs_received(songs);
}

auto
QMpdClient::fetch_album_art(QString uri) -> void
{
  auto std_uri = uri.toStdString();

  // Returns true (and clears) if the current cmd_conn error is a server-side
  // "no such file" — i.e. the file simply has no embedded/directory art.
  // Any other error is left for check_cmd_error to handle.
  auto clear_if_no_art = [this]() -> bool {
    if (!cmd_conn) return false;
    if (mpd_connection_get_error(cmd_conn) == MPD_ERROR_SERVER
        && mpd_connection_get_server_error(cmd_conn) == MPD_SERVER_ERROR_NO_EXIST)
      {
        mpd_connection_clear_error(cmd_conn);
        return true;
      }
    return false;
  };

  auto bytes = fetch_binary(mpd_run_albumart, std_uri);

  if (bytes.isEmpty())
    {
      if (!clear_if_no_art())
        check_cmd_error("fetch_album_art/albumart");
      spdlog::debug("QMpdClient: no albumart for [{}], trying readpicture", std_uri);
      bytes = fetch_binary(mpd_run_readpicture, std_uri);
    }

  if (bytes.isEmpty() && !clear_if_no_art())
    check_cmd_error("fetch_album_art/readpicture");

  emit album_art_received(uri, bytes);
}

auto
QMpdClient::trigger_db_update() -> void
{
  if (!cmd_conn) return;

  mpd_run_update(cmd_conn, nullptr);
  check_cmd_error("trigger_db_update");
}

// ---------------------------------------------------------------------------
// Favorites (stickers)
// ---------------------------------------------------------------------------

auto
QMpdClient::set_favorite(QString uri, bool fav) -> void
{
  if (!cmd_conn) return;

  auto std_uri = uri.toStdString();
  if (fav)
    mpd_run_sticker_set(cmd_conn, "song", std_uri.c_str(), "favorite", "1");
  else
    mpd_run_sticker_delete(cmd_conn, "song", std_uri.c_str(), "favorite");

  check_cmd_error("set_favorite");
}

auto
QMpdClient::fetch_favorites() -> void
{
  if (!cmd_conn)
    {
      emit favorites_received({});
      return;
    }

  // The sticker-find response interleaves "file:" (the song uri) and "sticker:"
  // lines; recv_pair_named discards the sticker pairs and yields each file.
  QSet<QString> uris;
  if (mpd_send_sticker_find(cmd_conn, "song", "", "favorite"))
    {
      mpd_pair *pair = nullptr;
      while ((pair = mpd_recv_pair_named(cmd_conn, "file")) != nullptr)
        {
          uris.insert(QString::fromUtf8(pair->value));
          mpd_return_pair(cmd_conn, pair);
        }
    }

  // A non-fatal server error here means stickers are unavailable (e.g. the
  // sticker database is disabled in mpd.conf). Clear it and gate the UI rather
  // than surfacing a user-facing error toast.
  if (mpd_connection_get_error(cmd_conn) == MPD_ERROR_SERVER
      && mpd_connection_clear_error(cmd_conn))
    {
      spdlog::debug("QMpdClient: stickers unavailable; favorites disabled");
      emit favorites_unsupported();
      emit favorites_received({});
      return;
    }

  check_cmd_error("fetch_favorites");
  emit favorites_received(uris);
}

// ---------------------------------------------------------------------------
// Stored playlists
// ---------------------------------------------------------------------------

auto
QMpdClient::fetch_playlists() -> void
{
  if (!cmd_conn)
    {
      emit playlists_received({});
      return;
    }

  std::vector<mpd::playlist_summary> playlists;
  if (mpd_send_list_playlists(cmd_conn))
    {
      mpd_playlist *p = nullptr;
      while ((p = mpd_recv_playlist(cmd_conn)) != nullptr)
        {
          playlists.emplace_back(mpd::playlist_summary{p});
          mpd_playlist_free(p);
        }
    }

  check_cmd_error("fetch_playlists");
  emit playlists_received(playlists);
}

auto
QMpdClient::fetch_playlist_songs(QString name) -> void
{
  if (!cmd_conn)
    {
      emit playlist_songs_received(name, {});
      return;
    }

  mpd_send_list_playlist_meta(cmd_conn, name.toUtf8().constData());
  auto songs = receive_songs();

  check_cmd_error("fetch_playlist_songs");
  emit playlist_songs_received(name, songs);
}

auto
QMpdClient::playlist_add_all(const QString &name, const QList<QString> &uris,
    const char *context) -> void
{
  if (!cmd_conn) return;
  if (!mpd_command_list_begin(cmd_conn, false)) return;

  const QByteArray name_bytes = name.toUtf8();
  for (const auto &uri : uris)
    mpd_send_playlist_add(cmd_conn, name_bytes.constData(), uri.toUtf8().constData());

  mpd_command_list_end(cmd_conn);
  mpd_response_finish(cmd_conn);

  if (check_cmd_error(context))
    emit stored_playlists_changed();
}

auto
QMpdClient::create_playlist(QString name, QList<QString> uris) -> void
{
  playlist_add_all(name, uris, "create_playlist");
}

auto
QMpdClient::add_to_playlist(QString name, QList<QString> uris) -> void
{
  playlist_add_all(name, uris, "add_to_playlist");
}

auto
QMpdClient::rename_playlist(QString from, QString to) -> void
{
  if (!cmd_conn) return;

  mpd_run_rename(cmd_conn, from.toUtf8().constData(), to.toUtf8().constData());
  if (check_cmd_error("rename_playlist"))
    emit stored_playlists_changed();
}

auto
QMpdClient::delete_playlist(QString name) -> void
{
  if (!cmd_conn) return;

  mpd_run_rm(cmd_conn, name.toUtf8().constData());
  if (check_cmd_error("delete_playlist"))
    emit stored_playlists_changed();
}

auto
QMpdClient::remove_from_playlist(QString name, QList<int> positions) -> void
{
  if (!cmd_conn) return;
  if (!mpd_command_list_begin(cmd_conn, false)) return;

  // Sort descending so later positions don't shift earlier ones
  std::sort(positions.begin(), positions.end(), std::greater<int>());

  const QByteArray name_bytes = name.toUtf8();
  for (auto pos : positions)
    mpd_send_playlist_delete(cmd_conn, name_bytes.constData(),
        static_cast<unsigned>(pos));

  mpd_command_list_end(cmd_conn);
  mpd_response_finish(cmd_conn);

  if (check_cmd_error("remove_from_playlist"))
    emit stored_playlists_changed();
}

auto
QMpdClient::rearrange_playlist(QString name,
    std::vector<std::pair<uint32_t, uint32_t>> moves) -> void
{
  if (!cmd_conn || moves.empty()) return;
  if (!mpd_command_list_begin(cmd_conn, false)) return;

  const QByteArray name_bytes = name.toUtf8();
  for (const auto &[from, to] : moves)
    mpd_send_playlist_move(cmd_conn, name_bytes.constData(), from, to);

  mpd_command_list_end(cmd_conn);
  mpd_response_finish(cmd_conn);

  if (check_cmd_error("rearrange_playlist"))
    emit stored_playlists_changed();
}

// ---------------------------------------------------------------------------
// Playback commands
// ---------------------------------------------------------------------------

auto
QMpdClient::play() -> void
{
  if (!cmd_conn) return;

  mpd_run_play(cmd_conn);
  check_cmd_error("play");
}

auto
QMpdClient::stop() -> void
{
  if (!cmd_conn) return;

  mpd_run_stop(cmd_conn);
  check_cmd_error("stop");
}

auto
QMpdClient::pause(bool mode) -> void
{
  if (!cmd_conn) return;

  mpd_run_pause(cmd_conn, mode);
  check_cmd_error("pause");
}

auto
QMpdClient::next() -> void
{
  if (!cmd_conn) return;

  mpd_run_next(cmd_conn);
  check_cmd_error("next");
}

auto
QMpdClient::prev() -> void
{
  if (!cmd_conn) return;

  mpd_run_previous(cmd_conn);
  check_cmd_error("prev");
}

auto
QMpdClient::seek(float pos) -> void
{
  if (!cmd_conn) return;

  mpd_run_seek_current(cmd_conn, pos, false);
  check_cmd_error("seek");
}

auto
QMpdClient::play_pos(uint pos) -> void
{
  if (!cmd_conn) return;

  mpd_run_play_pos(cmd_conn, pos);
  check_cmd_error("play_pos");
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

auto
QMpdClient::set_volume(uint vol) -> void
{
  if (!cmd_conn) return;

  mpd_run_set_volume(cmd_conn, vol);
  check_cmd_error("set_volume");
}

auto
QMpdClient::set_repeat(bool mode) -> void
{
  if (!cmd_conn) return;

  mpd_run_repeat(cmd_conn, mode);
  check_cmd_error("set_repeat");
}

auto
QMpdClient::set_random(bool mode) -> void
{
  if (!cmd_conn) return;

  mpd_run_random(cmd_conn, mode);
  check_cmd_error("set_random");
}

auto
QMpdClient::set_single(bool mode) -> void
{
  if (!cmd_conn) return;

  mpd_run_single(cmd_conn, mode);
  check_cmd_error("set_single");
}

auto
QMpdClient::set_consume(bool mode) -> void
{
  if (!cmd_conn) return;

  mpd_run_consume(cmd_conn, mode);
  check_cmd_error("set_consume");
}

auto
QMpdClient::set_crossfade(uint seconds) -> void
{
  if (!cmd_conn) return;

  mpd_run_crossfade(cmd_conn, seconds);
  check_cmd_error("set_crossfade");
}

// ---------------------------------------------------------------------------
// Queue commands
// ---------------------------------------------------------------------------

auto
QMpdClient::replace_queue_and_play(QStringList uris, uint playpos) -> void
{
  if (!cmd_conn) return;
  if (!mpd_command_list_begin(cmd_conn, false)) return;

  mpd_send_clear(cmd_conn);

  for (const auto &uri : uris)
    mpd_send_add_id(cmd_conn, uri.toUtf8().constData());

  mpd_send_play_pos(cmd_conn, playpos);
  mpd_command_list_end(cmd_conn);
  mpd_response_finish(cmd_conn);

  check_cmd_error("replace_queue_and_play");
}

auto
QMpdClient::append_queue(QStringList uris) -> void
{
  if (!cmd_conn) return;
  if (!mpd_command_list_begin(cmd_conn, false)) return;

  for (const auto &uri : uris)
    mpd_send_add_id(cmd_conn, uri.toUtf8().constData());

  mpd_command_list_end(cmd_conn);
  mpd_response_finish(cmd_conn);

  check_cmd_error("append_queue");
}

auto
QMpdClient::remove_from_queue(std::vector<uint32_t> indexes) -> void
{
  if (!cmd_conn) return;
  if (!mpd_command_list_begin(cmd_conn, false)) return;

  // Sort descending so later positions don't shift earlier ones
  std::sort(indexes.begin(), indexes.end(), std::greater<uint32_t>());

  for (auto idx : indexes)
    mpd_send_delete(cmd_conn, idx);

  mpd_command_list_end(cmd_conn);
  mpd_response_finish(cmd_conn);

  check_cmd_error("remove_from_queue");
}

auto
QMpdClient::rearrange_queue(std::vector<std::pair<uint32_t, uint32_t>> movements) -> void
{
  if (!cmd_conn) return;
  if (!mpd_command_list_begin(cmd_conn, false)) return;

  for (const auto &[from, to] : movements)
    mpd_send_swap(cmd_conn, from, to);

  mpd_command_list_end(cmd_conn);
  mpd_response_finish(cmd_conn);

  check_cmd_error("rearrange_queue");
}

auto
QMpdClient::insert_queue_at(uint32_t pos, QStringList uris) -> void
{
  if (!cmd_conn) return;
  if (!mpd_command_list_begin(cmd_conn, false)) return;

  // Reverse so first URI ends up at pos after repeated insertions
  QStringList rev;
  for (int i = uris.size() - 1; i >= 0; --i)
    rev << uris[i];

  for (const auto &uri : rev)
    mpd_send_add_id_to(cmd_conn, uri.toUtf8().constData(), pos);

  mpd_command_list_end(cmd_conn);
  mpd_response_finish(cmd_conn);

  check_cmd_error("insert_queue_at");
}
