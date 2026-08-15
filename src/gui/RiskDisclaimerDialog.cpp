#include "RiskDisclaimerDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

RiskDisclaimerDialog::RiskDisclaimerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Regular Camera \xe2\x80\x94 At Your Own Risk"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    auto* warning = new QLabel(
        tr("<b>A regular (non-IR) camera has no depth or liveness signal.</b><br><br>"
           "It can be fooled by a photo, a video, or a printed picture of your face. "
           "An IR camera (like the one Windows Hello uses) is strongly recommended "
           "instead.<br><br>"
           "Only continue if you understand and accept this risk."),
        this);
    warning->setWordWrap(true);
    layout->addWidget(warning);

    acknowledgeCheckbox_ = new QCheckBox(tr("I understand the risk and want to proceed anyway"), this);
    layout->addWidget(acknowledgeCheckbox_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(acknowledgeCheckbox_, &QCheckBox::toggled, buttons->button(QDialogButtonBox::Ok),
            &QWidget::setEnabled);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool RiskDisclaimerDialog::confirm(QWidget* parent) {
    RiskDisclaimerDialog dialog(parent);
    return dialog.exec() == QDialog::Accepted;
}
