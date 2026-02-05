#ifndef RME_MAIN_WINDOW_H_
#define RME_MAIN_WINDOW_H_

#include "app/main.h"
#include <QtWidgets/QLabel>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    virtual ~MainWindow();

private:
    void setupUi();
};

#endif
