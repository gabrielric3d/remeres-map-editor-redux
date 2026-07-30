//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "brushes/instance_zone/instance_zone_brush.h"

#include "map/map.h"
#include "map/tile.h"

InstanceZoneBrush::InstanceZoneBrush() {
	//
}

bool InstanceZoneBrush::canDraw(BaseMap* map, const Position& position) const {
	// Only paint tiles that actually exist and have ground, like the sound zone
	// and flag brushes. A zone over void would mark an area nobody can stand in.
	if (Tile* tile = map->getTile(position)) {
		return tile->hasGround();
	}
	return false;
}

void InstanceZoneBrush::draw(BaseMap* /*map*/, Tile* tile, void* /*parameter*/) {
	if (draw_zone_id != 0 && tile->hasGround()) {
		tile->setInstanceZoneId(draw_zone_id);
	}
}

void InstanceZoneBrush::undraw(BaseMap* /*map*/, Tile* tile) {
	tile->setInstanceZoneId(0);
}
