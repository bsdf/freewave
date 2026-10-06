# mprislib — generic Qt6 MPRIS server. Depends only on Qt6::Core + Qt6::DBus and
# includes no Freewave headers, so this directory can be git-subtree-split into a
# standalone OSS repo without source edits.

find_package(Qt6 COMPONENTS Core DBus REQUIRED)

add_library(mprislib
  metadata.cc
  qmprisserver.cc
  rootadaptor.cc
  playeradaptor.cc
)

set_target_properties(mprislib PROPERTIES
  AUTOMOC ON
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

# Headers are referenced as "mprislib/<file>.hh"; expose the parent directory so
# both this library and its consumers resolve that prefix.
target_include_directories(mprislib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(mprislib PUBLIC
  Qt6::Core
  Qt6::DBus
)
