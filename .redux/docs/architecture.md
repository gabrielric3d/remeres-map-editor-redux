# RME Redux - Architecture Overview

## Application Lifecycle

- **Entry**: `Application : wxApp` in `source/app/application.h`
  - `OnInit()` → `OnRun()` → `OnExit()`
  - Single instance enforcement via `wxSingleInstanceChecker`
- **Main Frame**: `MainFrame : wxFrame` in `source/ui/main_frame.h`
  - Contains MenuBar, ToolBar, wxAuiManager for layout

## Global Singletons

| Global | Class | File | Purpose |
|--------|-------|------|---------|
| `g_gui` | `GUI` | `source/ui/gui.h` | Central UI hub, holds AUI manager, tabbook, palette, copybuffer, gfx |
| `g_editors` | `EditorManager` | `source/editor/managers/editor_manager.h` | Manages multiple open maps/editors |
| `g_settings` | `Settings` | `source/app/settings.h` | TOML config, 228+ config keys via `Config::Key` enum |
| `g_brushes` | `Brushes` | `source/brushes/brush.h` | Global brush registry (multimap<string, unique_ptr<Brush>>) |
| `g_brush_manager` | `BrushManager` | `source/brushes/managers/brush_manager.h` | Current/previous brush, special brush refs |

## Core Classes

### Editor (`source/editor/editor.h`)
Main editing interface for a map. Members:
- `map` — the Map being edited
- `actionQueue` — `unique_ptr<ActionQueue>` for undo/redo
- `selection` — Selection object
- `copybuffer` — reference to global copy buffer
- Key methods: `draw()`, `undraw()`, `addBatch()`, `addAction()`, `moveSelection()`, `borderizeSelection()`

### Map (`source/map/map.h`, inherits `BaseMap`)
- `BaseMap` uses `SpatialHashGrid` for tile storage
- Members: `filename`, `width`, `height`, `mapVersion`, `Towns`, `Houses`, `Spawns`, `waypoints`
- Tile access: `getTile(pos)`, `createTile(pos)`, `getOrCreateTile(pos)`
- Iteration: `tiles()` method, `forEach()`, or `begin()`/`end()`

### Tile (`source/map/tile.h`)
- `ground` — `unique_ptr<Item>` (ground item)
- `items` — `vector<unique_ptr<Item>>` (items on tile)
- `creature` — `unique_ptr<Creature>`
- `spawn` — `unique_ptr<Spawn>`
- `house_id`, `mapflags`, `statflags`, `minimapColor`
- `location` — `TileLocation&` (position reference)

### Position (`source/map/position.h`)
- `int x, y, z`
- `PositionVector = std::vector<Position>`

## Source Module Map

```
source/
├── app/              # Application lifecycle, Settings, Preferences
├── brushes/          # All brush types + managers + loaders
├── editor/           # Editor, Action system, Selection, CopyBuffer
│   ├── managers/     # EditorManager
│   ├── operations/   # CopyOperations, DrawOperations, SelectionOperations
│   └── persistence/  # Map I/O coordination
├── game/             # Item, Creature, Spawn, House, Town, Waypoints, CameraPaths
├── io/               # IOMapOTBM, MapXMLIO, OTBM serialization modules
├── item_definitions/ # DAT/OTB/XML item definition loaders
├── live/             # LiveManager, LiveClient, LiveServer, NetworkedAction
├── map/              # Map, BaseMap, Tile, Position, TileOperations, SpatialHashGrid
├── net/              # NetConnection, ProcessCom
├── palette/          # PaletteWindow, PaletteManager, BrushPalettePanel, VirtualBrushGrid
├── rendering/        # MapDrawer, MapCanvas, GraphicManager, 20+ specialized drawers
│   ├── core/         # Graphics, Sprites, Shaders, Lighting
│   ├── drawers/      # Floor, Selection, Creature, Item, Grid drawers etc.
│   ├── ui/           # MapDisplay (wxGLCanvas), Controllers
│   └── postprocess/  # Post-processing effects
├── ui/               # GUI, MainFrame, Dialogs, Managers, Controls
│   ├── dialogs/      # Dialog windows
│   ├── managers/     # Layout, Loading, Minimap, Search, Status managers
│   └── controls/     # Custom wxWidgets controls
└── util/             # Common utilities, NanoVGCanvas, ImageManager, JSON
```

## Key Relationships

```
Application → MainFrame → wxAuiManager
                           ├── GUI (g_gui)
                           │   ├── EditorManager (g_editors)
                           │   │   └── Editor*
                           │   │       ├── Map (BaseMap + SpatialHashGrid)
                           │   │       ├── ActionQueue (undo/redo)
                           │   │       └── Selection
                           │   ├── MapTabbook → MapTab → MapWindow → MapCanvas
                           │   │                                      ├── MapDrawer (all drawers)
                           │   │                                      ├── SelectionController
                           │   │                                      └── DrawingController
                           │   ├── GraphicManager (sprites, textures)
                           │   ├── PaletteWindow* (brush palettes)
                           │   └── CopyBuffer
                           └── Settings (g_settings)
```

## Data Flow: User Draws on Map

1. User clicks/drags on `MapCanvas`
2. `DrawingController` queries `g_brush_manager->GetCurrentBrush()`
3. Controller calls `Editor::draw(position)`
4. `DrawOperations` uses brush to modify tiles
5. Changes wrapped in `Action → BatchAction → ActionQueue`
6. `Editor::notifyStateChange()` triggers UI refresh
7. `MapDrawer` redraws affected area

## Build System (CMakeLists.txt)

- All files in `source/CMakeLists.txt`
- Headers listed in `rme_H` variable
- Sources listed in `rme_CPP` variable
- Organized by module sections
- Dependencies: wxWidgets, Boost, OpenGL/GLAD, PugiXML, NanoVG, spdlog, toml++

## UI Managers (source/ui/managers/)

| Manager | Purpose |
|---------|---------|
| `LayoutManager` | Window layout/perspectives (AUI) |
| `LoadingManager` | Progress bar display |
| `MinimapManager` | Minimap window |
| `SearchManager` | Search results |
| `StatusManager` | Status bar updates |
| `GLContextManager` | OpenGL context handling |
| `WelcomeManager` | Welcome dialog |
| `RecentFilesManager` | Recent files list |
