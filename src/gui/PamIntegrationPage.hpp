#pragma once

#include <vector>

#include <QWidget>

#include "EnrollHelperRunner.hpp"

class QLabel;
class QPushButton;

// The one page in facial-auth-control that can touch something outside
// facial-auth's own files: wiring pam_facial.so into (or out of) a real
// /etc/pam.d/service via facial-auth-enroll's --pam-enable/--pam-disable
// (see src/enroll/main.cpp's runPamEnable/runPamDisable and
// core/pam/PamServiceConfig.hpp). Every safeguard lives on the
// privileged-helper side of the pkexec boundary, not here — this page is
// only UI: a fixed, hardcoded row per allow-listed service (never
// user-editable, never sshd), a typed confirmation before Enable, and
// status read directly from /etc/pam.d (world-readable, same precedent
// as SettingsPage reading config.conf) so opening this tab never itself
// triggers a polkit prompt.
class PamIntegrationPage : public QWidget {
    Q_OBJECT

public:
    explicit PamIntegrationPage(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void onHelperFinished(EnrollHelperRunner::Result result);

private:
    struct ServiceRow {
        QString service;
        QLabel* stateLabel;
        QPushButton* actionButton;
    };

    void refreshAll();
    void refreshRow(ServiceRow& row);
    void onActionClicked(const QString& service);
    QString currentUsername() const;

    EnrollHelperRunner* runner_;
    QLabel* statusLabel_;
    std::vector<ServiceRow> rows_;
    QString pendingService_;
    bool pendingIsEnable_ = false;
};
