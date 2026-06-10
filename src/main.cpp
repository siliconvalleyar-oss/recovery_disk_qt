#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("recovery-qt");
    app.setOrganizationName("recovery-tools");
    app.setApplicationVersion("1.0.0");

    app.setStyleSheet(
        "QMainWindow { background: #1e1e1e; }"
        "QWidget { color: #ddd; background: #2d2d2d; }"
        "QGroupBox { border: 1px solid #444; border-radius: 4px; margin-top: 8px;"
        "  padding-top: 16px; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QProgressBar { border: 1px solid #444; border-radius: 4px; text-align: center;"
        "  background: #333; color: #ddd; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "  stop:0 #2e7d32, stop:1 #4caf50); border-radius: 3px; }"
        "QListView, QTableView, QTreeView { background: #252525; alternate-background-color: #2a2a2a;"
        "  border: 1px solid #444; gridline-color: #333; }"
        "QHeaderView::section { background: #333; color: #ddd; border: 1px solid #444;"
        "  padding: 4px; }"
        "QTabWidget::pane { border: 1px solid #444; background: #2d2d2d; }"
        "QTabBar::tab { background: #333; color: #888; padding: 6px 12px;"
        "  border: 1px solid #444; border-bottom: none; }"
        "QTabBar::tab:selected { background: #2d2d2d; color: #fff; }"
        "QComboBox, QSpinBox, QLineEdit { background: #333; color: #ddd;"
        "  border: 1px solid #555; padding: 4px; border-radius: 3px; }"
        "QCheckBox { spacing: 6px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; }"
        "QScrollBar:vertical { background: #2d2d2d; width: 10px; }"
        "QScrollBar::handle:vertical { background: #555; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QToolBar { background: #333; border: none; spacing: 6px; padding: 4px; }"
        "QLabel { color: #ccc; }"
        "QStatusBar { background: #252525; border-top: 1px solid #444; }"
        "QMenuBar { background: #333; }"
        "QMenuBar::item:selected { background: #444; }"
    );

    MainWindow w;
    w.show();

    return app.exec();
}
