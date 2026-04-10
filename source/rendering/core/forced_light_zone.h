//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_CORE_FORCED_LIGHT_ZONE_H_
#define RME_RENDERING_CORE_FORCED_LIGHT_ZONE_H_

#include <string>
#include <vector>
#include <cstdint>
#include "map/position.h"

struct ForcedLightZone {
	std::string name;

	// Rectangular zone (fromPos + toPos)
	bool isRectangular() const { return !isCircular_; }
	bool isCircular() const { return isCircular_; }

	// Rectangular fields
	Position fromPos;
	Position toPos;

	// Circular fields
	Position center;
	int radius = 0;
	int floor = 0;

	// Light settings
	uint8_t ambient = 0;       // 0-255, lower = darker
	uint8_t ambientColor = 0;  // 8-bit color index

	// Check if a position is inside this zone
	bool contains(const Position& pos) const;

	// Get bounding box (for rendering/culling)
	Position getBoundsMin() const;
	Position getBoundsMax() const;

	// Set as circular zone
	void setCircular(bool circular) { isCircular_ = circular; }

private:
	bool isCircular_ = false;
};

class ForcedLightZoneManager {
public:
	static ForcedLightZoneManager& instance();

	void load(const std::string& filepath);
	void clear();

	const std::vector<ForcedLightZone>& getZones() const;

	// Find the zone at a given position (nullptr if none)
	const ForcedLightZone* getZoneAt(const Position& pos) const;

	// Get all zones that overlap with a rectangular viewport area on a given floor
	std::vector<const ForcedLightZone*> getZonesInArea(
		int minX, int minY, int maxX, int maxY, int floor) const;

	bool isLoaded() const { return loaded_; }

private:
	ForcedLightZoneManager() = default;
	std::vector<ForcedLightZone> zones_;
	bool loaded_ = false;
};

#endif
