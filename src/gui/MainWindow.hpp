#pragma once

#include <QMainWindow>

// Hosts the tabbed page shell. Deliberately a thin QTabWidget host —
// future settings/pages (Phase 2+) slot in as additional tabs without
// requiring this shell to be redesigned.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
};
