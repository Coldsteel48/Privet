// facial-auth-control: unprivileged Qt6 GUI for managing facial login
// enrollment and settings. Never touches /var/lib/facial-auth/ or
// /etc/facial-auth/config.conf directly — see EnrollHelperRunner and the
// project plan's "Privilege architecture" section.

#include <QApplication>

#include "MainWindow.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("facial-auth-control"));
    app.setApplicationDisplayName(QStringLiteral("Facial Auth Control"));

    MainWindow window;
    window.show();

    return app.exec();
}
