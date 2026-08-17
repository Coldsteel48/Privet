# pam_facial

IR-camera facial authentication for Linux — a C++-only, Windows-Hello /
Howdy-style PAM module. No Python or RUST anywhere in the stack.

**Status: Phase 1 — personal/local scaffold, not yet validated against
real IR hardware.** See `docs/testing-safely.md` before enabling this on
any real login path.

## Disclaimer

**THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND. USE OF
THIS SOFTWARE IS ENTIRELY AT YOUR OWN RISK.**

This project modifies system authentication (PAM). Misconfiguration,
a bug, or unexpected interaction with your distribution's specific setup
can, in principle, lock you out of your own machine, leave a service
less secure than before, or otherwise cause data loss, downtime, or
other damage. The safety measures described in this README and in
`docs/testing-safely.md` (the `sufficient` control flag, the confirmation
prompt, the fallback to `PAM_AUTHINFO_UNAVAIL`, and so on) are designed
in good faith to minimize that risk, but no software of this kind can
guarantee it will be free of defects, and none of those measures
constitute a warranty or a guarantee of fitness for any particular
purpose.

**By downloading, building, installing, or using this software, you
accept full responsibility for any and all consequences of doing so.**
The author(s) and contributors of this project accept **no liability
whatsoever** for any damage, data loss, security incident, lockout, or
other harm, direct or indirect, arising from the use, misuse, or
inability to use this software — including but not limited to loss of
access to your system, loss of data, or any consequential or incidental
damages — even if advised of the possibility of such damage. This
software is licensed under the GPLv3 (see `LICENSE`), which itself
disclaims all warranties in Sections 15 and 16; this section restates
that disclaimer in plain language and does not narrow it.

If you are not comfortable accepting these terms, do not use this
software — particularly do not add it to any authentication path
(`sudo`, `login`, a display manager greeter, or especially `sshd`) on a
machine you cannot afford to be locked out of. Always keep a spare,
independent way to log in (a root shell, a password fallback, physical
console access) before testing or deploying this software, as described
in `docs/testing-safely.md`.

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
- `facial-auth-enroll` — privileged CLI that records a short video while
  you turn your head, buckets the usable frames into several
  angle-tagged templates, and writes the enrollment to
  `/var/lib/facial-auth/`. Run directly as root, or elevated via
  `pkexec` by the GUI.
- `facial-auth-control` — an unprivileged Qt6 GUI for enrolling/managing
  your face and adjusting settings. Never touches privileged storage
  directly; always goes through `facial-auth-enroll` via `pkexec`.
- `src/core` (`facial_core`) — the shared library: V4L2 camera capture,
  face detection/embedding (OpenCV `objdetect`'s `FaceDetectorYN`/
  `FaceRecognizerSF`, i.e. YuNet/SFace ONNX models), embedding storage,
  matching, config, logging.

## Safety: this can never lock you out (by design)

- `pam_facial.so` never opens the camera silently: it first asks
  "Authenticate using face recognition?" and only proceeds on an explicit
  yes. Where a display is reachable (in practice: `sudo` run from an
  already-logged-in graphical session, with `DISPLAY`/`WAYLAND_DISPLAY`
  preserved — see `docs/testing-safely.md`) this is a real clickable
  Yes/No box, shown by a separate, privilege-dropped `facial-auth-confirm`
  helper rather than by `pam_facial.so` itself, which still links nothing
  but `libpam`. Everywhere else (console `login`, graphical greeters, or
  `sudo` without a preserved display) it falls back to the PAM
  conversation's text prompt, "Authenticate using face recognition?
  (y/n): ". Anything but an explicit yes — a bare Enter, "n", a closed
  dialog, no conversation/display available at all, or no answer within
  20 seconds — declines and falls through to your normal password prompt
  without ever touching the camera. The whole confirmation step runs in a
  forked child (process group) so a stuck/unresponsive front end can't
  hold the login flow open forever either. See `confirmCameraUseViaPam()`
  in `src/pam/pam_facial.cpp`.
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
  gated: it only ever offers a fixed allow-list of services (`sudo`, the
  console `login` prompt, and local greeters — never `sshd`, never a
  typed-in name — see `src/core/pam/PamServiceConfig.hpp`), requires a
  typed confirmation,
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

## Enrollment: multi-angle templates

`facial-auth-enroll` records raw frames for a fixed window (default 8s,
`enroll_video_duration_sec`) while you turn/tilt your head, then
post-processes the buffer into several angle-bucketed templates instead
of one averaged, Center-only template — a 3x3 yaw/pitch grid, self-
calibrating via tercile splits of the detector's landmark-derived
yaw/pitch ratios rather than a hardcoded angle threshold. Verification
scans every stored template per capture attempt and takes the best
match, so a live probe from any angle only needs to resemble its closest
template rather than a single frontal average — this directly raises the
per-attempt match probability, compounding with the capture-retry budget
below.

Degrades gracefully with less head motion or fewer usable frames: 12-35
usable frames falls back to yaw-only (Left/Center/Right), under 12 falls
back to a single Center template. The on-disk format bumped to v2 for
this (`EmbeddingStore::saveAll`/`loadAll`), but v1 files are still read
correctly as a single Center-tagged template — existing enrollments keep
working without a forced re-enrollment. The GUI's Enroll button relabels
itself "Re-enroll" once it learns an enrollment exists (from a successful
enroll, a "already enrolled" result, or a successful Test), rather than
being a separate button.

Each capture attempt during login (`max_capture_attempts`, default 20)
also gets a real budget rather than the original 3: on IR hardware that
strobes its illuminator on alternating frames, detection empirically
succeeds only ~25% of the time even when lit, so a small budget could
fail almost every attempt regardless of tuning. See
`config/facial-auth.conf.example` for both settings' full rationale.

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
