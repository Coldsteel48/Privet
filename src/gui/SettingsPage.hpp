#pragma once

#include <QWidget>

#include "EnrollHelperRunner.hpp"

class QLineEdit;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

// Device path, camera mode (gated by RiskDisclaimerDialog), and match
// threshold. Reads the current /etc/facial-auth/config.conf directly
// in-process (it's world-readable — non-secret) but only ever writes
// through EnrollHelperRunner (pkexec facial-auth-enroll --write-config),
// since /etc/facial-auth/config.conf itself requires root to modify.
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

private slots:
    void onSaveClicked();
    void onHelperFinished(EnrollHelperRunner::Result result);
    void onCameraModeChanged(int index);

private:
    void loadCurrentConfig();

    EnrollHelperRunner* runner_;
    QLineEdit* devicePathEdit_;
    QComboBox* cameraModeCombo_;
    QDoubleSpinBox* thresholdSpin_;
    QLabel* statusLabel_;
    int lastConfirmedCameraModeIndex_ = 0;
};
