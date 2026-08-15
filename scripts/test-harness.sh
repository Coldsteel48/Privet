#!/usr/bin/env bash
# scripts/test-harness.sh — build and exercise pam_facial.so (or the
# test-only pam_facial_test.so, default) against the throwaway
# facial-auth-test PAM service, via pam_test_harness (our in-tree
# pamtester replacement — see docs/testing-safely.md for why pamtester
# itself isn't used: not packaged here, and AUR is off-limits).
#
# This script NEVER touches /etc/pam.d/sudo, /etc/pam.d/login,
# /etc/pam.d/sshd, or any real service — only ever
# /etc/pam.d/facial-auth-test, and only with your explicit sudo approval.
#
# Usage:
#   scripts/test-harness.sh [username]
#
# Env vars:
#   USE_TEST_MODULE=0        Use the real pam_facial.so instead of the
#                             fake-verify-backed pam_facial_test.so.
#   FAKE_VERIFY_EXIT_CODE=N  Exit code fake-verify should return (0/1/2).
#   FAKE_VERIFY_SLEEP_MS=N   Milliseconds fake-verify should sleep before
#                             exiting — set above the outer timeout
#                             (8000ms) to test the SIGKILL-on-timeout path.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

BUILD_DIR="${BUILD_DIR:-build}"
USE_TEST_MODULE="${USE_TEST_MODULE:-1}"
USERNAME="${1:-$USER}"
EXIT_CODE="${FAKE_VERIFY_EXIT_CODE:-0}"
SLEEP_MS="${FAKE_VERIFY_SLEEP_MS:-0}"

cmake --build "$BUILD_DIR" >&2

if [[ "$USE_TEST_MODULE" == "1" ]]; then
    MODULE_PATH="$(realpath "$BUILD_DIR/src/pam/pam_facial_test.so")"
    CONTROL_FILE="$BUILD_DIR/tools/fake_verify/control.txt"
    mkdir -p "$(dirname "$CONTROL_FILE")"
    echo "$EXIT_CODE $SLEEP_MS" > "$CONTROL_FILE"
    echo "fake-verify control: exit=$EXIT_CODE sleep_ms=$SLEEP_MS" >&2
else
    MODULE_PATH="$(realpath "$BUILD_DIR/src/pam/pam_facial.so")"
fi
echo "Using module: $MODULE_PATH" >&2

sed "s|@PAM_MODULE_PATH@|$MODULE_PATH|" config/pam.d/facial-auth-test | sudo tee /etc/pam.d/facial-auth-test >/dev/null
echo "Installed throwaway service: /etc/pam.d/facial-auth-test" >&2

set +e
"$BUILD_DIR/tools/pam_test_harness/pam_test_harness" facial-auth-test "$USERNAME"
RC=$?
set -e

echo "--- recent pam_facial syslog ---" >&2
journalctl -t pam_facial -n 20 --no-pager 2>/dev/null || echo "(journalctl unavailable or no entries)" >&2

exit "$RC"
