include(GNUInstallDirs)

configure_file(
    org.xeyes.Freewave.metainfo.xml.in
    org.xeyes.Freewave.metainfo.xml
    @ONLY
)

install(FILES org.xeyes.Freewave.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.xeyes.Freewave.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

install(FILES org.xeyes.Freewave.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
