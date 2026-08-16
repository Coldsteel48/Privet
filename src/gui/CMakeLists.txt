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
