#ifndef BACKENDCONTROLLER_HH
#define BACKENDCONTROLLER_HH

#include "controller/backend.hh"
#include "controller/librarymanager.hh"

#include <memory>

// Stable façade over the active backend. The UI and AlbumArtManager bind once
// to BackendController; switching backends replaces the inner object without
// rewiring any external connections.
class BackendController : public Backend {
  Q_OBJECT
public:
  // No QObject parent on either constructor: instances are owned by shared_ptr
  // (MainWindow); accepting a parent here would invite double-delete.

  // Production constructor: reads the active profile from ProfileStore and
  // instantiates the backend via BackendFactory.
  explicit BackendController(std::shared_ptr<LibraryManager> libman);

  // Test/do_switch constructor: attach a pre-built backend directly, bypassing
  // ProfileStore and BackendFactory. Pass a libman to exercise clear() in tests.
  explicit BackendController(std::shared_ptr<Backend> initial,
      std::shared_ptr<LibraryManager> libman = nullptr);

  auto get_albums() -> QList<album> override;
  void fetch_songs(const album &a,
      std::function<void(const QList<song> &)> cb) override;
  bool supports(Feature f) const override;
  void set_viz_pcm_enabled(bool on) override;
  void configure_audio_cache(bool enabled, qint64 budget_bytes) override;
  void clear_audio_cache() override;
  auto audio_cache_bytes() const -> qint64 override;
  auto resolve_album_hash(const QString &native_album_id) const -> QString override;

  // Whether the active backend is actually connected. "Active" only means the
  // profile was selected — connect_to_server() is async and may still fail, so
  // the two must not be conflated (the Servers list keys its Retry button on this).
  auto is_connected() const -> bool { return connected; }

  // Switch the active backend to the profile identified by id. Looks up the
  // profile in ProfileStore and creates a new backend via BackendFactory before
  // tearing down the old one. No-op if id is already active. Returns false —
  // leaving the active backend untouched — when the profile is already active,
  // is not found, or cannot be created.
  auto switch_to(const QString &profile_id) -> bool;

  // Go back to the last profile that was actually connected — the escape hatch
  // for a switch the user backs out of, since do_switch() has already torn that
  // session down by then. Returns false when there is nothing to go back to
  // (the first connect of a session, or a chain of switches none of which came
  // up), leaving the active backend untouched.
  auto revert_to_previous() -> bool;

  // Lower-level switch: tear down the current backend and activate new_active
  // with the given id. Skips ProfileStore and BackendFactory; exposed for tests
  // and for the switch_to(id) implementation after the factory step succeeds.
  void do_switch(std::shared_ptr<Backend> new_active, const QString &new_id);

signals:
  // Emitted at the start of do_switch(), before the old backend is torn down.
  // MainWindow connects this to show the loading overlay and reset ever_loaded.
  void backend_switching();

  // Emitted at the end of do_switch(), once the new backend is attached —
  // unlike backend_switching, supports() queries the new backend here.
  void backend_switched();

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

private:
  void attach(std::shared_ptr<Backend> b);
  void detach();

  std::shared_ptr<LibraryManager> libman;
  std::shared_ptr<Backend> active;
  QString active_id;
  // Last profile id that reached a connected state, for revert_to_previous().
  QString previous_id;
  bool connected = false;
};

#endif // BACKENDCONTROLLER_HH
