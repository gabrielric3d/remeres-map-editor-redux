# Summary: Border Scan Tool (C++ shape-based edge classifier integrated into the Brushes Editor)

**Plan**: `.redux/plans/05_Border_Scan_Tool.md`
**Executed**: 2026-06-11
**Status**: Completed (including optional Task 4.2)

## Changes Made

| File | Action | Description |
|------|--------|-------------|
| source/brushes/ground/border_classifier.h | Created | UI-free kNN classifier API: `BorderScanResult`, `BorderClassifier` singleton, canonical `EDGE_NAMES` table |
| source/brushes/ground/border_classifier.cpp | Created | Own transparent-background sprite compositor, 71-float feature extraction, lazy training over `g_brushes.getBorders()`, kNN (k=5) with skip-self rule, leave-one-out validation |
| source/ui/dialogs/border_scan_dialog.h | Created | `BorderScanRow` model, `BorderScanGrid` (VirtualItemGrid subclass with filtered-view support), `BorderScanDialog` |
| source/ui/dialogs/border_scan_dialog.cpp | Created | Full dialog: candidate input (ranges/list/map selection), Auto-Detect, status-colored NanoVG grid, per-row override choice, conflict resolution, Check Edge filter, Apply |
| source/ui/dialogs/border_editor_dialog.h | Modified | Declared `OnScanBorder(wxCommandEvent&)` and `wxButton* m_scanButton` |
| source/ui/dialogs/border_editor_dialog.cpp | Modified | `ID_BORDER_SCAN` (wxID_HIGHEST + 19), "Scan..." button appended to `borderPropsRow` (Bind, sprite-loaded guard), `OnScanBorder` apply flow (REPLACE per returned edge + grid/preview refresh). `SaveBorder()`/`ValidateBorder()` untouched |
| source/CMakeLists.txt | Modified | Registered the 4 new files alphabetically in `rme_H` and `rme_CPP` |

## Tasks Executed
- Task 1.1 `BorderClassifier` header — Done
- Task 1.2 `BorderClassifier` implementation — Done
- Task 2.1 `BorderScanGrid` + `BorderScanDialog` header — Done
- Task 2.2 `BorderScanDialog` implementation — Done
- Task 3.1 "Scan..." button + apply flow in `BorderEditorDialog` — Done
- Task 3.2 CMakeLists registration — Done
- Task 4.1 Guards, validation, confidence styling — Done (folded into 2.2/3.1)
- Task 4.2 (optional) "Check Edge" mode — Done

## Testing Instructions
1. Open Brushes Editor → Border tab; the "Scan..." button sits at the end of the properties row (disabled with tooltip if no client/sprites are loaded).
2. Click Scan..., type a range (e.g. `4456-4467`) and Add; or select tiles on the map and use "From Map Selection".
3. Click Auto-Detect: each cell shows sprite, item id and `edge %`; border colors: green = assigned, yellow = duplicate/low confidence (< 50%), gray = excluded/already-in-border, red = rejected (multi-tile / no sprite), neutral = pending (< 25% or no match).
4. Select a cell and use the Edge choice to override (`(auto)` / edge name / `(excluded)`); manual edges show a `*` and win conflicts.
5. "Check edge" + Check filters the grid to one edge sorted by confidence; Show All restores.
6. Shift+click Auto-Detect shows the leave-one-out per-edge accuracy report.
7. "Apply to Border" fills the Border Editor grid/edge panels (one item per returned edge, chance 100); edges not returned are preserved. Save still goes through the normal Save flow.

## Notes
- **Enum trap respected**: classifier speaks canonical edge-name strings only; the single string→`BorderEdgePosition` conversion is `edgeStringToPosition()` in `BorderScanDialog::OnApply`. No numeric casts between `BorderType` and `BorderEdgePosition` anywhere.
- **Alpha-flatten trap respected**: the classifier has its own compositor (transparent background, frame 0, pattern 0,0,0, src-over preserving alpha); `NvgUtils::CreateCompositeRGBA`/`CreateItemTexture` are not used by the classifier (the grid still uses the base `GetOrCreateItemTexture` for display only).
- Training is lazy and session-cached; it re-trains automatically if `g_brushes.getBorders().size()` changes (client version switch).
- `BorderScanRow` gained a `scanned` flag (not in the plan's struct) so rows added before Auto-Detect render as "not scanned" instead of being misclassified as Rejected (default `BorderScanResult::status` is `NoSprite`).
- `BorderScanGrid` keeps the base `VirtualItemGrid` square-cell layout (`m_itemSize = 96`) instead of 96×72 cells: the base class layout/hit-test methods are non-virtual, so shadowing them would desync mouse handling. The two text lines fit comfortably in the 96px cell.
- Status label "(K rejected)" counts candidates that failed feature extraction (multi-tile/no sprite); the classifier itself does not expose a skipped-training-sample count (header signature was fixed by the plan).
- Future Lua exposure: the classifier is UI-free by design; no binding was added (per plan).
- NOT built/compiled (per project rules) — needs a manual build to validate.
