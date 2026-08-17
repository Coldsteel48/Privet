#include "ConfirmDialog.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

ConfirmDialog::ConfirmDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Facial Login"));
    setModal(true);
    // This pops up unannounced (there's no window it's transient for —
    // the invoking terminal or lock screen isn't a Qt window this
    // process knows about), so keep it above everything else rather
    // than risk it opening unnoticed behind the current window.
    setWindowFlag(Qt::WindowStaysOnTopHint, true);

    auto* layout = new QVBoxLayout(this);

    auto* question = new QLabel(tr("Authenticate using face recognition?"), this);
    question->setWordWrap(true);
    layout->addWidget(question);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool ConfirmDialog::confirm() {
    ConfirmDialog dialog;
    return dialog.exec() == QDialog::Accepted;
}
