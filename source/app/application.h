//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_APPLICATION_H_
#define RME_APPLICATION_H_

#include "app/main.h"
#include <QtWidgets/QApplication>

class Application : public QApplication {
    Q_OBJECT
public:
    Application(int& argc, char** argv);
    ~Application();

    bool Init();
    int OnExit();
    void Unload();

private:
    bool m_startup;
    wxString m_file_to_open;
    void FixVersionDiscrapencies();
    bool ParseCommandLineMap(wxString& fileName);

#ifdef _USE_PROCESS_COM
    // RMEProcessServer* m_proc_server;
    // wxSingleInstanceChecker* m_single_instance_checker;
#endif
};

// Accessor for the application instance
inline Application& wxGetApp() { return *static_cast<Application*>(qApp); }

#endif
