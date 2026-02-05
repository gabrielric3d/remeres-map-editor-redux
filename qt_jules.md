# Qt Migration Status - Phase 1

**Agent:** Jules
**Date:** 2023-10-27
**Target Framework:** Qt 6.7.3

## Summary of Completed Work

This session focused on the foundational "Phase 1" of migrating Remere's Map Editor from wxWidgets to Qt6. The goal was to establish a compiling build system and a running application shell.

### 1. Build System Refactoring
*   **Conan (`conanfile.py`):** Removed `wxwidgets` dependency. Added `qt/6.7.3` (Core, Gui, Widgets, OpenGL, OpenGLWidgets) and `xkbcommon` (Linux override).
*   **CMake (`CMakeLists.txt`):** Updated to link against Qt6 modules instead of wxWidgets.
*   **Source List (`source/CMakeLists.txt`):** Commented out ~90% of UI-related source files (dialogs, specific windows, managers) to isolate the core application shell for the initial port.

### 2. Core Application Port
*   **Application (`source/app/application.*`):**
    *   Replaced `wxApp` with `QApplication`.
    *   Implemented standard `main()` entry point.
    *   Stubbed out initialization logic that depended heavily on legacy UI components.
*   **Main Frame (`source/ui/main_frame.*`):**
    *   Replaced `wxFrame` with `QMainWindow`.
    *   Created a skeletal window implementation with a placeholder central widget (`QLabel`).
    *   Removed `wxAuiManager`, menus, and toolbars for this phase.

### 3. Rendering Engine
*   **NanoVGCanvas (`source/util/nanovg_canvas.*`):**
    *   Replaced `wxGLCanvas` with `QOpenGLWidget`.
    *   Ported event handling to `wheelEvent`, `paintGL`, `resizeGL`.
    *   *Note:* The NanoVG-GL3 backend integration needs verification when the map view is fully re-enabled.

### 4. Compatibility Layer (`app/main.h`)
To facilitate iterative porting without rewriting every single line of logic immediately, we introduced compatibility shims:
*   **String:** `wxString` aliased to `QString`. `wxT()`, `_()` macros stubbed.
*   **Files:** `FileName` (formerly `wxFileName`) implemented as a wrapper around `std::filesystem::path` with Qt compatibility helpers.
*   **Colors:** `wxColor` aliased to `QColor`.

### 5. GUI God Object Refactoring (`source/ui/gui.*`)
*   The global `GUI` class was heavily refactored.
*   Dependencies on unported components (PaletteWindow, MapTab, etc.) were stubbed or commented out.
*   This allows the "business logic" (Editor, Map, Actions) to compile without failing on missing UI calls.

---

## Technical Debt & Known Issues

1.  **Compilation Timeouts:** The Conan installation for Qt 6.7.3 times out in the current CI/Agent environment because it attempts to build Qt from source. A pre-compiled binary cache or a more powerful build environment is recommended.
2.  **Stubbed Managers:** `BrushManager`, `PaletteManager`, and `MinimapManager` interactions are currently no-ops.
3.  **Missing Features:**
    *   No Map View (Canvas is not yet instantiated in MainFrame).
    *   No Menus or Toolbars.
    *   No Dialogs (Welcome, Preferences, etc.).

---

## Next Steps (Phase 2)

The next session should focus on re-enabling the Map Editor's core functionality:

1.  **Restore MapCanvas:**
    *   Instantiate `NanoVGCanvas` inside `MainFrame`.
    *   Port `MapDrawer` interactions to work with the new `QOpenGLWidget` context.
    *   Verify `glad` / `QOpenGLFunctions` interoperability.
2.  **Input Handling:**
    *   Port mouse and keyboard event handling from `wxMouseEvent`/`wxKeyEvent` to `QEvent`.
    *   Reconnect the `Tool` and `Brush` logic to these events.
3.  **Basic UI Layout:**
    *   Implement a basic `QDockWidget` layout to replace `wxAuiManager`.
    *   Port the sidebars (MiniMap, Palette placeholder).

## Phase 3 (Future)

*   Porting individual Dialogs (`OutfitChooser`, `PropertiesWindow`, etc.).
*   Re-implementing the Menu Bar and Toolbars using `QAction`.
*   Restoring "Live" (Network) functionality if UI-dependent.
