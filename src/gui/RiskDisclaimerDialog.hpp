#pragma once

#include <QDialog>

class QCheckBox;

// Shown whenever the user selects a non-IR (RGB) camera mode, from both
// EnrollmentPage and SettingsPage. Wording is kept in sync (manually, for
// now — see docs/) with the equivalent CLI confirmation prompt in
// src/enroll/main.cpp's confirmRgbRisk().
class RiskDisclaimerDialog : public QDialog {
    Q_OBJECT

public:
    explicit RiskDisclaimerDialog(QWidget* parent = nullptr);

    // Convenience: constructs, executes modally, returns whether the user
    // checked the acknowledgement box and pressed OK.
    static bool confirm(QWidget* parent);

private:
    QCheckBox* acknowledgeCheckbox_;
};
