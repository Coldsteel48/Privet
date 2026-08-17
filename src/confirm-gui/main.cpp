// facial-auth-confirm: minimal Yes/No GUI helper, run exclusively by
// pam_facial.so (never directly by a user) in place of its PAM
// conversation text prompt, when a display is reachable. Communicates
// its result ONLY via exit code, mirroring facial-auth-verify's
// contract (see src/verify/main.cpp):
//   0 = user clicked Yes         -> pam_facial.so treats this as consent
//   1 = user clicked No / closed -> pam_facial.so treats this as decline
//   2 = couldn't show anything   -> pam_facial.so falls back to its PAM
//                                    text prompt instead
// See tryGuiConfirmation() in src/pam/pam_facial.cpp for how this is
// invoked: forked, privilege-dropped to the authenticating user, and
// exec'd with only a hand-picked allow-list of display-related
// environment variables (never the full environment).

#include <QApplication>

#include "ConfirmDialog.hpp"

namespace {
constexpr int kExitYes = 0;
constexpr int kExitNo = 1;
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return ConfirmDialog::confirm() ? kExitYes : kExitNo;
}
