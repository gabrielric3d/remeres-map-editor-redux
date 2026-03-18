# RME Redux - Data Formats

## Directory Structure

```
data/
├── 1098/                  # Client version 10.98 data
│   ├── Tibia.dat          # Sprite metadata
│   ├── Tibia.spr          # Sprite data
│   ├── Tibia.otfi         # Sprite format config
│   ├── items.otb          # Item database (binary)
│   ├── items.xml          # Item definitions
│   ├── materials.xml      # Master loader (includes all below)
│   ├── borders.xml        # Border definitions
│   ├── grounds.xml        # Ground brush definitions
│   ├── walls.xml          # Wall definitions
│   ├── doodads.xml        # Doodad definitions
│   ├── tilesets.xml       # Palette collections
│   └── creatures.xml      # Creature definitions
├── clients.toml           # Client version configs
├── menubar.xml            # Menu structure
├── presets/
│   ├── dungeon/           # Dungeon generator presets
│   ├── decoration/        # Area decoration presets
│   └── structures/        # Structure presets
└── config.toml            # Editor settings (generated)
```

## clients.toml

Defines Tibia client versions and their data signatures.

```toml
[[clients]]
name = '10.98'
version = 1098
configType = 'dat_otb'
datSignature = '42A3'
sprSignature = '57BBD603'
dataDirectory = '1098'
metadataFile = 'Tibia.dat'
spritesFile = 'Tibia.spr'
otbFile = 'items.otb'
otbId = 57
otbMajor = 3
otbmVersions = [ 3 ]
extended = true
frameDurations = true
frameGroups = true
transparency = true
default = false
```

## materials.xml (Master Loader)

```xml
<materials>
    <metaitem id="80"/>    <!-- Editor-only items -->
    <metaitem id="81"/>
    <include file="borders.xml"/>
    <include file="grounds.xml"/>
    <include file="walls.xml"/>
    <include file="doodads.xml"/>
    <include file="tilesets.xml"/>
</materials>
```

## borders.xml

Defines border sprites for 12 directions per border ID.

```xml
<materials>
    <border activated="true" id="1" group="optional_group_id">
        <borderitem edge="n" item="4456" />
        <borderitem edge="s" item="4458" />
        <borderitem edge="e" item="4457" />
        <borderitem edge="w" item="4459" />
        <borderitem edge="cnw" item="4460" />   <!-- corners -->
        <borderitem edge="cne" item="4461" />
        <borderitem edge="csw" item="4463" />
        <borderitem edge="cse" item="4462" />
        <borderitem edge="dnw" item="4464" />   <!-- diagonals -->
        <borderitem edge="dne" item="4465" />
        <borderitem edge="dsw" item="4467" />
        <borderitem edge="dse" item="4466" />
    </border>
</materials>
```

**Attributes**: `id` (unique), `activated` (true/false), `group` (optional matching)
**Edge values**: n, s, e, w, cnw, cne, csw, cse, dnw, dne, dsw, dse

## grounds.xml

Defines ground/terrain brushes with items, borders, and friends.

```xml
<materials>
    <brush name="grass" type="ground" server_lookid="4526" z-order="1200" activated="true">
        <!-- Item variations with probability weight -->
        <item id="4526" chance="2500" />
        <item id="4527" chance="10" />

        <!-- Border references (id matches borders.xml) -->
        <border align="outer" id="2" />
        <border align="inner" to="none" id="1" />
        <border align="outer" to="brush_name" id="3" />

        <!-- Optional (mountain) border -->
        <optional id="mountain_border_id" />
        <optional ground_equivalent="ground_item_id">
            <borderitem edge="n" item="..." />
        </optional>

        <!-- Friendly terrains (no border between them) -->
        <friend name="grass 2" />
    </brush>
</materials>
```

**Key attributes**: `name`, `type="ground"`, `server_lookid`, `z-order` (layer priority), `activated`
**Border align**: `outer` (toward lower z) / `inner` (toward higher z)
**Border to**: specific brush name, `"all"`, or `"none"`

## walls.xml

Defines wall brushes with directional variants and door options.

```xml
<materials>
    <brush name="stone wall" type="wall" server_lookid="1050">
        <wall type="horizontal">
            <item id="1050" chance="500" />
            <door id="6252" type="normal" open="false" />
            <door id="6254" type="normal" open="true" />
            <door id="6257" type="locked" open="false" />
            <door id="6261" type="quest" open="false" />
            <door id="6265" type="magic" open="false" />
            <door id="6444" type="hatch_window" open="false" />
            <door id="1267" type="window" />
        </wall>
        <wall type="vertical">
            <item id="1049" chance="400" />
        </wall>
        <wall type="corner">
            <item id="1053" chance="1000" />
        </wall>
        <wall type="pole">
            <item id="1051" chance="1000" />
        </wall>
    </brush>
</materials>
```

**Wall types**: `horizontal`, `vertical`, `corner`, `pole`
**Door types**: `normal`, `locked`, `quest`, `magic`, `hatch_window`, `window`, `archway`

## doodads.xml

Defines decorative objects with variations and composites.

```xml
<materials>
    <!-- Simple doodad -->
    <brush name="grass tufts" type="doodad" server_lookid="6216"
           draggable="true" on_blocking="false" thickness="25/100" activated="true">
        <item id="6218" chance="45" />
        <item id="6219" chance="60" />
    </brush>

    <!-- Composite (multi-tile) doodad -->
    <brush name="broken palm trees" type="doodad" server_lookid="8792"
           draggable="true" thickness="12/100" activated="true">
        <item id="8792" chance="10" />
        <composite chance="10">
            <tile x="0" y="0">
                <item id="8793" />
            </tile>
            <tile x="-1" y="0">
                <item id="8794" />
            </tile>
        </composite>
    </brush>
</materials>
```

**Attributes**: `draggable`, `on_blocking`, `thickness` (format: N/100), `on_duplicate`, `do_new_borders`
**Composite**: multi-tile structures with relative `x`, `y` coordinates

## tilesets.xml

Organizes brushes into named palette collections.

```xml
<materials>
    <tileset name="Nature">
        <!-- Terrain brushes (defined in grounds.xml) -->
        <terrain>
            <brush name="grass"/>
            <brush name="water"/>
        </terrain>

        <!-- Doodad brushes (defined in doodads.xml) -->
        <doodad>
            <brush name="green trees"/>
        </doodad>
    </tileset>

    <tileset name="Grounds">
        <!-- Raw items (by item ID) -->
        <raw>
            <item id="100"/>
            <item fromid="4609" toid="4625"/>
        </raw>
    </tileset>
</materials>
```

**Sections**: `<terrain>`, `<doodad>`, `<raw>` — each contains `<brush name="..."/>` or `<item id="..."/>`

## items.xml

Defines item properties (not sprite data — that's in .otb/.dat).

```xml
<items>
    <item id="100" name="void" />
    <item id="293" name="grass">
        <attribute key="floorchange" value="down" />
    </item>
    <item fromid="351" toid="353" name="dirt floor" />
    <item id="384" name="dirt floor">
        <attribute key="description" value="There is a hole." pt="Há um buraco." />
    </item>
</items>
```

**Common attributes**: `floorchange`, `decayTo`, `duration`, `fluidsource`, `walkStack`, `description`

## Dungeon Preset Format (`data/presets/dungeon/*.xml`)

```xml
<dungeon_preset name="Sand Dungeon" version="1.0">
    <terrain ground="17625" patch="231" fill="21197" brush="8033" />
    <walls north="8476" south="8476" east="8475" west="8475"
           nw="8479" ne="0" sw="0" se="0" pillar="8477" />
    <borders north="10625" south="10626" east="10628" west="10627"
             nw="10631" ne="10632" sw="10630" se="10629"
             inner_nw="10634" inner_ne="10633" inner_sw="10636" inner_se="10635" />
    <brush_borders north="8034" south="8036" east="8035" west="8037"
                   nw="8041" ne="8038" sw="8040" se="8039"
                   inner_nw="8044" inner_ne="8045" inner_sw="8043" inner_se="8042" />
    <details>
        <group chance="0.05" placement="anywhere">
            <item id="2234" />
        </group>
        <group chance="0.03" placement="north_wall">
            <item id="1450" />
        </group>
    </details>
    <hangables chance="0.15" enable_vertical="true">
        <horizontal id="5058" />
        <vertical id="5060" />
    </hangables>
</dungeon_preset>
```

## config.toml (Editor Settings)

Auto-generated. Structure:
```toml
[editor]
auto_assign_doorid = true
default_client_version = 57
ground_replace_modifier = 'Alt'

[graphics]
anti_aliasing = 0
frame_rate_limit = 144
screen_shader = '4xBRZ'

[view]
show_all_floors = true
show_grid = false

[window]
window_width = 2576
window_maximized = true
```

## menubar.xml

```xml
<menubar>
    <menu name="File">
        <item name="New..." hotkey="Ctrl+N" action="NEW" help="Create a new map." />
        <separator/>
        <menu name="Import">
            <item name="Import Map..." action="IMPORT_MAP" />
        </menu>
        <menu name="Recent Files" special="RECENT_FILES"/>
    </menu>
</menubar>
```

**Elements**: `<menu name>`, `<item name action hotkey help>`, `<separator/>`
