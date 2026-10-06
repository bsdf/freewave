#ifndef QMPDCLIENT_HH
#define QMPDCLIENT_HH

#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QByteArray>

#include <vector>
#include <optional>
#include <cstdint>
#include <utility>

#include "mpdlib/mpd.hh"

// Custom types used in cross-thread signals — declare for Qt's meta-object system.
Q_DECLARE_METATYPE(mpd::error)
Q_DECLARE_METATYPE(mpd::status)
Q_DECLARE_METATYPE(std::vector<mpd::song>)
Q_DECLARE_METATYPE(std::optional<mpd::song>)
Q_DECLARE_METATYPE(std::vector<mpd::playlist_summary>)

class QMpdClient : public QObject {
  Q_OBJECT

public:
  explicit QMpdClient(QObject *parent = nullptr);
  ~QMpdClient() override;

signals:
  void connected();
  void disconnected();
  void error(mpd::error err);

  // Fetch results — emitted on the worker thread, received via queued connection
  void status_received(mpd::status status);
  void queue_received(std::vector<mpd::song> songs);
  void current_song_received(std::optional<mpd::song> song);
  void all_songs_received(std::vector<mpd::song> songs);
  void album_art_received(QString uri, QByteArray bytes);

  // Full set of favorited song uris (from a sticker find). Empty set is valid.
  void favorites_received(QSet<QString> uris);
  // The sticker database is unavailable (e.g. disabled in mpd.conf) — favorites
  // can't be stored. Lets the manager gate the UI instead of erroring.
  void favorites_unsupported();

  // Stored playlists. playlists_received carries summaries (no counts —
  // contents come via playlist_songs_received); stored_playlists_changed
  // confirms a successful mutation (create/rename/rm/add/delete/move).
  void playlists_received(std::vector<mpd::playlist_summary> playlists);
  void playlist_songs_received(QString name, std::vector<mpd::song> songs);
  void stored_playlists_changed();

  // Idle notification (mpd_idle cast to unsigned int to avoid metatype registration
  // of the C enum; callers test with bitwise AND as before)
  void idle_event(unsigned int idle);

public slots:
  // Lifecycle — run on worker thread via queued connection
  void do_connect(QString host, uint16_t port);
  void do_disconnect();

  // Playback
  void play();
  void stop();
  void pause(bool mode);
  void next();
  void prev();
  void seek(float pos);
  void play_pos(uint pos);

  // Options
  void set_volume(uint vol);
  void set_repeat(bool mode);
  void set_random(bool mode);
  void set_single(bool mode);
  void set_consume(bool mode);
  void set_crossfade(uint seconds);

  // Queue commands
  void replace_queue_and_play(QStringList uris, uint playpos);
  void append_queue(QStringList uris);
  void remove_from_queue(std::vector<uint32_t> indexes);
  void rearrange_queue(std::vector<std::pair<uint32_t, uint32_t>> movements);
  void insert_queue_at(uint32_t pos, QStringList uris);

  // Fetch requests — results come back via signals above
  void fetch_status();
  void fetch_queue();
  void fetch_current_song();
  void fetch_all_songs();
  void fetch_album_art(QString uri);
  void trigger_db_update();

  // Favorites via stickers (name "favorite", value "1"). fetch_favorites emits
  // favorites_received; set_favorite sets/deletes the sticker on cmd_conn.
  void set_favorite(QString uri, bool fav);
  void fetch_favorites();

  // Stored playlists — the name is the id. create and add are both playlistadd
  // (MPD creates the playlist if missing); kept separate for interface clarity.
  void fetch_playlists();
  void fetch_playlist_songs(QString name);
  void create_playlist(QString name, QList<QString> uris);
  void add_to_playlist(QString name, QList<QString> uris);
  void rename_playlist(QString from, QString to);
  void delete_playlist(QString name);
  void remove_from_playlist(QString name, QList<int> positions);
  // Reorder via a batched sequence of playlistmove (from, to) ops — stored
  // playlists have no swap primitive. Applied in one command list.
  void rearrange_playlist(QString name, std::vector<std::pair<uint32_t, uint32_t>> moves);

private slots:
  void on_idle_readable();
  void on_keepalive();

private:
  // Returns true if no error; on error, emits error() and disconnects if fatal.
  bool check_cmd_error(const char *context);

  std::vector<mpd::song> receive_songs();
  QByteArray fetch_binary(decltype(mpd_run_readpicture) *f,
      const std::string &uri);
  void playlist_add_all(const QString &name, const QList<QString> &uris,
      const char *context);

  mpd_connection *cmd_conn = nullptr;  // command connection — never idles
  mpd_connection *idle_conn = nullptr; // idle connection — always idling

  std::unique_ptr<QSocketNotifier> idle_notifier;
  std::unique_ptr<QTimer> keepalive_timer;
};

#endif // QMPDCLIENT_HH
