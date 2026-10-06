#include "controller/mprisbridge.hh"

#include "controller/backend.hh"
#include "controller/librarymanager.hh"
#include "mprislib/qmprisserver.hh"
#include "mprislib/metadata.hh"

#include <QWidget>
#include <QUrl>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDBusObjectPath>

#include <cmath>
#include <cstdlib>

namespace {
constexpr auto TRACK_ID_PREFIX = "/org/mpris/freewave/track/";

auto
to_playback_status(PlayState s) -> mpris::PlaybackStatus
{
  switch (s)
    {
    case PlayState::Playing:
      return mpris::PlaybackStatus::Playing;
    case PlayState::Paused:
      return mpris::PlaybackStatus::Paused;
    case PlayState::Stopped:
      break;
    }
  return mpris::PlaybackStatus::Stopped;
}

// Freewave's repeat/single pair → MPRIS LoopStatus. single wins (a single-track
// loop), then playlist repeat, else off.
auto
to_loop_status(bool repeat, bool single) -> mpris::LoopStatus
{
  if (single)
    return mpris::LoopStatus::Track;
  if (repeat)
    return mpris::LoopStatus::Playlist;
  return mpris::LoopStatus::None;
}
} // namespace

MprisBridge::MprisBridge(Backend *backend, QDir art_cache,
    LibraryManager *libman, QWidget *window, QObject *parent)
  : QObject{parent}
  , backend{backend}
  , libman{libman}
  , window{window}
  , art_cache{art_cache}
{
  mpris::QMprisServer::Config cfg;
  cfg.serviceSuffix = QStringLiteral("freewave");
  cfg.identity = QStringLiteral("Freewave");
  cfg.desktopEntry = QStringLiteral("org.xeyes.Freewave");
  cfg.canRaise = true;
  cfg.canQuit = true;
  server = new mpris::QMprisServer(cfg, this);

  // Backend → server
  connect(backend, &Backend::playback_state_changed,
      this, &MprisBridge::on_playback_state);
  connect(backend, &Backend::current_song_changed,
      this, &MprisBridge::on_current_song);
  connect(backend, &Backend::connection_update,
      this, &MprisBridge::on_connection);

  // Server → backend
  connect(server, &mpris::QMprisServer::playRequested,
      backend, &Backend::play);
  connect(server, &mpris::QMprisServer::pauseRequested,
      backend, &Backend::pause);
  connect(server, &mpris::QMprisServer::playPauseRequested, this, [this] {
    if (last_state == PlayState::Playing)
      this->backend->pause();
    else
      this->backend->play();
  });
  connect(server, &mpris::QMprisServer::stopRequested,
      backend, &Backend::stop);
  connect(server, &mpris::QMprisServer::nextRequested,
      backend, &Backend::next);
  connect(server, &mpris::QMprisServer::previousRequested,
      backend, &Backend::prev);
  connect(server, &mpris::QMprisServer::seekRequested, this, [this](qlonglong off_us) {
    qlonglong target = static_cast<qlonglong>(last_elapsed_ms) + off_us / 1000;
    if (target < 0)
      target = 0;
    if (last_total_ms > 0 && target > last_total_ms)
      target = last_total_ms;
    this->backend->seek(static_cast<uint32_t>(target));
  });
  connect(server, &mpris::QMprisServer::setPositionRequested, this,
      [this](QDBusObjectPath trackId, qlonglong pos_us) {
        if (trackId.path() != current_track_id)
          return; // stale: controller targeted a different track
        this->backend->seek(static_cast<uint32_t>(pos_us / 1000));
      });
  connect(server, &mpris::QMprisServer::volumeRequested, this, [this](double v) {
    this->backend->set_volume(static_cast<int>(std::lround(v * 100.0)));
  });
  connect(server, &mpris::QMprisServer::loopStatusRequested,
      this, &MprisBridge::map_loop_request);
  connect(server, &mpris::QMprisServer::shuffleRequested,
      backend, &Backend::set_shuffle);
  connect(server, &mpris::QMprisServer::raiseRequested, this, [this] {
    if (!this->window)
      return;
    this->window->show();
    this->window->raise();
    this->window->activateWindow();
  });
  connect(server, &mpris::QMprisServer::quitRequested,
      qApp, &QCoreApplication::quit);
}

MprisBridge::~MprisBridge() = default;

bool
MprisBridge::register_on_bus()
{
  return server->registerOnBus();
}

void
MprisBridge::update_can_flags(bool connected)
{
  server->setCanControl(connected);
  server->setCanPlay(connected);
  server->setCanPause(connected);
  server->setCanGoNext(connected);
  server->setCanGoPrevious(connected);
  server->setCanSeek(connected);
}

void
MprisBridge::on_connection(bool connected)
{
  update_can_flags(connected);
  if (!connected)
    server->setPlaybackStatus(mpris::PlaybackStatus::Stopped);
}

void
MprisBridge::on_playback_state(const PlaybackState &st)
{
  server->setPlaybackStatus(to_playback_status(st.state));
  if (st.volume >= 0)
    server->setVolume(st.volume / 100.0);
  server->setLoopStatus(to_loop_status(st.repeat, st.single));
  server->setShuffle(st.shuffle);

  const qlonglong us = static_cast<qlonglong>(st.elapsed_ms) * 1000;
  const bool same_track = st.queue_pos == last_queue_pos;
  const auto drift = std::llabs(static_cast<qlonglong>(st.elapsed_ms)
                                - static_cast<qlonglong>(last_elapsed_ms));
  if (same_track && drift > 1000)
    server->emitSeeked(us); // discontinuous jump on the same track = a seek
  else
    server->updatePosition(us);

  last_state = st.state;
  last_elapsed_ms = st.elapsed_ms;
  last_total_ms = st.total_ms;
  last_queue_pos = st.queue_pos;
}

void
MprisBridge::on_current_song(const song &s)
{
  // A new track gets a fresh trackid; art that arrives later re-pushes against
  // this same id (see on_art_ready).
  current_track_id = TRACK_ID_PREFIX + QString::number(track_counter);
  ++track_counter;
  current_song = s;
  have_current_song = true;

  push_metadata();

  // A new track resets the seek-detection baseline.
  last_elapsed_ms = 0;
}

void
MprisBridge::push_metadata()
{
  if (!have_current_song)
    return;

  const song &s = current_song;
  mpris::Metadata m;
  m.trackId(QDBusObjectPath(current_track_id));

  m.title(s.title);
  if (!s.artist.isEmpty())
    m.artist({s.artist});
  if (s.duration > 0)
    m.length_us(static_cast<qlonglong>(s.duration) * 1000);
  if (s.track_number > 0)
    m.trackNumber(static_cast<int>(s.track_number));
  if (s.disc_number > 0)
    m.discNumber(static_cast<int>(s.disc_number));

  if (libman && libman->has_album(s.album_hash))
    {
      const album a = libman->get_album(s.album_hash);
      if (!a.name.isEmpty())
        m.album(a.name);
      if (!a.artist.isEmpty())
        m.albumArtist({a.artist});
    }

  // Art is fetched lazily; only advertise the cover once it's on disk.
  const QString jpg = art_cache.filePath(s.album_hash + QStringLiteral(".jpg"));
  if (QFileInfo::exists(jpg))
    m.artUrl(QUrl::fromLocalFile(jpg));

  server->setMetadata(m);
}

void
MprisBridge::on_art_ready(const QString &album_hash)
{
  // Re-push only when the cover that just landed belongs to the current track.
  if (have_current_song && album_hash == current_song.album_hash)
    push_metadata();
}

void
MprisBridge::map_loop_request(mpris::LoopStatus l)
{
  switch (l)
    {
    case mpris::LoopStatus::Track:
      backend->set_repeat(true, true);
      break;
    case mpris::LoopStatus::Playlist:
      backend->set_repeat(true, false);
      break;
    case mpris::LoopStatus::None:
      backend->set_repeat(false, false);
      break;
    }
}
