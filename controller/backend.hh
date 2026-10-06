#ifndef BACKEND_HH
#define BACKEND_HH

#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

#include <QObject>
#include <QList>
#include <QSet>
#include <QModelIndex>
#include <QByteArray>

#include "model/album.hh"
#include "model/playlist.hh"
#include "model/song.hh"
#include "controller/playbackstate.hh"

// Abstract backend interface. Concrete implementations: MpdBackend, SubsonicBackend.
//
// get_albums() is a synchronous cache read; per-album songs load via fetch_songs
// (synchronous for MPD, async for Subsonic). Safe to call from the main thread.
//
// All command slots are main-thread-safe; each implementation is responsible
// for dispatching work to the appropriate thread internally.
class Backend : public QObject {
  Q_OBJECT
public:
  explicit Backend(QObject *parent = nullptr)
    : QObject{parent}
  {
  }

  // Album list is a synchronous cache read — fully populated after library load.
  virtual auto get_albums() -> QList<album> = 0;

  // Per-album song lists may load lazily (e.g. Subsonic getAlbum). The callback
  // is invoked with the songs once available — synchronously for cached/local
  // backends (MPD), asynchronously after a network fetch for lazy backends. Pass
  // an empty callback to warm the cache (prefetch). Lazy backends also emit
  // songs_changed(hash) when a fetch completes.
  virtual void fetch_songs(const album &, std::function<void(const QList<song> &)> cb) = 0;

  // Resolve a song's native_album_id to the album_hash the library keys that
  // album under. Standalone songs (favorites, playlist entries) arrive knowing
  // only the native id, and the index behind this call fills in as the library
  // loads — so holders of resolved songs can re-run it on album_mapping_changed
  // rather than re-requesting the songs. Returns empty when the backend has no
  // such indirection (MPD hashes each song from its own tags).
  virtual auto resolve_album_hash(const QString &native_album_id) const -> QString
  {
    Q_UNUSED(native_album_id);
    return {};
  }

  // Per-backend feature support — UI gates controls on this; default false so
  // backends opt in to what they actually implement.
  enum class Feature { PollInterval,
    ServerScan,
    ReplayGain,
    Gapless,
    // The backend decodes audio locally and can tap its own output for the
    // visualizer (via viz_pcm / set_viz_pcm_enabled). When unsupported, the
    // visualizer falls back to a PipeWire default-sink monitor.
    LocalPcm,
    // The backend has a native, persistent per-song favorite mechanism
    // (MPD stickers / Subsonic star). Keyed by song.uri on both sides.
    Favorites,
    // Server-native stored playlists (MPD stored playlists / Subsonic
    // playlist API).
    Playlists,
    // The backend streams audio to this machine, so keeping played tracks on
    // disk is worth something. A backend whose server does its own playback has
    // nothing here to cache.
    AudioCache };

  // Why a connect attempt failed. Only the connection-state UI consumes this;
  // it decides which failure overlay to raise, and "the server answered and
  // refused the credentials" has to reach the user as something other than
  // "the server is unreachable".
  enum class ConnectError {
    // Nothing usable came back: refused, unroutable, timed out, or an answer
    // that was not a working Subsonic reply.
    Unreachable,
    // The server answered and rejected the credentials. Retrying cannot help;
    // only a change in Settings can.
    Auth
  };
  Q_ENUM(ConnectError)

  // Answers optimistically: a backend that cannot know a specific server's
  // limits until it has tried something says yes, then downgrades and emits
  // capabilities_changed. Re-query on that signal rather than caching the
  // answer past connect.
  virtual bool supports(Feature) const { return false; }

  // Enable/disable the local PCM tap that feeds viz_pcm. Default no-op; only
  // backends advertising Feature::LocalPcm act on it.
  virtual void set_viz_pcm_enabled(bool) {}

  // On-disk cache of played tracks; no-ops unless the backend advertises
  // Feature::AudioCache. Both settings arrive together because they are edited
  // together and neither means much alone.
  virtual void configure_audio_cache(bool enabled, qint64 budget_bytes)
  {
    Q_UNUSED(enabled);
    Q_UNUSED(budget_bytes);
  }
  virtual void clear_audio_cache() {}
  virtual auto audio_cache_bytes() const -> qint64 { return 0; }

public slots:
  // Connection lifecycle. Named to avoid shadowing QObject::connect/disconnect —
  // an unqualified disconnect() in a subclass would otherwise silently resolve
  // to the backend slot instead of severing signal connections.
  virtual bool connect_to_server() = 0;
  virtual void disconnect_from_server() = 0;

  // Transport
  virtual void play() = 0;
  virtual void pause() = 0;
  virtual void stop() = 0;
  virtual void prev() = 0;
  virtual void next() = 0;
  virtual void seek(uint32_t ms) = 0; // absolute position in milliseconds
  virtual void set_volume(int vol) = 0;
  virtual void play_pos(uint32_t pos) = 0;

  // Playback options
  virtual void set_repeat(bool repeat, bool single) = 0;
  virtual void set_shuffle(bool shuffle) = 0;

  // Queue
  virtual void insert_albums(const QList<album> &albums, uint32_t pos)
  {
    fetch_albums_songs(albums, [this, pos](const QList<song> &all) {
      insert_queue(all, pos);
    });
  }
  virtual void append_albums(const QList<album> &albums)
  {
    fetch_albums_songs(albums, [this](const QList<song> &all) {
      append_queue(all);
    });
  }
  // Replace the queue with these albums and start playing from the first track.
  virtual void replace_albums(const QList<album> &albums)
  {
    fetch_albums_songs(albums, [this](const QList<song> &all) {
      replace_queue(all, 0);
    });
  }
  virtual void insert_queue(const QList<song> &songs, uint32_t pos) = 0;
  virtual void append_queue(const QList<song> &songs) = 0;
  virtual void replace_queue(const QList<song> &songs, uint32_t playpos) = 0;
  virtual void remove_from_queue(const QList<QModelIndex> &indexes) = 0;
  virtual void rearrange_queue(uint32_t target, std::vector<uint32_t> indexes) = 0;

  // Library refresh
  virtual void refresh_library() {}

  // Set auto-refresh interval in minutes; 0 disables auto-refresh.
  // No-op for backends that don't poll (e.g. MPD uses idle events).
  virtual void set_poll_interval(int minutes) { Q_UNUSED(minutes); }

  // Art — uri is backend-specific (file path for MPD, song/album id for Subsonic)
  virtual void fetch_album_art(const QString &uri) = 0;

  // Favorites — persist a single per-song favorite. Default no-op; only backends
  // advertising Feature::Favorites act. uri is the song.uri (universal key).
  virtual void set_favorite(const QString &uri, bool fav)
  {
    Q_UNUSED(uri);
    Q_UNUSED(fav);
  }
  // Load the favorites. One read answers both consumers, so it emits
  // favorites_loaded (the uri set, for hearts) and favorite_songs_loaded (the
  // full songs, for the auto list) from the same response. Default no-op.
  virtual void fetch_favorites() {}

  // Playlists — server-native stored playlists. Default no-ops; only backends
  // advertising Feature::Playlists act. id is playlist_info::id.
  virtual void fetch_playlists() {}                    // → playlists_loaded
  virtual void fetch_playlist_songs(const QString &id) // → playlist_songs_loaded
  {
    Q_UNUSED(id);
  }
  virtual void create_playlist(const QString &name, const QList<song> &songs)
  {
    Q_UNUSED(name);
    Q_UNUSED(songs);
  }
  virtual void rename_playlist(const QString &id, const QString &new_name)
  {
    Q_UNUSED(id);
    Q_UNUSED(new_name);
  }
  virtual void delete_playlist(const QString &id) { Q_UNUSED(id); }
  virtual void add_to_playlist(const QString &id, const QList<song> &songs)
  {
    Q_UNUSED(id);
    Q_UNUSED(songs);
  }
  // positions are 0-based indexes into the playlist's current song list.
  virtual void remove_from_playlist(const QString &id, const QList<int> &positions)
  {
    Q_UNUSED(id);
    Q_UNUSED(positions);
  }
  // Reorder: move the songs at flat indexes `moved` so they land at flat
  // position `target` (the insertion point in the pre-move index space), same
  // convention as rearrange_queue / reorder::get_output_order. A whole coalesced
  // album row drags its full contiguous span. Backends realize this in bulk (one
  // round-trip): MPD via a batched playlistmove sequence, Subsonic via a
  // full-contents createPlaylist replace — a per-element move would be a full
  // playlist rewrite per track on Subsonic.
  virtual void rearrange_playlist(const QString &id, int target,
      const QList<int> &moved)
  {
    Q_UNUSED(id);
    Q_UNUSED(target);
    Q_UNUSED(moved);
  }
signals:
  void connection_update(bool connected);

  // Emitted immediately before connection_update(false) when a connect *attempt*
  // failed, to say why. A mid-session drop emits only connection_update(false),
  // so a listener must treat "no connection_failed" as an unclassified failure
  // rather than waiting for one.
  void connection_failed(Backend::ConnectError kind, const QString &detail);

  // The server's TLS certificate could not be verified against any known CA —
  // the common case being a self-hosted server behind a private CA. The UI
  // presents the fingerprint and, if the user accepts, pins it on the profile
  // (trust-on-first-use). Nothing is trusted until the user says so.
  void certificate_untrusted(const QString &host, const QString &fingerprint,
      const QString &issuer, const QString &reason);

  void playback_state_changed(PlaybackState state);
  void current_song_changed(const song &s);
  void queue_changed(const QList<song> &songs);

  void library_changed(); // library cache has been updated

  // The mapping standalone songs (favorites, playlist entries) resolve their
  // album_hash through has changed, so anything holding resolved hashes must
  // re-resolve. Emitted after library_changed and only when the mapping really
  // differs: a poll that reloads an identical library emits library_changed but
  // not this, since re-resolving would reproduce the same hashes. A backend
  // whose library load *is* the mapping (MPD hashes per track) emits both
  // together.
  void album_mapping_changed();
  void library_refresh_active(bool active); // scan/db-update in progress
  // The library fetch failed after the connection was reported up — e.g. the
  // server accepted `ping` but the first data request errored (a bad credential
  // on a server whose ping doesn't validate auth, like Bandcamp). Lets the
  // connection state machine escape the loading overlay instead of hanging on a
  // success-only signal that will never arrive.
  void library_load_failed(const QString &reason);
  void songs_changed(const QString &album_hash); // songs for an album became available

  void album_art_received(QString uri, QByteArray bytes);

  void error(const QString &message);

  // Mono PCM frames from a local-decoding backend's own output, for the
  // visualizer. Only emitted while the tap is enabled (Feature::LocalPcm).
  void viz_pcm(const QList<float> &samples);

  // The server's favorites may no longer match what we hold: a poll heartbeat
  // on a backend with no push channel, or a sticker change reported by one that
  // has. It reports the possibility, not a fetch — FavoritesManager owns the
  // decision to read, and is the only thing that calls fetch_favorites.
  void favorites_stale();

  // Favorites. One fetch_favorites emits both loaded signals, uris first:
  // favorites_loaded is the set behind the hearts, favorite_songs_loaded the
  // full songs behind the auto "Favorited Tracks" list (Subsonic favorites can
  // reference albums whose songs aren't cached locally). favorite_changed
  // confirms a single toggle.
  void favorites_loaded(const QSet<QString> &uris);
  void favorite_songs_loaded(const QList<song> &songs);
  void favorite_changed(const QString &uri, bool fav);

  // Playlists.
  void playlists_loaded(const QList<playlist_info> &lists);
  void playlist_songs_loaded(const QString &id, const QList<song> &songs);
  // A mutation completed server-side (create/rename/delete/add/remove/move).
  // Consumers refetch the playlist list; changed_id names the one playlist whose
  // contents are now stale, so only its cached songs need dropping. Empty means
  // "unknown, assume all" — a create (the new id isn't known yet) and every MPD
  // MPD_IDLE_STORED_PLAYLIST event, which names no playlist.
  void playlists_changed(const QString &changed_id);

  // A supports() answer changed for the live connection — a server disproved
  // one of the optimistic defaults. Consumers re-query supports(); they must not
  // assume which feature moved.
  void capabilities_changed();

protected:
  // Load songs for several albums concurrently, preserving album order, then
  // deliver the concatenated track list once every album has resolved.
  void fetch_albums_songs(const QList<album> &albums,
      std::function<void(const QList<song> &)> done)
  {
    if (albums.isEmpty())
      return;
    struct State {
      int remaining;
      QList<QList<song>> per_album;
      std::function<void(const QList<song> &)> done;
    };
    auto state = std::make_shared<State>();
    state->remaining = albums.size();
    state->per_album.resize(albums.size());
    state->done = std::move(done);
    for (int i = 0; i < albums.size(); ++i)
      fetch_songs(albums[i], [state, i](const QList<song> &songs) {
        state->per_album[i] = songs;
        if (--state->remaining > 0)
          return;
        QList<song> all;
        for (const auto &s : state->per_album)
          all += s;
        if (!all.isEmpty())
          state->done(all);
      });
  }
};

// Re-resolve each song's album_hash from the native album id it carries,
// in place; returns true if any hash moved. This is the local correction for
// songs parsed against a library index that was still filling in — a holder of
// resolved songs runs it on album_mapping_changed instead of re-requesting
// them. Songs the backend cannot resolve keep the hash they already have, so
// it is a no-op on MPD and on ids the library doesn't know.
auto reresolve_album_hashes(const Backend &backend, QList<song> &songs) -> bool;

#endif // BACKEND_HH
