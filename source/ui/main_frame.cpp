//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/main_frame.h"
#include "ui/gui.h"
#include "util/nanovg_canvas.h"
#include <QtWidgets/QLabel>
#include <QtGui/QCloseEvent>

// Stub implementation

MainFrame::MainFrame(const QString& title, QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(title);
    resize(1024, 768);

    // Set a central widget placeholder
    // In real implementation, this would be the MapTabbook or similar

    // For testing NanoVG port:
    // NanoVGCanvas* canvas = new NanoVGCanvas(this);
    // setCentralWidget(canvas);

    QLabel* label = new QLabel("Qt Port In Progress\nPhase 1: Architecture Migration", this);
    label->setAlignment(Qt::AlignCenter);
    setCentralWidget(label);

    // Status Bar
    statusBar()->showMessage("Welcome to RME Qt Port");
}

MainFrame::~MainFrame() = default;

void MainFrame::UpdateMenubar() {
    // Stub
}

bool MainFrame::DoQueryClose() {
    return true; // Always allow close for now
}

bool MainFrame::DoQuerySave(bool doclose) {
    return true;
}

bool MainFrame::DoQuerySaveTileset(bool doclose) {
    return true;
}

bool MainFrame::DoQueryImportCreatures() {
    return true;
}

bool MainFrame::LoadMap(FileName name) {
    // return g_gui.LoadMap(name);
    return false;
}

void MainFrame::AddRecentFile(const FileName& file) {
    // Stub
}

void MainFrame::LoadRecentFiles() {
    // Stub
}

void MainFrame::SaveRecentFiles() {
    // Stub
}

std::vector<wxString> MainFrame::GetRecentFiles() {
    return {};
}

void MainFrame::UpdateFloorMenu() {
    // Stub
}

void MainFrame::OnExit() {
    close();
}

void MainFrame::closeEvent(QCloseEvent* event) {
    // Logic to ask for save
    if (DoQueryClose()) {
        event->accept();
        // Application::Unload() called by main cleanup usually, or here
    } else {
        event->ignore();
    }
}
