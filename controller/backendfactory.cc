#include "backendfactory.hh"

#include "backend.hh"
#include "profilestore.hh"
#include "settings.hh"

#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <spdlog/spdlog.h>

#ifdef ENABLE_MPD
#include "mpdmanager.hh"
#endif
#ifdef ENABLE_SUBSONIC
#include "subsonicbackend.hh"
#endif
#ifdef ENABLE_KEYCHAIN
#include "credentialstore.hh"
#endif

auto
BackendFactory::create(const BackendProfile &profile,
    std::shared_ptr<LibraryManager> libman)
    -> std::shared_ptr<Backend>
{
#ifdef ENABLE_MPD
  if (profile.type == "mpd")
    {
      auto h = profile.params.value("host", "localhost").toString();
      auto p = static_cast<uint16_t>(profile.params.value("port", 6600u).toUInt());
      return std::make_shared<MpdManager>(libman, h, p);
    }
#endif
#ifdef ENABLE_SUBSONIC
  if (profile.type == "subsonic")
    {
      auto url = profile.params.value("url").toString();
      if (url.isEmpty())
        url = qEnvironmentVariable("SUBSONIC_URL", "http://localhost:4533");

      const auto auth_mode = profile.params.value("auth_mode", "apikey").toString();

      SubsonicAuth auth;
      if (auth_mode == "userpass")
        {
          auth.username = profile.params.value("username").toString();
          if (auth.username.isEmpty())
            auth.username = qEnvironmentVariable("SUBSONIC_USERNAME", "");
#ifdef ENABLE_KEYCHAIN
          CredentialStore cs;
          if (auto secret = cs.read_blocking(profile.id, "password"))
            auth.password = *secret;
#endif
          if (auth.password.isEmpty())
            auth.password = qEnvironmentVariable("SUBSONIC_PASSWORD", "");
        }
      else
        {
#ifdef ENABLE_KEYCHAIN
          CredentialStore cs;
          if (auto secret = cs.read_blocking(profile.id, "api_key"))
            auth.api_key = *secret;
#endif
          if (auth.api_key.isEmpty())
            auth.api_key = qEnvironmentVariable("SUBSONIC_API_KEY", "");
        }

      auto backend = std::make_shared<SubsonicBackend>(url, auth, libman, profile.id);
      backend->set_pinned_certificate(profile.params.value("cert_sha256").toString());
      Settings settings;
      auto *cache = new TrackCache{
          QDir{QStandardPaths::writableLocation(QStandardPaths::CacheLocation)},
          static_cast<qint64>(settings.audio_cache_budget_mb()) * 1024 * 1024};
      cache->set_enabled(settings.audio_cache_enabled());
      backend->set_track_cache(cache);
      return backend;
    }
#endif
  spdlog::error("BackendFactory: profile type '{}' not available in this build",
      profile.type.toStdString());
  return nullptr;
}
