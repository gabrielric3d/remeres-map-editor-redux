# Lua Scripting Integration — Autonomous Agent Prompt

## Your Mission

You are integrating the **Lua scripting subsystem** (located in `source/lua/`) into the **remeres-map-editor-redux** codebase. The `source/lua/` folder was ported from a legacy fork that is **~1000 commits behind** our current codebase. The legacy fork used wxWidgets 2.9, OpenGL 1.x (immediate mode), GLUT, Boost, and a flat source directory. Our codebase is a **major modernization** of that same editor.

**You MUST work recursively and autonomously in a continuous loop until:**
1. Every lua file compiles without errors
2. Every feature from the lua folder is fully integrated (no stubs, no placeholders, no `// TODO`)
3. The GitHub Action `build-linux` passes (workflow file: `.github/workflows/build.yml`, job: `build-linux`)
4. All integration points from the patches are implemented against our new codebase

**After every significant change, push your branch and trigger the `build-linux` GitHub Action to verify compilation. Fix ALL errors before moving on. Repeat until green.**

---

## Phase 1: Index the Current Codebase

### Project Architecture

This is a C++ desktop application (OpenTibia map editor) using:
- **C++20/23** (`CMAKE_CXX_STANDARD 23` in CMakeLists.txt)
- **CMake 3.28+** with vcpkg for dependency management
- **wxWidgets 3.2+** (with `WXWIN_COMPATIBILITY_3_3` defined — effectively wxWidgets 3.3.x APIs)
- **OpenGL 4.5** via GLAD (no legacy GL, no GLUT, no immediate mode)
- **NanoVG** for all text rendering, tooltips, overlays, and in-game UI (`NANOVG_GL3`)
- **GLM** for math
- **spdlog** for logging
- **nlohmann-json** for JSON
- **toml++** for config parsing
- **fmt** for string formatting
- **Boost** (asio, thread, system)
- **libarchive** for archive handling
- **ZLIB** for compression

### vcpkg Dependencies (vcpkg.json)
```json
{
  "dependencies": [
    "wxwidgets", "glad", "glm", "asio", "nlohmann-json", "fmt",
    {"name": "libiconv", "platform": "osx"},
    "libarchive", "boost-asio", "boost-thread", "nanovg", "spdlog", "tomlplusplus"
  ]
}
```

> **CRITICAL**: Lua and sol2 are NOT yet in `vcpkg.json`. You must add them: `"lua"` and `"sol2"`.

### Source Directory Structure (`source/`)

The codebase is **modular with subdirectories** (NOT flat like the legacy fork):

```
source/
├── app/                    # Application lifecycle, settings, preferences, versioning
│   ├── application.cpp/h   # wxApp subclass — app init, shutdown, MainFrame creation
│   ├── definitions.h       # Global defines, constants, version info
│   ├── settings.cpp/h      # Settings system
│   ├── preferences.cpp/h   # Preferences dialog
│   ├── preferences/        # Preference pages (general, editor, graphics, interface, client_version)
│   ├── managers/            # version_manager
│   └── client_version.cpp/h
├── editor/                 # Editor logic
│   ├── action.cpp/h        # Undo/redo action system
│   ├── action_queue.cpp/h  # Action queue with batching
│   ├── editor.cpp/h        # Editor state management
│   ├── selection.cpp/h     # Selection handling
│   ├── operations/         # copy_operations, draw_operations, selection_operations
│   ├── persistence/        # editor_persistence, tileset_exporter
│   └── managers/           # editor_manager
├── rendering/              # Modern OpenGL 4.5 rendering engine
│   ├── core/               # GPU resources: shader_program, texture_array, texture_atlas,
│   │                       #   sprite_batch, multi_draw_indirect_renderer, atlas_manager,
│   │                       #   pixel_buffer_object, ring_buffer, sync_handle, etc.
│   ├── drawers/            # Render passes
│   │   ├── tiles/          # floor_drawer, shade_drawer, tile_renderer, tile_color_calculator
│   │   ├── entities/       # item_drawer, sprite_drawer, creature_drawer, creature_name_drawer
│   │   ├── overlays/       # grid_drawer, selection_drawer, marker_drawer, preview_drawer,
│   │   │                   #   brush_overlay_drawer, hook_indicator_drawer, door_indicator_drawer
│   │   ├── cursors/        # brush_cursor_drawer, drag_shadow_drawer, live_cursor_drawer
│   │   └── minimap_drawer, minimap_renderer
│   ├── ui/                 # map_display (wxGLCanvas), tooltip_drawer, navigation_controller,
│   │                       #   drawing_controller, selection_controller, zoom_controller, etc.
│   ├── io/                 # editor_sprite_loader, screen_capture, screenshot_saver
│   ├── utilities/          # fps_counter, frame_pacer, light_calculator, light_drawer,
│   │                       #   sprite_icon_generator, tile_describer, icon_renderer
│   ├── postprocess/        # post_process_manager + effects (screen, scanline, xbrz)
│   ├── shaders/            # GLSL shader files
│   └── map_drawer.cpp/h    # Top-level draw orchestrator
├── ui/                     # wxWidgets UI windows and dialogs
│   ├── gui.cpp/h           # Central GUI manager (g_gui singleton)
│   ├── gui_ids.h           # Menu/control IDs
│   ├── main_frame.cpp/h    # MainFrame (wxFrame)
│   ├── main_menubar.cpp/h  # Menu bar system
│   ├── menubar/            # Decomposed menu handlers (file, search, view, map, navigation, palette)
│   ├── main_toolbar.cpp/h
│   ├── toolbar/            # Modular toolbars (brush, position, size, standard, light)
│   ├── map_window.cpp/h    # Map canvas wrapper
│   ├── map_tab.cpp/h       # Tab management
│   ├── dialogs/            # goto_position, find_dialog, outfit_chooser, etc.
│   ├── properties/         # Item/creature/spawn property editors
│   ├── tile_properties/    # Tile property panels
│   ├── managers/           # loading_manager, layout_manager, minimap_manager, etc.
│   ├── welcome/            # Welcome dialog components
│   ├── replace_tool/       # Visual rule-based item replacement system
│   ├── controls/           # Reusable controls (outfit_color_palette, sortable_list_box, modern_button)
│   └── map/                # map_properties_window, towns_window, import/export
├── game/                   # Game data model
│   ├── item.cpp/h          # Item class
│   ├── creature.cpp/h      # Creature class
│   ├── tile.cpp/h → (in map/) # Tile is in map/tile.cpp/h
│   ├── house.cpp/h, town.cpp/h, spawn.cpp/h, waypoints.cpp/h
│   ├── materials.cpp/h     # Material/tileset loading
│   └── creatures.cpp/h     # Creature database
├── map/                    # Map data structures
│   ├── map.cpp/h           # Map class
│   ├── tile.cpp/h          # Tile class
│   ├── position.h          # Position struct
│   ├── basemap.cpp/h       # Base map with quadtree
│   ├── tileset.cpp/h       # Tileset definitions
│   └── operations/         # map_processor
├── brushes/                # Brush system (ground, wall, table, carpet, doodad, creature, etc.)
├── palette/                # Palette windows and panels
├── io/                     # File I/O (OTBM loading/saving)
├── live/                   # Live collaboration (client/server)
├── net/                    # Network connections
├── item_definitions/       # Item definition loading (DAT, OTB, XML formats)
├── ingame_preview/         # In-game preview renderer
├── util/                   # Utilities (nanovg_canvas, nanovg_listbox, image_manager, common, json)
├── ext/                    # Third-party (pugixml)
├── lua/                    # ← THE CODE YOU ARE INTEGRATING
│   ├── patches/            # Reference patches showing legacy integration
│   │   ├── first/          # 24 patch files (initial lua integration)
│   │   └── second/         # 6 patch files (sprite overlay additions)
│   ├── lua_engine.cpp/h          # LuaEngine class (sol::state wrapper)
│   ├── lua_api.cpp/h             # API registration hub (LuaAPI::registerAll)
│   ├── lua_api_app.cpp/h         # App-level bindings (wx dialogs, file ops, GUI interaction)
│   ├── lua_api_tile.cpp/h        # Tile manipulation
│   ├── lua_api_item.cpp/h        # Item manipulation
│   ├── lua_api_map.cpp/h         # Map access
│   ├── lua_api_selection.cpp/h   # Selection manipulation
│   ├── lua_api_brush.cpp/h       # Brush access
│   ├── lua_api_creature.cpp/h    # Creature access
│   ├── lua_api_color.cpp/h       # Color type bindings
│   ├── lua_api_position.cpp/h    # Position type bindings
│   ├── lua_api_image.cpp/h       # Image loading/manipulation for lua
│   ├── lua_api_json.cpp/h        # JSON parsing for lua
│   ├── lua_api_http.cpp/h        # HTTP requests for lua
│   ├── lua_api_noise.cpp/h       # Perlin/simplex noise generation
│   ├── lua_api_algo.cpp/h        # Procedural generation algorithms
│   ├── lua_api_geo.cpp/h         # Geometric operations
│   ├── lua_dialog.cpp/h          # Full dialog creation system (Aseprite-style API)
│   ├── lua_script.cpp/h          # Script metadata and lifecycle
│   ├── lua_script_manager.cpp/h  # Global script manager (g_luaScripts singleton)
│   ├── lua_scripts_window.cpp/h  # Dockable Script Manager panel
│   ├── remove_lua_patches.py     # Utility to reverse patches
│   └── split_patches.py          # Utility to split patch files
└── CMakeLists.txt              # Source file listing (lua files NOT yet added)
```

### Build System (`CMakeLists.txt`)

Top-level `CMakeLists.txt`:
- Uses `find_package()` for all deps
- Defines `WXWIN_COMPATIBILITY_3_3` and `NANOVG_GL3`
- Links: wxWidgets, Boost, ZLIB, OpenGL, glad, glm, nanovg, spdlog, tomlplusplus

`source/CMakeLists.txt`:
- Lists ALL `.h` files in `rme_H` and ALL `.cpp` files in `rme_SRC`
- **Lua files are NOT currently listed** — you must add them

### Key Singletons/Globals
- `g_gui` — `GUI` class instance (`ui/gui.h`) — central UI manager
- `g_materials` — Material database
- `g_creatures` — Creature database  
- `g_settings` — Settings
- `g_luaScripts` — `LuaScriptManager::getInstance()` (the lua manager you're integrating)

---

## Phase 2: Index the Lua Folder

The `source/lua/` folder contains a **complete Lua scripting subsystem** using **sol2** (C++ binding library for Lua/LuaJIT). Here's what each file does:

### Core Engine
| File | Purpose |
|------|---------|
| `lua_engine.cpp/h` | Wraps `sol::state`, initializes Lua VM, sandboxing, base libraries, safe call wrappers |
| `lua_script.cpp/h` | `LuaScript` class — represents one script file with metadata (name, description, shortcut, auto-execute flag, enabled state) |
| `lua_script_manager.cpp/h` | `LuaScriptManager` singleton — discovers scripts in `scripts/` folder, manages lifecycle, event system (`emit`/`addEventListener`), context menu items, map overlay system |
| `lua_scripts_window.cpp/h` | `LuaScriptsWindow` — dockable wxPanel for managing scripts (list, enable/disable, execute, console output) |

### API Modules (each registers bindings via `LuaAPI::registerXxx(sol::state&)`)
| File | Bindings |
|------|----------|
| `lua_api_app` | `app.alert()`, `app.confirm()`, `app.prompt()`, `app.openFile()`, `app.saveFile()`, `app.getEditor()`, etc. |
| `lua_api_tile` | Tile read/write, item iteration, ground/border/wall access |
| `lua_api_item` | Item creation, property access, attribute manipulation |
| `lua_api_map` | Map iteration, tile access by position, map properties |
| `lua_api_selection` | Selection enumeration, modification |
| `lua_api_brush` | Brush lookup, application |
| `lua_api_creature` | Creature creation, property access |
| `lua_api_color` | `wxColor` bindings for Lua |
| `lua_api_position` | `Position` struct bindings |
| `lua_api_image` | Image loading, pixel manipulation, wxBitmap integration |
| `lua_api_json` | JSON parse/stringify via sol2 tables |
| `lua_api_http` | HTTP GET/POST for Lua scripts |
| `lua_api_noise` | Perlin/Simplex noise generators |
| `lua_api_algo` | Flood fill, pathfinding, procedural terrain |
| `lua_api_geo` | Geometric operations (circles, rectangles, polygons) |
| `lua_dialog` | Full dialog builder: `Dialog("title"):label{...}:input{...}:button{...}:show()` with method chaining |

### Map Overlay System
The `LuaScriptManager` includes a **map overlay system** that lets Lua scripts draw on the map:
- `MapOverlay` — registered overlays with `ondraw`/`onhover` callbacks
- `MapOverlayCommand` — rect, line, text, sprite draw commands
- `MapOverlayHoverState` — tooltips and commands for hovered tiles
- `MapOverlayShowItem` — toggle items in the View→Show menu

---

## Phase 3: Understand the Patches

The `source/lua/patches/` directory contains diff files showing exactly how the legacy fork integrated Lua. These are your **roadmap**, but the target files are different in our codebase.

### Patch Set 1 (`first/` — 24 patches)

| Patch | Legacy File | What It Does | Our Equivalent |
|-------|------------|--------------|----------------|
| `001` | `source/live_client.cpp` | Adds lua header include guarding | `source/live/live_client.cpp` |
| `002` | `source/live_client.h` | Forward declarations | `source/live/live_client.h` |
| `003` | `source/net_connection.cpp` | Adds lua header | `source/net/net_connection.cpp` |
| `004` | `source/net_connection.h` | Forward declarations | `source/net/net_connection.h` |
| `005` | `vcpkg.json` | Adds `lua` and `sol2` dependencies | `vcpkg.json` |
| `006` | `CMakeLists.txt` | Adds `find_package(Lua)`, `find_path(sol2)`, link libraries | `CMakeLists.txt` (our setup is very different—use modern CMake targets) |
| `007` | `source/CMakeLists.txt` | Adds lua source files to build | `source/CMakeLists.txt` |
| `009` | `vcproj/...vcxproj` | Visual Studio project (may be irrelevant) | `vcproj/` if it exists |
| `010` | `source/definitions.h` | Version string changes, possible lua-related defines | `source/app/definitions.h` |
| `011` | `data/menubar.xml` | Adds Scripts menu to XML menubar | `data/menubar.xml` |
| `012` | `source/application.cpp` | **KEY**: Initializes `g_luaScripts` on startup, creates `LuaScriptsWindow` panel, shuts down on exit | `source/app/application.cpp` |
| `013` | `source/gui_ids.h` | Adds menu IDs: `SCRIPTS_OPEN_FOLDER`, `SCRIPTS_RELOAD`, `SCRIPTS_MANAGER`, `SCRIPTS_FIRST..SCRIPTS_LAST`, `SHOW_CUSTOM_FIRST..SHOW_CUSTOM_LAST` | `source/ui/gui_ids.h` |
| `014` | `source/main_menubar.cpp` | **KEY**: Scripts menu loading, event handling, overlay toggle, dynamic script menu population | `source/ui/main_menubar.cpp` |
| `015` | `source/main_menubar.h` | Adds method declarations for script/overlay menu handling, member variables (`scriptsMenu`, `showMenu`, etc.) | `source/ui/main_menubar.h` |
| `016` | `source/action.cpp` | **KEY**: Emits `"actionChange"` event on undo/redo/batch | `source/editor/action.cpp` + `source/editor/action_queue.cpp` |
| `017` | `source/action.h` | Include for lua_script_manager | `source/editor/action.h` |
| `018` | `source/brush.h` | Virtual method or include for lua | `source/brushes/brush.h` |
| `019` | `source/editor.cpp` | Editor integration (possibly script hooks) | `source/editor/editor.cpp` |
| `020` | `source/editor.h` | Forward declarations / includes | `source/editor/editor.h` |
| `021` | `source/gui.cpp` | GUI integration (context menu lua items, reload notifications) | `source/ui/gui.cpp` |
| `022` | `source/map.cpp` | Map access helpers for Lua API | `source/map/map.cpp` |
| `023` | `source/selection.cpp` | Selection tracking for lua events | `source/editor/selection.cpp` |
| `024` | `source/map_drawer.cpp` | **KEY**: Draws lua overlay commands (rects, lines, text), handles hover, uses `glBegin`/`glEnd`, GLUT text, `glRasterPos`, immediate-mode GL | `source/rendering/map_drawer.cpp` |

### Patch Set 2 (`second/` — 6 patches)

| Patch | What It Does |
|-------|-------------|
| `001` | Adds `MapOverlayCommand::Type::Sprite` rendering (uses `BlitSpriteType` with immediate GL) |
| `002` | `map_overlay.h` — defines `MapViewInfo`, `MapOverlayCommand`, `MapOverlayTooltip`, `MapOverlayHoverState` structs |
| `003-005` | Adds sample lua scripts and documentation |
| `006` | Minor definitions.h version bump |

---

## Phase 4: Critical Mismatches Between Legacy and Our Codebase

### 🔴 RENDERING (MOST CRITICAL)

**Legacy (what the lua patches assume):**
- OpenGL 1.x immediate mode (`glBegin`/`glEnd`, `glVertex2f`, `glColor4ub`, `glRasterPos2i`)
- GLUT for text rendering (`glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, ...)`)
- `glEnable(GL_TEXTURE_2D)` / `glDisable(GL_TEXTURE_2D)` (fixed-function pipeline)
- `glMatrixMode(GL_PROJECTION)`, `glPushMatrix()`, `glOrtho()` (legacy matrix stack)
- `BlitSpriteType()` function for sprite drawing
- `MapDrawer::MakeTooltip()` for tooltip text
- `MapDrawer::drawFilledRect()`, `MapDrawer::drawRect()` for primitive drawing
- `glLineWidth()`, `glLineStipple()` for dashed lines
- Direct `glGetIntegerv(GL_VIEWPORT, vPort)` calls

**Our codebase:**
- **OpenGL 4.5** with shader programs, VAOs, VBOs, SSBOs
- `ShaderProgram` class for shader management
- `SpriteBatch` for batched sprite rendering
- `MultiDrawIndirectRenderer` for efficient draw calls
- `TextureArray` / `TextureAtlas` for texture management
- `PrimitiveRenderer` for drawing lines, rects, circles (`source/rendering/core/primitive_renderer.h`)
- `TextRenderer` for NanoVG-based text (`source/rendering/core/text_renderer.h`)
- `TooltipDrawer` for NanoVG tooltip rendering (`source/rendering/ui/tooltip_drawer.h`)
- `MapDrawer` is the top-level orchestrator but delegates to specialized drawers
- `CoordinateMapper` for world-to-screen transforms (`source/rendering/core/coordinate_mapper.h`)
- `RenderView` for viewport/camera state (`source/rendering/core/render_view.h`)
- `NanoVG` context available via `Graphics` class (`source/rendering/core/graphics.h`)

**What you MUST do:** Rewrite all overlay rendering in `map_drawer.cpp` (patch `first_024`) to use:
- `PrimitiveRenderer` for rects, lines, circles
- `TextRenderer` or `TooltipDrawer` for text/tooltips
- `SpriteBatch` or `SpriteDrawer` for sprite overlays
- `CoordinateMapper` for coordinate transforms (replaces the `mapToScreen` helper)
- Remove ALL `glBegin`/`glEnd`, `glVertex`, `glColor`, `glRasterPos`, `glMatrixMode`, `glutBitmapCharacter` calls

### 🔴 wxWidgets Version

**Legacy:** wxWidgets 2.9.x
**Ours:** wxWidgets 3.3.x (with `WXWIN_COMPATIBILITY_3_3`)

Key differences you'll encounter in `lua_dialog.cpp` (91KB file!) and other UI code:
- `newd` macro → use `new` (our codebase may define `newd` as `new` in debug)
- Event binding: legacy uses `Connect()` / `BEGIN_EVENT_TABLE` — our code uses both, but prefer `Bind()`
- `wxAuiManager` API changes
- `wxPropGrid` is now available (added to `find_package(wxWidgets ...)`)
- Some deprecated methods may need updating

### 🔴 Include Paths

**Legacy:** All sources in flat `source/` directory → `#include "map.h"`, `#include "editor.h"`
**Ours:** Modular subdirectories → `#include "map/map.h"`, `#include "editor/editor.h"`, `#include "ui/gui.h"`

You MUST update ALL includes in the lua files. The `lua_script_manager.h` already has one such include:
```cpp
#include "../map_overlay.h"  // This file doesn't exist in our codebase!
```

### 🔴 map_overlay.h Missing

The file `map_overlay.h` (or `source/map_overlay.h`) referenced by `lua_script_manager.h` is defined in patch `second_002`. It contains:
- `struct MapViewInfo` — view bounds, zoom, scroll, tile size, screen dimensions
- `struct MapOverlayCommand` — drawing commands (Rect, Line, Text, Sprite) with properties
- `struct MapOverlayTooltip` — tooltip data
- `struct MapOverlayHoverState` — hover state with tooltips and commands

**You must create this file** in an appropriate location (e.g., `source/rendering/core/map_overlay.h` or `source/lua/map_overlay.h`) and update include paths.

### 🟡 MainMenuBar Decomposition

**Legacy:** Single `main_menubar.cpp` with all methods
**Ours:** Decomposed into `source/ui/menubar/` with separate handlers:
- `menubar_action_manager.cpp` — action registry
- `file_menu_handler.cpp`
- `search_handler.cpp`
- `view_settings_handler.cpp`
- `map_actions_handler.cpp`
- `navigation_menu_handler.cpp`
- `palette_menu_handler.cpp`

The script menu handlers should go into a new file like `source/ui/menubar/script_menu_handler.cpp/h` or be added to the existing `main_menubar.cpp`.

### 🟡 Action System

**Legacy:** `action.cpp` has `ActionQueue::addBatch()`, `undo()`, `redo()` in one file
**Ours:** Split across `editor/action.cpp` and `editor/action_queue.cpp`

The `g_luaScripts.emit("actionChange")` calls must go into `action_queue.cpp`.

### 🟡 Editor/Selection

**Legacy:** `editor.cpp`, `selection.cpp` in flat `source/`
**Ours:** `editor/editor.cpp`, `editor/selection.cpp` — same logic, different paths

### 🟢 GUI System

`g_gui` still exists as the main singleton in `source/ui/gui.cpp/h`. It still has `aui_manager`, `root` (MainFrame), etc. The interfaces are similar but method signatures may have changed.

---

## Phase 5: Integration Steps

### Step 1: Add Dependencies
1. Add `"lua"` and `"sol2"` to `vcpkg.json`
2. Add `find_package(Lua REQUIRED)` and `find_package(sol2 CONFIG REQUIRED)` to top-level `CMakeLists.txt`
3. Link: `target_link_libraries(rme PRIVATE ${LUA_LIBRARIES} sol2::sol2)` (or equivalent modern CMake targets)
4. Add lua include dirs: `target_include_directories(rme PRIVATE ${LUA_INCLUDE_DIR})`

### Step 2: Create map_overlay.h
Create `source/lua/map_overlay.h` (or equivalent) with the structs from patch `second_002_source_map_overlay.h.patch`. Adapt the structs to use our types (e.g., use `DrawColor` instead of `wxColor` for overlay command colors where appropriate, but keep `wxColor` where it's already used by the lua code).

### Step 3: Fix All Include Paths in lua/ Files
Every `#include` in the 44 lua files will reference legacy flat paths. Map them:
```
"main.h"          → "app/main.h"
"editor.h"        → "editor/editor.h"
"map.h"           → "map/map.h"
"tile.h"          → "map/tile.h"
"item.h"          → "game/item.h"
"creature.h"      → "game/creature.h"
"creatures.h"     → "game/creatures.h"
"brush.h"         → "brushes/brush.h"
"gui.h"           → "ui/gui.h"
"materials.h"     → "game/materials.h"
"selection.h"     → "editor/selection.h"
"action.h"        → "editor/action.h"
"map_drawer.h"    → "rendering/map_drawer.h"
"settings.h"      → "app/settings.h"
"position.h"      → "map/position.h"
"house.h"         → "game/house.h"
"town.h"          → "game/town.h"
"spawn.h"         → "game/spawn.h"
"waypoints.h"     → "game/waypoints.h"
"complexitem.h"   → "game/complexitem.h"
"definitions.h"   → "app/definitions.h"
"live_client.h"   → "live/live_client.h"
"net_connection.h" → "net/net_connection.h"
```

Also fix the relative include in `lua_script_manager.h`:
```cpp
#include "../map_overlay.h"  →  #include "map_overlay.h"  (if in same directory)
```

### Step 4: Add Lua Files to source/CMakeLists.txt
Add all `.h` files to `rme_H` and all `.cpp` files to `rme_SRC`:
```cmake
# In rme_H section:
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_algo.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_app.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_brush.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_color.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_creature.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_geo.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_http.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_image.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_item.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_json.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_map.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_noise.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_position.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_selection.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_tile.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_dialog.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_engine.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_script.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_script_manager.h
${CMAKE_CURRENT_LIST_DIR}/lua/lua_scripts_window.h
${CMAKE_CURRENT_LIST_DIR}/lua/map_overlay.h

# In rme_SRC section:
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_algo.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_app.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_brush.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_color.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_creature.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_geo.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_http.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_image.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_item.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_json.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_map.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_noise.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_position.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_selection.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_api_tile.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_dialog.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_engine.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_script.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_script_manager.cpp
${CMAKE_CURRENT_LIST_DIR}/lua/lua_scripts_window.cpp
```

### Step 5: Integrate into Application Lifecycle
Modify `source/app/application.cpp`:
1. Include `lua/lua_script_manager.h` and `lua/lua_scripts_window.h`
2. In `OnInit()` (after GUI is loaded): Initialize `g_luaScripts`, load scripts menu
3. In `OnExit()`: Call `g_luaScripts.shutdown()`
4. In `MainFrame` constructor: Create `LuaScriptsWindow` panel, add to AUI manager

Use the pattern from patch `first_012` but adapt to our MainFrame/GUI structure.

### Step 6: Integrate Menu System
1. Add menu IDs to `source/ui/gui_ids.h` (from patch `first_013`)
2. Update `data/menubar.xml` to add Scripts menu (from patch `first_011`)
3. Add script menu handlers — create `source/ui/menubar/script_menu_handler.cpp/h` OR add to `main_menubar.cpp`
4. Implement: `LoadScriptsMenu()`, `RefreshScriptsMenu()`, `LoadShowMenu()`, `OnScriptExecute()`, etc.
5. The `MAIN_FRAME_MENU` offset pattern must match our existing menubar system

### Step 7: Integrate Action Events
Modify `source/editor/action_queue.cpp`:
1. Include `lua/lua_script_manager.h`
2. Add `g_luaScripts.emit("actionChange")` after batch add, undo, and redo

### Step 8: Integrate Map Overlay Rendering (HARDEST PART)
This is where the biggest rewrite is needed.

**DO NOT use any legacy OpenGL calls.** Instead:

1. Create a new drawer class like `source/rendering/drawers/overlays/lua_overlay_drawer.cpp/h`
2. Use `PrimitiveRenderer` for rectangles, lines
3. Use `TextRenderer` (NanoVG) for text labels
4. Use `TooltipDrawer` for tooltips
5. Use `CoordinateMapper` for world→screen coordinate transforms
6. Use `SpriteBatch` or `SpriteDrawer` for sprite overlay commands
7. Integrate into `MapDrawer::Draw()` pipeline at the appropriate point (after main rendering, before tooltips)

Study these files to understand how drawing works in our codebase:
- `source/rendering/core/primitive_renderer.h/cpp`
- `source/rendering/core/text_renderer.h/cpp`
- `source/rendering/ui/tooltip_drawer.h/cpp`
- `source/rendering/drawers/overlays/grid_drawer.h/cpp` (good example of an overlay drawer)
- `source/rendering/drawers/overlays/selection_drawer.h/cpp`
- `source/rendering/drawers/overlays/marker_drawer.h/cpp`
- `source/rendering/core/coordinate_mapper.h/cpp`
- `source/rendering/map_drawer.cpp` (the orchestrator)

### Step 9: Integrate Context Menu
Modify `source/ui/map_popup_menu.cpp` or `source/rendering/ui/popup_action_handler.cpp`:
- Add lua context menu items from `g_luaScripts.getContextMenuItems()`

### Step 10: Integrate Editor/Selection/GUI Hooks
Follow patches `019-023`:
- Add lua event emissions in selection changes
- Add lua integration in editor operations
- Add lua context items in GUI

### Step 11: Adapt lua_dialog.cpp for wxWidgets 3.3
The `lua_dialog.cpp` is 91KB and uses extensive wxWidgets APIs. Audit for:
- Deprecated wxWidgets 2.9 APIs
- `newd` macro usage (ensure it's defined or replace with `new`)
- `Connect()` vs `Bind()` event handling
- Any GLUT or immediate-mode GL usage in dialog rendering (unlikely but check)

### Step 12: Adapt lua_api_image.cpp
The image API likely uses wxImage/wxBitmap. Ensure compatibility with our rendering pipeline. The `LuaImage` class in `lua_api_image.h` may need to interface with our `TextureAtlas` or similar for previews.

---

## Phase 6: Autonomous Work Loop

**YOU MUST FOLLOW THIS LOOP:**

```
WHILE (integration not complete) DO:
    1. Make changes to a batch of files
    2. Push to branch
    3. Trigger GitHub Action "build-linux" (workflow_dispatch or push)
    4. Wait for result
    5. IF build fails:
        a. Read the error log
        b. Fix ALL errors
        c. GOTO 1
    6. IF build passes:
        a. Review: Are there any remaining TODO/placeholder/stub items?
        b. Are there any lua features not yet connected?
        c. Are there any patches not yet implemented?
        d. IF yes to any: implement them, GOTO 1
        e. IF no: integration is COMPLETE
END
```

### Verification Checklist (MUST ALL BE TRUE before you stop):

- [ ] `vcpkg.json` has `lua` and `sol2` dependencies
- [ ] `CMakeLists.txt` finds and links Lua and sol2
- [ ] `source/CMakeLists.txt` lists ALL lua `.h` and `.cpp` files
- [ ] `map_overlay.h` exists with proper structs
- [ ] ALL include paths in lua files are fixed for modular directory structure
- [ ] `application.cpp` initializes and shuts down lua engine
- [ ] `gui_ids.h` has script-related menu IDs
- [ ] `menubar.xml` has Scripts menu
- [ ] Script menu handlers are implemented and connected
- [ ] Action queue emits lua events
- [ ] Map overlay rendering uses modern OpenGL (NO `glBegin`/`glEnd`/`glVertex`/`glColor`/GLUT)
- [ ] Map overlay rendering uses `PrimitiveRenderer`, `TextRenderer`, `TooltipDrawer`
- [ ] Context menu lua integration works
- [ ] Selection/editor hooks emit lua events
- [ ] `lua_dialog.cpp` compiles with wxWidgets 3.3
- [ ] ALL 44 lua source files compile without errors
- [ ] `build-linux` GitHub Action passes ✅
- [ ] No `// TODO`, `// FIXME`, `// placeholder`, or stub implementations remain
- [ ] No `#include` references to legacy flat paths remain
- [ ] No GLUT dependency is introduced
- [ ] No `glBegin`, `glEnd`, `glVertex2f`, `glColor4ub`, `glRasterPos`, `glMatrixMode`, `glutBitmapCharacter` calls exist in any new or modified code

---

## Critical Constraints

1. **NEVER introduce GLUT as a dependency.** Our codebase does not use GLUT. All text must use NanoVG/TextRenderer.
2. **NEVER use OpenGL immediate mode** (`glBegin`/`glEnd`). Use our `PrimitiveRenderer`, `SpriteBatch`, and shader-based rendering.
3. **NEVER add `#include <GL/glut.h>`** anywhere.
4. **ALWAYS test with the `build-linux` GitHub Action** after changes.
5. **The lua folder's code IS authoritative for what features exist** — but ALL rendering, UI, and system integration code must be adapted to our modern codebase.
6. **Do not delete any existing code** unless specifically replacing legacy stubs.
7. **All new files must follow existing code style** — tabs for indentation, `#ifndef` guards matching pattern `RME_XXX_H`, same license header.
8. **The `g_luaScripts` macro and `LuaScriptManager` singleton pattern must be preserved** — that's how the lua system is accessed globally.
9. **C++20 features are available** — use `std::format`, `std::span`, `constexpr`, structured bindings, etc. where they improve the code.
10. **`newd` macro** — check if defined in our codebase; if not, define it or replace all occurrences with `new`.

---

## Quick Reference: Our Rendering API

```cpp
// PrimitiveRenderer (for rects, lines)
PrimitiveRenderer& primitives = graphics.getPrimitiveRenderer();
primitives.drawRect(x, y, w, h, color);
primitives.drawFilledRect(x, y, w, h, color);
primitives.drawLine(x1, y1, x2, y2, color, lineWidth);

// TextRenderer (NanoVG-based)
TextRenderer& text = graphics.getTextRenderer();
text.drawText(x, y, "Hello", fontSize, color);

// CoordinateMapper (world → screen)
CoordinateMapper& mapper = /* get from MapDrawer or RenderView */;
auto screenPos = mapper.mapToScreen(worldX, worldY, worldZ);

// SpriteBatch (for sprite rendering)
SpriteBatch& batch = graphics.getSpriteBatch();
batch.draw(textureId, x, y, w, h, ...);
```

> Note: The exact API may differ — **read the actual header files** to get the correct method signatures. The above is illustrative.

---

## Start working now. Begin with Phase 5, Step 1. Work through each step. Push and test after each major step. Loop until `build-linux` is green and all checklist items are satisfied. Do not stop until the integration is complete.
