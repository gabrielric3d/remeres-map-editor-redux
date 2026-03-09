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

#ifndef RME_AREA_DECORATION_H
#define RME_AREA_DECORATION_H

#include "map/position.h"
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <random>
#include <memory>
#include <queue>

class Editor;
class Map;
class Tile;
class Item;

namespace AreaDecoration {

//=============================================================================
// CompositeTile - One tile within a composite entry
//=============================================================================
struct CompositeTile {
	Position offset;
	std::vector<uint16_t> itemIds;
};

//=============================================================================
// ItemEntry - Decorative item or composite with weight
//=============================================================================
struct ItemEntry {
	uint16_t itemId = 0;
	int weight = 100;
	bool isComposite = false;
	std::vector<CompositeTile> compositeTiles;
	bool isCluster = false;
	int clusterCount = 3;
	int clusterRadius = 3;
	int clusterMinDistance = 2;
	bool hasCenterPoint = false;
	Position centerOffset;

	ItemEntry() = default;
	ItemEntry(uint16_t id, int w = 100) : itemId(id), weight(w), isComposite(false) {}

	static ItemEntry MakeComposite(const std::vector<CompositeTile>& tiles, int w = 100) {
		ItemEntry entry;
		entry.isComposite = true;
		entry.compositeTiles = tiles;
		entry.weight = w;
		return entry;
	}

	static ItemEntry MakeCluster(const std::vector<CompositeTile>& tiles, int w,
	                             int count, int radius, int minDistance) {
		ItemEntry entry;
		entry.isComposite = true;
		entry.isCluster = true;
		entry.compositeTiles = tiles;
		entry.weight = w;
		entry.clusterCount = count;
		entry.clusterRadius = radius;
		entry.clusterMinDistance = minDistance;
		return entry;
	}

	bool isCompositeEntry() const { return isComposite; }
	bool isClusterEntry() const { return isCluster; }
	bool hasCompositeTiles() const { return isComposite && !compositeTiles.empty(); }

	size_t getCompositeTileCount() const {
		return compositeTiles.size();
	}

	size_t getCompositeItemCount() const {
		size_t count = 0;
		for (const auto& tile : compositeTiles) {
			count += tile.itemIds.size();
		}
		return count;
	}

	uint16_t getRepresentativeItemId() const {
		if (!isComposite) return itemId;
		for (const auto& tile : compositeTiles) {
			for (uint16_t id : tile.itemIds) {
				if (id > 0) return id;
			}
		}
		return 0;
	}
};

//=============================================================================
// ItemGroup - Reusable collection of items
//=============================================================================
struct ItemGroup {
	std::string name;
	std::vector<ItemEntry> items;
	int totalWeight = 0;

	void recalculateWeights();
	const ItemEntry* selectRandom(std::mt19937& rng) const;
};

//=============================================================================
// RuleMode - Determines how a FloorRule operates
//=============================================================================
enum class RuleMode {
	SingleFloor,    // Match single floor ID
	FloorRange,     // Match floor ID range
	Cluster         // Cluster-based placement (no floor matching)
};

//=============================================================================
// FloorRule - Defines what items can spawn on specific floors
//=============================================================================
struct FloorRule {
	std::string name;

	// Floor matching: single ID or range
	uint16_t floorId = 0;
	uint16_t fromFloorId = 0;
	uint16_t toFloorId = 0;

	// Items for this rule
	std::vector<ItemEntry> items;

	// Border item to place on top of decoration items (0 = none)
	uint16_t borderItemId = 0;

	// Friend floor - bias placement toward another ground tile (0 = disabled)
	uint16_t friendFloorId = 0;
	uint16_t friendFromFloorId = 0;
	uint16_t friendToFloorId = 0;
	int friendChance = 0; // 0-100 (%)
	int friendStrength = 0; // 0-100 (stronger = tighter bias)

	// Placement settings
	int maxPlacements = -1;  // -1 = unlimited
	float density = 1.0f;    // 0.0 - 1.0
	int priority = 0;
	bool enabled = true;

	// Rule mode (determines which fields are active)
	RuleMode ruleMode = RuleMode::SingleFloor;

	// --- Cluster mode fields (only used when ruleMode == Cluster) ---
	std::vector<CompositeTile> clusterTiles;  // Items on a grid with offsets
	bool hasCenterPoint = false;
	Position centerOffset;  // Relative offset within the cluster grid

	// For centered mode: how many cluster instances to place
	int instanceCount = 1;
	int instanceMinDistance = 5;  // Min distance between instances

	bool matchesFloor(uint16_t groundId) const;
	bool isRangeRule() const { return ruleMode == RuleMode::FloorRange; }
	bool isClusterRule() const { return ruleMode == RuleMode::Cluster; }
	bool isFriendRange() const { return friendFromFloorId > 0 && friendToFloorId > 0; }
	bool matchesFriendFloor(uint16_t groundId) const {
		if (isFriendRange()) {
			return groundId >= friendFromFloorId && groundId <= friendToFloorId;
		}
		return groundId == friendFloorId;
	}
	bool hasFriendFloor() const { return (friendFloorId > 0) || isFriendRange(); }

	// Cluster helper methods
	void getClusterBounds(Position& outMin, Position& outMax) const;
	std::vector<uint16_t> getClusterItemIds() const;
	size_t getClusterTotalItemCount() const;
	uint16_t getClusterRepresentativeItemId() const;
};

//=============================================================================
// SpacingConfig
//=============================================================================
struct SpacingConfig {
	int minDistance = 1;
	int minSameItemDistance = 2;
	bool checkDiagonals = true;
};

//=============================================================================
// DistributionMode
//=============================================================================
enum class DistributionMode {
	PureRandom,
	Clustered,
	GridBased
};

struct DistributionConfig {
	DistributionMode mode = DistributionMode::PureRandom;
	float clusterStrength = 0.5f;
	int clusterCount = 3;
	int gridSpacingX = 3;
	int gridSpacingY = 3;
	int gridJitter = 1;
};

//=============================================================================
// AreaDefinition - Selected region to decorate
//=============================================================================
struct AreaDefinition {
	enum class Type {
		Rectangle,
		FloodFill,
		Selection
	};

	Type type = Type::Rectangle;

	// For Rectangle
	Position rectMin;
	Position rectMax;

	// For FloodFill
	Position floodOrigin;
	uint16_t floodTargetGround = 0;
	int floodMaxRadius = 100;

	std::vector<Position> getAllPositions(Map& map) const;
	bool contains(const Position& pos) const;
	void getBounds(Position& outMin, Position& outMax) const;

private:
	std::vector<Position> getPositionsRectangle() const;
	std::vector<Position> getPositionsFloodFill(Map& map) const;
	std::vector<Position> getPositionsFromSelection(Map& map) const;
};

//=============================================================================
// DecorationPreset - Complete configuration
//=============================================================================
struct DecorationPreset {
	std::string name;
	std::vector<FloorRule> floorRules;
	SpacingConfig spacing;
	DistributionConfig distribution;
	int maxItemsTotal = -1;
	bool skipBlockedTiles = true;
	uint64_t defaultSeed = 0;
	AreaDefinition area;
	bool hasArea = false;

	const FloorRule* findRule(uint16_t groundId) const;
	void getMatchingRules(uint16_t groundId, std::vector<const FloorRule*>& outRules) const;
	void sortRulesByPriority();
	bool validate(std::string& errorOut) const;

	// Serialization
	bool saveToFile(const std::string& filepath) const;
	bool loadFromFile(const std::string& filepath);
	std::string toXmlString() const;
	bool fromXmlString(const std::string& xml);
};

//=============================================================================
// PresetManager - Manages saved presets
//=============================================================================
class PresetManager {
public:
	static PresetManager& getInstance();

	bool loadPresets();
	bool savePresets();

	std::vector<std::string> getPresetNames() const;
	const DecorationPreset* getPreset(const std::string& name) const;
	bool addPreset(const DecorationPreset& preset);
	bool removePreset(const std::string& name);
	bool renamePreset(const std::string& oldName, const std::string& newName);

	std::string getPresetsDirectory() const;

private:
	PresetManager() = default;
	std::map<std::string, DecorationPreset> m_presets;
	bool m_loaded = false;
};

//=============================================================================
// PreviewItem - Single preview placement
//=============================================================================
struct PreviewItem {
	Position position;
	uint16_t itemId = 0;
	const FloorRule* sourceRule = nullptr;
};

//=============================================================================
// AppliedItem - Stored placement for last apply
//=============================================================================
struct AppliedItem {
	Position position;
	uint16_t itemId = 0;
};

//=============================================================================
// PreviewState - Non-destructive preview layer
//=============================================================================
class PreviewState {
public:
	std::vector<PreviewItem> items;
	int totalItemsPlaced = 0;
	std::unordered_map<uint16_t, int> itemCountById;
	std::unordered_map<const FloorRule*, int> placementsByRule;
	uint64_t seed = 0;
	bool isValid = false;
	std::string errorMessage;
	Position minPos;
	Position maxPos;

	void clear();
	bool hasItemAt(const Position& pos) const;
	std::vector<const PreviewItem*> getItemsAt(const Position& pos) const;
	void rebuildSpatialIndex();

private:
	std::unordered_map<uint64_t, std::vector<size_t>> spatialIndex;
	static uint64_t positionHash(const Position& pos);
};

//=============================================================================
// SpatialHashGrid - For efficient spacing checks
//=============================================================================
class SpatialHashGrid {
public:
	SpatialHashGrid(int cellSize = 8);

	void insert(const Position& pos, size_t itemIndex);
	std::vector<size_t> queryRadius(const Position& center, int radius) const;
	void clear();

private:
	int m_cellSize;
	std::unordered_map<uint64_t, std::vector<std::pair<Position, size_t>>> m_cells;
};

//=============================================================================
// DecorationEngine - Main processing class
//=============================================================================
class DecorationEngine {
public:
	DecorationEngine(Editor* editor);
	~DecorationEngine();

	void setArea(const AreaDefinition& area);
	void setPreset(const DecorationPreset& preset);

	bool generatePreview(uint64_t seed = 0);
	bool generatePreviewVirtual(int width, int height, uint16_t groundId, uint64_t seed = 0);
	bool rerollPreview();
	const PreviewState& getPreviewState() const { return m_previewState; }
	bool wasPreviewCapped() const { return m_previewWasCapped; }

	bool applyPreview();
	void clearPreview();
	bool removeLastApplied();
	bool hasLastApplied() const { return !m_lastAppliedItems.empty(); }
	size_t getLastAppliedCount() const { return m_lastAppliedItems.size(); }

	const std::string& getLastError() const { return m_lastError; }

private:
	Editor* m_editor;
	AreaDefinition m_area;
	DecorationPreset m_preset;
	PreviewState m_previewState;
	SpatialHashGrid m_spatialHash;
	std::string m_lastError;
	std::vector<AppliedItem> m_lastAppliedItems;
	std::vector<Position> m_clusterCenters;
	struct FriendDistanceLayer {
		int minX = 0;
		int minY = 0;
		int width = 0;
		int height = 0;
		std::vector<int> distances;
	};
	std::unordered_map<uint32_t, std::unordered_map<int, FriendDistanceLayer>> m_friendDistanceCache;

	std::mt19937 m_rng;
	uint64_t m_currentSeed = 0;
	bool m_virtualPreview = false;
	bool m_previewWasCapped = false;

	bool collectTileData(std::vector<std::pair<Position, uint16_t>>& outTiles);
	bool checkSpacing(const Position& pos, uint16_t itemId) const;
	bool validateTilePlacement(const Position& pos, uint16_t itemId) const;
	bool buildPlacementItems(const Position& basePos, const ItemEntry& entry,
	                         const FloorRule* rule, std::vector<PreviewItem>& outItems);
	bool checkSpacingForPlacement(const std::vector<PreviewItem>& placementItems) const;
	void commitPlacement(const std::vector<PreviewItem>& placementItems);
	bool checkClusterCenterSpacing(const Position& pos, int minDistance) const;
	void buildFriendDistanceCache(const std::vector<std::pair<Position, uint16_t>>& tiles);
	float applyFriendBias(const FloorRule* rule, const Position& pos, float baseDensity) const;
	int getFriendDistance(uint32_t friendKey, const Position& pos) const;

	void generatePureRandom(const std::vector<std::pair<Position, uint16_t>>& tiles);
	void generateClustered(const std::vector<std::pair<Position, uint16_t>>& tiles);
	void generateGridBased(const std::vector<std::pair<Position, uint16_t>>& tiles);

	void generateClusterCentered(const std::vector<std::pair<Position, uint16_t>>& tiles,
	                              const FloorRule& rule);
	void generateClusterRandom(const std::vector<std::pair<Position, uint16_t>>& tiles,
	                            const FloorRule& rule);

	const ItemEntry* selectItemFromRule(const FloorRule* rule);
};

//=============================================================================
// PreviewManager - Singleton for rendering integration
//=============================================================================
class PreviewManager {
public:
	static PreviewManager& getInstance();

	void setActivePreview(PreviewState* preview);
	PreviewState* getActivePreview() const { return m_activePreview; }
	void clearActivePreview();
	bool hasActivePreview() const;

	std::vector<const PreviewItem*> getPreviewItemsAt(const Position& pos) const;

	float getPreviewOpacity() const { return m_previewOpacity; }
	void setPreviewOpacity(float opacity) { m_previewOpacity = opacity; }

private:
	PreviewManager() = default;
	PreviewState* m_activePreview = nullptr;
	float m_previewOpacity = 0.7f;
};

} // namespace AreaDecoration

#endif // RME_AREA_DECORATION_H
