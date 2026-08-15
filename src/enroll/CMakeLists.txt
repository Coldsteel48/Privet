add_executable(facial-auth-enroll main.cpp)
target_link_libraries(facial-auth-enroll PRIVATE facial_core)

install(TARGETS facial-auth-enroll RUNTIME DESTINATION "${FACIAL_AUTH_LIBEXEC_DIR}")
# Also on $PATH for direct admin use and for the GUI's pkexec invocation.
install(TARGETS facial-auth-enroll RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
