//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// BlackTalon: painted extent of a zone on ONE floor, in map coordinates.
//
// Shared by SoundZone and InstanceZone: both are painted tile by tile, so neither
// has a stored rectangle to read (unlike ForcedLightZone, which is from/to in a
// config file). The box is what lets the map label sit at the zone's real center.
//
// Kept PER FLOOR because a zone can span several z levels in different places -- a
// single 2D box over the union would put the label outside the painted part of
// whichever floor you happen to be looking at.
//
// The box only ever GROWS. Erasing tiles from an edge leaves it too wide, which
// just means the label sits slightly off-center -- a cheap trade to avoid
// re-walking the map on every brush stroke. Each palette exposes a "Recenter"
// button that forces a full recount.

#ifndef RME_ZONE_BOUNDS_H_
#define RME_ZONE_BOUNDS_H_

#include <map>

struct ZoneBounds {
	int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
	bool seeded = false;

	void expand(int x, int y) {
		if (!seeded) {
			min_x = max_x = x;
			min_y = max_y = y;
			seeded = true;
			return;
		}
		if (x < min_x) { min_x = x; }
		if (x > max_x) { max_x = x; }
		if (y < min_y) { min_y = y; }
		if (y > max_y) { max_y = y; }
	}
};

// Mixin with the per-floor bookkeeping, so SoundZone and InstanceZone don't each
// carry their own copy of it.
class ZoneBoundsHolder {
public:
	void expandBounds(int x, int y, int z) {
		bounds_per_floor[z].expand(x, y);
	}
	void clearBounds() {
		bounds_per_floor.clear();
	}
	const ZoneBounds* boundsForFloor(int z) const {
		auto it = bounds_per_floor.find(z);
		return (it != bounds_per_floor.end() && it->second.seeded) ? &it->second : nullptr;
	}

	std::map<int, ZoneBounds> bounds_per_floor;
};

#endif
