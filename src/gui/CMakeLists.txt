set(CMAKE_AUTOMOC ON)

add_executable(facial-auth-control
    main.cpp
    MainWindow.cpp
    EnrollmentPage.cpp
    SettingsPage.cpp
    RiskDisclaimerDialog.cpp
    EnrollHelperRunner.cpp
    PamIntegrationPage.cpp
    PamEnableConfirmDialog.cpp
)

target_include_directories(facial-auth-control PRIVATE "${CMAKE_BINARY_DIR}")
target_link_libraries(facial-auth-control PRIVATE facial_core Qt6::Widgets)

install(TARGETS facial-auth-control RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Desktop menu entry (System/Utility) + hicolor icon, so facial-auth-control
# shows up in the application launcher instead of being terminal-only.
install(FILES "${CMAKE_SOURCE_DIR}/config/desktop/facial-auth-control.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES "${CMAKE_SOURCE_DIR}/config/desktop/facial-auth-control.svg"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")

# Refresh the desktop/icon caches so the entry appears without a logout.
# Both tools are optional on minimal systems; a missing one is not an
# install failure, so ignore their absence/exit status.
find_program(UPDATE_DESKTOP_DATABASE_EXE update-desktop-database)
if(UPDATE_DESKTOP_DATABASE_EXE)
    install(CODE "execute_process(COMMAND \"${UPDATE_DESKTOP_DATABASE_EXE}\" \"${CMAKE_INSTALL_FULL_DATADIR}/applications\" ERROR_QUIET)")
endif()
find_program(GTK_UPDATE_ICON_CACHE_EXE gtk-update-icon-cache gtk4-update-icon-cache)
if(GTK_UPDATE_ICON_CACHE_EXE)
    install(CODE "execute_process(COMMAND \"${GTK_UPDATE_ICON_CACHE_EXE}\" -f -t \"${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor\" ERROR_QUIET)")
endif()
