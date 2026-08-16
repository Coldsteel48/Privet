#include "EnrollmentPage.hpp"

#include <cstdlib>
#include <exception>

#include <QFont>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <opencv2/imgproc.hpp>

#include "core/camera/V4L2Camera.hpp"
#include "core/config/Config.hpp"

namespace {
constexpr int kPreviewIntervalMs = 100;  // ~10 fps, plenty for a positioning preview
constexpr int kGainSliderMin = 10;       // 0.10x
constexpr int kGainSliderMax = 600;      // 6.00x
constexpr int kGainSliderDefault = 100;  // 1.00x — matches Config::illuminationGain's default
}  // namespace

EnrollmentPage::EnrollmentPage(QWidget* parent)
    : QWidget(parent), runner_(new EnrollHelperRunner(this)) {
    previewLabel_ = new QLabel(tr("Camera preview unavailable"), this);
    previewLabel_->setMinimumSize(320, 240);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setStyleSheet(QStringLiteral("QLabel { background-color: black; color: white; }"));

    statusLabel_ = new QLabel(
        tr("Not checked yet \xe2\x80\x94 Enroll below, or press Test Recognition if you've "
           "already enrolled."),
        this);
    statusLabel_->setWordWrap(true);

    enrollButton_ = new QPushButton(tr("Enroll"), this);
    reEnrollButton_ = new QPushButton(tr("Re-enroll"), this);
    deleteButton_ = new QPushButton(tr("Delete Enrollment"), this);
    testButton_ = new QPushButton(tr("Test Recognition"), this);
    // Visibility/enablement never depends on a privileged status query (see
    // the class comment) — Re-enroll only appears once we actually learn
    // (from an enroll/re-enroll result) that an enrollment exists; the rest
    // are always available and just report a graceful outcome (e.g.
    // Test -> "unavailable" or Delete -> a no-op) if there's nothing to act on.
    reEnrollButton_->setVisible(false);

    connect(enrollButton_, &QPushButton::clicked, this, &EnrollmentPage::onEnrollClicked);
    connect(reEnrollButton_, &QPushButton::clicked, this, &EnrollmentPage::onReEnrollClicked);
    connect(deleteButton_, &QPushButton::clicked, this, &EnrollmentPage::onDeleteClicked);
    connect(testButton_, &QPushButton::clicked, this, &EnrollmentPage::onTestClicked);
    connect(runner_, &EnrollHelperRunner::finished, this, &EnrollmentPage::onHelperFinished);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(enrollButton_);
    buttonRow->addWidget(reEnrollButton_);
    buttonRow->addWidget(deleteButton_);
    buttonRow->addWidget(testButton_);

    // Illumination gain: a software brightness multiplier applied to every
    // captured frame (see V4L2Camera::captureFrame/CameraConfig::
    // illuminationGain) — not real IR-illuminator control, there's no such
    // control on the confirmed hardware. Dragging the slider only affects
    // this live preview; it has no effect on real enroll/verify captures
    // until "Save Illumination" is pressed, which is the only thing here
    // that elevates.
    auto* gainLabel = new QLabel(tr("Illumination:"), this);
    gainSlider_ = new QSlider(Qt::Horizontal, this);
    gainSlider_->setRange(kGainSliderMin, kGainSliderMax);
    gainSlider_->setValue(kGainSliderDefault);
    gainValueLabel_ = new QLabel(tr("1.00x"), this);
    gainValueLabel_->setMinimumWidth(48);
    saveGainButton_ = new QPushButton(tr("Save Illumination"), this);
    connect(gainSlider_, &QSlider::valueChanged, this, &EnrollmentPage::onGainSliderChanged);
    connect(saveGainButton_, &QPushButton::clicked, this, &EnrollmentPage::onSaveGainClicked);

    auto* gainRow = new QHBoxLayout();
    gainRow->addWidget(gainLabel);
    gainRow->addWidget(gainSlider_);
    gainRow->addWidget(gainValueLabel_);
    gainRow->addWidget(saveGainButton_);

    auto* gainHint = new QLabel(
        tr("Tip: drag the slider and press \xe2\x80\x9cTest Recognition\xe2\x80\x9d repeatedly "
           "until it reports a match, then Enroll/Re-enroll \xe2\x80\x94 that saves both your "
           "face and this illumination level together, so login uses the same setting."),
        this);
    gainHint->setWordWrap(true);
    QFont hintFont = gainHint->font();
    hintFont.setItalic(true);
    gainHint->setFont(hintFont);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(previewLabel_);
    layout->addWidget(statusLabel_);
    layout->addLayout(buttonRow);
    layout->addLayout(gainRow);
    layout->addWidget(gainHint);
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

double EnrollmentPage::gainValue() const {
    return gainSlider_->value() / 100.0;
}

void EnrollmentPage::startPreview() {
    if (camera_) return;  // already running

    const auto configOpt = facial_auth::Config::load("/etc/facial-auth/config.conf");
    if (!configOpt) {
        previewLabel_->setText(tr("No config at /etc/facial-auth/config.conf yet \xe2\x80\x94 "
                                   "set a camera device in Settings first."));
        return;
    }

    // Reflect the currently-saved gain so the slider matches what real
    // captures actually use; any unsaved drag from before a privileged
    // action ran is intentionally not preserved across a preview restart.
    gainSlider_->blockSignals(true);
    gainSlider_->setValue(static_cast<int>(configOpt->illuminationGain * 100.0));
    gainSlider_->blockSignals(false);
    gainValueLabel_->setText(QStringLiteral("%1x").arg(gainValue(), 0, 'f', 2));

    facial_auth::CameraConfig cameraConfig;
    cameraConfig.devicePath = configOpt->devicePath;
    cameraConfig.pixelFormat = configOpt->pixelFormat;
    cameraConfig.width = configOpt->frameWidth;
    cameraConfig.height = configOpt->frameHeight;
    cameraConfig.timeoutMs = configOpt->captureTimeoutMs;
    // Deliberately NOT setting illuminationGain here: the preview applies
    // the live slider value itself in onPreviewTick() (so dragging updates
    // instantly without reopening the camera); baking it into CameraConfig
    // too would double-apply it.

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

        cv::Mat frame = *frameOpt;
        const double gain = gainValue();
        if (gain != 1.0) {
            cv::convertScaleAbs(frame, frame, gain, 0);
        }
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

void EnrollmentPage::onGainSliderChanged(int value) {
    gainValueLabel_->setText(QStringLiteral("%1x").arg(value / 100.0, 0, 'f', 2));
}

void EnrollmentPage::onSaveGainClicked() {
    if (runner_->isBusy()) return;
    pendingAction_ = PendingAction::SaveGain;
    statusLabel_->setText(tr("Saving illumination setting\xe2\x80\xa6 check for a polkit "
                              "authentication prompt."));
    runner_->writeConfig(
        {QStringLiteral("illumination_gain=%1").arg(gainValue(), 0, 'f', 2)});
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
    // The illumination value currently on the slider is persisted as part
    // of this same privileged call — see EnrollHelperRunner::enroll() and
    // facial-auth-enroll's --illumination-gain — so whatever worked well
    // enough to enroll with becomes what real verify captures use too.
    runner_->enroll(currentUsername(), reEnroll, cameraMode, rgbMode, gainValue());
}

void EnrollmentPage::onDeleteClicked() {
    if (runner_->isBusy()) return;
    pendingAction_ = PendingAction::Delete;
    runner_->remove(currentUsername());
}

void EnrollmentPage::onTestClicked() {
    if (runner_->isBusy()) return;
    stopPreview();  // release the camera for the privileged helper, same as enroll
    pendingAction_ = PendingAction::Test;
    statusLabel_->setText(tr("Testing recognition\xe2\x80\xa6 look at the camera (a polkit "
                              "authentication prompt may appear)"));
    runner_->test(currentUsername());
}

void EnrollmentPage::onHelperFinished(EnrollHelperRunner::Result result) {
    const PendingAction action = pendingAction_;
    pendingAction_ = PendingAction::None;

    switch (action) {
        case PendingAction::Enroll:
        case PendingAction::ReEnroll:
            if (result.ok) {
                statusLabel_->setText(tr("Enrolled successfully (%1 samples, %2 mode, %3).")
                                           .arg(result.samples)
                                           .arg(result.cameraMode)
                                           .arg(result.enrolledAt));
                reEnrollButton_->setVisible(true);
            } else if (result.message.contains(QLatin1String("already enrolled"))) {
                statusLabel_->setText(tr("Already enrolled \xe2\x80\x94 use Re-enroll to overwrite."));
                reEnrollButton_->setVisible(true);
            } else {
                statusLabel_->setText(tr("Enrollment failed: %1").arg(result.message));
            }
            startPreview();  // resume preview now that the helper has released the camera
            break;

        case PendingAction::Delete:
            statusLabel_->setText(result.ok ? tr("Enrollment deleted.")
                                             : tr("Delete failed: %1").arg(result.message));
            if (result.ok) reEnrollButton_->setVisible(false);
            break;

        case PendingAction::Test:
            if (!result.ok) {
                statusLabel_->setText(tr("Test failed to run: %1").arg(result.message));
            } else if (result.matchOutcome == QLatin1String("true")) {
                statusLabel_->setText(tr("\xe2\x9c\x93 Recognized \xe2\x80\x94 this would succeed at login."));
                reEnrollButton_->setVisible(true);  // a match implies an enrollment exists
            } else if (result.matchOutcome == QLatin1String("false")) {
                statusLabel_->setText(
                    tr("A face was seen but didn't match your enrollment. Try adjusting "
                       "illumination, or Re-enroll if your appearance has changed."));
                reEnrollButton_->setVisible(true);
            } else {
                statusLabel_->setText(
                    tr("No face detected \xe2\x80\x94 not enrolled, camera unavailable, or no "
                       "face was seen in time. Check the preview and try again."));
            }
            startPreview();  // resume preview now that the helper has released the camera
            break;

        case PendingAction::SaveGain:
            statusLabel_->setText(result.ok
                                       ? tr("Illumination setting saved \xe2\x80\x94 future "
                                            "enroll/verify captures will use it.")
                                       : tr("Failed to save illumination setting: %1").arg(result.message));
            break;

        case PendingAction::None:
        default:
            break;
    }
}
