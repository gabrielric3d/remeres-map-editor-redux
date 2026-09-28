//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "map/tile_operations.h"
#include "app/main.h"
#include "rendering/ui/brush_selector.h"
#include "ui/gui.h"
#include "brushes/managers/brush_manager.h"
#include "editor/editor.h"
#include "map/tile.h"
#include "game/item.h"
#include "brushes/brush.h"
#include "brushes/ground/ground_brush.h"
#include "brushes/wall/wall_brush.h"
#include "brushes/carpet/carpet_brush.h"
#include "brushes/table/table_brush.h"
#include "brushes/raw/raw_brush.h"
#include "brushes/house/house_brush.h"
#include "brushes/spawn/spawn_brush.h"
#include "brushes/creature/creature_brush.h"
#include "brushes/door/door_brush.h"
#include "brushes/br_loot/br_loot_item_brush.h"
#include "brushes/br_loot/br_loot_zone_brush.h"
#include "game/br_loot_zones.h"
#include "palette/palette_br_loot.h"
#include "palette/palette_window.h"

void BrushSelector::SelectRAWBrush(Selection& selection) {
	if (selection.size() != 1) {
		return;
	}
	Tile* tile = selection.getSelectedTile();
	if (!tile) {
		return;
	}
	Item* item = TileOperations::getTopSelectedItem(tile);

	if (item && item->getRAWBrush()) {
		const auto sizeState = g_brush_manager.GetBrushSizeState();
		g_gui.SelectBrush(item->getRAWBrush(), TILESET_RAW);
		g_gui.RestoreBrushSizeState(sizeState);
	}
}

void BrushSelector::SelectGroundBrush(Selection& selection) {
	if (selection.size() != 1) {
		return;
	}
	Tile* tile = selection.getSelectedTile();
	if (!tile) {
		return;
	}
	GroundBrush* bb = tile->getGroundBrush();

	if (bb) {
		const auto sizeState = g_brush_manager.GetBrushSizeState();
		g_gui.SelectBrush(bb, TILESET_TERRAIN);
		g_gui.RestoreBrushSizeState(sizeState);
	}
}

void BrushSelector::SelectDoodadBrush(Selection& selection) {
	if (selection.size() != 1) {
		return;
	}
	Tile* tile = selection.getSelectedTile();
	if (!tile) {
		return;
	}
	Item* item = TileOperations::getTopSelectedItem(tile);

	if (item) {
		const auto sizeState = g_brush_manager.GetBrushSizeState();
		g_gui.SelectBrush(item->getDoodadBrush(), TILESET_DOODAD);
		g_gui.RestoreBrushSizeState(sizeState);
	}
}

void BrushSelector::SelectDoorBrush(Selection& selection) {
	if (selection.size() != 1) {
		return;
	}
	Tile* tile = selection.getSelectedTile();
	if (!tile) {
		return;
	}
	Item* item = TileOperations::getTopSelectedItem(tile);

	if (item) {
		const auto sizeState = g_brush_manager.GetBrushSizeState();
		g_gui.SelectBrush(item->getDoorBrush(), TILESET_TERRAIN);
		g_gui.RestoreBrushSizeState(sizeState);
	}
}

void BrushSelector::SelectWallBrush(Selection& selection) {
	if (selection.size() != 1) {
		return;
	}
	Tile* tile = selection.getSelectedTile();
	if (!tile) {
		return;
	}
	Item* wall = tile->getWall();
	WallBrush* wb = wall->getWallBrush();

	if (wb) {
		const auto sizeState = g_brush_manager.GetBrushSizeState();
		g_gui.SelectBrush(wb, TILESET_TERRAIN);
		g_gui.RestoreBrushSizeState(sizeState);
	}
}

void BrushSelector::SelectCarpetBrush(Selection& selection) {
	if (selection.size() != 1) {
		return;
	}
	Tile* tile = selection.getSelectedTile();
	if (!tile) {
		return;
	}
	Item* wall = tile->getCarpet();
	CarpetBrush* cb = wall->getCarpetBrush();

	if (cb) {
		const auto sizeState = g_brush_manager.GetBrushSizeState();
		g_gui.SelectBrush(cb);
		g_gui.RestoreBrushSizeState(sizeState);
	}
}

void BrushSelector::SelectTableBrush(Selection& selection) {
	if (selection.size() != 1) {
		return;
	}
	Tile* tile = selection.getSelectedTile();
	if (!tile) {
		return;
	}
	Item* wall = tile->getTable();
	TableBrush* tb = wall->getTableBrush();

	if (tb) {
		const auto sizeState = g_brush_manager.GetBrushSizeState();
		g_gui.SelectBrush(tb);
		g_gui.RestoreBrushSizeState(sizeState);
	}
}

void BrushSelector::SelectHouseBrush(Editor& editor, Selection& selection) {
	Tile* tile = selection.getSelectedTile();
	if (!tile) {
		return;
	}

	if (tile->isHouseTile()) {
		House* house = editor.map.houses.getHouse(tile->getHouseID());
		if (house) {
			g_brush_manager.house_brush->setHouse(house);
			const auto sizeState = g_brush_manager.GetBrushSizeState();
			g_gui.SelectBrush(g_brush_manager.house_brush, TILESET_HOUSE);
			g_gui.RestoreBrushSizeState(sizeState);
		}
	}
}

void BrushSelector::SelectCollectionBrush(Selection& selection) {
	Tile* tile = selection.getSelectedTile();
	if (!tile) {
		return;
	}

	const auto sizeState = g_brush_manager.GetBrushSizeState();
	for (const auto& item : tile->items) {
		if (item->isWall()) {
			WallBrush* wb = item->getWallBrush();
			if (wb && wb->visibleInPalette() && wb->hasCollection()) {
				g_gui.SelectBrush(wb, TILESET_COLLECTION);
				g_gui.RestoreBrushSizeState(sizeState);
				return;
			}
		}
		if (item->isTable()) {
			TableBrush* tb = item->getTableBrush();
			if (tb && tb->visibleInPalette() && tb->hasCollection()) {
				g_gui.SelectBrush(tb, TILESET_COLLECTION);
				g_gui.RestoreBrushSizeState(sizeState);
				return;
			}
		}
		if (item->isCarpet()) {
			CarpetBrush* cb = item->getCarpetBrush();
			if (cb && cb->visibleInPalette() && cb->hasCollection()) {
				g_gui.SelectBrush(cb, TILESET_COLLECTION);
				g_gui.RestoreBrushSizeState(sizeState);
				return;
			}
		}
		if (Brush* db = item->getDoodadBrush()) {
			if (db && db->visibleInPalette() && db->hasCollection()) {
				g_gui.SelectBrush(db, TILESET_COLLECTION);
				g_gui.RestoreBrushSizeState(sizeState);
				return;
			}
		}
		if (item->isSelected()) {
			RAWBrush* rb = item->getRAWBrush();
			if (rb && rb->hasCollection()) {
				g_gui.SelectBrush(rb, TILESET_COLLECTION);
				g_gui.RestoreBrushSizeState(sizeState);
				return;
			}
		}
	}
	GroundBrush* gb = tile->getGroundBrush();
	if (gb && gb->visibleInPalette() && gb->hasCollection()) {
		g_gui.SelectBrush(gb, TILESET_COLLECTION);
		g_gui.RestoreBrushSizeState(sizeState);
		return;
	}
}

void BrushSelector::SelectCreatureBrush(Selection& selection) {
	Tile* tile = selection.getSelectedTile();
	if (!tile) {
		return;
	}

	if (tile->creature) {
		const auto sizeState = g_brush_manager.GetBrushSizeState();
		g_gui.SelectBrush(tile->creature->getBrush(), TILESET_CREATURE);
		g_gui.RestoreBrushSizeState(sizeState);
	}
}

void BrushSelector::SelectSpawnBrush() {
	const auto sizeState = g_brush_manager.GetBrushSizeState();
	g_gui.SelectBrush(g_brush_manager.spawn_brush, TILESET_CREATURE);
	g_gui.RestoreBrushSizeState(sizeState);
}

void BrushSelector::SelectSmartBrush(Editor& editor, Tile* tile) {
	// Battle Royale: with a loot brush in hand the picker picks what that brush paints
	// -- the zone of the tile, or the item placed by hand on it -- and never trades
	// the loot brush for a RAW one.
	if (Brush* current = g_gui.GetCurrentBrush()) {
		if (current->is<BRLootZoneBrush>()) {
			SelectBRLootZone(editor, tile);
			return;
		}
		if (current->is<BRLootItemBrush>()) {
			SelectBRLootItem(editor, tile);
			return;
		}
	}

	if (tile && tile->size() > 0) {
		// Select visible creature
		if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
			CreatureBrush* brush = tile->creature->getBrush();
			if (brush) {
				const auto sizeState = g_brush_manager.GetBrushSizeState();
				g_gui.SelectBrush(brush, TILESET_CREATURE);
				g_gui.RestoreBrushSizeState(sizeState);
				return;
			}
		}
		// Fall back to item selection
		Item* item = tile->getTopItem();
		if (item && item->getRAWBrush()) {
			const auto sizeState = g_brush_manager.GetBrushSizeState();
			g_gui.SelectBrush(item->getRAWBrush(), TILESET_RAW);
			g_gui.RestoreBrushSizeState(sizeState);
		}
	}
}

// Battle Royale: the way to change a zone straight from the map. Its row is picked in
// the BR Loot Zones page and the brush paints it: paint to grow it, Ctrl to shrink
// it, Set tier to retier it.
bool BrushSelector::SelectBRLootZone(Editor& editor, Tile* tile) {
	const uint32_t id = tile ? tile->getBRLootZoneId() : 0;
	const BRLootZone* zone = editor.map.br_loot_zones.getZone(id);
	if (!zone) {
		if (id != 0) {
			g_gui.SetStatusText(wxString::Format("Loot zone %u is not in this map's table yet: saving the map adopts it as tier 1.", static_cast<unsigned>(id)));
		} else {
			g_gui.SetStatusText("No loot zone on this tile.");
		}
		return false;
	}
	const int tier = zone->tier;

	// Picked to be changed, so it has to be seen: the zones overlay comes on, the way
	// placing a creature turns the spawns on.
	if (!g_settings.getBoolean(Config::SHOW_BR_LOOT_ZONES)) {
		g_settings.setInteger(Config::SHOW_BR_LOOT_ZONES, 1);
		g_gui.UpdateMenubar();
	}

	const auto sizeState = g_brush_manager.GetBrushSizeState();
	// The page first: switching pages asks the page for its brush, and the pick below
	// has to be the last word.
	g_gui.SelectPalettePage(TILESET_BR_LOOT_ZONE);
	PaletteWindow* palette = g_gui.GetPalette();
	BRLootZonePalettePanel* panel = palette ? palette->GetBRLootZonePalette() : nullptr;
	if (!panel || !panel->PickZone(id)) {
		g_gui.RestoreBrushSizeState(sizeState);
		return false;
	}
	// The two-argument form: the brush already carries the zone, and the no-argument
	// one would hand over the house brush if a house is picked in the house palette.
	g_gui.SelectBrush(g_brush_manager.br_loot_zone_brush, TILESET_BR_LOOT_ZONE);
	g_gui.RestoreBrushSizeState(sizeState);
	g_gui.SetStatusText(wxString::Format("Loot zone %d-%u picked: paint to add tiles, Ctrl+click takes them out, Set tier changes its tier.", tier, static_cast<unsigned>(id)));
	return true;
}

// Battle Royale: the item placed by hand on this tile is loaded into the loot item
// brush, like a click on its row: clicking the tile again rewrites it.
bool BrushSelector::SelectBRLootItem(Editor& editor, Tile* tile) {
	const BRLootItem* item = tile ? editor.map.br_loot_zones.itemAt(tile->getPosition()) : nullptr;
	if (!item) {
		g_gui.SetStatusText("No loot item placed by hand on this tile.");
		return false;
	}
	const std::string name = item->name;
	const Position pos = item->pos;

	const auto sizeState = g_brush_manager.GetBrushSizeState();
	g_gui.SelectPalettePage(TILESET_BR_LOOT_ITEM);
	PaletteWindow* palette = g_gui.GetPalette();
	BRLootItemPalettePanel* panel = palette ? palette->GetBRLootItemPalette() : nullptr;
	if (!panel || !panel->PickPlaced(pos)) {
		g_gui.RestoreBrushSizeState(sizeState);
		return false;
	}
	g_gui.SelectBrush(g_brush_manager.br_loot_item_brush, TILESET_BR_LOOT_ITEM);
	g_gui.RestoreBrushSizeState(sizeState);
	g_gui.SetStatusText("Loot item " + wxstr(name) + " picked: change it in the palette and click its tile to rewrite it.");
	return true;
}
