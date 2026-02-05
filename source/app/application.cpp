//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/application.h"
#include "app/main.h"
#include "ui/gui.h"
#include "ui/main_frame.h"
#include "app/settings.h"
#include "util/file_system.h"
#include "editor/hotkey_manager.h"
#include "app/managers/version_manager.h"
#include "app/client_version.h"

// Temporary stubs for missing headers
// #include "ui/dialog_util.h"
// #include "ui/about_window.h"
// #include "ui/main_menubar.h"
// ... others

#include <spdlog/spdlog.h>
#include <iostream>

Application::Application(int& argc, char** argv) : QApplication(argc, argv) {
    setApplicationName("Remere's Map Editor");
    setApplicationVersion(__W_RME_VERSION__);
}

Application::~Application() {
}

bool Application::Init() {
    // Logging setup
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);
    spdlog::info("RME starting up - logging enabled");

    std::cout << "This is free software: you are free to change and redistribute it." << std::endl;
    std::cout << "There is NO WARRANTY, to the extent permitted by law." << std::endl;
    std::cout << "Review COPYING in RME distribution for details." << std::endl;

    // Discover data directory
    FileSystem::DiscoverDataDirectory("clients.xml");

    // Load internal stuff
    g_settings.load();
    FixVersionDiscrapencies();
    g_hotkeys.LoadHotkeys();
    ClientVersion::loadVersions();

    // Initialize Graphics (Preload sprites if needed)
    // g_gui.gfx.loadEditorSprites(); // Needs OpenGL context? Maybe later.

    m_file_to_open = "";
    ParseCommandLineMap(m_file_to_open);

    // Create Main Window
    g_gui.root = new MainFrame(__W_RME_APPLICATION_NAME__);
    g_gui.root->show();
    // g_gui.SetTitle("");

    // Load Perspective (stubbed)
    // g_gui.LoadPerspective();

    // Recent Files
    // g_gui.root->LoadRecentFiles();

    // Welcome Dialog (stubbed)
    /*
    if (g_settings.getInteger(Config::WELCOME_DIALOG) == 1 && m_file_to_open.isEmpty()) {
        g_gui.ShowWelcomeDialog(QIcon());
    }
    */

    // Recovery logic would go here

    m_startup = true;
    return true;
}

int Application::OnExit() {
    return 0;
}

void Application::Unload() {
    // g_gui.CloseAllEditors();
    g_version.UnloadVersion();
    g_hotkeys.SaveHotkeys();
    // g_gui.root->SaveRecentFiles();
    ClientVersion::saveVersions();
    ClientVersion::unloadVersions();
    g_settings.save(true);
    // g_gui.root = nullptr;
}

void Application::FixVersionDiscrapencies() {
    // Ported minimal logic
    if (g_settings.getInteger(Config::VERSION_ID) < MAKE_VERSION_ID(1, 0, 5)) {
        g_settings.setInteger(Config::USE_MEMCACHED_SPRITES_TO_SAVE, 0);
    }
    if (g_settings.getInteger(Config::VERSION_ID) < __RME_VERSION_ID__ && ClientVersion::getLatestVersion() != nullptr) {
        g_settings.setInteger(Config::DEFAULT_CLIENT_VERSION, ClientVersion::getLatestVersion()->getID());
    }
    g_settings.setInteger(Config::VERSION_ID, __RME_VERSION_ID__);
}

bool Application::ParseCommandLineMap(wxString& fileName) {
    QStringList args = arguments();
    if (args.size() == 2) {
        fileName = args[1];
        return true;
    }
    return false;
}

// Entry Point
int main(int argc, char* argv[]) {
    Application app(argc, argv);

    if (!app.Init()) {
        return 1;
    }

    return app.exec();
}
