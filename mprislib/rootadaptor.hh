#ifndef MPRISLIB_ROOTADAPTOR_HH
#define MPRISLIB_ROOTADAPTOR_HH

#include <QDBusAbstractAdaptor>
#include <QString>
#include <QStringList>

namespace mpris {

class QMprisServer;

// org.mpris.MediaPlayer2 — the root interface. Read-only metadata about the
// player plus Raise()/Quit(). All properties come from QMprisServer::Config and
// never change at runtime, so no PropertiesChanged is emitted here.
class RootAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
  Q_PROPERTY(bool CanQuit READ canQuit)
  Q_PROPERTY(bool CanRaise READ canRaise)
  Q_PROPERTY(bool HasTrackList READ hasTrackList)
  Q_PROPERTY(QString Identity READ identity)
  Q_PROPERTY(QString DesktopEntry READ desktopEntry)
  Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
  Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
  explicit RootAdaptor(QMprisServer *server);

  bool canQuit() const;
  bool canRaise() const;
  bool hasTrackList() const { return false; }
  QString identity() const;
  QString desktopEntry() const;
  QStringList supportedUriSchemes() const;
  QStringList supportedMimeTypes() const;

public slots:
  void Raise();
  void Quit();

private:
  QMprisServer *server;
};

} // namespace mpris

#endif // MPRISLIB_ROOTADAPTOR_HH
