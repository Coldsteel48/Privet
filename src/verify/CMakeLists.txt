add_executable(facial-auth-verify main.cpp)
target_link_libraries(facial-auth-verify PRIVATE facial_core)

install(TARGETS facial-auth-verify RUNTIME DESTINATION "${FACIAL_AUTH_LIBEXEC_DIR}")
