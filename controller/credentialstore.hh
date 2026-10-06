#ifndef CREDENTIALSTORE_HH
#define CREDENTIALSTORE_HH

#include <QString>
#include <expected>

// Wrapper over the OS secret store (QtKeychain / Secret Service). Stores per-profile
// secrets (Subsonic api_key or password, future MPD password). Service "freewave",
// entry key "<profile_id>/<field>". No plaintext fallback: failures are surfaced, never silently
// downgraded. The *_blocking variants spin a local event loop so callers on the
// UI thread (dialogs, backend construction) can read/write a secret synchronously.
//
// The error channel carries the keychain's own message rather than a bare failure
// flag: "no keyring is installed" and "the keyring refused this entry" need
// different words in front of a user, and only the backend can tell them apart.
class CredentialStore {
public:
  // Empty on success; on failure, the keychain's error text.
  using Result = std::expected<void, QString>;
  using SecretResult = std::expected<QString, QString>;

  // Fire-and-forget: drops a secret that is no longer relevant, where nothing
  // downstream depends on the outcome. A failure is logged, not reported.
  void remove(const QString &profile_id, const QString &field);

  // Blocking (spins a local QEventLoop) — for callers on the UI thread.
  auto write_blocking(const QString &profile_id, const QString &field, const QString &secret)
      -> Result;
  auto read_blocking(const QString &profile_id, const QString &field) -> SecretResult;
  auto remove_blocking(const QString &profile_id, const QString &field) -> Result;
};

#endif // CREDENTIALSTORE_HH
