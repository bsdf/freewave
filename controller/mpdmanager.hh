#ifndef MPDMANAGER_HH
#define MPDMANAGER_HH

#include <concepts>
#include <memory>

#include <QThread>
#include <QTimer>
#include <QModelIndex>

#include "mpdlib/QMpdClient.hh"

#include "model/album.hh"
#include "model/song.hh"

#include "controller/backend.hh"
#include "controller/librarymanager.hh"

class MpdManager : public Backend {
  Q_OBJECT

public:
  // No QObject parent: instances are owned by shared_ptr (factory/MainWindow);
  // accepting a parent here would invite double-delete.
  explicit MpdManager(std::shared_ptr<LibraryManager> libman = nullptr, QString host = {}, uint16_t port = 0);

  ~MpdManager();

  // Synchronous cache reads — no MPD I/O, safe from main thread
  auto get_albums() -> QList<album> override;
  void fetch_songs(const album &album, std::function<void(const QList<song> &)> cb) override;
  bool supports(Feature f) const override
  {
    return f == Feature::ServerScan
           || (f == Feature::Favorites && stickers_ok)
           || f == Feature::Playlists;
  }

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

  void refresh_library() override;
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

  void insert_queue(const QList<song> &songs, uint32_t pos = 0) override;
  void insert_queue(const QList<QString> &uris, uint32_t pos = 0);
  void replace_queue(const QList<song> &songs, uint32_t playpos = 0) override;
  void append_queue(const QList<song> &songs) override;
  void remove_from_queue(const QList<QModelIndex> &selected_indexes) override;
  void rearrange_queue(uint32_t target_idx, std::vector<uint32_t> indexes) override;

  // Legacy alias kept for UI backward compatibility
  void update_db();

private slots:
  void on_connected();
  void on_disconnected();
  void on_error(mpd::error err);
  void handle_idle(unsigned int idle);
  void on_status(mpd::status status);
  void on_queue(std::vector<mpd::song> songs);
  void on_current_song(std::optional<mpd::song> song);
  void on_all_songs(std::vector<mpd::song> songs);
  void on_favorites(QSet<QString> uris);
  void on_playlists(std::vector<mpd::playlist_summary> playlists);
  void on_playlist_songs(QString name, std::vector<mpd::song> songs);

private:
  template<typename Func, typename... Args>
    requires std::invocable<Func, QMpdClient *, Args...>
  void dispatch(Func func, Args &&...args);

  auto mpdsong_to_song(const mpd::song &mpdsong, QString album_hash = {}) -> song;
  auto get_album_hash(const mpd::song &mpdsong) -> QString;

  QString mpd_host;
  uint16_t mpd_port = 0;
  bool connected = false;
  bool stickers_ok = true; // cleared if the server's sticker DB is unavailable
  bool db_updating = false;
  bool pending_db_finish = false; // set when db update finishes; cleared in on_all_songs
  mpd::status last_status;        // most recent status

  QThread worker_thread;
  QMpdClient *client = nullptr;

  // MPD_IDLE_STICKER carries no song id, so we re-pull the whole favorite set;
  // this debounces bursts of sticker changes into a single refetch.
  QTimer fav_refresh_timer;

  // MPD_IDLE_STORED_PLAYLIST names no playlist; this debounces bursts into a
  // single playlists_changed so consumers refetch once.
  QTimer playlist_refresh_timer;

  std::shared_ptr<LibraryManager> libman;
};

#endif /* MPDMANAGER_HH */
