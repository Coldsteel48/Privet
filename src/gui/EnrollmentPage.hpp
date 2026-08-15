#pragma once

#include <memory>

#include <QWidget>

#include "EnrollHelperRunner.hpp"

class QLabel;
class QPushButton;
class QTimer;

namespace facial_auth {
class V4L2Camera;
}

// Live camera preview (opened directly, unprivileged — requires the
// invoking user to be in the `video` group, a standard Linux convention
// for webcam access) plus Enroll/Re-enroll/Delete actions, which never
// touch storage directly and instead go through EnrollHelperRunner
// (pkexec facial-auth-enroll). The preview camera is closed before every
// privileged helper invocation and reopened after, since most UVC drivers
// don't support two processes streaming the same device node at once.
class EnrollmentPage : public QWidget {
    Q_OBJECT

public:
    explicit EnrollmentPage(QWidget* parent = nullptr);
    ~EnrollmentPage() override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onPreviewTick();
    void onEnrollClicked();
    void onReEnrollClicked();
    void onDeleteClicked();
    void onHelperFinished(EnrollHelperRunner::Result result);

private:
    void startPreview();
    void stopPreview();
    void startEnroll(bool reEnroll);
    void refreshStatus();
    QString currentUsername() const;

    enum class PendingAction { None, Enroll, ReEnroll, Delete, Status };

    EnrollHelperRunner* runner_;
    QLabel* previewLabel_;
    QLabel* statusLabel_;
    QPushButton* enrollButton_;
    QPushButton* reEnrollButton_;
    QPushButton* deleteButton_;
    QTimer* previewTimer_;
    std::unique_ptr<facial_auth::V4L2Camera> camera_;
    PendingAction pendingAction_ = PendingAction::None;
};
