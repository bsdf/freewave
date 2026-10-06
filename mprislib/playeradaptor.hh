#ifndef MPRISLIB_PLAYERADAPTOR_HH
#define MPRISLIB_PLAYERADAPTOR_HH

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QString>
#include <QVariantMap>

namespace mpris {

class QMprisServer;

// org.mpris.MediaPlayer2.Player — transport interface. Property reads delegate to
// the server's cached state; writes and method calls re-emit as server request
// signals. PropertiesChanged is emitted manually by the server (batched), so the
// Q_PROPERTYs deliberately have no NOTIFY. Seeked is the one auto-relayed signal.
class PlayerAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
  Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
  Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
  Q_PROPERTY(double Rate READ rate WRITE setRate)
  Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
  Q_PROPERTY(QVariantMap Metadata READ metadata)
  Q_PROPERTY(double Volume READ volume WRITE setVolume)
  Q_PROPERTY(qlonglong Position READ position)
  Q_PROPERTY(double MinimumRate READ minimumRate)
  Q_PROPERTY(double MaximumRate READ maximumRate)
  Q_PROPERTY(bool CanGoNext READ canGoNext)
  Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
  Q_PROPERTY(bool CanPlay READ canPlay)
  Q_PROPERTY(bool CanPause READ canPause)
  Q_PROPERTY(bool CanSeek READ canSeek)
  Q_PROPERTY(bool CanControl READ canControl)

public:
  explicit PlayerAdaptor(QMprisServer *server);

  QString playbackStatus() const;
  QString loopStatus() const;
  void setLoopStatus(const QString &s);
  double rate() const { return 1.0; }
  void setRate(double) {}
  bool shuffle() const;
  void setShuffle(bool on);
  QVariantMap metadata() const;
  double volume() const;
  void setVolume(double v);
  qlonglong position() const;
  double minimumRate() const { return 1.0; }
  double maximumRate() const { return 1.0; }
  bool canGoNext() const;
  bool canGoPrevious() const;
  bool canPlay() const;
  bool canPause() const;
  bool canSeek() const;
  bool canControl() const;

  // Called by the server to fire the auto-relayed Seeked signal on the bus.
  void notifySeeked(qlonglong us) { Q_EMIT Seeked(us); }

public slots:
  void Next();
  void Previous();
  void Pause();
  void PlayPause();
  void Stop();
  void Play();
  void Seek(qlonglong offset_us);
  void SetPosition(const QDBusObjectPath &trackId, qlonglong pos_us);
  void OpenUri(const QString &uri);

signals:
  void Seeked(qlonglong us);

private:
  QMprisServer *server;
};

} // namespace mpris

#endif // MPRISLIB_PLAYERADAPTOR_HH
