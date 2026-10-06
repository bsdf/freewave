#include "mprislib/qmprisserver.hh"
#include "mprislib/rootadaptor.hh"
#include "mprislib/playeradaptor.hh"

#include <QDBusMessage>
#include <QMetaObject>

namespace mpris {

namespace {
constexpr auto OBJECT_PATH = "/org/mpris/MediaPlayer2";
constexpr auto PLAYER_IFACE = "org.mpris.MediaPlayer2.Player";
constexpr auto PROPS_IFACE = "org.freedesktop.DBus.Properties";
} // namespace

QMprisServer::QMprisServer(Config config, QObject *parent)
  : QObject{parent}
  , cfg{std::move(config)}
{
}

QMprisServer::~QMprisServer() = default;

bool
QMprisServer::registerOnBus(QDBusConnection connection)
{
  conn = connection;
  if (!root)
    root = new RootAdaptor(this);
  if (!player)
    player = new PlayerAdaptor(this);

  if (!conn.registerObject(QString::fromLatin1(OBJECT_PATH), this,
          QDBusConnection::ExportAdaptors))
    return false;

  const QString service = QStringLiteral("org.mpris.MediaPlayer2.") + cfg.serviceSuffix;
  if (!conn.registerService(service))
    {
      conn.unregisterObject(QString::fromLatin1(OBJECT_PATH));
      return false;
    }

  registered = true;
  return true;
}

auto
QMprisServer::playbackStatusString() const -> QString
{
  switch (status)
    {
    case PlaybackStatus::Playing:
      return QStringLiteral("Playing");
    case PlaybackStatus::Paused:
      return QStringLiteral("Paused");
    case PlaybackStatus::Stopped:
      break;
    }
  return QStringLiteral("Stopped");
}

auto
QMprisServer::loopStatusString() const -> QString
{
  switch (loop)
    {
    case LoopStatus::Track:
      return QStringLiteral("Track");
    case LoopStatus::Playlist:
      return QStringLiteral("Playlist");
    case LoopStatus::None:
      break;
    }
  return QStringLiteral("None");
}

auto
QMprisServer::loopStatusFromString(const QString &s) -> LoopStatus
{
  if (s == QStringLiteral("Track"))
    return LoopStatus::Track;
  if (s == QStringLiteral("Playlist"))
    return LoopStatus::Playlist;
  return LoopStatus::None;
}

void
QMprisServer::setPlaybackStatus(PlaybackStatus s)
{
  if (status == s)
    return;
  status = s;
  markDirty(QStringLiteral("PlaybackStatus"));
}

void
QMprisServer::setLoopStatus(LoopStatus l)
{
  if (loop == l)
    return;
  loop = l;
  markDirty(QStringLiteral("LoopStatus"));
}

void
QMprisServer::setShuffle(bool on)
{
  if (shuffle_on == on)
    return;
  shuffle_on = on;
  markDirty(QStringLiteral("Shuffle"));
}

void
QMprisServer::setVolume(double v)
{
  if (qFuzzyCompare(vol, v))
    return;
  vol = v;
  markDirty(QStringLiteral("Volume"));
}

void
QMprisServer::setMetadata(const Metadata &m)
{
  meta = m.toMap();
  markDirty(QStringLiteral("Metadata"));
}

void
QMprisServer::setCanGoNext(bool v)
{
  if (can_go_next == v)
    return;
  can_go_next = v;
  markDirty(QStringLiteral("CanGoNext"));
}

void
QMprisServer::setCanGoPrevious(bool v)
{
  if (can_go_previous == v)
    return;
  can_go_previous = v;
  markDirty(QStringLiteral("CanGoPrevious"));
}

void
QMprisServer::setCanPlay(bool v)
{
  if (can_play == v)
    return;
  can_play = v;
  markDirty(QStringLiteral("CanPlay"));
}

void
QMprisServer::setCanPause(bool v)
{
  if (can_pause == v)
    return;
  can_pause = v;
  markDirty(QStringLiteral("CanPause"));
}

void
QMprisServer::setCanSeek(bool v)
{
  if (can_seek == v)
    return;
  can_seek = v;
  markDirty(QStringLiteral("CanSeek"));
}

void
QMprisServer::setCanControl(bool v)
{
  if (can_control == v)
    return;
  can_control = v;
  markDirty(QStringLiteral("CanControl"));
}

void
QMprisServer::updatePosition(qlonglong us)
{
  pos_us = us;
}

void
QMprisServer::emitSeeked(qlonglong us)
{
  pos_us = us;
  if (player)
    player->notifySeeked(us);
}

void
QMprisServer::markDirty(const QString &property)
{
  dirty.insert(property);
  if (flush_scheduled)
    return;
  flush_scheduled = true;
  // Coalesce every change made in this event-loop turn into one PropertiesChanged.
  QMetaObject::invokeMethod(this, [this] { flushChanged(); }, Qt::QueuedConnection);
}

void
QMprisServer::flushChanged()
{
  flush_scheduled = false;
  const QSet<QString> changed = std::move(dirty);
  dirty.clear();
  if (!registered || changed.isEmpty())
    return;

  QVariantMap props;
  for (const QString &name : changed)
    {
      if (name == QStringLiteral("PlaybackStatus"))
        props[name] = playbackStatusString();
      else if (name == QStringLiteral("LoopStatus"))
        props[name] = loopStatusString();
      else if (name == QStringLiteral("Shuffle"))
        props[name] = shuffle_on;
      else if (name == QStringLiteral("Volume"))
        props[name] = vol;
      else if (name == QStringLiteral("Metadata"))
        props[name] = meta;
      else if (name == QStringLiteral("CanGoNext"))
        props[name] = can_go_next;
      else if (name == QStringLiteral("CanGoPrevious"))
        props[name] = can_go_previous;
      else if (name == QStringLiteral("CanPlay"))
        props[name] = can_play;
      else if (name == QStringLiteral("CanPause"))
        props[name] = can_pause;
      else if (name == QStringLiteral("CanSeek"))
        props[name] = can_seek;
      else if (name == QStringLiteral("CanControl"))
        props[name] = can_control;
    }

  QDBusMessage msg = QDBusMessage::createSignal(
      QString::fromLatin1(OBJECT_PATH),
      QString::fromLatin1(PROPS_IFACE),
      QStringLiteral("PropertiesChanged"));
  msg << QString::fromLatin1(PLAYER_IFACE) << props << QStringList{};
  conn.send(msg);
}

} // namespace mpris
