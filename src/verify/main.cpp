// facial-auth-verify: does the actual camera capture + face match. Run
// exclusively by pam_facial.so via fork()/execve() with the PAM-resolved
// username as argv[1] — never invoked directly by a user in normal
// operation. Communicates its result ONLY via exit code:
//   0 = confident match          -> pam_facial.so returns PAM_SUCCESS
//   1 = genuine failed match     -> pam_facial.so returns PAM_AUTH_ERR
//   2 = unavailable/error/other  -> pam_facial.so returns PAM_AUTHINFO_UNAVAIL
// See the project plan's "Privilege architecture" section for why this
// logic lives in its own process rather than inside pam_facial.so itself.

#include <exception>
#include <string>

#include "core/log/Logger.hpp"
#include "core/verify/VerifyRunner.hpp"

namespace {

constexpr int kExitMatch = 0;
constexpr int kExitNoMatch = 1;
constexpr int kExitUnavailable = 2;

// The actual capture/detect/match logic lives in facial_core's
// VerifyRunner, shared with facial-auth-enroll's --test mode (the GUI's
// "Test Recognition" button) — see VerifyRunner.hpp for why that sharing
// matters. This just maps the outcome to this process's exit-code
// contract.
int runVerify(const std::string& username) {
    switch (facial_auth::runVerification(username)) {
        case facial_auth::VerifyOutcome::Match:
            return kExitMatch;
        case facial_auth::VerifyOutcome::NoMatch:
            return kExitNoMatch;
        case facial_auth::VerifyOutcome::Unavailable:
        default:
            return kExitUnavailable;
    }
}

}  // namespace

int main(int argc, char** argv) {
    facial_auth::Logger logger("facial-auth-verify");

    if (argc != 2) {
        facial_auth::Logger::log(facial_auth::LogLevel::Error,
                                  "facial-auth-verify: expected exactly one argument (username)");
        return kExitUnavailable;
    }

    // This try/catch is this process's own boundary: its exit code is the
    // only thing pam_facial.so ever observes, so any exception anywhere
    // in the pipeline (bad model file, OpenCV error, etc.) must resolve
    // to a clean "unavailable" exit rather than a crash/nonzero signal
    // death, which pam_facial.so also maps to PAM_AUTHINFO_UNAVAIL but
    // this path logs the actual cause first.
    try {
        return runVerify(argv[1]);
    } catch (const std::exception& e) {
        facial_auth::Logger::log(facial_auth::LogLevel::Error,
                                  std::string("facial-auth-verify: unhandled exception: ") + e.what());
        return kExitUnavailable;
    } catch (...) {
        facial_auth::Logger::log(facial_auth::LogLevel::Error,
                                  "facial-auth-verify: unhandled unknown exception");
        return kExitUnavailable;
    }
}
