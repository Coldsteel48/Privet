# pam_facial

IR-camera facial authentication for Linux — a C++-only, Windows-Hello /
Howdy-style PAM module. No Python anywhere in the stack.

**Status: Phase 1 — personal/local scaffold, not yet validated against
real IR hardware.** See `docs/testing-safely.md` before enabling this on
any real login path.

## What's here

- `pam_facial.so` — a thin PAM module (`auth` only). Deliberately links
  nothing but `libpam` — no OpenCV, no V4L2. At authenticate time it
  forks and execs `facial-auth-verify`, a separate small helper that does
  the actual camera capture and face match, and only ever looks at that
  helper's exit code. This keeps the much larger face-recognition
  pipeline out of the address space of `login`/`sudo`/`sshd` — see
  `src/pam/pam_facial.cpp` for the full rationale.
- `facial-auth-verify` — does the actual capture + match. Never invoked
  directly; only ever exec'd by `pam_facial.so`.
- `facial-auth-enroll` — privileged CLI that captures your face and
  writes the enrollment to `/var/lib/facial-auth/`. Run directly as root,
  or elevated via `pkexec` by the GUI.
- `facial-auth-control` — an unprivileged Qt6 GUI for enrolling/managing
  your face and adjusting settings. Never touches privileged storage
  directly; always goes through `facial-auth-enroll` via `pkexec`.
- `src/core` (`facial_core`) — the shared library: V4L2 camera capture,
  face detection/embedding (OpenCV `objdetect`'s `FaceDetectorYN`/
  `FaceRecognizerSF`, i.e. YuNet/SFace ONNX models), embedding storage,
  matching, config, logging.

## Safety: this can never lock you out (by design)

- `pam_facial.so` returns `PAM_SUCCESS` **only** on a confident face
  match. Every other outcome — missing camera, no enrollment, a timeout,
  an internal error, an unexpected exception — resolves to
  `PAM_AUTHINFO_UNAVAIL`, which (configured correctly, see below) falls
  through to your normal password prompt.
- **You must configure it as `sufficient`, never `requisite` or
  `required`**, and keep your existing password `auth` line in place. See
  `config/pam.d/facial-auth.example`.
- Read `docs/testing-safely.md` and use `scripts/test-harness.sh` before
  adding this to any real service. Never add it to `sshd` first.
- `facial-auth-control`'s "System Login" tab can also do this for you,
  gated: it only ever offers a fixed allow-list of services (`sudo` and
  local greeters — never `sshd`, never a typed-in name — see
  `src/core/pam/PamServiceConfig.hpp`), requires a typed confirmation,
  and — enforced by the privileged helper itself, not the GUI — runs 5
  fresh recognition attempts and refuses to write anything unless at
  least 4 match. It never touches a line it didn't write itself (a
  hand-edited entry is reported, not modified), keeps a one-time backup
  of the original file, and its Disable button works even to recover from
  that hand-edited state.

## Building

See `docs/build-dependencies.md` for the exact package names on
Arch/CachyOS, Debian/Ubuntu, Fedora/RHEL-family, and openSUSE (the CMake
build itself is distro-agnostic — it discovers everything via
`pkg-config`/`find_package`, never a hardcoded path).

```sh
./models/download-models.sh        # fetches YuNet + SFace ONNX models, checksum-verified
cmake -S . -B build
cmake --build build
ctest --test-dir build             # pure-math unit tests, no camera/hardware needed
```

Or `scripts/install.sh` to do all of the above plus `cmake --install` in one
step, with distro-appropriate package names/PAM module paths for
Arch/CachyOS, Debian/Ubuntu, Fedora/RHEL-family, and openSUSE. It never
touches `/etc/pam.d` — see the script's header and the safety section
above.

## Camera setup — you need to do this first

The exact V4L2 pixel format your IR sensor uses isn't hardcoded — confirm
it before anything will work:

```sh
v4l2-ctl --list-devices
v4l2-ctl --list-formats-ext -d /dev/videoN   # for each IR-looking node
```

Then fill in `device_path`/`pixel_format` in
`/etc/facial-auth/config.conf` (see `config/facial-auth.conf.example`).
Only the `YUYV` pixel format is implemented in `V4L2Camera` right now —
`GREY`/`Y16` (the likely IR formats) are stubbed pending this
confirmation; see `src/core/camera/V4L2Camera.cpp`.

A plain RGB webcam also works (`camera_mode = rgb`), but it's an explicit
**at-your-own-risk** opt-in — no depth/liveness signal, far more
spoofable than IR. Both the CLI and the GUI require you to acknowledge
this before it's accepted.

## License

GPLv3 — see `LICENSE`.
