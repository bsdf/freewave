#include "mprislib/playeradaptor.hh"
#include "mprislib/qmprisserver.hh"

namespace mpris {

PlayerAdaptor::PlayerAdaptor(QMprisServer *server)
  : QDBusAbstractAdaptor{server}
  , server{server}
{
}

QString
PlayerAdaptor::playbackStatus() const
{
  return server->playbackStatusString();
}

QString
PlayerAdaptor::loopStatus() const
{
  return server->loopStatusString();
}

void
PlayerAdaptor::setLoopStatus(const QString &s)
{
  Q_EMIT server->loopStatusRequested(QMprisServer::loopStatusFromString(s));
}

bool
PlayerAdaptor::shuffle() const
{
  return server->shuffle();
}

void
PlayerAdaptor::setShuffle(bool on)
{
  Q_EMIT server->shuffleRequested(on);
}

QVariantMap
PlayerAdaptor::metadata() const
{
  return server->metadataMap();
}

double
PlayerAdaptor::volume() const
{
  return server->volume();
}

void
PlayerAdaptor::setVolume(double v)
{
  Q_EMIT server->volumeRequested(v);
}

qlonglong
PlayerAdaptor::position() const
{
  return server->position();
}

bool
PlayerAdaptor::canGoNext() const
{
  return server->canGoNext();
}

bool
PlayerAdaptor::canGoPrevious() const
{
  return server->canGoPrevious();
}

bool
PlayerAdaptor::canPlay() const
{
  return server->canPlay();
}

bool
PlayerAdaptor::canPause() const
{
  return server->canPause();
}

bool
PlayerAdaptor::canSeek() const
{
  return server->canSeek();
}

bool
PlayerAdaptor::canControl() const
{
  return server->canControl();
}

void
PlayerAdaptor::Next()
{
  Q_EMIT server->nextRequested();
}

void
PlayerAdaptor::Previous()
{
  Q_EMIT server->previousRequested();
}

void
PlayerAdaptor::Pause()
{
  Q_EMIT server->pauseRequested();
}

void
PlayerAdaptor::PlayPause()
{
  Q_EMIT server->playPauseRequested();
}

void
PlayerAdaptor::Stop()
{
  Q_EMIT server->stopRequested();
}

void
PlayerAdaptor::Play()
{
  Q_EMIT server->playRequested();
}

void
PlayerAdaptor::Seek(qlonglong offset_us)
{
  Q_EMIT server->seekRequested(offset_us);
}

void
PlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong pos_us)
{
  Q_EMIT server->setPositionRequested(trackId, pos_us);
}

void
PlayerAdaptor::OpenUri(const QString &)
{
  // No supported URI schemes are advertised; nothing to open.
}

} // namespace mpris
