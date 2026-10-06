cmake_minimum_required(VERSION 3.17)

project(freewave VERSION 0.9.3 LANGUAGES CXX)

set(FREEWAVE_GIT_SHA "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE FREEWAVE_GIT_SHA_OUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE FREEWAVE_GIT_SHA_RESULT)
  if(FREEWAVE_GIT_SHA_RESULT EQUAL 0 AND FREEWAVE_GIT_SHA_OUT)
    set(FREEWAVE_GIT_SHA "${FREEWAVE_GIT_SHA_OUT}")
  endif()
endif()

configure_file(version.hh.in version.hh @ONLY)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/")

option(BUILD_TESTS "Build tests" NO)

# Memory-safety harness: instruments the app, the in-tree libs, and every test
# target with ASan+UBSan (LSan leak checking comes with ASan on Linux). Use a
# dedicated build tree — see scripts/asan-tests.sh.
option(ENABLE_ASAN "Build with AddressSanitizer + UndefinedBehaviorSanitizer" NO)
function(fw_enable_asan target)
  if(ENABLE_ASAN)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

# Warnings on freewave's own targets only — never the FetchContent'd deps
# (spdlog/fmt/kissfft/googletest), whose headers/sources we don't control.
#
# FREEWAVE_WERROR turns those warnings into errors. Kept OFF by default so an
# ordinary/distro/Flatpak build never breaks on a warning a newer compiler
# emits; CI turns it ON, where the toolchain is pinned (GCC 14 / Qt 6.8 on
# trixie), so the zero-warning gate can't drift.
option(FREEWAVE_WERROR "Treat compiler warnings as errors on freewave targets (CI uses this)" OFF)
function(fw_enable_warnings target)
  # -Wno-missing-field-initializers: -Wextra flags designated-initializer partial
  # fills (BackendProfile{.id=…}, PipeWire's pw_stream_events{.version,.process}),
  # but unlisted fields are value-initialized to zero — a deliberate, safe idiom
  # here, not a bug. The sub-warning is pure noise for C++20 designated init.
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
  if(FREEWAVE_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

# aclistitem.cc's sizeHint has two unused Qt-override params (option/index). That
# function is off-limits (pixel-tuned grid math), so silence just that file's
# unused-parameter warning here rather than editing the signature — otherwise it
# would be the one thing blocking -Werror.
set_source_files_properties(ui/aclistitem.cc PROPERTIES COMPILE_OPTIONS -Wno-unused-parameter)

# Auto-detect available backends; user can override with -DENABLE_MPD=ON/OFF etc.
find_package(LibMPDClient QUIET)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(GSTREAMER QUIET gstreamer-1.0)
endif()
option(ENABLE_MPD "Enable MPD backend" ${LIBMPDCLIENT_FOUND})
option(ENABLE_SUBSONIC "Enable Subsonic backend" ${GSTREAMER_FOUND})
option(ENABLE_KEYCHAIN "Store credentials in the OS keychain via QtKeychain" ON)

# MPRIS D-Bus media-player adaptor — auto-enabled when Qt6::DBus is available.
find_package(Qt6 COMPONENTS DBus QUIET)
option(ENABLE_MPRIS "Expose Freewave on D-Bus via MPRIS" ${Qt6DBus_FOUND})

# Audio visualizer — auto-enabled when libpipewire is available.
if(PkgConfig_FOUND)
  pkg_check_modules(PIPEWIRE QUIET libpipewire-0.3)
endif()
option(ENABLE_VISUALIZER "Audio visualizer (PipeWire capture + spectrum)" ${PIPEWIRE_FOUND})

if(NOT ENABLE_MPD AND NOT ENABLE_SUBSONIC)
  message(FATAL_ERROR "At least one backend must be enabled (ENABLE_MPD or ENABLE_SUBSONIC)")
endif()

# Consume pre-installed spdlog/fmt/kissfft instead of FetchContent-ing them.
# For sandboxed packaging builds (Flatpak, distribution packaging), which run
# with no network and so cannot git-clone mid-configure; the packager provisions
# these from the platform. Off by default, so an ordinary build still
# fetches its own pinned copies and does not pick up whatever the system ships.
option(FREEWAVE_USE_SYSTEM_DEPS
  "Use installed spdlog/fmt/kissfft instead of fetching them (packaging builds)" OFF)

# 6.4 is the supported floor, so freewave builds on Ubuntu 24.04 LTS, which
# ships 6.4.2 and is supported to 2029. Anything newer needs a version shim
# rather than a bare call — acsortmodel.hh wraps 6.9's begin/endFilterChange
# over invalidateFilter — and QtQuick.Effects (6.5+) is unavailable, which is
# why the Now Playing cover shadow is a shader. Nothing here catches a
# violation: no CI job builds against the floor, and QML is parsed at runtime,
# so both kinds compile and pass on a newer Qt and fail only on 6.4.
find_package(Qt6 6.4 COMPONENTS Widgets Concurrent Network Test Quick Qml QuickWidgets ShaderTools REQUIRED)

if(ENABLE_KEYCHAIN)
  find_package(Qt6Keychain REQUIRED)
endif()

include(FetchSpdlog)

if(ENABLE_MPD)
  find_package(LibMPDClient REQUIRED)

  set(MPD_LIBRARY_SOURCES
    mpdlib/QMpdClient.cc
    mpdlib/song.cc
  )

  add_library(mpdlib
    ${MPD_LIBRARY_SOURCES}
  )

  target_include_directories(mpdlib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBMPDCLIENT_INCLUDE_DIR}
  )

  target_link_libraries(mpdlib PRIVATE
    ${LIBMPDCLIENT_LIBRARY}
    spdlog::spdlog
    Qt::Widgets
    Qt::Concurrent
  )

  fw_enable_asan(mpdlib)
  fw_enable_warnings(mpdlib)
endif()

set(PROJECT_SOURCES
  main.cc
  res/resources.qrc

  ui/mainwindow.cc
  ui/mainwindow.ui
  ui/albumview.ui
  ui/albumview.cc
  ui/emptylibraryview.cc

  ui/aclistview.cc
  ui/aclistmodel.cc
  ui/aclistitem.cc
  ui/acsortmodel.cc
  ui/textcover.cc
  ui/logwindow.cc

  ui/songlistmodel.cc
  ui/songlistitem.cc
  ui/songlistview.cc
  ui/favoriteheart.cc

  ui/queuelistview.cc
  ui/queuelistmodel.cc
  ui/queuelistitem.cc

  ui/playlistsview.cc
  ui/playlistitemmodel.cc
  ui/playlistsidebarmodel.cc
  ui/playlistitemdelegate.cc
  ui/playlistsidebaritem.cc
  ui/covermosaic.cc
  ui/addtoplaylistdialog.cc

  ui/theme/components.cc

  ui/playback.cc
  ui/nowplayingbadge.cc
  ui/searchbox.cc
  ui/segmentedcontrol.cc
  ui/toggle.cc
  ui/waveslider.cc
  ui/dotslider.cc
  ui/wavespinner.cc
  ui/wavepath.cc
  ui/nowplayingview.cc
  ui/nowplayingmodel.cc
  ui/themebridge.cc
  ui/settingsview.cc
  ui/onboardingdialog.cc
  ui/fwmark.cc
  ui/statewidgets.cc
  ui/loadingview.cc
  ui/connectionfailedview.cc
  ui/midsessionerrorview.cc
  ui/reconnectingview.cc
  ui/stalelibrarybanner.cc
  ui/connectionstatecontroller.cc
  ui/profileeditdialog.cc
  ui/queuepanelcontroller.cc
  ui/librarygridcontroller.cc
  ui/trayicon.cc

  controller/backendcontroller.cc
  controller/audioengine.cc
  controller/backend.cc
  controller/backendfactory.cc
  controller/albumartmanager.cc
  controller/albumcolor.cc
  controller/clockman.cc
  controller/credentialstore.cc
  controller/favoritesmanager.cc
  controller/librarymanager.cc
  controller/playlistrows.cc
  controller/playlistsmanager.cc
  controller/profilestore.cc
  controller/settings.cc

  model/album.cc

  reorder.cc
  timeutil.cc
)

add_executable(freewave
  ${PROJECT_SOURCES}
)

target_include_directories(freewave PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(freewave PRIVATE
  Qt::Widgets
  Qt::Concurrent
  Qt::Network
  Qt::Quick
  Qt::Qml
  Qt::QuickWidgets
  spdlog::spdlog
)

if(ENABLE_KEYCHAIN)
  target_link_libraries(freewave PRIVATE Qt6Keychain::Qt6Keychain)
  target_compile_definitions(freewave PRIVATE ENABLE_KEYCHAIN)
endif()

target_precompile_headers(freewave PRIVATE qtspdlog.hh)

fw_enable_asan(freewave)
fw_enable_warnings(freewave)

# Compile QML shaders to .qsb and embed them (qrc:/shaders/<name>.qsb).
qt6_add_shaders(freewave "freewave_shaders"
  PREFIX "/shaders"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/res/shaders"
  FILES
    res/shaders/wash.frag
    res/shaders/aurora.frag
    res/shaders/auroracalm.frag
    res/shaders/ridgeline.frag
    res/shaders/albumwash.frag
    res/shaders/coverfield.frag
    res/shaders/waterfall.frag
    res/shaders/covershadow.frag
)

if(ENABLE_MPD)
  target_sources(freewave PRIVATE controller/mpdmanager.cc)
  target_compile_definitions(freewave PRIVATE ENABLE_MPD)
  target_include_directories(freewave PRIVATE ${LIBMPDCLIENT_INCLUDE_DIR})
  target_link_libraries(freewave PRIVATE mpdlib ${LIBMPDCLIENT_LIBRARY})
endif()

if(ENABLE_SUBSONIC)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
  pkg_check_modules(GSTREAMER_AUDIO REQUIRED gstreamer-audio-1.0)
  target_sources(freewave PRIVATE
    controller/gstaudioengine.cc
    controller/subsonicbackend.cc
    controller/subsonicclient.cc
    controller/trackcache.cc
  )
  target_compile_definitions(freewave PRIVATE ENABLE_SUBSONIC)
  target_include_directories(freewave PRIVATE ${GSTREAMER_INCLUDE_DIRS} ${GSTREAMER_AUDIO_INCLUDE_DIRS})
  target_link_libraries(freewave PRIVATE ${GSTREAMER_LIBRARIES} ${GSTREAMER_AUDIO_LIBRARIES})
endif()

if(ENABLE_MPRIS)
  add_subdirectory(mprislib)
  fw_enable_asan(mprislib)
  fw_enable_warnings(mprislib)
  target_sources(freewave PRIVATE controller/mprisbridge.cc)
  target_compile_definitions(freewave PRIVATE ENABLE_MPRIS)
  target_link_libraries(freewave PRIVATE mprislib)
endif()

# Screensaver / display-blank inhibition while fullscreen — via the
# org.freedesktop.ScreenSaver D-Bus service. Always compiled (no-op stub without
# D-Bus); decoupled from MPRIS so it works even when that is disabled.
target_sources(freewave PRIVATE controller/screensaverinhibitor.cc)
if(Qt6DBus_FOUND)
  target_compile_definitions(freewave PRIVATE ENABLE_DBUS_INHIBIT)
  target_link_libraries(freewave PRIVATE Qt::DBus)
endif()

if(ENABLE_VISUALIZER)
  include(FetchKissFFT)
  add_subdirectory(vizlib)
  fw_enable_asan(vizlib)
  fw_enable_warnings(vizlib)
  target_sources(freewave PRIVATE controller/visualizercontroller.cc)
  target_compile_definitions(freewave PRIVATE ENABLE_VISUALIZER)
  target_link_libraries(freewave PRIVATE vizlib)
endif()

# Live shader test harness — hosts the real Now Playing QML scene and hot-reloads
# edited res/shaders/*.frag via the `qsb` tool. Standalone (no backend needed).
option(BUILD_SHADER_LAB "Build the live shader test harness" OFF)
if(BUILD_SHADER_LAB)
  add_executable(freewave_shader_lab
    shaderlab/main.cc
    shaderlab/shaderlabwindow.cc
    ui/nowplayingmodel.cc
    ui/themebridge.cc
    res/resources.qrc
  )
  target_include_directories(freewave_shader_lab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(freewave_shader_lab PRIVATE
    Qt::Widgets Qt::Quick Qt::Qml Qt::QuickWidgets)
  target_compile_definitions(freewave_shader_lab PRIVATE
    SHADER_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res/shaders")
  qt6_add_shaders(freewave_shader_lab "freewave_shader_lab_shaders"
    PREFIX "/shaders"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/res/shaders"
    FILES
      res/shaders/wash.frag
      res/shaders/aurora.frag
      res/shaders/auroracalm.frag
      res/shaders/ridgeline.frag
      res/shaders/albumwash.frag
      res/shaders/coverfield.frag
      res/shaders/waterfall.frag
  )
  if(ENABLE_VISUALIZER)
    target_sources(freewave_shader_lab PRIVATE controller/visualizercontroller.cc)
    target_compile_definitions(freewave_shader_lab PRIVATE ENABLE_VISUALIZER)
    target_link_libraries(freewave_shader_lab PRIVATE vizlib)
  endif()
endif()

include(GNUInstallDirs)
install(TARGETS freewave RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
add_subdirectory(data)
add_subdirectory(flatpak)

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
  file(GLOB_RECURSE FORMAT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hh"
  )
  add_custom_target(format
    COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES}
    COMMENT "clang-format"
    VERBATIM
  )
endif()

# Styling guardrail (Phase 5): fail if any setStyleSheet() in ui/ (outside
# ui/theme/) embeds a hardcoded hex color / font-family / font-size. The script
# needs clang-query and build/compile_commands.json (always generated, see
# CMAKE_EXPORT_COMPILE_COMMANDS). Run with: cmake --build build --target check-style-literals
find_program(CLANG_QUERY NAMES clang-query clang-query-21 clang-query-20 clang-query-19 clang-query-18)
if(CLANG_QUERY)
  add_custom_target(check-style-literals
    COMMAND ${CMAKE_COMMAND} -E env CLANG_QUERY=${CLANG_QUERY}
      ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-style-literals.sh
      ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}
    COMMENT "style-literal guardrail (clang-query)"
    VERBATIM
  )
endif()

if(BUILD_TESTS)
  include(FetchGtest)
  enable_testing()

  if(ENABLE_MPD)
    add_executable(freewave_tests
      test/test_collect_chunks.cc
      test/test_song.cc
      test/test_audio_fmt.cc
      test/test_stats.cc
    )

    target_include_directories(freewave_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${LIBMPDCLIENT_INCLUDE_DIR}
    )

    target_link_libraries(freewave_tests PRIVATE
      GTest::gtest_main
      mpdlib
      ${LIBMPDCLIENT_LIBRARY}
      spdlog::spdlog
    )

    fw_enable_asan(freewave_tests)
    fw_enable_warnings(freewave_tests)

    gtest_discover_tests(freewave_tests)

    # ---------------------------------------------------------------------------
    # Qt-aware test suite: QMpdClient, MpdManager, AlbumArtManager
    # Uses a custom main() that creates QApplication with offscreen platform.
    # ---------------------------------------------------------------------------

    add_executable(freewave_qt_tests
      test/test_main.cc
      test/test_qmpdclient.cc
      test/test_mpdmanager.cc
      test/test_albumartmanager.cc
      test/test_albumcolor.cc
      test/test_reorder.cc
      test/test_stateviews.cc
      test/test_settingsview.cc
      test/test_theme.cc
      test/test_songlistmodel.cc
      test/test_songlistitem.cc
      test/test_acsortmodel.cc
      test/test_profilestore.cc
      test/test_credentialstore.cc
      test/test_settings.cc
      test/test_backendfactory.cc
      test/test_backendcontroller.cc
      test/test_backend_albumops.cc
      test/test_connectionstatecontroller.cc
      test/test_profileeditdialog.cc
      test/test_onboardingdialog.cc
      test/test_queuepanelcontroller.cc
      test/test_librarygridcontroller.cc
      test/test_librarymanager.cc
      test/test_favoritesmanager.cc
      test/test_playlistsmanager.cc
      test/test_playlistrows.cc
      test/test_playlistmodels.cc
      test/test_timeutil.cc
      test/test_clockman.cc
      test/test_playbackview.cc
      res/resources.qrc

      # Controller and model sources referenced by the tests
      controller/backend.cc
      controller/backendfactory.cc
      controller/backendcontroller.cc
      controller/clockman.cc
      controller/credentialstore.cc
      controller/favoritesmanager.cc
      controller/mpdmanager.cc
      controller/albumartmanager.cc
      controller/albumcolor.cc
      controller/librarymanager.cc
      controller/playlistrows.cc
      controller/playlistsmanager.cc
      controller/profilestore.cc
      controller/settings.cc
      model/album.cc
      ui/songlistmodel.cc
      ui/playlistitemmodel.cc
      ui/playlistsidebarmodel.cc
      ui/acsortmodel.cc
      reorder.cc
      timeutil.cc

      # UI state widget sources
      ui/theme/components.cc
      ui/statewidgets.cc
      ui/reconnectingview.cc
      ui/stalelibrarybanner.cc
      ui/connectionfailedview.cc
      ui/midsessionerrorview.cc
      ui/loadingview.cc
      ui/connectionstatecontroller.cc
      ui/profileeditdialog.cc
      ui/queuepanelcontroller.cc
      ui/librarygridcontroller.cc
      ui/albumview.ui
      ui/albumview.cc
      ui/emptylibraryview.cc
      ui/aclistview.cc
      ui/aclistmodel.cc
      ui/aclistitem.cc
      ui/songlistitem.cc
      ui/songlistview.cc
      ui/favoriteheart.cc
      ui/queuelistview.cc
      ui/queuelistmodel.cc
      ui/queuelistitem.cc
      ui/fwmark.cc
      ui/onboardingdialog.cc
      ui/settingsview.cc
      ui/segmentedcontrol.cc
      ui/toggle.cc
      ui/wavepath.cc
      ui/waveslider.cc
      ui/wavespinner.cc
      ui/textcover.cc
      ui/playback.cc
      ui/nowplayingbadge.cc
      ui/dotslider.cc
    )

    target_include_directories(freewave_qt_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_BINARY_DIR}
      ${LIBMPDCLIENT_INCLUDE_DIR}
    )

    target_compile_definitions(freewave_qt_tests PRIVATE ENABLE_MPD)

    if(ENABLE_KEYCHAIN)
      target_compile_definitions(freewave_qt_tests PRIVATE ENABLE_KEYCHAIN)
      target_link_libraries(freewave_qt_tests PRIVATE Qt6Keychain::Qt6Keychain)
    endif()

    target_link_libraries(freewave_qt_tests PRIVATE
      GTest::gtest      # custom main() — do not use gtest_main
      mpdlib
      Qt::Widgets
      Qt::Concurrent
      Qt::Network       # onboardingdialog connection probe
      Qt::Test          # QSignalSpy
      ${LIBMPDCLIENT_LIBRARY}
      spdlog::spdlog
    )

    target_precompile_headers(freewave_qt_tests PRIVATE qtspdlog.hh)

    fw_enable_asan(freewave_qt_tests)
    fw_enable_warnings(freewave_qt_tests)

    # QT_QPA_PLATFORM=offscreen is baked into test_main.cc via qputenv(); no
    # extra CMake ENVIRONMENT property is needed for headless CI.
    gtest_discover_tests(freewave_qt_tests)
  endif()

  if(ENABLE_SUBSONIC)
    add_executable(freewave_subsonic_tests
      test/test_main.cc
      test/mock_audio_engine.cc
      test/http_test_server.cc
      test/test_gstaudioengine.cc
      test/test_gstaudioengine_integration.cc
      test/test_subsonicbackend.cc
      test/test_trackcache.cc

      controller/audioengine.cc
      controller/backend.cc
      controller/gstaudioengine.cc
      controller/settings.cc
      controller/subsonicbackend.cc
      controller/subsonicclient.cc
      controller/trackcache.cc
      controller/favoritesmanager.cc
      controller/librarymanager.cc
      model/album.cc
      reorder.cc
    )

    target_include_directories(freewave_subsonic_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${GSTREAMER_INCLUDE_DIRS}
      ${GSTREAMER_AUDIO_INCLUDE_DIRS}
    )

    target_compile_definitions(freewave_subsonic_tests PRIVATE ENABLE_SUBSONIC)

    target_link_libraries(freewave_subsonic_tests PRIVATE
      GTest::gtest
      Qt::Widgets
      Qt::Concurrent
      Qt::Network
      Qt::Test
      ${GSTREAMER_LIBRARIES}
      ${GSTREAMER_AUDIO_LIBRARIES}
      spdlog::spdlog
    )

    target_precompile_headers(freewave_subsonic_tests PRIVATE qtspdlog.hh)

    fw_enable_asan(freewave_subsonic_tests)
    fw_enable_warnings(freewave_subsonic_tests)

    file(COPY test/res DESTINATION ${CMAKE_BINARY_DIR}/test/)
    gtest_discover_tests(freewave_subsonic_tests)
  endif()

  if(ENABLE_MPRIS)
    # MPRIS tests are backend-agnostic: they exercise mprislib + the bridge
    # against a stub Backend, so they don't depend on ENABLE_MPD/ENABLE_SUBSONIC.
    add_executable(freewave_mpris_tests
      test/test_main.cc
      test/test_mpris_metadata.cc
      test/test_mpris_server.cc
      test/test_mpris_bridge.cc
      test/test_mpris_dbus.cc

      controller/mprisbridge.cc
      controller/backend.cc
      controller/librarymanager.cc
      model/album.cc
    )

    target_include_directories(freewave_mpris_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_compile_definitions(freewave_mpris_tests PRIVATE ENABLE_MPRIS)

    target_link_libraries(freewave_mpris_tests PRIVATE
      GTest::gtest      # custom main() — do not use gtest_main
      mprislib
      Qt::Widgets
      Qt::DBus
      Qt::Test          # QSignalSpy
    )

    fw_enable_asan(freewave_mpris_tests)
    fw_enable_warnings(freewave_mpris_tests)

    gtest_discover_tests(freewave_mpris_tests)
  endif()

  if(ENABLE_VISUALIZER)
    # vizlib core is Qt-free, so this suite needs no QApplication/offscreen main.
    # Linking vizlib propagates its libpipewire + kissfft link requirements.
    add_executable(freewave_viz_tests
      test/test_vizlib_ringbuffer.cc
      test/test_vizlib_spectrum.cc
      test/test_vizlib_capture.cc
    )

    target_include_directories(freewave_viz_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(freewave_viz_tests PRIVATE
      GTest::gtest_main
      vizlib
    )

    fw_enable_asan(freewave_viz_tests)
    fw_enable_warnings(freewave_viz_tests)

    gtest_discover_tests(freewave_viz_tests)
  endif()
endif()
