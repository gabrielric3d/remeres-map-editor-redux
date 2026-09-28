//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "brushes/br_loot/br_loot_item_brush.h"

#include "map/basemap.h"
#include "map/tile.h"

#include <algorithm>

bool BRLootItemBrush::canDraw(BaseMap* map, const Position& position) const {
	if (item_name.empty()) {
		return false;
	}
	if (Tile* tile = map->getTile(position)) {
		return tile->hasGround();
	}
	return false;
}

void BRLootItemBrush::setAmounts(int min_count, int max_count, int chance_percent) {
	min = std::clamp(min_count, 1, 100);
	max = std::clamp(max_count, min, 100);
	chance = std::clamp(chance_percent, 1, 100);
}

BRLootItem BRLootItemBrush::makeItem(const Position& pos) const {
	BRLootItem item;
	item.pos = pos;
	item.name = item_name;
	item.min = min;
	item.max = max;
	item.chance = chance;
	return item;
}
