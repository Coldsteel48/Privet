configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/control_path.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/control_path.hpp"
    @ONLY
)

add_executable(fake-verify main.cpp)
target_include_directories(fake-verify PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
# Test-only tool, never installed.
