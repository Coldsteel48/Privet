# Testing pam_facial safely (no lockout risk)

pam_facial is designed so that no failure mode locks you out (see the
"never lock out" design in `src/pam/pam_facial.cpp` and
`src/verify/main.cpp`), but *iterating on the code itself* is a different
risk than the finished module misbehaving — a bug, a bad build, or a
typo'd `/etc/pam.d` file can still leave a service unauthenticatable while
you're actively working on it. Follow this procedure rather than testing
directly against `login`/`sudo`/`sshd`.

## 1. Never touch a real service while iterating

Do not add `pam_facial.so` to `/etc/pam.d/sudo`, `/etc/pam.d/login`,
`/etc/pam.d/sshd`, or any display manager's greeter config until it has
been validated repeatedly via the throwaway test service below.

## 2. Keep a spare root shell open

Before touching *any* live PAM config (even the throwaway test one),
have a second terminal already logged in as root (or with a working
`sudo`) so you have a guaranteed way to undo a mistake.

## 3. Use `pam_test_harness`, not `pamtester`

`pamtester` isn't packaged in this project's supported distros' official
repositories, and AUR is intentionally not used for this project. Instead,
`tools/pam_test_harness` is a small in-tree tool that links `libpam`
directly (`pam_start`/`pam_authenticate`/`pam_end`) against a **throwaway**
PAM service, `facial-auth-test` — see `config/pam.d/facial-auth-test`.
This never touches `sudo`/`login`/`sshd`.

```
scripts/test-harness.sh [username]
```

By default this points the throwaway service at `pam_facial_test.so` — a
test-only build variant that execs the in-tree `fake-verify` stub instead
of the real `facial-auth-verify` (see `tools/fake_verify/`), so you can
exercise the fork/exec/timeout/exit-code-mapping logic in
`pam_facial.cpp` without a camera, a model, or a real enrollment:

```
FAKE_VERIFY_EXIT_CODE=0 scripts/test-harness.sh   # simulate a match       -> PAM_SUCCESS
FAKE_VERIFY_EXIT_CODE=1 scripts/test-harness.sh   # simulate a failed match -> PAM_AUTH_ERR
FAKE_VERIFY_EXIT_CODE=2 scripts/test-harness.sh   # simulate "unavailable" -> PAM_AUTHINFO_UNAVAIL
FAKE_VERIFY_SLEEP_MS=20000 scripts/test-harness.sh  # exceeds the 8s outer timeout -> killed, PAM_AUTHINFO_UNAVAIL
```

Before any of the above, `pam_facial.so` now asks a "Authenticate using
face recognition? (y/n)" Yes/No prompt over the PAM conversation and only opens the
camera on an explicit "y"/"yes" — see `confirmCameraUseViaPam()` in
`src/pam/pam_facial.cpp`. The harness auto-answers "y" so the scenarios
above still exercise the full fork/exec path non-interactively; set
`PAM_TEST_HARNESS_DECLINE=1` to instead simulate a user declining (always
falls through to `PAM_AUTHINFO_UNAVAIL`, camera never opened):

```
PAM_TEST_HARNESS_DECLINE=1 scripts/test-harness.sh   # simulate declining the prompt -> PAM_AUTHINFO_UNAVAIL, camera untouched
PAM_TEST_HARNESS_DELAY_MS=25000 scripts/test-harness.sh  # exceeds the 20s confirmation timeout -> declined, camera untouched
```

To test the real module against a real camera/enrollment instead:

```
USE_TEST_MODULE=0 scripts/test-harness.sh <your-username>
```

## 4. Test the deliberately-broken scenarios, not just the happy path

Before trusting the module at all, confirm each of these returns a
sensible result and — critically — never hangs:

- No enrollment on file for the test user
- Camera unplugged / wrong `device_path` in the config
- Wrong face presented
- `facial-auth-verify` killed/crashing (simulate via `FAKE_VERIFY_EXIT_CODE`
  set to something unexpected, or a signal)
- The outer-timeout path (`FAKE_VERIFY_SLEEP_MS` above the 8s backstop)
- The confirmation-prompt timeout path (`PAM_TEST_HARNESS_DELAY_MS` above
  the 20s confirmation deadline) — confirm it declines cleanly rather than
  hanging, and that the forked child gets reaped, not left as a zombie

## 5. Only then, add it to a real stack — locally first

Once the above has been validated repeatedly, either edit `/etc/pam.d`
by hand or use `facial-auth-control`'s "System Login" tab — both end up
doing the same thing; pick whichever you're more comfortable auditing.

### 5a. By hand

1. Add the `pam_facial.so` line to a **local, non-remote** service first
   (e.g. `sudo`, or a display manager greeter on the machine you're
   sitting at) — see `config/pam.d/facial-auth.example` for the exact
   snippet and why it must be `sufficient`, never `requisite`.
2. Keep the spare root shell from step 2 open while you test it.
3. **Never add this to `sshd` first.** A mistake in a local service is
   recoverable from the console; a mistake in `sshd` on a remote/headless
   machine can lock you out of your only way in.

### 5b. Via facial-auth-control's "System Login" tab

This does the same edit, but with the constraints baked into the
privileged helper (`facial-auth-enroll --pam-enable`, see
`src/core/pam/PamServiceConfig.hpp` and `runPamEnable` in
`src/enroll/main.cpp`) rather than left to a careful hand-edit:

- Only a fixed allow-list of services is ever offered — `sudo`, the
  console `login` prompt, and local greeters (`gdm-password`, `sddm`,
  `lightdm`). `sshd` is not on the list and there is no free-text field,
  so it is never reachable from the GUI at all.
- Clicking Enable requires a typed `CONFIRM`, plus an explicit
  acknowledgement that you have a spare way in.
- Before writing anything, the helper itself (not the GUI) runs 5 fresh
  recognition attempts and requires at least 4 to match. If that
  threshold isn't met, nothing is written — same "never lock out" logic
  as `pam_facial.so` itself, just applied one step earlier.
- The line is always inserted as `sufficient`, first in the file, above
  whatever was already there. A pre-existing `pam_facial.so` line with
  any other control flag (i.e. someone hand-edited it) is left alone and
  reported, never auto-corrected.
- The first time it ever touches a given service's file, it saves a
  `NAME.pam_facial.orig` backup alongside it and never overwrites that
  backup again.
- Disable removes any `pam_facial.so` line regardless of control flag —
  this is also the recovery path if a hand-edit left the file in a state
  the tool won't touch automatically.

Keep the spare root shell open regardless of which path you use.

## 6. Seeing the clickable Yes/No box instead of the text prompt

`pam_facial.so`'s confirmation step (`confirmCameraUseViaPam()` in
`src/pam/pam_facial.cpp`) tries a real GUI Yes/No box — a separate,
privilege-dropped `facial-auth-confirm` helper, never linked into
`pam_facial.so` itself — before falling back to the PAM conversation's
plain-text prompt. It only attempts the GUI when `DISPLAY` or
`WAYLAND_DISPLAY` is set in `pam_facial.so`'s own process environment at
that moment (`hasDisplayEnv()` in `src/core/pam/PamConfirmationPrompt.hpp`).

In practice that means:

- **Console `login`, and graphical greeters (`gdm-password`/`sddm`/
  `lightdm`, including COSMIC via greetd)**: no display is reachable at
  PAM-conversation time for an arbitrary child process to draw into, so
  the box can never appear here. This is a structural limitation, not a
  bug — patching a specific greeter's own rendering to draw the box
  itself isn't pursued here (see the plan discussion in
  `src/pam/pam_facial.cpp`'s comments). What *is* configurable is what
  happens instead — see "Choosing the confirmation behavior" below.
- **`sudo`, run from an already-logged-in graphical session**: this is
  the one place the GUI box can realistically appear, but most distros'
  default sudoers config (`Defaults env_reset`) strips `DISPLAY`/
  `WAYLAND_DISPLAY`/`XAUTHORITY`/`XDG_RUNTIME_DIR` before
  `pam_sm_authenticate` ever runs — so by default you'll still see the
  text prompt even here. To actually see the GUI box under `sudo`, add a
  drop-in preserving those variables, e.g.:

  ```
  # /etc/sudoers.d/facial-auth-gui  (edit with visudo -f, never by hand)
  Defaults env_keep += "DISPLAY WAYLAND_DISPLAY XAUTHORITY XDG_RUNTIME_DIR"
  ```

  `XDG_RUNTIME_DIR` matters on Wayland sessions specifically — without it,
  the Wayland client library has no socket to connect to, regardless of
  desktop environment. This has been confirmed working end-to-end on a
  generic Wayland session and on COSMIC; the same mechanism (Qt's stock
  xcb/wayland platform plugins, no per-DE code) applies equally under
  GNOME, KDE Plasma, XFCE, Cinnamon, and MATE, X11 or Wayland, as long as
  the Qt6 Wayland platform plugin is installed on Wayland sessions — see
  the "Qt6 Wayland platform plugin" row in `docs/build-dependencies.md`.

  This is a deliberate, manual opt-in step — `scripts/install.sh` never
  writes sudoers config for you, matching its "never auto-touch privileged
  config" policy for `/etc/pam.d` above.

`scripts/test-harness.sh` / `pam_test_harness` always exercises the
text-prompt path regardless of your desktop session: the harness clears
`DISPLAY`/`WAYLAND_DISPLAY` from its own environment before authenticating
(`tools/pam_test_harness/main.cpp`), since `pam_facial.so` is dlopen'd
in-process and would otherwise see — and try to act on — whatever display
variables are set in the terminal you ran the harness from.

### Choosing the confirmation behavior

facial-auth-control's Settings page exposes three controls (persisted as
`confirmation_mode` / `greeter_confirmation_mode` /
`confirmation_timeout_sec` in `/etc/facial-auth/config.conf` — see
`config/facial-auth.conf.example` and
`ConfirmationMode`/`GreeterConfirmationMode` in
`src/core/pam/PamConfirmationPrompt.hpp`):

- **Confirmation prompt** — the primary mode:
  - *Clickable Yes/No box (mouse, recommended)* — today's default
    behavior: try the GUI box, and where it can't appear, fall back per
    the second control below.
  - *Plain text Y/N prompt* — always use the PAM conversation's
    `(y/n)` prompt, everywhere, never attempting the GUI box even where
    it could appear (e.g. `sudo` with the env-preserving drop-in above).
  - *No confirmation — authenticate immediately* — skip asking
    altogether, everywhere; face recognition just runs. This bypasses
    `PAM_CONV` entirely, so it also works on a host process that
    provides no conversation at all.
- **At the login screen, when no box is possible** — only consulted when
  the primary mode above is *Clickable Yes/No box* and no display was
  reachable for it (console `login`, or a graphical greeter's login
  screen — including COSMIC via greetd): either fall back to the text
  prompt (default, unchanged), or skip confirmation and authenticate
  immediately in that context specifically, without changing how `sudo`
  or any other display-capable context behaves.
- **Confirmation timeout** — how long (1-300s, default 20s) pam_facial.so
  waits for an answer before treating silence as a decline. Not
  consulted when the primary mode is *No confirmation*. This field used
  to only be settable by hand-editing the config file, and — being
  absent from `Config` — was silently dropped the next time anything else
  was saved through the GUI; it's now a proper `Config` field (see
  `confirmationTimeoutSec` in `src/core/config/Config.hpp`) so it
  round-trips correctly.

This is what actually answers "can the login screen be mouse-only?" —
it can't show a mouse-driven box (no display is reachable there, see
above), but it can skip the prompt altogether by setting the second
control to *authenticate immediately*, which removes the keyboard `y` +
Enter step at the login screen entirely.
