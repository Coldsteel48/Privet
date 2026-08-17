#include "SettingsPage.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "RiskDisclaimerDialog.hpp"
#include "core/config/Config.hpp"

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent), runner_(new EnrollHelperRunner(this)) {
    auto* form = new QFormLayout();

    devicePathEdit_ = new QLineEdit(this);
    devicePathEdit_->setPlaceholderText(tr("/dev/videoN \xe2\x80\x94 confirm via v4l2-ctl --list-devices"));
    form->addRow(tr("Camera device:"), devicePathEdit_);

    cameraModeCombo_ = new QComboBox(this);
    cameraModeCombo_->addItem(tr("IR (recommended)"), QStringLiteral("ir"));
    cameraModeCombo_->addItem(tr("Regular / RGB (at your own risk)"), QStringLiteral("rgb"));
    form->addRow(tr("Camera mode:"), cameraModeCombo_);
    connect(cameraModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &SettingsPage::onCameraModeChanged);

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
        tr("Only applies when the clickable box above can't be shown \xe2\x80\x94 e.g. the console "
           "login prompt, or a graphical greeter such as GDM, SDDM, LightDM, or COSMIC's "
           "(via greetd). Those run before any desktop session exists, so there's no display "
           "for a mouse-driven box to draw into and this decides what happens instead."));
    form->addRow(tr("At the login screen, when no box is possible:"), greeterConfirmationModeCombo_);

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

    devicePathEdit_->setText(QString::fromStdString(config.devicePath));
    cameraModeCombo_->setCurrentIndex(config.cameraMode == facial_auth::CameraMode::RGB ? 1 : 0);
    lastConfirmedCameraModeIndex_ = cameraModeCombo_->currentIndex();
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

void SettingsPage::onCameraModeChanged(int index) {
    if (index == 1 && !RiskDisclaimerDialog::confirm(this)) {
        cameraModeCombo_->blockSignals(true);
        cameraModeCombo_->setCurrentIndex(lastConfirmedCameraModeIndex_);
        cameraModeCombo_->blockSignals(false);
        return;
    }
    lastConfirmedCameraModeIndex_ = index;
}

// The "at the login screen" choice only means anything when the primary
// mode is Gui (it's what decides the fallback once the clickable box has
// already been ruled out as unreachable there) — greyed out rather than
// hidden for Text/None so its current value stays visible even when it's
// not the thing in effect.
void SettingsPage::onConfirmationModeChanged(int /*index*/) {
    const QString mode = confirmationModeCombo_->currentData().toString();
    greeterConfirmationModeCombo_->setEnabled(mode == QStringLiteral("gui"));
    confirmationTimeoutSpin_->setEnabled(mode != QStringLiteral("none"));
}

void SettingsPage::onSaveClicked() {
    if (runner_->isBusy()) return;

    QStringList overrides;
    overrides << QStringLiteral("device_path=%1").arg(devicePathEdit_->text());
    overrides << QStringLiteral("camera_mode=%1").arg(cameraModeCombo_->currentData().toString());
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
