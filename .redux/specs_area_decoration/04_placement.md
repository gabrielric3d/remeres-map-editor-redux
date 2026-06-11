# area_decoration.cpp lines 1110-1305: DecorationEngine::buildPlacementItems, checkSpacingForPlacement, commitPlacement, selectItemFromRule (plus their direct dependencies validateTilePlacement @1094-1108, checkSpacing @1059-1092, SpatialHashGrid::insert/queryRadius @479-511)

## Summary
buildPlacementItems converts one selected ItemEntry at a base map position into a flat list of PreviewItems (decoration items first, then optional border items), handling three shapes: simple single item, multi-tile composite (origin + per-tile offset), and per-entry cluster (the composite stamped at the base position plus up to clusterCount-1 RNG-scattered copies within clusterRadius, each copy at least clusterMinDistance Chebyshev apart, 20 retries per copy, hard-stop on first failed copy slot). Any composite tile landing on a map tile that already has stacked items, or that fails the blocking-tile validation, aborts the ENTIRE entry (return false); tiles missing ground (when rule->requireGround) or with all-zero item ids are silently skipped. checkSpacingForPlacement validates every pending item (including border items) against ALREADY-COMMITTED preview items via the spatial hash using minDistance/minSameItemDistance with Chebyshev (checkDiagonals=true) or Manhattan distance, same-z only. commitPlacement appends each item to previewState.items, inserts its position+index into the 8-cell spatial hash, increments totalItemsPlaced and itemCountById per item, then increments placementsByRule once per commit keyed on the FIRST item's sourceRule. selectItemFromRule does one weighted roll: uniform_int in [0, sum(weights)-1], walks entries in order accumulating weight, returns first entry with roll < cumulative, fallback last entry. Entry-level hasCenterPoint/centerOffset is NOT read inside buildPlacementItems — callers (generateClusterCentered ~line 1727, generateClusterRandom ~line 1882) pre-subtract centerOffset (x, y AND z) from the target position to form basePos.

## Semantics
FILES: b:/Github/rme_redux/source/editor/area_decoration.cpp (target), b:/Github/rme_redux/source/editor/area_decoration.h (structs), b:/Github/rme_redux/source/map/position.h (Position: three plain ints x,y,z; operator+ is component-wise addition of all three including z; operator- likewise).

=====================================================================
validateTilePlacement(pos, itemId) -> bool  [lines 1094-1108, dependency]
=====================================================================
Order of checks:
1. if m_virtualPreview == true -> return true (everything valid in virtual mode).
2. if m_editor == nullptr -> return false.
3. tile = m_editor->map.getTile(pos); if tile == nullptr -> return false (no tile object = invalid; note: a tile with no ground but with a Tile object passes this check).
4. if m_preset.skipBlockedTiles && tile->isBlocking() -> return false.
5. return true.
IMPORTANT: the itemId parameter is NEVER used in the body. It only affects nothing; callers still compute a "representative id" to pass in.

=====================================================================
buildPlacementItems(basePos, entry, rule, outItems) -> bool  [lines 1110-1258]
=====================================================================
Side effect FIRST: outItems.clear() unconditionally at entry (line 1112). On a false return, outItems may contain PARTIAL items (e.g. items from earlier cluster copies appended before a later copy failed) — callers must discard outItems whenever the function returns false. (All current callers do: they `continue` and the vector is re-cleared on the next call.)

Local lambda addBorderItem(pos): if rule != nullptr AND rule->borderItemId > 0, push PreviewItem{position=pos, itemId=rule->borderItemId, sourceRule=rule} onto outItems. Border items are always appended AFTER the decoration items they cover (list order = stacking order on the map).

--- BRANCH A: simple item (entry.isComposite == false), lines 1125-1139 ---
1. if entry.itemId == 0 -> return false.
2. if !validateTilePlacement(basePos, entry.itemId) -> return false.
3. push PreviewItem{basePos, entry.itemId, rule}.
4. addBorderItem(basePos) (so outItems = [item] or [item, border]).
5. return true.
Note: NO ground-existence check and NO "tile already has items" check for simple items — only validateTilePlacement (tile exists + not blocking). This is asymmetric with composites.

--- Composite preamble, line 1141 ---
if entry.compositeTiles.empty() -> return false.
Declare local placedPositions : vector<Position> (positions where >=1 decoration item was actually pushed; used later for border items).

--- Local lambda appendCompositeAt(origin) -> bool, lines 1146-1199 ---
Iterates entry.compositeTiles IN VECTOR ORDER. For each CompositeTile `tile`:
  a. if tile.itemIds.empty() -> continue (skip tile, not a failure).
  b. pos = origin + tile.offset  — Position::operator+ adds x, y, AND z component-wise. Composite offsets are therefore relative map deltas from `origin`: a tile with offset (2,-1,0) lands at (origin.x+2, origin.y-1, origin.z). Negative offsets are legal. z offsets are honored too.
  c. requireGround skip: if (rule != nullptr && rule->requireGround && !m_virtualPreview && m_editor != nullptr): mapTile = map.getTile(pos); if (mapTile == nullptr || mapTile->ground == nullptr) -> continue (skip THIS tile only; the rest of the composite still places). Note rule->requireGround defaults to true in FloorRule.
  d. occupied-tile HARD REJECT: if (!m_virtualPreview && m_editor != nullptr): mapTile = map.getTile(pos); if (mapTile != nullptr && !mapTile->items.empty()) -> RETURN FALSE from the lambda → the whole composite copy fails → buildPlacementItems returns false for the whole entry. (Comment in code: only overwrite bare ground, never existing borders/decorations.) Order matters: step (c) runs before (d), so a groundless tile is skipped at (c) and never triggers (d) when requireGround is on; with requireGround OFF, a groundless tile that has stacked items WILL hard-reject at (d).
  e. validateId = first id > 0 scanning tile.itemIds in order; if all ids are 0 (validateId == 0) -> continue (skip tile).
  f. if !validateTilePlacement(pos, validateId) -> RETURN FALSE (whole composite fails; e.g. missing Tile object, or blocking tile with skipBlockedTiles). In virtual preview this always passes.
  g. for each id in tile.itemIds in order: if id == 0 -> skip id; else push PreviewItem{pos, id, rule} and set addedAny=true. (Multiple ids per composite tile stack in listed order.)
  h. if addedAny -> placedPositions.push_back(pos).
Returns true after iterating all composite tiles.

--- BRANCH B: non-cluster composite (entry.isComposite && !entry.isCluster), lines 1201-1208 ---
1. if !appendCompositeAt(basePos) -> return false.
2. for each pos in placedPositions in order -> addBorderItem(pos). (ALL borders appended after ALL decoration items.)
3. return !outItems.empty(). (If every composite tile was skipped — no ground / all-zero ids — appendCompositeAt returned true but nothing was pushed, so this returns false. Border items are tied to placedPositions, so an empty outItems implies no borders either.)

--- BRANCH C: per-entry cluster (entry.isComposite && entry.isCluster), lines 1210-1257 ---
1. count   = max(1, entry.clusterCount)        [defaults: clusterCount=3]
   radius  = max(0, entry.clusterRadius)       [default 3]
   minDist = max(0, entry.clusterMinDistance)  [default 2]
2. centers : vector<Position>; reserve(count); first center is ALWAYS Position(0,0,0) pushed unconditionally with NO RNG draw and NO spacing test.
3. offsetDist = std::uniform_int_distribution<int>(-radius, radius)  (a single distribution object reused for both axes).
4. for i = 1; i < count; ++i:
     placed = false
     for attempt = 0; attempt < 20; ++attempt:        // exactly 20 tries max per extra center
        dx = offsetDist(m_rng)   // RNG draw 1
        dy = offsetDist(m_rng)   // RNG draw 2 (dx ALWAYS drawn before dy)
        if dx == 0 && dy == 0 -> continue  (attempt consumed, both draws consumed)
        tooClose = false
        for each existing center in `centers` IN INSERTION ORDER:
            dist = max(abs(center.x - dx), abs(center.y - dy))   // Chebyshev between OFFSETS (not world coords)
            if dist < minDist -> tooClose = true; break
        if tooClose -> continue (next attempt)
        centers.push_back(Position(dx, dy, 0)); placed = true; break
     if !placed -> break OUT OF THE i-LOOP ENTIRELY (no further centers attempted at all; comment: "If we can't place more centers, stop early"). The cluster proceeds with however many centers were placed (at least the (0,0,0) one).
   Consequences: if radius == 0 and count > 1, every attempt draws (0,0), all 20 attempts are exhausted, and the loop breaks after i=1 — result: single copy at basePos, 40 RNG draws consumed (subject to distribution implementation, see rngUsage). If minDist == 0, duplicate non-zero offsets are allowed (dist 0 < 0 is false), so two copies can land on the exact same offset and double-stack items.
5. for each center in centers IN ORDER (starting with (0,0,0)):
     if !appendCompositeAt(basePos + center) -> return false   // basePos+center adds z too, but center.z is always 0
   So ANY copy hitting the occupied-tile reject (d) or failed validateTilePlacement (f) kills the ENTIRE entry, including already-appended earlier copies (outItems left partially filled; callers discard).
6. for each pos in placedPositions in order -> addBorderItem(pos).
7. return !outItems.empty().

ENTRY-LEVEL hasCenterPoint/centerOffset: NOT read anywhere inside buildPlacementItems. The mapping "composite offsets -> map positions" is purely pos = basePos(+clusterCenter) + tile.offset. Callers implement centering BEFORE calling: generateClusterCentered (~lines 1727-1731) and generateClusterRandom (~lines 1882-1886) do, when (selected->isCompositeEntry() && selected->hasCenterPoint): basePos.x -= centerOffset.x; basePos.y -= centerOffset.y; basePos.z -= centerOffset.z; so the composite's designated center tile lands exactly on the chosen target position (basePos + centerOffset == target). For simple (non-composite) entries and for composites without hasCenterPoint, basePos is the target position itself (the (0,0) offset tile lands on the target).

=====================================================================
checkSpacingForPlacement(placementItems) -> bool  [lines 1260-1267]
=====================================================================
for each item in placementItems IN ORDER: if !checkSpacing(item.position, item.itemId) -> return false. return true.
Border items ARE included (they are in the vector), so a border id matching a previously committed item id within minSameItemDistance can veto the placement. Items within the SAME pending placement are NOT checked against each other (checkSpacing only consults committed state).

checkSpacing(pos, itemId) [lines 1059-1092, dependency]:
1. spacing = m_preset.spacing (defaults: minDistance=1, minSameItemDistance=2, checkDiagonals=true).
2. maxRadius = max(spacing.minDistance, spacing.minSameItemDistance).
3. nearby = m_spatialHash.queryRadius(pos, maxRadius)  — returns indices into m_previewState.items of COMMITTED items within Chebyshev distance <= maxRadius in x/y (z ignored by the grid).
4. for each idx in nearby (grid iteration order; order does not matter — pure AND of rejections):
   - if idx >= m_previewState.items.size() -> continue (stale-index guard).
   - existing = m_previewState.items[idx].
   - if existing.position.z != pos.z -> continue (spacing is per-floor).
   - dx = abs(pos.x - existing.position.x); dy = abs(pos.y - existing.position.y).
   - distance = checkDiagonals ? max(dx, dy) : (dx + dy)   (Chebyshev vs Manhattan).
   - if distance < spacing.minDistance -> return false. (distance 0 = same tile fails whenever minDistance >= 1.)
   - if itemId == existing.itemId && distance < spacing.minSameItemDistance -> return false.
5. return true.

SpatialHashGrid::queryRadius(center, radius) [lines 486-511]: cellRadius = (radius / m_cellSize) + 1 (integer division); cx = center.x / m_cellSize; cy = center.y / m_cellSize (C++ truncating division — rounds toward zero for negatives); double loop dy outer then dx inner, each from -cellRadius..+cellRadius inclusive; cellHash = (uint64_t)(cx+dx) << 32 | (uint64_t)(cy+dy); for every (Position, index) pair stored in that cell: include index if max(abs(entry.x - center.x), abs(entry.y - center.y)) <= radius — ALWAYS Chebyshev with <=, regardless of checkDiagonals, and z is ignored, so the result is a superset that checkSpacing then filters.

=====================================================================
commitPlacement(placementItems)  [lines 1269-1279]
=====================================================================
for each item IN ORDER:
  1. m_previewState.items.push_back(item)
  2. m_spatialHash.insert(item.position, m_previewState.items.size() - 1)   // index of the just-pushed item; insert computes cx = pos.x / 8, cy = pos.y / 8 (m_spatialHash constructed with cellSize 8 at line 550), cellHash = (uint64_t)cx << 32 | (uint64_t)cy, appends {pos, index} to that cell's vector
  3. m_previewState.totalItemsPlaced++          // border items COUNT toward the total (and thus toward maxItemsTotal checks done by callers)
  4. m_previewState.itemCountById[item.itemId]++  // unordered_map<uint16_t,int>, default-constructed 0; border ids counted too
After the loop: if !placementItems.empty(): m_previewState.placementsByRule[placementItems.front().sourceRule]++  — keyed by the FIRST item's sourceRule POINTER; exactly ONE increment per commit no matter how many items/copies; callers use this (or a local rulePlacements mirror) against rule->maxPlacements, i.e. maxPlacements counts placements/stamps, not items.
No return value; never fails; no RNG.

=====================================================================
selectItemFromRule(rule) -> const ItemEntry*  [lines 1281-1303]
=====================================================================
1. if rule == nullptr || rule->items.empty() -> return nullptr (NO RNG consumed).
2. totalWeight = sum of item.weight over rule->items (raw int sum; negative weights included as-is; ItemEntry default weight = 100).
3. if totalWeight <= 0 -> return nullptr (NO RNG consumed).
4. roll = uniform_int_distribution<int>(0, totalWeight - 1)(m_rng)   // exactly one draw.
5. cumulative = 0; for each item in rule->items IN VECTOR ORDER: cumulative += item.weight; if roll < cumulative -> return &item. (Items with weight 0 can never be selected — cumulative unchanged. Items with negative weight shrink cumulative, can make later items unselectable and earlier returns skewed; behavior as-is.)
6. fallback: return &rule->items.back() (reachable only with negative weights present).

## Constants
- cluster retry attempts = 20 -- Max attempts (line 1221, `attempt < 20`) to find a valid offset for EACH additional cluster center beyond the first; exhausting them aborts placing ALL remaining centers, not just the current one.
- count floor = max(1, entry.clusterCount) -- Effective number of cluster copies; clusterCount <= 0 still yields exactly 1 copy at basePos. ItemEntry default clusterCount = 3.
- radius floor = max(0, entry.clusterRadius) -- Scatter half-extent; offsets drawn uniform in [-radius, +radius] per axis. ItemEntry default clusterRadius = 3.
- minDist floor = max(0, entry.clusterMinDistance) -- Minimum Chebyshev distance between cluster-center OFFSETS; reject candidate if dist < minDist (strict). ItemEntry default clusterMinDistance = 2.
- first cluster center = Position(0, 0, 0) -- Always placed unconditionally at basePos with no RNG draw and no distance check.
- cluster center z = 0 -- Scattered centers are Position(dx, dy, 0); all copies stay on basePos's floor (composite tile offsets may still shift z).
- SpatialHashGrid cell size = 8 -- m_spatialHash constructed as SpatialHashGrid(8) in DecorationEngine ctor (line 550); cell coords = pos.x/8, pos.y/8 truncating division.
- queryRadius cell ring = (radius / cellSize) + 1 -- Number of cells scanned each side of the center cell (inclusive range -cellRadius..+cellRadius, dy outer, dx inner).
- SpacingConfig defaults = minDistance=1, minSameItemDistance=2, checkDiagonals=true -- Header defaults; checkDiagonals=true => Chebyshev distance, false => Manhattan.
- ItemEntry default weight = 100 -- Weight used in selectItemFromRule's cumulative roll.
- FloorRule.requireGround default = true -- When true (and not virtual preview), composite tiles whose map tile lacks ground are silently skipped in appendCompositeAt.
- borderItemId disabled = 0 -- rule->borderItemId > 0 gates border item emission; 0 means no border items.
- itemId 0 sentinel = 0 -- Simple entry with itemId 0 fails; composite tile ids equal to 0 are skipped individually; a composite tile whose ids are ALL 0 is skipped entirely (also skipped from validation).

## RNG
All draws use the engine's member std::mt19937 m_rng (seeded outside this section). Draws in this section, in execution order per placement attempt by a caller:

1. selectItemFromRule (called by the generator BEFORE buildPlacementItems): exactly ONE uniform_int draw in [0, totalWeight-1] choosing the ItemEntry. Zero draws if rule is null/empty or totalWeight <= 0 (early nullptr return) — these early exits change the downstream sequence, so the Lua port must reproduce them exactly.

2. buildPlacementItems — ONLY the per-entry cluster branch draws. Branch A (simple) and Branch B (plain composite) consume ZERO draws. Cluster branch: a single uniform_int_distribution<int>(-radius, +radius) object is used for both axes. For each extra center i = 1..count-1: up to 20 attempts; each attempt draws dx then dy (2 draws, fixed order). An attempt ends early (continue) on dx==0&&dy==0 or on tooClose, both still having consumed its 2 draws. Success consumes the attempt's 2 draws and stops that center's attempts. Exhausting 20 attempts (40 draws) without success breaks the whole center loop — later centers consume nothing. The composite-stamping pass afterwards consumes no RNG. Note: center draws happen even when a later appendCompositeAt fails — a failed placement still consumed (selection draw) + (all center draws), shifting the global sequence; preserve this for per-seed determinism.

3. checkSpacingForPlacement, commitPlacement, validateTilePlacement, checkSpacing: zero RNG.

Determinism caveat: std::uniform_int_distribution's mapping from raw mt19937 32-bit outputs to a bounded int is NOT standardized — MSVC (this project's compiler) uses rejection sampling (_Rng_from_urng) that may consume a variable number of engine calls per draw, and libstdc++ differs. A Lua port using its own bounded-int mapping will be internally deterministic per seed but will NOT reproduce the C++ build's exact sequences unless it replicates MSVC's algorithm over a faithful mt19937. Also unspecified: whether a degenerate distribution like (-0, +0) (radius == 0) consumes engine state at all is implementation-defined; the source still logically performs 2 draws per attempt.

## Edge cases
1. Partial outItems on failure: buildPlacementItems returns false AFTER appending items (cluster copy N fails after copies 0..N-1 appended; or composite tile K hard-rejects after tiles 0..K-1 appended). outItems is only cleared at function ENTRY; callers must discard the vector on false (current callers do, because the next call re-clears).
2. "All tiles skipped" composite: appendCompositeAt can return true having placed nothing (every tile lacked ground under requireGround, or had all-zero ids, or empty itemIds). Branch B/C then return !outItems.empty() => false. Distinct from hard rejection but same caller-visible result.
3. Asymmetric checks: simple items get NO requireGround check and NO occupied-tile (items non-empty) check — only validateTilePlacement. Composites get all three. A simple item can be placed on a tile already holding user items; a composite cannot.
4. Occupied-tile reject scope: ONE composite tile landing on a tile with existing items rejects the WHOLE entry including all other cluster copies (lambda returns false at line 1165 -> buildPlacementItems false at 1202/1248).
5. validateTilePlacement ignores its itemId argument entirely; only tile existence and isBlocking matter. m_virtualPreview short-circuits it to always-true; m_editor == nullptr makes it always-false (so non-virtual with no editor can never place anything).
6. Cluster radius 0 with count > 1: all 20 attempts draw (0,0) and are skipped, loop breaks, single copy results — but ~40 logical RNG draws are consumed first.
7. clusterMinDistance 0: duplicate non-zero center offsets allowed (strict `dist < minDist` with minDist 0 never triggers), so identical copies can stack on the same tiles. checkSpacing does NOT catch this because items within one pending placement are never checked against each other (only against committed items); duplicates commit as stacked duplicates.
8. Strict inequalities everywhere: spacing fails on distance < minDistance (so distance == minDistance passes); cluster centers fail on dist < minDist; selection returns on roll < cumulative.
9. checkSpacing same-tile case: distance 0 < minDistance(>=1) fails — committed preview items block re-placement on their own tile via spacing, not via any dedicated check. With minDistance=0 and minSameItemDistance=0 nothing is ever blocked.
10. Spacing is z-filtered in checkSpacing (existing.position.z != pos.z skipped) but the spatial hash itself ignores z — items on other floors at same x/y share grid cells and are filtered late.
11. Border items: emitted once per position with >=1 decoration item (placedPositions), appended after ALL decoration items, carry sourceRule, are spacing-checked, and inflate totalItemsPlaced/itemCountById. If rule == nullptr (defensive) no borders are emitted.
12. commitPlacement counts: placementsByRule increments exactly once per commit using the FIRST item's sourceRule pointer (pointer-keyed map). A multi-copy cluster of 30 items = 1 placement against maxPlacements but 30 against maxItemsTotal.
13. selectItemFromRule with negative weights: totalWeight may still be > 0; cumulative can decrease, skewing selection; fallback `return &items.back()` is then reachable. With all weights >= 0 and totalWeight > 0 the loop always returns before the fallback. weight == 0 entries are unselectable.
14. Composite tile.offset.z is honored (Position::operator+ adds z), so composites can span floors; cluster scatter never changes z (centers have z=0).
15. checkSpacingForPlacement on an empty vector returns true; commitPlacement on an empty vector does nothing (the placementsByRule increment is guarded by !empty()).

## Porting notes
- 0 vs 1 indexing: commitPlacement stores `items.size() - 1` (0-based C++ index) into the spatial hash, and checkSpacing guards `idx >= items.size()`. In Lua use #items after table.insert (1-based) and adjust the stale-index guard to `idx > #items`; PreviewState::getItemsAt/rebuildSpatialIndex elsewhere use the same 0-based indices — keep one convention everywhere.
- Integer division: SpatialHashGrid uses C++ truncation-toward-zero (`pos.x / 8`). Lua's `//` floors (differs for negatives: -1//8 == -1 but C++ -1/8 == 0). Use a trunc-div helper if negative coordinates are possible; with floor division the grid is actually CORRECT for negatives, but it changes which cell a position maps to versus the C++ build.
- Cell-hash collision bug for negative cells (describe-as-is, flagging): `(uint64_t)(cx) << 32 | (uint64_t)(cy)` — a negative cy sign-extends to 64 bits and its high 32 bits smear over the cx field, so e.g. (cx=0, cy=-1) collides with (cx=0xFFFFFFFF, cy=-1) patterns. Harmless on typical non-negative map coords; in Lua, hash cells with a string key "cx:cy" or pack with masking ((cx & 0xFFFFFFFF) * 2^32 + (cy & 0xFFFFFFFF)) to mimic, or just use a 2-level table.
- Pointer-keyed maps: placementsByRule keys on const FloorRule* and itemCountById on uint16_t. In Lua, key on the rule TABLE reference (works like the pointer) and the numeric id. sourceRule identity must be preserved across PreviewItems (don't deep-copy the rule per item).
- RNG determinism: mt19937 is portable, but std::uniform_int_distribution / uniform_real_distribution mappings are implementation-defined (MSVC here). A Lua port cannot match the C++ build's per-seed output without reimplementing MSVC's bounded-int rejection sampling on top of mt19937. Recommended: implement mt19937 + your own fixed mapping (e.g. Lemire or modulo) and accept different-but-deterministic sequences; preserve DRAW ORDER and COUNT exactly as specified (selection draw first; then for clusters dx-then-dy, 2 draws per attempt, max 20 attempts per extra center, draws consumed even on skipped/failed attempts and even when the placement is later rejected).
- Strict `<` comparisons throughout (spacing, cluster min distance, weighted roll). Do not convert to `<=`.
- The early-`break` on a failed center slot (line 1240-1243) aborts ALL remaining centers, not just one — easy to get wrong as a per-center `continue`.
- validateTilePlacement's unused itemId: keep the parameter for call-site fidelity but know it is dead; the composite code still scans for the first non-zero id (step e) and SKIPS the tile if none — that skip is load-bearing even though the id itself isn't.
- The requireGround `continue` (skip tile) vs occupied-items `return false` (kill entry) distinction, and their ORDER (ground check first), must be preserved exactly.
- outItems.clear() at entry + possible partial contents on false return: in Lua, either clear at entry identically or always pass a fresh table; never commit a table from a false return.
- iteration-order assumptions: compositeTiles, itemIds, rule->items, centers, and placementItems are std::vector — preserve array order in Lua (ipairs). The only unordered containers touched (itemCountById, placementsByRule, spatial-hash cells) are never iterated in this section, so Lua table iteration order is safe for them here (but checkSpacing's `nearby` list order doesn't affect the boolean result).
- Float vs int: this section is integer-only except rule->density/friend bias used by CALLERS before invoking these functions; no float comparisons to worry about inside lines 1110-1305.
- Entry-level hasCenterPoint/centerOffset is applied by callers (subtract centerOffset.x/y/z from the target to get basePos at cpp lines ~1727-1731 and ~1882-1886), NOT inside buildPlacementItems — don't double-apply it in the port.