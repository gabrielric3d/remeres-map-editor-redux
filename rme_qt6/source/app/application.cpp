#include "app/application.h"

Application::Application(int& argc, char** argv) : QApplication(argc, argv) {
    setApplicationName(__RME_APPLICATION_NAME_STR__);
    setApplicationVersion(__RME_VERSION__.c_str());
    setOrganizationName("OTAcademy");
    setOrganizationDomain("otacademy.com"); // Placeholder
}

Application::~Application() {
}

bool Application::init() {
    // Basic initialization logic here
    // Ported from OnInit

    // Seed random (if not handled elsewhere)
    srand(time(nullptr));

    // Handle command line
    QString fileName;
    if (parseCommandLineMap(fileName)) {
        // TODO: Open file
        qDebug() << "Requested to open file:" << fileName;
    }

    return true;
}

void Application::fixVersionDiscrepancies() {
    // TODO: implementation
}

bool Application::parseCommandLineMap(QString& fileName) {
    const QStringList args = arguments();
    if (args.size() > 1) {
        fileName = args.at(1);
        return true;
    }
    return false;
}
