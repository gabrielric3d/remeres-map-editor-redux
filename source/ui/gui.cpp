#include "app/main.h"
#include "ui/gui.h"
#include "ui/main_frame.h"
#include "map/map.h"

GUI g_gui;

GUI::GUI() :
    root(nullptr),
    secondary_map(nullptr),
    house_palette(nullptr),
    tool_options(nullptr),
    mode(SELECTION_MODE),
    pasting(false),
    disabled_counter(0),
    hotkeys_enabled(true) {
}

GUI::~GUI() {
}

void GUI::UpdateMenubar() {
    if(root) root->UpdateMenubar();
}

Map& GUI::GetCurrentMap() {
    static Map dummyMap;
    return dummyMap;
}

void SetWindowToolTip(QWidget* a, const QString& tip) {
    if(a) a->setToolTip(tip);
}
