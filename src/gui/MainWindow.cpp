#include "MainWindow.hpp"

#include <QTabWidget>

#include "EnrollmentPage.hpp"
#include "SettingsPage.hpp"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Facial Auth Control"));
    resize(480, 560);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(new EnrollmentPage(tabs), tr("Enrollment"));
    tabs->addTab(new SettingsPage(tabs), tr("Settings"));
    setCentralWidget(tabs);
}
