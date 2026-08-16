#include "PamEnableConfirmDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

PamEnableConfirmDialog::PamEnableConfirmDialog(const QString& service, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Enable Facial Login for \xe2\x80\x9c%1\xe2\x80\x9d").arg(service));
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    auto* warning = new QLabel(
        tr("<b>This changes how \xe2\x80\x9c%1\xe2\x80\x9d authenticates on this machine.</b><br><br>"
           "Before continuing:<br>"
           "&bull; Have a spare way in \xe2\x80\x94 a root shell already open, another logged-in "
           "account, or physical access to recovery media \xe2\x80\x94 in case something is "
           "wrong.<br>"
           "&bull; This will run 5 real recognition attempts right now and only proceed if at "
           "least 4 match your enrollment. If it doesn't reach that, nothing is written.<br>"
           "&bull; Your password will keep working exactly as before \xe2\x80\x94 facial login is "
           "only ever an additional way in, inserted as \xe2\x80\x9csufficient\xe2\x80\x9d, never "
           "a replacement.")
            .arg(service),
        this);
    warning->setWordWrap(true);
    layout->addWidget(warning);

    acknowledgeCheckbox_ =
        new QCheckBox(tr("I have a spare way to log in ready, and understand the risk"), this);
    layout->addWidget(acknowledgeCheckbox_);

    auto* confirmLabel =
        new QLabel(tr("Type CONFIRM to proceed:"), this);
    layout->addWidget(confirmLabel);
    confirmEdit_ = new QLineEdit(this);
    layout->addWidget(confirmEdit_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    okButton_ = buttons->button(QDialogButtonBox::Ok);
    okButton_->setEnabled(false);
    connect(acknowledgeCheckbox_, &QCheckBox::toggled, this, &PamEnableConfirmDialog::updateOkEnabled);
    connect(confirmEdit_, &QLineEdit::textChanged, this, &PamEnableConfirmDialog::updateOkEnabled);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void PamEnableConfirmDialog::updateOkEnabled() {
    okButton_->setEnabled(acknowledgeCheckbox_->isChecked() &&
                           confirmEdit_->text() == QLatin1String("CONFIRM"));
}

bool PamEnableConfirmDialog::confirm(const QString& service, QWidget* parent) {
    PamEnableConfirmDialog dialog(service, parent);
    return dialog.exec() == QDialog::Accepted;
}
