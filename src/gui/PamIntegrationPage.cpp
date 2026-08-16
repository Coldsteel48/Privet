#include "PamIntegrationPage.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include <QFont>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "PamEnableConfirmDialog.hpp"
#include "core/pam/PamServiceConfig.hpp"

PamIntegrationPage::PamIntegrationPage(QWidget* parent)
    : QWidget(parent), runner_(new EnrollHelperRunner(this)) {
    auto* banner = new QLabel(
        tr("<b>This did not touch anything by default.</b> Enabling facial login here edits a "
           "real /etc/pam.d file. Only the services listed below are ever offered \xe2\x80\x94 "
           "sshd and anything else are never reachable from this app. Facial login is always "
           "added as an <i>additional</i> way in (\xe2\x80\x9csufficient\xe2\x80\x9d); your "
           "password keeps working exactly as before. See docs/testing-safely.md."),
        this);
    banner->setWordWrap(true);
    banner->setStyleSheet(QStringLiteral("QLabel { padding: 6px; }"));

    auto* form = new QFormLayout();
    for (const char* service : facial_auth::kAllowedPamServices) {
        auto* stateLabel = new QLabel(this);
        auto* actionButton = new QPushButton(this);
        const QString serviceName = QString::fromUtf8(service);
        connect(actionButton, &QPushButton::clicked, this,
                [this, serviceName] { onActionClicked(serviceName); });

        auto* row = new QWidget(this);
        auto* rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(stateLabel);
        rowLayout->addWidget(actionButton);
        form->addRow(serviceName, row);

        rows_.push_back(ServiceRow{serviceName, stateLabel, actionButton});
    }

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(banner);
    layout->addLayout(form);
    layout->addWidget(statusLabel_);
    layout->addStretch();

    connect(runner_, &EnrollHelperRunner::finished, this, &PamIntegrationPage::onHelperFinished);

    refreshAll();
}

void PamIntegrationPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refreshAll();
}

QString PamIntegrationPage::currentUsername() const {
    const char* user = std::getenv("USER");
    return user ? QString::fromUtf8(user) : QString();
}

void PamIntegrationPage::refreshAll() {
    for (auto& row : rows_) refreshRow(row);
}

void PamIntegrationPage::refreshRow(ServiceRow& row) {
    const std::string path = "/etc/pam.d/" + row.service.toStdString();
    std::ifstream in(path);
    if (!in.is_open()) {
        row.stateLabel->setText(tr("not installed on this system"));
        row.actionButton->setVisible(false);
        return;
    }

    std::ostringstream buf;
    buf << in.rdbuf();
    switch (facial_auth::detectPamFacialState(buf.str())) {
        case facial_auth::PamFacialState::Absent:
            row.stateLabel->setText(tr("Facial login: disabled"));
            row.actionButton->setText(tr("Enable\xe2\x80\xa6"));
            row.actionButton->setVisible(true);
            break;
        case facial_auth::PamFacialState::EnabledSafe:
            row.stateLabel->setText(tr("\xe2\x9c\x93 Facial login: enabled"));
            row.actionButton->setText(tr("Disable"));
            row.actionButton->setVisible(true);
            break;
        case facial_auth::PamFacialState::EnabledUnsafe:
            row.stateLabel->setText(
                tr("\xe2\x9a\xa0 A pam_facial.so line exists here with an unexpected control "
                   "flag (hand-edited?) \xe2\x80\x94 not managed automatically."));
            row.actionButton->setText(tr("Remove pam_facial.so"));
            row.actionButton->setVisible(true);
            break;
    }
}

void PamIntegrationPage::onActionClicked(const QString& service) {
    if (runner_->isBusy()) return;

    const std::string path = "/etc/pam.d/" + service.toStdString();
    std::ifstream in(path);
    if (!in.is_open()) return;
    std::ostringstream buf;
    buf << in.rdbuf();
    const bool currentlyAbsent =
        facial_auth::detectPamFacialState(buf.str()) == facial_auth::PamFacialState::Absent;

    if (currentlyAbsent) {
        if (!PamEnableConfirmDialog::confirm(service, this)) return;
        pendingService_ = service;
        pendingIsEnable_ = true;
        statusLabel_->setText(
            tr("Running pre-enable recognition check (5 attempts, need at least 4 to match)"
               "\xe2\x80\xa6 this can take up to about 30 seconds. Check for a polkit "
               "authentication prompt."));
        runner_->enablePam(service, currentUsername());
    } else {
        pendingService_ = service;
        pendingIsEnable_ = false;
        statusLabel_->setText(tr("Removing facial login from \xe2\x80\x9c%1\xe2\x80\x9d\xe2\x80\xa6 "
                                  "check for a polkit authentication prompt.")
                                   .arg(service));
        runner_->disablePam(service);
    }
}

void PamIntegrationPage::onHelperFinished(EnrollHelperRunner::Result result) {
    const QString service = pendingService_;
    const bool wasEnable = pendingIsEnable_;
    pendingService_.clear();
    if (service.isEmpty()) return;  // defensive: finished() fired with no pending action tracked

    if (wasEnable) {
        if (result.ok) {
            statusLabel_->setText(tr("\xe2\x9c\x93 Enabled on \xe2\x80\x9c%1\xe2\x80\x9d (passed %2/5 "
                                      "recognition attempts).")
                                       .arg(service)
                                       .arg(result.testPasses));
        } else {
            statusLabel_->setText(
                tr("Not enabled on \xe2\x80\x9c%1\xe2\x80\x9d: %2").arg(service, result.message));
        }
    } else {
        statusLabel_->setText(result.ok
                                   ? tr("Removed facial login from \xe2\x80\x9c%1\xe2\x80\x9d.").arg(service)
                                   : tr("Failed to remove: %1").arg(result.message));
    }
    refreshAll();
}
