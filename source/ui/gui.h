#ifndef RME_GUI_H_
#define RME_GUI_H_

#include "app/main.h"
#include "map/position.h"
#include "editor/copybuffer.h"
#include "brushes/brush_enums.h"
#include "ui/gui_ids.h"
#include "rendering/core/graphics.h"

// Forward declarations
class BaseMap;
class Map;
class Editor;
class Brush;
class MainFrame;
class LiveSocket;
class LiveClient;
class PaletteWindow;
class EditorTab;
class MapTab;
class ToolOptionsWindow;
class HousePalette;

// Stubbed Managers (included in build?)
// #include "brushes/managers/brush_manager.h"
// ... others

class GUI {
public:
    GUI();
    ~GUI();

    // Perspective
    void SavePerspective() {}
    void LoadPerspective() {}

    // Loading Bar
    void CreateLoadBar(QString message, bool canCancel = false) {}
    void SetLoadScale(int32_t from, int32_t to) {}
    bool SetLoadDone(int32_t done, const QString& newMessage = "") { return true; }
    void DestroyLoadBar() {}

    // Welcome Dialog
    void ShowWelcomeDialog(const QIcon& icon) {}
    void FinishWelcomeDialog() {}
    bool IsWelcomeDialogShown() { return false; }

    // Menus & Rendering
    void UpdateMenubar();
    bool IsRenderingEnabled() const { return disabled_counter == 0; }
    void AddPendingCanvasEvent(QEvent* event) {} // Changed signature

    void DisableRendering() { ++disabled_counter; }
    void EnableRendering() { --disabled_counter; }

    // Title & Status
    void SetTitle(QString newtitle) {}
    void UpdateTitle() {}
    void UpdateMenus() {}
    void ShowToolbar(int id, bool show) {}
    void SetStatusText(QString text) {}

    // GL Context (Stub)
    void* GetGLContext(void* win) { return nullptr; }

    // Search (Stub)
    void* ShowSearchWindow() { return nullptr; }
    void HideSearchWindow() {}

    // Minimap (Stub)
    void CreateMinimap() {}
    void HideMinimap() {}
    void DestroyMinimap() {}
    void UpdateMinimap(bool immediate = false) {}
    bool IsMinimapVisible() const { return false; }

    // Floor & Zoom
    int GetCurrentFloor() { return 7; }
    void ChangeFloor(int newfloor) {}
    double GetCurrentZoom() { return 1.0; }
    void SetCurrentZoom(double zoom) {}

    // Modes
    void SwitchMode() {}
    void SetSelectionMode() {}
    void SetDrawingMode() {}
    bool IsSelectionMode() const { return mode == SELECTION_MODE; }
    bool IsDrawingMode() const { return mode == DRAWING_MODE; }

    // Brushes (Stubs - calling BrushManager if available, else no-op)
    void FillDoodadPreviewBuffer() {}
    void UpdateAutoborderPreview(Position pos) {}
    void SelectBrush() {}
    bool SelectBrush(const Brush* brush, PaletteType pt = TILESET_UNKNOWN) { return false; }
    void SelectPreviousBrush() {}
    void SelectBrushInternal(Brush* brush) {}

    Brush* GetCurrentBrush() const { return nullptr; }
    BrushShape GetBrushShape() const { return BRUSHSHAPE_SQUARE; } // Default
    int GetBrushSize() const { return 1; }
    int GetBrushVariation() const { return 0; }
    int GetSpawnTime() const { return 60; }

    void SetSpawnTime(int time) {}
    void SetLightIntensity(float v) {}
    float GetLightIntensity() const { return 1.0f; }
    void SetAmbientLightLevel(float v) {}
    float GetAmbientLightLevel() const { return 1.0f; }
    void SetBrushSize(int nz) {}
    void SetBrushSizeInternal(int nz) {}
    void SetBrushShape(BrushShape bs) {}
    void SetBrushVariation(int nz) {}
    void SetBrushThickness(int low, int ceil) {}
    void SetBrushThickness(bool on, int low = -1, int ceil = -1) {}
    void DecreaseBrushSize(bool wrap = false) {}
    void IncreaseBrushSize(bool wrap = false) {}
    void SetDoorLocked(bool on) {}
    bool HasDoorLocked() { return false; }

    // View
    void SetScreenCenterPosition(Position pos) {}
    void RefreshView() {}
    void FitViewToMap() {}
    void FitViewToMap(MapTab* mt) {}

    // Copy/Paste
    void DoCut() {}
    void DoCopy() {}
    void DoPaste() {}
    void PreparePaste() {}
    void StartPasting() {}
    void EndPasting() {}
    bool IsPasting() const { return pasting; }

    // Undo/Redo
    bool CanUndo() { return false; }
    bool CanRedo() { return false; }
    bool DoUndo() { return false; }
    bool DoRedo() { return false; }

    // Editors
    // wxAuiManager* GetAuiManager() const { return nullptr; }
    EditorTab* GetCurrentTab() { return nullptr; }
    EditorTab* GetTab(int idx) { return nullptr; }
    int GetTabCount() const { return 0; }
    bool IsAnyEditorOpen() const { return false; }
    bool IsEditorOpen() const { return false; }
    void CloseCurrentEditor() {}
    Editor* GetCurrentEditor() { return nullptr; }
    MapTab* GetCurrentMapTab() const { return nullptr; }
    void CycleTab(bool forward = true) {}
    bool CloseLiveEditors(LiveSocket* sock) { return true; }
    bool CloseAllEditors() { return true; }
    void NewMapView() {}
    void AddPendingLiveClient(std::unique_ptr<LiveClient> client) {}
    std::unique_ptr<LiveClient> PopPendingLiveClient(LiveClient* ptr) { return nullptr; }

    // Map
    Map& GetCurrentMap(); // Can't stub easily if it returns reference
    int GetOpenMapCount() { return 0; }
    bool ShouldSave() { return false; }
    void SaveCurrentMap(FileName filename, bool showdialog) {}
    void SaveCurrentMap(bool showdialog = true) {}
    bool NewMap() { return false; }
    void OpenMap() {}
    void SaveMap() {}
    void SaveMapAs() {}
    bool LoadMap(const FileName& fileName) { return false; }
    void RefreshPalettes(Map* m = nullptr, bool usedfault = true) {}

    // Palettes (Stubs)
    PaletteWindow* NewPalette() { return nullptr; }
    void ActivatePalette(PaletteWindow* p) {}
    void RebuildPalettes() {}
    void RefreshOtherPalettes(PaletteWindow* p) {}
    void ShowPalette() {}
    void SelectPalettePage(PaletteType pt) {}
    PaletteWindow* GetPalette() { return nullptr; }
    // const std::list<PaletteWindow*>& GetPalettes(); // Need to return ref to list

    void DestroyPalettes() {}
    PaletteWindow* CreatePalette() { return nullptr; }

public:
    MainFrame* root;
    CopyBuffer copybuffer;
    GraphicManager gfx;
    BaseMap* secondary_map;
    HousePalette* house_palette;
    ToolOptionsWindow* tool_options;
    EditorMode mode;
    bool pasting;
    // Hotkey hotkeys[10];
    bool hotkeys_enabled;

protected:
    int disabled_counter;
    // std::mutex pending_live_clients_mutex;
    // std::vector<std::unique_ptr<LiveClient>> pending_live_clients;

    friend class RenderingLock;
};

extern GUI g_gui;

class RenderingLock {
    bool acquired;
public:
    RenderingLock() : acquired(true) { g_gui.DisableRendering(); }
    ~RenderingLock() { release(); }
    void release() { g_gui.EnableRendering(); acquired = false; }
};

class ScopedLoadingBar {
public:
    ScopedLoadingBar(QString message, bool canCancel = false) {}
    ~ScopedLoadingBar() {}
    void SetLoadDone(int32_t done, const QString& newmessage = "") {}
    void SetLoadScale(int32_t from, int32_t to) {}
};

#define UnnamedRenderingLock() RenderingLock __unnamed_rendering_lock_##__LINE__

void SetWindowToolTip(QWidget* a, const QString& tip);

#endif
