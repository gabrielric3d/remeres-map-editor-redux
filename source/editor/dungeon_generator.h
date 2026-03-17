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
	float chance = 0.0f; // 0.0 - 1.0
	enum Placement {
		Anywhere,
		NearWall,
		NorthWall,
		WestWall,
		Center
	};
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

	// Terrain
	uint16_t groundId = 0;
	uint16_t patchId = 0;
	uint16_t fillId = 0;
	uint16_t brushId = 0;

	// Structure
	WallConfig walls;
	BorderConfig borders;
	BorderConfig brushBorders;

	// Decoration
	std::vector<DetailGroup> details; // up to 6 groups
	HangableConfig hangables;

	// Serialization
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
// DungeonConfig - Runtime generation parameters
//=============================================================================
struct DungeonConfig {
	int width = 70;
	int height = 70;
	int pathWidth = 4;
	Position center; // center of generation area
	std::string presetName;
	uint64_t seed = 0; // 0 = random
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

	// Generate dungeon with given config and preset
	bool generate(const DungeonConfig& config, const DungeonPreset& preset);

	const std::string& getLastError() const { return m_lastError; }
	int getTilesGenerated() const { return m_tilesGenerated; }

private:
	Editor* m_editor;
	std::mt19937 m_rng;
	std::string m_lastError;
	int m_tilesGenerated = 0;

	// Internal grid: position hash -> tile ID placed
	std::unordered_map<uint64_t, uint16_t> m_grid;

	// Floor positions tracked during generation
	std::vector<Position> m_floorPositions;

	// Wall placements tracked for hangable decoration
	std::vector<WallPlacement> m_wallPlacements;

	// Position hash helper
	static uint64_t posHash(int x, int y);
	uint16_t getGridTile(int x, int y) const;
	void setGridTile(int x, int y, uint16_t id);
	bool isFloorTile(int x, int y, uint16_t floorId) const;

	// Generation steps (match Lua pipeline)
	void fillBackground(int startX, int startY, int width, int height, int z, uint16_t fillId,
	                     std::vector<std::pair<Position, uint16_t>>& tileChanges);

	struct Waypoint { int x; int y; };
	std::vector<Waypoint> generateWaypoints(int startX, int startY, int width, int height);
	std::vector<Waypoint> orderWaypoints(std::vector<Waypoint> waypoints);

	void carveRooms(const std::vector<Waypoint>& waypoints, int z, uint16_t floorId,
	                std::vector<std::pair<Position, uint16_t>>& tileChanges);
	void carveCorridors(const std::vector<Waypoint>& waypoints, int z, uint16_t floorId,
	                    int pathWidth, std::vector<std::pair<Position, uint16_t>>& tileChanges);

	void buildWalls(int startX, int startY, int width, int height, int z,
	                uint16_t floorId, const WallConfig& wallConfig,
	                std::vector<std::pair<Position, uint16_t>>& tileChanges);

	void createHangableDetails(int z, const HangableConfig& hangables,
	                           std::vector<std::pair<Position, uint16_t>>& tileChanges);

	void applyPatches(int z, const DungeonPreset& preset,
	                  std::vector<std::pair<Position, uint16_t>>& tileChanges);

	void decorateFloors(int z, const DungeonPreset& preset,
	                    std::vector<std::pair<Position, uint16_t>>& tileChanges);

	// Border building helper (shared by patches and brush borders)
	void buildBorders(const std::vector<Position>& floorPositions, int z,
	                  const BorderConfig& borderConfig,
	                  std::vector<std::pair<Position, uint16_t>>& tileChanges);

	// Placement validation for detail groups
	bool isPlacementValid(DetailGroup::Placement placement, int x, int y, uint16_t floorId) const;

	// Paint a floor tile and track it
	void paintFloor(int x, int y, int z, uint16_t floorId,
	                std::vector<std::pair<Position, uint16_t>>& tileChanges);

	// Apply all tile changes to map via Action system
	bool applyChanges(const std::vector<std::pair<Position, uint16_t>>& tileChanges);
};

} // namespace DungeonGen

#endif // RME_DUNGEON_GENERATOR_H
