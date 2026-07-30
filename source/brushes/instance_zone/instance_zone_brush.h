//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_INSTANCE_ZONE_BRUSH_H_
#define RME_INSTANCE_ZONE_BRUSH_H_

#include "brushes/brush.h"

// BlackTalon: paints an instance zone id onto tiles. The palette sets which zone
// is active via setInstanceZone(); draw() stamps that id, undraw() clears it.
// Modeled on SoundZoneBrush/HouseBrush (a real tile-writing brush, unlike the
// waypoint/camera "virtual" brushes). Each zone renders in its own color (hash of
// the id) via TileColorCalculator, so painted areas are visually distinct.
class InstanceZoneBrush : public Brush {
public:
	InstanceZoneBrush();
	~InstanceZoneBrush() override = default;

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
		return "Instance Zone Brush";
	}

	// The zone this brush will paint (0 = none; draw() is a no-op when 0).
	void setInstanceZone(uint32_t zoneId) {
		draw_zone_id = zoneId;
	}
	uint32_t getInstanceZone() const {
		return draw_zone_id;
	}

protected:
	uint32_t draw_zone_id = 0;
};

#endif
