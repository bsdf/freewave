#ifndef SUBSONICBACKEND_HH
#define SUBSONICBACKEND_HH

#include <functional>

#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QTimer>

#include "controller/backend.hh"
#include "controller/audioengine.hh"
#include "controller/librarymanager.hh"
#include "controller/subsonicclient.hh"
#include "controller/trackcache.hh"

class SubsonicBackend : public Backend {
  Q_OBJECT
public:
  // No QObject parent: instances are owned by shared_ptr (factory/MainWindow);
  // accepting a parent here would invite double-delete.
  // profile_id namespaces the fallback (no-MBID) album cache key so that two
  // servers can't collide on the same native album id.
  explicit SubsonicBackend(
      QString server_url,
      SubsonicAuth auth,
      std::shared_ptr<LibraryManager> libman = nullptr,
      QString profile_id = {});

  // Inject a custom AudioEngine — used by tests (MockAudioEngine).
  SubsonicBackend(
      QString server_url,
      SubsonicAuth auth,
      AudioEngine *engine,
      std::shared_ptr<LibraryManager> libman = nullptr,
      QString profile_id = {});

  ~SubsonicBackend();

  auto get_albums() -> QList<album> override;
  void fetch_songs(const album &, std::function<void(const QList<song> &)> cb) override;
  bool supports(Feature f) const override
  {
    if (f == Feature::PollInterval)
      return true;
#ifdef ENABLE_VISUALIZER
    if (f == Feature::LocalPcm)
      return true; // GstAudioEngine taps its own decoded output
#endif
    if (f == Feature::Favorites)
      return cap_favorites; // native star / unstar
    if (f == Feature::Playlists)
      return cap_playlists; // Subsonic playlist API
    if (f == Feature::AudioCache)
      return cache != nullptr;
    return false;
  }

  void configure_audio_cache(bool enabled, qint64 budget_bytes) override
  {
    if (!cache) return;
    cache->set_enabled(enabled);
    cache->set_budget(budget_bytes);
  }
  void clear_audio_cache() override
  {
    if (cache) cache->clear();
  }
  auto audio_cache_bytes() const -> qint64 override
  {
    return cache ? cache->bytes_used() : 0;
  }

  void set_viz_pcm_enabled(bool on) override { engine->set_viz_pcm_enabled(on); }

  auto resolve_album_hash(const QString &native_album_id) const -> QString override;

  // SHA-256 the user pinned for this server via the trust-on-first-use prompt.
  // Empty means only CA-verifiable certificates are accepted. Set by
  // BackendFactory at construction — the backend deliberately does not reach
  // into ProfileStore itself.
  void set_pinned_certificate(QString sha256)
  {
    client->set_pinned_certificate(std::move(sha256));
    // A pinned certificate is the user's decision to trust this server. The
    // streaming stack verifies against its own trust store, which cannot be given
    // the pin, so it has to be told to stop verifying — otherwise the library
    // loads and playback silently fails.
    engine->set_allow_untrusted_tls(client->has_pinned_certificate());
  }

  // Take ownership of an on-disk cache of whole tracks. A cached track plays
  // from the local file — which is what makes a seek inside it instant — and the
  // successor is prefetched while the current one plays. Without a cache every
  // play streams, which is what the backend does on its own.
  void set_track_cache(TrackCache *track_cache);

public slots:
  bool connect_to_server() override;
  void disconnect_from_server() override;

  void play() override;
  void pause() override;
  void stop() override;
  void prev() override;
  void next() override;
  void seek(uint32_t ms) override;
  void set_volume(int vol) override;
  void play_pos(uint32_t pos) override;

  void set_repeat(bool repeat, bool single) override;
  void set_shuffle(bool shuffle) override;

  void insert_queue(const QList<song> &songs, uint32_t pos) override;
  void append_queue(const QList<song> &songs) override;
  void replace_queue(const QList<song> &songs, uint32_t playpos) override;
  void remove_from_queue(const QList<QModelIndex> &indexes) override;
  void rearrange_queue(uint32_t target, std::vector<uint32_t> indexes) override;

  void refresh_library() override;
  void set_poll_interval(int minutes) override;

  void fetch_album_art(const QString &uri) override;

  void set_favorite(const QString &uri, bool fav) override;
  void fetch_favorites() override;

  void fetch_playlists() override;
  void fetch_playlist_songs(const QString &id) override;
  void create_playlist(const QString &name, const QList<song> &songs) override;
  void rename_playlist(const QString &id, const QString &new_name) override;
  void delete_playlist(const QString &id) override;
  void add_to_playlist(const QString &id, const QList<song> &songs) override;
  void remove_from_playlist(const QString &id, const QList<int> &positions) override;
  void rearrange_playlist(const QString &id, int target,
      const QList<int> &moved) override;

private slots:
  // The library read on its own (connect), and the periodic/manual poll, which
  // is a library read plus "the server's favorites may have moved since".
  void load_library();
  void poll_library();
  void engine_state_changed(AudioEngine::State state);
  void engine_track_changed(const QUrl &uri);
  void engine_track_finished();

private:
  // Locate the queue entry a playback URL refers to, searching forward from the
  // current position (so duplicate tracks advance rather than jump back). The
  // id comes from the stream URL's query, or from the cache for a local file.
  // Returns -1 if neither yields an id, or if it matches nothing in the queue.
  auto locate_in_queue(const QUrl &url) const -> int;
  void fetch_album_page(int offset);
  // What a refusal of a given mutation proves. Only creating a fresh playlist
  // proves the server has no playlist support: it is the one write a read-only
  // bridge reliably refuses, and the one every playlist-capable server must
  // accept. A refused rename, delete or contents-replace speaks about that
  // playlist or that request — treating it as proof would let a name clash
  // disable the whole feature.
  enum class RefusalMeaning { ThisRequest,
    NoPlaylistSupport };
  // Issue a playlist-mutating GET; on ok, invalidate invalidate_id's contents
  // cache (if non-empty) and emit playlists_changed, else emit error.
  void run_playlist_mutation(const QUrl &url, const QString &context,
      const QString &invalidate_id,
      RefusalMeaning refusal = RefusalMeaning::ThisRequest);
  // The favorites read behind fetch_favorites. Tries the
  // ID3 endpoint (getStarred2) first; on an unusable reply (HTTP error or a
  // non-"ok" body — some bridges, e.g. Bandcamp, don't implement it) retries
  // once with the v1 directory endpoint (getStarred), whose song objects are
  // otherwise identical. A successful fallback sets `starred_needs_v1`, and
  // later reads go straight to v1.
  void fetch_starred_endpoint(const QString &endpoint, const QString &key,
      std::function<void(const QJsonArray &)> on_songs);
  void play_current();
  // The server's stream URL for a track — always what a miss plays.
  auto stream_url(const QString &track_id) const -> QUrl;
  // What to hand the engine for a track: the cached file if there is one, else
  // the stream URL.
  auto playback_url(const QString &track_id) -> QUrl;
  // Arm `track_id` as the gapless successor, and start prefetching it if it is
  // worth prefetching.
  void arm_next(const QString &track_id);
  // Which queue entry plays after the current one, accounting for repeat and
  // single. Empty when nothing does.
  auto next_track_id() const -> QString;
  void update_engine_next_url();
  void emit_playback_state();
  void save_state() const;
  void restore_state();
  // Populates `extensions` via getOpenSubsonicExtensions, then calls
  // send_ping(). Runs before auth is ever checked: Navidrome/LMS answer it
  // without valid credentials (verified live), and servers that do gate it
  // (gonic) are still satisfied by the credentials build_url attaches — the
  // sole blind spot is an apiKey profile on a gate-and-no-apiKey server, which
  // could not have connected anyway. Useful beyond apiKey: any future
  // capability query (songLyrics, playbackReport, ...) reads the same map
  // instead of a new round trip.
  void fetch_extensions();
  void send_ping();

  // Transport: URL building, auth, TLS trust, reply normalization. Every
  // request the backend makes goes through it; the backend itself holds no
  // QNetworkAccessManager and parses no envelopes.
  SubsonicClient *client;
  QString profile_id;

  bool connected = false;
  bool repeat = false;
  bool single = false;
  bool shuffle = false;

  int queue_pos = -1;
  QList<song> queue;
  QList<song> unshuffled_queue;
  uint32_t restore_position_ms = 0;

  QList<album> pending_albums;
  QSet<QString> songs_inflight; // album hashes with a UI getAlbum in flight

  // Last-fetched contents per playlist id, kept only so rearrange_playlist can
  // rebuild the full ordered songId list (Subsonic has no native move — reorder
  // is a full-contents replace). Invalidated per-id on any successful mutation;
  // the manager's refetch repopulates it.
  QHash<QString, QList<song>> playlist_contents;

  // Server-advertised OpenSubsonic extension -> supported versions, from
  // getOpenSubsonicExtensions at connect. Stays empty against a plain (non-
  // OpenSubsonic) server or on a failed/unreachable query; `extensions_known`
  // distinguishes that "couldn't confirm" case from a confirmed empty list.
  QHash<QString, QList<int>> extensions;
  bool extensions_known = false;
  bool has_extension(const QString &name) const { return extensions.contains(name); }

  // Session memo: this server's getStarred2 is unusable and the getStarred v1
  // fallback works (e.g. Bandcamp's bridge) — go straight to v1 and skip the
  // doomed round trip. Set only after a *successful* fallback so a transient
  // failure can't downgrade the session; reset per connection alongside
  // `extensions`.
  bool starred_needs_v1 = false;

  // Per-connection capability downgrades behind supports(). The protocol has no
  // flag for core endpoints — playlists and favorites are mandatory on paper, so
  // no server announces them — and a read-only bridge answers the reads fine
  // while refusing the writes. So both start optimistic and only a server that
  // demonstrably refuses turns one off, never a transport failure: a blip during
  // a poll tick must not strip a working server of its features for the session.
  // Reset per connection alongside `extensions`.
  bool cap_favorites = true;
  bool cap_playlists = true;
  void downgrade_favorites();
  void downgrade_playlists();

  std::shared_ptr<LibraryManager> libman;
  AudioEngine *engine;
  TrackCache *cache = nullptr;
  // The track whose stream URL is currently armed as the gapless successor.
  // It is armed before the prefetch of it can have finished, so when that lands
  // the arming has to be redone against the local file. Empty when nothing is
  // armed, or when what is armed is already local.
  QString armed_next_id;

  QTimer *poll_timer;
};

#endif // SUBSONICBACKEND_HH
