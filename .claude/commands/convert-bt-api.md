# Convert BT_MAPEDITORv3 Code to RME Redux API

You are an expert C++ code converter. Your task is to convert source code written for the OLD BT_MAPEDITORv3 API to the NEW rme_redux API.

## Input
The user will provide file paths to convert. Read each file, apply ALL conversion rules below, and write the converted code back.

## Reference Codebase
- **OLD API**: `B:\Github\BT_MAPEDITORv3` (reference only, do NOT modify)
- **NEW API**: `b:\Github\rme_redux` (target codebase)

When unsure about a NEW API method signature, ALWAYS check the actual header files in rme_redux before writing code. Key headers to check:
- `source/editor/editor.h` - Editor class
- `source/editor/action.h` - Action/BatchAction/Change classes
- `source/editor/action_queue.h` - ActionQueue methods
- `source/editor/selection.h` - Selection class
- `source/map/tile.h` - Tile class
- `source/map/tile_operations.h` - TileOperations namespace
- `source/map/basemap.h` - BaseMap tile access
- `source/game/item.h` - Item factory
- `source/brushes/brush.h` - Brush base class with template is<>/as<>
- `source/ui/gui.h` - GUI singleton
- `source/ui/theme.h` - Theme system
- `source/editor/copybuffer.h` - CopyBuffer class

---

## CONVERSION RULES (apply ALL that match)

### 1. INCLUDE PATHS (flat → hierarchical)

Replace flat includes with prefixed paths:

| Old Include | New Include |
|---|---|
| `"main.h"` | `"app/main.h"` |
| `"settings.h"` | `"app/settings.h"` |
| `"editor.h"` | `"editor/editor.h"` |
| `"action.h"` | `"editor/action.h"` |
| `"action_queue.h"` | `"editor/action_queue.h"` |
| `"selection.h"` | `"editor/selection.h"` |
| `"copybuffer.h"` | `"editor/copybuffer.h"` |
| `"position.h"` | `"map/position.h"` |
| `"tile.h"` | `"map/tile.h"` |
| `"map.h"` | `"map/map.h"` |
| `"basemap.h"` | `"map/basemap.h"` |
| `"tile_operations.h"` | `"map/tile_operations.h"` |
| `"item.h"` | `"game/item.h"` |
| `"items.h"` | `"game/items.h"` |
| `"creature.h"` | `"game/creature.h"` |
| `"sprites.h"` | `"game/sprites.h"` |
| `"brush.h"` | `"brushes/brush.h"` |
| `"doodad_brush.h"` | `"brushes/doodad/doodad_brush.h"` |
| `"ground_brush.h"` | `"brushes/ground/ground_brush.h"` |
| `"wall_brush.h"` | `"brushes/wall/wall_brush.h"` |
| `"carpet_brush.h"` | `"brushes/carpet/carpet_brush.h"` |
| `"table_brush.h"` | `"brushes/table/table_brush.h"` |
| `"door_brush.h"` | `"brushes/door/door_brush.h"` |
| `"creature_brush.h"` | `"brushes/creature/creature_brush.h"` |
| `"spawn_brush.h"` | `"brushes/spawn/spawn_brush.h"` |
| `"house_brush.h"` | `"brushes/house/house_brush.h"` |
| `"house_exit_brush.h"` | `"brushes/house/house_exit_brush.h"` |
| `"waypoint_brush.h"` | `"brushes/waypoint/waypoint_brush.h"` |
| `"raw_brush.h"` | `"brushes/raw/raw_brush.h"` |
| `"optional_border_brush.h"` | `"brushes/border/optional_border_brush.h"` |
| `"camera_path_brush.h"` | `"brushes/camera/camera_path_brush.h"` |
| `"gui.h"` | `"ui/gui.h"` |
| `"gui_ids.h"` | `"ui/gui_ids.h"` |
| `"properties_window.h"` | `"ui/properties/properties_window.h"` |
| `"old_properties_window.h"` | `"ui/properties/old_properties_window.h"` |
| `"browse_tile_window.h"` | `"ui/browse_tile_window.h"` |
| `"map_tab.h"` | `"ui/map_tab.h"` |
| `"image_manager.h"` | `"util/image_manager.h"` |

If an include doesn't match any pattern above, search the rme_redux codebase for the actual file location using Glob.

### 2. EDITOR ACCESS (getter methods → direct members)

| Old Pattern | New Pattern |
|---|---|
| `editor->getMap()` | `editor->map` |
| `editor.getMap()` | `editor.map` |
| `editor->getSelection()` | `editor->selection` |
| `editor.getSelection()` | `editor.selection` |
| `editor->getHistoryActions()` | `editor->actionQueue.get()` |
| `editor.getHistoryActions()` | `editor.actionQueue.get()` |

### 3. ACTION SYSTEM (raw pointers → unique_ptr)

**Creating actions:**
```cpp
// OLD:
BatchAction* batch = editor->createBatch(ACTION_DRAW);
Action* action = editor->createAction(batch);
// or:
BatchAction* batch = editor.createBatch(ACTION_DRAW);
Action* action = editor.createAction(batch);

// NEW:
auto batch = editor->actionQueue->createBatch(ACTION_DRAW);
auto action = editor->actionQueue->createAction(batch.get());
// or with dot notation:
auto batch = editor.actionQueue->createBatch(ACTION_DRAW);
auto action = editor.actionQueue->createAction(batch.get());
```

**Adding changes:**
```cpp
// OLD:
action->addChange(new Change(newTile));
action->addChange(newd Change(newTile));

// NEW:
action->addChange(std::make_unique<Change>(std::move(newTile)));
```

**Committing actions:**
```cpp
// OLD:
batch->addAndCommitAction(action);
editor->addBatch(batch);
// or:
batch->addAction(action);
editor.addBatch(batch, 2);

// NEW:
batch->addAndCommitAction(std::move(action));
editor->addBatch(std::move(batch));
// or:
batch->addAction(std::move(action));
editor.addBatch(std::move(batch), 2);
```

**Deleting actions (remove entirely):**
```cpp
// OLD:
delete action;
delete batch;

// NEW: (remove these lines - unique_ptr handles cleanup)
```

**Action::empty() does NOT exist in rme_redux:**
```cpp
// OLD:
if (action->empty()) { ... }

// NEW:
if (action->size() == 0) { ... }
```

### 4. TILE OPERATIONS (methods moved to namespace)

```cpp
// OLD:
Tile* newTile = tile->deepCopy(map);

// NEW:
auto newTile = TileOperations::deepCopy(tile, map);
// Returns std::unique_ptr<Tile>
```

Add `#include "map/tile_operations.h"` if using TileOperations.

### 5. ITEM CREATION (raw → unique_ptr)

```cpp
// OLD:
Item* item = Item::Create(id);
tile->addItem(item);

// NEW:
auto item = Item::Create(id);
tile->addItem(std::move(item));
// or directly:
tile->addItem(Item::Create(id));
```

```cpp
// OLD:
Item* copy = item->deepCopy();

// NEW:
auto copy = item->deepCopy();
// Returns std::unique_ptr<Item>
```

### 6. TILE MEMBERS (raw → unique_ptr)

```cpp
// OLD:
Item* ground = tile->ground;
for (Item* item : tile->items) { ... }
delete tile->ground;
tile->ground = nullptr;

// NEW:
Item* ground = tile->ground.get();
for (const auto& item : tile->items) { ... }
tile->ground.reset();
```

When iterating and needing the raw pointer:
```cpp
// OLD:
for (Item* item : tile->items) {
    uint16_t id = item->getID();
}

// NEW:
for (const auto& item : tile->items) {
    uint16_t id = item->getID();
}
```

### 7. BRUSH TYPE SYSTEM (virtual methods → templates)

```cpp
// OLD:
if (brush->isDoodad()) { DoodadBrush* db = brush->asDoodad(); ... }
if (brush->isRaw()) { RAWBrush* rb = brush->asRaw(); ... }
if (brush->isGround()) { GroundBrush* gb = brush->asGround(); ... }
if (brush->isWall()) { WallBrush* wb = brush->asWall(); ... }
if (brush->isCreature()) { CreatureBrush* cb = brush->asCreature(); ... }
if (brush->isHouse()) { HouseBrush* hb = brush->asHouse(); ... }
if (brush->isHouseExit()) { ... }
if (brush->isSpawn()) { ... }
if (brush->isDoor()) { ... }
if (brush->isCarpet()) { ... }
if (brush->isTable()) { ... }
if (brush->isWaypoint()) { ... }
if (brush->isEraser()) { ... }

// NEW:
if (brush->is<DoodadBrush>()) { auto* db = brush->as<DoodadBrush>(); ... }
if (brush->is<RAWBrush>()) { auto* rb = brush->as<RAWBrush>(); ... }
if (brush->is<GroundBrush>()) { auto* gb = brush->as<GroundBrush>(); ... }
if (brush->is<WallBrush>()) { auto* wb = brush->as<WallBrush>(); ... }
if (brush->is<CreatureBrush>()) { auto* cb = brush->as<CreatureBrush>(); ... }
if (brush->is<HouseBrush>()) { auto* hb = brush->as<HouseBrush>(); ... }
if (brush->is<HouseExitBrush>()) { ... }
if (brush->is<SpawnBrush>()) { ... }
if (brush->is<DoorBrush>()) { ... }
if (brush->is<CarpetBrush>()) { ... }
if (brush->is<TableBrush>()) { ... }
if (brush->is<WaypointBrush>()) { ... }
if (brush->is<EraserBrush>()) { ... }
```

**IMPORTANT:** The NEW API also keeps the old `isX()`/`asX()` virtual methods on each subclass. However, the preferred pattern is the template `is<T>()`/`as<T>()`. Convert to the template pattern.

### 8. THEME SYSTEM

```cpp
// OLD:
const auto& colors = Theme::Dark();
wxColour bg = colors.background;
wxColour text = colors.text;
wxColour accent = colors.accent;
wxColour border = colors.border;
wxColour surface = colors.surface;
wxColour textMuted = colors.textMuted;
wxColour surfaceAlt = colors.surfaceAlt;
wxColour surfaceHighlight = colors.surfaceHighlight;
wxColour controlBase = colors.controlBase;
wxColour controlHover = colors.controlHover;
wxColour controlActive = colors.controlActive;
wxColour accentSoft = colors.accentSoft;

// NEW:
wxColour bg = Theme::Get(Theme::Role::Background);
wxColour text = Theme::Get(Theme::Role::Text);
wxColour accent = Theme::Get(Theme::Role::Accent);
wxColour border = Theme::Get(Theme::Role::Border);
wxColour surface = Theme::Get(Theme::Role::Surface);
wxColour textMuted = Theme::Get(Theme::Role::TextSubtle);
wxColour surfaceAlt = Theme::Get(Theme::Role::Header);
wxColour surfaceHighlight = Theme::Get(Theme::Role::Selected);
wxColour controlBase = Theme::Get(Theme::Role::CardBase);
wxColour controlHover = Theme::Get(Theme::Role::CardBaseHover);
wxColour controlActive = Theme::Get(Theme::Role::Accent);
wxColour accentSoft = Theme::Get(Theme::Role::AccentHover);
```

Add `#include "ui/theme.h"` when using Theme.

Remove any `ThemeColors` struct usage - use `Theme::Get(Theme::Role::X)` inline.

### 9. GUI METHODS

**Methods that DON'T exist in rme_redux (need alternatives):**

| Old Method | Replacement |
|---|---|
| `g_gui.GetDataDirectory()` | `g_gui.getFoundDataDirectory()` or check `g_settings` for paths |
| `g_gui.BeginRectanglePick(...)` | Does NOT exist - must implement alternative (comment with TODO) |
| `g_gui.CancelRectanglePick()` | Does NOT exist - must implement alternative (comment with TODO) |
| `g_gui.IsRectanglePickActive()` | Does NOT exist (comment with TODO) |

When a method doesn't exist, add a `// TODO: [method] not available in rme_redux - needs implementation` comment.

### 10. MEMORY MANAGEMENT CLEANUP

Remove all instances of:
```cpp
// Remove these patterns:
map.allocator.freeTile(tile);
map.allocator.allocateTile(location);
newd  // Replace with std::make_unique or new as appropriate
```

Replace:
```cpp
// OLD:
Tile* newTile = newd Tile(*location);

// NEW:
auto newTile = std::make_unique<Tile>(*location);
```

### 11. SELECTION TYPE

```cpp
// OLD:
TileSet tiles = selection.getTiles();
for (Tile* tile : tiles) { ... }

// NEW:
const auto& tiles = selection.getTiles();  // Returns const std::vector<Tile*>&
for (Tile* tile : tiles) { ... }
```

### 12. COPYBUFFER

```cpp
// OLD:
copybuffer.setBuffer(newMap, position);

// NEW:
// setBuffer() does NOT exist in rme_redux
// TODO: CopyBuffer::setBuffer() not available - needs alternative approach
```

### 13. CHANGE CONSTRUCTION

```cpp
// OLD:
new Change(newTile)
newd Change(newTile)

// NEW (tile must be unique_ptr<Tile>):
std::make_unique<Change>(std::move(newTile))
```

### 14. DoodadBrush API Changes

The old DoodadBrush had direct access methods that don't exist in new API:

```cpp
// OLD:
int count = brush->getSingleCount(variation);
uint16_t id = brush->getSingleItemId(variation, i);
int chance = brush->getSingleItemChance(variation, i);
int compCount = brush->getCompositeCount(variation);
auto& composite = brush->getCompositeAt(variation, i);
int compChance = brush->getCompositeChanceAt(variation, i);

// NEW - access through getItems().getAlternatives():
const auto& alternatives = brush->getItems().getAlternatives();
if (variation < alternatives.size()) {
    const auto& alt = alternatives[variation];
    // Singles:
    for (const auto& single : alt->single_items) {
        uint16_t id = single.item->getID();
        int chance = single.chance;
    }
    // Composites:
    for (const auto& comp : alt->composite_items) {
        const CompositeTileList& items = comp.items;
        int chance = comp.chance;
    }
}
```

---

## WORKFLOW

1. **Read** each file the user specifies
2. **Apply ALL matching conversion rules** above systematically
3. **When unsure** about a new API, search/read the actual rme_redux headers before guessing
4. **Write** the converted file back
5. **Report** a summary of changes made per file

## IMPORTANT NOTES

- Do NOT add unnecessary includes. Only add what's needed for the conversions.
- Preserve the original code logic and structure - only change API calls.
- If a pattern doesn't have a clear conversion, add a `// TODO:` comment explaining what needs manual attention.
- Do NOT build/compile - the user will do that separately.
- When converting `for` loops over `tile->items`, use `const auto&` since items are now `unique_ptr`.
- When a raw pointer was used to store a result that's now `unique_ptr`, use `auto` and adjust downstream code accordingly.
- Pay special attention to move semantics - `std::move()` is required when transferring ownership of `unique_ptr`.

$ARGUMENTS - Files or directories to convert. If a directory, convert all .cpp and .h files in it.
