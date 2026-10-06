#include "mprislib/rootadaptor.hh"
#include "mprislib/qmprisserver.hh"

namespace mpris {

RootAdaptor::RootAdaptor(QMprisServer *server)
  : QDBusAbstractAdaptor{server}
  , server{server}
{
}

bool
RootAdaptor::canQuit() const
{
  return server->config().canQuit;
}

bool
RootAdaptor::canRaise() const
{
  return server->config().canRaise;
}

QString
RootAdaptor::identity() const
{
  return server->config().identity;
}

QString
RootAdaptor::desktopEntry() const
{
  return server->config().desktopEntry;
}

QStringList
RootAdaptor::supportedUriSchemes() const
{
  return server->config().supportedUriSchemes;
}

QStringList
RootAdaptor::supportedMimeTypes() const
{
  return server->config().supportedMimeTypes;
}

void
RootAdaptor::Raise()
{
  Q_EMIT server->raiseRequested();
}

void
RootAdaptor::Quit()
{
  Q_EMIT server->quitRequested();
}

} // namespace mpris
