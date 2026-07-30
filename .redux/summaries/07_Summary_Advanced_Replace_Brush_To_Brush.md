# Summary: Advanced Replace Tool — Brush→Brush Replacement + Undo

**Plan**: `.redux/plans/07_Plan_Advanced_Replace_Brush_To_Brush.md`
**Executed**: 2026-07-19
**Status**: Completed

## Changes Made

| File | Action | Description |
|------|--------|-------------|
| `source/ui/replace_tool/rule_manager.h` | Modified | `SlotKind{Item,Brush}` enum; `kind`/`brushName` on `ReplacementTarget`; `fromKind`/`fromBrushName` + `isBrushRule()`/`hasSource()` on `ReplacementRule` |
| `source/ui/replace_tool/rule_manager.cpp` | Modified | JSON writes the new fields always, reads them with `j.value(...)` → old rule sets load unchanged (default `SlotKind::Item`) |
| `source/brushes/carpet/carpet_brush.h` | Modified | Public `getItems()` getter for the protected `m_items` |
| `source/ui/replace_tool/brush_mapping_service.h` | Created | Role-equivalence API: `FindBrush`, `AreCompatible`, `MapItem`, `GetPreviewItemId`, `GetFamilyName` |
| `source/ui/replace_tool/brush_mapping_service.cpp` | Created | Ground (center + border direction 1-12), Wall (17 alignments, doors/windows by `DoorType` + locked flag), Carpet (14 alignment groups) |
| `source/ui/replace_tool/replacement_engine.cpp` | Modified | Full undo refactor (BatchAction/Action/Change over deep copies) + brush-rule evaluation |
| `source/ui/replace_tool/brush_picker_dialog.h` | Created | Filterable modal brush chooser API |
| `source/ui/replace_tool/brush_picker_dialog.cpp` | Created | Name/Type list, search field with hotkey suspend, family filter, Theme colors |
| `source/ui/replace_tool/rule_builder_panel.h` | Modified | 3 new `HitResult::Type` values appended at the end; shared `ReplaceToolSuppressAddSlot()` helper |
| `source/ui/replace_tool/rule_builder_panel.cpp` | Modified | Badge hit-test, kind toggling, picker wiring, single-target cardinality, item drops rejected on brush slots |
| `source/ui/replace_tool/rule_card_renderer.h` | Modified | `KIND_BADGE_H`, `DrawRuleBrushCard`, `DrawSlotKindBadge` |
| `source/ui/replace_tool/rule_card_renderer.cpp` | Modified | Slot dispatch by kind, badge drawing, magic hover literals replaced by enum names, ghost slot now uses the shared suppression helper |
| `source/ui/replace_tool/replace_tool_window.cpp` | Modified | Kind-aware completeness check, brush rules skipped in `existingIds`, pre-execution warning for unknown brushes, similarity panel guard |
| `source/CMakeLists.txt` | Modified | Registered the 4 new files |

## Tasks Executed

- **1.1** Extend `ReplacementRule` with item/brush mode — Done
- **1.2** Expose `CarpetBrush` items — Done
- **2.1** Create `BrushMappingService` — Done
- **3.1** Refactor `ExecuteReplacement` onto the action system — Done
- **3.2** Evaluate brush rules in the engine — Done
- **4.1** Create `BrushPickerDialog` — Done
- **4.2** Brush mode in card slots (hit-test + interaction) — Done
- **4.3** Draw slots in brush mode — Done
- **5.1** Register new files in CMakeLists — Done
- **5.2** Window adjustments — Done (see note on the alleged syntax error)

Tasks 1.1 – 4.2 were already present in the working tree from an earlier partial execution; they were re-read and verified against the real headers rather than rewritten. Tasks 4.3, 5.1 and 5.2 were implemented in this pass.

## Testing Instructions

1. **Persistence backward-compat**: open a rule set saved before this change (`%APPDATA%/.../replacer_rules/*.json`). It must load identically with every slot in ITEM mode. Save and reopen — the new fields appear without changing behaviour.
2. **Undo (critical, also covers item rules)**: run any replacement, press Ctrl+Z. The map must return exactly to the previous state in a single undo. Ctrl+Y redoes.
3. **Undo with offset**: rule with offset ≠ 0,0 — undo must restore both the source tile (item back) and the destination tile (item removed).
4. **Ground→Ground**: paint a "grass" area with borders against empty; rule brush `grass` → `sand`; execute. Every ground becomes sand ground and **every border keeps its direction** — the outline shape must not change.
5. **Wall→Wall**: draw walls with corners, a T and a stretch with a door and a window. Swap the brush: segments keep orientation, the door stays a door (locked stays locked), the window stays a window.
6. **Carpet→Carpet**: swap a carpet with borders; alignments preserved.
7. **Incompatibility**: with a GroundBrush source, the target picker must not list Wall/Carpet brushes at all.
8. **Missing brush**: hand-edit a JSON with an invalid `fromBrushName`. Execute → warning dialog listing the broken name, rule skipped, no crash.
9. **Precedence**: brush rule `grass`→`sand` plus an item rule for one specific grass border id. The item rule wins for that id.
10. **Scopes**: repeat 4 and 5 under Selection, Viewport and All Map.

## Notes

### Undo refactor (Task 3.1 — highest risk item)

`ExecuteReplacement` no longer mutates live tiles. All edits accumulate in a `std::map<Position, std::unique_ptr<Tile>>` of deep copies (`acquireTile()`), and exactly **one** `Change` is emitted per position at the end — the mandatory invariant, since two Changes for the same tile inside one Action would make the second silently overwrite the first.

`PendingMove` now stores `Position` + item index instead of live `Tile*`/`Item*` pointers. Relocation runs in two phases over the same pending-tile map: removals first (indices sorted descending so earlier erases do not invalidate later ones), creations second. A position that is both a move source and a move target therefore still produces a single combined Change.

Verified invariants:
- The scan phase never calls `getOrCreateTile`, so the map is not mutated while `foreach_TileOnMap` iterates it (All Map scope).
- In-place `setID` edits never shift indices, so the indices recorded during the scan stay valid in the removal phase.
- `Action::commit()` swaps selection membership internally (`removeInternal`/`addInternal`), so the Selection scope does not end up with dangling tile pointers.
- Nothing is added to the queue when no tile changed, keeping the undo history clean.

### Deviation from the plan

The plan stated that `replace_tool_window.cpp:430` contained a build-blocking stray backslash (`\ Auto-add: ...`). **This is not true in the current tree** — the line is already a well-formed `// Auto-add: ...` comment in HEAD, and the file contains no backslashes at all. Nothing was "fixed"; the rest of Task 5.2 was applied normally.

### Extras applied beyond the literal task text (all inside Task 4.3/5.2 scope)

- `DrawRuleCard` now derives its ghost-slot/height decision from the shared `ReplaceToolSuppressAddSlot()` helper instead of its own inline `hasTrash` loop. Without this the renderer would draw a `+` slot for single-target brush rules that the hit-test and `GetRuleHeight` already suppress, giving a dead clickable area and a card height mismatch.
- `OnRuleSelected` no longer feeds `fromId == 0` (a brush rule) into `VisualSimilarityService::FindSimilar`.
- Added `<functional>` to `replacement_engine.cpp` (`std::greater`) and `rendering/core/graphics.h` to `brush_picker_dialog.cpp` (`Sprite`, `SPRITE_SIZE_32x32`) — both were only available transitively.

### Design constraints preserved

- **No re-bordering**: `doBorders`/`doWalls`/`doCarpets` are never called. Substitution is strictly 1:1 by role, so the existing layout is preserved exactly.
- **Safe failure**: an item that belongs to the source brush but has no equivalent in the destination (`resolved == false`) keeps its original item rather than leaving a hole.
- **Item rules take precedence** over brush rules (the `fromId` map is consulted first).
- **Out of scope by decision**: Table and Doodad brushes — the picker does not list them.

### Known limitations

- Only the first outer and first inner `AutoBorder` of a ground brush are reachable through the public accessors, so brushes with several `<border to="X">` blocks only map their first pair. Widening this would need a new `getAllAutoBorders()` accessor on `GroundBrush`.
- A brush slot's body re-opens the picker, so a brush target cannot be deleted by clicking it — toggle the badge back to ITEM first, or delete the whole rule.
- All Map scope now deep-copies every changed tile; on a large map with a broad rule the undo history may exceed `Config::UNDO_MEM_SIZE`, which `ActionQueue` handles by dropping old entries (graceful degradation).

**Build**: not run — the user compiles manually.
