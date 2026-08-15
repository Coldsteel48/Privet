#include "SettingsPage.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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

void SettingsPage::onSaveClicked() {
    if (runner_->isBusy()) return;

    QStringList overrides;
    overrides << QStringLiteral("device_path=%1").arg(devicePathEdit_->text());
    overrides << QStringLiteral("camera_mode=%1").arg(cameraModeCombo_->currentData().toString());
    overrides << QStringLiteral("match_threshold=%1").arg(thresholdSpin_->value());

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
