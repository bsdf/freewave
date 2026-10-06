#ifndef PROFILESTORE_HH
#define PROFILESTORE_HH

#include <QList>
#include <QString>
#include <QVariantMap>
#include <optional>

// One configured backend connection. params holds type-specific fields:
// mpd -> {host: QString, port: uint}; subsonic -> {url: QString,
// auth_mode: QString ("apikey"|"userpass", default "apikey"), username:
// QString (only when auth_mode is "userpass")}. Secrets (api_key, password)
// never live here — they're in the OS keychain via CredentialStore, keyed by
// profile id.
struct BackendProfile {
  QString id;
  QString type;
  QString name;
  QVariantMap params;
};

// QSettings-backed store of connection profiles + the active selection.
// Single owner of connection-config persistence. Storage format:
//   profiles/size=N ; profiles/<i>/{id,type,name,<param keys...>} ; active_profile=<id>
class ProfileStore {
public:
  auto load() const -> QList<BackendProfile>;
  void save(const QList<BackendProfile> &profiles);

  auto active_id() const -> QString;
  void set_active_id(const QString &id);
  auto active_profile() const -> std::optional<BackendProfile>;

  void add(const BackendProfile &p);
  void update(const BackendProfile &p);
  void remove(const QString &id);

  auto profile_by_id(const QString &id) const -> std::optional<BackendProfile>;
};

#endif // PROFILESTORE_HH
