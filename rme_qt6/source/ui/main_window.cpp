#include "ui/main_window.h"
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMenu>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QStatusBar>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setupUi();
}

MainWindow::~MainWindow() {
}

void MainWindow::setupUi() {
    setWindowTitle(__RME_APPLICATION_NAME_STR__);
    resize(1024, 768);

    // Central Widget
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout* layout = new QVBoxLayout(centralWidget);
    QLabel* label = new QLabel("Welcome to Remere's Map Editor - Qt6 Port", centralWidget);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);

    // Menu Bar
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    QMenu* fileMenu = menuBar->addMenu("&File");
    QAction* exitAction = fileMenu->addAction("E&xit");
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    // Status Bar
    statusBar()->showMessage("Ready");
}
