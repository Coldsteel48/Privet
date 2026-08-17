# Build dependencies by distro

pam_facial's CMake build discovers everything via `pkg-config`/
`find_package`/`find_library` — never a hardcoded path or package name —
so the same `CMakeLists.txt` works across distros; only the package you
install beforehand differs. See the top-level `CMakeLists.txt` for the
`opencv4`→`opencv5` pkg-config fallback (Debian/Ubuntu/Fedora/openSUSE
currently ship OpenCV 4.x; Arch/CachyOS has moved to 5.x) and the
`PAM_SECURITY_DIR` cache variable (the PAM module install directory is
not the same path on every distro).

| Dependency | Arch/CachyOS | Debian/Ubuntu | Fedora/RHEL-family | openSUSE |
|---|---|---|---|---|
| Build tools | `base-devel cmake pkgconf` | `build-essential cmake pkg-config` | `gcc-c++ cmake pkgconfig` | `patterns-devel-C-C++-devel_C_C++ cmake pkgconf` |
| OpenCV (dnn/objdetect) | `opencv` (pkg-config module `opencv5`) | `libopencv-dev` (module `opencv4`) | `opencv-devel` (module `opencv4`) | `opencv-devel` (module `opencv4`) |
| V4L2 | `v4l-utils` | `libv4l-dev` | `libv4l-devel` | `libv4l-devel` |
| PAM headers | `pam` (bundled, no split) | `libpam0g-dev` | `pam-devel` | `pam-devel` |
| Qt6 Widgets | `qt6-base` | `qt6-base-dev` | `qt6-qtbase-devel` | `qt6-base-devel` |
| Qt6 Wayland platform plugin | `qt6-wayland` | `qt6-wayland` | `qt6-qtwayland` | `qt6-wayland` |
| polkit | `polkit` | `policykit-1` (or `polkitd`) | `polkit` | `polkit` |

The Wayland platform plugin isn't linked against at build time (Qt loads
it as a runtime plugin), but without it installed, `facial-auth-confirm`
(see `src/confirm-gui/`) and `facial-auth-control` silently fail to draw
under any Wayland session — GNOME, KDE Plasma, Cinnamon, COSMIC, Sway,
Hyprland, etc. — even though the build itself only needs Qt6 Widgets.

Minimum OpenCV version is **4.5.4** (when `cv::FaceDetectorYN` /
`cv::FaceRecognizerSF` landed in the `objdetect` module) — the CMake
configure step fails with a clear message if your OpenCV is older.

## Install commands

**Arch / CachyOS / Manjaro:**
```sh
sudo pacman -S base-devel cmake pkgconf opencv v4l-utils pam qt6-base qt6-wayland polkit
```

**Debian / Ubuntu / derivatives:**
```sh
sudo apt install build-essential cmake pkg-config libopencv-dev libv4l-dev \
                  libpam0g-dev qt6-base-dev qt6-wayland policykit-1
```

**Fedora / RHEL / CentOS Stream / Rocky / Alma:**
```sh
sudo dnf install gcc-c++ cmake pkgconfig opencv-devel libv4l-devel \
                  pam-devel qt6-qtbase-devel qt6-qtwayland polkit
```

**openSUSE:**
```sh
sudo zypper install patterns-devel-C-C++-devel_C_C++ cmake pkgconf \
                     opencv-devel libv4l-devel pam-devel qt6-base-devel qt6-wayland polkit
```

## `PAM_SECURITY_DIR` — set this if the CMake default guesses wrong

The default (`${CMAKE_INSTALL_FULL_LIBDIR}/security`, via `GNUInstallDirs`)
is usually right, but PAM module directories vary by distro. If
`pam_facial.so` installs to the wrong place, override at configure time:

| Distro | Typical value |
|---|---|
| Arch/CachyOS | `/usr/lib/security` |
| Debian/Ubuntu (x86_64) | `/usr/lib/x86_64-linux-gnu/security` |
| Fedora/RHEL-family (x86_64) | `/usr/lib64/security` |
| openSUSE (x86_64) | `/usr/lib64/security` |

```sh
cmake -S . -B build -DPAM_SECURITY_DIR=/usr/lib/x86_64-linux-gnu/security
```

Distro packagers building `.deb`/`.rpm`/`PKGBUILD` files (Phase 2 — not
built yet, see the project plan) are expected to pass the correct value
explicitly for their target rather than relying on the guess.

## AUR

This project does not use the AUR (Arch User Repository) for build
dependencies or tooling, by explicit project policy — everything above
comes from each distro's official repositories.
