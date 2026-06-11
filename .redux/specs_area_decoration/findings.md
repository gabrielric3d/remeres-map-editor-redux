# 00_structs.md — area_decoration.cpp lines 40-285: ItemGroup selection, FloorRule matching/ [divergences-found]

## [undocumented-divergence] Engine:selectItemFromRule (main.lua:1035-1055); no ItemGroup type anywhere in the file
- ESPERADO: Spec defines ItemGroup {name, items, totalWeight=0 CACHED, not auto-maintained} with recalculateWeights() and selectRandom(rng) that trusts the cached totalWeight: stale-high cache makes the &items.back() fallback reachable (last item over-selected), stale-low makes tail items unreachable, and a never-recalculated group always returns nullptr (spec Edge cases 1-3).
- ATUAL: No ItemGroup/totalWeight cache exists. The only weighted selection is Engine:selectItemFromRule, which recomputes the weight sum on every call, so the stale-cache semantics are unreproducible. Everything else matches the spec: nil on empty items, nil with NO RNG draw when total <= 0, exactly one inclusive draw in [0, total-1] (MT:int modulo mapping is the documented RNG divergence), cumulative scan with strict roll < cumulative, fallback rule.items[#rule.items].
- FIX: Accept, with a header note: I verified in the C++ source that ItemGroup::selectRandom/recalculateWeights have ZERO call sites (the engine uses DecorationEngine::selectItemFromRule at source/editor/area_decoration.cpp:1281-1303, which also recomputes the sum and is logic-identical to the Lua). Add an ItemGroup port (table with cached totalWeight + recalculateWeights + selectRandom) only if a later phase ports code that actually uses it.

## [undocumented-divergence] missing function — no findRule in main.lua (module exports only getMatchingRules, main.lua:2093-2103)
- ESPERADO: DecorationPreset::findRule(groundId): linear scan over floorRules in current (post-sort) vector order, return the FIRST rule with (rule.enabled && rule.matchesFloor(groundId)), enabled tested first; nullptr if none; cluster rules never returned.
- ATUAL: Not ported at all; only the all-matches getMatchingRules exists.
- FIX: Add `local function findRule(preset, groundId)` with an early-return loop (same predicate as getMatchingRules) and export it; or accept with a documented omission — grep shows findRule has zero call sites anywhere in the C++ tree, so current behavior cannot differ.

## [undocumented-divergence] missing function — no getClusterItemIds equivalent in main.lua
- ESPERADO: FloorRule::getClusterItemIds(): de-duplicated set of ids > 0 collected across clusterTiles (vector order) and tile.itemIds (vector order); C++ returns hash-set order (unspecified); spec porting note says a Lua port should pick first-seen insertion order.
- ATUAL: Not ported.
- FIX: Add a helper that collects ids > 0 with a seen-table preserving first-seen insertion order, and document the deterministic-order choice in the header divergence list; or accept (zero call sites in the C++ tree).

## [undocumented-divergence] missing function — no getClusterTotalItemCount equivalent in main.lua
- ESPERADO: FloorRule::getClusterTotalItemCount(): count += #tile.itemIds over all clusterTiles, counting id==0 slots and duplicates with no filtering (spec Edge case 8).
- ATUAL: Not ported.
- FIX: Add the trivial sum loop (no filtering, to preserve the [as-is] zero/duplicate counting); or accept (zero call sites in the C++ tree).

## [undocumented-divergence] representativeItemId (main.lua:275-287) covers only the ItemEntry variant; no FloorRule-level helper exists
- ESPERADO: Spec also mandates FloorRule::getClusterRepresentativeItemId(): iterate rule.clusterTiles in vector order, then itemIds in vector order, return the FIRST id > 0, else 0. The C++ UI consumes it (source/ui/dialogs/area_decoration_dialog.cpp:2623), so the planned phase 2/3 UI port will need it.
- ATUAL: Only the ItemEntry-level mirror is implemented (returns entry.itemId when not isComposite, else first nonzero id in compositeTiles, else 0 — that part is faithful). Nothing scans rule.clusterTiles.
- FIX: Add `local function clusterRepresentativeItemId(rule)` looping rule.clusterTiles -> ct.itemIds and returning the first id > 0, else 0.

## [note] representativeItemId (main.lua:275-287) vs AreaDecoration export table (main.lua:2093-2103)
- ESPERADO: Ported helpers should be reachable by callers/offline tests (the module exports matchesFloor, getMatchingRules, validatePreset, sortRulesByPriority for exactly that purpose).
- ATUAL: representativeItemId is defined but never called anywhere in the file and is not exported — dead code that offline tests cannot exercise.
- FIX: Add `representativeItemId = representativeItemId` to the AreaDecoration export table (and wire it into the phase 2/3 UI where the C++ dialog uses the rule-level variant).

## [note] matchesFloor (main.lua:222-230), validatePreset (290-380), representativeItemId (275-287)
- ESPERADO: groundId/floorId/from-to/itemIds are uint16_t in C++ (always 0..65535, negatives impossible); spec porting note: 'clamp Lua values to 0..65535 or comparisons like id > 0 and range matching diverge'.
- ATUAL: Clamping exists only at the XML boundary (attrU16, main.lua:1891-1901). Presets built via the exported Defaults constructors, hand-edited storage JSON, or future UI inputs are compared raw — e.g. floorId = -5 passes validate's `floorId == 0` check and then never matches any ground, whereas C++ uint16_t would wrap it to 65531. All currently reachable data flows (XML import + RealMap adapter ids) stay in range, so no observable difference today.
- FIX: Clamp ids (wrap negatives mod 2^32 then mod 65536, like attrU16) when loading presets from storage or in the Defaults constructors; alternatively document that only attrU16-sourced data is supported.

## [note] Defaults.makeCluster (main.lua:142-149)
- ESPERADO: ItemEntry defaults clusterCount=3, clusterRadius=3, clusterMinDistance=2 (spec Constants section).
- ATUAL: makeCluster overwrites the correct defaults set by Defaults.itemEntry with the raw arguments; calling makeCluster(tiles, weight) without the three optional args leaves clusterCount/clusterRadius/clusterMinDistance = nil, which later raises 'attempt to compare nil with number' inside validatePreset (`item.clusterCount <= 0`). The XML importer always passes all three explicitly, so current flows are safe.
- FIX: Use `e.clusterCount = count or 3; e.clusterRadius = radius or 3; e.clusterMinDistance = minDistance or 2` to preserve the struct defaults.

# 01_area-spatial.md (C++ area_decoration.cpp lines 285-545: PreviewState, AreaDefinition, S [divergences-found]

## [bug] Missing entirely — no counterpart anywhere in main.lua (preview.index is built by rebuildPreviewIndex at lines 1468-1493 but has no accessors)
- ESPERADO: Spec lines 15-17 (C++ area_decoration.cpp:304-322): PreviewState::hasItemAt(pos) returns true iff the bucket for the position key exists AND is non-empty; getItemsAt(pos) returns items in bucket order (ascending index) with a defensive stale-index guard (idx < items.size(), i.e. idx <= #items in 1-based Lua), and does NOT verify items[idx].position == pos.
- ATUAL: The Lua builds preview.index (posKey -> array of indices) but exposes no hasItemAt/getItemsAt functions. The index is write-only dead data in the port; no code can query 'what preview items are at position P'.
- FIX: Add accessors on Engine or the preview table: hasItemAt(x,y,z) = (index[posKey] ~= nil and #index[posKey] > 0); getItemsAt(x,y,z) iterating the bucket in order with 'if idx <= #preview.items then out[#out+1] = preview.items[idx] end'. Alternatively add this omission to the file-header documented-divergences list (it is not listed there today).

## [bug] Missing entirely — AreaOps (main.lua 432-517) only implements positions/rectangle/floodFill/selection
- ESPERADO: Spec lines 25-27 (C++ 361-391): AreaDefinition::contains(pos) — inclusive 6-bound test using RAW rectMin/rectMax (not normalized; inverted rect => always false), and ALWAYS false for FloodFill/Selection types; getBounds(outMin,outMax) — Rectangle returns raw fields verbatim, FloodFill returns the square Chebyshev box (origin +/- floodMaxRadius, z fixed to origin z), Selection/other returns (0,0,0)/(0,0,0). Spec explicitly says 'Inconsistent by design; port as-is' and porting notes flag bugs (a)-(c) to 'port all six as-is'.
- ATUAL: No AreaOps.contains and no AreaOps.getBounds exist in the Lua port. All the as-is quirks the spec mandates preserving (raw non-normalized contains/getBounds vs normalized getPositionsRectangle, square-box-around-diamond flood bounds, unconditional false for non-Rectangle contains) are simply absent.
- FIX: Implement AreaOps.contains(area, pos) and AreaOps.getBounds(area) replicating the C++ verbatim including the quirks (no normalization, false for type ~= 0, FloodFill square box with z = floodOrigin.z, (0,0,0) fallback), or list the omission in the header's documented-divergences block.

## [bug] Missing entirely — no PreviewManager analog in main.lua; also Engine:clearPreview (571-579) and runGeneration (1509-1535) lack the attach/detach calls
- ESPERADO: Spec lines 55-62 and constant 0.7 (C++ 521-543): PreviewManager singleton with setActivePreview (raw pointer store), clearActivePreview (detach only, no clear()), hasActivePreview (non-null AND isValid == true — an attached-but-invalid preview counts as no preview), getPreviewItemsAt (gated by hasActivePreview, delegates to getItemsAt), and previewOpacity default 0.7 with UNCLAMPED setter. The C++ engine attaches the preview after a successful real generatePreview (cpp:637) and detaches at the start of clearPreview (cpp:718); the virtual path does not attach.
- ATUAL: No singleton, no attach/detach, no isValid-gated lookup, no 0.7 opacity anywhere in the Lua. The Phase-1 harness applies items directly to the map instead of exposing a render-preview layer, but this omission is NOT in the header's documented-divergences list (which only covers RNG, stable sort, string keys, grid clamps).
- FIX: Either add a small PreviewManager table (active = nil; hasActivePreview() = active ~= nil and active.isValid; getPreviewItemsAt gated on that; opacity = 0.7 unclamped) wired into generatePreview success / clearPreview, or explicitly extend the header divergence list with 'Phase 1 omits the PreviewManager render-integration layer (hasItemAt/getItemsAt/opacity)'. Note: this does not change generation output — in the current C++ these APIs have no callers outside area_decoration.cpp either.

## [undocumented-divergence] AreaOps.floodFill, main.lua lines 500-502 ('if head % 512 == 0 then yield() end'); same pattern in collectTileData (649), friend BFS (711, 768) and generators
- ESPERADO: Spec lines 31-41 (C++ 416-456): getPositionsFloodFill is a single atomic BFS loop with no re-entry points; the map cannot change between dequeues.
- ATUAL: The Lua injects app.yield() every 512 dequeues inside the BFS. If app.yield pumps editor/UI events, the live map can mutate mid-flood (tiles added/removed/reground), producing a result no C++ execution could produce. With a static map the output is bit-identical, but this cooperative-yield divergence is not in the file header's documented list.
- FIX: Add the yield() injections to the header's documented-divergences block (stating map must not mutate during generation), or verify app.yield cannot dispatch map-mutating events; otherwise accept as a pragmatic scripting necessity.

## [note] SpatialHash + SPATIAL_CELL constant, main.lua lines 29, 386-425
- ESPERADO: Spec lines 47 (C++ 477): SpatialHashGrid ctor takes cellSize parameter, default 8; engine always constructs with 8 (cpp:550 'm_spatialHash(8)').
- ATUAL: Cell size is a hardcoded module constant SPATIAL_CELL = 8; SpatialHash.new() takes no parameter. Behaviorally identical for every use the engine makes (always 8). Everything else verified exact: idiv truncation toward zero matches C++ int division for negatives (idiv(-7,8)=0, idiv(-9,8)=-1), cellRadius = trunc(radius/8)+1, dy-outer/dx-inner inclusive scan, inclusive Chebyshev filter, per-cell insertion order, negative radius yields empty via filter, clear() drops cells only. String 'cx:cy' keys are covered by the header's documented string-key divergence (and the spec's porting notes explicitly sanction that choice when documented).
- FIX: Accept — no caller ever uses a non-default cell size; optionally accept an optional cellSize argument in SpatialHash.new for API parity.

## [note] idiv(), main.lua lines 36-42
- ESPERADO: C++ truncating integer division on int operands (exact for the full int range).
- ATUAL: Implemented via float division (a/b) then floor/ceil — exact only for |a| < 2^53. Correct for all realistic map coordinates and radii; both branches return Lua integers so string keys are stable ('0' not '-0').
- FIX: Accept; for purity could use the integer-only form 'local q = a // b; if q < 0 and q * b ~= a then q = q + 1 end return q'.

## [note] Engine:rerollPreview, main.lua lines 1598-1600 (OUT OF SCOPE for this spec section — flagging so it is not lost)
- ESPERADO: C++ rerollPreview (cpp:711-715, belongs to the spec section covering lines 549-745) ALWAYS draws a fresh std::random_device seed and calls generatePreview(newSeed), ignoring preset.defaultSeed.
- ATUAL: Lua rerollPreview calls generatePreview(0); resolveSeed(0) prefers preset.defaultSeed when non-zero, so for presets with a defaultSeed every 'reroll' regenerates the IDENTICAL layout instead of a new random one. Result-changing, but it diverges from a different spec section than 01_area-spatial.md, so it is reported here as a note for the reviewer of that section.
- FIX: In rerollPreview, generate a fresh random seed directly (bypassing the defaultSeed branch of resolveSeed), e.g. seed the same time-based fallback expression and pass it explicitly.

# 02_lifecycle — DecorationEngine ctor/setArea/setPreset/generatePreview/generatePreviewVirt [divergences-found]

## [bug] Engine:rerollPreview, main.lua lines 1598-1600
- ESPERADO: C++ (area_decoration.cpp 711-715): rerollPreview draws fresh OS entropy (std::random_device, nonzero in practice) and passes that newSeed to generatePreview, BYPASSING preset.defaultSeed (spec edge case 4: 'rerollPreview ignores defaultSeed unless random_device returns exactly 0').
- ATUAL: Lua does `return self:generatePreview(0)`. Seed 0 is the 'unspecified' sentinel, so resolveSeed prefers preset.defaultSeed when nonzero — for any preset with defaultSeed != 0, every reroll regenerates the IDENTICAL preview instead of a new random one. (The dialog 'Reroll' button at line 2322 calls generatePreview(0) directly and has the same behavior, though it is harness code.)
- FIX: Extract the entropy expression from resolveSeed into a local entropySeed() helper and implement rerollPreview as `local s = entropySeed(); return self:generatePreview(s)` so a nonzero random seed is passed explicitly, bypassing defaultSeed exactly like the C++.

## [bug] Engine:clearPreview (lines 571-579) and Engine:generatePreview (lines 1537-1564); no PreviewManager anywhere in main.lua
- ESPERADO: Spec clearPreview step 1: PreviewManager::getInstance().clearActivePreview(); generatePreview step 14: setActivePreview(&m_previewState) registers the preview for rendering (generatePreviewVirtual deliberately does NOT register — that asymmetry is part of the contract). PreviewManager API: hasActivePreview() = pointer non-null AND isValid, getPreviewItemsAt(pos) via spatial index, default opacity 0.7. Destructor clears preview so the manager never dangles.
- ATUAL: The Lua port has no PreviewManager equivalent at all: generatePreview never registers the preview, clearPreview never detaches it, and hasActivePreview/getPreviewItemsAt/previewOpacity=0.7 do not exist. The generatePreview-vs-generatePreviewVirtual step-14 distinction is therefore lost. This is a spec'd feature with no corresponding Lua code; it is not in the file-header documented-divergence list (RNG, stable sort, string hash keys, grid clamps). It does not change generated items or applied map state (rendering-only), but it is a missing spec feature.
- FIX: Add a minimal PreviewManager module: `local PreviewManager = { active = nil, previewOpacity = 0.7 }` with setActivePreview/clearActivePreview/hasActivePreview (active ~= nil and active.isValid)/getPreviewItemsAt (lookup preview.index[posKey]); call clearActivePreview() as the first line of Engine:clearPreview and setActivePreview(self.preview) at the end of generatePreview (NOT generatePreviewVirtual). Alternatively, explicitly add 'sem PreviewManager/render de preview (Fase 1)' to the documented-divergence header list.

## [undocumented-divergence] Engine:setArea (lines 560-563) and Engine:setPreset (lines 565-569)
- ESPERADO: C++ copies by value: m_area = area and m_preset = preset are deep copies; sortRulesByPriority() then sorts the engine's PRIVATE copy. Caller-side objects are never mutated, and later caller mutations never leak into the engine (the clearPreview-before-reassign ordering exists precisely to prevent stale references into the old preset).
- ATUAL: Lua stores raw table references (`self.area = area`, `self.preset = preset`) and then `sortRulesByPriority(self.preset)` sorts the CALLER's floorRules array in place. In the bundled harness, setPreset(db.presets[name]) permanently reorders the stored preset's rules (persisted on the next STORE:save), and any external mutation of the preset/area table aliases directly into the engine. Results are unchanged in the shipped workflow (the index-tiebreak sort is idempotent and setPreset is re-called before every generation), but the copy semantics differ from spec and are a latent hazard for any other caller.
- FIX: Deep-copy the argument in both setters (simple recursive table copy of preset/area) before assignment, then sort the engine's private copy — preserving the clearPreview-first ordering.

## [undocumented-divergence] runGeneration distribution switch, main.lua lines 1513-1520
- ESPERADO: C++ (612-622/684-694) switches on DistributionMode with cases PureRandom(0)/Clustered(1)/GridBased(2) and NO default: an out-of-range mode (reachable — the C++ XML loader does an unvalidated `static_cast<DistributionMode>(as_int(0))` at lines 2243/2564, e.g. mode=3 or negative) runs NO distribution generator; only the cluster pass runs.
- ATUAL: Lua maps `mode == 1` to Clustered, `mode == 2` to GridBased, and EVERYTHING ELSE (including 3, -1, etc., reachable via the Lua XML importer's unvalidated attrInt(node, "mode", 0)) to generatePureRandom. For malformed presets the Lua places PureRandom items where the C++ places none from the distribution pass — and also consumes RNG, desynchronizing the cluster pass stream.
- FIX: Match the C++ no-default switch: `if mode == 0 then self:generatePureRandom(tiles) elseif mode == 1 then self:generateClustered(tiles) elseif mode == 2 then self:generateGridBased(tiles) end` (no else branch).

## [undocumented-divergence] Engine:applyPreview (lines 1626-1648) and Engine:removeLastApplied (lines 1682-1716) failure paths
- ESPERADO: Spec applyPreview step 7 / edge case 7: when action->size()==0 ('No changes were applied') the batch and action are destroyed before commit — NOTHING reaches the undo queue. Same for removeLastApplied's 'No items from last apply were found to remove' path (batch/action discarded). Batch types are ACTION_DRAW / ACTION_DELETE_TILES.
- ATUAL: Lua opens and closes app.transaction("Area Decoration" / "Area Decoration (remover)") UNCONDITIONALLY, then checks applied==0 / anyChange afterwards. On the failure paths an (empty) transaction has already been executed; whether that pushes an empty undo step onto the host's undo stack depends on app.transaction semantics — if it does, the user-visible undo history differs from the C++ contract. Final map state is identical either way. (Named transactions replacing ACTION_DRAW/ACTION_DELETE_TILES batch types is an inherent scripting-API substitution.)
- FIX: Pre-check before opening the transaction: in applyPreview, skip the transaction (and fail with 'No changes were applied') if no group's tile exists; in removeLastApplied, probe buckets for at least one existing tile containing a matching ground/item id first. Or verify that the host's app.transaction drops empty transactions and document that assumption next to the [as-is] comments.

## [note] resolveSeed, main.lua lines 1495-1507
- ESPERADO: C++: when seed==0 and defaultSeed==0, seed = random_device() which CAN return 0 (spec edge case 3: mt19937 then seeded with 0, currentSeed/preview.seed = 0).
- ATUAL: Lua entropy fallback `(os.time()*7919 + floor((os.clock()*1000)%65536)) % 2^32` forces `if seed == 0 then seed = 1 end`, so seed 0 can never occur from the entropy path. Affects only the nondeterministic branch (1-in-2^32 in C++); the os.time/os.clock source itself is explicitly sanctioned by the spec's porting notes.
- FIX: Accept — the divergence lives entirely in the non-deterministic path and never affects explicit-seed reproducibility. (Optionally drop the forced-nonzero to mirror C++ exactly.)

## [note] Engine:applyPreview item-add loop, main.lua lines 1633-1640
- ESPERADO: C++ step 6d: Item::Create(itemId) returns null for invalid/unknown ids and the item is skipped silently (does not set addedAny, does not count toward action->size()).
- ATUAL: Lua wraps `tile:addItem(it.itemId)` in pcall and counts success. The skip-on-invalid-id behavior is only equivalent if the host's tile:addItem RAISES for unknown item ids; if it silently no-ops or creates a placeholder, Lua counts it as applied where C++ would skip (which could flip the 'No changes were applied' outcome in the all-invalid edge case).
- FIX: Verify the host tile:addItem contract for invalid ids; if it does not raise, pre-validate the id (e.g. an app-side item-exists check) before counting `applied`.

## [note] Engine.new, main.lua line 551 (`rng = MT.new(0)`)
- ESPERADO: C++ m_rng is a default-constructed std::mt19937 (default seed 5489) until the first generatePreview reseeds it.
- ATUAL: Lua initializes the engine RNG with MT.new(0).
- FIX: Accept — unreachable difference: both generate entry points unconditionally reseed before any draw, and the spec places no contract on the pre-generation RNG state.

# 03_friend-bias (buildFriendDistanceCache, getFriendDistance, applyFriendBias, collectTileD [divergences-found]

## [undocumented-divergence] Engine:applyFriendBias, lines 798-825 (esp. 814, 820-824)
- ESPERADO: All arithmetic in 32-bit float: friendChance/100.0f, proximity = 1.0f/(1.0f+distance), std::pow(float,float), blend base*(1-c)+base*proximity*c. Porting note 3 mandates either rounding intermediates through float32 precision OR accepting the divergence and documenting it.
- ATUAL: All arithmetic is done in Lua doubles, including '^' (double pow). The structure, constants, comparison and early-return order are exact, but low-bit results differ from C++ float32. The file header's documented-divergence list (RNG, stable sort, string hash keys, grid clamps) does NOT mention float precision, so this divergence is undocumented.
- FIX: Add a line to the header divergence list documenting that applyFriendBias (and density math generally) uses doubles instead of C++ float32, noting it is subsumed by the already-documented RNG divergence (per-seed values already differ from the MSVC binary, so only the draw ORDER is preserved and double-vs-float cannot flip a draw count independently of that). Alternatively, emulate float32 by rounding each intermediate (e.g., via string.pack('f', x) round-trip), but documenting is the proportionate fix.

## [note] Engine:validateTilePlacement, lines 857-872
- ESPERADO: C++ signature validateTilePlacement(pos, itemId) where itemId is completely unused.
- ATUAL: Lua drops the itemId parameter entirely (signature is (x, y, z)); all callers pass only coordinates. Behavior is identical since the spec confirms the parameter is dead.
- FIX: accept — behaviorally identical; the [as-is] comment on line 857 already records that itemId is unused.

## [note] collectTileData line 649-651, buildFriendDistanceCache lines 711 and 768-770
- ESPERADO: No yield/coroutine points exist in the C++ loops.
- ATUAL: Lua inserts app.yield() calls (every 1024 positions in collectTileData, per scan row and every 2048 BFS pops in buildFriendDistanceCache). These consume no RNG and mutate no engine state; the UI harness guards reentrancy with a 'busy' flag, so results are unaffected.
- FIX: accept — cooperative-multitasking only; optionally note in the header that yields are inserted but are state-neutral.

## [note] Engine:collectTileData, lines 606-609
- ESPERADO: C++ returns false (outTiles untouched) iff m_editor is null, otherwise true with outTiles filled.
- ATUAL: Lua returns nil iff self.map is nil, otherwise the tiles array. The caller (generatePreview line 1550-1555) treats nil exactly like the C++ false path, producing the same 'Failed to collect tile data from area' flow.
- FIX: accept — equivalent falsy semantics; content and order of the returned array match the spec exactly.

## [note] Engine:checkSpacing, lines 835-837
- ESPERADO: Defensive skip 'if idx >= m_previewState.items.size()' before dereferencing the preview item.
- ATUAL: Implemented as 'local existing = self.preview.items[idx]; if existing ...' — a nil-check. Equivalent because preview.items is a contiguous 1-based array and the spatial hash stores indices assigned at commit time (commitPlacement line 1024), exactly as porting note 11 prescribes.
- FIX: accept — semantically identical defensive guard.

# 04_placement — buildPlacementItems / checkSpacingForPlacement / commitPlacement / selectIt [faithful]

## [note] Engine:validateTilePlacement, b:/Github/rme_redux/scripts/area_decoration/main.lua lines 858-872
- ESPERADO: C++ signature validateTilePlacement(pos, itemId) keeps an itemId parameter that is never used in the body; spec porting note suggests keeping it for call-site fidelity.
- ATUAL: Lua signature is validateTilePlacement(x, y, z) with the dead itemId parameter dropped; callers (lines 894, 934, 1373, 1437) pass only coordinates. Check order and logic (virtualPreview -> true, no map -> false, tile missing -> false, skipBlockedTiles && isBlocking -> false, else true) match the spec exactly.
- FIX: accept — the parameter is provably dead in the C++ (spec line 18); zero behavioral difference. Optionally re-add an unused 4th parameter for signature fidelity.

## [note] Engine:buildPlacementItems, main.lua lines 879-1009 (return convention)
- ESPERADO: C++ returns bool and writes into a caller-owned outItems vector, calling outItems.clear() at entry; on false return the vector may hold partial items that callers must discard.
- ATUAL: Lua returns a freshly allocated array on success and nil on failure, so partial contents are never observable by callers (tryPlaceAt line 1100, generateClusterCentered line 1376, generateClusterRandom line 1440 all test the returned value).
- FIX: accept — explicitly sanctioned by the spec porting note ('either clear at entry identically or always pass a fresh table; never commit a table from a false return'); RNG draws on failed cluster placements are still consumed before the nil return, preserving the draw sequence.

## [note] Engine:commitPlacement, main.lua lines 1028-1031
- ESPERADO: C++ m_previewState.placementsByRule[placementItems.front().sourceRule]++ silently accepts a nullptr sourceRule key (unordered_map keyed on const FloorRule*).
- ATUAL: Lua preview.placementsByRule[rule] = (preview.placementsByRule[rule] or 0) + 1 would raise 'table index is nil' if items[1].rule were nil. Unreachable in practice: every PreviewItem built by buildPlacementItems carries the rule parameter, and all callers (tryPlaceAt, generateClusterCentered, generateClusterRandom) pass a non-nil rule table; only the defensive addBorder nil-guard mirrors the C++ rule==nullptr path, and that path emits no items.
- FIX: accept — unreachable with the current call graph; if extra robustness is wanted, key nil rules under a sentinel (e.g. a module-local NIL_RULE table) instead of skipping the increment, to keep counter semantics identical.

## [note] SpatialHash:insert/queryRadius, main.lua lines 397-425
- ESPERADO: C++ cellHash = (uint64_t)cx << 32 | (uint64_t)cy sign-extends a negative cy so its high bits smear over the cx field, creating cell-key collisions for negative cell coordinates (spec flags this bug as describe-as-is).
- ATUAL: Lua uses string keys 'cx:cy' (with idiv trunc-division correctly matching C++ pos/8 for negatives), so the negative-cell collision is NOT reproduced; for valid editor map coordinates (non-negative x/y) behavior is identical, and queryRadius preserves dy-outer/dx-inner ring order and the always-Chebyshev <= radius inclusion with z ignored.
- FIX: accept — covered by the file-header documented divergence ('Chaves de hash espacial/posicao usam strings...mesma identidade para coordenadas de mapa normais') and the spec's own porting recommendation; map positions cannot be negative in this editor.

# 05_distributions — generatePureRandom / generateClustered / generateGridBased + shared per [divergences-found]

## [bug] Engine:generateClustered, line 1169 (`local minDist = math.huge`), interacting with the falloff at line 1196
- ESPERADO: C++ initializes the per-tile nearest-center distance to std::numeric_limits<float>::max() — a FINITE sentinel (~3.40282e38). With zero centers (clusterCount <= 0) distanceScore stays FLT_MAX, and falloff = expf(((-FLT_MAX) * clusterStrength) * 0.1f). When clusterStrength == 0 this is expf(-0.0f) = 1.0f, so adjustedDensity = rule.density and the rule places at FULL density on every tile. (When clusterStrength > 0 it underflows to 0.0f, per spec edge case line 99.)
- ATUAL: Lua uses math.huge (IEEE +inf) as the sentinel. With zero centers and clusterStrength == 0, -math.huge * 0 = NaN, math.exp(NaN) = NaN, adjustedDensity = NaN, and the gate `float() <= NaN` is always false — ZERO placements. Opposite result from C++ (full-density placement vs nothing). For clusterStrength > 0 both produce falloff 0, so only the clusterStrength == 0 (and negative-strength overflow) paths diverge.
- FIX: Replace `local minDist = math.huge` with a finite FLT_MAX sentinel: `local FLT_MAX = 3.402823466e38` and `local minDist = FLT_MAX`. Then -FLT_MAX * 0 * 0.1 = -0.0 -> exp = 1.0 matching C++, and -FLT_MAX * positiveStrength * 0.1 -> exp underflows to 0 matching C++. (The sort tiebreak is unaffected: FLT_MAX ties fall to the index comparison exactly like math.huge ties.)

## [undocumented-divergence] Engine:tryPlaceAt, line 1087 (`if self.rng:float() <= adjusted then`)
- ESPERADO: C++ density gate is written as `if (densityDist(m_rng) > adjustedDensity) continue;` — a SKIP condition. When adjustedDensity is NaN, `draw > NaN` is false, so C++ does NOT skip and the placement proceeds. (Spec line 24 paraphrases this as 'proceeds when d <= adjustedDensity', which is only equivalent for non-NaN values.)
- ATUAL: Lua inverts the comparison into a PROCEED condition `draw <= adjusted`. For NaN adjusted density, `draw <= NaN` is false, so Lua skips — the exact opposite of C++. NaN adjustedDensity is reachable: density == 0 combined with an infinite falloff (negative clusterStrength over large distance, or the math.huge sentinel from the other finding), or friendChance == 100 with infinite base density (inf * 0 = NaN inside applyFriendBias). Identical behavior for all non-NaN densities, so benign in sane presets.
- FIX: Mirror the C++ skip-condition verbatim: `if not (self.rng:float() > adjusted) then ... end`. This is a no-op for every non-NaN density and restores C++ NaN semantics. Note this divergence is not covered by any of the four header-documented divergences (RNG mappings, stable sort, string keys, grid clamps).

## [note] Engine:generateClustered line 1196, Engine:applyFriendBias lines 820-824 (and all float math)
- ESPERADO: C++ computes falloff/proximity/density with single-precision expf/powf/sqrtf and float accumulation. Spec porting note (line 113) explicitly accepts double-precision divergence at gate boundaries as long as formula order is preserved: falloff = exp(((-dist) * clusterStrength) * 0.1), bias = base*(1-fc) + (base*proximity)*fc.
- ATUAL: Lua evaluates everything in doubles, but preserves the mandated formula order exactly (`-s.d * cfg.clusterStrength * 0.1` is left-to-right ((-d)*strength)*0.1; bias is `baseDensity * (1.0 - fc) + (baseDensity * proximity) * fc`). Tiny gate-boundary differences vs the C++ binary are possible. This float->double divergence is sanctioned by the spec but is NOT listed in the file header's documented-divergence list (which only names RNG, sort stability, string keys, and grid clamps).
- FIX: Accept (spec-sanctioned, exact per-seed parity is already impossible due to the documented RNG divergence); optionally add one line to the header divergence list mentioning double-precision float math.

# 06_cluster-rules (generateClusterCentered, generateClusterRandom, tileMatchesClusterPatter [faithful]

## [note] Engine:generateClusterCentered line 1371-1374 and Engine:generateClusterRandom line 1435-1438 (plus Engine:validateTilePlacement line 858)
- ESPERADO: C++ P6/R4e computes checkId = isCompositeEntry() ? getRepresentativeItemId() : itemId and passes it to validateTilePlacement(basePos, checkId) (area_decoration.cpp:1736, 1891).
- ATUAL: Lua omits the checkId computation entirely; validateTilePlacement's signature drops the itemId parameter (documented [as-is] comment at line 857 notes the param is unused).
- FIX: Accept: the C++ itemId argument is dead code (validateTilePlacement never reads it, spec P6 confirms 'itemId param is unused'), getRepresentativeItemId has no side effects and consumes no RNG, so behavior and draw order are identical.

## [note] Engine:collectClusterCandidates lines 1316-1318 (yield() every 256 tiles); similar yields in collectTileData/floodFill/friend cache/distribution loops
- ESPERADO: C++ candidate scan (1652-1680, 1796-1824) runs synchronously with no event processing between pattern matching and placement.
- ATUAL: Lua calls yield() (app.yield) periodically during candidate collection, introducing a theoretical window where UI event processing could mutate the map between the C2 pattern match and the placement loop.
- FIX: Accept (no engine-state or RNG effect; deterministic output unchanged for a static map). If app.yield dispatches user input rather than only paint events, consider documenting the reentrancy window or snapshotting stack data, but this matches the file's pervasive cooperative-yield pattern.

## [note] posKey (lines 31-33) used by collectClusterCandidates valid-set (line 1293, 1302)
- ESPERADO: C++ posHash masks each coordinate to 16 bits (((x&0xFFFF)<<32)|((y&0xFFFF)<<16)|(z&0xFFFF)), so a negative absPos coordinate (e.g. x=-1 -> 0xFFFF) can alias with coordinate 65535 (spec edge case 14).
- ATUAL: Lua uses exact string keys 'x:y:z'; negative footprint coordinates never alias, they simply fail the C1 containment check.
- FIX: Accept: covered by the documented 'string hash keys' divergence in the header ('mesma identidade para coordenadas de mapa normais'), and spec porting note (d) explicitly sanctions this choice for non-negative map coordinates; in practice C++ aliasing would also require a collected tile at coordinate 65535, which cannot occur on real maps.

## [note] Engine:generateClusterRandom line 1426 (self.rng:float() <= rule.density)
- ESPERADO: C++ compares a float32 draw against float32 rule.density via 'draw > density => skip' (area_decoration.cpp:1872); densities like 0.3 round to ~0.30000001192 in float32.
- ATUAL: Lua compares float64 draw <= float64 density (exact complement of the C++ reject operator, correct direction and strictness), but at float64 precision the acceptance threshold for non-representable densities differs from the C++ float32 threshold by ~1e-8.
- FIX: Accept: subsumed by the documented RNG divergence (draw values already differ from MSVC by construction, only draw ORDER is preserved, and one float() call maps to one densityDist call); spec porting note (e) explicitly deems this negligible and non-bit-identical by design. The port is self-consistent.

# 07_xml-format.md — XML (de)serialization of DecorationPreset + PresetManager, vs scripts/a [divergences-found]

## [bug] rawNumber (main.lua lines 1858-1872), used by attrFloat (1882-1888)
- ESPERADO: as_float = strtod prefix parsing: a junk-suffixed float like density="0.5abc" parses to 0.5, "1.5x" to 1.5 (spec PART 1 VALUE FORMATS: 'float READ (as_float): strtod cast to float').
- ATUAL: When tonumber() fails (junk suffix), rawNumber tries the INTEGER prefix pattern '^%s*([-+]?%d+)' BEFORE the float pattern, so it stops at the decimal point: "0.5abc" -> 0, "1.5x" -> 1. The float fallback pattern on line 1867 is unreachable for any string starting with a digit.
- FIX: In rawNumber, try the float-prefix pattern before the integer-prefix pattern (or use a single strtod-like prefix match '^%s*[-+]?%d*%.?%d*' and validate non-empty digits). Only affects density/cluster_strength with malformed values, but it is exactly the strtol/strtod prefix semantics the helper claims to implement.

## [bug] attrInt (line 1879) and attrU16 (line 1896): math.floor(rawNumber(v))
- ESPERADO: strtol/strtoul stop at the '.' (truncation toward zero): priority="-3.7" -> -3; attrU16 of "-0.5" -> strtoul parses "-0" -> 0.
- ATUAL: math.floor rounds toward negative infinity on the full float value parsed by tonumber: "-3.7" -> -4; attrU16("-0.5") -> floor(-0.5) = -1 -> -1 % 2^32 = 4294967295 -> % 65536 = 65535 instead of 0. Positive fractions coincide (floor(3.7)=3) so only negative fractional values diverge.
- FIX: For int/u16 paths, parse the integer prefix directly (match '^%s*([-+]?%d+)') instead of flooring a float, which reproduces strtol stopping at '.' and truncation toward zero.

## [undocumented-divergence] rawNumber line 1859 (tonumber first), used by attrInt for all integer attributes
- ESPERADO: Spec: 'int READ (as_int): strtol base10' — "0x10" -> 0, "1e3" -> 1; also strtol/pugixml saturate at INT_MAX/LONG_MAX on overflow ("2147483648" -> 2147483647).
- ATUAL: tonumber accepts Lua numeric syntax: "0x10" -> 16, "1e3" -> 1000, "2147483648" -> 2147483648 (no clamp). Caveat: modern pugixml's string_to_integer also accepts a 0x prefix, so the hex case may match the real binary even though it contradicts the spec text; the exponent and no-saturation cases diverge either way.
- FIX: Use the digit-prefix pattern for attrInt instead of raw tonumber (and clamp to int32 range if exact pugixml saturation is desired). Hand-authored-XML-only impact; document if accepted as-is.

## [undocumented-divergence] attrU16 (lines 1891-1901)
- ESPERADO: strtoul SATURATES at ULONG_MAX (2^32-1) on overflow before the uint16 truncation: id="4294967296" -> 4294967295 -> 65535; id="99999999999" -> 65535. (Negative overflow like "-4294967297" also saturates to 65535, which Lua's modulo happens to match.)
- ATUAL: Lua wraps instead of saturating for magnitudes >= 2^32: 4294967296 % 65536 = 0; 99999999999 % 65536 = 59391. The negative-wrap branch (num % 2^32 for num < 0) is correct for the in-range cases the spec documents (-1 -> 65535, -70000 -> 61072).
- FIX: After the sign wrap, clamp: if num >= 4294967296 then num = 4294967295 end, then % 65536. Pathological hand-authored input only; accept-with-documentation is also defensible.

## [undocumented-divergence] presetFromString settings block, line 1977 (default_seed)
- ESPERADO: default_seed read via strtoull base 10 over the FULL uint64 range: "18446744073709551615" -> 18446744073709551615, which the engine consumes mod 2^32 as 4294967295. Spec porting notes explicitly require math.tointeger plus a fallback representation for values above 2^63-1.
- ATUAL: math.floor(rawNumber(s)): decimal strings >= 2^63 overflow Lua's integer lexer and come back as imprecise floats (1.8446744073709552e19); after the engine's % 4294967296 the seed becomes 0 instead of 4294967295 — a different RNG stream than C++ for large seeds. Values <= 2^63-1 are exact and correct.
- FIX: Parse with math.tointeger(tonumber(s)); when nil due to overflow, compute the value mod 2^32 digit-by-digit from the decimal string (only seed % 2^32 is consumed downstream), e.g. acc = (acc*10 + digit) % 2^32.

## [undocumented-divergence] Xml.parse opening-tag branch (lines 1819-1842): xml:find('>', s, true)
- ESPERADO: pugixml accepts a raw '>' inside attribute values on read (legal XML; pugixml only escapes '>' on write): name="A>B" loads as the literal string A>B.
- ATUAL: The tag is cut at the first '>', so name="A>B" truncates the tag mid-attribute; the unterminated-quote attribute fails the gmatch pattern and the preset falls back to "Unnamed Preset", with the residue 'B">' skipped as text. Round-trip-safe for pugixml-written files (which escape '>'), breaks legal hand-edited XML.
- FIX: Replace the plain find('>') with a small quote-aware scan that ignores '>' inside double-quoted attribute values.

## [undocumented-divergence] Xml.parse attribute pattern, line 1832: ([%w_]+)%s*=%s*"([^\"]*)"
- ESPERADO: pugixml accepts both quoting styles for attributes (name='X' is valid XML and parses identically to name="X").
- ATUAL: Only double-quoted attributes match; single-quoted attributes are silently dropped, so every such field falls back to its absent-default instead of its value.
- FIX: Add an alternate match for single-quoted values, e.g. also gmatch "([%w_]+)%s*=%s*'([^']*)'" (only relevant for hand-authored files; pugixml writes double quotes).

## [undocumented-divergence] Xml.parse overall (lines 1796-1845) + presetFromString root check (1947-1951)
- ESPERADO: doc.load_buffer FAILS on malformed XML (truncated documents, mismatched close tags) and fromXmlString returns false; in loadPresets such files are silently skipped, never registered. Empty/garbage strings also return false (spec edge case 15).
- ATUAL: The scanner never reports errors: empty/garbage with no <decoration_preset> correctly yields nil, but a TRUNCATED or tag-mismatched document still produces a partial tree and presetFromString returns a partial preset, which importXmlPresets then stores (possibly overwriting a good preset of the same name). Additionally, close tags pop the stack without name matching, and an element whose name does not start with [%w_] is dropped while its children re-parent and its close tag pops one level early.
- FIX: Track open-tag names on the stack; on a mismatched close tag or a non-empty stack at EOF, return nil from Xml.parse (and propagate nil from presetFromString) to mirror pugixml's parse failure.

## [undocumented-divergence] Whole file: no XML write side and no PresetManager file persistence (spec PART 2: saveToFile, toXmlString, getPresetsDirectory, savePresets, addPreset, removePreset, renamePreset, filename sanitization)
- ESPERADO: Spec mandates an XML serializer (saveToFile with version="1.0", toXmlString without it; tab indent, escaping, float shortest-form) and PresetManager semantics: per-preset <sanitized-name>.xml files under <data>/presets/decoration, the 9-char sanitization set, non-transactional rename, directory auto-creation, map keyed by internal name.
- ATUAL: Phase 1 deliberately persists presets as JSON via app.storage("area_decoration") keyed by preset.name (lines 2114-2161); no serializer, no sanitization, no file-level add/remove/rename exists. The header line 2-3 declares the phase scope, but this replacement is NOT in the four-item documented-divergence list.
- FIX: Either add this storage-model replacement to the documented-divergence header list, or implement Xml.presetToString mirroring the schema in a later phase. The read/import path itself is equivalent (keyed by name attribute, last duplicate wins, failed parses skipped).

## [undocumented-divergence] importXmlPresets (lines 2134-2161) vs PresetManager::loadPresets
- ESPERADO: loadPresets unconditionally clears m_presets (step 1) before enumerating *.xml, so the resulting set is exactly what is on disk.
- ATUAL: importXmlPresets merges into the persisted db.presets without clearing: presets whose XML files were deleted or renamed on disk linger forever in the JSON store, so the preset list can differ from a fresh C++ load. (Case-insensitive '.xml' match via f:lower() and counting failures instead of silent skips are benign refinements.)
- FIX: Rebuild from scratch: start importXmlPresets with a fresh table and assign db.presets = imported set (or document the merge-on-import semantics as intended for the JSON storage model).

## [note] unescape (lines 1778-1793)
- ESPERADO: pugixml decodes numeric character references to UTF-8 byte sequences (&#256; -> 0xC4 0x80) and, with default parse_wconv_attribute, normalizes tab/newline inside attribute values to spaces.
- ATUAL: string.char(n % 256) wraps codepoints above 255 to a single wrong byte, and attribute whitespace is kept verbatim. pugixml only ever WRITES numeric refs for control chars < 32, so round-trips of editor-written files are unaffected; only hand-authored references >255 diverge.
- FIX: Accept (round-trip safe), or encode codepoints > 127 as UTF-8 via utf8.char and add a gsub of [\t\r\n] -> ' ' on attribute values for full pugixml fidelity.

## [note] Xml.parse attribute loop (line 1832-1834): node.attrs[k] = unescape(v)
- ESPERADO: pugixml keeps duplicate attributes and attribute(name) returns the FIRST occurrence (it does not reject duplicates by default).
- ATUAL: gmatch overwrites, so the LAST duplicate wins: <rule density="0.5" density="0.9"> reads 0.9 in Lua vs 0.5 in C++. Hand-authored-only (pugixml never writes duplicates).
- FIX: Only assign when attrs[k] == nil to mirror first-wins, or accept as pathological-input-only.

## [note] Xml.presetFromString API shape (lines 1946-2087)
- ESPERADO: C++ loadFromFile/fromXmlString MUTATE an existing DecorationPreset: missing <spacing>/<distribution>/<settings>/<area> leave previously-held (stale) values; hasArea/floorRules/name always reset (spec edge case 1).
- ATUAL: The Lua always constructs a fresh Defaults.preset(), so the stale-value semantics is unreachable; for fresh objects the behavior is identical because every Defaults field matches the struct defaults the spec lists (spacing {1,2,true}, distribution {0,0.5,3,3,3,1}, maxItemsTotal -1, skipBlocked true, seed 0, floodMaxRadius 100), and hasArea is still explicitly reset at line 1980.
- FIX: Accept: the only Lua call site (importer) always wants fresh-object semantics, and the reuse-a-stale-object quirk is a C++ API hazard, not a format behavior.

