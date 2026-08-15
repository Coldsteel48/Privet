cmake_minimum_required(VERSION 3.20)
project(pam_facial LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

include(GNUInstallDirs)
include(CTest)

option(FACIAL_BUILD_GUI "Build the Qt6 facial-auth-control GUI" ON)

find_package(PkgConfig REQUIRED)

# --- OpenCV ---------------------------------------------------------------
# Debian/Ubuntu/Fedora/openSUSE currently ship OpenCV 4.x under the
# "opencv4" pkg-config module name; this Arch/CachyOS dev machine ships
# OpenCV 5.x under "opencv5". Try the more common name first, fall back to
# the newer one, so the same CMake logic works across distros without
# per-distro branches. See docs/build-dependencies.md.
pkg_check_modules(OPENCV IMPORTED_TARGET opencv4)
set(_facial_opencv_module "opencv4")
if(NOT OPENCV_FOUND)
    pkg_check_modules(OPENCV REQUIRED IMPORTED_TARGET opencv5)
    set(_facial_opencv_module "opencv5")
endif()
if(OPENCV_VERSION VERSION_LESS "4.5.4")
    message(FATAL_ERROR
        "OpenCV ${OPENCV_VERSION} found, but pam_facial requires >= 4.5.4 "
        "(cv::FaceDetectorYN / cv::FaceRecognizerSF landed in the objdetect "
        "module at 4.5.4). Please upgrade your OpenCV development package.")
endif()
message(STATUS "Using OpenCV ${OPENCV_VERSION} (pkg-config module: ${_facial_opencv_module})")

# Link only the specific OpenCV component libraries actually used
# (core/imgproc/objdetect) rather than the full pkg-config bundle. The
# monolithic opencv4/opencv5 .pc file links ~60 component libraries
# including opencv_viz, which on some systems is a broken package (built
# against VTK that isn't installed) — pulling it in as an unused
# transitive dependency causes unrelated link failures that have nothing
# to do with this project.
set(_facial_opencv_components opencv_core opencv_imgproc opencv_objdetect)
set(FACIAL_OPENCV_LIBRARIES "")
foreach(_comp ${_facial_opencv_components})
    find_library(_facial_opencv_lib_${_comp} NAMES ${_comp} PATHS ${OPENCV_LIBRARY_DIRS})
    if(NOT _facial_opencv_lib_${_comp})
        message(FATAL_ERROR "Could not find required OpenCV component library: ${_comp}")
    endif()
    list(APPEND FACIAL_OPENCV_LIBRARIES ${_facial_opencv_lib_${_comp}})
endforeach()

# --- libv4l2 ----------------------------------------------------------------
pkg_check_modules(LIBV4L2 REQUIRED IMPORTED_TARGET libv4l2)

# --- PAM ---------------------------------------------------------------
# No pkg-config module exists for Linux-PAM on any distro; headers may be
# bundled with the base package (Arch) or a split -dev/-devel package
# (Debian/Fedora/openSUSE) — find_library/find_path work either way.
find_library(PAM_LIBRARY NAMES pam)
find_path(PAM_INCLUDE_DIR NAMES security/pam_appl.h)
if(NOT PAM_LIBRARY OR NOT PAM_INCLUDE_DIR)
    message(FATAL_ERROR
        "Linux-PAM development files not found. Install libpam0g-dev "
        "(Debian/Ubuntu), pam-devel (Fedora/openSUSE), or pam (Arch). "
        "See docs/build-dependencies.md.")
endif()

# --- Qt6 (GUI) ---------------------------------------------------------------
if(FACIAL_BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(NOT Qt6_FOUND)
        message(WARNING "Qt6 Widgets not found — facial-auth-control GUI will be skipped. "
                         "See docs/build-dependencies.md.")
        set(FACIAL_BUILD_GUI OFF)
    endif()
endif()

# --- Install locations ---------------------------------------------------
# The PAM module directory is NOT the same path on every distro (Debian
# multiarch, Fedora/openSUSE lib64, Arch lib) — default to a GNUInstallDirs
# guess but let distro packagers override it explicitly at configure time.
set(PAM_SECURITY_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/security" CACHE PATH
    "Install directory for pam_facial.so. Override for your distro, e.g. \
-DPAM_SECURITY_DIR=/usr/lib/x86_64-linux-gnu/security on Debian, \
-DPAM_SECURITY_DIR=/usr/lib64/security on Fedora/openSUSE. See docs/build-dependencies.md.")

set(FACIAL_AUTH_LIBEXEC_DIR "${CMAKE_INSTALL_FULL_LIBEXECDIR}/facial-auth" CACHE PATH
    "Install directory for the privileged facial-auth-verify/facial-auth-enroll helpers.")

# facial-auth-verify and facial-auth-enroll are execve'd by fixed, compiled-in
# absolute path (never looked up on $PATH). Generate that path as a header
# usable by src/pam (verify) and src/gui (enroll, via pkexec).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/facial_auth_paths.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/facial_auth_paths.hpp"
    @ONLY
)

add_subdirectory(src/core)
add_subdirectory(src/verify)
add_subdirectory(src/enroll)
add_subdirectory(src/pam)

if(FACIAL_BUILD_GUI)
    add_subdirectory(src/gui)
endif()

if(BUILD_TESTING)
    add_subdirectory(tools/fake_verify)
    add_subdirectory(tools/pam_test_harness)
    add_subdirectory(tests)
endif()

set(FACIAL_AUTH_ENROLL_BIN_PATH "${CMAKE_INSTALL_FULL_BINDIR}/facial-auth-enroll")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config/polkit/com.pam-facial.enroll.policy.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/com.pam-facial.enroll.policy"
    @ONLY
)

install(FILES config/pam.d/facial-auth.example config/facial-auth.conf.example
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/com.pam-facial.enroll.policy"
        DESTINATION "/usr/share/polkit-1/actions")
