// fake-verify: minimal, controllable test double for facial-auth-verify.
// Used only by the test-only pam_facial_test PAM module variant and
// tools/pam_test_harness to exercise pam_facial.cpp's fork/exec/timeout
// and exit-code-mapping logic without a camera, a model, or a real
// enrollment — see docs/testing-safely.md.
//
// Controlled via a fixed local file (not environment variables or extra
// argv), because pam_facial.cpp deliberately execve()s its child with an
// empty environment and a fixed {binary, username} argv even for this
// test binary — the control channel has to survive that. File format:
// line 1 = exit code (int, default 0), line 2 = sleep milliseconds before
// exiting (int, default 0, used to test the outer-timeout kill path).
// Missing file = exit 0 immediately.

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

#include "control_path.hpp"

int main(int /*argc*/, char** /*argv*/) {
    int exitCode = 0;
    int sleepMs = 0;

    if (std::ifstream file(kFakeVerifyControlPath); file.is_open()) {
        file >> exitCode >> sleepMs;
    }

    if (sleepMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    return exitCode;
}
