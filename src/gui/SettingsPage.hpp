#pragma once

#include <vector>

#include <QWidget>

#include "EnrollHelperRunner.hpp"
#include "core/camera/CameraEnumerator.hpp"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QLabel;

namespace facial_auth {
struct Config;
}

// Camera device + mode (linked: the mode dropdown only ever offers what
// the selected physical camera actually supports, gated by
// RiskDisclaimerDialog whenever that means RGB), match threshold, and
// the pam_facial.so confirmation-prompt behavior. Reads the current
// /etc/facial-auth/config.conf directly in-process (it's world-readable
// — non-secret) but only ever writes through EnrollHelperRunner (pkexec
// facial-auth-enroll --write-config), since /etc/facial-auth/config.conf
// itself requires root to modify.
class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

private slots:
    void onSaveClicked();
    void onHelperFinished(EnrollHelperRunner::Result result);
    void onCameraDeviceChanged(int index);
    void onCameraModeChanged(int index);
    void onConfirmationModeChanged(int index);
    void onRefreshCamerasClicked();

private:
    void loadCurrentConfig();
    // Repopulates cameraDeviceCombo_ from listCameras(), selecting whichever
    // physical camera exposes config.devicePath (as either its ir or rgb
    // stream). If nothing detected matches, the configured device is kept
    // as a selectable fallback entry so Save never silently changes it out
    // from under the user. Always ends by calling populateCameraModeCombo()
    // for the newly selected device — signals are blocked throughout, so
    // this never pops the RGB risk disclaimer (that's only for interactive
    // changes, via onCameraDeviceChanged/onCameraModeChanged below).
    void populateCameraDeviceCombo(const facial_auth::Config& config);
    // Rebuilds cameraModeCombo_ for the currently selected physical camera
    // (cameras_[cameraDeviceCombo_->currentData().toInt()]), offering only
    // the mode(s) it actually supports, and selects `mode` ("ir"/"rgb") if
    // that device offers it. Also shows/hides rgbOnlyDisclaimerLabel_.
    // Never itself triggers the risk disclaimer — callers that change the
    // effective mode as a result of user interaction must gate that first.
    void populateCameraModeCombo(const QString& mode);

    EnrollHelperRunner* runner_;
    std::vector<facial_auth::CameraDevice> cameras_;
    QComboBox* cameraDeviceCombo_;
    QPushButton* refreshCamerasButton_;
    QComboBox* cameraModeCombo_;
    QLabel* rgbOnlyDisclaimerLabel_;
    QDoubleSpinBox* thresholdSpin_;
    QComboBox* confirmationModeCombo_;
    QComboBox* greeterConfirmationModeCombo_;
    QSpinBox* confirmationTimeoutSpin_;
    QLabel* statusLabel_;
    int lastConfirmedCameraDeviceIndex_ = 0;
    QString lastConfirmedCameraMode_ = QStringLiteral("ir");
};
