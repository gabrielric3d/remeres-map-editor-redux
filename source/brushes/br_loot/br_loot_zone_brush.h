//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_BR_LOOT_ZONE_BRUSH_H_
#define RME_BR_LOOT_ZONE_BRUSH_H_

#include "brushes/brush.h"

// Battle Royale: paints a loot zone id onto tiles. The palette picks the zone with
// setZone(); draw() stamps it, undraw() (Ctrl) takes the tile out of any zone.
// A real tile-writing brush, like the house brush: the painting goes through the
// normal tile actions, so undo, brush size and Fill Selection come for free.
class BRLootZoneBrush : public Brush {
public:
	BRLootZoneBrush() = default;
	~BRLootZoneBrush() override = default;

	bool canDraw(BaseMap* map, const Position& position) const override;
	void draw(BaseMap* map, Tile* tile, void* parameter) override;
	void undraw(BaseMap* map, Tile* tile) override;

	bool canDrag() const override {
		return true;
	}

	int getLookID() const override {
		return 0;
	}
	std::string getName() const override {
		return "BR Loot Zone Brush";
	}

	// The zone this brush paints (0 = none; draw() is then a no-op).
	void setZone(uint32_t zone_id) {
		draw_zone_id = zone_id;
	}
	uint32_t getZone() const {
		return draw_zone_id;
	}

protected:
	uint32_t draw_zone_id = 0;
};

#endif
