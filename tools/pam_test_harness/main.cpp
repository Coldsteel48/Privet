// pam_test_harness: minimal replacement for `pamtester` (not packaged in
// official repos on this system, and AUR is off-limits per project
// policy). Links libpam directly and drives pam_start/pam_authenticate/
// pam_end against a throwaway PAM service — see
// config/pam.d/facial-auth-test and docs/testing-safely.md. This lets the
// whole fork/exec/timeout/exit-code path in pam_facial.so be exercised in
// isolation, without ever touching the real login/sudo/sshd PAM stacks.

#include <security/pam_appl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int conversationFunc(int numMsg, const struct pam_message** /*msg*/, struct pam_response** resp,
                      void* /*appdataPtr*/) {
    // Facial auth has no interactive prompts; respond empty to anything
    // that does appear rather than leaving the conversation hanging.
    auto* responses = static_cast<struct pam_response*>(calloc(static_cast<size_t>(numMsg),
                                                                 sizeof(struct pam_response)));
    for (int i = 0; i < numMsg; ++i) {
        responses[i].resp = strdup("");
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
