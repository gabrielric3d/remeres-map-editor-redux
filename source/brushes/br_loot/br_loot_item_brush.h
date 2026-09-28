//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_BR_LOOT_ITEM_BRUSH_H_
#define RME_BR_LOOT_ITEM_BRUSH_H_

#include "brushes/brush.h"
#include "game/br_loot_zones.h"

#include <string>

// Battle Royale: places one loot item by hand on a tile (click), or takes it off
// (Ctrl+click). It does NOT write the tile: the placed item is a line of the loot
// table (BRLootZones), and the server rolls it every match -- a real map item would
// be on the island from boot and never be cleared. So the click is handled in
// DrawOperations::draw, which turns it into an undoable table change; draw() and
// undraw() here are never meant to change a tile.
class BRLootItemBrush : public Brush {
public:
	BRLootItemBrush() = default;
	~BRLootItemBrush() override = default;

	bool canDraw(BaseMap* map, const Position& position) const override;
	void draw(BaseMap* map, Tile* tile, void* parameter) override { }
	void undraw(BaseMap* map, Tile* tile) override { }

	// One tile per click, whatever the brush size, and no smearing: dragging over a
	// room must not scatter a copy on every tile.
	bool oneSizeFitsAll() const override {
		return true;
	}
	bool canSmear() const override {
		return false;
	}

	int getLookID() const override {
		return 0;
	}
	std::string getName() const override {
		return "BR Loot Item Brush";
	}

	// What the next click places. `server_id` only draws the preview; the table
	// stores the catalog name.
	void setItem(const std::string& name, uint16_t server_id) {
		item_name = name;
		item_server_id = server_id;
	}
	void setAmounts(int min_count, int max_count, int chance_percent);

	const std::string& getItemName() const {
		return item_name;
	}
	uint16_t getServerId() const {
		return item_server_id;
	}
	BRLootItem makeItem(const Position& pos) const;

protected:
	std::string item_name;
	uint16_t item_server_id = 0;
	int min = 1;
	int max = 1;
	int chance = 100;
};

#endif
