//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "brushes/br_loot/br_loot_zone_brush.h"

#include "map/basemap.h"
#include "map/tile.h"

bool BRLootZoneBrush::canDraw(BaseMap* map, const Position& position) const {
	// Only tiles that exist and have ground: loot is dropped on the floor, and a zone
	// over void would be a zone nothing can land in.
	if (Tile* tile = map->getTile(position)) {
		return tile->hasGround();
	}
	return false;
}

void BRLootZoneBrush::draw(BaseMap* /*map*/, Tile* tile, void* /*parameter*/) {
	if (draw_zone_id != 0 && tile->hasGround()) {
		tile->setBRLootZoneId(draw_zone_id);
	}
}

void BRLootZoneBrush::undraw(BaseMap* /*map*/, Tile* tile) {
	tile->setBRLootZoneId(0);
}
