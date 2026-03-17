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
#include <numeric>
#include <sstream>
#include <wx/dir.h>
#include <wx/filename.h>

namespace DungeonGen {

//=============================================================================
// Algorithm helpers
//=============================================================================

std::string algorithmToString(Algorithm algo) {
	switch (algo) {
		case Algorithm::BSP: return "BSP";
		case Algorithm::RandomWalk: return "Random Walk";
		default: return "Room Placement";
	}
}

Algorithm algorithmFromString(const std::string& str) {
	if (str == "BSP") return Algorithm::BSP;
	if (str == "Random Walk") return Algorithm::RandomWalk;
	return Algorithm::RoomPlacement;
}

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
// DungeonGenerator - Core
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

void DungeonGenerator::fillBackground(int startX, int startY, int width, int height, int z,
                                       uint16_t fillId,
                                       std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	if (fillId == 0) return;
	int halfW = width / 2;
	int halfH = height / 2;
	for (int y = 0; y <= height; ++y) {
		for (int x = 0; x <= width; ++x) {
			tileChanges.push_back({Position(startX + x - halfW, startY + y - halfH, z), fillId});
		}
	}
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

	if (config.seed != 0) {
		m_rng.seed(static_cast<unsigned int>(config.seed));
	} else {
		std::random_device rd;
		m_rng.seed(rd());
	}

	uint16_t floorId = preset.groundId;

	std::vector<std::pair<Position, uint16_t>> tileChanges;
	tileChanges.reserve(static_cast<size_t>(width) * height);

	// Step 1: Fill background
	if (!reportProgress("Filling background...", 5)) { m_lastError = "Cancelled"; return false; }
	fillBackground(startX, startY, width, height, z, preset.fillId, tileChanges);

	// Step 2: Generate layout
	if (!reportProgress("Generating layout...", 15)) { m_lastError = "Cancelled"; return false; }
	switch (config.algorithm) {
		case Algorithm::BSP:
			generateBSP(startX, startY, width, height, z, floorId, config.bsp, tileChanges);
			break;
		case Algorithm::RandomWalk:
			generateRandomWalk(startX, startY, width, height, z, floorId, config.randomWalk, tileChanges);
			break;
		case Algorithm::RoomPlacement:
		default:
			generateRoomPlacement(startX, startY, width, height, z, floorId, config.roomPlacement, tileChanges);
			break;
	}

	if (m_floorPositions.empty()) {
		m_lastError = "Algorithm produced no floor tiles";
		return false;
	}

	// Step 3: Build walls
	if (!reportProgress("Building walls...", 45)) { m_lastError = "Cancelled"; return false; }
	if (preset.walls.isValid()) {
		buildWalls(startX, startY, width, height, z, floorId, preset.walls, tileChanges);
		if (preset.hangables.isValid()) {
			createHangableDetails(z, preset.hangables, tileChanges);
		}
		for (const auto& wall : m_wallPlacements) {
			setGridTile(wall.pos.x, wall.pos.y, 0xFFFF);
		}
	}

	// Step 4: Patches + borders
	if (!reportProgress("Applying patches and borders...", 60)) { m_lastError = "Cancelled"; return false; }
	applyPatches(z, preset, tileChanges);

	// Step 5: Floor decoration
	if (!reportProgress("Decorating floors...", 75)) { m_lastError = "Cancelled"; return false; }
	decorateFloors(z, preset, tileChanges);

	// Apply to map
	if (!reportProgress("Applying to map...", 90)) { m_lastError = "Cancelled"; return false; }
	if (!applyChanges(tileChanges)) {
		return false;
	}

	m_tilesGenerated = static_cast<int>(tileChanges.size());
	return true;
}

//=============================================================================
// Common: carve a room and a corridor
//=============================================================================

void DungeonGenerator::carveRoom(const Room& room, int z, uint16_t floorId,
                                  std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	for (int ry = room.y; ry < room.y + room.h; ++ry) {
		for (int rx = room.x; rx < room.x + room.w; ++rx) {
			paintFloor(rx, ry, z, floorId, tileChanges);
		}
	}
}

void DungeonGenerator::carveCorridor(int x1, int y1, int x2, int y2, int z, uint16_t floorId,
                                      int pathWidth, std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	int pw = std::max(1, pathWidth);
	int half = pw / 2;

	int cx = x1, cy = y1;

	// Horizontal first
	int stepX = (x2 > cx) ? 1 : -1;
	while (cx != x2) {
		for (int off = -half; off < pw - half; ++off) {
			paintFloor(cx, cy + off, z, floorId, tileChanges);
		}
		cx += stepX;
	}

	// Then vertical
	int stepY = (y2 > cy) ? 1 : -1;
	while (cy != y2) {
		for (int off = -half; off < pw - half; ++off) {
			paintFloor(cx + off, cy, z, floorId, tileChanges);
		}
		cy += stepY;
	}
}

//=============================================================================
// Algorithm 1: Room Placement (improved with MST)
//=============================================================================

std::vector<std::pair<int,int>> DungeonGenerator::buildMST(const std::vector<Room>& rooms) {
	int n = static_cast<int>(rooms.size());
	if (n <= 1) return {};

	// Prim's algorithm
	std::vector<bool> inMST(n, false);
	std::vector<double> minCost(n, std::numeric_limits<double>::max());
	std::vector<int> parent(n, -1);
	std::vector<std::pair<int,int>> edges;

	minCost[0] = 0;

	for (int iter = 0; iter < n; ++iter) {
		int u = -1;
		for (int i = 0; i < n; ++i) {
			if (!inMST[i] && (u == -1 || minCost[i] < minCost[u])) {
				u = i;
			}
		}
		inMST[u] = true;
		if (parent[u] != -1) {
			edges.push_back({parent[u], u});
		}

		for (int v = 0; v < n; ++v) {
			if (inMST[v]) continue;
			int dx = rooms[u].centerX() - rooms[v].centerX();
			int dy = rooms[u].centerY() - rooms[v].centerY();
			double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
			if (dist < minCost[v]) {
				minCost[v] = dist;
				parent[v] = u;
			}
		}
	}

	return edges;
}

void DungeonGenerator::generateRoomPlacement(int startX, int startY, int width, int height, int z,
                                              uint16_t floorId, const RoomPlacementParams& params,
                                              std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	int halfW = width / 2;
	int halfH = height / 2;

	std::vector<Room> rooms;
	std::uniform_int_distribution<int> sizeDist(params.minRoomSize, params.maxRoomSize);

	int attempts = 0;
	while (static_cast<int>(rooms.size()) < params.numRooms && attempts < 1000) {
		++attempts;

		int rw = sizeDist(m_rng) | 1; // Ensure odd for symmetry
		int rh = sizeDist(m_rng) | 1;

		std::uniform_int_distribution<int> xDist(startX - halfW + 1, startX + halfW - rw - 1);
		std::uniform_int_distribution<int> yDist(startY - halfH + 1, startY + halfH - rh - 1);

		Room room = {xDist(m_rng), yDist(m_rng), rw, rh};

		// Check collision with existing rooms
		bool valid = true;
		for (const auto& other : rooms) {
			int dx = std::abs(room.centerX() - other.centerX());
			int dy = std::abs(room.centerY() - other.centerY());
			if (dx < (room.w + other.w) / 2 + 2 && dy < (room.h + other.h) / 2 + 2) {
				valid = false;
				break;
			}
			// Also check minimum center distance
			double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
			if (dist < params.minRoomDistance) {
				valid = false;
				break;
			}
		}

		if (valid) {
			rooms.push_back(room);
		}
	}

	// Carve rooms
	for (const auto& room : rooms) {
		carveRoom(room, z, floorId, tileChanges);
	}

	// Connect rooms
	if (params.useMST) {
		auto edges = buildMST(rooms);
		for (const auto& [a, b] : edges) {
			carveCorridor(rooms[a].centerX(), rooms[a].centerY(),
			              rooms[b].centerX(), rooms[b].centerY(),
			              z, floorId, params.pathWidth, tileChanges);
		}
		// Add a few extra random connections for loops
		if (rooms.size() > 3) {
			int extras = std::max(1, static_cast<int>(rooms.size()) / 5);
			std::uniform_int_distribution<int> roomDist(0, static_cast<int>(rooms.size()) - 1);
			for (int i = 0; i < extras; ++i) {
				int a = roomDist(m_rng);
				int b = roomDist(m_rng);
				if (a != b) {
					carveCorridor(rooms[a].centerX(), rooms[a].centerY(),
					              rooms[b].centerX(), rooms[b].centerY(),
					              z, floorId, params.pathWidth, tileChanges);
				}
			}
		}
	} else {
		// Nearest-neighbor ordering (original behavior)
		std::vector<int> order;
		order.push_back(0);
		std::vector<bool> visited(rooms.size(), false);
		visited[0] = true;

		for (size_t iter = 1; iter < rooms.size(); ++iter) {
			int last = order.back();
			int nearest = -1;
			double minD = std::numeric_limits<double>::max();
			for (int j = 0; j < static_cast<int>(rooms.size()); ++j) {
				if (visited[j]) continue;
				int dx = rooms[last].centerX() - rooms[j].centerX();
				int dy = rooms[last].centerY() - rooms[j].centerY();
				double d = std::sqrt(static_cast<double>(dx * dx + dy * dy));
				if (d < minD) { minD = d; nearest = j; }
			}
			if (nearest >= 0) {
				visited[nearest] = true;
				order.push_back(nearest);
			}
		}

		for (size_t i = 0; i + 1 < order.size(); ++i) {
			carveCorridor(rooms[order[i]].centerX(), rooms[order[i]].centerY(),
			              rooms[order[i+1]].centerX(), rooms[order[i+1]].centerY(),
			              z, floorId, params.pathWidth, tileChanges);
		}
	}
}

//=============================================================================
// Algorithm 2: BSP (Binary Space Partitioning)
//=============================================================================

void DungeonGenerator::splitBSP(BSPNode* node, int depth, const BSPParams& params) {
	if (depth >= params.maxDepth) return;
	if (node->w < params.minPartitionSize * 2 && node->h < params.minPartitionSize * 2) return;

	// Decide split direction
	bool splitH;
	if (node->w < params.minPartitionSize * 2) splitH = true;
	else if (node->h < params.minPartitionSize * 2) splitH = false;
	else splitH = std::uniform_int_distribution<int>(0, 1)(m_rng) == 0;

	if (splitH) {
		if (node->h < params.minPartitionSize * 2) return;
		int splitRange = node->h - params.minPartitionSize * 2;
		int splitY = params.minPartitionSize + (splitRange > 0 ? std::uniform_int_distribution<int>(0, splitRange)(m_rng) : 0);

		node->left = std::make_unique<BSPNode>();
		node->left->x = node->x;
		node->left->y = node->y;
		node->left->w = node->w;
		node->left->h = splitY;

		node->right = std::make_unique<BSPNode>();
		node->right->x = node->x;
		node->right->y = node->y + splitY;
		node->right->w = node->w;
		node->right->h = node->h - splitY;
	} else {
		if (node->w < params.minPartitionSize * 2) return;
		int splitRange = node->w - params.minPartitionSize * 2;
		int splitX = params.minPartitionSize + (splitRange > 0 ? std::uniform_int_distribution<int>(0, splitRange)(m_rng) : 0);

		node->left = std::make_unique<BSPNode>();
		node->left->x = node->x;
		node->left->y = node->y;
		node->left->w = splitX;
		node->left->h = node->h;

		node->right = std::make_unique<BSPNode>();
		node->right->x = node->x + splitX;
		node->right->y = node->y;
		node->right->w = node->w - splitX;
		node->right->h = node->h;
	}

	splitBSP(node->left.get(), depth + 1, params);
	splitBSP(node->right.get(), depth + 1, params);
}

void DungeonGenerator::createBSPRooms(BSPNode* node, int startX, int startY, int z, uint16_t floorId,
                                       const BSPParams& params,
                                       std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	if (!node) return;

	if (!node->left && !node->right) {
		// Leaf node — create a room
		int maxW = node->w - params.roomPadding * 2;
		int maxH = node->h - params.roomPadding * 2;
		if (maxW < params.minRoomSize) maxW = params.minRoomSize;
		if (maxH < params.minRoomSize) maxH = params.minRoomSize;

		int rw = std::uniform_int_distribution<int>(params.minRoomSize, std::min(maxW, node->w - 2))(m_rng);
		int rh = std::uniform_int_distribution<int>(params.minRoomSize, std::min(maxH, node->h - 2))(m_rng);

		int rx = node->x + std::uniform_int_distribution<int>(params.roomPadding, std::max(params.roomPadding, node->w - rw - params.roomPadding))(m_rng);
		int ry = node->y + std::uniform_int_distribution<int>(params.roomPadding, std::max(params.roomPadding, node->h - rh - params.roomPadding))(m_rng);

		node->room = {startX + rx - node->w, startY + ry - node->h, rw, rh};
		// Offset to absolute coordinates
		int halfW = 0; // The node coords are already relative
		node->room = {startX - (node->w + node->x) / 2 + rx, startY - (node->h + node->y) / 2 + ry, rw, rh};
		// Simpler: just offset from start
		node->room = {startX + rx, startY + ry, rw, rh};
		node->hasRoom = true;

		carveRoom(node->room, z, floorId, tileChanges);
		return;
	}

	createBSPRooms(node->left.get(), startX, startY, z, floorId, params, tileChanges);
	createBSPRooms(node->right.get(), startX, startY, z, floorId, params, tileChanges);
}

DungeonGenerator::Room DungeonGenerator::getBSPRoomCenter(BSPNode* node) {
	if (!node) return {0, 0, 1, 1};
	if (node->hasRoom) return node->room;

	if (node->left && node->right) {
		// Prefer left, then right
		Room lr = getBSPRoomCenter(node->left.get());
		if (lr.w > 0) return lr;
		return getBSPRoomCenter(node->right.get());
	}

	if (node->left) return getBSPRoomCenter(node->left.get());
	if (node->right) return getBSPRoomCenter(node->right.get());

	return {0, 0, 1, 1};
}

void DungeonGenerator::connectBSPRooms(BSPNode* node, int startX, int startY, int z, uint16_t floorId,
                                        int corridorWidth, std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	if (!node || !node->left || !node->right) return;

	connectBSPRooms(node->left.get(), startX, startY, z, floorId, corridorWidth, tileChanges);
	connectBSPRooms(node->right.get(), startX, startY, z, floorId, corridorWidth, tileChanges);

	Room leftRoom = getBSPRoomCenter(node->left.get());
	Room rightRoom = getBSPRoomCenter(node->right.get());

	carveCorridor(leftRoom.centerX(), leftRoom.centerY(),
	              rightRoom.centerX(), rightRoom.centerY(),
	              z, floorId, corridorWidth, tileChanges);
}

void DungeonGenerator::generateBSP(int startX, int startY, int width, int height, int z,
                                    uint16_t floorId, const BSPParams& params,
                                    std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	int halfW = width / 2;
	int halfH = height / 2;
	int originX = startX - halfW;
	int originY = startY - halfH;

	BSPNode root;
	root.x = 0;
	root.y = 0;
	root.w = width;
	root.h = height;

	splitBSP(&root, 0, params);
	createBSPRooms(&root, originX, originY, z, floorId, params, tileChanges);
	connectBSPRooms(&root, originX, originY, z, floorId, params.corridorWidth, tileChanges);
}

//=============================================================================
// Algorithm 3: Random Walk (Drunkard's Walk)
//=============================================================================

void DungeonGenerator::generateRandomWalk(int startX, int startY, int width, int height, int z,
                                           uint16_t floorId, const RandomWalkParams& params,
                                           std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	int halfW = width / 2;
	int halfH = height / 2;
	int minX = startX - halfW + 1;
	int maxX = startX + halfW - 1;
	int minY = startY - halfH + 1;
	int maxY = startY + halfH - 1;

	int totalArea = width * height;
	int targetFloors = static_cast<int>(totalArea * params.coverage);

	static const int DX[] = {0, 0, 1, -1};
	static const int DY[] = {-1, 1, 0, 0};

	struct Walker {
		int x, y, dir;
	};

	std::vector<Walker> walkers;
	for (int i = 0; i < params.walkerCount; ++i) {
		int wx = params.startCenter ? startX : std::uniform_int_distribution<int>(minX + 5, maxX - 5)(m_rng);
		int wy = params.startCenter ? startY : std::uniform_int_distribution<int>(minY + 5, maxY - 5)(m_rng);
		int dir = std::uniform_int_distribution<int>(0, 3)(m_rng);
		walkers.push_back({wx, wy, dir});
	}

	std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
	std::uniform_int_distribution<int> dirDist(0, 3);
	std::uniform_int_distribution<int> roomChanceDist(0, 99);
	int floorCount = 0;
	int totalSteps = 0;

	while (floorCount < targetFloors && totalSteps < params.maxSteps) {
		for (auto& walker : walkers) {
			if (floorCount >= targetFloors) break;

			// Maybe change direction
			if (chanceDist(m_rng) < params.turnChance) {
				walker.dir = dirDist(m_rng);
			}

			int nx = walker.x + DX[walker.dir];
			int ny = walker.y + DY[walker.dir];

			// Bounds check
			if (nx <= minX || nx >= maxX || ny <= minY || ny >= maxY) {
				walker.dir = dirDist(m_rng);
				continue;
			}

			walker.x = nx;
			walker.y = ny;

			if (getGridTile(nx, ny) != floorId) {
				paintFloor(nx, ny, z, floorId, tileChanges);
				++floorCount;
			}

			// Occasionally carve a small room
			if (roomChanceDist(m_rng) < params.roomChance) {
				int rw = std::uniform_int_distribution<int>(params.minRoomSize, params.maxRoomSize)(m_rng);
				int rh = std::uniform_int_distribution<int>(params.minRoomSize, params.maxRoomSize)(m_rng);
				int rx = nx - rw / 2;
				int ry = ny - rh / 2;

				for (int dy = 0; dy < rh; ++dy) {
					for (int dx = 0; dx < rw; ++dx) {
						int px = rx + dx;
						int py = ry + dy;
						if (px > minX && px < maxX && py > minY && py < maxY) {
							if (getGridTile(px, py) != floorId) {
								paintFloor(px, py, z, floorId, tileChanges);
								++floorCount;
							}
						}
					}
				}
			}

			++totalSteps;
		}
	}
}

//=============================================================================
// Post-processing: Walls
//=============================================================================

void DungeonGenerator::buildWalls(int startX, int startY, int width, int height, int z,
                                   uint16_t floorId, const WallConfig& wallConfig,
                                   std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	int halfW = width / 2;
	int halfH = height / 2;

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

			if (check(0, 1) == 0) {
				if (check(1, 0) == 0) {
					place(absX, absY, idCorner, WallPlacement::Corner);
				} else {
					place(absX, absY, idHorz, WallPlacement::Horizontal);
				}
			} else if (check(0, 1) == floorId) {
				if (check(1, 0) == floorId && check(0, -1) == floorId && check(-1, 0) == floorId) {
					if (check(-1, 1) == 0 && check(1, -1) == 0) {
						place(absX, absY, idCorner, WallPlacement::Corner);
					}
				}
			}

			if (check(1, 0) == 0 && check(0, 1) != 0) {
				place(absX, absY, idVert, WallPlacement::Vertical);
			}

			if (check(0, -1) == 0) {
				if (check(1, -1) == floorId) {
					place(absX, absY - 1, idCorner, WallPlacement::Corner);
				} else {
					place(absX, absY - 1, idHorz, WallPlacement::Horizontal);
				}
			}

			if (check(-1, 0) == 0 && check(-1, 1) != floorId) {
				place(absX - 1, absY, idVert, WallPlacement::Vertical);
			}

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
// Post-processing: Hangables
//=============================================================================

void DungeonGenerator::createHangableDetails(int z, const HangableConfig& hangables,
                                              std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	if (m_wallPlacements.empty()) return;
	std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);

	for (const auto& wall : m_wallPlacements) {
		if (wall.type == WallPlacement::Horizontal && !hangables.horizontalIds.empty()) {
			if (chanceDist(m_rng) < hangables.chance) {
				std::uniform_int_distribution<int> itemDist(0, static_cast<int>(hangables.horizontalIds.size()) - 1);
				tileChanges.push_back({wall.pos, hangables.horizontalIds[itemDist(m_rng)]});
			}
		}
		if (wall.type == WallPlacement::Vertical && hangables.enableVertical && !hangables.verticalIds.empty()) {
			if (chanceDist(m_rng) < hangables.chance) {
				std::uniform_int_distribution<int> itemDist(0, static_cast<int>(hangables.verticalIds.size()) - 1);
				tileChanges.push_back({wall.pos, hangables.verticalIds[itemDist(m_rng)]});
			}
		}
	}
}

//=============================================================================
// Post-processing: Patches + Borders
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

	auto floors = m_floorPositions;
	for (const auto& pos : floors) {
		if (patchChance(m_rng) >= 1) continue;
		int cx = pos.x, cy = pos.y;
		int radius = radiusDist(m_rng);

		for (int i = -radius; i <= radius; ++i) {
			for (int j = -radius; j <= radius; ++j) {
				if (std::abs(i) + std::abs(j) > radius) continue;
				int rx = cx + i, ry = cy + j;
				if (getGridTile(rx, ry) == floorId) {
					paintFloor(rx, ry, z, patchId, tileChanges);
					patchPositions.push_back(Position(rx, ry, z));
				}
			}
		}
	}

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

	if (preset.borders.isValid() && !patchPositions.empty()) {
		buildBorders(patchPositions, z, preset.borders, tileChanges);
	}
	if (preset.brushBorders.isValid() && !brushPositions.empty()) {
		buildBorders(brushPositions, z, preset.brushBorders, tileChanges);
	}
}

//=============================================================================
// Post-processing: Border building
//=============================================================================

void DungeonGenerator::buildBorders(const std::vector<Position>& floorPositions, int z,
                                     const BorderConfig& borderConfig,
                                     std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	std::unordered_map<uint64_t, bool> floorLookup;
	for (const auto& pos : floorPositions) {
		floorLookup[posHash(pos.x, pos.y)] = true;
	}

	auto isFloor = [&](int x, int y) -> bool {
		return floorLookup.count(posHash(x, y)) > 0;
	};

	std::unordered_map<uint64_t, Position> candidates;
	static const int DIR[][2] = {
		{0,-1}, {0,1}, {1,0}, {-1,0}, {1,-1}, {-1,-1}, {1,1}, {-1,1}
	};

	for (const auto& pos : floorPositions) {
		for (const auto& off : DIR) {
			int nx = pos.x + off[0], ny = pos.y + off[1];
			uint64_t hash = posHash(nx, ny);
			if (!isFloor(nx, ny) && candidates.find(hash) == candidates.end()) {
				candidates[hash] = Position(nx, ny, z);
			}
		}
	}

	for (const auto& [hash, pos] : candidates) {
		bool n = isFloor(pos.x, pos.y - 1);
		bool s = isFloor(pos.x, pos.y + 1);
		bool e = isFloor(pos.x + 1, pos.y);
		bool w = isFloor(pos.x - 1, pos.y);

		uint16_t borderId = 0;

		if (s && e) borderId = borderConfig.nw;
		else if (s && w) borderId = borderConfig.ne;
		else if (n && e) borderId = borderConfig.sw;
		else if (n && w) borderId = borderConfig.se;
		else if (s && !n && !e && !w) borderId = borderConfig.north;
		else if (n && !s && !e && !w) borderId = borderConfig.south;
		else if (w && !n && !s && !e) borderId = borderConfig.east;
		else if (e && !n && !s && !w) borderId = borderConfig.west;
		else {
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
// Post-processing: Floor decoration
//=============================================================================

bool DungeonGenerator::isPlacementValid(DetailGroup::Placement placement,
                                         int x, int y, uint16_t floorId) const {
	switch (placement) {
		case DetailGroup::NearWall:
			return !(isFloorTile(x+1,y,floorId) && isFloorTile(x-1,y,floorId) &&
			         isFloorTile(x,y+1,floorId) && isFloorTile(x,y-1,floorId));
		case DetailGroup::NorthWall:
			return !isFloorTile(x, y-1, floorId);
		case DetailGroup::WestWall:
			return !isFloorTile(x-1, y, floorId);
		case DetailGroup::Center:
			for (int dx = -1; dx <= 1; ++dx)
				for (int dy = -1; dy <= 1; ++dy)
					if (!(dx == 0 && dy == 0) && !isFloorTile(x+dx, y+dy, floorId)) return false;
			return true;
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
			tileChanges.push_back({Position(pos.x, pos.y, z), group.itemIds[itemDist(m_rng)]});
			break;
		}
	}
}

//=============================================================================
// Apply changes to map
//=============================================================================

bool DungeonGenerator::applyChanges(const std::vector<std::pair<Position, uint16_t>>& tileChanges) {
	if (tileChanges.empty()) {
		m_lastError = "No changes to apply";
		return false;
	}

	Map& map = m_editor->map;
	auto batch = m_editor->actionQueue->createBatch(ACTION_DRAW);
	auto action = m_editor->actionQueue->createAction(batch.get());

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
