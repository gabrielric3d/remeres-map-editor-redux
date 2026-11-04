//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_MAP_MAP_STACK_H_
#define RME_MAP_MAP_STACK_H_

#include "map/basemap.h"

#include <vector>

class Tile;

// Several maps read as one (map layers, see MapLayers): at any position the tile of the
// last map that has one wins, the same order the map view draws them in.
class MapStack {
public:
	MapStack() = default;
	explicit MapStack(const BaseMap& map) {
		add(map);
	}

	void add(const BaseMap& map) {
		maps_.push_back(&map);
	}

	[[nodiscard]] const Tile* getTile(int x, int y, int z) const {
		for (auto it = maps_.rbegin(); it != maps_.rend(); ++it) {
			if (const Tile* tile = (*it)->getTile(x, y, z)) {
				return tile;
			}
		}
		return nullptr;
	}

	[[nodiscard]] const Tile* getTile(const Position& pos) const {
		return getTile(pos.x, pos.y, pos.z);
	}

	// Bottom to top, for walks that need every map (lights).
	[[nodiscard]] const std::vector<const BaseMap*>& maps() const {
		return maps_;
	}

private:
	std::vector<const BaseMap*> maps_;
};

#endif // RME_MAP_MAP_STACK_H_
