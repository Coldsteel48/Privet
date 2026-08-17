add_library(pam_facial MODULE pam_facial.cpp)
set_target_properties(pam_facial PROPERTIES PREFIX "" OUTPUT_NAME "pam_facial")
target_include_directories(pam_facial PRIVATE ${PAM_INCLUDE_DIR} "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(pam_facial PRIVATE ${PAM_LIBRARY})
target_link_options(pam_facial PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/pam_facial.map")
set_target_properties(pam_facial PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/pam_facial.map")
# Deliberately no facial_core/OpenCV/libv4l2 link — verified at build time
# with `nm -D`/`ldd` per the project plan's verification section.

install(TARGETS pam_facial LIBRARY DESTINATION "${PAM_SECURITY_DIR}")

# --- Test-only variant -------------------------------------------------
# Same source, but pointed at the in-tree fake-verify test stub instead of
# the real installed facial-auth-verify, so tools/pam_test_harness can
# exercise the fork/exec/timeout/exit-code-mapping logic without a camera,
# a model, or a real enrollment. Never installed.
if(BUILD_TESTING)
    file(WRITE "${CMAKE_BINARY_DIR}/generated_test/generated/facial_auth_paths.hpp" "
#pragma once
namespace facial_auth::paths {
inline constexpr const char* kVerifyBinary = \"${CMAKE_BINARY_DIR}/tools/fake_verify/fake-verify\";
inline constexpr const char* kEnrollBinary = \"${CMAKE_BINARY_DIR}/tools/fake_verify/fake-verify\";
// pam_test_harness unsets DISPLAY/WAYLAND_DISPLAY before authenticating
// (see tools/pam_test_harness/main.cpp), so tryGuiConfirmation() always
// bails out before this path would ever be exec'd — it only needs to
// exist to satisfy the compiler.
inline constexpr const char* kConfirmGuiBinary = \"${CMAKE_BINARY_DIR}/tools/fake_verify/fake-verify\";
}  // namespace facial_auth::paths
")

    add_library(pam_facial_test MODULE pam_facial.cpp)
    set_target_properties(pam_facial_test PROPERTIES PREFIX "" OUTPUT_NAME "pam_facial_test")
    target_include_directories(pam_facial_test PRIVATE ${PAM_INCLUDE_DIR}
                                "${CMAKE_BINARY_DIR}/generated_test" "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(pam_facial_test PRIVATE ${PAM_LIBRARY})
    target_link_options(pam_facial_test PRIVATE
                         "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/pam_facial.map")
    add_dependencies(pam_facial_test fake-verify)
endif()
