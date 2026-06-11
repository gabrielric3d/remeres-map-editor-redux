# area_decoration.cpp lines 2080-2869: DecorationPreset::saveToFile / loadFromFile / toXmlString / fromXmlString + PresetManager (getInstance, getPresetsDirectory, loadPresets, savePresets, getPresetNames, getPreset, addPreset, removePreset, renamePreset)

## Summary
XML (de)serialization of DecorationPreset via pugixml, plus a singleton PresetManager that maps preset-name -> DecorationPreset and persists each preset as <sanitized-name>.xml under <data>/presets/decoration. saveToFile and toXmlString emit identical XML except toXmlString omits the root version="1.0" attribute. loadFromFile and fromXmlString are byte-for-byte identical parsing logic (one reads a file, one a buffer). No RNG in this section. Key quirks: rule density defaults to 0.3 on load but 1.0 in the struct; friend range overrides friend single-id; rule_mode backward-compat inference; legacy "chance" attribute fallback for composite/cluster weight; id==0 items dropped both ways; preset map keyed by the XML name attribute, not the filename.

## Semantics
============================================================
PART 1 — XML SCHEMA (complete; produced by saveToFile/toXmlString, consumed by loadFromFile/fromXmlString)
============================================================
Document: pugixml default output = `<?xml version="1.0"?>` declaration, tab ("\t") indentation, one attribute-quoted-with-double-quotes per pugixml default. Self-closing empty elements as `<name ... />`.

ROOT: <decoration_preset name="STR" version="1.0">
  - name: preset name string (only free-text field besides rule name; needs XML escaping). Read default when absent: "Unnamed Preset".
  - version: written ONLY by saveToFile (always literal "1.0"); toXmlString does NOT write it; NEVER read by any loader. Purely informational.

CHILD ORDER AS WRITTEN (fixed): <spacing>, <distribution>, <settings>, [<area>] (only if hasArea), <floor_rules>.
READ: each is looked up by name via root.child(...), so reader must not depend on order. If <spacing>/<distribution>/<settings> is ABSENT, the corresponding fields are NOT touched (they keep whatever the DecorationPreset object already held — for a fresh object, the struct defaults). hasArea is unconditionally reset to false before checking <area>. floorRules is unconditionally cleared. name is always assigned.

<spacing min_distance="I" same_item_distance="I" check_diagonals="B"/>
  Read defaults (attr absent): min_distance=1, same_item_distance=2, check_diagonals=true.
  Maps to SpacingConfig{minDistance, minSameItemDistance, checkDiagonals}.

<distribution mode="I" cluster_strength="F" cluster_count="I" grid_spacing_x="I" grid_spacing_y="I" grid_jitter="I"/>
  mode is the int cast of DistributionMode: 0=PureRandom, 1=Clustered, 2=GridBased. Read default 0. NO range validation on load (mode="7" is stored as-is).
  Read defaults: cluster_strength=0.5, cluster_count=3, grid_spacing_x=3, grid_spacing_y=3, grid_jitter=1.

<settings max_items_total="I" skip_blocked="B" default_seed="U64STR"/>
  Read defaults: max_items_total=-1, skip_blocked=true, default_seed string "0".
  default_seed: WRITTEN as std::to_string(uint64) (decimal string); READ via strtoull(seedStr, nullptr, 10) — full unsigned 64-bit range, base 10 only.

<area .../> — written ONLY when hasArea==true; on read, presence of <area> sets hasArea=true.
  Attributes (all written always when node exists):
    type="I" (int cast of AreaDefinition::Type: 0=Rectangle, 1=FloodFill, 2=Selection; read default 0, no validation)
    rect_min_x, rect_min_y, rect_min_z, rect_max_x, rect_max_y, rect_max_z (ints, read default 0)
    flood_origin_x, flood_origin_y, flood_origin_z (ints, read default 0)
    flood_target_ground (uint, read default 0)
    flood_max_radius (int, read default 100)

<floor_rules> contains zero or more <rule> elements. Rules are written in floorRules vector order and read back in document order (order preserved). If <floor_rules> is absent the loop simply runs zero times (pugixml null-node chaining); result: empty rule list, load still returns true.

<rule> attributes — WRITE ORDER (all written for every rule unless noted):
  1. name="STR" (read default "Rule")
  2. rule_mode="cluster" | rule_mode="range" — written ONLY for Cluster/FloorRange modes; OMITTED entirely for SingleFloor (backward compat).
  3. floor_id="U" (read default 0)
  4. from_floor_id="U" (read default 0)
  5. to_floor_id="U" (read default 0)
  6. density="F" (read default 0.3 — NOTE: struct default is 1.0; see quirks)
  7. max_placements="I" (read default -1)
  8. priority="I" (read default 0)
  9. enabled="B" (read default true)
  10. border_item_id="U" (read default 0)
  11. (cluster rules only) has_center="B" (default false), center_x/center_y/center_z="I" (default 0), instance_count="I" (default 1), instance_min_distance="I" (default 5), require_ground="B" (default true)
  12. friend_floor_id, friend_from_floor_id, friend_to_floor_id: WRITE: if rule.isFriendRange() (friendFromFloorId>0 AND friendToFloorId>0) write friend_floor_id="0", friend_from_floor_id=val, friend_to_floor_id=val; ELSE write friend_floor_id=val, friend_from_floor_id="0", friend_to_floor_id="0". The three are mutually exclusive by construction.
  13. friend_chance="I" (read default 0), friend_strength="I" (read default 0)

<rule> READ ORDER OF OPERATIONS (matters for field interactions):
  a. Read name, floor_id, from_floor_id, to_floor_id, density, max_placements, priority, enabled, border_item_id.
  b. rule_mode resolution: read attribute as string with default "". Exact lowercase compares: "cluster" -> RuleMode::Cluster; "range" -> RuleMode::FloorRange; ANYTHING ELSE (including absent, "Range", "CLUSTER", garbage) -> backward compat: if (fromFloorId > 0 && toFloorId > 0) -> FloorRange, else -> SingleFloor.
  c. IF rule is Cluster mode: read has_center, center_x/y/z, instance_count, instance_min_distance, require_ground (defaults above); then parse <cluster_tile> children of <rule> (NOT inside <items>): each cluster_tile has x="I" y="I" z="I" (default 0 each) and zero+ <item id="U"/> children; item ids with id>0 are appended in document order; cluster_tile is kept ONLY if it ended with >=1 valid item id. For non-cluster rules these attributes/children are never read (so has_center etc. keep FloorRule struct defaults: hasCenterPoint=false, centerOffset=(0,0,0), instanceCount=1, instanceMinDistance=5, requireGround=true).
  d. Friend fields: read friend_from_floor_id and friend_to_floor_id first (defaults 0). If BOTH > 0: friendFloorId=0, friendFromFloorId=from, friendToFloorId=to (friend_floor_id attribute is IGNORED in this branch). Else: friendFloorId = friend_floor_id attr (default 0), friendFromFloorId=0, friendToFloorId=0. Then friend_chance and friend_strength (defaults 0).
  e. Parse <items> children (see below). NOTE: this loop runs for ALL rules including cluster-mode rules.
  f. Push rule into floorRules (always pushed, even with zero items).

<items> element — always written, even for cluster rules (typically `<items />` empty there). Children are a heterogeneous ordered sequence; the reader iterates itemsNode.first_child()..next_sibling() over ALL children (any element name) and dispatches on the element name; unknown names are silently skipped. Document order is preserved into rule.items.

  <item id="U" weight="I"/> — simple entry. Read: id default 0, weight default 100. Entry is appended ONLY if id > 0 (id==0 or absent => silently dropped). Maps to ItemEntry{itemId=id, weight, isComposite=false}.

  <composite weight="I"> and <cluster weight="I" count="I" radius="I" min_distance="I"> — composite entries (isComposite=true; <cluster> additionally isCluster=true).
    WRITE: element name is "cluster" if entry.isClusterEntry() else "composite"; weight always written; count/radius/min_distance written only for cluster entries.
    READ weight (legacy compat): weight = attr("weight").as_int(0); if weight <= 0 then weight = attr("chance").as_int(100). So: absent weight -> 0 -> falls back to chance (absent chance -> 100). weight="0" or negative also falls to chance. weight="50" -> 50, chance ignored.
    Children: zero+ <tile x="I" y="I" z="I"> (offsets, default 0 each; may be negative), each containing zero+ <item id="U"/>. ids with id>0 appended in document order; tile kept only if it has >=1 valid id (so an id-less or all-zero tile is dropped, shifting nothing else — order of remaining tiles preserved).
    The whole composite/cluster entry is appended ONLY if tiles is non-empty after filtering.
    READ cluster params (only for element name "cluster"): count default 3, radius default 3, min_distance default 2. Constructed via ItemEntry::MakeCluster(tiles, weight, count, radius, minDistance) — sets isComposite=true, isCluster=true. "composite" -> ItemEntry::MakeComposite(tiles, weight) — isComposite=true, isCluster=false.
    WRITE filtering mirrors read: tiles with empty itemIds skipped; ids==0 skipped inside tiles.

  IMPORTANT distinction: <cluster> inside <items> is a weighted cluster ITEM ENTRY (ItemEntry.isCluster, attributes count/radius/min_distance). <cluster_tile> as a direct child of <rule> belongs to rule_mode="cluster" RULES (FloorRule.clusterTiles, attributes has_center/instance_count/etc. on the rule). Both coexist in the schema; do not conflate.

NESTING DEPTH the Lua parser must handle (more than flat attribute matching):
  decoration_preset > floor_rules > rule > items > (item | composite | cluster) > tile > item
  decoration_preset > floor_rules > rule > cluster_tile > item
  No text nodes anywhere — 100% attribute-based; all leaf data is in attributes. No CDATA, no mixed content. The only nesting-sensitive ambiguity: <item> appears at TWO depths (direct child of <items> = weighted entry with weight attr; grandchild inside <tile>/<cluster_tile> = bare id-only). A pattern-based Lua parser must track element nesting to disambiguate; matching `<item id="%d+"` globally is NOT sufficient.

VALUE FORMATS (pugixml semantics the Lua port must replicate):
  - bool WRITE: literal "true"/"false". bool READ (as_bool): true iff FIRST character of the attribute value is one of '1','t','T','y','Y'; everything else (including "TRUE" -> 'T' is true actually; note 'T' IS in the set) — precisely: first char in {1,t,T,y,Y} => true, else false. Absent attribute => stated default.
  - int READ (as_int): strtol base10-ish (pugixml uses strtol with base 10 via get_value_int). Absent => default. PRESENT BUT NON-NUMERIC => 0 (NOT the default!) — strtol("abc")=0. Leading whitespace/sign handled per strtol.
  - uint READ (as_uint): strtoul; "-1" wraps to 4294967295 then truncates into uint16_t fields (see edge cases).
  - float READ (as_float): strtod cast to float; absent => default; non-numeric present => 0.
  - float WRITE: pugixml %.9g-style shortest form: 1.0f -> "1", 0.5f -> "0.5", 0.3f -> "0.300000012" potentially (float precision 9 sig digits; in practice 0.5 and 1 are exact). Lua port writing XML should emit minimal decimal that round-trips.
  - uint16_t fields written through int promotion (plain decimal, no padding).
  - ESCAPING (write): pugixml escapes in attribute values: & -> &amp;, < -> &lt;, > -> &gt;, " -> &quot;, control chars -> &#NN;. Reader must unescape at minimum &amp; &lt; &gt; &quot; &apos; and numeric &#dd;/&#xhh;. Only name attributes (preset name, rule name) can realistically contain such characters; all numeric attributes never need escaping.

============================================================
PART 2 — FUNCTION-BY-FUNCTION
============================================================
DecorationPreset::saveToFile(filepath) const -> bool: builds the document exactly as the schema above (root gets version="1.0"), returns doc.save_file(filepath) (pugixml default flags: declaration + tab indent). Returns false only on file I/O failure.

DecorationPreset::loadFromFile(filepath) -> bool: doc.load_file; on parse failure return false; missing <decoration_preset> root return false; otherwise parse per schema and return true (no validation of contents; partial/empty documents still succeed). MUTATES: name (always), spacing/distribution/settings fields (only if node present), hasArea (always reset false, set true if <area>), area fields (only if <area> present — stale area values persist otherwise), floorRules (always cleared then rebuilt).

DecorationPreset::toXmlString() const -> string: identical serialization minus root version attribute; doc.save(ostringstream) with defaults; returns string (includes XML declaration and trailing newline).

DecorationPreset::fromXmlString(xml) -> bool: doc.load_buffer(xml.c_str(), xml.size()); thereafter literally identical to loadFromFile's parse code (duplicated, not shared).

PresetManager::getInstance(): C++ function-local static singleton.

PresetManager::getPresetsDirectory() const -> string:
  dir = FileSystem::GetDataDirectory() + "/presets"; presetsDir = dir + "/decoration".
  If "/presets" doesn't exist: wxMkdir it (non-recursive, parent first). Then if "/presets/decoration" doesn't exist: wxMkdir it. Returns "/presets/decoration" path as std::string. Side effect: creates directories on EVERY call (including from removePreset etc.). mkdir failures ignored.

PresetManager::loadPresets() -> bool:
  1. m_presets.clear() (unconditional).
  2. dir = getPresetsDirectory(); open wxDir(dir). If not opened -> return false (m_loaded stays false, map stays empty).
  3. Enumerate files matching glob "*.xml", FILES ONLY (no subdirectories, wxDIR_FILES). Enumeration order is filesystem/OS-dependent (NOT sorted — on NTFS it is usually name-ordered but this is not guaranteed).
  4. For each file: construct filepath = dir + "/" + filename; DecorationPreset preset; if preset.loadFromFile(filepath) succeeds: m_presets[preset.name] = preset — KEYED BY THE name ATTRIBUTE INSIDE THE XML, not the filename. Two files with the same internal name: the later-enumerated one silently wins. Failed parses silently skipped.
  5. m_loaded = true; return true.

PresetManager::savePresets() -> bool:
  For each (name, preset) in m_presets — std::map iteration = ascending byte-wise string order of names. filename = name with each of the 9 chars / \ : * ? " < > | replaced by '_'. filepath = dir + "/" + filename + ".xml". preset.saveToFile(filepath) — RESULT IGNORED. Always returns true.

PresetManager::getPresetNames() const: copies map keys into vector (already ascending), then std::sort (byte-wise <; redundant but harmless). Returns sorted vector.

PresetManager::getPreset(name) const: map find; pointer to stored preset or nullptr.

PresetManager::addPreset(preset) -> bool:
  1. If preset.name.empty() -> false.
  2. m_presets[preset.name] = preset (insert or overwrite).
  3. Immediately persist: sanitize name (same 9-char replacement), write to dir/"<sanitized>.xml" via saveToFile; return its bool. So the in-memory map can hold a preset even when this returns false (disk write failed).

PresetManager::removePreset(name) -> bool:
  1. find in map; absent -> false.
  2. sanitize name, build filepath, wxRemoveFile(filepath) — result ignored (file may not exist).
  3. erase map entry; return true.

PresetManager::renamePreset(oldName, newName) -> bool (exact early-exit order):
  1. oldName == newName -> return true (no-op).
  2. newName.empty() -> return false.
  3. oldName not in map -> return false.
  4. newName already in map -> return false.
  5. Copy preset by value, set copy.name = newName.
  6. removePreset(oldName) (deletes old file + map entry).
  7. return addPreset(copy) (inserts + writes new file). Note: if step 7's disk write fails, the old preset is already gone (not transactional).

============================================================
PART 3 — WORKED EXAMPLE (ICE.xml, b:/Github/rme_redux/data/presets/decoration/ICE.xml)
============================================================
<?xml version="1.0"?>
<decoration_preset name="ICE" version="1.0">
  <spacing min_distance="1" same_item_distance="2" check_diagonals="true" />
  <distribution mode="0" cluster_strength="0.5" cluster_count="3" grid_spacing_x="3" grid_spacing_y="3" grid_jitter="1" />
  <settings max_items_total="-1" skip_blocked="true" default_seed="0" />
  <area type="2" rect_min_x="902" rect_min_y="996" rect_min_z="6" rect_max_x="1141" rect_max_y="1230" rect_max_z="6" flood_origin_x="0" ... flood_target_ground="0" flood_max_radius="100" />
  <floor_rules>
    <rule name="New Rule" rule_mode="range" floor_id="0" from_floor_id="44935" to_floor_id="44943" density="1" max_placements="-1" priority="0" enabled="true" border_item_id="0" friend_floor_id="0" friend_from_floor_id="0" friend_to_floor_id="0" friend_chance="0" friend_strength="0">
      <items>
        <cluster weight="100" count="1" radius="3" min_distance="2">
          <tile x="-2" y="-1" z="0"><item id="4744" /></tile>
          <tile x="0" y="0" z="0"><item id="6678" /><item id="4744" /></tile>   <!-- multiple stacked ids per tile, order preserved -->
          ... (15 tiles in this entry)
        </cluster>
        ... (6 cluster entries total, all weight=100 count=1 radius=3 min_distance=2)
      </items>
    </rule>
  </floor_rules>
</decoration_preset>
Decodes to: name="ICE"; spacing{1,2,true}; distribution{PureRandom,0.5,3,3,3,1}; maxItemsTotal=-1, skipBlockedTiles=true, defaultSeed=0; hasArea=true, area.type=Selection(2), rectMin=(902,996,6), rectMax=(1141,1230,6), floodOrigin=(0,0,0), floodTargetGround=0, floodMaxRadius=100; one FloorRule: name "New Rule", ruleMode=FloorRange (explicit "range"), floorId=0, fromFloorId=44935, toFloorId=44943, density=1.0, maxPlacements=-1, priority=0, enabled=true, borderItemId=0, no friend (all 0); rule.items = 6 ItemEntry with isComposite=true, isCluster=true, weight=100, clusterCount=1, clusterRadius=3, clusterMinDistance=2, each with its compositeTiles list of {offset(x,y,0), itemIds[...] in document order}. Note: ICE has NO rule_mode="cluster" rule, so no <cluster_tile>/has_center attributes appear. circus.xml is structurally identical (type="0" Rectangle area, range 18764-18766, 7 cluster entries, tiles with up to 3 stacked item ids).

## Constants
- root element = decoration_preset -- Required root; load fails (returns false) if absent
- version attribute = "1.0" -- Written by saveToFile only (not toXmlString); never read
- name default (preset) = "Unnamed Preset" -- Used when root name attribute absent
- rule name default = "Rule" -- Used when rule name attribute absent
- spacing defaults = min_distance=1, same_item_distance=2, check_diagonals=true -- Load defaults when attributes absent (match struct defaults)
- distribution defaults = mode=0, cluster_strength=0.5, cluster_count=3, grid_spacing_x=3, grid_spacing_y=3, grid_jitter=1 -- Load defaults (match struct defaults)
- settings defaults = max_items_total=-1, skip_blocked=true, default_seed="0" -- Load defaults
- rule density load default = 0.3 -- as_float(0.3f) when density attribute absent — DIFFERS from FloorRule struct default 1.0
- rule defaults (load) = floor_id=0, from=0, to=0, max_placements=-1, priority=0, enabled=true, border_item_id=0 -- Per-attribute load defaults
- cluster-rule defaults (load) = has_center=false, center=(0,0,0), instance_count=1, instance_min_distance=5, require_ground=true -- Only read when ruleMode==Cluster
- friend defaults (load) = friend_floor_id=0, friend_from=0, friend_to=0, friend_chance=0, friend_strength=0 -- Range (both from&to>0) overrides single id; single id branch zeroes the range
- simple item weight default = 100 -- <item weight> absent -> 100
- composite/cluster weight fallback = weight as_int(0); if <=0 use chance as_int(100) -- Legacy 'chance' attribute compatibility
- cluster entry defaults (load) = count=3, radius=3, min_distance=2 -- <cluster> element inside <items>
- flood_max_radius default = 100 -- Area load default
- DistributionMode enum = 0=PureRandom, 1=Clustered, 2=GridBased -- int encoding in mode attribute
- AreaDefinition::Type enum = 0=Rectangle, 1=FloodFill, 2=Selection -- int encoding in area type attribute
- rule_mode strings = "cluster", "range", absent=SingleFloor (or inferred range) -- Exact lowercase match; anything else triggers backward-compat inference
- filename sanitization set = / \ : * ? " < > |  -> '_' -- 9 characters replaced when deriving filename from preset name
- presets directory = <GetDataDirectory()>/presets/decoration -- Created on demand (two non-recursive mkdirs); file glob "*.xml", files only
- bool true chars (pugixml as_bool) = first char in {1,t,T,y,Y} -- Parsing rule for boolean attributes

## RNG
NONE. This section performs zero RNG draws. The only RNG-adjacent datum is default_seed, which is serialized as a base-10 decimal string of a uint64 (std::to_string on write, strtoull base 10 on read) and merely stored for later use by the engine. Determinism per seed is unaffected by this code; the only order-sensitive behaviors here are deterministic: document order of rules/items/tiles/ids is preserved exactly into the vectors (which later feeds weighted selection and placement order), and PresetManager's std::map keeps presets in ascending byte-wise name order (savePresets write order, getPresetNames order).

## Edge cases
1. Missing <spacing>/<distribution>/<settings> nodes: fields are left UNCHANGED (not defaulted) — reusing a preset object across loads can leak stale values. Same for <area> child attributes, but hasArea is always reset to false first; floorRules always cleared; name always overwritten.
2. density attribute absent -> 0.3 (load default), but a freshly constructed FloorRule has density 1.0. A round-trip always writes density explicitly, so this only bites hand-written XML.
3. rule_mode backward compat: absent/unknown value -> FloorRange iff from_floor_id>0 AND to_floor_id>0, else SingleFloor. Strict lowercase "cluster"/"range" only.
4. Friend precedence on load: if friend_from_floor_id>0 AND friend_to_floor_id>0, friend_floor_id attribute is ignored and forced to 0; otherwise the range fields are forced to 0 even if one of them was nonzero (a half-range like from=5,to=0 is silently discarded). Writer mirrors this (mutually exclusive output).
5. weight<=0 on composite/cluster (including explicit weight="0" or negative) falls back to legacy chance attribute (default 100). A deliberately-zero weight cannot be round-tripped for composites; simple <item> entries keep weight verbatim (even 0/negative).
6. <item> with id absent or id="0" silently dropped on load; ids==0 skipped on write. <tile>/<cluster_tile> with no surviving ids dropped; <composite>/<cluster> with no surviving tiles dropped entirely. Rules with zero items are still kept.
7. id parsed via as_uint then assigned to uint16_t: values >65535 truncate mod 65536 in C++ (e.g. id="70000" -> 4464); "-1" -> strtoul wrap -> truncates to 65535. floor_id/from/to/border/friend ids same.
8. enum casts unvalidated: mode="9" or area type="9" load as out-of-range enum values; ruleMode is safe (string-mapped).
9. Non-numeric attribute VALUES parse to 0 (strtol/strtod behavior), NOT the documented default — defaults apply only when the attribute is ABSENT.
10. <items> children iterate ALL element names; unknown names skipped silently; cluster-mode rules still parse <items> (can carry regular items alongside cluster_tiles).
11. loadPresets keys the map by the XML name attribute, not the filename: file FOO.xml containing name="Bar" registers as "Bar"; duplicate internal names -> last enumerated file wins (OS-dependent enumeration order); renaming a preset's name attribute by hand orphans the old filename (savePresets/addPreset write to the sanitized NEW name; the old file lingers until removePreset).
12. loadPresets returns false only if the directory can't be opened; individual parse failures are silent. savePresets always returns true even if every write fails. addPreset returns the disk-write result but the map mutation already happened.
13. renamePreset is non-transactional: old file+entry removed before new write; a failing addPreset loses the preset on disk (it survives only in the local copy that is then discarded, though the map does get the new entry inside addPreset before the failed save).
14. removePreset on a preset whose file was never written (or whose name sanitizes to a different existing file) still returns true; wxRemoveFile result ignored. Sanitization collisions: names "a/b" and "a:b" both map to file "a_b.xml" — they overwrite each other on disk while coexisting in memory.
15. fromXmlString on an empty/garbage string returns false (pugixml parse error); a well-formed document with the wrong root returns false; a minimal `<decoration_preset/>` returns TRUE with name "Unnamed Preset" and everything else untouched/cleared per item 1.

## Porting notes
LUA PARSER REQUIREMENTS: The format is purely attribute-based (no text nodes/CDATA), but a flat gmatch over attributes is insufficient — you MUST track nesting because (a) <item> means two different things depending on depth (weighted entry under <items> vs bare id under <tile>/<cluster_tile>), (b) <cluster> under <items> is an item entry while <cluster_tile> under <rule> is a rule-level grid tile, (c) document order of rules/entries/tiles/ids must be preserved into arrays (later weighted selection iterates these in order). A practical approach: a small stack-based scanner over `<(/?)([%w_]+)(...)>` tags with an attribute sub-parser `([%w_]+)%s*=%s*"([^"]*)"`, plus entity unescaping (&amp; &lt; &gt; &quot; &apos; &#dd; &#xhh;) applied to every attribute value (realistically only name attributes need it, but apply uniformly). Handle both self-closing `<x/>` and paired forms. Skip the `<?xml?>` declaration and (defensively) comments.
PUGIXML SEMANTICS TO REPLICATE: (1) attribute-absent vs attribute-malformed differ: absent -> stated default; present-but-non-numeric -> 0 (tonumber returns nil in Lua — map nil-from-tonumber to 0 when the attribute exists, to the default when it doesn't). (2) as_bool = first character in {'1','t','T','y','Y'}; do NOT use tostring comparisons or Lua truthiness. (3) Write side: booleans as "true"/"false"; floats in shortest round-trip decimal ("1" not "1.000000", "0.5"); string.format("%.9g") approximates pugixml float output but beware Lua doubles vs C++ floats — density 0.3f stored as float may print as "0.300000012" from C++; accept either on read.
NUMERIC PITFALLS: uint16 truncation (id % 65536 after strtoul wrap) — decide whether to replicate the C++ truncation or reject ids >65535; replicating requires `id % 0x10000` and mapping negative strings through 2^32 wrap first (strtoul("-1") = 4294967295 -> & 0xFFFF = 65535). default_seed is a full uint64: Lua 5.3+ signed 64-bit integers overflow above 2^63-1 — parse with math.tointeger and fall back to a string/two-32-bit representation if you must support the full range; LuaJIT needs uint64_t cdata. No integer-division or float-comparison hazards in this section.
INDEXING: all offsets (tile x/y/z, center_x/y/z) are signed ints, frequently negative (see ICE.xml tile x="-2") — they are map-relative offsets, not array indices; keep them verbatim. Lua arrays for rules/items/tiles should be 1-based ipairs-ordered matching document order; never use pairs() over these (iteration order matters downstream).
CONTAINER ORDER: PresetManager uses std::map (sorted, byte-wise string <). In Lua, keep a name->preset hash PLUS derive sorted name lists via table.sort with the default string < (which is byte-wise in Lua for ASCII; locale-sensitive os configurations could differ — force byte comparison if needed). savePresets iterates in sorted order (cosmetic) and getPresetNames sorts again (redundant in C++; required in Lua since hash order is undefined).
SUSPECTED BUGS / AS-IS BEHAVIORS TO PRESERVE OR FLAG: (1) toXmlString omits version="1.0" while saveToFile writes it — harmless (never read) but a fidelity difference if you diff outputs. (2) density load default 0.3 contradicts the struct default 1.0 — as-is: hand-authored XML without density gets 0.3. (3) composite/cluster weight<=0 is unrepresentable after a round-trip (falls back to chance=100). (4) renamePreset/addPreset non-transactional disk behavior and sanitization filename collisions (two names mapping to one file). (5) loadPresets keying by internal name can orphan files and lets enumeration order decide duplicate-name winners. (6) The friend half-range silently degrades to single-id-only-from-attribute (and that attribute may be 0), losing data. Replicate all of these exactly unless the port owner decides otherwise; if diverging, document it. Files referenced: b:/Github/rme_redux/source/editor/area_decoration.h, b:/Github/rme_redux/source/editor/area_decoration.cpp (lines 2080-2869), b:/Github/rme_redux/data/presets/decoration/ICE.xml, b:/Github/rme_redux/data/presets/decoration/circus.xml.