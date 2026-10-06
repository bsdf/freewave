#ifndef BACKENDFACTORY_HH
#define BACKENDFACTORY_HH

#include <memory>

class Backend;
class LibraryManager;
struct BackendProfile;

// Builds a Backend from a BackendProfile, resolving secrets from the OS keychain.
// Returns nullptr if the profile's type is not compiled into this build.
class BackendFactory {
public:
  static auto create(const BackendProfile &profile,
      std::shared_ptr<LibraryManager> libman)
      -> std::shared_ptr<Backend>;
};

#endif // BACKENDFACTORY_HH
