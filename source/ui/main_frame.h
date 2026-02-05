//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_UI_MAIN_FRAME_H_
#define RME_UI_MAIN_FRAME_H_

#include "app/main.h"
#include <QtWidgets/QMainWindow>

class NanoVGCanvas;

class MainFrame : public QMainWindow {
    Q_OBJECT
public:
    MainFrame(const QString& title, QWidget* parent = nullptr);
    ~MainFrame() override;

    void UpdateMenubar();
    bool DoQueryClose();
    bool DoQuerySave(bool doclose = true);
    bool DoQuerySaveTileset(bool doclose = true);
    bool DoQueryImportCreatures();
    bool LoadMap(FileName name);

    void AddRecentFile(const FileName& file);
    void LoadRecentFiles();
    void SaveRecentFiles();
    std::vector<wxString> GetRecentFiles();

    void UpdateFloorMenu();
    void OnExit();

protected:
    void closeEvent(QCloseEvent* event) override;

    // Stubbed pointers for now
    // std::unique_ptr<MainMenuBar> menu_bar;
    // std::unique_ptr<MainToolBar> tool_bar;

    friend class Application;
    friend class GUI;
};

#endif
