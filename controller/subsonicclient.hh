#ifndef SUBSONICCLIENT_HH
#define SUBSONICCLIENT_HH

#include <functional>

#include <QJsonDocument>
#include <QJsonValue>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QSslError>
#include <QString>
#include <QUrl>

class QNetworkReply;

// Whether a pinned fingerprint accepts the certificate the server presented.
// An empty pin must never accept — that is the whole difference between "the user
// trusted this certificate" and "nothing has been trusted". Hex case is not
// meaningful, so the comparison ignores it.
auto cert_pin_accepts(const QString &pinned, const QString &fingerprint) -> bool;

// Either the legacy apiKey= query param (LMS extension, not in the Subsonic
// spec) or username/password, salted-token auth (u/t/s) per the real spec.
// username non-empty selects the latter.
struct SubsonicAuth {
  QString api_key;
  QString username;
  QString password;
};

// One answered request, normalized. The two flags are deliberately separate:
// several decisions in the app turn on telling "the server answered and refused"
// apart from "the request never got through", and a single success bool loses
// exactly that distinction.
struct SubsonicReply {
  // The reply arrived intact — no network or HTTP-level failure. Says nothing
  // about whether the server honored the request.
  bool transport_ok = false;
  // transport_ok, and the body is a subsonic-response with status "ok". The only
  // condition under which `doc` carries usable payload.
  bool ok = false;
  QJsonDocument doc;
  QByteArray body; // raw, for non-JSON endpoints (getCoverArt)
  // Failure text, empty when ok: the HTTP/network error, else the server's own
  // message, else a stand-in for a body that explains nothing.
  QString error;
  // The Subsonic error code from a rejected reply's envelope (spec: 40 wrong
  // username or password, 44 invalid API key, ...). 0 when the reply was
  // honored, when the failure was below the Subsonic layer, or when the body
  // carried no code — none of which are distinguishable from code 0's own
  // "generic error", and none of which any caller treats differently.
  int error_code = 0;

  // Value at subsonic-response.<key>, the shape every endpoint nests its
  // payload in.
  auto payload(const QString &key) const -> QJsonValue
  {
    return doc["subsonic-response"][key];
  }
};

// The transport layer for a Subsonic server: URL building and auth, TLS trust,
// and turning a QNetworkReply into a SubsonicReply. It holds no domain state —
// nothing here knows what an album or a playlist is — so what a *server* can do
// (capabilities) lives in SubsonicBackend above it, not here.
class SubsonicClient : public QObject {
  Q_OBJECT
public:
  using Params = QList<QPair<QString, QString>>;
  using Handler = std::function<void(const SubsonicReply &)>;

  explicit SubsonicClient(QString server_url, SubsonicAuth auth,
      QObject *parent = nullptr);
  ~SubsonicClient();

  // Build a fully-authed Subsonic request URL (salted-token or apiKey, plus the
  // pinned protocol version and json format). Static so callers outside a client
  // instance — the onboarding connection probe — can build a request identical
  // to the ones the library load issues.
  static auto build_authed_url(const QString &server_url, const SubsonicAuth &auth,
      const QString &endpoint, const Params &params = {}) -> QUrl;

  // Authed URL for this client's server. Public because stream URLs are handed
  // to the audio engine rather than fetched here.
  auto url(const QString &endpoint, const Params &params = {}) const -> QUrl;

  auto server() const -> QString { return server_url; }
  // apiKey rather than username/password. The distinction is transport's, but
  // the connect handshake has to know: only apiKey profiles need the server to
  // advertise the apiKeyAuthentication extension.
  auto uses_api_key() const -> bool { return auth.username.isEmpty(); }

  // binary_reply skips JSON parsing and the Subsonic envelope check entirely
  // (getCoverArt: the body is image bytes, not a subsonic-response). transport_ok
  // is still set; ok is true whenever transport_ok is, since there is no envelope
  // to fail.
  void get(const QString &endpoint, const Params &params, Handler done,
      bool binary_reply = false);
  void get(const QString &endpoint, Handler done) { get(endpoint, {}, std::move(done)); }
  // For a URL the caller already built (the playlist mutations assemble theirs
  // from repeated same-name params).
  void get_url(const QUrl &url, Handler done, bool binary_reply = false);

  // SHA-256 the user pinned for this server via the trust-on-first-use prompt.
  // Empty means only CA-verifiable certificates are accepted.
  void set_pinned_certificate(QString sha256) { pinned_cert = std::move(sha256); }
  auto has_pinned_certificate() const -> bool { return !pinned_cert.isEmpty(); }

  // Drop handlers and abort everything in flight. Callers whose reply handlers
  // touch state that is being torn down must call this before that state goes:
  // abort() emits finished() synchronously.
  void abort_all();

signals:
  void certificate_untrusted(const QString &host, const QString &fingerprint,
      const QString &issuer, const QString &details);

private:
  void on_ssl_errors(QNetworkReply *reply, const QList<QSslError> &errors);

  QString server_url;
  SubsonicAuth auth;
  QString pinned_cert;
  // One prompt per client: without this every in-flight request would raise its
  // own dialog for the same certificate.
  bool cert_prompted = false;
  QNetworkAccessManager *network;
};

#endif // SUBSONICCLIENT_HH
