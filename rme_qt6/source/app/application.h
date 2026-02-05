#ifndef RME_APPLICATION_H_
#define RME_APPLICATION_H_

#include "app/main.h"

class Application : public QApplication {
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    virtual ~Application();

    bool init();

private:
    void fixVersionDiscrepancies();
    bool parseCommandLineMap(QString& fileName);
};

#endif
