#include "SettingsPage.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "RiskDisclaimerDialog.hpp"
#include "core/camera/CameraSelection.hpp"
#include "core/config/Config.hpp"

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent), runner_(new EnrollHelperRunner(this)) {
    auto* form = new QFormLayout();

    cameraDeviceCombo_ = new QComboBox(this);
    cameraDeviceCombo_->setToolTip(
        tr("Cameras detected on this machine that expose a usable video-capture stream."));
    refreshCamerasButton_ = new QPushButton(tr("Refresh"), this);
    connect(refreshCamerasButton_, &QPushButton::clicked, this,
            &SettingsPage::onRefreshCamerasClicked);
    auto* cameraDeviceRow = new QHBoxLayout();
    cameraDeviceRow->addWidget(cameraDeviceCombo_, /*stretch=*/1);
    cameraDeviceRow->addWidget(refreshCamerasButton_);
    form->addRow(tr("Camera device:"), cameraDeviceRow);
    connect(cameraDeviceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsPage::onCameraDeviceChanged);

    cameraModeCombo_ = new QComboBox(this);
    form->addRow(tr("Camera mode:"), cameraModeCombo_);
    connect(cameraModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsPage::onCameraModeChanged);

    rgbOnlyDisclaimerLabel_ = new QLabel(
        tr("\xe2\x9a\xa0 This camera has no IR sensor. Plain RGB video carries no depth or "
           "liveness signal and is far more spoofable (e.g. by a photo or video replay) than "
           "an IR-based check \xe2\x80\x94 you'll be asked to confirm this trade-off before it's saved."),
        this);
    rgbOnlyDisclaimerLabel_->setWordWrap(true);
    rgbOnlyDisclaimerLabel_->setVisible(false);
    form->addRow(QString(), rgbOnlyDisclaimerLabel_);

    thresholdSpin_ = new QDoubleSpinBox(this);
    thresholdSpin_->setRange(0.0, 2.0);
    thresholdSpin_->setSingleStep(0.01);
    thresholdSpin_->setDecimals(3);
    form->addRow(tr("Match threshold:"), thresholdSpin_);

    confirmationModeCombo_ = new QComboBox(this);
    confirmationModeCombo_->addItem(tr("Clickable Yes/No box (mouse, recommended)"),
                                     QStringLiteral("gui"));
    confirmationModeCombo_->addItem(tr("Plain text Y/N prompt"), QStringLiteral("text"));
    confirmationModeCombo_->addItem(tr("No confirmation \xe2\x80\x94 authenticate immediately"),
                                     QStringLiteral("none"));
    confirmationModeCombo_->setToolTip(
        tr("How pam_facial.so asks \xe2\x80\x9cOK to use the camera?\xe2\x80\x9d before authenticating.\n"
           "The clickable box can only appear where a display is actually reachable "
           "(e.g. sudo from an already-logged-in session) \xe2\x80\x94 see the option below for "
           "what happens where it can't (console login, most graphical greeters)."));
    form->addRow(tr("Confirmation prompt:"), confirmationModeCombo_);
    connect(confirmationModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsPage::onConfirmationModeChanged);

    greeterConfirmationModeCombo_ = new QComboBox(this);
    greeterConfirmationModeCombo_->addItem(tr("Fall back to plain text Y/N prompt"),
                                            QStringLiteral("text"));
    greeterConfirmationModeCombo_->addItem(tr("Skip confirmation \xe2\x80\x94 authenticate immediately"),
                                            QStringLiteral("none"));
    greeterConfirmationModeCombo_->setToolTip(
        tr("Applies at the console login prompt or a graphical greeter such as GDM, SDDM, "
           "LightDM, or COSMIC's (via greetd) \xe2\x80\x94 contexts that run before any desktop "
           "session exists, so there's no display for a mouse-driven box to draw into. "
           "Independent of the Confirmation prompt option above, which only governs sessions "
           "where a display is reachable (e.g. sudo from an already-logged-in session)."));
    form->addRow(tr("At the login screen, when no box is possible:"), greeterConfirmationModeCombo_);
    connect(greeterConfirmationModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsPage::onConfirmationModeChanged);

    confirmationTimeoutSpin_ = new QSpinBox(this);
    confirmationTimeoutSpin_->setRange(facial_auth::kMinConfirmationTimeoutMs / 1000,
                                        facial_auth::kMaxConfirmationTimeoutMs / 1000);
    confirmationTimeoutSpin_->setSuffix(tr(" s"));
    confirmationTimeoutSpin_->setToolTip(
        tr("How long to wait for an answer to the confirmation prompt above (box click or "
           "typed y/n) before treating silence as a decline. Not consulted when the "
           "confirmation prompt is set to \xe2\x80\x9cNo confirmation.\xe2\x80\x9d"));
    form->addRow(tr("Confirmation timeout:"), confirmationTimeoutSpin_);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);

    auto* saveButton = new QPushButton(tr("Save Settings"), this);
    connect(saveButton, &QPushButton::clicked, this, &SettingsPage::onSaveClicked);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(saveButton);
    layout->addWidget(statusLabel_);
    layout->addStretch();

    connect(runner_, &EnrollHelperRunner::finished, this, &SettingsPage::onHelperFinished);

    loadCurrentConfig();
}

void SettingsPage::loadCurrentConfig() {
    const auto configOpt = facial_auth::Config::load("/etc/facial-auth/config.conf");
    const facial_auth::Config config = configOpt.value_or(facial_auth::Config::defaults());

    populateCameraDeviceCombo(config);
    thresholdSpin_->setValue(config.matchThreshold);

    confirmationModeCombo_->setCurrentIndex(confirmationModeCombo_->findData(
        QString::fromStdString(facial_auth::toString(config.confirmationMode))));
    greeterConfirmationModeCombo_->setCurrentIndex(greeterConfirmationModeCombo_->findData(
        QString::fromStdString(facial_auth::toString(config.greeterConfirmationMode))));
    confirmationTimeoutSpin_->setValue(config.confirmationTimeoutSec);
    onConfirmationModeChanged(confirmationModeCombo_->currentIndex());

    if (!configOpt) {
        statusLabel_->setText(tr("No config file found yet at /etc/facial-auth/config.conf \xe2\x80\x94 "
                                  "showing defaults. Saving will create it."));
    }
}

void SettingsPage::populateCameraDeviceCombo(const facial_auth::Config& config) {
    const facial_auth::CameraDeviceSelection selection = facial_auth::selectCameraDevice(
        facial_auth::listCameras(), config.devicePath, config.pixelFormat, config.cameraMode,
        config.frameWidth, config.frameHeight,
        tr("%1 (not detected)").arg(QString::fromStdString(config.devicePath)).toStdString(),
        tr("No cameras detected").toStdString());
    cameras_ = selection.devices;

    cameraDeviceCombo_->blockSignals(true);
    cameraDeviceCombo_->clear();
    for (int i = 0; i < static_cast<int>(cameras_.size()); ++i) {
        cameraDeviceCombo_->addItem(QString::fromStdString(cameras_[i].friendlyName), i);
    }
    cameraDeviceCombo_->setCurrentIndex(selection.selectedIndex);
    cameraDeviceCombo_->blockSignals(false);

    lastConfirmedCameraDeviceIndex_ = selection.selectedIndex;
    lastConfirmedCameraMode_ = QString::fromStdString(selection.mode);
    populateCameraModeCombo(lastConfirmedCameraMode_);
}

void SettingsPage::populateCameraModeCombo(const QString& mode) {
    const int deviceIndex = cameraDeviceCombo_->currentData().toInt();
    const bool validDevice = deviceIndex >= 0 && deviceIndex < static_cast<int>(cameras_.size());
    const facial_auth::CameraDevice empty{};
    const facial_auth::CameraDevice& device = validDevice ? cameras_[deviceIndex] : empty;

    const facial_auth::CameraModeSelection selection =
        facial_auth::selectCameraMode(device, mode.toStdString());

    cameraModeCombo_->blockSignals(true);
    cameraModeCombo_->clear();
    if (selection.irAvailable) cameraModeCombo_->addItem(tr("IR (recommended)"), QStringLiteral("ir"));
    if (selection.rgbAvailable)
        cameraModeCombo_->addItem(tr("Regular / RGB (at your own risk)"), QStringLiteral("rgb"));
    if (cameraModeCombo_->count() == 0) {
        cameraModeCombo_->addItem(tr("N/A \xe2\x80\x94 no usable stream on this camera"), QString());
    }
    const int idx = cameraModeCombo_->findData(QString::fromStdString(selection.selectedMode));
    cameraModeCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
    cameraModeCombo_->blockSignals(false);

    rgbOnlyDisclaimerLabel_->setVisible(selection.showRgbOnlyDisclaimer);
}

void SettingsPage::onRefreshCamerasClicked() {
    // Re-detect, keeping whatever's currently selected (device + mode) as
    // the target to re-match against the fresh scan — same mechanism
    // loadCurrentConfig() uses for the on-disk config.
    facial_auth::Config config = facial_auth::Config::defaults();
    const int deviceIndex = cameraDeviceCombo_->currentData().toInt();
    if (deviceIndex >= 0 && deviceIndex < static_cast<int>(cameras_.size())) {
        const auto& device = cameras_[deviceIndex];
        const QString mode = cameraModeCombo_->currentData().toString();
        const auto& stream = mode == QStringLiteral("rgb") ? device.rgb : device.ir;
        if (stream) {
            config.devicePath = stream->devicePath;
            config.pixelFormat = stream->pixelFormat;
            config.cameraMode = facial_auth::cameraModeFromString(mode.toStdString());
            config.frameWidth = stream->width;
            config.frameHeight = stream->height;
        }
    }
    populateCameraDeviceCombo(config);
}

// Re-selecting the same physical camera, or one that still supports the
// previously-confirmed mode, never re-prompts. Switching to a camera that
// only offers the other mode (most commonly: an RGB-only camera when IR
// was selected) goes through the same RiskDisclaimerDialog gate as
// onCameraModeChanged() below; declining reverts the device selection
// entirely, since there's no in-between state for a single-mode camera.
void SettingsPage::onCameraDeviceChanged(int /*index*/) {
    const int deviceIndex = cameraDeviceCombo_->currentData().toInt();
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(cameras_.size())) return;
    const auto& device = cameras_[deviceIndex];

    const QString desiredMode = QString::fromStdString(facial_auth::resolveModeForDeviceChange(
        device, lastConfirmedCameraMode_.toStdString()));

    if (desiredMode == QStringLiteral("rgb") && !RiskDisclaimerDialog::confirm(this)) {
        cameraDeviceCombo_->blockSignals(true);
        cameraDeviceCombo_->setCurrentIndex(
            cameraDeviceCombo_->findData(lastConfirmedCameraDeviceIndex_));
        cameraDeviceCombo_->blockSignals(false);
        return;
    }

    lastConfirmedCameraDeviceIndex_ = deviceIndex;
    lastConfirmedCameraMode_ = desiredMode;
    populateCameraModeCombo(desiredMode);
}

void SettingsPage::onCameraModeChanged(int /*index*/) {
    const QString mode = cameraModeCombo_->currentData().toString();
    if (mode.isEmpty() || mode == lastConfirmedCameraMode_) return;

    if (mode == QStringLiteral("rgb") && !RiskDisclaimerDialog::confirm(this)) {
        cameraModeCombo_->blockSignals(true);
        cameraModeCombo_->setCurrentIndex(cameraModeCombo_->findData(lastConfirmedCameraMode_));
        cameraModeCombo_->blockSignals(false);
        return;
    }

    lastConfirmedCameraMode_ = mode;
}

// The "at the login screen" choice applies whenever no display is
// reachable there (console login, most graphical greeters), independent
// of the general Confirmation prompt mode above — pam_facial.so consults
// it on its own, not as a Gui-only fallback. The timeout spinner is only
// irrelevant when neither setting can ever produce a prompt.
void SettingsPage::onConfirmationModeChanged(int /*index*/) {
    const QString mode = confirmationModeCombo_->currentData().toString();
    const QString greeterMode = greeterConfirmationModeCombo_->currentData().toString();
    confirmationTimeoutSpin_->setEnabled(mode != QStringLiteral("none") ||
                                          greeterMode != QStringLiteral("none"));
}

void SettingsPage::onSaveClicked() {
    if (runner_->isBusy()) return;

    const int deviceIndex = cameraDeviceCombo_->currentData().toInt();
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(cameras_.size())) {
        statusLabel_->setText(tr("No camera selected \xe2\x80\x94 pick a camera device first."));
        return;
    }
    const auto& device = cameras_[deviceIndex];
    const QString mode = cameraModeCombo_->currentData().toString();
    const auto& stream = mode == QStringLiteral("rgb") ? device.rgb : device.ir;
    if (!stream) {
        statusLabel_->setText(tr("Selected camera doesn't support that mode \xe2\x80\x94 nothing saved."));
        return;
    }

    QStringList overrides;
    overrides << QStringLiteral("device_path=%1").arg(QString::fromStdString(stream->devicePath));
    overrides << QStringLiteral("pixel_format=%1")
                     .arg(QString::fromStdString(facial_auth::toString(stream->pixelFormat)));
    overrides << QStringLiteral("camera_mode=%1").arg(mode);
    overrides << QStringLiteral("frame_width=%1").arg(stream->width);
    overrides << QStringLiteral("frame_height=%1").arg(stream->height);
    overrides << QStringLiteral("match_threshold=%1").arg(thresholdSpin_->value());
    overrides << QStringLiteral("confirmation_mode=%1").arg(confirmationModeCombo_->currentData().toString());
    overrides << QStringLiteral("greeter_confirmation_mode=%1")
                     .arg(greeterConfirmationModeCombo_->currentData().toString());
    overrides << QStringLiteral("confirmation_timeout_sec=%1").arg(confirmationTimeoutSpin_->value());

    statusLabel_->setText(tr("Saving\xe2\x80\xa6 check for a polkit authentication prompt."));
    runner_->writeConfig(overrides);
}

void SettingsPage::onHelperFinished(EnrollHelperRunner::Result result) {
    if (result.ok) {
        statusLabel_->setText(tr("Settings saved."));
    } else {
        statusLabel_->setText(tr("Failed to save settings: %1").arg(result.message));
    }
}
