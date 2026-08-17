# TODO

## Hardcoded values audit (2026-08-17)

Ranked most-actionable first. See conversation/memory for full audit context.

1. **Face-detector thresholds bypass Config entirely** —
   `src/core/face/FaceDetector.hpp:24-25`: `scoreThreshold=0.9f`,
   `nmsThreshold=0.3f`, `inputSize=320x320` are compile-time defaults, not
   read from Config/config.conf. Only `detectorModelPath` is configurable.
   More consequential than `match_threshold` (which is already
   configurable) since it gates whether a frame counts as a detected face
   at all. Promote to Config (e.g. `detector_score_threshold`,
   `detector_nms_threshold`).

2. **Dead code caused by #1** — `src/enroll/main.cpp:42,532`:
   `kMinDetectionScore = 0.85f` check is unreachable because the detector
   itself (see #1) already rejects anything scoring below 0.9. Fix
   alongside #1.

3. **Confirmation prompt text duplicated, not configurable** —
   `src/confirm-gui/ConfirmDialog.cpp:18` and
   `src/pam/pam_facial.cpp:153` each hardcode their own copy of
   "Authenticate using face recognition?" independently, no shared
   constant, no config key. `confirmation_mode` /
   `greeter_confirmation_mode` / `confirmation_timeout_sec` make the
   *behavior* configurable but not the *wording*.

4. **`/etc/facial-auth/config.conf` path re-typed 9+ times** instead of
   reusing `facial_auth::kFacialAuthConfigPath` (defined once in
   `src/core/pam/PamConfirmationPrompt.hpp:33`, used only there).
   Other readers with the raw literal:
   `src/core/verify/VerifyRunner.cpp:20,22`,
   `src/enroll/main.cpp:347,434,436,449,452`,
   `src/gui/EnrollmentPage.cpp:138,140,252`,
   `src/gui/SettingsPage.cpp:110,124`. Should be a single shared constant,
   probably relocated out of the PAM-specific header since non-PAM
   binaries have no reason to depend on it.

5. **`EmbeddingStore` storage directory has no Config key** —
   `src/core/storage/EmbeddingStore.hpp:28`: defaults to
   `/var/lib/facial-auth`, unlike `detectorModelPath`/`embedderModelPath`
   which are proper Config fields. All call sites use the zero-arg
   default. Candidate for promotion to Config, or a deliberate decision
   to leave it fixed.

6. **RGB-camera risk disclaimer text duplicated CLI vs GUI** (lower
   priority, already self-documented as "kept in sync manually for now")
   — `src/enroll/main.cpp:170-188` (`confirmRgbRisk`) vs
   `src/gui/RiskDisclaimerDialog.cpp:9-35`.

### Reviewed and judged fine as-is (no action needed)
- `pam_facial.so`'s own timeouts (`kOuterTimeoutMs`, `kPollIntervalMs` in
  `src/pam/pam_facial.cpp:42-43`) — deliberately not Config-linked to
  minimize the attack surface of a root-loaded PAM module.
- CMake-baked binary paths (`cmake/facial_auth_paths.hpp.in`) —
  intentional, defeats `$PATH` hijacking from a root PAM module.
- On-disk format magic bytes/version (`EmbeddingFormat.hpp:14-15`).
- mmap buffer count (`V4L2Camera.cpp:20`).
- Pre-enable recognition-check policy constants
  (`kPamPreEnableAttempts`/`kPamPreEnableMinPasses`,
  `src/enroll/main.cpp:60-61`) — security policy, shouldn't be
  admin-configurable.
- Multi-angle bucketing sample-count minimums
  (`kMinFramesForYawBucketing=12`, `kMinFramesForFullGridBucketing=36`,
  `src/enroll/main.cpp:52-53`) — structural constants tied to the fixed
  3x3 grid geometry.
- `CameraEnumerator.cpp` device-enumeration paths (`/dev/v4l/by-id`,
  `/dev`) — standard kernel/udev paths, not deployment-specific.
- No hardcoded runtime camera device nodes, credentials, tokens, or
  secrets found anywhere in `src/`, `tools/`, `config/`.

## Deployed-machine follow-ups (from earlier work, still open)
- Deployed `/etc/facial-auth/config.conf` still has
  `max_capture_attempts = 5` from a manual bump; needs
  `sudo facial-auth-enroll --write-config --set max_capture_attempts=20`
  (or hand-edit) to pick up the new default of 20.
- Full enroll→verify round trip on real hardware with the multi-angle
  flow not yet done (confirm `meta.json` shows `angle_bucket_count > 1`).
- Reinstall the fixed `pam_facial.so` (terminal-orphaning regression fix)
  and confirm a declined/timed-out `sudo` face-auth prompt correctly
  falls through to a working password prompt — not yet retested against
  a real terminal.
