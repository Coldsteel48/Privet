#pragma once

#include <QDialog>

// The Yes/No box facial-auth-confirm shows in place of pam_facial.so's
// PAM-conversation text prompt, when a display is reachable (see
// tryGuiConfirmation() in src/pam/pam_facial.cpp). Deliberately minimal —
// no checkbox/typed-confirmation gate like RiskDisclaimerDialog or
// PamEnableConfirmDialog use elsewhere in this project, since declining
// here has zero consequence: it just falls through to the normal
// password prompt.
class ConfirmDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConfirmDialog(QWidget* parent = nullptr);

    // Convenience: constructs, executes modally, returns whether the
    // user clicked Yes.
    static bool confirm();
};
