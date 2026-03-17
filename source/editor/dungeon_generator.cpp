//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "editor/dungeon_generator.h"
#include "editor/editor.h"
#include "editor/action.h"
#include "editor/action_queue.h"
#include "map/map.h"
#include "map/tile.h"
#include "map/tile_operations.h"
#include "game/item.h"
#include "ui/gui.h"
#include "util/file_system.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <wx/dir.h>
#include <wx/filename.h>

namespace DungeonGen {

//=============================================================================
// DetailGroup helpers
//=============================================================================

DetailGroup::Placement DetailGroup::placementFromString(const std::string& str) {
	if (str == "near_wall") return NearWall;
	if (str == "north_wall") return NorthWall;
	if (str == "west_wall") return WestWall;
	if (str == "center") return Center;
	return Anywhere;
}

std::string DetailGroup::placementToString(Placement p) {
	switch (p) {
		case NearWall: return "near_wall";
		case NorthWall: return "north_wall";
		case WestWall: return "west_wall";
		case Center: return "center";
		default: return "anywhere";
	}
}

//=============================================================================
// DungeonPreset validation
//=============================================================================

bool DungeonPreset::validate(std::string& errorOut) const {
	if (groundId == 0) {
		errorOut = "Preset missing ground tile ID";
		return false;
	}
	return true;
}

//=============================================================================
// DungeonGenerator
//=============================================================================

DungeonGenerator::DungeonGenerator(Editor* editor)
	: m_editor(editor) {
}

uint64_t DungeonGenerator::posHash(int x, int y) {
	return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
	       static_cast<uint64_t>(static_cast<uint32_t>(y));
}

uint16_t DungeonGenerator::getGridTile(int x, int y) const {
	auto it = m_grid.find(posHash(x, y));
	return it != m_grid.end() ? it->second : 0;
}

void DungeonGenerator::setGridTile(int x, int y, uint16_t id) {
	m_grid[posHash(x, y)] = id;
}

bool DungeonGenerator::isFloorTile(int x, int y, uint16_t floorId) const {
	return getGridTile(x, y) == floorId;
}

void DungeonGenerator::paintFloor(int x, int y, int z, uint16_t floorId,
                                   std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	setGridTile(x, y, floorId);
	m_floorPositions.push_back(Position(x, y, z));
	tileChanges.push_back({Position(x, y, z), floorId});
}

//=============================================================================
// Main generate entry point
//=============================================================================

bool DungeonGenerator::generate(const DungeonConfig& config, const DungeonPreset& preset) {
	m_grid.clear();
	m_floorPositions.clear();
	m_wallPlacements.clear();
	m_tilesGenerated = 0;
	m_lastError.clear();

	if (!m_editor) {
		m_lastError = "No editor available";
		return false;
	}

	std::string validationError;
	if (!preset.validate(validationError)) {
		m_lastError = "Invalid preset: " + validationError;
		return false;
	}

	int width = std::max(10, config.width);
	int height = std::max(10, config.height);
	int startX = config.center.x;
	int startY = config.center.y;
	int z = config.center.z;
	int pathWidth = std::max(2, config.pathWidth);

	// Seed RNG
	if (config.seed != 0) {
		m_rng.seed(static_cast<unsigned int>(config.seed));
	} else {
		std::random_device rd;
		m_rng.seed(rd());
	}

	uint16_t floorId = preset.groundId;

	// Collect all tile changes before applying
	std::vector<std::pair<Position, uint16_t>> tileChanges;
	tileChanges.reserve(static_cast<size_t>(width) * height);

	// Step 1: Fill background
	fillBackground(startX, startY, width, height, z, preset.fillId, tileChanges);

	// Step 2-3: Generate and order waypoints
	auto waypoints = generateWaypoints(startX, startY, width, height);
	waypoints = orderWaypoints(std::move(waypoints));

	if (waypoints.empty()) {
		m_lastError = "Failed to generate waypoints - area may be too small";
		return false;
	}

	// Step 4: Carve rooms
	carveRooms(waypoints, z, floorId, tileChanges);

	// Step 5: Carve corridors
	carveCorridors(waypoints, z, floorId, pathWidth, tileChanges);

	// Step 6: Build walls
	if (preset.walls.isValid()) {
		buildWalls(startX, startY, width, height, z, floorId, preset.walls, tileChanges);

		// Step 7: Hangable details on walls
		if (preset.hangables.isValid()) {
			createHangableDetails(z, preset.hangables, tileChanges);
		}

		// Mark wall positions in grid so decorators avoid them
		for (const auto& wall : m_wallPlacements) {
			setGridTile(wall.pos.x, wall.pos.y, 0xFFFF); // sentinel: wall marker
		}
	}

	// Step 8: Apply patches
	applyPatches(z, preset, tileChanges);

	// Step 9: Decorate floors
	decorateFloors(z, preset, tileChanges);

	// Apply all changes to the map via action system
	if (!applyChanges(tileChanges)) {
		return false;
	}

	m_tilesGenerated = static_cast<int>(tileChanges.size());
	return true;
}

//=============================================================================
// Step 1: Fill background
//=============================================================================

void DungeonGenerator::fillBackground(int startX, int startY, int width, int height, int z,
                                       uint16_t fillId,
                                       std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	if (fillId == 0) return;

	int halfW = width / 2;
	int halfH = height / 2;

	for (int y = 0; y <= height; ++y) {
		for (int x = 0; x <= width; ++x) {
			int absX = startX + x - halfW;
			int absY = startY + y - halfH;
			tileChanges.push_back({Position(absX, absY, z), fillId});
		}
	}
}

//=============================================================================
// Step 2: Generate waypoints (room centers)
//=============================================================================

std::vector<DungeonGenerator::Waypoint> DungeonGenerator::generateWaypoints(
	int startX, int startY, int width, int height) {

	std::vector<Waypoint> waypoints;
	const int numRooms = 18;
	const int minDist = 11;

	int minW = -width / 2 + 2;
	int maxW = width / 2 - 2;
	int minH = -height / 2 + 2;
	int maxH = height / 2 - 2;

	if (minW >= maxW) { minW = -width / 2; maxW = width / 2; }
	if (minH >= maxH) { minH = -height / 2; maxH = height / 2; }

	std::uniform_int_distribution<int> distW(minW, maxW);
	std::uniform_int_distribution<int> distH(minH, maxH);

	int attempts = 0;
	while (static_cast<int>(waypoints.size()) < numRooms && attempts < 1000) {
		++attempts;
		int wx = distW(m_rng);
		int wy = distH(m_rng);
		int px = startX + wx;
		int py = startY + wy;

		bool valid = true;
		for (const auto& wp : waypoints) {
			int dx = px - wp.x;
			int dy = py - wp.y;
			double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
			if (dist < minDist) {
				valid = false;
				break;
			}
		}

		if (valid) {
			waypoints.push_back({px, py});
		}
	}

	return waypoints;
}

//=============================================================================
// Step 3: Order waypoints by nearest-neighbor
//=============================================================================

std::vector<DungeonGenerator::Waypoint> DungeonGenerator::orderWaypoints(
	std::vector<Waypoint> waypoints) {

	if (waypoints.empty()) return {};

	std::vector<Waypoint> ordered;
	ordered.reserve(waypoints.size());

	ordered.push_back(waypoints[0]);
	waypoints.erase(waypoints.begin());

	while (!waypoints.empty()) {
		const auto& current = ordered.back();
		int nearestIdx = 0;
		double minDist = std::numeric_limits<double>::max();

		for (int i = 0; i < static_cast<int>(waypoints.size()); ++i) {
			int dx = current.x - waypoints[i].x;
			int dy = current.y - waypoints[i].y;
			double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
			if (dist < minDist) {
				minDist = dist;
				nearestIdx = i;
			}
		}

		ordered.push_back(waypoints[nearestIdx]);
		waypoints.erase(waypoints.begin() + nearestIdx);
	}

	return ordered;
}

//=============================================================================
// Step 4: Carve rooms at each waypoint
//=============================================================================

static const int ROOM_SIZES[][2] = {
	{5, 5}, {7, 7}, {5, 7}, {9, 5}, {7, 9}
};
static const int NUM_ROOM_SIZES = 5;

void DungeonGenerator::carveRooms(const std::vector<Waypoint>& waypoints, int z,
                                   uint16_t floorId,
                                   std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	std::uniform_int_distribution<int> sizeDist(0, NUM_ROOM_SIZES - 1);

	for (const auto& wp : waypoints) {
		int sizeIdx = sizeDist(m_rng);
		int rw = ROOM_SIZES[sizeIdx][0];
		int rh = ROOM_SIZES[sizeIdx][1];

		for (int i = 0; i < rw; ++i) {
			for (int j = 0; j < rh; ++j) {
				int fx = wp.x - rw / 2 + i;
				int fy = wp.y - rh / 2 + j;
				paintFloor(fx, fy, z, floorId, tileChanges);
			}
		}
	}
}

//=============================================================================
// Step 5: Carve corridors between ordered waypoints
//=============================================================================

void DungeonGenerator::carveCorridors(const std::vector<Waypoint>& waypoints, int z,
                                       uint16_t floorId, int pathWidth,
                                       std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	int pw = std::max(2, pathWidth);
	int left = (pw - 1) / 2;
	int right = pw - 1 - left;

	for (size_t i = 0; i + 1 < waypoints.size(); ++i) {
		int cx = waypoints[i].x;
		int cy = waypoints[i].y;
		int tx = waypoints[i + 1].x;
		int ty = waypoints[i + 1].y;

		// Horizontal segment
		int stepX = (tx > cx) ? 1 : -1;
		while (cx != tx) {
			cx += stepX;
			for (int offset = -left; offset <= right; ++offset) {
				paintFloor(cx, cy + offset, z, floorId, tileChanges);
			}
		}

		// Vertical segment
		int stepY = (ty > cy) ? 1 : -1;
		while (cy != ty) {
			cy += stepY;
			for (int offset = -left; offset <= right; ++offset) {
				paintFloor(cx + offset, cy, z, floorId, tileChanges);
			}
		}
	}
}

//=============================================================================
// Step 6: Build walls around floor edges
//=============================================================================

void DungeonGenerator::buildWalls(int startX, int startY, int width, int height, int z,
                                   uint16_t floorId, const WallConfig& wallConfig,
                                   std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	int halfW = width / 2;
	int halfH = height / 2;

	// Resolve wall IDs: horizontal, vertical, pillar, corner
	uint16_t idHorz = wallConfig.north;
	uint16_t idVert = wallConfig.west;
	uint16_t idPillar = wallConfig.pillar;
	uint16_t idCorner = wallConfig.nw;

	auto place = [&](int x, int y, uint16_t id, WallPlacement::Type type) {
		if (id == 0) return;
		tileChanges.push_back({Position(x, y, z), id});
		m_wallPlacements.push_back({Position(x, y, z), type, id});
	};

	for (int y = 0; y <= height; ++y) {
		for (int x = 0; x <= width; ++x) {
			int absX = startX + x - halfW;
			int absY = startY + y - halfH;
			uint16_t itemId = getGridTile(absX, absY);

			auto check = [&](int dx, int dy) -> uint16_t {
				return getGridTile(absX + dx, absY + dy);
			};

			if (itemId != floorId) continue;

			// South edge: floor tile with rock below
			if (check(0, 1) == 0) {
				if (check(1, 0) == 0) {
					place(absX, absY, idCorner, WallPlacement::Corner);
				} else {
					place(absX, absY, idHorz, WallPlacement::Horizontal);
				}
			} else if (check(0, 1) == floorId) {
				// Inner corner check
				if (check(1, 0) == floorId && check(0, -1) == floorId && check(-1, 0) == floorId) {
					if (check(-1, 1) == 0 && check(1, -1) == 0) {
						place(absX, absY, idCorner, WallPlacement::Corner);
					}
				}
			}

			// East edge: floor tile with rock to the right
			if (check(1, 0) == 0 && check(0, 1) != 0) {
				place(absX, absY, idVert, WallPlacement::Vertical);
			}

			// North edge: floor tile with rock above
			if (check(0, -1) == 0) {
				if (check(1, -1) == floorId) {
					place(absX, absY - 1, idCorner, WallPlacement::Corner);
				} else {
					place(absX, absY - 1, idHorz, WallPlacement::Horizontal);
				}
			}

			// West edge: floor tile with rock to the left
			if (check(-1, 0) == 0 && check(-1, 1) != floorId) {
				place(absX - 1, absY, idVert, WallPlacement::Vertical);
			}

			// Pillar: floor with diagonal rock where both adjacent sides are floor
			if (check(1, 1) == 0 && check(1, 0) == floorId && check(0, 1) == floorId) {
				place(absX, absY, idPillar, WallPlacement::Pillar);
			}

			if (check(-1, -1) == 0 && check(-1, 0) == 0 && check(0, -1) == 0) {
				place(absX - 1, absY - 1, idPillar, WallPlacement::Pillar);
			}
		}
	}
}

//=============================================================================
// Step 7: Place hangable items on walls
//=============================================================================

void DungeonGenerator::createHangableDetails(int z, const HangableConfig& hangables,
                                              std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	if (m_wallPlacements.empty()) return;

	std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);

	for (const auto& wall : m_wallPlacements) {
		if (wall.type == WallPlacement::Horizontal && !hangables.horizontalIds.empty()) {
			if (chanceDist(m_rng) < hangables.chance) {
				std::uniform_int_distribution<int> itemDist(0, static_cast<int>(hangables.horizontalIds.size()) - 1);
				uint16_t itemId = hangables.horizontalIds[itemDist(m_rng)];
				tileChanges.push_back({wall.pos, itemId});
			}
		}

		if (wall.type == WallPlacement::Vertical && hangables.enableVertical && !hangables.verticalIds.empty()) {
			if (chanceDist(m_rng) < hangables.chance) {
				std::uniform_int_distribution<int> itemDist(0, static_cast<int>(hangables.verticalIds.size()) - 1);
				uint16_t itemId = hangables.verticalIds[itemDist(m_rng)];
				tileChanges.push_back({wall.pos, itemId});
			}
		}
	}
}

//=============================================================================
// Step 8: Apply terrain patches
//=============================================================================

void DungeonGenerator::applyPatches(int z, const DungeonPreset& preset,
                                     std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	if (preset.patchId == 0) return;

	std::uniform_int_distribution<int> patchChance(0, 99);
	std::uniform_int_distribution<int> radiusDist(1, 4);
	std::uniform_real_distribution<float> brushChance(0.0f, 1.0f);

	uint16_t floorId = preset.groundId;
	uint16_t patchId = preset.patchId;

	std::vector<Position> patchPositions;
	std::vector<Position> brushPositions;

	// Copy floor positions since we iterate and may modify grid
	auto floors = m_floorPositions;

	for (const auto& pos : floors) {
		if (patchChance(m_rng) >= 1) continue; // 1% chance

		int cx = pos.x;
		int cy = pos.y;
		int radius = radiusDist(m_rng);

		for (int i = -radius; i <= radius; ++i) {
			for (int j = -radius; j <= radius; ++j) {
				if (std::abs(i) + std::abs(j) > radius) continue;

				int rx = cx + i;
				int ry = cy + j;

				if (getGridTile(rx, ry) == floorId) {
					paintFloor(rx, ry, z, patchId, tileChanges);
					patchPositions.push_back(Position(rx, ry, z));
				}
			}
		}
	}

	// Apply brush on top of patches (15% chance)
	if (preset.brushId > 0 && !patchPositions.empty()) {
		for (const auto& pos : patchPositions) {
			if (brushChance(m_rng) < 0.15f) {
				if (getGridTile(pos.x, pos.y) == patchId) {
					paintFloor(pos.x, pos.y, z, preset.brushId, tileChanges);
					brushPositions.push_back(pos);
				}
			}
		}
	}

	// Build borders around patches
	if (preset.borders.isValid() && !patchPositions.empty()) {
		buildBorders(patchPositions, z, preset.borders, tileChanges);
	}

	// Build borders around brush areas
	if (preset.brushBorders.isValid() && !brushPositions.empty()) {
		buildBorders(brushPositions, z, preset.brushBorders, tileChanges);
	}
}

//=============================================================================
// Border building helper
//=============================================================================

void DungeonGenerator::buildBorders(const std::vector<Position>& floorPositions, int z,
                                     const BorderConfig& borderConfig,
                                     std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	// Build a lookup of the provided floor positions
	std::unordered_map<uint64_t, bool> floorLookup;
	for (const auto& pos : floorPositions) {
		floorLookup[posHash(pos.x, pos.y)] = true;
	}

	auto isFloor = [&](int x, int y) -> bool {
		return floorLookup.count(posHash(x, y)) > 0;
	};

	// Gather candidate positions (non-floor neighbors of floor tiles)
	std::unordered_map<uint64_t, Position> candidates;
	static const int DIR[][2] = {
		{0,-1}, {0,1}, {1,0}, {-1,0}, {1,-1}, {-1,-1}, {1,1}, {-1,1}
	};

	for (const auto& pos : floorPositions) {
		for (const auto& off : DIR) {
			int nx = pos.x + off[0];
			int ny = pos.y + off[1];
			uint64_t hash = posHash(nx, ny);
			if (!isFloor(nx, ny) && candidates.find(hash) == candidates.end()) {
				candidates[hash] = Position(nx, ny, z);
			}
		}
	}

	// Pick border ID based on neighbor pattern
	for (const auto& [hash, pos] : candidates) {
		bool n = isFloor(pos.x, pos.y - 1);
		bool s = isFloor(pos.x, pos.y + 1);
		bool e = isFloor(pos.x + 1, pos.y);
		bool w = isFloor(pos.x - 1, pos.y);

		uint16_t borderId = 0;

		// Corners (two adjacent sides)
		if (s && e) borderId = borderConfig.nw;
		else if (s && w) borderId = borderConfig.ne;
		else if (n && e) borderId = borderConfig.sw;
		else if (n && w) borderId = borderConfig.se;
		// Straight edges
		else if (s && !n && !e && !w) borderId = borderConfig.north;
		else if (n && !s && !e && !w) borderId = borderConfig.south;
		else if (w && !n && !s && !e) borderId = borderConfig.east;
		else if (e && !n && !s && !w) borderId = borderConfig.west;
		else {
			// Inner corners (diagonal only)
			bool ne = isFloor(pos.x + 1, pos.y - 1);
			bool nw = isFloor(pos.x - 1, pos.y - 1);
			bool se = isFloor(pos.x + 1, pos.y + 1);
			bool sw = isFloor(pos.x - 1, pos.y + 1);

			if (se) borderId = borderConfig.inner_nw;
			else if (sw) borderId = borderConfig.inner_ne;
			else if (ne) borderId = borderConfig.inner_sw;
			else if (nw) borderId = borderConfig.inner_se;
		}

		if (borderId > 0) {
			tileChanges.push_back({pos, borderId});
		}
	}
}

//=============================================================================
// Step 9: Decorate floor tiles
//=============================================================================

bool DungeonGenerator::isPlacementValid(DetailGroup::Placement placement,
                                         int x, int y, uint16_t floorId) const {
	switch (placement) {
		case DetailGroup::NearWall:
			return !(isFloorTile(x + 1, y, floorId) && isFloorTile(x - 1, y, floorId) &&
			         isFloorTile(x, y + 1, floorId) && isFloorTile(x, y - 1, floorId));

		case DetailGroup::NorthWall:
			return !isFloorTile(x, y - 1, floorId);

		case DetailGroup::WestWall:
			return !isFloorTile(x - 1, y, floorId);

		case DetailGroup::Center:
			for (int dx = -1; dx <= 1; ++dx) {
				for (int dy = -1; dy <= 1; ++dy) {
					if (dx == 0 && dy == 0) continue;
					if (!isFloorTile(x + dx, y + dy, floorId)) return false;
				}
			}
			return true;

		case DetailGroup::Anywhere:
		default:
			return true;
	}
}

void DungeonGenerator::decorateFloors(int z, const DungeonPreset& preset,
                                       std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	if (preset.details.empty()) return;

	uint16_t floorId = preset.groundId;
	std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);

	for (const auto& pos : m_floorPositions) {
		if (getGridTile(pos.x, pos.y) != floorId) continue;

		for (const auto& group : preset.details) {
			if (group.itemIds.empty() || group.chance <= 0.0f) continue;
			if (chanceDist(m_rng) >= group.chance) continue;

			if (!isPlacementValid(group.placement, pos.x, pos.y, floorId)) continue;

			std::uniform_int_distribution<int> itemDist(0, static_cast<int>(group.itemIds.size()) - 1);
			uint16_t itemId = group.itemIds[itemDist(m_rng)];
			tileChanges.push_back({Position(pos.x, pos.y, z), itemId});
			break; // Only one detail group per tile (matches Lua behavior)
		}
	}
}

//=============================================================================
// Apply all collected changes to map via Action system
//=============================================================================

bool DungeonGenerator::applyChanges(const std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	if (tileChanges.empty()) {
		m_lastError = "No changes to apply";
		return false;
	}

	Map& map = m_editor->map;

	auto batch = m_editor->actionQueue->createBatch(ACTION_DRAW);
	auto action = m_editor->actionQueue->createAction(batch.get());

	// Group changes by position to minimize tile copies
	std::unordered_map<uint64_t, std::vector<const std::pair<Position, uint16_t>*>> changesByPos;
	for (const auto& change : tileChanges) {
		uint64_t hash = (static_cast<uint64_t>(change.first.x & 0xFFFF) << 32) |
		                (static_cast<uint64_t>(change.first.y & 0xFFFF) << 16) |
		                static_cast<uint64_t>(change.first.z & 0xFFFF);
		changesByPos[hash].push_back(&change);
	}

	for (const auto& [hash, changes] : changesByPos) {
		const Position& pos = changes[0]->first;

		Tile* existingTile = map.getTile(pos);
		std::unique_ptr<Tile> newTile;

		if (existingTile) {
			newTile = TileOperations::deepCopy(existingTile, map);
		} else {
			newTile = std::make_unique<Tile>(pos.x, pos.y, pos.z);
		}

		for (const auto* change : changes) {
			auto item = Item::Create(change->second);
			if (item) {
				newTile->addItem(std::move(item));
			}
		}

		action->addChange(std::make_unique<Change>(std::move(newTile)));
	}

	if (action->size() == 0) {
		m_lastError = "No tile changes were produced";
		return false;
	}

	batch->addAndCommitAction(std::move(action));
	m_editor->addBatch(std::move(batch));

	return true;
}

} // namespace DungeonGen
