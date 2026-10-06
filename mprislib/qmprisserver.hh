#ifndef MPRISLIB_QMPRISSERVER_HH
#define MPRISLIB_QMPRISSERVER_HH

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QVariantMap>
#include <QDBusConnection>
#include <QDBusObjectPath>

#include "mprislib/metadata.hh"

namespace mpris {

enum class PlaybackStatus { Playing,
  Paused,
  Stopped };

enum class LoopStatus { None,
  Track,
  Playlist };

// Generic Qt6 MPRIS server. Knows nothing about MPD/Subsonic/Freewave — the app
// pushes state in via the set*() methods and reacts to the *Requested() signals
// a bus controller triggers. Owns the two QDBusAbstractAdaptor objects exported
// at /org/mpris/MediaPlayer2.
//
// Spin-off candidate: depends only on Qt6::Core + Qt6::DBus.
class QMprisServer : public QObject {
  Q_OBJECT
public:
  struct Config {
    QString serviceSuffix; // "freewave" → org.mpris.MediaPlayer2.freewave
    QString identity;      // human-readable name, e.g. "Freewave"
    QString desktopEntry;  // .desktop basename, no extension
    QStringList supportedUriSchemes;
    QStringList supportedMimeTypes;
    bool canRaise = true;
    bool canQuit = true;
  };

  explicit QMprisServer(Config config, QObject *parent = nullptr);
  ~QMprisServer() override;

  bool registerOnBus(QDBusConnection connection = QDBusConnection::sessionBus());

  // --- app → bus: state push. Each change is coalesced into one batched
  // org.freedesktop.DBus.Properties.PropertiesChanged on the next event loop turn.
  void setPlaybackStatus(PlaybackStatus s);
  void setLoopStatus(LoopStatus l);
  void setShuffle(bool on);
  void setVolume(double v); // 0.0–1.0
  void setMetadata(const Metadata &m);
  void setCanGoNext(bool v);
  void setCanGoPrevious(bool v);
  void setCanPlay(bool v);
  void setCanPause(bool v);
  void setCanSeek(bool v);
  void setCanControl(bool v);

  // Position is NOT a PropertiesChanged property (per spec). updatePosition only
  // caches the value for the Position getter; emitSeeked additionally fires the
  // Player.Seeked signal for a discontinuous jump.
  void updatePosition(qlonglong us);
  void emitSeeked(qlonglong us);

  // --- getters read by the adaptors (and the flush builder) ---
  auto playbackStatusString() const -> QString;
  auto loopStatusString() const -> QString;
  auto shuffle() const -> bool { return shuffle_on; }
  auto volume() const -> double { return vol; }
  auto metadataMap() const -> QVariantMap { return meta; }
  auto position() const -> qlonglong { return pos_us; }
  auto canGoNext() const -> bool { return can_go_next; }
  auto canGoPrevious() const -> bool { return can_go_previous; }
  auto canPlay() const -> bool { return can_play; }
  auto canPause() const -> bool { return can_pause; }
  auto canSeek() const -> bool { return can_seek; }
  auto canControl() const -> bool { return can_control; }
  auto config() const -> const Config & { return cfg; }

  // Bus controllers write LoopStatus/Shuffle/Volume as properties; the adaptors
  // route those writes (and method calls) here, which re-emit as request signals.
  static auto loopStatusFromString(const QString &s) -> LoopStatus;

signals:
  // bus → app: a controller invoked something. The app is responsible for
  // performing the action and then pushing the resulting state back via set*().
  void playRequested();
  void pauseRequested();
  void playPauseRequested();
  void stopRequested();
  void nextRequested();
  void previousRequested();
  void seekRequested(qlonglong offset_us);
  void setPositionRequested(QDBusObjectPath trackId, qlonglong pos_us);
  void volumeRequested(double v);
  void loopStatusRequested(mpris::LoopStatus l);
  void shuffleRequested(bool on);
  void raiseRequested();
  void quitRequested();

private:
  void markDirty(const QString &property);
  void flushChanged();

  Config cfg;
  QDBusConnection conn = QDBusConnection::sessionBus();
  bool registered = false;

  class RootAdaptor *root = nullptr;
  class PlayerAdaptor *player = nullptr;

  PlaybackStatus status = PlaybackStatus::Stopped;
  LoopStatus loop = LoopStatus::None;
  bool shuffle_on = false;
  double vol = 1.0;
  QVariantMap meta;
  bool can_go_next = false;
  bool can_go_previous = false;
  bool can_play = false;
  bool can_pause = false;
  bool can_seek = false;
  bool can_control = false;
  qlonglong pos_us = 0;

  QSet<QString> dirty;
  bool flush_scheduled = false;
};

} // namespace mpris

Q_DECLARE_METATYPE(mpris::LoopStatus)

#endif // MPRISLIB_QMPRISSERVER_HH
