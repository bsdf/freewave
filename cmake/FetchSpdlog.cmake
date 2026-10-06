# Vendor spdlog + fmt via FetchContent at pinned versions for a reproducible,
# toolchain-compatible build, rather than relying on whatever the system ships.
# Keep these versions in lockstep with the packaging manifests (flatpak/).
set(FREEWAVE_FMT_VERSION    12.1.0)
set(FREEWAVE_SPDLOG_VERSION 1.17.0)

# The floors a *system* dep must clear, deliberately lower than the fetched pins
# above. A distribution ships what it froze with, not what upstream tagged last:
# Debian stable has fmt 10.1.1 and spdlog 1.15.2, and freewave builds and passes
# its suite against both. Requiring the fetch pins here would lock every
# distribution package out for no compatibility reason.
set(FREEWAVE_FMT_MIN_VERSION    9.1.0)
set(FREEWAVE_SPDLOG_MIN_VERSION 1.12.0)

if(FREEWAVE_USE_SYSTEM_DEPS)
  # REQUIRED, not QUIET-with-fallback: in a packaging build a missing dep must
  # fail loudly here rather than silently reaching for the network and dying
  # deeper in the configure with a confusing git-clone error.
  find_package(fmt ${FREEWAVE_FMT_MIN_VERSION} REQUIRED)
  find_package(spdlog ${FREEWAVE_SPDLOG_MIN_VERSION} REQUIRED)
  return()
endif()

include(FetchContent)

# Fetch fmt first so its fmt::fmt target already exists when spdlog configures:
# spdlog only find_package(fmt)s when the target is absent, so this makes it use
# the pinned fmt below.
set(FMT_INSTALL OFF CACHE INTERNAL "")
FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG        ${FREEWAVE_FMT_VERSION}
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(fmt)

set(SPDLOG_FMT_EXTERNAL ON CACHE INTERNAL "") # use the fetched fmt, not spdlog's bundled copy
set(SPDLOG_INSTALL OFF CACHE INTERNAL "")
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v${FREEWAVE_SPDLOG_VERSION}
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)
