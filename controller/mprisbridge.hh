#ifndef MPRISBRIDGE_HH
#define MPRISBRIDGE_HH

#include <QObject>
#include <QDir>
#include <QString>

#include "controller/playbackstate.hh"
#include "model/song.hh"

class Backend;
class LibraryManager;
class QWidget;

namespace mpris {
class QMprisServer;
enum class LoopStatus;
}

// Freewave-specific glue between BackendController and the generic mpris library.
// All Freewave knowledge (LoopStatus ⇆ repeat/single, art cache layout, volume
// scaling, ms→µs) lives here; mprislib stays backend-agnostic and spin-off-ready.
class MprisBridge : public QObject {
  Q_OBJECT
public:
  MprisBridge(Backend *backend, QDir art_cache,
      LibraryManager *libman, QWidget *window, QObject *parent = nullptr);
  ~MprisBridge() override;

  // Register on the session bus. Returns false if D-Bus is unavailable or the
  // service name is already taken (another instance running).
  bool register_on_bus();

  // The owned server — exposed for tests and as an extension point. Stays valid
  // for the lifetime of the bridge.
  auto server_object() const -> mpris::QMprisServer * { return server; }

public slots:
  // Wire AlbumArtManager::art_ready here so a cover that lands after the track
  // started (lazy fetch) re-pushes the current track's metadata with artUrl set.
  void on_art_ready(const QString &album_hash);

private:
  void on_playback_state(const PlaybackState &st);
  void on_current_song(const song &s);
  void on_connection(bool connected);
  void update_can_flags(bool connected);

  // Build the MPRIS metadata for the current song and push it to the server.
  void push_metadata();

  void map_loop_request(mpris::LoopStatus l);

  Backend *backend;
  LibraryManager *libman;
  QWidget *window;
  QDir art_cache;
  mpris::QMprisServer *server;

  // Cached transport state needed to translate relative seeks, play/pause toggle,
  // and to distinguish a seek (discontinuous position jump) from normal progress.
  PlayState last_state = PlayState::Stopped;
  uint32_t last_elapsed_ms = 0;
  uint32_t last_total_ms = 0;
  int last_queue_pos = -1;

  // Monotonic counter for the unique mpris:trackid object path.
  quint64 track_counter = 0;
  QString current_track_id;

  // Last song pushed — retained so art arriving after the track started
  // (lazy fetch) can re-push the same metadata with the cover filled in.
  song current_song;
  bool have_current_song = false;
};

#endif // MPRISBRIDGE_HH
