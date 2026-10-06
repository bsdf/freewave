# vizlib — backend-agnostic audio visualizer core. Captures the default PipeWire
# sink monitor and runs its own spectrum DSP. Depends only on libpipewire-0.3 +
# KissFFT and includes no Qt or Freewave headers, so this directory can be
# git-subtree-split into a standalone OSS repo without source edits.

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)

add_library(vizlib
  pipewirecapture.cc
  spectrumanalyzer.cc
)

set_target_properties(vizlib PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

# Headers are referenced as "vizlib/<file>.hh"; expose the parent directory so
# both this library and its consumers resolve that prefix.
target_include_directories(vizlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_include_directories(vizlib PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
target_compile_options(vizlib PRIVATE ${PIPEWIRE_CFLAGS_OTHER})

target_link_libraries(vizlib
  PRIVATE ${PIPEWIRE_LIBRARIES} kissfft::kissfft
)
