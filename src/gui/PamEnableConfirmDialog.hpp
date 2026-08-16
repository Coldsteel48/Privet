#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;

// Shown before facial-auth-control ever asks facial-auth-enroll to wire
// pam_facial.so into a real /etc/pam.d/service. This is the one GUI
// action in the whole app that touches something outside facial-auth's
// own files — see README's "never lock out" section — so unlike
// RiskDisclaimerDialog (a single checkbox), this requires explicitly
// typing CONFIRM as well as checking the box, matching the weight of
// what's about to happen.
class PamEnableConfirmDialog : public QDialog {
    Q_OBJECT

public:
    explicit PamEnableConfirmDialog(const QString& service, QWidget* parent = nullptr);

    // Convenience: constructs, executes modally, returns whether the user
    // completed both confirmations and pressed OK.
    static bool confirm(const QString& service, QWidget* parent);

private slots:
    void updateOkEnabled();

private:
    QCheckBox* acknowledgeCheckbox_;
    class QLineEdit* confirmEdit_;
    class QPushButton* okButton_;
};
