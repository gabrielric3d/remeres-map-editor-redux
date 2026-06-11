# Plan: Border Scan Tool (C++ shape-based edge classifier integrated into the Brushes Editor)

## Overview
Implement "Border Scan" as a **C++ tool integrated into the Border Editor** (Brushes Editor → Border tab). The user provides candidate item IDs (range text, comma list, or current map selection); a shape-based kNN classifier — trained lazily, once per session, on the ~248 border groups already loaded in `g_brushes.getBorders()` (~2600 labeled samples) — predicts which of the 12 border edges (`n/e/s/w, cnw/cne/csw/cse, dnw/dne/dse/dsw`) each sprite belongs to with a confidence %. The user reviews/corrects results in a NanoVG grid dialog and clicks "Apply to Border", which fills `BorderEditorDialog::m_borderItems` per edge and refreshes the existing preview/edge panels. Saving stays 100% in the existing `SaveBorder()` flow (untouched). Two new file pairs: `border_classifier.{h,cpp}` (UI-free logic) and `border_scan_dialog.{h,cpp}` (UI).

## User Request
Decisão definitiva do usuário: arquitetura **C++ integrada ao Brushes Editor** (não Lua/Script Manager). Motivo: o Lua não alcança a UI do `BorderEditorDialog` — não preenche `EdgeItemsPanel` nem reusa o fluxo de Save existente. Funcionalidade: classificar sprites candidatos pela FORMA (ocupação alpha, centróide, densidade) em uma das 12 edges com % de confiança, revisão visual em grid, correção manual via wxChoice, tratamento de conflitos/duplicatas, aviso de itens já registrados em outras borders, e aplicação do resultado diretamente no editor de borders (replace por edge + `UpdateEdgeItemsList()` + `UpdatePreview()`), deixando o Save existente intocado.

## Analysis
- **New Files**: Yes
  - `source/brushes/ground/border_classifier.h` — UI-free classifier (training, features, kNN)
  - `source/brushes/ground/border_classifier.cpp`
  - `source/ui/dialogs/border_scan_dialog.h` — scan dialog + `BorderScanGrid` (NanoVG results grid)
  - `source/ui/dialogs/border_scan_dialog.cpp`
- **Modified Files**: Yes
  - `source/ui/dialogs/border_editor_dialog.h` — declare `OnScanBorder(wxCommandEvent&)` + `wxButton* m_scanButton`
  - `source/ui/dialogs/border_editor_dialog.cpp` — "Scan..." button in `borderPropsRow` (cpp:389-417) + handler (apply flow). **`SaveBorder()` (cpp:1146+) is NOT touched.**
  - `source/CMakeLists.txt` — register the 4 new files
- **CMakeLists Update**: Yes (4 entries; anchors below)
- **Menu/Toolbar Wiring**: No — entry point is the "Scan..." button inside the Border tab of the Brushes Editor.
- **Action System**: No — no map mutation. The tool only reads the map selection and mutates dialog state (`m_borderItems`).
- **Data Files (XML/TOML)**: No — saving `borders.xml` remains the existing `SaveBorder()` responsibility.

## Verified API Routes (re-verified in source on 2026-06-11; do NOT re-discover)

### Training data: `g_brushes.getBorders()`
- `Brushes::BorderMap = std::unordered_map<uint32_t, std::unique_ptr<AutoBorder>>` (`source/brushes/brush.h:68`); accessor `getBorders()` (`brush.h:98-100`).
- `AutoBorder::tiles` = `std::array<std::vector<BorderItemChance>, 13>` (`source/brushes/ground/auto_border.h:94`); `BorderItemChance { uint16_t id; int chance = 100; }` (`auto_border.h:24-27`). Index = `BorderType` (slot 0 = `BORDER_NONE`, unused; **edges live in slots 1..12**). Every entry in the vector is a variant — ALL variants become training samples, not just the first.
- `BorderType` (`source/brushes/brush_enums.h`, `BORDER_NONE=0` at line 44): `1=N, 2=E, 3=S, 4=W, 5=CNW, 6=CNE, `**`7=CSW, 8=CSE`**`, 9=DNW, 10=DNE, `**`11=DSE, 12=DSW`**.
- Canonical edge-name table (index = BorderType): `{"n","e","s","w","cnw","cne","csw","cse","dnw","dne","dse","dsw"}` — matches `AutoBorder::edgeNameToID` (`auto_border.cpp:17-31`).

### UI edge enum — THE ENUM TRAP (verified)
`BorderEdgePosition` lives in `source/ui/dialogs/border_editor_dialog.h:54-69` (conversion fns `edgeStringToPosition`/`edgePositionToString` declared at h:71-72, implemented at cpp:84-117): `EDGE_NONE=-1, EDGE_N=0, EDGE_E=1, EDGE_S=2, EDGE_W=3, EDGE_CNW=4, EDGE_CNE=5, `**`EDGE_CSE=6, EDGE_CSW=7`**`, EDGE_DNW=8, EDGE_DNE=9, EDGE_DSE=10, EDGE_DSW=11`.
- `BorderType` has **CSW(7) before CSE(8)**; `BorderEdgePosition` has **CSE(6) before CSW(7)** — different corner order AND different base offset. **Numeric casts between the two enums are ALWAYS a bug.** All conversions go through the canonical edge-name string: classifier speaks `std::string` edge names; the dialog converts with `edgeStringToPosition()`.

### Sprite pixels (with REAL alpha)
Route: `g_item_definitions.get(serverId)` → `ItemDefinitionView` (`source/item_definitions/core/item_definition_store.h:113`; falsy when unknown; `clientId()` at h:66) → `g_gui.gfx.getSprite(clientId)` → `Sprite*` (`source/rendering/core/graphics.h:63`) → `dynamic_cast<GameSprite*>`.
- `GameSprite` (`source/rendering/core/game_sprite.h:73`): public `width, height, layers, pattern_x, pattern_y, pattern_z, frames` (h:126-132), `spriteList` = `std::vector<NormalImage*>` (h:150), `size_t getIndex(w, h, layer, pattern_x, pattern_y, pattern_z, frame)` (h:78).
- `NormalImage::getRGBAData()` → `std::unique_ptr<uint8_t[]>` of 32×32×4 straight-alpha RGBA (`source/rendering/core/normal_image.h:30`, base virtual at `source/rendering/core/image.h:21-22`).
- **ALPHA-FLATTEN TRAP (verified)**: `NvgUtils::CreateCompositeRGBA` (`source/util/nvg_utils.h:23-44`) fills the composite with the **opaque ICON_BACKGROUND shade (alpha=255)** before blending — it destroys the alpha information the classifier needs. It also picks `pattern_x = (gs.pattern_x >= 3) ? 2 : 0` (nvg_utils.h:46), not pattern 0. **Do NOT reuse it** (nor `CreateItemTexture` at nvg_utils.h:109). The classifier implements its own compositor over a fully transparent background (alpha=0), `frame 0, pattern 0,0,0`, layers 0..layers-1 blended src-over preserving alpha.
- Multi-tile sprites: `gs->width > 1 || gs->height > 1` → reject candidate with `MultiTile` (borders are always 1×1).

### "Already registered" detection
`g_brushes.findAutoBordersByBorderItem(itemId, BorderType alignmentHint = BORDER_NONE)` (`source/brushes/brush.h:96`, impl `source/brushes/brush.cpp:226-253`) → `std::vector<const AutoBorder*>` **sorted ascending by border id** (empty when not registered). With the default hint it scans all directions.

### BorderEditorDialog integration surface (`source/ui/dialogs/border_editor_dialog.{h,cpp}`)
- `BorderEditorDialog` is a **wxPanel** hosted as a tab of `BrushesEditorDialog` (h:234-238) — modal dialogs parented to `this` work fine.
- Public members (all verified, h:275-356): `UpdatePreview()` (h:275), `UpdateEdgeItemsList()` (h:276), `m_edgeItemsPanel` (h:306), `m_borderItems` = `std::map<BorderEdgePosition, std::vector<BorderEdgeItem>>` (h:344; `BorderEdgeItem { uint16_t itemId; int chance = 100; }` h:75-81), `m_gridPanel` = `BorderGridPanel*` (h:356; `SetItemId(pos, id)` / `SetItemCount(pos, n)` h:375-376).
- `borderPropsRow` (cpp:389-417): horizontal sizer with Group spin, Optional/Ground checks, Load combo, "Find ID" spin + button (cpp:409-415) — the "Scan..." button is appended here.
- Local dialog IDs are `#define`s `wxID_HIGHEST + 1 .. + 18` (cpp:36-53) — next free is `wxID_HIGHEST + 19`.
- `SaveBorder()` (protected, h:283; impl cpp:1146+) — **untouched**; the user reviews the applied edges and presses the existing Save.

### Results grid base: `VirtualItemGrid` (`source/util/virtual_item_grid.h:13-67`)
NanoVGCanvas-based abstract grid: pure virtual data provider `GetItemCount()` / `GetItem(index)` (+ optional `GetItemName`), selection (`SetSelection`/`GetSelection`/`GetSelectedItemId`), `RefreshGrid()`, `EnsureVisible()`, virtual `OnNanoVGPaint(NVGcontext*, int, int)` override point, `OnItemSelected(int)` hook, `GetOrCreateItemTexture(vg, itemId)` helper, protected layout fields `m_itemSize`, `m_padding`, `m_columns` + `UpdateLayout()` / `HitTest()` / `GetItemRect()`. `BorderScanGrid` subclasses it and overrides `OnNanoVGPaint` to draw status-colored cells (executor: read `source/util/virtual_item_grid.cpp` for the layout math before overriding; if the base layout proves too rigid for 2-line labels, fall back to a standalone `NanoVGCanvas` subclass copying its layout logic — keep the public surface identical either way).

### Map selection (for "From Map Selection")
`g_gui.GetCurrentEditor()` → `Editor*` (nullable); `editor->selection.getTiles()` → `const std::vector<Tile*>&` (`source/editor/selection.h:112`), `empty()`/`size()` (h:96-103). Per tile: `tile->ground` (`unique_ptr<Item>`) and `tile->items` (`vector<unique_ptr<Item>>`); `item->getID()` (`source/game/item.h:174`).

### Misc
- Sprites-loaded guard: `g_gui.gfx.isUnloaded()` (`source/rendering/core/graphics.h:105`).
- CMakeLists anchors (`source/CMakeLists.txt`): `rme_H` — `brushes/ground/auto_border.h` at ~39, `ui/dialogs/border_editor_dialog.h` at ~121; `rme_CPP` — `brushes/ground/auto_border.cpp` at ~474, `ui/dialogs/border_editor_dialog.cpp` at ~548. Insert the new entries alphabetically inside those sections.

## Design Decisions (resolved by planner)

1. **Classifier is UI-free and session-cached.** `border_classifier.h` has **no wx includes** (only `<cstdint> <string> <vector> <array>`); the .cpp includes what the pixel route needs (`ui/gui.h` for `g_gui.gfx`, `item_definitions/...`, `rendering/core/game_sprite.h`, `rendering/core/normal_image.h`, `brushes/brush.h`, `brushes/ground/auto_border.h`). Access via `BorderClassifier::Get()` (Meyers singleton) so training happens **once per session**, lazily, no matter how many times the dialog opens. `ensureTrained()` re-trains if `g_brushes.getBorders().size()` changed since the last training (client/version switch robustness).
2. **Edges travel as canonical name strings** (`"n".."dsw"`). The classifier never sees `BorderEdgePosition`; the dialog never sees `BorderType`. Conversion to `BorderEdgePosition` happens in exactly one place: building the `Apply` result map via `edgeStringToPosition()`.
3. **Own pixel compositor** (static helper in `border_classifier.cpp`): zero-filled 32×32 RGBA buffer (transparent), `getIndex(0, 0, layer, 0, 0, 0, 0)` for `layer = 0..layers-1`, straight-alpha src-over blend (`a = srcA/255; outRGB = srcRGB*a + dstRGB*(1-a); outA = srcA + dstA*(1-a)`), guarding `getIndex` results against `spriteList.size()` and null `getRGBAData()`. Frame 0 / pattern 0 consistently for training AND queries, so any animation/pattern bias cancels out.
4. **Feature vector (71 floats)**: `[0..63]` 8×8 alpha-occupancy grid (fraction of pixels with `alpha > 20` per 4×4 cell, row-major); `[64..65]` opaque-pixel centroid `(cx, cy)` normalized to `[0,1]` **stored ×2.0** (strongest edge discriminator under plain L2); `[66]` global density (opaque / 1024); `[67..70]` quadrant densities (NW, NE, SW, SE = mean of each 4×4 quarter of the grid). Zero opaque pixels → reject as `NoSprite`.
5. **Algorithm**: brute-force kNN, k=5, L2 distance, vote weight `1/(d*d + 1e-4)`; **confidence** = winning edge's share of total vote weight × 100; runner-up edge kept for tooltips. **Skip-self rule**: samples whose `itemId` equals the query id never vote (items already registered cannot classify themselves). ~2600 samples × 71 floats in C++ is sub-millisecond per query; training (~2600 sprite fetches + feature extraction) is well under a second — a `wxBusyCursor` + status label suffices, no progress dialog.
6. **`AlreadyInBorder` is informative, not a hard rejection**: the result still carries edge/confidence (skip-self applied) plus `existingBorderId` (lowest id from `findAutoBordersByBorderItem`). The dialog excludes such rows from auto-assignment by default; the user can force-include via the override wxChoice.
7. **Conflict resolution lives in the dialog** (UI policy, re-run after every Auto-Detect and every manual override): rows with an effective edge are grouped per edge; the highest confidence becomes `Assigned`, the rest `Duplicate`. Manual overrides count as confidence 101 (always win their edge; display keeps the classifier % with a `*` marker). Confidence thresholds: `< 25%` → row is `Pending` (not auto-assigned; manual decision required); `25–50%` → assigned but drawn with the warning color.
8. **Apply semantics = REPLACE per returned edge**: for each `(BorderEdgePosition, itemId)` in the dialog result, `m_borderItems[pos] = { BorderEdgeItem(itemId, 100) };` — only edges present in the result are touched; all other slots of `m_borderItems` are preserved. Then refresh `m_gridPanel` (`SetItemId`/`SetItemCount` for each affected position, mirroring the refresh sequence at the end of `OnLoadBorder` — executor: read that function in `border_editor_dialog.cpp` and replicate its grid-refresh statements), then `UpdateEdgeItemsList()` + `UpdatePreview()`. The user reviews and presses the **existing Save**.
9. **Leave-one-out validation is a debug method**, never run automatically: `validateLeaveOneOut()` returns a formatted per-edge accuracy report (classify each sample against the rest). Wired only to Shift+click on "Auto-Detect" (report shown via `wxMessageBox`/log) — useful for the manual test pass, zero cost otherwise.
10. **Future Lua exposure (note only — OUT of this plan's scope)**: because `BorderClassifier` is UI-free and speaks plain `uint16_t`/string edges, a later `app.classifyBorderItems(ids)` binding can wrap `Get().classify()` directly. No binding is added now.

## Tasks

### Phase 1: Classifier core (UI-free logic)

#### Task 1.1: `BorderClassifier` header
- **Description**: Public API of the classifier, no wx includes.
- **Files**: `source/brushes/ground/border_classifier.h` (new)
- **Details**:
  - Include guard `RME_BORDER_CLASSIFIER_H` + `#include <array> <cstdint> <string> <vector>` only.
  - ```cpp
    struct BorderScanResult {
        enum class Status { Classified, MultiTile, NoSprite, AlreadyInBorder };
        uint16_t itemId = 0;
        std::string edge;              // canonical name "n".."dsw"; empty when not classified
        float confidence = 0.0f;       // 0..100
        std::string secondEdge;        // runner-up, for tooltips
        float secondConfidence = 0.0f;
        Status status = Status::NoSprite;
        uint32_t existingBorderId = 0; // lowest matching border id when AlreadyInBorder
    };

    class BorderClassifier {
    public:
        static constexpr size_t FEATURE_COUNT = 71;
        static constexpr size_t EDGE_COUNT = 12;
        // Canonical edge names, index = BorderType - 1 (csw before cse, dse before dsw).
        static const std::array<std::string, EDGE_COUNT> EDGE_NAMES;

        static BorderClassifier& Get();              // session singleton

        bool ensureTrained();                        // lazy; true when sampleCount() > 0
        size_t sampleCount() const;
        size_t groupCount() const;

        std::vector<BorderScanResult> classify(const std::vector<uint16_t>& candidates);

        std::string validateLeaveOneOut();           // debug: per-edge accuracy report
    private:
        struct Sample {
            std::array<float, FEATURE_COUNT> features;
            uint8_t edgeIndex;                       // 0..11 = BorderType - 1
            uint16_t itemId;
            uint32_t borderId;
        };
        BorderScanResult classifyOne(uint16_t itemId) const;
        static bool extractFeatures(uint16_t itemId, std::array<float, FEATURE_COUNT>& out,
                                    BorderScanResult::Status& failure);
        std::vector<Sample> m_samples;
        size_t m_groupCount = 0;
        size_t m_trainedBorderMapSize = 0;           // retrain trigger on version switch
        bool m_trained = false;
    };
    ```
  - Header comment documenting: edge strings are the only cross-module currency (never cast `BorderType` ↔ `BorderEdgePosition` numerically); `AlreadyInBorder` results still carry edge/confidence; class is UI-free by design (future Lua exposure).

#### Task 1.2: `BorderClassifier` implementation
- **Depends On**: Task 1.1
- **Description**: Pixel compositor, feature extraction, lazy training, kNN, LOO debug.
- **Files**: `source/brushes/ground/border_classifier.cpp` (new)
- **Details**:
  - Includes: `"app/main.h"`, own header, `"brushes/brush.h"`, `"brushes/ground/auto_border.h"`, `"item_definitions/core/item_definition_store.h"`, `"ui/gui.h"` (pixel route only — `g_gui.gfx`), `"rendering/core/game_sprite.h"`, `"rendering/core/normal_image.h"`, `<algorithm> <cmath> <format>`.
  - `EDGE_NAMES = { "n","e","s","w","cnw","cne","csw","cse","dnw","dne","dse","dsw" }` (index = BorderType − 1; cross-check against `AutoBorder::edgeNameToID`, `auto_border.cpp:17-31`).
  - **Pixel helper** `static bool getItemRGBA32(uint16_t itemId, std::array<uint8_t, 32*32*4>& out, BorderScanResult::Status& failure)`:
    - `const auto def = g_item_definitions.get(itemId); if (!def)` → `NoSprite`.
    - `GameSprite* gs = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(def.clientId())); if (!gs)` → `NoSprite`.
    - `if (gs->width > 1 || gs->height > 1)` → `MultiTile`.
    - Zero-fill `out` (transparent). For `layer = 0..gs->layers-1`: `size_t idx = gs->getIndex(0, 0, layer, 0, 0, 0, 0);` guard `idx < gs->spriteList.size()`; `auto data = gs->spriteList[idx]->getRGBAData();` skip if null; src-over blend per Design Decision 3. **Own compositor — do NOT call `NvgUtils::CreateCompositeRGBA`** (opaque-background + pattern!=0 trap, nvg_utils.h:23-46).
  - **`extractFeatures`**: call `getItemRGBA32`; single pass over the 1024 pixels building: per-cell opaque counts (`gx = px / 4, gy = py / 4`, threshold `alpha > 20`), centroid sums, global count. Assemble the 71 floats per Design Decision 4 (centroid guarded `0.5f, 0.5f` when no opaque pixels — but 0 opaque → return false with `NoSprite` anyway).
  - **`ensureTrained`**: if `m_trained && m_trainedBorderMapSize == g_brushes.getBorders().size()` → return cached. Else clear and iterate `g_brushes.getBorders()`: for each group, for `slot = 1..12`, for **every** `BorderItemChance` variant in `tiles[slot]`: `extractFeatures(variant.id)` success → push `Sample{features, uint8_t(slot - 1), variant.id, border->id}` (failures silently skipped — counted for the status label). Set `m_groupCount`, `m_trainedBorderMapSize`, `m_trained = true`. Return `!m_samples.empty()`. Items appearing in several groups/edges contribute one sample per occurrence (each sample stands on its own).
  - **`classifyOne`**: extract features (failure → result with that `Status`, confidence 0). kNN per Design Decision 5 (k=5, skip `sample.itemId == itemId`, weight `1/(d2 + 1e-4f)`); fill `edge/confidence/secondEdge/secondConfidence`; `status = Classified`. Then `auto matches = g_brushes.findAutoBordersByBorderItem(itemId);` non-empty → `status = AlreadyInBorder; existingBorderId = matches.front()->id;` (vector is sorted ascending — verified brush.cpp:249-251). Defensive rule: if no neighbor voted (empty training set — the dialog already guards `ensureTrained()` before calling), leave `edge` empty and `confidence = 0` with `status = Classified`; the dialog maps an empty edge to `Pending`.
  - **`classify`**: `ensureTrained()` then map `classifyOne` over candidates (order-preserving).
  - **`validateLeaveOneOut`**: for each sample, classify its feature vector against all other samples (skip same `itemId`, which also covers skip-self); accumulate per-edge correct/total; return a formatted string (per-edge `name: correct/total (pct%)` + overall). No automatic invocation.

### Phase 2: Scan dialog (UI)

#### Task 2.1: `BorderScanGrid` + `BorderScanDialog` header
- **Depends On**: Task 1.1 (uses `BorderScanResult` in the row model)
- **Description**: Dialog class + NanoVG results grid declaration + row model.
- **Files**: `source/ui/dialogs/border_scan_dialog.h` (new)
- **Details**:
  - Includes: `<wx/dialog.h> <wx/textctrl.h> <wx/button.h> <wx/choice.h> <wx/stattext.h>`, `"util/virtual_item_grid.h"`, `"brushes/ground/border_classifier.h"`, `"ui/dialogs/border_editor_dialog.h"` (for `BorderEdgePosition` in the result map), `<map> <vector>`.
  - Row model:
    ```cpp
    struct BorderScanRow {
        BorderScanResult result;
        bool manual = false;          // user picked an edge in the wxChoice
        std::string manualEdge;       // valid when manual
        bool excluded = false;        // "(excluded)" picked
        enum class State { Pending, Assigned, Duplicate, Excluded, Rejected, AlreadyInBorder };
        State state = State::Pending;
        std::string effectiveEdge() const; // manualEdge when manual, else result.edge
    };
    ```
  - `class BorderScanGrid : public VirtualItemGrid` — ctor `(wxWindow* parent, std::vector<BorderScanRow>* rows)`; implements `GetItemCount()`/`GetItem(index)` over the row vector; overrides `OnNanoVGPaint` (status-colored cells, sprite + 2 text lines) and `OnItemSelected` (invokes a `std::function<void(int)>` callback set by the dialog); sets `m_itemSize`/`m_padding` in the ctor for ~96×72 cells.
  - `class BorderScanDialog : public wxDialog`:
    ```cpp
    public:
        explicit BorderScanDialog(wxWindow* parent);
        const std::map<BorderEdgePosition, uint16_t>& GetEdgeAssignments() const; // valid after wxID_OK
    private:
        void OnAddCandidates(wxCommandEvent&);
        void OnFromMapSelection(wxCommandEvent&);
        void OnClearCandidates(wxCommandEvent&);
        void OnAutoDetect(wxCommandEvent&);
        void OnEdgeOverride(wxCommandEvent&);
        void OnApply(wxCommandEvent&);
        void OnRowSelected(int index);
        void AddCandidateIds(const std::vector<uint16_t>& ids);
        bool ParseCandidateText(const wxString& text, std::vector<uint16_t>& out, wxString& error);
        void RecomputeStates();          // conflict resolution (Design Decision 7)
        void RefreshGridAndCounts();
        void UpdateSelectedRowUI();
        std::vector<BorderScanRow> m_rows;
        std::map<BorderEdgePosition, uint16_t> m_assignments;
        // controls: m_candidateInput, m_addButton, m_fromSelectionButton, m_clearButton,
        // m_detectButton, m_statusLabel, m_grid, m_selThumbLabel/m_selInfoLabel,
        // m_edgeChoice, m_applyButton, m_cancelButton
    ```
  - Local control IDs in an anonymous-namespace enum in the .cpp (`wxID_HIGHEST + 5200` block, per the ui-patterns convention); prefer `Bind()` over event tables.

#### Task 2.2: `BorderScanDialog` implementation
- **Depends On**: Task 2.1, Task 1.2 (runtime)
- **Description**: Layout, candidate intake, Auto-Detect, grid painting, override row, conflict resolution, Apply.
- **Files**: `source/ui/dialogs/border_scan_dialog.cpp` (new)
- **Details**:
  - Includes: `"app/main.h"`, own header, `"brushes/ground/border_classifier.h"`, `"editor/editor.h"`, `"editor/selection.h"`, `"map/tile.h"`, `"game/item.h"`, `"ui/gui.h"`, `"ui/theme.h"`, `<wx/sizer.h> <wx/msgdlg.h>` etc.
  - **Layout** (vertical, `SetSizerAndFit`, min size ~640×560, resizable, centered; all colors via `Theme::Get(...)`, fonts via `Theme::GetFont(...)`, spacing via `Theme::Grid(...)`; ALL UI strings in English):
    1. Candidates row: `wxTextCtrl m_candidateInput` (hint `"e.g. 55550-55561, 60000"`), buttons `Add`, `From Map Selection`, `Clear`, and a count label.
    2. Detect row: `Auto-Detect` button + `m_statusLabel` (`"Classifier not trained yet"` → `"Trained on N samples from M border groups (K skipped)"`).
    3. `BorderScanGrid m_grid` (proportion 1, `wxEXPAND`).
    4. Selected-row line: small info label `"item 12345 — suggested cnw (87%), 2nd dnw (9%) — assigned"` + `wxChoice m_edgeChoice` with 14 entries: `"(auto)"`, the 12 canonical edge names (from `BorderClassifier::EDGE_NAMES`, in that order), `"(excluded)"`.
    5. Bottom row: `Apply to Border` (primary) + `Cancel`.
  - **Candidate parsing** (`ParseCandidateText`): split on `','`, trim; token = `N` or `A-B` (`A <= B`, both in `[100, 65535]`); invalid token → error message naming it; cap one Add at 2000 ids (alert). Dedup against existing rows (`std::set<uint16_t>`).
  - **From Map Selection**: `Editor* editor = g_gui.GetCurrentEditor();` (button disabled at ctor when null — Phase 4); collect unique `tile->ground->getID()` + every `tile->items[i]->getID()` from `editor->selection.getTiles()`; empty selection → info message, no crash.
  - **Vetting on add** (cheap, no training): run `BorderClassifier::Get().classify({id})`? No — too eager. Instead store rows as `Pending` with just `result.itemId`; full classification happens on Auto-Detect. (Multi-tile/no-sprite rejection therefore also lands on Auto-Detect — acceptable and simpler; the grid shows `Pending` until then.)
  - **Auto-Detect** (`OnAutoDetect`): `wxBusyCursor`; `auto& clf = BorderClassifier::Get();` `if (!clf.ensureTrained())` → message `"No training data: no borders are loaded."` and return; update `m_statusLabel`; `clf.classify(ids)` for ALL row ids (re-detect clears previous manual overrides — simplest correct rule, noted in the status tooltip); copy results into rows; `RecomputeStates(); RefreshGridAndCounts();`. **Shift held** (`wxGetKeyState(WXK_SHIFT)`): additionally show `clf.validateLeaveOneOut()` in a message box (debug, Design Decision 9).
  - **`RecomputeStates`** (Design Decision 7): rejected statuses (`MultiTile`/`NoSprite`) → `Rejected`; `excluded` → `Excluded`; `AlreadyInBorder && !manual` → `AlreadyInBorder` (not auto-assigned); `!manual && confidence < 25` → `Pending`; remaining rows grouped by `effectiveEdge()`: winner (manual=101, else highest confidence) → `Assigned`, others → `Duplicate`.
  - **Grid painting** (`BorderScanGrid::OnNanoVGPaint`): per cell — rounded rect background `Theme::Role::CardBase` (selected: `Selected`), 2px status border: `Assigned` → `Success`, `Duplicate` → `Warning`, `Pending` → `Border`, `Excluded`/`AlreadyInBorder` → `TextSubtle`, `Rejected` → `Error`; assigned with confidence `< 50` → `Warning` border instead (Phase 4 rule); sprite via `GetOrCreateItemTexture(vg, itemId)`; line 1 = item id; line 2 = `"cnw 87%"` / `"duplicate"` / `"multi-tile"` / `"no sprite"` / `"in border 12"` / `"excluded"` (manual edges suffixed `*`). Text colors `Theme::Role::Text` / `TextSubtle`.
  - **Override flow**: grid selection → `OnRowSelected` updates the info label and sets `m_edgeChoice` (re-entrancy guard flag so programmatic `SetSelection` doesn't trigger `OnEdgeOverride`). `OnEdgeOverride`: `"(auto)"` → `manual=false, excluded=false`; `"(excluded)"` → `excluded=true`; edge name → `manual=true, manualEdge=name, excluded=false` (this is also how `AlreadyInBorder` rows get force-included). Then `RecomputeStates() + RefreshGridAndCounts()`.
  - **Apply** (`OnApply`): build `m_assignments` from `Assigned` rows via `edgeStringToPosition(row.effectiveEdge())` (assert ≠ `EDGE_NONE`); empty → message `"Nothing to apply — no edge has an assigned item."`; else `EndModal(wxID_OK)`. **The dialog does NOT touch `m_borderItems` nor save XML** — the caller applies.

### Phase 3: Integration + CMakeLists

#### Task 3.1: "Scan..." button + apply flow in `BorderEditorDialog`
- **Depends On**: Task 2.2
- **Description**: Entry point inside the Border tab and the REPLACE-per-edge apply.
- **Files**: `source/ui/dialogs/border_editor_dialog.h`, `source/ui/dialogs/border_editor_dialog.cpp` (modify)
- **Details**:
  - Header: declare `void OnScanBorder(wxCommandEvent& event);` next to the other handlers (h:241-277 block) and `wxButton* m_scanButton = nullptr;` in the Border Tab member block (near h:306).
  - cpp: `#define ID_BORDER_SCAN wxID_HIGHEST + 19` (after `ID_MODIFY_GROUND_BORDER`, cpp:53). In `CreateGUIControls`, append to `borderPropsRow` after the Find button (cpp:415): `m_scanButton = newd wxButton(m_borderPanel, ID_BORDER_SCAN, "Scan...");` + tooltip `"Classify candidate items into border edges by sprite shape"`; add with the row's existing flag style; wire `m_scanButton->Bind(wxEVT_BUTTON, &BorderEditorDialog::OnScanBorder, this);`.
  - `OnScanBorder`:
    ```cpp
    BorderScanDialog dlg(this);
    if (dlg.ShowModal() != wxID_OK) return;
    for (const auto& [pos, itemId] : dlg.GetEdgeAssignments()) {
        m_borderItems[pos] = { BorderEdgeItem(itemId, 100) };   // REPLACE only returned edges
        m_gridPanel->SetItemId(pos, itemId);
        m_gridPanel->SetItemCount(pos, 1);
    }
    UpdateEdgeItemsList();
    UpdatePreview();
    ```
    (Executor: before finalizing, read the end of `OnLoadBorder` in this file and mirror any additional grid-refresh statement it performs — e.g. a `Refresh()` on the grid panel — so Scan-applied borders render identically to loaded ones.) Add `#include "ui/dialogs/border_scan_dialog.h"` at the top.
  - **`SaveBorder()` (cpp:1146+) and `ValidateBorder()` are NOT modified** — the user reviews the applied edges and uses the existing Save button.

#### Task 3.2: Register new files in CMakeLists
- **Parallel**: Yes (can run with Task 3.1)
- **Description**: Add the 4 new files to the build.
- **Files**: `source/CMakeLists.txt` (modify)
- **Details**:
  - `rme_H`: `${CMAKE_CURRENT_LIST_DIR}/brushes/ground/border_classifier.h` alphabetically in the brushes/ground header block (anchor: `auto_border.h` ~line 39); `${CMAKE_CURRENT_LIST_DIR}/ui/dialogs/border_scan_dialog.h` in the ui/dialogs header block (anchor: `border_editor_dialog.h` ~line 121, scan sorts right after editor).
  - `rme_CPP`: `${CMAKE_CURRENT_LIST_DIR}/brushes/ground/border_classifier.cpp` (anchor: `auto_border.cpp` ~line 474); `${CMAKE_CURRENT_LIST_DIR}/ui/dialogs/border_scan_dialog.cpp` (anchor: `border_editor_dialog.cpp` ~line 548).

### Phase 4: Robustness & polish

#### Task 4.1: Guards, validation, confidence styling
- **Depends On**: Task 3.1
- **Description**: Degraded-state handling and the confidence color rules.
- **Files**: `source/ui/dialogs/border_editor_dialog.cpp`, `source/ui/dialogs/border_scan_dialog.cpp` (modify)
- **Details**:
  - Sprites not loaded (`g_gui.gfx.isUnloaded()`, graphics.h:105): disable `m_scanButton` at creation (and re-check in `OnScanBorder` defensively) with tooltip `"Requires a loaded client (sprites)"`.
  - No editor open (`g_gui.GetCurrentEditor() == nullptr`): disable `From Map Selection` at dialog construction, tooltip `"Open a map to collect items from the selection"`.
  - Empty candidate list on Auto-Detect → status label message (Theme `Error` color), no crash; invalid range tokens → `wxMessageBox` naming the offending token; `A-B` with `A > B` rejected.
  - Confidence styling (already specified in Task 2.2 painting, confirm both): `< 50%` → warning border/label color on the cell; `< 25%` → row forced `Pending` (never auto-assigned; user must pick manually).
  - Duplicate Add of an id already in the list → silently skipped, count label says `"+0 new"`.

#### Task 4.2 (OPTIONAL — extra phase, skip if over budget): "Check Edge" mode
- **Depends On**: Task 4.1
- **Description**: Parity with the original tool's "Check Element" tab: pick a target edge, list which candidates classify into it, sorted by confidence.
- **Files**: `source/ui/dialogs/border_scan_dialog.{h,cpp}` (modify)
- **Details**: a `wxChoice` with the 12 edges + `Check` button above the grid switches the grid into a filtered view (rows whose top edge equals the target, sorted by confidence desc); a `Show All` toggle restores the normal view. No new files, no classifier changes.

## Execution Order
1. Phase 1: Task 1.1 → Task 1.2 (header before impl)
2. Phase 2: Task 2.1 → Task 2.2 (2.1 can start as soon as 1.1 exists)
3. Phase 3: Task 3.1 + Task 3.2 (parallel)
4. Phase 4: Task 4.1 → Task 4.2 (optional)

## Testing Notes (manual — the agent NEVER builds; the user compiles and verifies)
1. **Entry point**: Brushes Editor → Border tab → "Scan..." button at the end of the properties row; disabled with tooltip when no client is loaded.
2. **Self-test with a known group**: pick a border group from `data/1098/borders.xml` (e.g. the 12 items of border id 1), enter them as a range/list → Auto-Detect → expect ≥10/12 correct edges with high confidence (skip-self guarantees they can't match themselves); all rows show `in border 1` status (AlreadyInBorder) — force-include one via the wxChoice and confirm it becomes `Assigned`.
3. **LOO report**: Shift+click Auto-Detect → per-edge accuracy report appears; overall accuracy should be well above chance (~8.3%); straight edges (n/e/s/w) typically strongest.
4. **From Map Selection**: select mapped border tiles → unique ground+item ids are added; with no map open the button is disabled.
5. **Rejections**: a 64×64 item id → `multi-tile`; an unused id → `no sprite`; both colored with the Error border and never assignable.
6. **Conflicts**: two candidates landing on the same edge → higher confidence `Assigned`, other `Duplicate` (warning color); overriding the duplicate to a free edge makes both `Assigned`.
7. **Thresholds**: a low-confidence row (<25%) stays `Pending` until manually assigned; 25–50% shows the warning border.
8. **Apply flow**: Apply to Border → only the returned edges replace their `m_borderItems` slots (pre-fill a different edge manually first and confirm it survives); `EdgeItemsPanel` and the preview refresh immediately; the grid panel shows the new sprites.
9. **Save unchanged**: after Apply, the existing Save button writes `borders.xml` exactly as before (restart required to load it — pre-existing limitation, not a regression).
10. **Session caching**: open the scan dialog twice — second Auto-Detect skips training (status label appears instantly).

## Risks & Considerations
- **ENUM TRAP (highest risk)**: `BorderType` (brush_enums.h: `7=CSW, 8=CSE`) and `BorderEdgePosition` (border_editor_dialog.h:54-69: `6=CSE, 7=CSW`) disagree on the south-corner order *and* on the base offset — **any numeric cast between them is a silent corner-swap bug**. All conversions go through canonical edge-name strings (`EDGE_NAMES`, `edgeStringToPosition`, `edgePositionToString`, `AutoBorder::edgeNameToID`); the only conversion point is `OnApply` building the result map.
- **ALPHA-FLATTEN TRAP**: `NvgUtils::CreateCompositeRGBA` (nvg_utils.h:23-46) pre-fills an opaque background (ICON_BACKGROUND, alpha 255) and selects `pattern_x = 2` for ≥3-pattern sprites — both poison the shape features. The classifier MUST use its own transparent-background compositor with frame 0 / pattern 0 (Design Decision 3). Reject sprites where `width > 1 || height > 1`.
- **Training-set composition**: partial groups (missing edges) and items registered in multiple groups/edges are fine — every `BorderItemChance` variant is an independent labeled sample; no per-group completeness is assumed. Skipped items (no sprite) are only counted for the status label.
- **No materials runtime reload**: applying + saving still requires an editor restart for the new border to load (`SaveBorder` limitation, untouched and out of scope — the existing Save already communicates this).
- **`VirtualItemGrid` extension risk**: the base paint draws its own labels; `BorderScanGrid` replaces `OnNanoVGPaint` entirely and reuses only layout/selection/texture helpers. If the base layout (`m_itemSize`-driven) can't host two text lines cleanly, fall back to a standalone `NanoVGCanvas` subclass copying the layout math — keep the dialog-facing API identical so the swap is local.
- **Classifier statelessness vs. UI policy**: thresholds (25/50), conflict resolution, and manual-override precedence are dialog policy, NOT classifier logic — keeps the classifier reusable (future Lua binding note, Design Decision 10).
- **Re-entrancy on the override wxChoice**: programmatic `SetSelection` during `OnRowSelected` must not fire `OnEdgeOverride` (guard flag), or manual flags get clobbered while browsing rows.
- **`findAutoBordersByBorderItem` cost**: linear scan over ~248 groups per candidate — negligible for ≤2000 candidates, do not pre-index.
- **Dialog parentage**: `BorderEditorDialog` is a wxPanel inside the Brushes Editor dialog; parenting the modal `BorderScanDialog` to `this` is correct (wx resolves the top-level parent).
- **Conventions**: `newd` for wx children (parent-owned), `unique_ptr` elsewhere; every color via `Theme::Get(...)`; all UI strings in English; 4 new files registered in CMakeLists (Task 3.2) or the build fails.

## Superseded (from the Lua plan this file previously contained — intentionally dropped)
- `scripts/border_scan/` (manifest, border_scan.lua, classifier.lua, borders_xml.lua), the `Image:spriteFeatures` / `app.writeFile` / `app.getVersionDataDirectory` bindings, and the Lua-side borders.xml writer — all replaced by the integrated C++ tool above, because Lua cannot reach the `BorderEditorDialog` UI (no `EdgeItemsPanel` fill, no reuse of the existing Save flow).
- The `app.borders` `tiles[k]` off-by-one trap no longer applies; its successor is the `BorderType` ↔ `BorderEdgePosition` enum trap above.
- Kept from the Lua plan (still valid here): the 71-float feature design, kNN k=5 with margin-weighted vote share as confidence, skip-self rule, conflict-resolution policy, and the restart-after-save limitation.
