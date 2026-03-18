# Code Review: New Map Dialog (Expanded MapPropertiesWindow)

## Summary
- **Files Reviewed**: 8
- **Issues Found**: 8 (2 critical, 3 medium, 3 low)
- **Overall Assessment**: NEEDS FIXES

## Critical Issues

### [CRITICAL] Null pointer dereference in OnClickOK - ClientVersion::get()
- **File**: source/ui/map/map_properties_window.cpp:434
- **Type**: Null Pointer Dereference / Crash Risk
- **Description**: `ClientVersion::get()` can return `nullptr` if the protocol choice string doesn't match any known version. The result is immediately dereferenced with `->getProtocolID()` without a null check.
- **Impact**: Application crash if user selects an invalid or unrecognized protocol string.
- **Fix**:
```cpp
ClientVersion* selected_version = ClientVersion::get(nstr(protocol_choice->GetStringSelection()));
if (!selected_version) {
    DialogUtil::PopupDialog(this, "Error", "Invalid client version selected.", wxOK);
    return;
}
new_ver.client = selected_version->getProtocolID();
```

### [CRITICAL] UpdateProtocolList does not populate choices when USE_OTBM_4_FOR_ALL_MAPS is enabled
- **File**: source/ui/map/map_properties_window.cpp:311-332
- **Type**: Bug / Logic Error
- **Description**: When `USE_OTBM_4_FOR_ALL_MAPS` is true, `versions` is populated via `ClientVersion::getAllVisible()` but the loop that appends items to `protocol_choice` is inside the `else` branch. The `versions` variable is never iterated, resulting in an empty protocol choice dropdown.
- **Impact**: Protocol choice will be empty when `USE_OTBM_4_FOR_ALL_MAPS` is enabled, likely causing a crash on line 434 when trying to get the selected protocol.
- **Fix**:
```cpp
void MapPropertiesWindow::UpdateProtocolList() {
    wxString ver = version_choice->GetStringSelection();
    wxString client = protocol_choice->GetStringSelection();

    protocol_choice->Clear();

    ClientVersionList versions;
    if (g_settings.getInteger(Config::USE_OTBM_4_FOR_ALL_MAPS)) {
        versions = ClientVersion::getAllVisible();
    } else {
        MapVersionID map_version = MAP_OTBM_1;
        if (ver.Contains("0.5.0")) {
            map_version = MAP_OTBM_1;
        } else if (ver.Contains("0.6.0")) {
            map_version = MAP_OTBM_2;
        } else if (ver.Contains("0.6.1")) {
            map_version = MAP_OTBM_3;
        } else if (ver.Contains("0.7.0")) {
            map_version = MAP_OTBM_4;
        }
        versions = ClientVersion::getAllForOTBMVersion(map_version);
    }

    for (const auto& ver : versions) {
        protocol_choice->Append(wxstr(ver->getName()));
    }
    protocol_choice->SetSelection(0);
    protocol_choice->SetStringSelection(client);
}
```

## Medium Issues

### [MEDIUM] Config keys placed under wrong section in settings.cpp
- **File**: source/app/settings.cpp:360-361
- **Type**: Convention / Organization
- **Description**: `MAP_PROPERTIES_REMEMBER_SAVE_LOCATION` and `MAP_PROPERTIES_DEFAULT_SAVE_LOCATION` are registered under the "Graphics" section (between `PALETTE_GRID_ICON_SIZE` and cursor colors). These are map/editor properties, not graphics settings.
- **Fix**: Move them to the "Editor" section or create a new "MapProperties" section. At minimum, use `Bool` instead of `Int` for `MAP_PROPERTIES_REMEMBER_SAVE_LOCATION` since it's used as a boolean.

### [MEDIUM] getBestMatch vs get for initial protocol selection
- **File**: source/ui/map/map_properties_window.cpp:290-293
- **Type**: Bug / Robustness
- **Description**: The constructor uses `ClientVersion::getBestMatch()` which is more tolerant of version mismatches, but the initial line 198 `protocol_choice->SetStringSelection(wxstr(g_version.GetCurrentVersion().getName()))` sets it before `UpdateProtocolList()` is called, which clears and repopulates the list. Then lines 290-293 correctly set it again. This is not a bug per se, but the line 198 call is wasted work.
- **Fix**: Remove line 198 (`protocol_choice->SetStringSelection(...)`) since it gets overwritten after `UpdateProtocolList()` on lines 290-293.

### [MEDIUM] MAP_PROPERTIES_REMEMBER_SAVE_LOCATION registered as Int, should be Bool
- **File**: source/app/settings.cpp:360
- **Type**: Convention
- **Description**: The setting is used as a boolean (via `getBoolean()` and `setInteger(... ? 1 : 0)`) but registered with `Int(MAP_PROPERTIES_REMEMBER_SAVE_LOCATION, 0)`. Should use `Bool()` for consistency with other boolean settings.
- **Fix**:
```cpp
Bool(MAP_PROPERTIES_REMEMBER_SAVE_LOCATION, false);
```

## Low Issues

### [LOW] Redundant map name set on line 471 then overwritten on line 500
- **File**: source/ui/map/map_properties_window.cpp:471,500
- **Type**: Code Quality
- **Description**: `map.setName(map_filename)` is called on line 471, then if `save_location` is not empty, `map.setName(nstr(target_map_file.GetFullName()))` is called on line 500, overwriting the first call. While not a bug (the second value includes the path), it's redundant.
- **Suggestion**: Only call `setName()` once, after determining the final value.

### [LOW] Missing tooltips on version_choice in BT comparison
- **File**: source/ui/map/map_properties_window.cpp:168-198
- **Type**: Quality / UX Enhancement
- **Description**: The Redux version adds tooltips to all controls, which is a nice improvement over BT. However, the `protocol_choice` tooltip on line 197 says "Select the target client version" but the label says "Client Version" - consider making them consistent.
- **Suggestion**: Minor, no action needed.

### [LOW] Waypoint filename control not guarded by sync checkbox
- **File**: source/ui/map/map_properties_window.cpp:266-269
- **Type**: Code Quality / Feature Gap
- **Description**: `waypoint_filename_ctrl` is an extension over BT (which doesn't have it), but it's not managed by the "Auto External Files" sync checkbox. When the user toggles "Use map name", house and spawn filenames are auto-synced but the waypoint filename is not.
- **Suggestion**: Consider adding auto-sync for waypoint filename as well (e.g., `BuildAutoWaypointFilename()`) or document that it's intentionally manual.

## Positive Notes

- Clean conversion from BT API patterns (`editor.getMap()` -> `editor.map`, etc.)
- Good use of `FROM_DIP()` for DPI-aware dialog sizing (BT doesn't have this)
- Proper use of `unique_ptr` for the editor in `NewMap()` - good RAII pattern with automatic cleanup on cancel
- Added tooltips to all controls for better UX
- Added button icons (check/xmark) for OK/Cancel
- Proper use of `Bind()` instead of event tables (modern wxWidgets pattern)
- Selection snapshot via `CopyBuffer` in `NewMap()` is well-implemented with proper ownership
- `waypoint_filename_ctrl` is a useful Redux-specific addition
- `SetIcons()` call for the dialog window icon

## Recommendations

1. **Fix the two critical issues first** - the null pointer dereference and empty protocol list are both crash risks.
2. **Move config keys** to the appropriate section in settings.cpp.
3. **Use `Bool()` macro** for boolean settings to maintain consistency.
4. Consider adding a waypoint auto-sync to the external files feature.

## Review Checklist

```
[x] No raw new/delete for Actions, Changes, Tiles, Items
[x] std::move() used for all unique_ptr transfers
[x] Theme::Get(Theme::Role::X) for all colors (no colors used)
[x] brush->is<T>()/as<T>() template pattern (no brushes used)
[x] Hierarchical include paths
[x] editor->map / editor->selection / editor->actionQueue
[x] New files registered in CMakeLists.txt (no new files)
[ ] Null checks before pointer dereference - FAIL (line 434)
[x] No use-after-move
[x] wxWidgets event handlers properly bound
[x] No hardcoded colors
[x] Consistent coding style with project
```
