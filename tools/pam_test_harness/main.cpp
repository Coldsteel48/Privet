// pam_test_harness: minimal replacement for `pamtester` (not packaged in
// official repos on this system, and AUR is off-limits per project
// policy). Links libpam directly and drives pam_start/pam_authenticate/
// pam_end against a throwaway PAM service — see
// config/pam.d/facial-auth-test and docs/testing-safely.md. This lets the
// whole fork/exec/timeout/exit-code path in pam_facial.so be exercised in
// isolation, without ever touching the real login/sudo/sshd PAM stacks.

#include <security/pam_appl.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace {

int conversationFunc(int numMsg, const struct pam_message** /*msg*/, struct pam_response** resp,
                      void* /*appdataPtr*/) {
    // pam_facial.so now asks a Yes/No "Authenticate using face
    // recognition? (y/n)" prompt
    // (PAM_PROMPT_ECHO_ON) before ever opening the camera, in a forked
    // child so it can enforce its own 20s answer timeout — see
    // confirmCameraUseViaPam() in src/pam/pam_facial.cpp. Auto-answer "y"
    // to every prompt so the harness continues to exercise the full
    // fork/exec/timeout/exit-code path non-interactively; set
    // PAM_TEST_HARNESS_DECLINE=1 to instead simulate a user declining, or
    // PAM_TEST_HARNESS_DELAY_MS to simulate a slow/unresponsive front end
    // (set above 20000 to exercise the confirmation-timeout path itself —
    // this call happens in a copy of this process forked off by
    // pam_facial.so, so sleeping here doesn't block the harness itself).
    if (const char* delayMs = std::getenv("PAM_TEST_HARNESS_DELAY_MS"); delayMs != nullptr) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(delayMs)));
    }
    const char* answer = std::getenv("PAM_TEST_HARNESS_DECLINE") != nullptr ? "n" : "y";
    auto* responses = static_cast<struct pam_response*>(calloc(static_cast<size_t>(numMsg),
                                                                 sizeof(struct pam_response)));
    for (int i = 0; i < numMsg; ++i) {
        responses[i].resp = strdup(answer);
        responses[i].resp_retcode = 0;
    }
    *resp = responses;
    return PAM_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s <pam-service-name> <username>\n", argv[0]);
        std::fprintf(stderr,
                      "Exercises pam_authenticate() against the given service via libpam "
                      "directly. See config/pam.d/facial-auth-test and docs/testing-safely.md.\n");
        return 2;
    }

    // pam_facial.so now tries a GUI Yes/No helper before falling back to
    // this harness's conversationFunc (see tryGuiConfirmation() in
    // src/pam/pam_facial.cpp), whenever DISPLAY/WAYLAND_DISPLAY looks
    // set — which it normally will be if this harness is run from an
    // interactive desktop terminal, since pam_facial.so is dlopen'd
    // in-process and reads this very process's environment. Clear both
    // so the harness always exercises conversationFunc below and stays
    // non-interactive regardless of where it's run from.
    unsetenv("DISPLAY");
    unsetenv("WAYLAND_DISPLAY");

    const std::string service = argv[1];
    const std::string user = argv[2];

    struct pam_conv conv;
    conv.conv = conversationFunc;
    conv.appdata_ptr = nullptr;

    pam_handle_t* pamh = nullptr;
    int rc = pam_start(service.c_str(), user.c_str(), &conv, &pamh);
    if (rc != PAM_SUCCESS) {
        std::fprintf(stderr, "pam_start() failed: %d\n", rc);
        return 2;
    }

    rc = pam_authenticate(pamh, 0);
    std::printf("pam_authenticate() returned %d (%s)\n", rc, pam_strerror(pamh, rc));

    pam_end(pamh, rc);
    return rc == PAM_SUCCESS ? 0 : 1;
}
