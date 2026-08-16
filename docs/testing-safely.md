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

- Only a fixed allow-list of services is ever offered — `sudo` and local
  greeters (`gdm-password`, `sddm`, `lightdm`). `sshd` is not on the list
  and there is no free-text field, so it is never reachable from the GUI
  at all.
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
