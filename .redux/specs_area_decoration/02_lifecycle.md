# area_decoration.cpp lines 549-745 (DecorationEngine ctor/dtor, setArea, setPreset, generatePreview, generatePreviewVirtual, rerollPreview, clearPreview, checkClusterCenterSpacing) and lines 1924-2074 (applyPreview, removeLastApplied), plus directly-referenced helpers: DecorationPreset::sortRulesByPriority/validate/getMatchingRules (139-162, 164-~260), FloorRule::matchesFloor (75-84), PreviewState::clear/positionHash/rebuildSpatialIndex (285-343), PreviewManager (521-543)

## Summary
The DecorationEngine orchestration layer: lifecycle (construct with Editor*, spatial hash cell size 8), preset/area setters that defensively clear the preview, the two preview generators (real-map and virtual-grid) that share an identical pipeline — clear state, resolve seed (explicit > preset defaultSeed > random_device), seed a 32-bit-truncated mt19937, validate preset, gather tiles, build friend-distance cache, run EXACTLY ONE distribution generator (PureRandom/Clustered/GridBased), then a SEPARATE sequential pass for enabled cluster rules (centered vs random variant per rule.hasCenterPoint), rebuild spatial index, mark valid. applyPreview groups preview items by position, deep-copies each existing tile, appends Item instances, commits one batch (ACTION_DRAW) with one action through the undo queue, snapshots ALL preview items into m_lastAppliedItems, then clears the preview. removeLastApplied reverses that by counting applied ids per position and erasing topmost matching items (and matching ground) from deep-copied tiles in a ACTION_DELETE_TILES batch. m_previewWasCapped is reset in clearPreview/both generators and set only inside the five distribution/cluster generators when maxItemsTotal is hit.

## Semantics
=== STATE LAYOUT (DecorationEngine members, from area_decoration.h) ===
m_editor (Editor*), m_area (AreaDefinition), m_preset (DecorationPreset), m_previewState (PreviewState), m_spatialHash (SpatialHashGrid), m_lastError (string), m_lastAppliedItems (vector<AppliedItem{position,itemId}>), m_clusterCenters (vector<Position>), m_friendDistanceCache (map friendKey -> map z -> FriendDistanceLayer), m_rng (std::mt19937), m_currentSeed (uint64, init 0), m_virtualPreview (bool, init false), m_previewWasCapped (bool, init false).

=== CONSTRUCTOR / DESTRUCTOR (549-555) ===
DecorationEngine(Editor* editor): m_editor = editor; m_spatialHash constructed with cellSize = 8. Nothing else.
~DecorationEngine(): calls clearPreview() (important: this nulls PreviewManager's raw pointer to m_previewState so the renderer never dangles).

=== setArea (557-560) ===
1. m_area = area (copy).
2. clearPreview().
Does NOT touch m_preset (does not sync m_preset.area / m_preset.hasArea). Does NOT clear m_lastAppliedItems or m_lastError.

=== setPreset (565-569) — ORDER IS LOAD-BEARING ===
1. clearPreview() FIRST. (Source comment: PreviewItem.sourceRule stores raw pointers into m_preset.floorRules; clearing before reassignment prevents dangling pointers.)
2. m_preset = preset (copy).
3. m_preset.sortRulesByPriority(): std::sort (NOT stable) on floorRules with comparator a.priority > b.priority, i.e. DESCENDING priority. Ties have unspecified relative order in C++.
Sorting happens ONLY here — generatePreview assumes rules are already sorted. The cluster-rule pass and getMatchingRules both iterate floorRules in this sorted order.

=== clearPreview (717-725) — exact reset list, in order ===
1. PreviewManager::getInstance().clearActivePreview() → m_activePreview = nullptr.
2. m_previewState.clear() → items cleared, totalItemsPlaced = 0, itemCountById cleared, placementsByRule cleared, seed = 0, isValid = false, errorMessage cleared, spatialIndex cleared, minPos = Position(), maxPos = Position().
3. m_spatialHash.clear().
4. m_clusterCenters.clear().
5. m_friendDistanceCache.clear().
6. m_virtualPreview = false.
7. m_previewWasCapped = false.
NOT reset: m_lastAppliedItems, m_lastError, m_currentSeed, m_area, m_preset, m_rng internal state.

=== generatePreview(uint64 seed = 0) (571-640) — EXACT ORDER ===
1. clearPreview() (full reset above; also detaches the rendering preview).
2. m_virtualPreview = false (redundant); m_previewWasCapped = false (redundant).
3. SEED RESOLUTION: if (seed == 0) { if (m_preset.defaultSeed != 0) seed = m_preset.defaultSeed; else seed = std::random_device{}(); }  — i.e. explicit nonzero arg wins; else preset defaultSeed; else fresh OS entropy (32-bit value widened to uint64).
4. m_currentSeed = seed; m_rng.seed(static_cast<unsigned int>(seed))  ← SEED TRUNCATED TO LOW 32 BITS for the mt19937; m_previewState.seed = seed (FULL 64 bits stored for display/reproduction).
5. VALIDATE: if (!m_preset.validate(err)) { m_lastError = "Invalid preset: " + err; m_previewState.errorMessage = m_lastError; return false; }  (m_previewState.isValid stays false.)
   validate() checks, per rule in order, first failure returns false: (a) floorRules empty → "No floor rules defined"; (b) cluster rule: clusterTiles empty → "Cluster rule '<name>' has no cluster tiles"; no tile has any itemIds → "...has no items in cluster tiles"; hasCenterPoint and centerOffset outside getClusterBounds (component-wise x/y/z, inclusive) → "...centerOffset is outside cluster bounds"; instanceCount <= 0 → "...instanceCount must be > 0"; (c) range rule: fromFloorId > toFloorId → "Invalid floor range: fromFloorId > toFloorId"; (d) else (SingleFloor) floorId == 0 → "Floor rule has no floor ID specified"; (e) friendChance outside [0,100] → "Friend chance must be between 0 and 100"; (f) friendStrength outside [0,100] → "Friend strength must be between 0 and 100"; (g) friend range with from > to → "Invalid friend floor range: fromFloorId > toFloorId"; (h) non-cluster rule with empty items → "Floor rule '<name>' has no items"; (i) per item: weight <= 0 → "Item weight must be positive"; composite with no tiles → "Composite entry has no tiles"; composite with no item ids in any tile → "Composite entry has no items".
6. TILE COLLECTION: collectTileData(tiles) fills vector<pair<Position,uint16 groundId>> from m_area; if it returns false → m_lastError = "Failed to collect tile data from area"; errorMessage = same; return false.
7. if (tiles.empty()) → m_lastError = "No valid tiles found in selected area"; errorMessage = same; return false.
8. m_spatialHash.clear() (redundant after step 1).
9. buildFriendDistanceCache(tiles).
10. DISTRIBUTION SWITCH — exactly ONE runs, on m_preset.distribution.mode: PureRandom → generatePureRandom(tiles); Clustered → generateClustered(tiles); GridBased → generateGridBased(tiles). These consume m_rng and place non-cluster rules only — FloorRule::matchesFloor() returns false unconditionally for RuleMode::Cluster, so getMatchingRules() (which filters rule.enabled && rule.matchesFloor(groundId), preserving sorted order) NEVER yields cluster rules to the distribution pass.
11. CLUSTER PASS (separate, AFTER distribution): for each rule in m_preset.floorRules IN SORTED (priority-desc) ORDER: if (!rule.enabled || !rule.isClusterRule()) continue; if (rule.hasCenterPoint) generateClusterCentered(tiles, rule); else generateClusterRandom(tiles, rule). These share the SAME m_rng stream, so they consume random numbers after the distribution pass — order matters for per-seed determinism. m_clusterCenters (cleared in step 1) accumulates across all cluster rules in this pass.
12. m_previewState.rebuildSpatialIndex(): clears spatialIndex; for i = 0..items.size()-1 insert index i under positionHash(items[i].position); if items non-empty, recompute minPos/maxPos as component-wise min/max over all item positions starting from items[0] (if empty, min/max stay default Position()).
13. m_previewState.isValid = true (UNCONDITIONALLY — even if zero items were placed).
14. PreviewManager::getInstance().setActivePreview(&m_previewState) (registers preview for rendering).
15. return true.
m_previewWasCapped at this point is whatever the generators set (see below); it is exposed via wasPreviewCapped().

=== CAPPING LOGIC / m_previewWasCapped (set sites are in the generators, lines 1316, 1329, 1413, 1429, 1518, 1542, 1696, 1841) ===
Pattern in every generator: the cap condition is (m_preset.maxItemsTotal >= 0 && m_previewState.totalItemsPlaced >= m_preset.maxItemsTotal). maxItemsTotal = -1 means unlimited (condition never true). The check appears (a) at the TOP of each candidate-position loop → sets m_previewWasCapped = true then break (PureRandom 1316, Clustered 1413, cluster-centered 1696, cluster-random 1841) or return (GridBased 1518), and (b) INSIDE the per-matching-rule inner loop → sets m_previewWasCapped = true then return from the whole generator (1329, 1429, 1542). Consequence: maxItemsTotal == 0 caps immediately (m_previewWasCapped = true, zero items). The flag is monotonic within one generation; reset only by clearPreview/generatePreview/generatePreviewVirtual entry.

=== generatePreviewVirtual(int width, int height, uint16 groundId, uint64 seed = 0) (642-709) ===
Identical pipeline with these differences, in order:
1. clearPreview(); 2. m_virtualPreview = true; m_previewWasCapped = false; 3. seed resolution IDENTICAL to generatePreview; 4. m_currentSeed/m_rng.seed(truncated)/m_previewState.seed identical; 5. preset validate identical (same error string); 6. PARAMETER CHECK comes AFTER preset validation: if (width <= 0 || height <= 0 || groundId == 0) → m_lastError = "Invalid virtual preview configuration"; errorMessage = same; return false; 7. tiles built synthetically (no map access): reserve width*height; ROW-MAJOR loops — outer for y = 0..height-1, inner for x = 0..width-1, push {Position(x, y, 0), groundId}; z is ALWAYS 0; 8-11. m_spatialHash.clear(), buildFriendDistanceCache(tiles), identical distribution switch, identical cluster-rule pass; 12-13. rebuildSpatialIndex(), isValid = true; 14. DOES NOT call PreviewManager::setActivePreview (virtual preview is never registered for map rendering); 15. return true.
Note: per-seed output of virtual vs real generation matches only if tile lists match, since the tile vector order feeds the generators.

=== rerollPreview (711-715) ===
newSeed = std::random_device{}() (32-bit entropy, widened to uint64); return generatePreview(newSeed). NOTE: rerollPreview bypasses defaultSeed — unless rd() happens to return 0, in which case generatePreview's seed==0 branch kicks in and may reuse defaultSeed.

=== checkClusterCenterSpacing(pos, minDistance) const (727-743) ===
1. if (minDistance <= 0) return true.
2. For each center in m_clusterCenters (insertion order): if (center.z != pos.z) continue; dx = abs(center.x - pos.x); dy = abs(center.y - pos.y); dist = max(dx, dy) (CHEBYSHEV distance); if (dist < minDistance) return false (STRICT less-than: dist == minDistance passes).
3. return true.
Pure read; does not mutate. m_clusterCenters is appended elsewhere (in the cluster generators) and cleared only by clearPreview.

=== positionHash formula (used identically in 3 places: PreviewState::positionHash 298-302, inline in applyPreview 1942-1944, lambda in removeLastApplied 2010-2014) ===
hash = ((uint64)(x & 0xFFFF) << 32) | ((uint64)(y & 0xFFFF) << 16) | (uint64)(z & 0xFFFF). x occupies bits 32-47, y bits 16-31, z bits 0-15. Coordinates wrap mod 65536 — positions differing by multiples of 65536 in x or y collide (irrelevant for normal map sizes).

=== applyPreview (1924-1987) — EXACT ORDER ===
1. if (!m_previewState.isValid || m_previewState.items.empty()) → m_lastError = "No valid preview to apply"; return false.
2. if (!m_editor) → m_lastError = "No editor available"; return false.
3. Map& map = m_editor->map.
4. batch = m_editor->actionQueue->createBatch(ACTION_DRAW); action = m_editor->actionQueue->createAction(batch.get()).
5. GROUP BY POSITION: unordered_map<uint64, vector<const PreviewItem*>> itemsByPos; iterate m_previewState.items in vector order, compute hash (formula above), push pointer — so WITHIN a position, items keep preview placement order; ACROSS positions, the subsequent iteration order is unordered_map order (nondeterministic).
6. For each (hash, group) in itemsByPos:
   a. pos = group[0]->position.
   b. tile = map.getTile(pos); if (!tile) continue → items targeting a nonexistent tile are SILENTLY SKIPPED; no tile is created.
   c. newTile = TileOperations::deepCopy(tile, map); addedAny = false.
   d. For each previewItem in group (preview order): newItem = Item::Create(previewItem->itemId); if (!newItem) continue (invalid/unknown id skipped silently); newTile->addItem(std::move(newItem)); addedAny = true.
   e. if (addedAny) action->addChange(make_unique<Change>(std::move(newTile))) — one Change (whole-tile replacement) per modified position. If nothing was added the deep copy is discarded.
7. if (action->size() == 0) → m_lastError = "No changes were applied"; return false. (batch and action unique_ptrs are simply destroyed; nothing reaches the undo queue; m_previewState and m_lastAppliedItems untouched in this path.)
8. batch->addAndCommitAction(std::move(action)) — committing the action is what mutates the live map (Change objects swap tiles in); then m_editor->addBatch(std::move(batch)) pushes it onto the undo stack as a single undoable unit.
9. SNAPSHOT: m_lastAppliedItems.clear(); reserve(m_previewState.items.size()); for EVERY item in m_previewState.items (in order): push {position, itemId}. NOTE: this includes items that were skipped in step 6b/6d (missing tile or failed Item::Create) — m_lastAppliedItems can overcount what actually landed on the map.
10. clearPreview() (detaches renderer preview, resets all preview state incl. m_virtualPreview and m_previewWasCapped).
11. return true.
NOTE: applyPreview does NOT check m_virtualPreview — a virtual preview (positions 0..w-1/0..h-1 at z=0) can be applied to the real map wherever tiles happen to exist there.

=== removeLastApplied (1989-2074) — EXACT ORDER ===
1. if (!m_editor) → m_lastError = "No editor available"; return false.
2. if (m_lastAppliedItems.empty()) → m_lastError = "No applied items to remove"; return false.
3. BUCKET BUILD: struct RemovalBucket { Position pos; unordered_map<uint16,int> counts; }; unordered_map<uint64, RemovalBucket> buckets, reserve(m_lastAppliedItems.size()). For each applied item (vector order): hash = positionHash(item.position); bucket = buckets[hash] (default-constructed on first touch); bucket.pos = item.position (re-assigned every time, same value); bucket.counts[item.itemId]++ (zero-init then increment).
4. batch = actionQueue->createBatch(ACTION_DELETE_TILES); action = createAction(batch.get()); anyChange = false.
5. For each bucket (unordered iteration over positions):
   a. tile = map.getTile(bucket.pos); if (!tile) continue.
   b. newTile = TileOperations::deepCopy(tile, map); changed = false.
   c. For each (id, count) in bucket.counts (unordered iteration over item ids; count is a local copy decremented below):
      - if (count <= 0) continue (defensive; counts are always >= 1 as built).
      - GROUND CHECK FIRST: if (newTile->ground && newTile->ground->getID() == id && count > 0) { newTile->ground.reset(); count--; changed = true; } — the tile's GROUND is removed if its id matches an applied item id (the `count > 0` re-check is redundant with the earlier guard).
      - THEN ITEM STACK, TOP-DOWN: reverse-iterate newTile->items from rbegin() while it != rend() && count > 0: if (*it && (*it)->getID() == id) { erase that element via baseIt = std::next(it).base(); it = make_reverse_iterator(items.erase(baseIt)); count--; changed = true; } else ++it. Net effect: scan the item vector from LAST index to FIRST, erasing matching ids until `count` removals are done; after an erase, scanning resumes at the element just below the erased one. Removes the TOPMOST `count` occurrences of each id.
   d. if (changed) { action->addChange(make_unique<Change>(std::move(newTile))); anyChange = true; }
6. if (!anyChange) → m_lastError = "No items from last apply were found to remove"; return false. IMPORTANT: m_lastAppliedItems is NOT cleared on this failure path (batch/action discarded).
7. batch->addAndCommitAction(std::move(action)); m_editor->addBatch(std::move(batch)) (single undoable batch).
8. m_lastAppliedItems.clear(); return true.

=== PreviewManager (521-543, referenced by this section) ===
Singleton; setActivePreview stores raw pointer; clearActivePreview nulls it; hasActivePreview() = pointer non-null AND ->isValid; getPreviewItemsAt returns {} if no active preview, else m_activePreview->getItemsAt(pos) (spatial-index lookup, skips stale indices >= items.size()). Default m_previewOpacity = 0.7f.

## Constants
- SpatialHashGrid cell size = 8 -- m_spatialHash constructed with cellSize=8 in DecorationEngine constructor (line 550)
- mt19937 seed truncation = static_cast<unsigned int>(seed) -- only the low 32 bits of the 64-bit seed feed the RNG; full 64 bits stored in m_previewState.seed
- seed sentinel = 0 -- seed==0 means 'unspecified' → fall back to preset.defaultSeed (if nonzero) else std::random_device
- maxItemsTotal unlimited sentinel = -1 -- cap check is (maxItemsTotal >= 0 && totalItemsPlaced >= maxItemsTotal); -1 disables; 0 caps immediately and sets m_previewWasCapped
- positionHash = ((x & 0xFFFF) << 32) | ((y & 0xFFFF) << 16) | (z & 0xFFFF) -- 64-bit position key used in PreviewState index, applyPreview grouping, and removeLastApplied buckets; coords wrap mod 65536
- ACTION_DRAW = action-queue batch type -- undo batch type for applyPreview
- ACTION_DELETE_TILES = action-queue batch type -- undo batch type for removeLastApplied
- rule sort comparator = a.priority > b.priority -- floorRules sorted DESCENDING by priority via std::sort (unstable) in setPreset only
- cluster spacing metric = max(|dx|,|dy|), strict < -- checkClusterCenterSpacing uses Chebyshev distance, same-z only; fails when dist < minDistance, passes at exactly minDistance; minDistance <= 0 always passes
- PreviewManager default opacity = 0.7f -- m_previewOpacity initial value (header)
- error strings = "Invalid preset: "+err / "Failed to collect tile data from area" / "No valid tiles found in selected area" / "Invalid virtual preview configuration" / "No valid preview to apply" / "No editor available" / "No changes were applied" / "No applied items to remove" / "No items from last apply were found to remove" -- exact m_lastError texts; the first three are also copied to m_previewState.errorMessage

## RNG
RNG engine: std::mt19937 (standard 32-bit Mersenne Twister, MT19937 parameters), member m_rng, seeded once per generation with the LOW 32 BITS of the resolved 64-bit seed (m_rng.seed(static_cast<unsigned int>(seed))). Two seeds equal mod 2^32 produce identical previews while reporting different m_previewState.seed values.

Draws within this section itself: NONE — lines 549-745 and 1924-2074 contain no direct m_rng calls. std::random_device (OS entropy, nondeterministic, returns 32-bit unsigned) is invoked in exactly two situations: (1) inside generatePreview/generatePreviewVirtual seed resolution when seed==0 AND preset.defaultSeed==0 (one draw); (2) rerollPreview makes one draw to produce newSeed and forwards it to generatePreview. random_device draws are not part of the deterministic stream — they only pick the seed.

Draw ORDER that this section dictates (critical for per-seed determinism): after seeding, the SINGLE shared m_rng stream is consumed strictly in this sequence: (1) buildFriendDistanceCache (consumes nothing — BFS only, but verify in its own section), (2) exactly one of generatePureRandom / generateClustered / generateGridBased depending on distribution.mode, (3) then for each enabled cluster rule, in floorRules order AFTER the priority-descending sort, generateClusterCentered (if hasCenterPoint) or generateClusterRandom. A Lua port must reproduce mt19937 bit-exactly (including std::shuffle/std::uniform_int_distribution/std::uniform_real_distribution semantics used inside the generators — those are libstdc++/MSVC-implementation-defined; see their sections) AND must replicate this exact call sequence, including the unstable sort's tie order for equal-priority rules, or the stream desynchronizes. applyPreview, removeLastApplied, clearPreview, checkClusterCenterSpacing consume no RNG. clearPreview does NOT reseed or reset m_rng.

## Edge cases
1. Zero items placed is still SUCCESS: generatePreview sets isValid=true and returns true even with empty items; applyPreview later refuses ("No valid preview to apply") because of the items.empty() check. 2. maxItemsTotal==0: every generator caps on its first loop iteration → m_previewWasCapped=true, zero items, generation still returns true. 3. seed==0 passed explicitly is indistinguishable from 'no seed'; actual seed 0 only occurs if random_device returns 0 (then mt19937 is seeded with 0). 4. rerollPreview ignores defaultSeed unless random_device returns exactly 0. 5. generatePreviewVirtual validates the PRESET before width/height/groundId — error precedence matters for reproducing m_lastError. 6. generatePreviewVirtual never registers with PreviewManager; generatePreview does (step 14). 7. applyPreview skips positions with no existing Tile and item ids for which Item::Create returns null — both silently; if ALL placements skip, action->size()==0 → "No changes were applied", return false with NOTHING pushed to undo and m_lastAppliedItems/preview state untouched. 8. m_lastAppliedItems snapshots EVERY preview item including the skipped ones (overcount); hasLastApplied()/getLastAppliedCount() reflect that. 9. removeLastApplied failure path "No items from last apply were found to remove" does NOT clear m_lastAppliedItems (retry possible); success path clears it. 10. removeLastApplied can DELETE THE TILE'S GROUND if an applied item id equals the ground id — ground is checked before the item stack and consumes one count. 11. Item removal is top-of-stack-first (reverse vector scan), capped at the applied count per id per position; pre-existing identical items lower in the stack survive only if enough higher copies absorb the count — i.e. the TOPMOST N matching items are removed regardless of which were 'ours'. 12. Cluster rules never run in the distribution pass (matchesFloor returns false for Cluster mode), only in the dedicated pass; disabled cluster rules are skipped there. 13. checkClusterCenterSpacing only compares same-z centers; dist == minDistance is allowed. 14. clearPreview leaves m_lastError stale from previous failures. 15. Destructor clears the preview so PreviewManager never holds a dangling pointer. 16. setArea does not propagate to m_preset.area/hasArea. 17. applyPreview does not check m_virtualPreview — a virtual preview can be applied to real-map coordinates (0..w-1, 0..h-1, z=0) if tiles exist there.

## Porting notes
INDEXING / ITERATION: virtual tile grid is 0-based, row-major (y outer, x inner), z always 0 — keep 0-based coordinates in Lua even though Lua arrays are 1-based; pair.second[0] in applyPreview is the FIRST group element (Lua index 1). The floorRules cluster pass iterates the SORTED array in order — preserve that order in Lua tables (ipairs over an array, never pairs over a hash).

SORT STABILITY: std::sort is unstable; ties in priority have unspecified order in C++ but a fixed order per binary build. Lua's table.sort is also unstable. For deterministic ports, sort by (priority desc, original index asc) and document that equal-priority rule order may differ from the C++ binary — this changes RNG consumption order and thus per-seed output when priorities tie.

RNG FIDELITY: must implement mt19937 exactly (624-word state, standard tempering) and seed with seed % 2^32 (Lua 5.3: seed & 0xFFFFFFFF). The 64-bit displayed seed and the 32-bit effective seed differ — store both. random_device should map to a non-deterministic source (os.time/os.clock mix or equivalent); it never affects determinism given a seed.

INTEGER/64-BIT: positionHash needs true 64-bit integers (Lua 5.3+ ok; LuaJIT needs ffi uint64 or use a string key "x:y:z" — string keys are SAFE here because all three hash usages are local lookups/grouping, never persisted). The & 0xFFFF masks mean coords wrap mod 65536.

UNORDERED CONTAINER ORDER: applyPreview iterates itemsByPos and removeLastApplied iterates buckets/counts in unordered_map order — nondeterministic in C++. The FINAL MAP STATE is order-independent (per-position groups are disjoint; per-id removal counts are independent), so Lua may use any deterministic order; only the ordering of Change records inside the undo action differs, which is invisible to users. Within one position, preview-order of items IS preserved when adding (affects item stacking order on the tile — preserve it).

REVERSE-ERASE IDIOM: the rbegin/rend erase dance in removeLastApplied is equivalent to: for i = #items, 1, -1 do if count > 0 and items[i].id == id then table.remove(items, i); count = count - 1 end end. Note ground is checked BEFORE the stack and consumes one count if it matches.

POINTER SEMANTICS: PreviewItem.sourceRule is a raw pointer into m_preset.floorRules (hence clearPreview-before-reassign in setPreset, and placementsByRule keyed by rule pointer). In Lua use rule table references or rule indices; replicate the 'clear preview before replacing preset' ordering anyway so stale references never leak into a live preview.

SUSPECTED BUGS / AS-IS BEHAVIOR TO FLAG: (1) m_lastAppliedItems records preview items that were never actually placed (null tile / failed Item::Create) — removeLastApplied tolerates this (counts simply find nothing) but getLastAppliedCount overcounts; reproduce as-is or document divergence. (2) removeLastApplied can remove a tile's GROUND when a decoration item id coincides with the ground id, and removes the TOPMOST matching items even if they pre-existed the apply — both as-is. (3) applyPreview doesn't guard against m_virtualPreview==true, so a virtual preview is appliable to the real map at translated-to-origin coordinates. (4) y in positionHash is shifted to bits 16-31 while x sits at 32-47 — fine, but any port copying the formula must keep the masks or large coords collide differently. (5) The redundant `count > 0` inside the ground-removal condition and the redundant m_virtualPreview/m_previewWasCapped resets after clearPreview are harmless — no need to replicate, but listed for completeness.

FLOAT: no float math in this section except none — density/clusterStrength are consumed in the generators (other sections); nothing here compares floats.