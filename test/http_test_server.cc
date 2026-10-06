#include "http_test_server.hh"

#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpSocket>

HttpTestServer::HttpTestServer(QObject *parent)
  : QObject(parent)
{
  connect(&server, &QTcpServer::newConnection, this,
      &HttpTestServer::on_new_connection);
  server.listen(QHostAddress::LocalHost, 0); // ephemeral port
}

void
HttpTestServer::add_file(const QString &path, QByteArray data, Mode mode,
    qint64 partial)
{
  files.insert(path, Entry{std::move(data), mode, partial, 0});
}

void
HttpTestServer::set_mode(const QString &path, Mode mode, qint64 partial)
{
  auto it = files.find(path);
  if (it == files.end()) return;
  it->mode = mode;
  it->partial = partial;
}

void
HttpTestServer::release_stalled()
{
  auto held = std::move(stalled);
  stalled.clear();
  for (const Held &h : held)
    {
      if (!h.sock) continue;
      auto it = files.find(h.path);
      if (it != files.end() && h.next <= h.end)
        h.sock->write(it->data.constData() + h.next, h.end - h.next + 1);
      h.sock->flush();
      h.sock->disconnectFromHost();
    }
}

int
HttpTestServer::request_count(const QString &path) const
{
  auto it = files.constFind(path);
  return it == files.constEnd() ? 0 : it->served;
}

qint64
HttpTestServer::last_range_start(const QString &path) const
{
  auto it = files.constFind(path);
  return it == files.constEnd() ? -1 : it->last_range;
}

QUrl
HttpTestServer::url(const QString &path) const
{
  return QUrl(QString("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path));
}

void
HttpTestServer::on_new_connection()
{
  while (auto *sock = server.nextPendingConnection())
    {
      auto buf = std::make_shared<QByteArray>();
      connect(sock, &QTcpSocket::readyRead, sock, [this, sock, buf] {
        buf->append(sock->readAll());
        int end = buf->indexOf("\r\n\r\n");
        if (end < 0) return; // headers not complete yet
        handle(sock, buf->left(end));
      });
      connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void
HttpTestServer::handle(QTcpSocket *sock, const QByteArray &head)
{
  const QString text = QString::fromUtf8(head);
  const QStringList lines = text.split("\r\n");
  if (lines.isEmpty())
    {
      sock->disconnectFromHost();
      return;
    }

  // Request line: "GET /path?query HTTP/1.1". Files are registered by path, so
  // that a caller which builds its own query (a Subsonic stream URL carries auth
  // and a track id) still lands on a registered blob.
  const QStringList req = lines.first().split(' ');
  QString path = req.size() >= 2 ? req[1] : QString();
  if (const int q = path.indexOf('?'); q >= 0) path.truncate(q);

  auto it = files.find(path);
  if (it == files.end())
    {
      sock->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                  "Connection: close\r\n\r\n");
      sock->disconnectFromHost();
      return;
    }
  Entry &e = it.value();
  const qint64 total = e.data.size();

  // Range: bytes=START-[END]
  qint64 start = 0;
  qint64 end = total - 1;
  bool ranged = false;
  for (const QString &l : lines)
    {
      if (!l.startsWith("Range:", Qt::CaseInsensitive)) continue;
      static const QRegularExpression re("bytes=(\\d+)-(\\d*)");
      auto m = re.match(l);
      if (m.hasMatch())
        {
          ranged = true;
          start = m.captured(1).toLongLong();
          if (!m.captured(2).isEmpty()) end = m.captured(2).toLongLong();
        }
      break;
    }
  if (start < 0 || start >= total) start = 0;
  if (end < start || end >= total) end = total - 1;
  const qint64 len = end - start + 1;

  // Drop the first full-file fetch (start==0) mid-body to force one mid-stream
  // error; ranged recovery requests (start>0) and later fetches serve whole.
  const bool drop_now = e.mode == DropOnce && e.served == 0 && start == 0;
  ++e.served;
  e.last_range = ranged ? start : -1;

  QByteArray header;
  if (ranged)
    {
      header += "HTTP/1.1 206 Partial Content\r\n";
      header += QString("Content-Range: bytes %1-%2/%3\r\n")
                    .arg(start)
                    .arg(end)
                    .arg(total)
                    .toUtf8();
    }
  else
    {
      header += "HTTP/1.1 200 OK\r\n";
    }
  header += "Accept-Ranges: bytes\r\n";
  header += QString("Content-Length: %1\r\n").arg(len).toUtf8();
  header += "Content-Type: audio/ogg\r\n";
  header += "Connection: close\r\n\r\n";
  sock->write(header);

  if (drop_now)
    {
      // Declare the full length above but send only `partial` bytes, then close:
      // souphttpsrc reads a short body and posts a mid-stream stream error.
      sock->write(e.data.constData() + start, qMin(e.partial, len));
      sock->flush();
      ++drops;
      sock->disconnectFromHost();
      return;
    }

  if (e.mode == Stall)
    {
      // Header and a prefix now, the rest on release_stalled(). The socket stays
      // open, so the client sees a download that is genuinely still running.
      const qint64 sent = qMin(e.partial, len);
      if (sent > 0) sock->write(e.data.constData() + start, sent);
      sock->flush();
      stalled.append(Held{sock, path, start + sent, end});
      return;
    }

  sock->write(e.data.constData() + start, len);
  sock->flush();
  sock->disconnectFromHost();
}
