// pam_facial.so — thin PAM shim. Deliberately links only libpam + libc:
// no OpenCV, no V4L2, no facial_core. At authenticate time this module is
// already running inside an already-privileged host process (login, sudo,
// sshd's privileged path, a greeter's PAM helper), so it never needs
// setuid itself — it just forks and execs facial-auth-verify, a separate
// small helper that does the actual camera capture and face match, and
// interprets only that helper's exit code. This keeps the much larger,
// larger-attack-surface face pipeline out of the address space of the
// very processes that gate system login. See the project plan's
// "Privilege architecture" section for the full rationale.

#include <security/pam_modules.h>

#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <thread>

#include "generated/facial_auth_paths.hpp"

namespace {

// Outer backstop timeout for the whole facial-auth-verify run. Deliberately
// larger than that helper's own internal per-attempt/multi-attempt budget
// (~5-6s, see src/verify/main.cpp) — this is a second, independent layer
// of the never-lock-out guarantee, not the primary one.
constexpr int kOuterTimeoutMs = 8000;
constexpr int kPollIntervalMs = 50;

void safeSyslog(int priority, const char* message) {
    // openlog/closelog per call rather than once at module load: this
    // module is dlopen'd into long-lived host processes (login, sshd),
    // and holding a syslog fd open for the module's entire lifetime in
    // someone else's process is more global state than this shim needs.
    openlog("pam_facial", LOG_PID, LOG_AUTHPRIV);
    syslog(priority, "%s", message);
    closelog();
}

// Waits for `pid` with a hard wall-clock timeout, polling waitpid(WNOHANG)
// on a short interval rather than installing a process-wide SIGALRM
// handler — this module runs inside arbitrary host processes, and
// mutating global signal state there risks clobbering a handler the host
// itself relies on. Returns the child's exit code on clean exit, or -1 if
// it was killed on timeout or died abnormally (signal, etc).
int waitForChildWithTimeout(pid_t pid, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    int status = 0;
    for (;;) {
        const pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
        if (result < 0) {
            return -1;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);  // reap the killed child
            safeSyslog(LOG_WARNING, "pam_facial: facial-auth-verify timed out, killed");
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
}

int authenticateImpl(pam_handle_t* pamh) {
    const char* username = nullptr;
    if (pam_get_user(pamh, &username, nullptr) != PAM_SUCCESS || username == nullptr ||
        username[0] == '\0') {
        return PAM_AUTHINFO_UNAVAIL;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        safeSyslog(LOG_ERR, "pam_facial: fork() failed");
        return PAM_AUTHINFO_UNAVAIL;
    }

    if (pid == 0) {
        // Child: fixed absolute path, fixed argv, minimal environment.
        // Never $PATH lookup, never system()/popen() — no shell involved
        // anywhere in this call, so there is no shell-injection surface.
        char* const argv[] = {
            const_cast<char*>(facial_auth::paths::kVerifyBinary),
            const_cast<char*>(username),
            nullptr,
        };
        char* const envp[] = {nullptr};
        execve(facial_auth::paths::kVerifyBinary, argv, envp);
        _exit(127);  // execve() only returns on failure
    }

    const int exitCode = waitForChildWithTimeout(pid, kOuterTimeoutMs);
    switch (exitCode) {
        case 0:
            return PAM_SUCCESS;
        case 1:
            return PAM_AUTH_ERR;
        default:
            return PAM_AUTHINFO_UNAVAIL;
    }
}

}  // namespace

extern "C" int pam_sm_authenticate(pam_handle_t* pamh, int /*flags*/, int /*argc*/,
                                               const char** /*argv*/) {
    // The one mandatory catch-all at this ABI boundary: no C++ exception
    // may ever unwind back into libpam/login/sudo/sshd. In practice
    // nothing here should throw (this file uses no exception-throwing
    // core-library code at all), but the guarantee must hold regardless
    // of what future changes touch this function.
    try {
        return authenticateImpl(pamh);
    } catch (...) {
        safeSyslog(LOG_ERR, "pam_facial: unhandled exception in pam_sm_authenticate");
        return PAM_AUTHINFO_UNAVAIL;
    }
}

extern "C" int pam_sm_setcred(pam_handle_t* /*pamh*/, int /*flags*/, int /*argc*/,
                                          const char** /*argv*/) {
    return PAM_SUCCESS;
}
