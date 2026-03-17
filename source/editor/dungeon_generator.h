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

#ifndef RME_DUNGEON_GENERATOR_H
#define RME_DUNGEON_GENERATOR_H

#include "map/position.h"
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <random>
#include <memory>
#include <functional>
#include <cstdint>

class Editor;
class Map;
class Tile;

namespace DungeonGen {

//=============================================================================
// WallConfig - Wall tile IDs for each direction
//=============================================================================
struct WallConfig {
	uint16_t north = 0;
	uint16_t south = 0;
	uint16_t east = 0;
	uint16_t west = 0;
	uint16_t nw = 0;
	uint16_t ne = 0;
	uint16_t sw = 0;
	uint16_t se = 0;
	uint16_t pillar = 0;

	bool isValid() const { return north > 0 || west > 0; }
};

//=============================================================================
// BorderConfig - Border tile IDs for each direction + inner corners
//=============================================================================
struct BorderConfig {
	uint16_t north = 0;
	uint16_t south = 0;
	uint16_t east = 0;
	uint16_t west = 0;
	uint16_t nw = 0;
	uint16_t ne = 0;
	uint16_t sw = 0;
	uint16_t se = 0;
	uint16_t inner_nw = 0;
	uint16_t inner_ne = 0;
	uint16_t inner_sw = 0;
	uint16_t inner_se = 0;

	bool isValid() const { return north > 0 || south > 0; }
};

//=============================================================================
// DetailGroup - A group of decorative items with placement rules
//=============================================================================
struct DetailGroup {
	std::vector<uint16_t> itemIds;
	float chance = 0.0f;
	enum Placement { Anywhere, NearWall, NorthWall, WestWall, Center };
	Placement placement = Anywhere;

	static Placement placementFromString(const std::string& str);
	static std::string placementToString(Placement p);
};

//=============================================================================
// HangableConfig - Wall-mounted decorative items
//=============================================================================
struct HangableConfig {
	std::vector<uint16_t> horizontalIds;
	std::vector<uint16_t> verticalIds;
	float chance = 0.05f;
	bool enableVertical = false;

	bool isValid() const { return !horizontalIds.empty() || !verticalIds.empty(); }
};

//=============================================================================
// DungeonPreset - Complete dungeon generation preset
//=============================================================================
struct DungeonPreset {
	std::string name;

	uint16_t groundId = 0;
	uint16_t patchId = 0;
	uint16_t fillId = 0;
	uint16_t brushId = 0;

	WallConfig walls;
	BorderConfig borders;
	BorderConfig brushBorders;

	std::vector<DetailGroup> details;
	HangableConfig hangables;

	bool saveToFile(const std::string& filepath) const;
	bool loadFromFile(const std::string& filepath);
	bool validate(std::string& errorOut) const;
};

//=============================================================================
// PresetManager - Manages saved dungeon presets
//=============================================================================
class PresetManager {
public:
	static PresetManager& getInstance();

	bool loadPresets();
	bool savePresets();

	std::vector<std::string> getPresetNames() const;
	const DungeonPreset* getPreset(const std::string& name) const;
	DungeonPreset* getPresetMutable(const std::string& name);
	bool addPreset(const DungeonPreset& preset);
	bool removePreset(const std::string& name);
	bool renamePreset(const std::string& oldName, const std::string& newName);

	std::string getPresetsDirectory() const;

private:
	PresetManager() = default;
	std::map<std::string, DungeonPreset> m_presets;
	bool m_loaded = false;
};

//=============================================================================
// Algorithm types
//=============================================================================
enum class Algorithm {
	RoomPlacement,  // Original: waypoints + rooms + corridors (improved with MST)
	BSP,            // Binary Space Partitioning: recursive subdivision
	RandomWalk      // Drunkard's Walk: organic cave generation
};

std::string algorithmToString(Algorithm algo);
Algorithm algorithmFromString(const std::string& str);

//=============================================================================
// Per-algorithm parameters
//=============================================================================
struct RoomPlacementParams {
	int numRooms = 18;         // Max rooms to place
	int minRoomSize = 5;       // Min room width/height
	int maxRoomSize = 11;      // Max room width/height
	int minRoomDistance = 8;    // Min distance between room centers
	int pathWidth = 4;         // Corridor width
	bool useMST = true;        // Use Minimum Spanning Tree for connections (vs nearest-neighbor)
};

struct BSPParams {
	int minPartitionSize = 12; // Min partition width/height before stopping splits
	int minRoomSize = 5;       // Min room size within partition
	int roomPadding = 2;       // Min padding between room edge and partition edge
	int corridorWidth = 3;     // Corridor width
	int maxDepth = 6;          // Max recursion depth
};

struct RandomWalkParams {
	int walkerCount = 3;       // Number of simultaneous walkers
	float coverage = 0.40f;    // Target floor coverage (0.0 - 1.0)
	int maxSteps = 50000;      // Safety limit for total steps
	bool startCenter = true;   // Start walkers from center
	float turnChance = 0.3f;   // Chance to change direction each step
	int roomChance = 10;       // % chance to carve a small room at walker position
	int minRoomSize = 3;       // Min room size when carving
	int maxRoomSize = 7;       // Max room size when carving
};

//=============================================================================
// DungeonConfig - Runtime generation parameters
//=============================================================================
struct DungeonConfig {
	int width = 70;
	int height = 70;
	Position center;
	std::string presetName;
	uint64_t seed = 0;

	Algorithm algorithm = Algorithm::RoomPlacement;
	RoomPlacementParams roomPlacement;
	BSPParams bsp;
	RandomWalkParams randomWalk;
};

//=============================================================================
// WallPlacement - Tracks placed wall positions for hangable decoration
//=============================================================================
struct WallPlacement {
	Position pos;
	enum Type { Horizontal, Vertical, Corner, Pillar };
	Type type;
	uint16_t itemId;
};

//=============================================================================
// DungeonGenerator - Main procedural generation engine
//=============================================================================
class DungeonGenerator {
public:
	DungeonGenerator(Editor* editor);
	~DungeonGenerator() = default;

	// Progress callback: (step_name, progress 0-100) -> return false to cancel
	using ProgressCallback = std::function<bool(const std::string& step, int progress)>;
	void setProgressCallback(ProgressCallback cb) { m_progressCallback = std::move(cb); }

	bool generate(const DungeonConfig& config, const DungeonPreset& preset);

	const std::string& getLastError() const { return m_lastError; }
	int getTilesGenerated() const { return m_tilesGenerated; }

private:
	Editor* m_editor;
	std::mt19937 m_rng;
	std::string m_lastError;
	int m_tilesGenerated = 0;
	ProgressCallback m_progressCallback;

	bool reportProgress(const std::string& step, int progress) {
		if (m_progressCallback) return m_progressCallback(step, progress);
		return true;
	}

	// Internal grid
	std::unordered_map<uint64_t, uint16_t> m_grid;
	std::vector<Position> m_floorPositions;
	std::vector<WallPlacement> m_wallPlacements;

	// Position hash helpers
	static uint64_t posHash(int x, int y);
	uint16_t getGridTile(int x, int y) const;
	void setGridTile(int x, int y, uint16_t id);
	bool isFloorTile(int x, int y, uint16_t floorId) const;

	// Common helpers
	void paintFloor(int x, int y, int z, uint16_t floorId,
	                std::vector<std::pair<Position, uint16_t>>& tileChanges);
	void fillBackground(int startX, int startY, int width, int height, int z, uint16_t fillId,
	                     std::vector<std::pair<Position, uint16_t>>& tileChanges);

	// ===== Layout algorithms =====
	// Each fills m_grid and m_floorPositions with floor tiles

	// Room Placement (improved with MST)
	struct Room { int x, y, w, h; int centerX() const { return x + w/2; } int centerY() const { return y + h/2; } };
	void generateRoomPlacement(int startX, int startY, int width, int height, int z,
	                           uint16_t floorId, const RoomPlacementParams& params,
	                           std::vector<std::pair<Position, uint16_t>>& tileChanges);
	void carveRoom(const Room& room, int z, uint16_t floorId,
	               std::vector<std::pair<Position, uint16_t>>& tileChanges);
	void carveCorridor(int x1, int y1, int x2, int y2, int z, uint16_t floorId,
	                   int pathWidth, std::vector<std::pair<Position, uint16_t>>& tileChanges);
	std::vector<std::pair<int,int>> buildMST(const std::vector<Room>& rooms);

	// BSP (Binary Space Partitioning)
	struct BSPNode {
		int x, y, w, h;
		std::unique_ptr<BSPNode> left, right;
		Room room; // Only leaf nodes have rooms
		bool hasRoom = false;
	};
	void generateBSP(int startX, int startY, int width, int height, int z,
	                  uint16_t floorId, const BSPParams& params,
	                  std::vector<std::pair<Position, uint16_t>>& tileChanges);
	void splitBSP(BSPNode* node, int depth, const BSPParams& params);
	void createBSPRooms(BSPNode* node, int startX, int startY, int z, uint16_t floorId,
	                    const BSPParams& params, std::vector<std::pair<Position, uint16_t>>& tileChanges);
	void connectBSPRooms(BSPNode* node, int startX, int startY, int z, uint16_t floorId,
	                     int corridorWidth, std::vector<std::pair<Position, uint16_t>>& tileChanges);
	Room getBSPRoomCenter(BSPNode* node);

	// Random Walk
	void generateRandomWalk(int startX, int startY, int width, int height, int z,
	                        uint16_t floorId, const RandomWalkParams& params,
	                        std::vector<std::pair<Position, uint16_t>>& tileChanges);

	// ===== Post-processing (shared by all algorithms) =====
	void buildWalls(int startX, int startY, int width, int height, int z,
	                uint16_t floorId, const WallConfig& wallConfig,
	                std::vector<std::pair<Position, uint16_t>>& tileChanges);
	void createHangableDetails(int z, const HangableConfig& hangables,
	                           std::vector<std::pair<Position, uint16_t>>& tileChanges);
	void applyPatches(int z, const DungeonPreset& preset,
	                  std::vector<std::pair<Position, uint16_t>>& tileChanges);
	void decorateFloors(int z, const DungeonPreset& preset,
	                    std::vector<std::pair<Position, uint16_t>>& tileChanges);
	void buildBorders(const std::vector<Position>& floorPositions, int z,
	                  const BorderConfig& borderConfig,
	                  std::vector<std::pair<Position, uint16_t>>& tileChanges);
	bool isPlacementValid(DetailGroup::Placement placement, int x, int y, uint16_t floorId) const;
	bool applyChanges(const std::vector<std::pair<Position, uint16_t>>& tileChanges);
};

} // namespace DungeonGen

#endif // RME_DUNGEON_GENERATOR_H
