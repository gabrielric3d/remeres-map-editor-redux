# Summary: New Map Dialog

**Plan**: `.redux/plans/01_New_Map_Dialog.md`
**Executed**: 2026-03-17
**Status**: Completed

## Changes Made

| File | Action | Description |
|------|--------|-------------|
| source/app/settings.h | Modified | Added MAP_PROPERTIES_REMEMBER_SAVE_LOCATION and MAP_PROPERTIES_DEFAULT_SAVE_LOCATION config keys |
| source/app/settings.cpp | Modified | Registered defaults for new config keys |
| source/ui/gui_ids.h | Modified | Added MAP_PROPERTIES_SYNC_EXTERNAL_FILES and MAP_PROPERTIES_BROWSE_SAVE_LOCATION IDs |
| source/map/map.h | Modified | Added setFilename() method (was missing in Redux) |
| source/ui/map/map_properties_window.h | Modified | Expanded with new members, methods, and allow_create_from_selection parameter |
| source/ui/map/map_properties_window.cpp | Modified | Full rewrite with BT features: Map Name, Save Location, Size Presets, Auto External Files, Create From Selection |
| source/editor/managers/editor_manager.cpp | Modified | NewMap() now shows MapPropertiesWindow dialog before creating map, supports Create From Selection |
| source/ui/main_menubar.cpp | Modified | Updated OnMapProperties to handle wxID_OK/wxID_CANCEL return values |

## Tasks Executed
- [1.1] Add Config Keys - Done
- [1.2] Add GUI IDs - Done
- [2.1] Update MapPropertiesWindow header - Done
- [2.2] Implement expanded MapPropertiesWindow - Done
- [3.1] Integrate dialog in EditorManager::NewMap() - Done
- [4.1] Verify API signatures - Done
- [4.2] Keep waypoint file field - Done

## API Adaptations
- `CopyBuffer::copy()` in Redux takes 2 args (Editor&, int) vs BT's 3 args (Editor&, int, bool). Selection is copied by default.
- Added `Map::setFilename()` method since Redux didn't have one (BT does).
- Redux `setHouseFilename`/`setSpawnFilename` take 1 arg (no `keep_unnamed` param). Simplified accordingly.
- Changed `EndModal(1)` to `EndModal(wxID_OK)` / `EndModal(wxID_CANCEL)` for proper dialog result handling.
- Updated `main_menubar.cpp` caller to check `!= wxID_OK && != wxID_CANCEL` instead of `== 0` for failure detection.
- Kept `waypoint_filename_ctrl` (Redux-specific, not in BT).
- Used `ClientVersion::getBestMatch()` instead of `ClientVersion::get()` for initial version selection (Redux pattern).

## Testing Instructions
- Compile and open the editor
- File > New should open MapPropertiesWindow dialog
- Verify all fields: Map Name, Save Location, Remember, Description, Map/Client Version, Size Preset, Dimensions, Auto External Files, House/Spawn/Waypoint files
- Size Preset changes should update width/height
- Width/height changes should sync preset (or show "Custom")
- Map Name changes should update house/spawn filenames when Auto External Files is checked
- Browse should open wxDirDialog
- Cancel should not create a map
- OK should create map with chosen settings
- If there was an active selection, "Create From Selection" checkbox should appear and paste selection at (0,0,7) on the new map
- Remember Save Location should persist between sessions
- File > Map Properties on an existing map should still work correctly

## Code Review Fixes Applied (2026-03-17)

Review: `.redux/reviews/01_Review_New_Map_Dialog.md`

| # | Severity | Issue | Fix |
|---|----------|-------|-----|
| 1 | Critical | Null pointer dereference - `ClientVersion::get()` result used without null check (line 434) | Added null check with error dialog and early return |
| 2 | Critical | Empty protocol dropdown when `USE_OTBM_4_FOR_ALL_MAPS` enabled - loop was inside `else` branch only | Moved loop outside if/else so it iterates `versions` in both branches |
| 3 | Medium | Config keys `MAP_PROPERTIES_*` placed under "Graphics" section | Moved to "Editor" section in settings.cpp |
| 4 | Medium | Wasted `SetStringSelection` on protocol_choice before `UpdateProtocolList()` clears it | Removed the premature call at line 198 |
| 5 | Medium | `MAP_PROPERTIES_REMEMBER_SAVE_LOCATION` registered as `Int` but used as boolean | Changed to `Bool(MAP_PROPERTIES_REMEMBER_SAVE_LOCATION, false)` |
| 6 | Low | Redundant `map.setName()` on line 471 overwritten by line 500 | Removed first call; added `else` branch to set name when no save location |
| 7 | Low | Tooltip inconsistency on protocol_choice | Updated tooltip to "Select the client version (protocol)" |
| 8 | Low | `waypoint_filename_ctrl` not managed by auto-sync checkbox | Added `BuildAutoWaypointFilename()`, `default_waypoint_filename` member, and included waypoint in sync logic |

## Notes
- The dialog is now mandatory before creating a new map (File > New)
- The Create From Selection feature captures the selection snapshot BEFORE creating the new editor, so the source map's selection is preserved
- The unique_ptr pattern ensures the editor is automatically cleaned up if the user cancels
