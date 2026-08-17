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

#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/pam/PamConfirmationPrompt.hpp"
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

// Tries to show a real, clickable Yes/No box via the separate
// facial-auth-confirm GUI helper instead of PAM's text-only conversation
// channel — the whole reason this exists is that several PAM front ends
// (e.g. COSMIC's greeter) don't render a text conv() prompt's options
// legibly. Returns a definitive answer only if the helper actually ran
// and the user answered (exit 0/1); std::nullopt means "no display
// reachable, or the helper couldn't start", telling the caller to fall
// back to the conv()-based text prompt.
//
// At PAM-authenticate time this process is generally still running as
// root (sudo/login), so handing it a full desktop GUI toolkit is treated
// with the same suspicion as the facial-auth-verify execve() below:
//  - only a hand-picked allow-list of display-related environment
//    variables is passed through (facial_auth::kGuiEnvVarNames) — never
//    the full environment — so a root process launching Qt/X11/Wayland
//    code never honors an attacker-controlled LD_PRELOAD/LD_LIBRARY_PATH/
//    QT_PLUGIN_PATH/etc.
//  - privilege is dropped to the authenticating user before exec: a
//    confirmation dialog needs zero privilege, so there's no reason for
//    it to keep running as root, and doing so would turn any Qt/X11/
//    Wayland-parsing bug into a root exploit.
std::optional<bool> tryGuiConfirmation(const char* username) {
    if (!facial_auth::hasDisplayEnv(getenv("DISPLAY"), getenv("WAYLAND_DISPLAY"))) {
        return std::nullopt;
    }

    long pwBufSize = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (pwBufSize <= 0) {
        pwBufSize = 16384;
    }
    std::vector<char> pwStrBuf(static_cast<size_t>(pwBufSize));
    struct passwd pwEntry;
    struct passwd* pw = nullptr;
    if (getpwnam_r(username, &pwEntry, pwStrBuf.data(), pwStrBuf.size(), &pw) != 0 || pw == nullptr) {
        return std::nullopt;
    }
    const uid_t targetUid = pw->pw_uid;
    const gid_t targetGid = pw->pw_gid;

    const pid_t pid = fork();
    if (pid < 0) {
        return std::nullopt;
    }

    if (pid == 0) {
        // Drop from root to the authenticating user — gid before uid,
        // since dropping uid first would strip the ability to change
        // gid — then verify the drop actually took before ever handing
        // this process a GUI toolkit to run.
        if (setgroups(0, nullptr) != 0 || setresgid(targetGid, targetGid, targetGid) != 0 ||
            setresuid(targetUid, targetUid, targetUid) != 0 || geteuid() != targetUid ||
            getegid() != targetGid) {
            _exit(2);
        }

        std::vector<std::string> envStorage;
        for (const char* name : facial_auth::kGuiEnvVarNames) {
            if (const char* value = getenv(name); value != nullptr) {
                envStorage.push_back(std::string(name) + "=" + value);
            }
        }
        std::vector<char*> envp;
        envp.reserve(envStorage.size() + 1);
        for (std::string& entry : envStorage) {
            envp.push_back(entry.data());
        }
        envp.push_back(nullptr);

        char* const argv[] = {const_cast<char*>(facial_auth::paths::kConfirmGuiBinary), nullptr};
        execve(facial_auth::paths::kConfirmGuiBinary, argv, envp.data());
        _exit(2);  // execve() only returns on failure (e.g. helper not installed)
    }

    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) {
        return std::nullopt;
    }
    switch (WEXITSTATUS(status)) {
        case 0:
            return true;
        case 1:
            return false;
        default:
            return std::nullopt;
    }
}

// The plain-text PAM conversation "(y/n)" prompt — the original
// confirmation mechanism, still used when confirmation_mode = text, and
// as the Gui-mode fallback when no display is reachable (subject to
// greeter_confirmation_mode). Must be called from inside the forked child
// in confirmCameraUseViaPam(): conv() has no defined timeout, and this
// module never blocks its host process on one directly (see that
// function's comment).
bool doTextConfirmation(const struct pam_conv* conv) {
    struct pam_message msg;
    msg.msg_style = PAM_PROMPT_ECHO_ON;
    msg.msg = "Authenticate using face recognition? (y/n): ";
    const struct pam_message* msgs[1] = {&msg};
    struct pam_response* resp = nullptr;

    const int rc = conv->conv(1, msgs, &resp, conv->appdata_ptr);
    if (rc != PAM_SUCCESS || resp == nullptr) {
        return false;
    }
    const bool affirmative = facial_auth::isAffirmativeResponse(resp[0].resp);
    if (resp[0].resp != nullptr) {
        std::memset(resp[0].resp, 0, std::strlen(resp[0].resp));  // don't leave the answer in memory
        std::free(resp[0].resp);
    }
    std::free(resp);
    return affirmative;
}

// Asks the user whether it's OK to open the camera for this
// authentication attempt — first by trying a real Yes/No GUI box
// (tryGuiConfirmation(), when a display is reachable), falling back to
// the host's PAM conversation as a text prompt otherwise. Returns true
// only on an explicit "yes" obtained within the configured answer
// timeout (see readConfirmationTimeoutMs(), default 20s) — any missing
// conv, conversation error, timeout, or non-affirmative answer declines.
// This runs before fork()/execve()'ing facial-auth-verify, so it's the
// only point in the whole pam_facial.so chain that has access to `pamh`.
//
// Both the GUI attempt and the conv() call happen in a forked child
// rather than inline: PAM defines no timeout on a conversation call, and
// neither the front end on the other end of it (a terminal, or a
// greeter's IPC channel) nor a GUI toolkit waiting on user input is code
// this module controls or can trust to respond promptly. The parent
// enforces the actual wall-clock deadline via a pipe and, on timeout,
// SIGKILLs the child's whole process group (not just the child itself —
// see setpgid() below) so an unanswered GUI dialog can never linger on
// screen after this module has already moved on. This module must never
// be the thing that leaves a login prompt hanging, mirroring the
// fork/timeout/kill pattern already used below for facial-auth-verify.
bool confirmCameraUseViaPam(pam_handle_t* pamh, const char* username) {
    // confirmation_mode = none means "never ask" full stop — resolved
    // before ever touching PAM_CONV (unlike Text/Gui-fallback below) so a
    // host process that happens not to provide a conversation structure
    // still doesn't block this mode from working.
    if (facial_auth::readConfirmationMode() == facial_auth::ConfirmationMode::None) {
        return true;
    }

    const struct pam_conv* conv = nullptr;
    if (pam_get_item(pamh, PAM_CONV, reinterpret_cast<const void**>(&conv)) != PAM_SUCCESS ||
        conv == nullptr || conv->conv == nullptr) {
        safeSyslog(LOG_WARNING, "pam_facial: no PAM conversation available, declining camera use");
        return false;
    }

    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        safeSyslog(LOG_ERR, "pam_facial: pipe() failed, declining camera use");
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        safeSyslog(LOG_ERR, "pam_facial: fork() failed, declining camera use");
        return false;
    }

    // Both branches call setpgid() for the same pid, redundantly: this is
    // the standard fork+process-group idiom to close the race between the
    // child actually running it and the parent needing the group to
    // exist (for the timeout kill below) regardless of which of the two
    // gets scheduled first. Errors are ignored — whichever call runs
    // first wins, and the outcome is identical either way.
    if (pid == 0) {
        setpgid(0, 0);

        // Child: owns the actual (unbounded) confirmation call — GUI or
        // conv() text prompt. Writes a single result byte back and
        // exits; never reads the parent's mind about whether it gave up
        // and moved on already.
        close(pipeFds[0]);

        char result = 0;
        switch (facial_auth::readConfirmationMode()) {
            case facial_auth::ConfirmationMode::None:
                result = 1;
                break;
            case facial_auth::ConfirmationMode::Text:
                result = doTextConfirmation(conv) ? 1 : 0;
                break;
            case facial_auth::ConfirmationMode::Gui:
            default:
                if (const std::optional<bool> guiAnswer = tryGuiConfirmation(username);
                    guiAnswer.has_value()) {
                    result = *guiAnswer ? 1 : 0;
                } else if (facial_auth::readGreeterConfirmationMode() ==
                           facial_auth::GreeterConfirmationMode::None) {
                    // No display reachable (console login / graphical greeter,
                    // e.g. COSMIC via greetd) and the admin has opted this
                    // context out of any prompt at all.
                    result = 1;
                } else {
                    result = doTextConfirmation(conv) ? 1 : 0;
                }
                break;
        }
        const ssize_t written = write(pipeFds[1], &result, 1);
        (void)written;  // best-effort: parent may already be gone on timeout
        _exit(0);
    }
    setpgid(pid, pid);

    close(pipeFds[1]);

    const auto deadline = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(facial_auth::readConfirmationTimeoutMs());
    char result = 0;
    bool gotAnswer = false;
    for (;;) {
        const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      deadline - std::chrono::steady_clock::now())
                                      .count();
        if (remainingMs <= 0) {
            break;
        }
        struct pollfd pfd;
        pfd.fd = pipeFds[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
        const int pollRc = poll(&pfd, 1, static_cast<int>(remainingMs));
        if (pollRc > 0 && (pfd.revents & POLLIN)) {
            gotAnswer = read(pipeFds[0], &result, 1) == 1;
            break;
        }
        if (pollRc < 0 && errno != EINTR) {
            break;
        }
        // pollRc == 0 (timed out) or EINTR: loop re-checks the deadline.
    }
    close(pipeFds[0]);

    if (!gotAnswer) {
        kill(-pid, SIGKILL);  // whole process group: also kills a GUI helper grandchild
        safeSyslog(LOG_WARNING, "pam_facial: confirmation prompt timed out, declining camera use");
    }
    waitpid(pid, nullptr, 0);  // always reap, timed out or not

    if (gotAnswer && result == 0) {
        safeSyslog(LOG_INFO, "pam_facial: camera authentication declined by user");
    }
    return gotAnswer && result == 1;
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

    // Confirm with the user before ever touching the camera. Declining
    // (or any conversation failure) falls through to the next stacked
    // auth method rather than failing hard — pam_facial.so is always
    // `sufficient`, never the only line, so this is a safe default.
    if (!confirmCameraUseViaPam(pamh, username)) {
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
