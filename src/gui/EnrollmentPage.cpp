#include "EnrollmentPage.hpp"

#include <cstdlib>
#include <exception>

#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "core/camera/V4L2Camera.hpp"
#include "core/config/Config.hpp"

namespace {
constexpr int kPreviewIntervalMs = 100;  // ~10 fps, plenty for a positioning preview
}

EnrollmentPage::EnrollmentPage(QWidget* parent)
    : QWidget(parent), runner_(new EnrollHelperRunner(this)) {
    previewLabel_ = new QLabel(tr("Camera preview unavailable"), this);
    previewLabel_->setMinimumSize(320, 240);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setStyleSheet(QStringLiteral("QLabel { background-color: black; color: white; }"));

    statusLabel_ = new QLabel(tr("Checking enrollment status\xe2\x80\xa6"), this);
    statusLabel_->setWordWrap(true);

    enrollButton_ = new QPushButton(tr("Enroll"), this);
    reEnrollButton_ = new QPushButton(tr("Re-enroll"), this);
    deleteButton_ = new QPushButton(tr("Delete Enrollment"), this);
    reEnrollButton_->setVisible(false);
    deleteButton_->setEnabled(false);

    connect(enrollButton_, &QPushButton::clicked, this, &EnrollmentPage::onEnrollClicked);
    connect(reEnrollButton_, &QPushButton::clicked, this, &EnrollmentPage::onReEnrollClicked);
    connect(deleteButton_, &QPushButton::clicked, this, &EnrollmentPage::onDeleteClicked);
    connect(runner_, &EnrollHelperRunner::finished, this, &EnrollmentPage::onHelperFinished);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(enrollButton_);
    buttonRow->addWidget(reEnrollButton_);
    buttonRow->addWidget(deleteButton_);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(previewLabel_);
    layout->addWidget(statusLabel_);
    layout->addLayout(buttonRow);
    layout->addStretch();

    previewTimer_ = new QTimer(this);
    connect(previewTimer_, &QTimer::timeout, this, &EnrollmentPage::onPreviewTick);
}

EnrollmentPage::~EnrollmentPage() {
    stopPreview();
}

void EnrollmentPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    startPreview();
    refreshStatus();
}

void EnrollmentPage::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    stopPreview();
}

QString EnrollmentPage::currentUsername() const {
    // facial-auth-control is a self-service tool: it always acts on the
    // account it's running as, never an arbitrary --user (that stays a
    // CLI/admin capability). $USER is set by the session; falling back to
    // getlogin()-equivalent isn't needed for a GUI app launched normally.
    const char* user = std::getenv("USER");
    return user ? QString::fromUtf8(user) : QString();
}

void EnrollmentPage::startPreview() {
    if (camera_) return;  // already running

    const auto configOpt = facial_auth::Config::load("/etc/facial-auth/config.conf");
    if (!configOpt) {
        previewLabel_->setText(tr("No config at /etc/facial-auth/config.conf yet \xe2\x80\x94 "
                                   "set a camera device in Settings first."));
        return;
    }

    facial_auth::CameraConfig cameraConfig;
    cameraConfig.devicePath = configOpt->devicePath;
    cameraConfig.pixelFormat = configOpt->pixelFormat;
    cameraConfig.width = configOpt->frameWidth;
    cameraConfig.height = configOpt->frameHeight;
    cameraConfig.timeoutMs = configOpt->captureTimeoutMs;

    auto camera = std::make_unique<facial_auth::V4L2Camera>(cameraConfig);
    if (!camera->open()) {
        previewLabel_->setText(tr("Could not open camera '%1' \xe2\x80\x94 check the device path in "
                                   "Settings and that you're in the 'video' group.")
                                    .arg(QString::fromStdString(cameraConfig.devicePath)));
        return;
    }

    camera_ = std::move(camera);
    previewTimer_->start(kPreviewIntervalMs);
}

void EnrollmentPage::stopPreview() {
    previewTimer_->stop();
    camera_.reset();
}

void EnrollmentPage::onPreviewTick() {
    if (!camera_) return;

    // captureFrame() throws for pixel formats without an implemented
    // conversion yet (GREY/Y16 IR — see V4L2Camera) — this must never
    // escape a Qt slot invoked from the event loop, so it's caught here
    // rather than left to propagate.
    try {
        auto frameOpt = camera_->captureFrame();
        if (!frameOpt) return;  // per-frame timeout — try again next tick

        const cv::Mat& frame = *frameOpt;
        const QImage image(frame.data, frame.cols, frame.rows, static_cast<int>(frame.step),
                            QImage::Format_BGR888);
        previewLabel_->setPixmap(QPixmap::fromImage(image.copy())
                                      .scaled(previewLabel_->size(), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
    } catch (const std::exception& e) {
        stopPreview();
        previewLabel_->setText(tr("Camera preview error: %1").arg(QString::fromUtf8(e.what())));
    }
}

void EnrollmentPage::onEnrollClicked() {
    startEnroll(false);
}

void EnrollmentPage::onReEnrollClicked() {
    startEnroll(true);
}

void EnrollmentPage::startEnroll(bool reEnroll) {
    if (runner_->isBusy()) return;

    // Release the preview camera handle before the privileged helper
    // opens the same device — most UVC drivers reject a second
    // concurrent streaming open.
    stopPreview();

    const auto configOpt = facial_auth::Config::load("/etc/facial-auth/config.conf");
    const bool rgbMode = configOpt && configOpt->cameraMode == facial_auth::CameraMode::RGB;
    // RGB is only reachable here if it was already explicitly acknowledged
    // once via RiskDisclaimerDialog when saved in SettingsPage — no need
    // to re-prompt on every single enroll click.
    const QString cameraMode = rgbMode ? QStringLiteral("rgb") : QStringLiteral("ir");

    pendingAction_ = reEnroll ? PendingAction::ReEnroll : PendingAction::Enroll;
    statusLabel_->setText(tr("Look at the camera\xe2\x80\xa6 (a polkit authentication prompt may appear)"));
    runner_->enroll(currentUsername(), reEnroll, cameraMode, rgbMode);
}

void EnrollmentPage::onDeleteClicked() {
    if (runner_->isBusy()) return;
    pendingAction_ = PendingAction::Delete;
    runner_->remove(currentUsername());
}

void EnrollmentPage::refreshStatus() {
    if (runner_->isBusy()) return;
    pendingAction_ = PendingAction::Status;
    runner_->queryStatus(currentUsername());
}

void EnrollmentPage::onHelperFinished(EnrollHelperRunner::Result result) {
    const PendingAction action = pendingAction_;
    pendingAction_ = PendingAction::None;

    switch (action) {
        case PendingAction::Status:
            if (result.ok && result.enrolled) {
                statusLabel_->setText(tr("Enrolled (%1 mode, %2 samples, since %3)")
                                           .arg(result.cameraMode.toUpper())
                                           .arg(result.samples)
                                           .arg(result.enrolledAt));
                enrollButton_->setVisible(false);
                reEnrollButton_->setVisible(true);
                deleteButton_->setEnabled(true);
            } else if (result.ok) {
                statusLabel_->setText(tr("Not enrolled yet."));
                enrollButton_->setVisible(true);
                reEnrollButton_->setVisible(false);
                deleteButton_->setEnabled(false);
            } else {
                statusLabel_->setText(tr("Could not check enrollment status: %1").arg(result.message));
            }
            break;

        case PendingAction::Enroll:
        case PendingAction::ReEnroll:
            if (result.ok) {
                statusLabel_->setText(
                    tr("Enrolled successfully (%1 samples, %2 mode).").arg(result.samples).arg(result.cameraMode));
            } else {
                statusLabel_->setText(tr("Enrollment failed: %1").arg(result.message));
            }
            startPreview();  // resume preview now that the helper has released the camera
            refreshStatus();
            break;

        case PendingAction::Delete:
            statusLabel_->setText(result.ok ? tr("Enrollment deleted.")
                                             : tr("Delete failed: %1").arg(result.message));
            refreshStatus();
            break;

        case PendingAction::None:
        default:
            break;
    }
}
