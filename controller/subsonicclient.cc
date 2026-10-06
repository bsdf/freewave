#include "subsonicclient.hh"

#include <QCryptographicHash>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSslCertificate>
#include <QUrlQuery>

#include <spdlog/spdlog.h>

namespace {

auto
net_error(const QNetworkReply *reply) -> QString
{
  auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
  if (status.isValid())
    {
      auto reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
      return reason.isEmpty() ? QString("HTTP %1").arg(status.toInt())
                              : QString("HTTP %1 %2").arg(status.toInt()).arg(reason);
    }
  // No HTTP status: nothing was served. Qt's own wording ("Host not found",
  // "Connection refused") names the cause; the enum value does not.
  return reply->errorString();
}

auto
parse_reply_json(const QByteArray &raw) -> QJsonDocument
{
  QJsonParseError err;
  auto doc = QJsonDocument::fromJson(raw, &err);
  if (err.error == QJsonParseError::NoError) return doc;

  // LMS truncates long tags at a byte limit, which can split a multi-byte
  // UTF-8 sequence; Qt's strict parser then rejects the whole response.
  // Re-encode lossily (invalid bytes -> U+FFFD) and retry so one mangled
  // string doesn't cost every song in the reply.
  doc = QJsonDocument::fromJson(QString::fromUtf8(raw).toUtf8(), &err);
  if (err.error == QJsonParseError::NoError)
    {
      spdlog::warn("SubsonicClient: response contained invalid UTF-8, "
                   "recovered via lossy re-encode");
      return doc;
    }

  spdlog::warn("SubsonicClient: JSON parse failed: {}", err.errorString());
  return {};
}

// A reply is usable iff it parses to a subsonic-response with status "ok".
// Anything else — malformed root, status absent/"failed" — means the endpoint
// did not honor the request (some bridges, e.g. Bandcamp, answer unimplemented
// endpoints with HTTP 200 + a non-Subsonic error body instead of the spec's
// error code, so checking the HTTP status alone isn't enough).
auto
reply_ok(const QJsonDocument &doc) -> bool
{
  return doc["subsonic-response"]["status"].toString() == "ok";
}

// The server's own explanation for a rejected reply. A body that isn't a
// Subsonic error envelope carries none — which is exactly the shape a bridge
// returns for an endpoint it never implemented — so substitute something true
// rather than surfacing an empty string as the error text.
auto
reply_error_message(const QJsonDocument &doc) -> QString
{
  auto msg = doc["subsonic-response"]["error"]["message"].toString();
  if (!msg.isEmpty())
    return msg;
  return QStringLiteral("The server rejected the request without explaining why.");
}

} // namespace

auto
cert_pin_accepts(const QString &pinned, const QString &fingerprint) -> bool
{
  if (pinned.isEmpty() || fingerprint.isEmpty())
    return false;
  return pinned.compare(fingerprint, Qt::CaseInsensitive) == 0;
}

SubsonicClient::SubsonicClient(QString server_url, SubsonicAuth auth, QObject *parent)
  : QObject{parent}
  , server_url{std::move(server_url)}
  , auth{std::move(auth)}
{
  network = new QNetworkAccessManager(this);
  QObject::connect(network, &QNetworkAccessManager::sslErrors,
      this, &SubsonicClient::on_ssl_errors);
  // Path only: the query string carries the credentials.
  QObject::connect(network, &QNetworkAccessManager::finished,
      this, [](QNetworkReply *reply) {
        spdlog::debug("SubsonicClient: reply {}", reply->url().path());
      });
  // Without this, a server that accepts the TCP connection but never replies
  // (or a silently-dropped SYN) leaves a request in flight forever — most
  // visibly the initial ping, which then leaves the UI on "Connecting to
  // server" indefinitely with no error and no way to retry.
  network->setTransferTimeout(15000);
}

SubsonicClient::~SubsonicClient()
{
  abort_all();
}

void
SubsonicClient::abort_all()
{
  network->disconnect(this);
  for (auto *reply : network->findChildren<QNetworkReply *>())
    {
      // Drop the finished-handlers first: abort() emits finished()
      // synchronously, and those handlers run owner code that may be
      // mid-teardown.
      reply->disconnect(this);
      reply->abort();
    }
}

auto
SubsonicClient::url(const QString &endpoint, const Params &params) const -> QUrl
{
  return build_authed_url(server_url, auth, endpoint, params);
}

auto
SubsonicClient::build_authed_url(const QString &server_url, const SubsonicAuth &auth,
    const QString &endpoint, const Params &params) -> QUrl
{
  QUrlQuery query;
  if (auth.username.isEmpty())
    {
      query.addQueryItem("apiKey", auth.api_key);
    }
  else
    {
      // Salted-token auth per the Subsonic spec: fresh salt/token per request
      // so the password never appears in a URL (these also become GStreamer
      // stream URIs and get logged).
      const auto salt = QString::number(QRandomGenerator::global()->generate64(), 16);
      const auto token = QCryptographicHash::hash(
          (auth.password + salt).toUtf8(), QCryptographicHash::Md5)
                             .toHex();
      query.addQueryItem("u", auth.username);
      query.addQueryItem("t", token);
      query.addQueryItem("s", salt);
    }
  // Deliberately below the newest spec level. A server rejects any client
  // whose `v` exceeds its own (Subsonic error 30) *before* it even looks at
  // auth, and Airsonic-Advanced caps out at 1.15.0 — sending 1.16.0 made every
  // request, ping included, fail as "Incompatible ... protocol version" with no
  // way for the user to connect. Nothing freewave calls needs 1.16: all of its
  // endpoints exist by 1.13, as does salted-token auth. Verified live that
  // 1.15.0 changes nothing on LMS/Navidrome/gonic. Only raise this if an
  // endpoint freewave actually uses requires it.
  query.addQueryItem("v", "1.15.0");
  query.addQueryItem("c", "freewave");
  query.addQueryItem("f", "json");

  for (const auto &[key, val] : params)
    query.addQueryItem(key, val);

  QUrl url(QString("%1/rest/%2").arg(server_url, endpoint));
  url.setQuery(query);
  return url;
}

void
SubsonicClient::get(const QString &endpoint, const Params &params, Handler done,
    bool binary_reply)
{
  get_url(url(endpoint, params), std::move(done), binary_reply);
}

void
SubsonicClient::get_url(const QUrl &request_url, Handler done, bool binary_reply)
{
  auto *reply = network->get(QNetworkRequest(request_url));

  QObject::connect(reply, &QNetworkReply::finished, this,
      [reply, done = std::move(done), binary_reply] {
        reply->deleteLater();

        SubsonicReply out;
        out.transport_ok = reply->error() == QNetworkReply::NoError;
        if (!out.transport_ok)
          {
            out.error = net_error(reply);
            done(out);
            return;
          }

        out.body = reply->readAll();
        if (binary_reply)
          {
            out.ok = true;
            done(out);
            return;
          }

        out.doc = parse_reply_json(out.body);
        out.ok = reply_ok(out.doc);
        if (!out.ok)
          {
            out.error = reply_error_message(out.doc);
            out.error_code = out.doc["subsonic-response"]["error"]["code"].toInt();
          }
        done(out);
      });
}

void
SubsonicClient::on_ssl_errors(QNetworkReply *reply, const QList<QSslError> &errors)
{
  const QSslCertificate cert = reply->sslConfiguration().peerCertificate();
  if (cert.isNull())
    return; // nothing to identify the server by; let the request fail

  const QString fingerprint
      = QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());

  if (cert_pin_accepts(pinned_cert, fingerprint))
    {
      // The user pinned this exact certificate, so the certificate itself — not
      // a CA's say-so — is what identifies the server. Any renewal changes the
      // fingerprint and lands back in the prompt below rather than here.
      reply->ignoreSslErrors(errors);
      return;
    }

  if (cert_prompted)
    return;
  cert_prompted = true;

  QStringList reasons;
  for (const QSslError &e : errors)
    reasons << e.errorString();

  spdlog::warn("SubsonicClient: untrusted certificate for {}: {}",
      reply->url().host().toStdString(), reasons.join("; ").toStdString());

  emit certificate_untrusted(reply->url().host(), fingerprint,
      cert.issuerDisplayName(), reasons.join("\n"));
}
