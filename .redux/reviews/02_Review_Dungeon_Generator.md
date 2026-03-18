# Code Review: Dungeon Generator System

## Summary
- **Files Reviewed**: 10 (3 core logic, 6 UI dialogs, 1 data file)
- **Issues Found**: 24 (4 critical, 9 medium, 11 low)
- **Overall Assessment**: NEEDS FIXES

## Critical Issues

### [CRITICAL-1] BSP Room Coordinate Calculation is Broken
- **File**: source/editor/dungeon_generator.cpp:485-490
- **Type**: Bug
- **Description**: The `createBSPRooms` function computes the room position three times in succession, overwriting the result each time. The first two computations are dead code. The final calculation `{startX + rx, startY + ry, rw, rh}` is incorrect because `rx` already includes `node->x` offset from the padding distribution, meaning rooms will be offset incorrectly and may overlap or fall outside the intended area. The `startX/startY` is the origin (top-left corner), not center, so adding the full `rx` (which is relative to node, not origin) produces wrong absolute coordinates.
- **Impact**: BSP algorithm generates rooms at wrong positions; rooms may cluster, overlap, or fall outside the dungeon bounds.
- **Fix**:
```cpp
// Replace lines 485-490 with proper coordinate calculation:
int absX = startX + node->x + rx;
int absY = startY + node->y + ry;
node->room = {absX, absY, rw, rh};
node->hasRoom = true;
```

### [CRITICAL-2] Duplicate Floor Entries in m_floorPositions
- **File**: source/editor/dungeon_generator.cpp:117-122
- **Type**: Bug / Performance
- **Description**: The `paintFloor` function unconditionally pushes to `m_floorPositions` and `tileChanges` even if the position was already painted. Corridors and overlapping rooms will add the same position multiple times. This causes:
  1. `m_floorPositions` grows to potentially massive size (corridors repaint floors)
  2. `decorateFloors` iterates over duplicates, wasting cycles
  3. `applyChanges` groups by position but creates duplicate items on the same tile
- **Impact**: Duplicate ground items stacked on tiles; performance degradation on large dungeons; wasted memory.
- **Fix**:
```cpp
void DungeonGenerator::paintFloor(int x, int y, int z, uint16_t floorId,
                                   std::vector<std::pair<Position, uint16_t>>& tileChanges) {
    if (getGridTile(x, y) == floorId) return; // Already painted
    setGridTile(x, y, floorId);
    m_floorPositions.push_back(Position(x, y, z));
    tileChanges.push_back({Position(x, y, z), floorId});
}
```

### [CRITICAL-3] Position Hash Collision in applyChanges
- **File**: source/editor/dungeon_generator.cpp:922-924
- **Type**: Bug
- **Description**: The position hash in `applyChanges` uses `x & 0xFFFF` which truncates coordinates to 16 bits. For maps with coordinates > 65535, different positions will hash to the same value, merging changes from different tiles together. The separate `posHash` function (line 99-101) uses 32 bits per coordinate which is correct, but `applyChanges` reimplements a different, weaker hash.
- **Impact**: On large maps (coordinates > 65535), tiles at different positions will receive each other's items.
- **Fix**: Use the existing `posHash` function or include the z coordinate properly:
```cpp
uint64_t hash = posHash(change.first.x, change.first.y);
// Or if z matters, add a separate z-aware hash
```

### [CRITICAL-4] Wall Placement Overwrites Floor Data
- **File**: source/editor/dungeon_generator.cpp:208-210
- **Type**: Bug
- **Description**: After building walls, the code calls `setGridTile(wall.pos.x, wall.pos.y, 0xFFFF)` for each wall position. But wall positions include tiles that are already floor tiles (walls are placed ON floor tiles, not adjacent to them). This overwrites the floor ID in the grid, which breaks subsequent `isFloorTile` checks used by `decorateFloors` and `isPlacementValid`. Floor tiles that received walls will not get decorations.
- **Impact**: Floor tiles with walls never receive detail decorations; placement checks using `isFloorTile` give wrong results after wall phase.
- **Fix**: Use a separate data structure for wall tracking, or mark walls with a flag that doesn't destroy the floor information:
```cpp
// Option 1: Use a separate set for wall positions
std::unordered_set<uint64_t> m_wallPositionSet;
// Option 2: Don't overwrite the grid, just skip decoration on wall positions
```

## Medium Issues

### [MEDIUM-1] Hardcoded Colors Throughout Dialog (Convention Violation)
- **File**: source/ui/dialogs/dungeon_generator_dialog.cpp (21 instances)
- **Type**: Convention Violation
- **Description**: Extensive use of hardcoded `wxColour(...)` values instead of `Theme::Get(Theme::Role::X)`. Found in:
  - Lines 57, 69: CreateItemBitmap background/fallback colors
  - Lines 161, 164: selection label colors (green/red)
  - Lines 538, 558, 568, 575, 586, 592: pick status text colors
  - Lines 736-839: entire algorithm preview panel uses hardcoded colors
- **Fix**: Replace all with Theme roles. For status colors, consider adding semantic theme roles or using existing ones like `Theme::Role::Success`, `Theme::Role::Error`, `Theme::Role::Warning`.

### [MEDIUM-2] Hardcoded Colors in Preset Editor and Border Grid
- **File**: source/ui/dialogs/dungeon_preset_editor_dialog.cpp:45, 295
- **File**: source/ui/dialogs/dungeon_border_grid_panel.cpp:165
- **Type**: Convention Violation
- **Description**: `MakeItemBitmap` uses hardcoded dark blue background `wxColour(0x0C, 0x14, 0x2A)`. The border grid uses `wxColour(30, 30, 40)` for empty slots. Hint text uses `wxColour(140, 140, 140)`.
- **Fix**: Use `Theme::Get(Theme::Role::Background)` and `Theme::Get(Theme::Role::TextSubtle)`.

### [MEDIUM-3] Wall Algorithm Only Uses north/west/nw/pillar IDs
- **File**: source/editor/dungeon_generator.cpp:652-656
- **Type**: Bug / Incomplete Implementation
- **Description**: The `buildWalls` function only uses `wallConfig.north` (for horizontal), `wallConfig.west` (for vertical), `wallConfig.nw` (for corners), and `wallConfig.pillar`. The `south`, `east`, `ne`, `sw`, `se` fields from WallConfig are completely ignored. This means all horizontal walls look the same, all vertical walls look the same, and all corners look the same, regardless of their actual orientation.
- **Impact**: Walls do not use directional variants; the preset supports them in data but the generator ignores them.
- **Fix**: Map each wall direction correctly:
```cpp
// Use direction-specific IDs when placing walls
// e.g., south-facing walls should use wallConfig.south
// east-facing walls should use wallConfig.east
// Corner pieces should use their specific corner IDs
```

### [MEDIUM-4] Random Walk totalSteps Counter is Per-Walker Not Global
- **File**: source/editor/dungeon_generator.cpp:637
- **Type**: Bug
- **Description**: `totalSteps` is incremented once per walker step inside the walker loop, but the outer while loop checks `totalSteps < params.maxSteps`. With 3 walkers, the effective max steps is `maxSteps / walkerCount` per walker since all walkers increment the same counter. This is not documented and may confuse users who expect `maxSteps` to be per-walker.
- **Impact**: With default 3 walkers and 50000 maxSteps, each walker gets ~16666 steps, not 50000. Minor behavioral issue.
- **Fix**: Either document this clearly or make the counter truly global by incrementing once per outer loop iteration.

### [MEDIUM-5] fillBackground Off-by-One: Generates (width+1)*(height+1) Tiles
- **File**: source/editor/dungeon_generator.cpp:130-131
- **Type**: Bug
- **Description**: The loops use `y <= height` and `x <= width`, generating `(width+1)*(height+1)` tiles instead of `width*height`. This means the fill area is always 1 tile wider and taller than specified.
- **Impact**: Fill area extends 1 tile beyond the intended dungeon boundary in both dimensions.
- **Fix**: Change to `y < height` and `x < width`.

### [MEDIUM-6] No Ground Tile Replacement - Items Stack on Existing Tiles
- **File**: source/editor/dungeon_generator.cpp:939-943
- **Type**: Design Issue
- **Description**: In `applyChanges`, the generator always calls `addItem` to add the ground tile as a regular item. It never sets or replaces the tile's ground. This means:
  1. If an existing tile has a ground, the new "ground" is added as a stacked item on top
  2. The generated dungeon floor items are just items, not actual ground tiles
- **Impact**: Generated dungeons don't properly replace existing terrain; items stack incorrectly.
- **Fix**: Check if the item to be placed is a ground item and use `newTile->ground = std::move(item)` for ground changes, or at minimum clear existing items when generating a fresh dungeon area.

### [MEDIUM-7] wxYield() in Progress Callback Can Cause Reentrancy
- **File**: source/ui/dialogs/dungeon_generator_dialog.cpp:686
- **Type**: Bug Risk
- **Description**: `wxYield()` inside the progress callback processes all pending events, which could allow the user to click Generate again or close the dialog while generation is in progress, causing reentrancy or use-after-free.
- **Impact**: Potential crash if user interacts with dialog during generation.
- **Fix**: Use `wxSafeYield()` or disable the Generate button during generation:
```cpp
generateBtn->Disable();
// ... generate ...
generateBtn->Enable();
```

### [MEDIUM-8] Corridor Algorithm Always L-Shaped (Horizontal Then Vertical)
- **File**: source/editor/dungeon_generator.cpp:244-268
- **Type**: Quality / Algorithm
- **Description**: `carveCorridor` always goes horizontal first, then vertical, creating uniform L-shaped corridors. This produces visually monotonous dungeons where all corridors bend the same way.
- **Impact**: Less interesting dungeon layouts.
- **Fix**: Randomly choose between horizontal-first and vertical-first:
```cpp
bool horizontalFirst = std::uniform_int_distribution<int>(0, 1)(m_rng) == 0;
if (horizontalFirst) { /* horizontal then vertical */ }
else { /* vertical then horizontal */ }
```

### [MEDIUM-9] PresetManager::savePresets Doesn't Report Failures
- **File**: source/editor/dungeon_generator_preset.cpp:289-303
- **Type**: Quality
- **Description**: `savePresets` always returns `true` even if `preset.saveToFile` fails for individual presets. No error is reported.
- **Fix**: Track and report failures.

## Low Issues

### [LOW-1] Dead Variable in createBSPRooms
- **File**: source/editor/dungeon_generator.cpp:487
- **Type**: Code Quality
- **Description**: `int halfW = 0;` is declared but never used (the comment says "The node coords are already relative"). The three successive room assignments (lines 485, 488, 490) are confusing dead code.
- **Fix**: Remove dead code; keep only the final correct computation.

### [LOW-2] Magic Numbers in Patch Generation
- **File**: source/editor/dungeon_generator.cpp:750-761
- **Type**: Code Quality
- **Description**: The patch chance (`>= 1` meaning 1% chance), radius range (1-4), and brush chance (0.15f) are all hardcoded magic numbers. These should be configurable or at least named constants.
- **Fix**: Add constants or make them part of `DungeonPreset`.

### [LOW-3] Unused WallConfig Fields (south, east, ne, sw, se)
- **File**: source/editor/dungeon_generator.h:41-52
- **Type**: Code Quality
- **Description**: The struct declares south, east, ne, sw, se fields that are stored/loaded but never used by the wall building algorithm. This is misleading to users who configure these values in the editor expecting them to work.

### [LOW-4] BSP getBSPRoomCenter Always Prefers Left Subtree
- **File**: source/editor/dungeon_generator.cpp:501-516
- **Type**: Algorithm Quality
- **Description**: When connecting BSP rooms, `getBSPRoomCenter` always returns the leftmost/topmost room in a subtree. This means corridors always connect to the same room in each partition, creating star-like patterns. A better approach would be to find the room closest to the sibling partition.
- **Fix**: Return the room with center closest to a target point, or randomly pick among leaf rooms.

### [LOW-5] No Room Size Variation in BSP
- **File**: source/editor/dungeon_generator.cpp:479-480
- **Type**: Algorithm Quality
- **Description**: BSP rooms can use `std::min(maxW, node->w - 2)` which may limit room size unnecessarily. More importantly, the partition constraints don't encourage variety -- rooms tend to fill their partitions since padding is small.
- **Fix**: Consider adding a room-to-partition ratio parameter to create more varied room sizes within the BSP grid.

### [LOW-6] Random Walk Lacks Connectivity Guarantee
- **File**: source/editor/dungeon_generator.cpp:556-639
- **Type**: Algorithm Quality
- **Description**: When `startCenter` is false, walkers start at random positions and may create disconnected cave systems. There's no flood-fill connectivity check or post-processing to connect isolated regions.
- **Fix**: After generation, run a flood fill from the largest connected component and either discard or connect smaller components.

### [LOW-7] Unused halfW Variable in buildWalls
- **File**: source/editor/dungeon_generator.cpp:649-650
- **Type**: Code Quality (minor)
- **Description**: `halfW` and `halfH` are computed and used correctly, but the naming could be clearer since they transform the coordinate system.

### [LOW-8] DungeonPresetEditorDialog::OnNewPreset Leaks on Rapid Clicks
- **File**: source/ui/dialogs/dungeon_generator_dialog.cpp:474-478
- **Type**: Potential Leak
- **Description**: `new DungeonPresetEditorDialog(...)` is allocated with `new` and shown via `Show()` (modeless). If the user clicks "New" multiple times rapidly, multiple editor windows are created. wxWidgets manages their lifetime via `Destroy()`, so they won't leak, but it's poor UX.
- **Fix**: Disable the button while an editor is open, or use `ShowModal()`.

### [LOW-9] applyPatches Copies Entire m_floorPositions Vector
- **File**: source/editor/dungeon_generator.cpp:759
- **Type**: Performance
- **Description**: `auto floors = m_floorPositions;` copies the entire vector (potentially tens of thousands of entries) just to iterate over it. The copy is needed because `paintFloor` appends to `m_floorPositions`, but a simpler solution exists.
- **Fix**: Store the current size and iterate by index up to that size:
```cpp
size_t originalSize = m_floorPositions.size();
for (size_t i = 0; i < originalSize; ++i) {
    const auto& pos = m_floorPositions[i];
    // ...
}
```

### [LOW-10] No Validation of Min/Max Room Size Relationship
- **File**: source/ui/dialogs/dungeon_generator_dialog.cpp:637-640
- **Type**: Input Validation
- **Description**: Users can set minRoomSize > maxRoomSize in the UI controls, which would cause `std::uniform_int_distribution` to have undefined behavior or throw an exception.
- **Fix**: Add validation before generation, or constrain the spin controls.

### [LOW-11] Unused `m_lastGenerateSuccess` After Reroll
- **File**: source/ui/dialogs/dungeon_generator_dialog.cpp:713-729
- **Type**: Logic
- **Description**: `OnReroll` calls `editor->actionQueue->undo()` then `OnGenerate`. But if the undo fails or the generation fails, `m_lastGenerateSuccess` may be in an inconsistent state. The undo should be conditional on `m_lastGenerateSuccess`.

## Positive Notes

1. **Clean architecture**: Good separation between generator engine, preset system, and UI dialogs. The `DungeonGen` namespace keeps things organized.
2. **Modern C++ patterns**: Proper use of `std::unique_ptr` for BSP tree nodes, `std::mt19937` for reproducible RNG, structured bindings.
3. **Action system integration**: `applyChanges` correctly uses the editor's action queue with batch/action/change pattern and unique_ptr ownership.
4. **Good UI design**: The preset editor with visual grid panels, drag-and-drop from palette, brush loaders, and search fields is well thought out.
5. **Progress callback**: Nice cancellation support with progress dialog for large generations.
6. **All files properly registered** in CMakeLists.txt.
7. **Proper include paths**: All includes use hierarchical format per project conventions.
8. **brush->as<T>() template pattern** used correctly throughout (WallBrush, GroundBrush, DoodadBrush).

## Recommendations

### Algorithm Improvements for Better Dungeons

1. **Randomize corridor direction** (MEDIUM-8): Alternate between horizontal-first and vertical-first L-corridors, or add S-shaped and Z-shaped corridors for variety.

2. **Fix BSP coordinate system** (CRITICAL-1): This is the most impactful fix -- the BSP algorithm likely generates broken layouts due to the coordinate bug.

3. **Add room shape variation**: Currently all rooms are rectangles. Consider:
   - L-shaped rooms (two overlapping rectangles)
   - Circular/oval rooms (carve using distance from center)
   - Rooms with alcoves (carve small extensions from rectangle edges)

4. **Improve corridor variety**:
   - Add winding corridors (multiple turns instead of single L)
   - Variable-width corridors (wider near rooms, narrower in between)
   - Occasional corridor rooms (small open areas along corridors)

5. **Add post-processing passes**:
   - Smooth cave walls for Random Walk (cellular automata pass)
   - Remove dead-end corridors (optional)
   - Connectivity verification (flood fill)
   - Room feature placement (pillars in large rooms, stairs, etc.)

6. **Use directional wall IDs** (MEDIUM-3): The preset supports different wall tiles per direction but the algorithm ignores them. Using south/east wall variants would significantly improve visual quality.

7. **Ensure ground tile replacement** (MEDIUM-6): The generator should set the ground properly, not just stack items. This is likely the most user-visible issue after the BSP bug.
