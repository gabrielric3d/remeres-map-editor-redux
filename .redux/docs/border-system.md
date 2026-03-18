# RME Redux - Border System

## Overview

The auto-border system automatically places transition tiles between different ground types. When a ground brush is painted, the system calculates which borders are needed based on the 8 neighboring tiles.

## Core Classes

| Class | File | Purpose |
|-------|------|---------|
| `AutoBorder` | `source/brushes/ground/auto_border.h` | Stores 13 directional tile IDs for a border |
| `GroundBrush` | `source/brushes/ground/ground_brush.h` | Ground brush with border definitions |
| `GroundBorderCalculator` | `source/brushes/ground/ground_border_calculator.h` | Calculates borders for a tile |
| `OptionalBorderBrush` | `source/brushes/border/optional_border_brush.h` | Toggle optional (mountain) borders |

## AutoBorder

```cpp
class AutoBorder {
    std::array<uint32_t, 13> tiles;  // Item IDs indexed by BorderType
    uint32_t id;                      // Unique border ID
    uint16_t group;                   // Group for specific case matching
    bool ground;                      // Is ground border?

    static BorderType edgeNameToID(std::string_view edgename);
    // "n"→1, "e"→2, "s"→3, "w"→4, "cnw"→5, "cne"→6, "csw"→7, "cse"→8,
    // "dnw"→9, "dne"→10, "dse"→11, "dsw"→12
};
```

## BorderType Enum (`source/brushes/brush_enums.h`)

```cpp
enum BorderType {
    BORDER_NONE = 0,
    NORTH_HORIZONTAL = 1,     EAST_HORIZONTAL = 2,
    SOUTH_HORIZONTAL = 3,     WEST_HORIZONTAL = 4,
    NORTHWEST_CORNER = 5,     NORTHEAST_CORNER = 6,
    SOUTHWEST_CORNER = 7,     SOUTHEAST_CORNER = 8,
    NORTHWEST_DIAGONAL = 9,   NORTHEAST_DIAGONAL = 10,
    SOUTHEAST_DIAGONAL = 11,  SOUTHWEST_DIAGONAL = 12,
};

enum TileAlignement {
    TILE_NORTHWEST = 1,   TILE_NORTH = 2,      TILE_NORTHEAST = 4,
    TILE_WEST = 8,        TILE_EAST = 16,
    TILE_SOUTHWEST = 32,  TILE_SOUTH = 64,     TILE_SOUTHEAST = 128,
};
```

## GroundBrush Border Structures

```cpp
struct BorderBlock {
    bool outer;              // Outer or inner border?
    bool super;              // Super border flag
    uint32_t to;             // Target brush ID (0xFFFFFFFF=all, 0=none)
    std::unique_ptr<AutoBorder> owned_autoborder;
    AutoBorder* autoborder;
    std::vector<std::unique_ptr<SpecificCaseBlock>> specific_cases;
};

struct SpecificCaseBlock {
    std::vector<uint16_t> items_to_match;
    uint32_t match_group;
    BorderType group_match_alignment;
    uint16_t to_replace_id;
    uint16_t with_id;
    bool delete_all;
    bool keepBorder;
};

struct BorderCluster {
    uint32_t alignment;       // 8-bit neighbor bitmask
    int32_t z;                // Z-order for layering
    const AutoBorder* border;
};
```

## GroundBrush Key Members

```cpp
class GroundBrush : public TerrainBrush {
    int32_t z_order;
    bool has_zilch_outer_border;     // Border to empty tile (outer)
    bool has_zilch_inner_border;     // Border to empty tile (inner)
    bool has_outer_border;           // Border to other brushes (outer)
    bool has_inner_border;
    AutoBorder* optional_border;     // Mountain/optional border
    bool use_only_optional;          // Suppress normal when optional shown
    bool randomize;
    std::vector<std::unique_ptr<BorderBlock>> borders;
    std::vector<ItemChanceBlock> border_items;  // Ground tile items
    static uint32_t border_types[256];          // Lookup table

    static void doBorders(BaseMap* map, Tile* tile);
    static BorderBlock* getBrushTo(GroundBrush* first, GroundBrush* second);
    AutoBorder* getFirstAutoBorder() const;
    bool useSoloOptionalBorder() const;
};
```

## Border Calculation Algorithm

**`GroundBorderCalculator::calculate(BaseMap* map, Tile* tile)`**

1. **Extract neighbors** — check 8 surrounding tiles, get their GroundBrush*
2. **Detect borders** — for each neighbor with different ground:
   - Use `getBrushTo(current, neighbor)` to find appropriate BorderBlock
   - Cases: both have ground (different), current has ground / neighbor empty, etc.
3. **Build BorderClusters** — accumulate alignment bitmask for same-border neighbors
4. **Lookup alignment** — `border_types[alignment]` returns packed 4-byte result:
   ```cpp
   // Each byte = one BorderType direction (up to 4 borders per configuration)
   directions[0] = (border_types[alignment] & 0x000000FF);
   directions[1] = (border_types[alignment] & 0x0000FF00) >> 8;
   directions[2] = (border_types[alignment] & 0x00FF0000) >> 16;
   directions[3] = (border_types[alignment] & 0xFF000000) >> 24;
   ```
5. **Resolve items** — `autoborder->tiles[direction]` = item ID to place
6. **Handle diagonals** — if diagonal item missing, split into perpendicular components
7. **Apply specific cases** — check SpecificCaseBlock conditions, replace/delete borders
8. **Cleanup & apply** — `TileOperations::cleanBorders(tile)` then `addBorderItem()`

## getBrushTo() Logic

Determines which BorderBlock to use for two adjacent brushes:

- **Both exist, different z-order**: lower z shows inner, higher shows outer
- **One is null**: uses zilch borders
- **Wildcard**: `to=0xFFFFFFFF` matches all brushes
- **Friends**: `TerrainBrush::friendOf()` skips borders between friendly brushes

## Optional (Mountain) Borders

- Second border layer shown when `tile->hasOptionalBorder() == true`
- Z-order set to `0x7FFFFFFF` (above all normal borders)
- `OptionalBorderBrush::draw()` → `tile->setOptionalBorder(true)`
- `use_only_optional` flag suppresses normal border when optional is shown

## XML Format

### borders.xml
```xml
<border activated="true" id="1" group="optional_group">
    <borderitem edge="n" item="4456" />    <!-- 12 directions -->
    <borderitem edge="s" item="4458" />
    <borderitem edge="e" item="4457" />
    <borderitem edge="w" item="4459" />
    <borderitem edge="cnw" item="4460" />  <!-- corners -->
    <borderitem edge="cne" item="4461" />
    <borderitem edge="csw" item="4463" />
    <borderitem edge="cse" item="4462" />
    <borderitem edge="dnw" item="4464" />  <!-- diagonals -->
    <borderitem edge="dne" item="4465" />
    <borderitem edge="dsw" item="4467" />
    <borderitem edge="dse" item="4466" />
</border>
```

### grounds.xml border references
```xml
<brush name="grass" type="ground" server_lookid="4526" z-order="1200">
    <item id="4526" chance="2500" />
    <border align="outer" id="2" />
    <border align="inner" to="none" id="1" />
    <optional id="mountain_border_id" />
    <friend name="grass 2" />
</brush>
```

## Item Flags Set by AutoBorder::load()

- `ItemFlag::AlwaysOnBottom` — borders render below other items
- `ItemFlag::IsBorder` — item is a border
- `ItemFlag::IsOptionalBorder` — for optional borders
- `ItemAttributeKey::BorderAlignment` — which direction
- `ItemAttributeKey::BorderGroup` — group ID
- `ItemAttributeKey::GroundEquivalent` — ground tile this border represents

## File Locations

| Component | Path |
|-----------|------|
| AutoBorder | `source/brushes/ground/auto_border.h/cpp` |
| GroundBrush | `source/brushes/ground/ground_brush.h/cpp` |
| GroundBrushLoader | `source/brushes/ground/ground_brush_loader.h/cpp` |
| GroundBorderCalculator | `source/brushes/ground/ground_border_calculator.h/cpp` |
| Alignment Table | `source/brushes/ground/ground_brush_arrays.cpp` |
| Border Enums | `source/brushes/brush_enums.h` |
| OptionalBorderBrush | `source/brushes/border/optional_border_brush.h/cpp` |
| Border XML | `data/1098/borders.xml` |
| Ground XML | `data/1098/grounds.xml` |
