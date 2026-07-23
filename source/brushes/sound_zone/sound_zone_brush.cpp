//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "brushes/sound_zone/sound_zone_brush.h"

#include "map/map.h"
#include "map/tile.h"

SoundZoneBrush::SoundZoneBrush() {
	//
}

bool SoundZoneBrush::canDraw(BaseMap* map, const Position& position) const {
	// Only paint tiles that actually exist and have ground, like the flag brush.
	if (Tile* tile = map->getTile(position)) {
		return tile->hasGround();
	}
	return false;
}

void SoundZoneBrush::draw(BaseMap* /*map*/, Tile* tile, void* /*parameter*/) {
	if (draw_zone_id != 0 && tile->hasGround()) {
		tile->setSoundZoneId(draw_zone_id);
	}
}

void SoundZoneBrush::undraw(BaseMap* /*map*/, Tile* tile) {
	tile->setSoundZoneId(0);
}
