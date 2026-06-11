# area_decoration.cpp lines 40-285: ItemGroup::recalculateWeights/selectRandom; FloorRule::matchesFloor/getClusterBounds/getClusterItemIds/getClusterTotalItemCount/getClusterRepresentativeItemId; DecorationPreset::sortRulesByPriority/findRule/getMatchingRules/validate (file: b:/Github/rme_redux/source/editor/area_decoration.cpp; structs in b:/Github/rme_redux/source/editor/area_decoration.h)

## Summary
Weighted random item selection over a cached totalWeight using one uniform_int draw with cumulative-sum scan and last-item fallback; floor-rule matching (Cluster never matches, FloorRange is inclusive, SingleFloor is exact equality); cluster geometry helpers (componentwise 3D min/max bounds, unordered de-dup of item IDs, raw count including zeros, first-nonzero representative ID); preset rule ordering via UNSTABLE std::sort descending by priority; first-match findRule and all-matches getMatchingRules over enabled rules in vector order; and a strictly ordered, fail-fast validate() with 16 distinct error messages that also validates disabled rules.

## Semantics
TYPES (from area_decoration.h):
- Position: plain `int x, y, z`; default ctor = (0,0,0).
- ItemEntry fields/defaults: itemId:uint16_t=0, weight:int=100, isComposite:bool=false, compositeTiles:vector<CompositeTile>, isCluster:bool=false, clusterCount:int=3, clusterRadius:int=3, clusterMinDistance:int=2, hasCenterPoint:bool=false, centerOffset:Position(0,0,0). isCompositeEntry()==isComposite; isClusterEntry()==isCluster.
- CompositeTile: offset:Position, itemIds:vector<uint16_t>.
- ItemGroup: name:string, items:vector<ItemEntry>, totalWeight:int=0 (CACHED, not auto-maintained).
- FloorRule fields/defaults: name; floorId=0, fromFloorId=0, toFloorId=0 (all uint16_t); items:vector<ItemEntry>; borderItemId=0; friendFloorId=0, friendFromFloorId=0, friendToFloorId=0 (uint16_t); friendChance:int=0; friendStrength:int=0; maxPlacements:int=-1; density:float=1.0f; priority:int=0; enabled:bool=true; ruleMode:RuleMode=SingleFloor (enum {SingleFloor, FloorRange, Cluster}); clusterTiles:vector<CompositeTile>; hasCenterPoint=false; centerOffset:Position(0,0,0); instanceCount:int=1; instanceMinDistance:int=5; requireGround:bool=true. Inline helpers used below: isRangeRule()==(ruleMode==FloorRange); isClusterRule()==(ruleMode==Cluster); isFriendRange()==(friendFromFloorId>0 && friendToFloorId>0).
- DecorationPreset: name, floorRules:vector<FloorRule>, spacing:SpacingConfig{minDistance=1,minSameItemDistance=2,checkDiagonals=true}, distribution, maxItemsTotal=-1, skipBlockedTiles=true, defaultSeed=0, area, hasArea=false.

=== ItemGroup::recalculateWeights() (lines 47-52) ===
1. totalWeight = 0.
2. For each item in items, IN VECTOR ORDER: totalWeight += item.weight (plain signed int addition; no clamping, no skipping of zero/negative weights).
Side effect: mutates only this->totalWeight.

=== const ItemEntry* ItemGroup::selectRandom(std::mt19937& rng) const (lines 54-69) ===
1. Early exit: if (items.empty() || totalWeight <= 0) return nullptr. NOTE: uses the CACHED totalWeight field; if recalculateWeights() was not called after the items vector changed, the stale value is used.
2. Construct std::uniform_int_distribution<int> dist(0, totalWeight - 1) — INCLUSIVE range [0, totalWeight-1]. Draw exactly one value: int roll = dist(rng).
3. int cumulative = 0. For each item IN VECTOR ORDER: cumulative += item.weight; if (roll < cumulative) return &item. (Strict less-than. So entry i is selected iff prefixSum(0..i-1) <= roll < prefixSum(0..i); probability = weight_i / totalWeight when all weights positive and cache is fresh.)
4. Fallback after the loop: return &items.back() (pointer to the LAST element). Reachable only if cached totalWeight exceeds the true sum, or if negative weights make the running cumulative dip such that roll is never < cumulative.
No mutation; rng state advances by the draw.

=== bool FloorRule::matchesFloor(uint16_t groundId) const (lines 75-84) ===
Order of checks:
1. if (ruleMode == Cluster) return false; — cluster rules NEVER match any floor (this is why findRule/getMatchingRules silently skip them).
2. if (ruleMode == FloorRange) return groundId >= fromFloorId && groundId <= toFloorId; — both bounds INCLUSIVE, unsigned 16-bit comparison. A range rule with from=to=0 matches only groundId 0.
3. else (SingleFloor): return groundId == floorId. Exact equality; floorId 0 matches groundId 0 (though validate() rejects SingleFloor rules with floorId==0).

=== void FloorRule::getClusterBounds(Position& outMin, Position& outMax) const (lines 86-104) ===
1. If clusterTiles.empty(): outMin = Position(0,0,0); outMax = Position(0,0,0); return.
2. outMin = outMax = clusterTiles[0].offset.
3. For EVERY tile in clusterTiles (including index 0 again, harmless), componentwise: outMin.x = min(outMin.x, tile.offset.x); same for y and z; outMax.x = max(outMax.x, tile.offset.x); same for y and z. Offsets are signed ints (may be negative).
Mutates only the two out parameters.

=== std::vector<uint16_t> FloorRule::getClusterItemIds() const (lines 106-116) ===
1. unordered_set<uint16_t> uniqueIds.
2. For each tile in clusterTiles (vector order), for each id in tile.itemIds (vector order): if (id > 0) insert into the set (de-duplicates; id 0 excluded).
3. Return vector built from set iteration — ORDER IS UNSPECIFIED/implementation-dependent (hash-set order, NOT insertion order, NOT sorted).

=== size_t FloorRule::getClusterTotalItemCount() const (lines 118-124) ===
count = 0; for each tile in clusterTiles: count += tile.itemIds.size(). Counts EVERY slot including itemId==0 entries and duplicates — no filtering whatsoever.

=== uint16_t FloorRule::getClusterRepresentativeItemId() const (lines 126-133) ===
Iterate clusterTiles in vector order; within each tile iterate itemIds in vector order; return the FIRST id > 0 encountered. If none, return 0. (Mirrors ItemEntry::getRepresentativeItemId() in the header, which returns this->itemId when !isComposite, else first nonzero id in compositeTiles, else 0.)

=== void DecorationPreset::sortRulesByPriority() (lines 139-144) ===
std::sort(floorRules.begin(), floorRules.end(), [](a,b){ return a.priority > b.priority; }) — DESCENDING by priority (higher priority first). std::sort is NOT STABLE: relative order of equal-priority rules after sorting is unspecified. Mutates floorRules in place; since findRule/getMatchingRules pick first-match in vector order, this sort determines match precedence.

=== const FloorRule* DecorationPreset::findRule(uint16_t groundId) const (lines 146-153) ===
Linear scan over floorRules in current vector order; return pointer to the FIRST rule where (rule.enabled && rule.matchesFloor(groundId)). Short-circuit: enabled is tested first. Returns nullptr if none. Cluster rules can never be returned (matchesFloor==false). Disabled rules are skipped.

=== void DecorationPreset::getMatchingRules(uint16_t groundId, std::vector<const FloorRule*>& outRules) const (lines 155-162) ===
1. outRules.clear() FIRST (always, even if nothing matches).
2. For each rule in floorRules in vector order: if (rule.enabled && rule.matchesFloor(groundId)) push_back(&rule). Preserves vector (post-sort) order; cluster rules never included.

=== bool DecorationPreset::validate(std::string& errorOut) const (lines 164-279) ===
Fail-fast: the FIRST failing check sets errorOut and returns false immediately. On success returns true WITHOUT touching errorOut (not cleared). EXACT order and EXACT messages:
1. if (floorRules.empty()) -> errorOut = "No floor rules defined"; return false.
2. For each rule in floorRules in vector order (NOTE: disabled rules are validated too — `enabled` is never checked here):
   2a. If rule.isClusterRule():
       - if (rule.clusterTiles.empty()) -> "Cluster rule '" + rule.name + "' has no cluster tiles"
       - hasClusterItems = exists a tile in clusterTiles with !tile.itemIds.empty() (loop with break on first; NOTE: a tile whose itemIds contains only 0s still counts). If !hasClusterItems -> "Cluster rule '" + rule.name + "' has no items in cluster tiles"
       - if (rule.hasCenterPoint): compute cMin,cMax via getClusterBounds; if (centerOffset.x < cMin.x || centerOffset.x > cMax.x || centerOffset.y < cMin.y || centerOffset.y > cMax.y || centerOffset.z < cMin.z || centerOffset.z > cMax.z) -> "Cluster rule '" + rule.name + "' centerOffset is outside cluster bounds"
       - if (rule.instanceCount <= 0) -> "Cluster rule '" + rule.name + "' instanceCount must be > 0"
   2b. else if rule.isRangeRule(): if (rule.fromFloorId > rule.toFloorId) -> "Invalid floor range: fromFloorId > toFloorId" (no rule name in message). A 0..0 range passes.
   2c. else (SingleFloor): if (rule.floorId == 0) -> "Floor rule has no floor ID specified" (no rule name).
   2d. Then for ALL rule modes (including Cluster):
       - if (rule.friendChance < 0 || rule.friendChance > 100) -> "Friend chance must be between 0 and 100"
       - if (rule.friendStrength < 0 || rule.friendStrength > 100) -> "Friend strength must be between 0 and 100"
       - if (rule.isFriendRange() && rule.friendFromFloorId > rule.friendToFloorId) -> "Invalid friend floor range: fromFloorId > toFloorId" (isFriendRange requires BOTH friendFromFloorId>0 AND friendToFloorId>0, so e.g. from=5,to=0 is NOT caught here)
       - if (!rule.isClusterRule() && rule.items.empty()) -> "Floor rule '" + rule.name + "' has no items" (cluster rules are allowed an empty items list)
   2e. Then for each item in rule.items in vector order (this loop ALSO runs for cluster rules if their items vector is non-empty):
       - if (item.weight <= 0) -> "Item weight must be positive"
       - if (item.isCompositeEntry()):
           * if (item.compositeTiles.empty()) -> "Composite entry has no tiles"
           * hasItems = exists tile in compositeTiles with !tile.itemIds.empty() (same only-checks-non-empty caveat). If !hasItems -> "Composite entry has no items"
           * if (item.isClusterEntry()):
               - if (item.clusterCount <= 0) -> "Cluster entry has invalid count"
               - if (item.clusterRadius < 0) -> "Cluster entry has invalid radius"
               - if (item.clusterMinDistance < 0) -> "Cluster entry has invalid spacing"
       - else if (item.itemId == 0) -> "Item entry has invalid item ID"
3. AFTER the rules loop: if (spacing.minDistance < 0) -> "minDistance cannot be negative"
4. return true.

## Constants
- ItemEntry.weight default = 100 -- Default selection weight for an item entry
- ItemEntry.clusterCount default = 3 -- Default per-entry cluster placement count
- ItemEntry.clusterRadius default = 3 -- Default per-entry cluster radius
- ItemEntry.clusterMinDistance default = 2 -- Default per-entry cluster min spacing
- ItemGroup.totalWeight default = 0 -- Cached weight sum; 0 until recalculateWeights() runs, making selectRandom return nullptr
- FloorRule.maxPlacements default = -1 -- -1 = unlimited placements
- FloorRule.density default = 1.0f -- Full placement density
- FloorRule.priority default = 0 -- Sort key for sortRulesByPriority (descending)
- FloorRule.enabled default = true -- Rule participates in findRule/getMatchingRules (but validate ignores this flag)
- FloorRule.ruleMode default = RuleMode::SingleFloor -- Default match mode: exact floorId equality
- FloorRule.instanceCount default = 1 -- Cluster instances; validate requires > 0
- FloorRule.instanceMinDistance default = 5 -- Min distance between cluster instances
- FloorRule.requireGround default = true -- Cluster placement requires ground under all tiles
- friendChance valid range = 0..100 inclusive -- validate rejects values outside; message 'Friend chance must be between 0 and 100'
- friendStrength valid range = 0..100 inclusive -- validate rejects values outside; message 'Friend strength must be between 0 and 100'
- SpacingConfig.minDistance default = 1 -- Min spacing between items; validate requires >= 0
- SpacingConfig.minSameItemDistance default = 2 -- Min spacing between identical item IDs (not validated in this section)
- DecorationPreset.maxItemsTotal default = -1 -- -1 = no global item cap
- selectRandom roll range = [0, totalWeight-1] inclusive -- uniform_int_distribution<int>(0, totalWeight - 1)
- getClusterBounds empty fallback = Position(0,0,0) for both outMin and outMax -- Returned when clusterTiles is empty

## RNG
Exactly ONE RNG draw site in this section: ItemGroup::selectRandom draws a single std::uniform_int_distribution<int> over the INCLUSIVE range [0, totalWeight - 1] from the caller-supplied std::mt19937. The draw decides which ItemEntry is returned via cumulative-prefix-sum scan (entry i selected iff prefix(0..i-1) <= roll < prefix(0..i)). Draw order: one logical draw per selectRandom call; calls are interleaved with the engine's other draws (outside this section), so each call advances the shared mt19937 stream and exact per-seed reproduction requires preserving call order AND the number of raw mt19937 32-bit outputs each draw consumes. CRITICAL: std::uniform_int_distribution's mapping from raw mt19937 output to the target range is IMPLEMENTATION-DEFINED (MSVC STL, libstdc++, and libc++ all differ, and may consume a variable number of raw outputs via rejection). This is a Windows/MSVC build, so byte-faithful per-seed Lua porting must reimplement the MSVC STL algorithm (or the port must accept that sequences match only within its own Lua implementation, not the C++ binary). No RNG is used by recalculateWeights, matchesFloor, the cluster helpers, sortRulesByPriority, findRule, getMatchingRules, or validate.

## Edge cases
1) selectRandom early-exit when items is empty OR cached totalWeight <= 0 -> nullptr (a never-recalculated group has totalWeight 0 and always returns nullptr even with items present). 2) selectRandom trusts the CACHED totalWeight; if stale-high, the dist range exceeds the true sum and the loop can fall through to the &items.back() fallback (last item over-selected); if stale-low, later items become unreachable. 3) Negative/zero weights are not filtered at selection time (validate rejects weight <= 0, but selectRandom is independent of validate); a negative weight makes 'cumulative' decrease, skewing or skipping entries, with the back() fallback as the safety net. 4) Cluster-mode rules return false from matchesFloor unconditionally, so findRule/getMatchingRules never yield them — callers must handle cluster rules through a separate path. 5) FloorRange rule with fromFloorId == toFloorId == 0 passes validate (0 > 0 is false) yet matches only groundId 0; SingleFloor with floorId 0 is rejected by validate but would match groundId 0 if validation were skipped. 6) validate checks DISABLED rules too (enabled flag never consulted), so one bad disabled rule fails the whole preset. 7) hasClusterItems/hasItems checks only test 'itemIds vector non-empty' — a tile whose itemIds are all 0 passes those checks even though getClusterItemIds/getRepresentativeItemId would yield nothing. 8) getClusterTotalItemCount counts id==0 slots and duplicates. 9) Friend-range check: isFriendRange() requires BOTH ends > 0, so a half-set range (e.g. from=5, to=0) silently bypasses the 'Invalid friend floor range' check and hasFriendFloor()/matchesFriendFloor fall back to friendFloorId semantics. 10) getClusterBounds with empty clusterTiles returns (0,0,0)/(0,0,0) — combined with validate's order, the centerOffset bounds check can only run after clusterTiles non-emptiness already passed. 11) On validate success, errorOut is left untouched (any pre-existing content survives). 12) The items-validation loop (2e) runs for cluster rules as well if their items vector is non-empty. 13) Error messages embedding rule.name use straight single quotes and exact casing as listed; range/floor-ID/friend/weight messages contain NO rule name — reproduce verbatim.

## Porting notes
INDEXING: all loops are sequential over vectors; in Lua use ipairs/1..#t. 'items.back()' fallback = items[#items]. clusterTiles[0] (C++) = clusterTiles[1] (Lua) for bounds init. SELECTION MATH: roll is in [0, totalWeight-1]; if Lua uses math.random(1, totalWeight) the equivalent test becomes roll <= cumulative — keep one convention and the strict '<' vs '<=' consistent, or probabilities shift by one unit per boundary. uniform_int_distribution is implementation-defined (MSVC here, may consume variable raw mt19937 outputs); per-seed byte-equality with the C++ exe requires reimplementing MSVC's mapping plus mt19937 itself. UNORDERED CONTAINERS: getClusterItemIds returns hash-set order (nondeterministic across implementations); a Lua port should pick a deterministic order (first-seen insertion is the natural choice) and any caller relying on the C++ order cannot be matched anyway. UNSTABLE SORT: sortRulesByPriority uses std::sort (NOT stable_sort) with a.priority > b.priority; equal-priority order is unspecified in C++. Lua's table.sort is also unstable; for deterministic ties, sort by (priority desc, original index asc) and flag the deliberate divergence — this affects which rule findRule returns first among equal priorities. INTEGER TYPES: weights/totalWeight are 32-bit signed ints (overflow is C++ UB; Lua doubles/64-bit ints will not wrap); groundId/floorId/itemIds are uint16_t — clamp Lua values to 0..65535 or comparisons like 'id > 0' and range matching diverge. density is a float but unused in this section. POSSIBLE BUGS (behavior as-is, replicate but flag): (a) selectRandom uses the cached totalWeight and silently falls back to the last item on mismatch instead of asserting; (b) validate's 'has items' checks for cluster/composite tiles accept tiles containing only itemId 0; (c) validate validates disabled rules; (d) half-set friend ranges (one end 0) bypass the friend-range validation; (e) FloorRange 0..0 passes validate while SingleFloor floorId 0 is rejected — inconsistent treatment of ground 0; (f) getClusterTotalItemCount counts zero/duplicate IDs, inconsistent with getClusterItemIds filtering. ERROR MESSAGES: reproduce the 16 strings verbatim including quoting of rule.name; validation is fail-fast in the exact order given in semantics, and success must NOT clear/modify errorOut.