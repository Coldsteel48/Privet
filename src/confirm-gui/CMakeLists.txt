set(CMAKE_AUTOMOC ON)

add_executable(facial-auth-confirm main.cpp ConfirmDialog.cpp)
target_link_libraries(facial-auth-confirm PRIVATE Qt6::Widgets)

install(TARGETS facial-auth-confirm RUNTIME DESTINATION "${FACIAL_AUTH_LIBEXEC_DIR}")
