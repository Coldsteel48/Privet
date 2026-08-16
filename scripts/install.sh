#!/usr/bin/env bash
# scripts/install.sh — build and install pam_facial from a cloned checkout
# on Arch/CachyOS/Manjaro, Debian/Ubuntu, Fedora/RHEL-family, or openSUSE.
#
# This is a BUILD-AND-INSTALL script, not a PAM-wiring script: it never
# touches /etc/pam.d. Per README.md's "Safety: this can never lock you
# out" section and docs/testing-safely.md, wiring pam_facial.so into a
# real login path (sudo/login/a greeter/sshd) is a deliberate, manual,
# tested step you take yourself, only after validating with
# scripts/test-harness.sh — automating that step here would undermine the
# exact guarantee this project is built around. This script stops at
# "installed and ready to test" and prints the manual next steps.
#
# Also never touched: package installation only ever uses each distro's
# official repositories — no AUR, by project policy (see
# docs/build-dependencies.md).
#
# Usage (env vars, matching scripts/test-harness.sh's style):
#   scripts/install.sh
#   SKIP_DEPS=1 scripts/install.sh     # don't install OS packages (assume already present)
#   NO_GUI=1 scripts/install.sh        # skip building/launching the Qt6 facial-auth-control GUI
#   ASSUME_YES=1 scripts/install.sh    # non-interactive package-manager install
#   BUILD_DIR=build scripts/install.sh # build directory (default: build)
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    sed -n '2,23p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
fi

SKIP_DEPS="${SKIP_DEPS:-0}"
NO_GUI="${NO_GUI:-0}"
ASSUME_YES="${ASSUME_YES:-0}"
BUILD_DIR="${BUILD_DIR:-build}"

if [[ "$(id -u)" -eq 0 ]]; then
    echo "Don't run this script itself as root/sudo — it calls sudo only for" >&2
    echo "the specific steps that need it (package install, cmake --install," >&2
    echo "/etc and /var/lib writes). Run it as your normal user." >&2
    exit 1
fi

# --- Distro detection --------------------------------------------------
# Best-effort match on /etc/os-release ID + ID_LIKE. Used for both the
# package-manager step and the PAM_SECURITY_DIR guess below — the PAM
# module install directory is not the same path on every distro, see
# docs/build-dependencies.md.
DISTRO_FAMILY="unknown"
if [[ -f /etc/os-release ]]; then
    . /etc/os-release
    case "${ID:-} ${ID_LIKE:-}" in
        *arch*|*cachyos*|*manjaro*) DISTRO_FAMILY="arch" ;;
        *debian*|*ubuntu*)          DISTRO_FAMILY="debian" ;;
        *fedora*|*rhel*|*centos*|*rocky*|*alma*) DISTRO_FAMILY="fedora" ;;
        *opensuse*|*suse*)          DISTRO_FAMILY="opensuse" ;;
    esac
fi

# --- 1. Install OS packages ------------------------------------------------
if [[ "$SKIP_DEPS" == "1" ]]; then
    echo "SKIP_DEPS=1: not installing OS packages."
else
    case "$DISTRO_FAMILY" in
        arch)
            pac_flags=(-S --needed)
            [[ "$ASSUME_YES" == "1" ]] && pac_flags+=(--noconfirm)
            sudo pacman "${pac_flags[@]}" base-devel cmake pkgconf opencv v4l-utils pam qt6-base polkit
            ;;
        debian)
            apt_flags=(install)
            [[ "$ASSUME_YES" == "1" ]] && apt_flags+=(-y)
            sudo apt-get update
            sudo apt-get "${apt_flags[@]}" build-essential cmake pkg-config libopencv-dev \
                libv4l-dev libpam0g-dev qt6-base-dev policykit-1
            ;;
        fedora)
            dnf_flags=(install)
            [[ "$ASSUME_YES" == "1" ]] && dnf_flags+=(-y)
            sudo dnf "${dnf_flags[@]}" gcc-c++ cmake pkgconfig opencv-devel libv4l-devel \
                pam-devel qt6-qtbase-devel polkit
            ;;
        opensuse)
            zyp_flags=(install)
            [[ "$ASSUME_YES" == "1" ]] && zyp_flags+=(-y)
            sudo zypper "${zyp_flags[@]}" patterns-devel-C-C++-devel_C_C++ cmake pkgconf \
                opencv-devel libv4l-devel pam-devel qt6-base-devel polkit
            ;;
        *)
            echo "Could not identify your distro from /etc/os-release (ID='${ID:-}' ID_LIKE='${ID_LIKE:-}')." >&2
            echo "Install the packages listed in docs/build-dependencies.md yourself, then re-run" >&2
            echo "this script with SKIP_DEPS=1." >&2
            exit 1
            ;;
    esac
fi

# --- 2. Fetch models ---------------------------------------------------
./models/download-models.sh
YUNET_FILE="models/face_detection_yunet_2023mar.onnx"
SFACE_FILE="models/face_recognition_sface_2021dec.onnx"

# --- 3. Configure + build -----------------------------------------------
# CMAKE_INSTALL_PREFIX=/usr rather than CMake's default /usr/local: this
# installs a PAM module and a polkit action, both system integration
# points that on every mainstream distro are expected under /usr (the
# polkit action file is in fact always installed to the hardcoded
# /usr/share/polkit-1/actions regardless of prefix — see CMakeLists.txt —
# so a mismatched /usr/local prefix here would leave facial-auth-enroll's
# real path and the path polkit knows about looking inconsistent).
PAM_SECURITY_DIR=""
case "$DISTRO_FAMILY" in
    arch)     PAM_SECURITY_DIR="/usr/lib/security" ;;
    debian)
        triplet="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || true)"
        if [[ -n "$triplet" ]]; then
            PAM_SECURITY_DIR="/usr/lib/${triplet}/security"
        else
            echo "Warning: dpkg-architecture unavailable, guessing x86_64-linux-gnu for" >&2
            echo "PAM_SECURITY_DIR — verify this matches your arch after install." >&2
            PAM_SECURITY_DIR="/usr/lib/x86_64-linux-gnu/security"
        fi
        ;;
    fedora)   PAM_SECURITY_DIR="/usr/lib64/security" ;;
    opensuse) PAM_SECURITY_DIR="/usr/lib64/security" ;;
    *)
        echo "Unknown distro: leaving PAM_SECURITY_DIR to CMake's GNUInstallDirs guess." >&2
        echo "Confirm afterward that pam_facial.so landed somewhere already on your" >&2
        echo "system's PAM module search path (check /etc/pam.d/common-auth or" >&2
        echo "similar for how other .so modules there are referenced)." >&2
        ;;
esac

cmake_args=(-S . -B "$BUILD_DIR" -DCMAKE_INSTALL_PREFIX=/usr)
[[ -n "$PAM_SECURITY_DIR" ]] && cmake_args+=(-DPAM_SECURITY_DIR="$PAM_SECURITY_DIR")
[[ "$NO_GUI" == "1" ]] && cmake_args+=(-DFACIAL_BUILD_GUI=OFF)

cmake "${cmake_args[@]}"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# Pure-math unit tests, no camera/hardware needed — a failure here means
# something is genuinely broken, not a hardware/environment issue.
ctest --test-dir "$BUILD_DIR" --output-on-failure

# --- 4. Install --------------------------------------------------------
sudo cmake --install "$BUILD_DIR"

# --- 5. Runtime config + models -----------------------------------------
sudo install -d -m 0755 /etc/facial-auth /etc/facial-auth/models
sudo install -d -m 0700 /var/lib/facial-auth

if [[ -f /etc/facial-auth/config.conf ]]; then
    echo "Existing /etc/facial-auth/config.conf left untouched."
else
    sudo install -m 0644 config/facial-auth.conf.example /etc/facial-auth/config.conf
    echo "Installed /etc/facial-auth/config.conf from the example template."
fi

# Renamed to match Config.hpp's defaults (detector_model_path/embedder_model_path).
sudo install -m 0644 "$YUNET_FILE" /etc/facial-auth/models/face_detection_yunet.onnx
sudo install -m 0644 "$SFACE_FILE" /etc/facial-auth/models/face_recognition_sface.onnx

# --- 6. Next steps -------------------------------------------------------
cat <<'EOF'

============================================================================
Installed. This did NOT touch /etc/pam.d — that step is manual, on
purpose (see README.md's "Safety: this can never lock you out" section).

Before this can actually authenticate anyone, in order:

1. Find your IR camera node and confirm its pixel format:
     v4l2-ctl --list-devices
     v4l2-ctl --list-formats-ext -d /dev/videoN
   Then set device_path/pixel_format in /etc/facial-auth/config.conf
   (prefer a /dev/v4l/by-id/... path — see the comments in that file).

2. Enroll your face:
     sudo facial-auth-enroll
   or via the GUI: facial-auth-control (Enrollment tab).

3. Validate against the THROWAWAY test service before touching any real
   one — read docs/testing-safely.md first, then:
     scripts/test-harness.sh
     USE_TEST_MODULE=0 scripts/test-harness.sh <your-username>   # real camera

4. Only after that passes repeatedly, add pam_facial.so to a real,
   LOCAL, non-remote service. Either:
     - by hand: see config/pam.d/facial-auth.example for the exact
       "sufficient" snippet (never "requisite"/"required", and never
       remove the existing password auth line under it); or
     - via facial-auth-control's "System Login" tab, which offers only a
       fixed allow-list (sudo + local greeters), requires a typed
       confirmation, and won't write anything unless 4 of 5 fresh
       recognition attempts match first.
   Never add it to sshd first — it's intentionally not offered in the GUI
   either. See docs/testing-safely.md.
============================================================================
EOF

# --- 7. Launch the GUI ---------------------------------------------------
if [[ "$NO_GUI" != "1" ]]; then
    if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
        echo "Launching facial-auth-control..."
        nohup facial-auth-control >/dev/null 2>&1 &
        disown
    else
        echo "No graphical session detected (\$DISPLAY/\$WAYLAND_DISPLAY unset)." >&2
        echo "Run 'facial-auth-control' yourself once you're in one." >&2
    fi
fi
