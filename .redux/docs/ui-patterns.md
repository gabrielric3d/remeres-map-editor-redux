# RME Redux - UI Patterns

## Theme System (`source/ui/theme.h`)

**All colors via `Theme::Get(Theme::Role::X)`**:

| Role | Usage |
|------|-------|
| Surface | General surface color |
| Background | Window/dialog background |
| Header | Header/toolbar areas |
| Accent | Primary accent |
| AccentHover | Accent on hover |
| Text | Primary text |
| TextSubtle | Secondary/muted text |
| TextOnAccent | Text on accent background |
| Border | General borders |
| Selected | Selected items |
| Error, Warning, Success | Status colors |
| CardBase, CardBaseHover, CardBorder | Card/panel components |
| PanelBackground, RaisedSurface, FooterSurface | Panel variants |
| SelectionFill | Selection overlay |
| PrimaryButton, PrimaryButtonHover | Primary buttons |
| SecondaryButton, SecondaryButtonHover | Secondary buttons |
| ButtonDisabled | Disabled state |
| TooltipBg, TooltipLabel, TooltipBodyText, etc. | Tooltip system |
| TooltipBorderWaypoint, TooltipBorderItem, etc. | Tooltip borders |

**Additional helpers**:
```cpp
wxFont font = Theme::GetFont(9, false);   // size, bold
int px = Theme::Grid(4);                   // DPI-aware grid units
Theme::setType(Theme::Type::Dark);         // Dark/Light/System
```

## Creating a New Dialog — Step by Step

### 1. Header (`source/ui/dialogs/my_dialog.h`)
```cpp
#ifndef RME_MY_DIALOG_H
#define RME_MY_DIALOG_H

#include <wx/wx.h>

class Editor;

class MyDialog : public wxDialog {
public:
    MyDialog(wxWindow* parent, Editor& editor);
    ~MyDialog() { }

private:
    void OnOK(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);

    Editor& editor;
    wxTextCtrl* m_input;
};

#endif
```

### 2. Implementation (`source/ui/dialogs/my_dialog.cpp`)
```cpp
#include "app/main.h"
#include "ui/dialogs/my_dialog.h"
#include "editor/editor.h"
#include "ui/theme.h"
#include "util/image_manager.h"

MyDialog::MyDialog(wxWindow* parent, Editor& editor) :
    wxDialog(parent, wxID_ANY, "My Dialog", wxDefaultPosition, wxDefaultSize),
    editor(editor) {

    // Main sizer
    wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

    // Controls
    sizer->Add(newd wxStaticText(this, wxID_ANY, "Label:"),
               0, wxTOP | wxLEFT | wxRIGHT, 20);

    m_input = newd wxTextCtrl(this, wxID_ANY);
    sizer->Add(m_input, 0, wxLEFT | wxRIGHT | wxEXPAND, 20);

    // OK/Cancel buttons
    wxSizer* btn_sizer = newd wxBoxSizer(wxHORIZONTAL);

    wxButton* ok = newd wxButton(this, wxID_OK, "OK");
    ok->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_CHECK, wxSize(16, 16)));
    btn_sizer->Add(ok, wxSizerFlags(1).Center());

    wxButton* cancel = newd wxButton(this, wxID_CANCEL, "Cancel");
    cancel->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_XMARK, wxSize(16, 16)));
    btn_sizer->Add(cancel, wxSizerFlags(1).Center());

    sizer->Add(btn_sizer, 0, wxALL | wxCENTER, 20);

    SetSizerAndFit(sizer);
    Centre(wxBOTH);

    // Icon
    wxIcon icon;
    icon.CopyFromBitmap(IMAGE_MANAGER.GetBitmap(ICON_YOUR_ICON, wxSize(32, 32)));
    SetIcon(icon);

    // Event binding (prefer Bind over event tables)
    ok->Bind(wxEVT_BUTTON, &MyDialog::OnOK, this);
    cancel->Bind(wxEVT_BUTTON, &MyDialog::OnCancel, this);
}

void MyDialog::OnOK(wxCommandEvent&) { EndModal(wxID_OK); }
void MyDialog::OnCancel(wxCommandEvent&) { EndModal(wxID_CANCEL); }
```

### 3. Register in CMakeLists.txt
Add both `.h` and `.cpp` in appropriate section of `source/CMakeLists.txt`.

### 4. Wire into GUI
```cpp
// In gui.h
void ShowMyDialog();

// In gui.cpp
void GUI::ShowMyDialog() {
    MyDialog dlg(root, *GetCurrentEditor());
    dlg.ShowModal();
}
```

### 5. Add menu entry (optional)
In `data/menubar.xml`:
```xml
<item name="My Dialog..." action="MY_DIALOG_ACTION" help="Description" />
```
And handle in the menu action dispatcher.

## Event Binding Patterns

**Modern Bind() — PREFERRED**:
```cpp
button->Bind(wxEVT_BUTTON, &MyClass::OnClick, this);
text->Bind(wxEVT_TEXT, &MyClass::OnTextChanged, this);
list->Bind(wxEVT_LIST_ITEM_SELECTED, &MyClass::OnSelect, this);
```

**Legacy event table — used in complex dialogs**:
```cpp
// Header
wxDECLARE_EVENT_TABLE();

// Implementation
wxBEGIN_EVENT_TABLE(MyDialog, wxDialog)
    EVT_BUTTON(ID_MY_BTN, MyDialog::OnMyButton)
wxEND_EVENT_TABLE()
```

**Custom events**:
```cpp
wxDECLARE_EVENT(EVT_MY_EVENT, wxCommandEvent);
wxDEFINE_EVENT(EVT_MY_EVENT, wxCommandEvent);
// Fire: wxCommandEvent evt(EVT_MY_EVENT); ProcessEvent(evt);
```

## Local Dialog IDs Pattern

```cpp
namespace {
    enum {
        ID_MY_CONTROL_1 = wxID_HIGHEST + 5000,
        ID_MY_CONTROL_2,
        ID_MY_CONTROL_3,
    };
}
```

## GUI Singleton (`source/ui/gui.h`)

Key methods:
```cpp
extern GUI g_gui;

// Editor/Tab access
Editor* GetCurrentEditor();
Map& GetCurrentMap();
MapTab* GetCurrentMapTab() const;

// View control
void SetScreenCenterPosition(Position pos);
void RefreshView();
void ChangeFloor(int newfloor);
double GetCurrentZoom();
void SetCurrentZoom(double zoom);

// Mode
void SetSelectionMode();
void SetDrawingMode();
bool IsSelectionMode() const;
bool IsDrawingMode() const;

// Rectangle pick (area selection)
void BeginRectanglePick(RectanglePickComplete onComplete, ...);
void CancelRectanglePick();
bool IsRectanglePicking() const;

// UI updates
void UpdateMenubar();
void SetStatusText(wxString text);
wxAuiManager* GetAuiManager() const;
```

Public members: `aui_manager`, `tabbook`, `root`, `copybuffer`, `gfx`, `house_palette`, `tool_options`, `tile_properties_panel`

## Custom Controls

### NanoVGCanvas (`source/util/nanovg_canvas.h`)
Hardware-accelerated GL canvas for custom drawing:
```cpp
class MyPanel : public NanoVGCanvas {
    void OnNanoVGPaint(NVGcontext* vg, int w, int h) override {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 10, 10, 100, 50, 4.0f);
        nvgFillColor(vg, nvgRGBA(80, 80, 80, 255));
        nvgFill(vg);
    }
    wxSize DoGetBestClientSize() const override { return wxSize(200, 100); }
};
```

Helper methods: `GetOrCreateItemImage()`, `GetOrCreateSpriteTexture()`, `UpdateScrollbar()`, `MakeContextCurrent()`

### DCButton (`source/ui/dcbutton.h`)
Custom button with sprite rendering (NanoVGCanvas-based).

### ItemFieldControl
Custom control with drag-drop support for item ID input + sprite preview.

## Map Canvas (`source/rendering/ui/map_display.h`)

```cpp
class MapCanvas : public wxGLCanvas {
    Editor& editor;
    std::unique_ptr<MapDrawer> drawer;
    std::unique_ptr<SelectionController> selection_controller;
    std::unique_ptr<DrawingController> drawing_controller;
    int floor; double zoom;

    void ScreenToMap(int sx, int sy, int* mx, int* my);
    Position GetCursorPosition() const;
    void EnterSelectionMode();
    void EnterDrawingMode();
};
```

## Panel Pattern

Side panels use `wxPanel` + AUI pane:
```cpp
class MyPanel : public wxPanel {
    MyPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
        wxBoxSizer* sizer = newd wxBoxSizer(wxVERTICAL);
        // ... add controls ...
        SetSizer(sizer);
    }
};

// Register in AUI:
g_gui.aui_manager->AddPane(panel, wxAuiPaneInfo()
    .Name("MyPanel").Caption("My Panel")
    .Right().Layer(1).CloseButton(true).Hide());
```

## GUI IDs (`source/ui/gui_ids.h`)

```cpp
enum EditorActionID {
    MAIN_FRAME_MENU = wxID_HIGHEST + 1,
    MAP_WINDOW_HSCROLL = MAIN_FRAME_MENU + 1000,
    MAP_POPUP_MENU_CUT, MAP_POPUP_MENU_COPY, MAP_POPUP_MENU_PASTE,
    PALETTE_ITEM_CHOICEBOOK, PALETTE_HOUSE_LISTBOX, PALETTE_DOODAD_SLIDER,
    // ... many more ...
};

enum ToolBarID { TOOLBAR_STANDARD, TOOLBAR_BRUSHES, TOOLBAR_POSITION,
                 TOOLBAR_SIZES, TOOLBAR_LIGHT };
```

## Drag & Drop Pattern

```cpp
class MyDropTarget : public wxTextDropTarget {
    bool OnDropText(wxCoord x, wxCoord y, const wxString& data) override {
        // Parse data, update control
        return true;
    }
};
// Usage: panel->SetDropTarget(new MyDropTarget(this));
```
