#ifndef HTTP_TEST_SERVER_HH
#define HTTP_TEST_SERVER_HH

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTcpServer>
#include <QUrl>

class QTcpSocket;

// Minimal localhost HTTP/1.1 file server for GStreamer souphttpsrc regression
// tests. The local-file (`tone_url`) fixtures can't model the network path:
// file reads never underrun queue2 (no D1) and never fail mid-stream (no D2).
// This serves in-memory blobs over real TCP with byte-range support so the
// engine can seek, plus a per-file "drop once" mode that truncates the first
// response mid-body (declares the full Content-Length but closes after `partial`
// bytes) to force a single mid-stream stream error — exercising the in-place
// recovery path without needing the intermittent real `souphttpsrc -5`.
//
// Runs on the caller's thread; responses are written synchronously when the
// test spins the Qt event loop (QSignalSpy::wait / QTest::qWait).
class HttpTestServer : public QObject {
  Q_OBJECT
public:
  enum Mode { Normal,
    DropOnce,
    Stall };

  explicit HttpTestServer(QObject *parent = nullptr);

  // Register `data` at `/path`. DropOnce truncates the first full-file fetch to
  // `partial` bytes then closes the socket; later fetches (and any ranged
  // recovery request) serve the blob whole. Stall sends `partial` bytes and then
  // holds the connection open until release_stalled() — a download that is
  // reliably still in flight when the test does something to it.
  void add_file(const QString &path, QByteArray data, Mode mode = Normal,
      qint64 partial = 0);
  // Change how a registered file answers, without disturbing its counters.
  void set_mode(const QString &path, Mode mode, qint64 partial = 0);

  // Finish every held response and close it.
  void release_stalled();

  quint16 port() const { return server.serverPort(); }
  QUrl url(const QString &path) const;
  int drop_count() const { return drops; }
  // Requests answered for `path` — the assertion behind "a replay went to disk,
  // not to the server".
  int request_count(const QString &path) const;
  // First byte the last request for `path` asked for, or -1 if it asked for the
  // whole file. Distinguishes a resumed download from a restarted one.
  qint64 last_range_start(const QString &path) const;

private slots:
  void on_new_connection();

private:
  struct Entry {
    QByteArray data;
    Mode mode = Normal;
    qint64 partial = 0;
    int served = 0;
    qint64 last_range = -1;
  };

  // A response whose body has been started and is waiting on release_stalled().
  struct Held {
    QPointer<QTcpSocket> sock;
    QString path;
    qint64 next = 0; // first byte not yet written
    qint64 end = 0;  // last byte of the requested range
  };

  void handle(QTcpSocket *sock, const QByteArray &head);

  QTcpServer server;
  QHash<QString, Entry> files;
  QList<Held> stalled;
  int drops = 0;
};

#endif // HTTP_TEST_SERVER_HH
