# RME Redux - Brush System

## Base Class (`source/brushes/brush.h`)

```cpp
class Brush {
    uint32_t id;
    bool visible;

    // Pure virtual
    virtual void draw(BaseMap* map, Tile* tile, void* parameter = nullptr) = 0;
    virtual void undraw(BaseMap* map, Tile* tile) = 0;
    virtual bool canDraw(BaseMap* map, const Position& position) const = 0;
    virtual std::string getName() const = 0;
    virtual int getLookID() const = 0;

    // Optional virtual
    virtual bool load(pugi::xml_node node, std::vector<std::string>& warnings);
    virtual bool needBorders() const { return false; }
    virtual bool canDrag() const { return false; }
    virtual bool canSmear() const { return true; }
    virtual int32_t getMaxVariation() const { return 0; }

    // Type query/conversion templates
    template <typename T> T* as();
    template <typename T> const T* as() const;
    template <typename T> bool is() const;
};
```

### TerrainBrush (intermediate base)
```cpp
class TerrainBrush : public Brush {
    std::string name;
    uint16_t look_id;
    std::vector<uint32_t> friends;

    bool friendOf(TerrainBrush* other) const;
    int32_t getZ() const;
};
```

## All Brush Types

| Brush | Base | File | XML Load | Purpose |
|-------|------|------|----------|---------|
| `GroundBrush` | TerrainBrush | `brushes/ground/ground_brush.h` | Yes | Terrain with auto-borders |
| `WallBrush` | TerrainBrush | `brushes/wall/wall_brush.h` | Yes | Walls, 17 alignments |
| `WallDecorationBrush` | WallBrush | `brushes/wall/wall_brush.h` | Yes | Decorations on walls |
| `RAWBrush` | Brush | `brushes/raw/raw_brush.h` | No | Single item by ID |
| `DoodadBrush` | Brush | `brushes/doodad/doodad_brush.h` | Yes | Decorations with variations |
| `TableBrush` | Brush | `brushes/table/table_brush.h` | Yes | Tables, 7 alignments |
| `CarpetBrush` | Brush | `brushes/carpet/carpet_brush.h` | Yes | Carpets, 14 alignments |
| `DoorBrush` | Brush | `brushes/door/door_brush.h` | No | Door placement (8 types) |
| `CreatureBrush` | Brush | `brushes/creature/creature_brush.h` | No | Creature placement |
| `SpawnBrush` | Brush | `brushes/spawn/spawn_brush.h` | No | Spawn area marker |
| `HouseBrush` | Brush | `brushes/house/house_brush.h` | No | House floor marker |
| `HouseExitBrush` | Brush | `brushes/house/house_exit_brush.h` | No | House exit marker |
| `WaypointBrush` | Brush | `brushes/waypoint/waypoint_brush.h` | No | Waypoint marker |
| `CameraPathBrush` | Brush | `brushes/camera/camera_path_brush.h` | No | Camera path marker |
| `FlagBrush` | Brush | `brushes/flag/flag_brush.h` | No | Tile flags (PZ, PvP, etc.) |
| `EraserBrush` | Brush | `brushes/brush.h` | No | Removes items/tiles |
| `OptionalBorderBrush` | Brush | `brushes/border/optional_border_brush.h` | No | Optional border toggle |

## Brush Registration

### Brushes Registry (`g_brushes` in `source/brushes/brush.h`)
```cpp
class Brushes {
    // Storage: multimap<string, unique_ptr<Brush>>
    void addBrush(std::unique_ptr<Brush> brush);
    Brush* getBrush(std::string_view name);
    bool unserializeBrush(pugi::xml_node node, ...);  // XML → Brush
    bool unserializeBorder(pugi::xml_node node, ...);  // XML → Border
};
extern Brushes g_brushes;
```

### BrushManager (`g_brush_manager` in `source/brushes/managers/brush_manager.h`)
Manages current/previous brush selection and holds references to special brushes (door types, flag types, eraser, spawn, house, waypoint, camera, optional border).

### XML Type Mapping
```
"border"          → GroundBrush (border loading)
"ground"          → GroundBrush
"wall"            → WallBrush
"wall decoration" → WallDecorationBrush
"carpet"          → CarpetBrush
"table"           → TableBrush
"doodad"          → DoodadBrush
```

## Drawing Cycle

1. `canDraw(map, position)` → check if brush can draw here
2. `draw(map, tile, parameter)` → apply brush to tile
3. For terrain brushes: `doBorders(map, tile)` adjusts surrounding tiles

### draw() Parameter by Brush Type
| Brush | Parameter Type | Purpose |
|-------|---------------|---------|
| GroundBrush | `pair<bool, GroundBrush*>*` | volatility + other brush |
| WallBrush | `bool*` | shift alignment |
| RAWBrush | `bool*` | SimOne mode |
| DoodadBrush | `int*` | variation index |

## Per-Brush Loaders

| Loader | File |
|--------|------|
| `GroundBrushLoader` | `brushes/ground/ground_brush_loader.h/cpp` |
| `WallBrushLoader` | `brushes/wall/wall_brush_loader.h/cpp` |
| `DoodadBrushLoader` | `brushes/doodad/doodad_brush_loader.h/cpp` |
| `TableBrushLoader` | `brushes/table/table_brush_loader.h/cpp` |
| `CarpetBrushLoader` | `brushes/carpet/carpet_brush_loader.h/cpp` |

## DoodadBrush Details

```cpp
struct DoodadBrushItems {
    struct AlternativeBlock {
        std::vector<SingleBlock> single_items;    // {chance, unique_ptr<Item>}
        std::vector<CompositeBlock> composite_items; // {chance, CompositeTileList}
    };
    std::vector<std::unique_ptr<AlternativeBlock>> alternatives;
};
```

- Each alternative = a variation (different look per selection)
- Single items: one item per tile
- Composite items: multi-tile structures with relative positions

## Border Calculators

| Calculator | File | Purpose |
|-----------|------|---------|
| `GroundBorderCalculator` | `brushes/ground/ground_border_calculator.h/cpp` | Ground borders |
| `WallBorderCalculator` | `brushes/wall/wall_border_calculator.h/cpp` | Wall alignment |
| `TableBorderCalculator` | `brushes/table/table_border_calculator.h/cpp` | Table alignment |
| `CarpetBorderCalculator` | `brushes/carpet/carpet_border_calculator.h/cpp` | Carpet alignment |

Each uses a static 256-entry lookup table mapping neighbor bitmask → border directions.

## File Structure

```
source/brushes/
├── brush.h/cpp           (base + Brushes registry + EraserBrush)
├── brush_enums.h         (BorderType, DoorType, BrushShape enums)
├── managers/brush_manager.h/cpp
├── ground/               (GroundBrush, AutoBorder, Calculator, Loader, Arrays)
├── wall/                 (WallBrush, WallDecorationBrush, Calculator, Loader)
├── doodad/               (DoodadBrush, Items, Types, Loader, PreviewManager)
├── raw/                  (RAWBrush)
├── door/                 (DoorBrush)
├── carpet/               (CarpetBrush, Calculator, Loader, Arrays)
├── table/                (TableBrush, Calculator, Loader)
├── creature/             (CreatureBrush)
├── spawn/                (SpawnBrush)
├── house/                (HouseBrush, HouseExitBrush)
├── waypoint/             (WaypointBrush)
├── camera/               (CameraPathBrush)
├── flag/                 (FlagBrush)
├── border/               (OptionalBorderBrush)
└── eraser/               (eraser_brush.cpp)
```
