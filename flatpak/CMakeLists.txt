configure_file(
    org.xeyes.Freewave.yml.in
    org.xeyes.Freewave.yml
    @ONLY
)

find_program(FLATPAK_BUILDER flatpak-builder)
if(FLATPAK_BUILDER)
    add_custom_target(flatpak
        COMMAND ${FLATPAK_BUILDER}
            --user --install --force-clean
            ${CMAKE_CURRENT_BINARY_DIR}/flatpak-build
            ${CMAKE_CURRENT_BINARY_DIR}/org.xeyes.Freewave.yml
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Building and installing Flatpak (org.xeyes.Freewave)"
        VERBATIM
    )
else()
    message(STATUS "flatpak-builder not found — 'flatpak' target unavailable")
endif()
