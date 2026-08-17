// facial-auth-control: unprivileged Qt6 GUI for managing facial login
// enrollment and settings. Never touches /var/lib/facial-auth/ or
// /etc/facial-auth/config.conf directly — see EnrollHelperRunner and the
// project plan's "Privilege architecture" section.

#include <QApplication>
#include <QIcon>

#include "MainWindow.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("facial-auth-control"));
    app.setApplicationDisplayName(QStringLiteral("Facial Auth Control"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("facial-auth-control")));

    MainWindow window;
    window.show();

    return app.exec();
}
