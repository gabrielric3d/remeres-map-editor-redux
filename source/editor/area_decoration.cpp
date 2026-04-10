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
#include "editor/area_decoration.h"
#include "editor/editor.h"
#include "map/map.h"
#include "map/tile.h"
#include "game/item.h"
#include "item_definitions/core/item_definition_store.h"
#include "editor/action.h"
#include "editor/action_queue.h"
#include "editor/selection.h"
#include "map/tile_operations.h"
#include "ui/gui.h"
#include "util/file_system.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <numeric>
#include <sstream>
#include <wx/dir.h>
#include <wx/filename.h>

namespace AreaDecoration {

//=============================================================================
// ItemGroup Implementation
//=============================================================================

void ItemGroup::recalculateWeights() {
	totalWeight = 0;
	for (const auto& item : items) {
		totalWeight += item.weight;
	}
}

const ItemEntry* ItemGroup::selectRandom(std::mt19937& rng) const {
	if (items.empty() || totalWeight <= 0) return nullptr;

	std::uniform_int_distribution<int> dist(0, totalWeight - 1);
	int roll = dist(rng);

	int cumulative = 0;
	for (const auto& item : items) {
		cumulative += item.weight;
		if (roll < cumulative) {
			return &item;
		}
	}

	return &items.back();
}

//=============================================================================
// FloorRule Implementation
//=============================================================================

bool FloorRule::matchesFloor(uint16_t groundId) const {
	// Cluster rules don't match floors - they place independently
	if (ruleMode == RuleMode::Cluster) {
		return false;
	}
	if (ruleMode == RuleMode::FloorRange) {
		return groundId >= fromFloorId && groundId <= toFloorId;
	}
	return groundId == floorId;
}

void FloorRule::getClusterBounds(Position& outMin, Position& outMax) const {
	if (clusterTiles.empty()) {
		outMin = Position(0, 0, 0);
		outMax = Position(0, 0, 0);
		return;
	}

	outMin = clusterTiles[0].offset;
	outMax = clusterTiles[0].offset;

	for (const auto& tile : clusterTiles) {
		outMin.x = std::min(outMin.x, tile.offset.x);
		outMin.y = std::min(outMin.y, tile.offset.y);
		outMin.z = std::min(outMin.z, tile.offset.z);
		outMax.x = std::max(outMax.x, tile.offset.x);
		outMax.y = std::max(outMax.y, tile.offset.y);
		outMax.z = std::max(outMax.z, tile.offset.z);
	}
}

std::vector<uint16_t> FloorRule::getClusterItemIds() const {
	std::unordered_set<uint16_t> uniqueIds;
	for (const auto& tile : clusterTiles) {
		for (uint16_t id : tile.itemIds) {
			if (id > 0) {
				uniqueIds.insert(id);
			}
		}
	}
	return std::vector<uint16_t>(uniqueIds.begin(), uniqueIds.end());
}

size_t FloorRule::getClusterTotalItemCount() const {
	size_t count = 0;
	for (const auto& tile : clusterTiles) {
		count += tile.itemIds.size();
	}
	return count;
}

uint16_t FloorRule::getClusterRepresentativeItemId() const {
	for (const auto& tile : clusterTiles) {
		for (uint16_t id : tile.itemIds) {
			if (id > 0) return id;
		}
	}
	return 0;
}

//=============================================================================
// DecorationPreset Implementation
//=============================================================================

void DecorationPreset::sortRulesByPriority() {
	std::sort(floorRules.begin(), floorRules.end(),
		[](const FloorRule& a, const FloorRule& b) {
			return a.priority > b.priority;
		});
}

const FloorRule* DecorationPreset::findRule(uint16_t groundId) const {
	for (const auto& rule : floorRules) {
		if (rule.enabled && rule.matchesFloor(groundId)) {
			return &rule;
		}
	}
	return nullptr;
}

void DecorationPreset::getMatchingRules(uint16_t groundId, std::vector<const FloorRule*>& outRules) const {
	outRules.clear();
	for (const auto& rule : floorRules) {
		if (rule.enabled && rule.matchesFloor(groundId)) {
			outRules.push_back(&rule);
		}
	}
}

bool DecorationPreset::validate(std::string& errorOut) const {
	if (floorRules.empty()) {
		errorOut = "No floor rules defined";
		return false;
	}

	for (const auto& rule : floorRules) {
		if (rule.isClusterRule()) {
			// Cluster rule validation
			if (rule.clusterTiles.empty()) {
				errorOut = "Cluster rule '" + rule.name + "' has no cluster tiles";
				return false;
			}
			bool hasClusterItems = false;
			for (const auto& tile : rule.clusterTiles) {
				if (!tile.itemIds.empty()) {
					hasClusterItems = true;
					break;
				}
			}
			if (!hasClusterItems) {
				errorOut = "Cluster rule '" + rule.name + "' has no items in cluster tiles";
				return false;
			}
			if (rule.hasCenterPoint) {
				Position cMin, cMax;
				rule.getClusterBounds(cMin, cMax);
				if (rule.centerOffset.x < cMin.x || rule.centerOffset.x > cMax.x ||
				    rule.centerOffset.y < cMin.y || rule.centerOffset.y > cMax.y ||
				    rule.centerOffset.z < cMin.z || rule.centerOffset.z > cMax.z) {
					errorOut = "Cluster rule '" + rule.name + "' centerOffset is outside cluster bounds";
					return false;
				}
			}
			if (rule.instanceCount <= 0) {
				errorOut = "Cluster rule '" + rule.name + "' instanceCount must be > 0";
				return false;
			}
		} else if (rule.isRangeRule()) {
			if (rule.fromFloorId > rule.toFloorId) {
				errorOut = "Invalid floor range: fromFloorId > toFloorId";
				return false;
			}
		} else if (rule.floorId == 0) {
			errorOut = "Floor rule has no floor ID specified";
			return false;
		}
		if (rule.friendChance < 0 || rule.friendChance > 100) {
			errorOut = "Friend chance must be between 0 and 100";
			return false;
		}
		if (rule.friendStrength < 0 || rule.friendStrength > 100) {
			errorOut = "Friend strength must be between 0 and 100";
			return false;
		}
		if (rule.isFriendRange() && rule.friendFromFloorId > rule.friendToFloorId) {
			errorOut = "Invalid friend floor range: fromFloorId > toFloorId";
			return false;
		}

		// Cluster rules use clusterTiles, not items list
		if (!rule.isClusterRule() && rule.items.empty()) {
			errorOut = "Floor rule '" + rule.name + "' has no items";
			return false;
		}

		for (const auto& item : rule.items) {
			if (item.weight <= 0) {
				errorOut = "Item weight must be positive";
				return false;
			}

			if (item.isCompositeEntry()) {
				bool hasItems = false;
				if (item.compositeTiles.empty()) {
					errorOut = "Composite entry has no tiles";
					return false;
				}
				for (const auto& tile : item.compositeTiles) {
					if (!tile.itemIds.empty()) {
						hasItems = true;
						break;
					}
				}
				if (!hasItems) {
					errorOut = "Composite entry has no items";
					return false;
				}
				if (item.isClusterEntry()) {
					if (item.clusterCount <= 0) {
						errorOut = "Cluster entry has invalid count";
						return false;
					}
					if (item.clusterRadius < 0) {
						errorOut = "Cluster entry has invalid radius";
						return false;
					}
					if (item.clusterMinDistance < 0) {
						errorOut = "Cluster entry has invalid spacing";
						return false;
					}
				}
			} else if (item.itemId == 0) {
				errorOut = "Item entry has invalid item ID";
				return false;
			}
		}
	}

	if (spacing.minDistance < 0) {
		errorOut = "minDistance cannot be negative";
		return false;
	}

	return true;
}

//=============================================================================
// PreviewState Implementation
//=============================================================================

void PreviewState::clear() {
	items.clear();
	totalItemsPlaced = 0;
	itemCountById.clear();
	placementsByRule.clear();
	seed = 0;
	isValid = false;
	errorMessage.clear();
	spatialIndex.clear();
	minPos = Position();
	maxPos = Position();
}

uint64_t PreviewState::positionHash(const Position& pos) {
	return (static_cast<uint64_t>(pos.x & 0xFFFF) << 32) |
	       (static_cast<uint64_t>(pos.y & 0xFFFF) << 16) |
	       static_cast<uint64_t>(pos.z & 0xFFFF);
}

bool PreviewState::hasItemAt(const Position& pos) const {
	uint64_t hash = positionHash(pos);
	auto it = spatialIndex.find(hash);
	return it != spatialIndex.end() && !it->second.empty();
}

std::vector<const PreviewItem*> PreviewState::getItemsAt(const Position& pos) const {
	std::vector<const PreviewItem*> result;
	uint64_t hash = positionHash(pos);
	auto it = spatialIndex.find(hash);
	if (it != spatialIndex.end()) {
		for (size_t idx : it->second) {
			if (idx < items.size()) {
				result.push_back(&items[idx]);
			}
		}
	}
	return result;
}

void PreviewState::rebuildSpatialIndex() {
	spatialIndex.clear();
	for (size_t i = 0; i < items.size(); ++i) {
		uint64_t hash = positionHash(items[i].position);
		spatialIndex[hash].push_back(i);
	}

	if (!items.empty()) {
		minPos = items[0].position;
		maxPos = items[0].position;
		for (const auto& item : items) {
			minPos.x = std::min(minPos.x, item.position.x);
			minPos.y = std::min(minPos.y, item.position.y);
			minPos.z = std::min(minPos.z, item.position.z);
			maxPos.x = std::max(maxPos.x, item.position.x);
			maxPos.y = std::max(maxPos.y, item.position.y);
			maxPos.z = std::max(maxPos.z, item.position.z);
		}
	}
}

//=============================================================================
// AreaDefinition Implementation
//=============================================================================

std::vector<Position> AreaDefinition::getAllPositions(Map& map) const {
	switch (type) {
		case Type::Rectangle:
			return getPositionsRectangle();
		case Type::FloodFill:
			return getPositionsFloodFill(map);
		case Type::Selection:
			return getPositionsFromSelection(map);
	}
	return {};
}

bool AreaDefinition::contains(const Position& pos) const {
	switch (type) {
		case Type::Rectangle:
			return pos.x >= rectMin.x && pos.x <= rectMax.x &&
			       pos.y >= rectMin.y && pos.y <= rectMax.y &&
			       pos.z >= rectMin.z && pos.z <= rectMax.z;
		default:
			return false;
	}
}

void AreaDefinition::getBounds(Position& outMin, Position& outMax) const {
	switch (type) {
		case Type::Rectangle:
			outMin = rectMin;
			outMax = rectMax;
			break;
		case Type::FloodFill:
			outMin = Position(floodOrigin.x - floodMaxRadius,
			                  floodOrigin.y - floodMaxRadius,
			                  floodOrigin.z);
			outMax = Position(floodOrigin.x + floodMaxRadius,
			                  floodOrigin.y + floodMaxRadius,
			                  floodOrigin.z);
			break;
		default:
			outMin = Position(0, 0, 0);
			outMax = Position(0, 0, 0);
			break;
	}
}

std::vector<Position> AreaDefinition::getPositionsRectangle() const {
	std::vector<Position> result;

	int minX = std::min(rectMin.x, rectMax.x);
	int maxX = std::max(rectMin.x, rectMax.x);
	int minY = std::min(rectMin.y, rectMax.y);
	int maxY = std::max(rectMin.y, rectMax.y);
	int minZ = std::min(rectMin.z, rectMax.z);
	int maxZ = std::max(rectMin.z, rectMax.z);

	result.reserve((maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1));

	for (int z = minZ; z <= maxZ; ++z) {
		for (int y = minY; y <= maxY; ++y) {
			for (int x = minX; x <= maxX; ++x) {
				result.push_back(Position(x, y, z));
			}
		}
	}

	return result;
}

std::vector<Position> AreaDefinition::getPositionsFloodFill(Map& map) const {
	std::vector<Position> result;
	std::unordered_set<uint64_t> visited;
	std::queue<Position> queue;

	uint16_t targetGround = floodTargetGround;
	if (targetGround == 0) {
		Tile* originTile = map.getTile(floodOrigin);
		if (!originTile || !originTile->ground) return result;
		targetGround = originTile->ground->getID();
	}

	queue.push(floodOrigin);

	while (!queue.empty()) {
		Position pos = queue.front();
		queue.pop();

		int dist = std::abs(pos.x - floodOrigin.x) + std::abs(pos.y - floodOrigin.y);
		if (dist > floodMaxRadius) continue;

		uint64_t hash = (static_cast<uint64_t>(pos.x & 0xFFFF) << 32) |
		                (static_cast<uint64_t>(pos.y & 0xFFFF) << 16) |
		                static_cast<uint64_t>(pos.z & 0xFFFF);
		if (visited.count(hash)) continue;
		visited.insert(hash);

		Tile* tile = map.getTile(pos);
		if (!tile || !tile->ground) continue;
		if (tile->ground->getID() != targetGround) continue;

		result.push_back(pos);

		queue.push(Position(pos.x - 1, pos.y, pos.z));
		queue.push(Position(pos.x + 1, pos.y, pos.z));
		queue.push(Position(pos.x, pos.y - 1, pos.z));
		queue.push(Position(pos.x, pos.y + 1, pos.z));
	}

	return result;
}

std::vector<Position> AreaDefinition::getPositionsFromSelection(Map& map) const {
	std::vector<Position> result;

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) return result;

	const Selection& selection = editor->selection;
	const auto& tiles = selection.getTiles();
	for (Tile* tile : tiles) {
		result.push_back(tile->getPosition());
	}

	return result;
}

//=============================================================================
// SpatialHashGrid Implementation
//=============================================================================

SpatialHashGrid::SpatialHashGrid(int cellSize) : m_cellSize(cellSize) {}

void SpatialHashGrid::insert(const Position& pos, size_t itemIndex) {
	int cx = pos.x / m_cellSize;
	int cy = pos.y / m_cellSize;
	uint64_t cellHash = (static_cast<uint64_t>(cx) << 32) | static_cast<uint64_t>(cy);
	m_cells[cellHash].push_back({pos, itemIndex});
}

std::vector<size_t> SpatialHashGrid::queryRadius(const Position& center, int radius) const {
	std::vector<size_t> result;

	int cellRadius = (radius / m_cellSize) + 1;
	int cx = center.x / m_cellSize;
	int cy = center.y / m_cellSize;

	for (int dy = -cellRadius; dy <= cellRadius; ++dy) {
		for (int dx = -cellRadius; dx <= cellRadius; ++dx) {
			uint64_t cellHash = (static_cast<uint64_t>(cx + dx) << 32) |
			                    static_cast<uint64_t>(cy + dy);
			auto it = m_cells.find(cellHash);
			if (it != m_cells.end()) {
				for (const auto& entry : it->second) {
					int dist = std::max(std::abs(entry.first.x - center.x),
					                    std::abs(entry.first.y - center.y));
					if (dist <= radius) {
						result.push_back(entry.second);
					}
				}
			}
		}
	}

	return result;
}

void SpatialHashGrid::clear() {
	m_cells.clear();
}

//=============================================================================
// PreviewManager Implementation
//=============================================================================

PreviewManager& PreviewManager::getInstance() {
	static PreviewManager instance;
	return instance;
}

void PreviewManager::setActivePreview(PreviewState* preview) {
	m_activePreview = preview;
}

void PreviewManager::clearActivePreview() {
	m_activePreview = nullptr;
}

bool PreviewManager::hasActivePreview() const {
	return m_activePreview != nullptr && m_activePreview->isValid;
}

std::vector<const PreviewItem*> PreviewManager::getPreviewItemsAt(const Position& pos) const {
	if (!hasActivePreview()) {
		return {};
	}
	return m_activePreview->getItemsAt(pos);
}

//=============================================================================
// DecorationEngine Implementation
//=============================================================================

DecorationEngine::DecorationEngine(Editor* editor)
	: m_editor(editor), m_spatialHash(8) {
}

DecorationEngine::~DecorationEngine() {
	clearPreview();
}

void DecorationEngine::setArea(const AreaDefinition& area) {
	m_area = area;
	clearPreview();
}

// IMPORTANT: clearPreview() must be called before m_preset is modified,
// because PreviewItem.sourceRule stores raw pointers to elements of m_preset.floorRules.
// Modifying m_preset without clearing preview would create dangling pointers.
void DecorationEngine::setPreset(const DecorationPreset& preset) {
	clearPreview();
	m_preset = preset;
	m_preset.sortRulesByPriority();
}

bool DecorationEngine::generatePreview(uint64_t seed) {
	clearPreview();
	m_virtualPreview = false;
	m_previewWasCapped = false;

	if (seed == 0) {
		if (m_preset.defaultSeed != 0) {
			seed = m_preset.defaultSeed;
		} else {
			std::random_device rd;
			seed = rd();
		}
	}

	m_currentSeed = seed;
	m_rng.seed(static_cast<unsigned int>(seed));
	m_previewState.seed = seed;

	std::string validationError;
	if (!m_preset.validate(validationError)) {
		m_lastError = "Invalid preset: " + validationError;
		m_previewState.errorMessage = m_lastError;
		return false;
	}

	std::vector<std::pair<Position, uint16_t>> tiles;
	if (!collectTileData(tiles)) {
		m_lastError = "Failed to collect tile data from area";
		m_previewState.errorMessage = m_lastError;
		return false;
	}

	if (tiles.empty()) {
		m_lastError = "No valid tiles found in selected area";
		m_previewState.errorMessage = m_lastError;
		return false;
	}

	m_spatialHash.clear();
	buildFriendDistanceCache(tiles);

	switch (m_preset.distribution.mode) {
		case DistributionMode::PureRandom:
			generatePureRandom(tiles);
			break;
		case DistributionMode::Clustered:
			generateClustered(tiles);
			break;
		case DistributionMode::GridBased:
			generateGridBased(tiles);
			break;
	}

	// Process cluster rules (separate pass)
	for (const auto& rule : m_preset.floorRules) {
		if (!rule.enabled || !rule.isClusterRule()) continue;
		if (rule.hasCenterPoint) {
			generateClusterCentered(tiles, rule);
		} else {
			generateClusterRandom(tiles, rule);
		}
	}

	m_previewState.rebuildSpatialIndex();
	m_previewState.isValid = true;

	PreviewManager::getInstance().setActivePreview(&m_previewState);

	return true;
}

bool DecorationEngine::generatePreviewVirtual(int width, int height, uint16_t groundId, uint64_t seed) {
	clearPreview();
	m_virtualPreview = true;
	m_previewWasCapped = false;

	if (seed == 0) {
		if (m_preset.defaultSeed != 0) {
			seed = m_preset.defaultSeed;
		} else {
			std::random_device rd;
			seed = rd();
		}
	}

	m_currentSeed = seed;
	m_rng.seed(static_cast<unsigned int>(seed));
	m_previewState.seed = seed;

	std::string validationError;
	if (!m_preset.validate(validationError)) {
		m_lastError = "Invalid preset: " + validationError;
		m_previewState.errorMessage = m_lastError;
		return false;
	}

	if (width <= 0 || height <= 0 || groundId == 0) {
		m_lastError = "Invalid virtual preview configuration";
		m_previewState.errorMessage = m_lastError;
		return false;
	}

	std::vector<std::pair<Position, uint16_t>> tiles;
	tiles.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			tiles.push_back({Position(x, y, 0), groundId});
		}
	}

	m_spatialHash.clear();
	buildFriendDistanceCache(tiles);

	switch (m_preset.distribution.mode) {
		case DistributionMode::PureRandom:
			generatePureRandom(tiles);
			break;
		case DistributionMode::Clustered:
			generateClustered(tiles);
			break;
		case DistributionMode::GridBased:
			generateGridBased(tiles);
			break;
	}

	// Process cluster rules (separate pass)
	for (const auto& rule : m_preset.floorRules) {
		if (!rule.enabled || !rule.isClusterRule()) continue;
		if (rule.hasCenterPoint) {
			generateClusterCentered(tiles, rule);
		} else {
			generateClusterRandom(tiles, rule);
		}
	}

	m_previewState.rebuildSpatialIndex();
	m_previewState.isValid = true;
	return true;
}

bool DecorationEngine::rerollPreview() {
	std::random_device rd;
	uint64_t newSeed = rd();
	return generatePreview(newSeed);
}

void DecorationEngine::clearPreview() {
	PreviewManager::getInstance().clearActivePreview();
	m_previewState.clear();
	m_spatialHash.clear();
	m_clusterCenters.clear();
	m_friendDistanceCache.clear();
	m_virtualPreview = false;
	m_previewWasCapped = false;
}

bool DecorationEngine::checkClusterCenterSpacing(const Position& pos, int minDistance) const {
	if (minDistance <= 0) {
		return true;
	}
	for (const auto& center : m_clusterCenters) {
		if (center.z != pos.z) {
			continue;
		}
		int dx = std::abs(center.x - pos.x);
		int dy = std::abs(center.y - pos.y);
		int dist = std::max(dx, dy);
		if (dist < minDistance) {
			return false;
		}
	}
	return true;
}

void DecorationEngine::buildFriendDistanceCache(const std::vector<std::pair<Position, uint16_t>>& tiles) {
	m_friendDistanceCache.clear();
	struct FriendRange {
		uint16_t from = 0;
		uint16_t to = 0;
		uint32_t key = 0;
	};

	auto makeFriendKey = [](uint16_t from, uint16_t to) -> uint32_t {
		return (static_cast<uint32_t>(from) << 16) | static_cast<uint32_t>(to);
	};

	std::unordered_set<uint32_t> friendKeys;
	std::vector<FriendRange> friendRanges;
	friendRanges.reserve(m_preset.floorRules.size());

	for (const auto& rule : m_preset.floorRules) {
		if (!rule.enabled || rule.friendChance <= 0 || !rule.hasFriendFloor()) {
			continue;
		}
		uint16_t from = rule.isFriendRange() ? rule.friendFromFloorId : rule.friendFloorId;
		uint16_t to = rule.isFriendRange() ? rule.friendToFloorId : rule.friendFloorId;
		if (from == 0 || to == 0) {
			continue;
		}
		if (from > to) {
			std::swap(from, to);
		}
		uint32_t key = makeFriendKey(from, to);
		if (friendKeys.insert(key).second) {
			friendRanges.push_back({from, to, key});
		}
	}

	if (friendRanges.empty()) {
		return;
	}

	struct Bounds {
		int minX = 0;
		int minY = 0;
		int maxX = 0;
		int maxY = 0;
		bool initialized = false;
	};

	std::unordered_map<int, Bounds> boundsByZ;
	boundsByZ.reserve(8);

	for (const auto& tile : tiles) {
		const Position& pos = tile.first;
		auto& bounds = boundsByZ[pos.z];
		if (!bounds.initialized) {
			bounds.minX = pos.x;
			bounds.maxX = pos.x;
			bounds.minY = pos.y;
			bounds.maxY = pos.y;
			bounds.initialized = true;
		} else {
			bounds.minX = std::min(bounds.minX, pos.x);
			bounds.maxX = std::max(bounds.maxX, pos.x);
			bounds.minY = std::min(bounds.minY, pos.y);
			bounds.maxY = std::max(bounds.maxY, pos.y);
		}
	}

	std::unordered_map<uint32_t, std::unordered_map<int, std::vector<Position>>> friendPositions;
	if (m_editor && !m_virtualPreview) {
		Map& map = m_editor->map;
		const int kFriendPadding = 2;
		for (auto& boundsPair : boundsByZ) {
			int z = boundsPair.first;
			Bounds& bounds = boundsPair.second;
			if (!bounds.initialized) {
				continue;
			}
			bounds.minX -= kFriendPadding;
			bounds.minY -= kFriendPadding;
			bounds.maxX += kFriendPadding;
			bounds.maxY += kFriendPadding;

			for (int y = bounds.minY; y <= bounds.maxY; ++y) {
				for (int x = bounds.minX; x <= bounds.maxX; ++x) {
					Tile* tile = map.getTile(x, y, z);
					if (!tile || !tile->ground) {
						continue;
					}
					uint16_t groundId = tile->ground->getID();
					for (const auto& range : friendRanges) {
						if (groundId >= range.from && groundId <= range.to) {
							friendPositions[range.key][z].push_back(Position(x, y, z));
						}
					}
				}
			}
		}
	} else {
		for (const auto& tile : tiles) {
			uint16_t groundId = tile.second;
			for (const auto& range : friendRanges) {
				if (groundId >= range.from && groundId <= range.to) {
					friendPositions[range.key][tile.first.z].push_back(tile.first);
				}
			}
		}
	}

	for (const auto& range : friendRanges) {
		auto positionsIt = friendPositions.find(range.key);
		if (positionsIt == friendPositions.end()) {
			continue;
		}

		for (const auto& zEntry : positionsIt->second) {
			int z = zEntry.first;
			auto boundsIt = boundsByZ.find(z);
			if (boundsIt == boundsByZ.end() || !boundsIt->second.initialized) {
				continue;
			}

			const Bounds& bounds = boundsIt->second;
			int width = bounds.maxX - bounds.minX + 1;
			int height = bounds.maxY - bounds.minY + 1;
			if (width <= 0 || height <= 0) {
				continue;
			}

			FriendDistanceLayer layer;
			layer.minX = bounds.minX;
			layer.minY = bounds.minY;
			layer.width = width;
			layer.height = height;
			layer.distances.assign(static_cast<size_t>(width) * static_cast<size_t>(height), -1);

			std::queue<Position> queue;
			for (const Position& pos : zEntry.second) {
				int x = pos.x - bounds.minX;
				int y = pos.y - bounds.minY;
				if (x < 0 || y < 0 || x >= width || y >= height) {
					continue;
				}
				size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
				if (layer.distances[idx] == 0) {
					continue;
				}
				layer.distances[idx] = 0;
				queue.push(pos);
			}

			static const int kOffsets[8][2] = {
				{-1, -1}, {0, -1}, {1, -1},
				{-1,  0},          {1,  0},
				{-1,  1}, {0,  1}, {1,  1}
			};

			while (!queue.empty()) {
				Position current = queue.front();
				queue.pop();

				int baseX = current.x - bounds.minX;
				int baseY = current.y - bounds.minY;
				size_t baseIdx = static_cast<size_t>(baseY) * static_cast<size_t>(width) + static_cast<size_t>(baseX);
				int baseDist = layer.distances[baseIdx];

				for (const auto& offset : kOffsets) {
					int nx = current.x + offset[0];
					int ny = current.y + offset[1];
					if (nx < bounds.minX || nx > bounds.maxX || ny < bounds.minY || ny > bounds.maxY) {
						continue;
					}

					int lx = nx - bounds.minX;
					int ly = ny - bounds.minY;
					size_t nIdx = static_cast<size_t>(ly) * static_cast<size_t>(width) + static_cast<size_t>(lx);
					if (layer.distances[nIdx] != -1) {
						continue;
					}

					layer.distances[nIdx] = baseDist + 1;
					queue.push(Position(nx, ny, z));
				}
			}

			m_friendDistanceCache[range.key][z] = std::move(layer);
		}
	}
}

int DecorationEngine::getFriendDistance(uint32_t friendKey, const Position& pos) const {
	auto friendIt = m_friendDistanceCache.find(friendKey);
	if (friendIt == m_friendDistanceCache.end()) {
		return -1;
	}
	auto layerIt = friendIt->second.find(pos.z);
	if (layerIt == friendIt->second.end()) {
		return -1;
	}
	const FriendDistanceLayer& layer = layerIt->second;
	if (pos.x < layer.minX || pos.x >= layer.minX + layer.width ||
	    pos.y < layer.minY || pos.y >= layer.minY + layer.height) {
		return -1;
	}
	size_t idx = static_cast<size_t>(pos.y - layer.minY) * static_cast<size_t>(layer.width) +
	             static_cast<size_t>(pos.x - layer.minX);
	if (idx >= layer.distances.size()) {
		return -1;
	}
	return layer.distances[idx];
}

float DecorationEngine::applyFriendBias(const FloorRule* rule, const Position& pos, float baseDensity) const {
	if (!rule || rule->friendChance <= 0 || !rule->hasFriendFloor()) {
		return baseDensity;
	}
	uint16_t from = rule->isFriendRange() ? rule->friendFromFloorId : rule->friendFloorId;
	uint16_t to = rule->isFriendRange() ? rule->friendToFloorId : rule->friendFloorId;
	if (from == 0 || to == 0) {
		return baseDensity;
	}
	if (from > to) {
		std::swap(from, to);
	}
	uint32_t key = (static_cast<uint32_t>(from) << 16) | static_cast<uint32_t>(to);
	int distance = getFriendDistance(key, pos);
	if (distance < 0) {
		return baseDensity;
	}

	float friendChance = rule->friendChance / 100.0f;
	if (friendChance < 0.0f) {
		friendChance = 0.0f;
	} else if (friendChance > 1.0f) {
		friendChance = 1.0f;
	}

	float proximity = 1.0f / (1.0f + static_cast<float>(distance));
	if (rule->friendStrength > 0) {
		float exponent = 1.0f + (static_cast<float>(rule->friendStrength) / 20.0f);
		proximity = std::pow(proximity, exponent);
	}
	float biasedDensity = baseDensity * proximity;
	return baseDensity * (1.0f - friendChance) + biasedDensity * friendChance;
}

bool DecorationEngine::collectTileData(std::vector<std::pair<Position, uint16_t>>& outTiles) {
	if (!m_editor) return false;

	Map& map = m_editor->map;
	std::vector<Position> positions = m_area.getAllPositions(map);

	std::vector<std::pair<uint16_t, uint16_t>> friendRanges;
	friendRanges.reserve(m_preset.floorRules.size());
	for (const auto& rule : m_preset.floorRules) {
		if (!rule.enabled || rule.friendChance <= 0 || !rule.hasFriendFloor()) {
			continue;
		}
		uint16_t from = rule.isFriendRange() ? rule.friendFromFloorId : rule.friendFloorId;
		uint16_t to = rule.isFriendRange() ? rule.friendToFloorId : rule.friendFloorId;
		if (from == 0 || to == 0) {
			continue;
		}
		if (from > to) {
			std::swap(from, to);
		}
		friendRanges.emplace_back(from, to);
	}

	// Check if any enabled cluster rules exist
	bool hasAnyClusterRules = false;
	for (const auto& rule : m_preset.floorRules) {
		if (rule.enabled && rule.isClusterRule()) {
			hasAnyClusterRules = true;
			break;
		}
	}

	outTiles.reserve(positions.size());

	for (const Position& pos : positions) {
		Tile* tile = map.getTile(pos);
		if (!tile) continue;

		Item* ground = tile->ground.get();
		if (!ground) {
			// If cluster rules exist, include tiles without ground as well
			// (using groundId 0 so they won't match regular floor rules)
			if (hasAnyClusterRules) {
				outTiles.push_back({pos, 0});
			}
			continue;
		}

		uint16_t groundId = ground->getID();

		bool isFriendGround = false;
		if (!friendRanges.empty()) {
			for (const auto& range : friendRanges) {
				if (groundId >= range.first && groundId <= range.second) {
					isFriendGround = true;
					break;
				}
			}
		}

		if (m_preset.skipBlockedTiles && tile->isBlocking() && !isFriendGround && !hasAnyClusterRules) {
			continue;
		}

		outTiles.push_back({pos, groundId});
	}

	return true;
}

bool DecorationEngine::checkSpacing(const Position& pos, uint16_t itemId) const {
	const SpacingConfig& spacing = m_preset.spacing;

	int maxRadius = std::max(spacing.minDistance, spacing.minSameItemDistance);
	auto nearby = m_spatialHash.queryRadius(pos, maxRadius);

	for (size_t idx : nearby) {
		if (idx >= m_previewState.items.size()) continue;

		const PreviewItem& existing = m_previewState.items[idx];

		if (existing.position.z != pos.z) continue;

		int dx = std::abs(pos.x - existing.position.x);
		int dy = std::abs(pos.y - existing.position.y);

		int distance;
		if (spacing.checkDiagonals) {
			distance = std::max(dx, dy);
		} else {
			distance = dx + dy;
		}

		if (distance < spacing.minDistance) {
			return false;
		}

		if (itemId == existing.itemId && distance < spacing.minSameItemDistance) {
			return false;
		}
	}

	return true;
}

bool DecorationEngine::validateTilePlacement(const Position& pos, uint16_t itemId) const {
	if (m_virtualPreview) return true;
	if (!m_editor) return false;

	Map& map = m_editor->map;
	Tile* tile = map.getTile(pos);

	if (!tile) return false;

	if (m_preset.skipBlockedTiles && tile->isBlocking()) {
		return false;
	}

	return true;
}

bool DecorationEngine::buildPlacementItems(const Position& basePos, const ItemEntry& entry,
                                           const FloorRule* rule, std::vector<PreviewItem>& outItems) {
	outItems.clear();

	// Helper to add border item on top of placed items at a position
	auto addBorderItem = [&](const Position& pos) {
		if (rule && rule->borderItemId > 0) {
			PreviewItem borderItem;
			borderItem.position = pos;
			borderItem.itemId = rule->borderItemId;
			borderItem.sourceRule = rule;
			outItems.push_back(borderItem);
		}
	};

	if (!entry.isCompositeEntry()) {
		if (entry.itemId == 0) return false;
		if (!validateTilePlacement(basePos, entry.itemId)) return false;

		PreviewItem previewItem;
		previewItem.position = basePos;
		previewItem.itemId = entry.itemId;
		previewItem.sourceRule = rule;
		outItems.push_back(previewItem);

		// Add border item on top
		addBorderItem(basePos);

		return true;
	}

	if (entry.compositeTiles.empty()) return false;

	// Track positions where we placed items (to add border items)
	std::vector<Position> placedPositions;

	auto appendCompositeAt = [&](const Position& origin) -> bool {
		for (const auto& tile : entry.compositeTiles) {
			if (tile.itemIds.empty()) continue;
			Position pos = origin + tile.offset;

			// Skip tiles without ground when requireGround is active
			if (rule && rule->requireGround && !m_virtualPreview && m_editor) {
				Tile* mapTile = m_editor->map.getTile(pos);
				if (!mapTile || !mapTile->ground) {
					continue;
				}
			}

			uint16_t validateId = 0;
			for (uint16_t id : tile.itemIds) {
				if (id > 0) {
					validateId = id;
					break;
				}
			}
			if (validateId == 0) continue;

			if (!validateTilePlacement(pos, validateId)) {
				return false;
			}

			bool addedAny = false;
			for (uint16_t id : tile.itemIds) {
				if (id == 0) continue;
				PreviewItem previewItem;
				previewItem.position = pos;
				previewItem.itemId = id;
				previewItem.sourceRule = rule;
				outItems.push_back(previewItem);
				addedAny = true;
			}

			// Track position for border item
			if (addedAny) {
				placedPositions.push_back(pos);
			}
		}
		return true;
	};

	if (!entry.isClusterEntry()) {
		if (!appendCompositeAt(basePos)) return false;
		// Add border items on top of all placed positions
		for (const auto& pos : placedPositions) {
			addBorderItem(pos);
		}
		return !outItems.empty();
	}

	int count = std::max(1, entry.clusterCount);
	int radius = std::max(0, entry.clusterRadius);
	int minDist = std::max(0, entry.clusterMinDistance);

	std::vector<Position> centers;
	centers.reserve(static_cast<size_t>(count));
	centers.push_back(Position(0, 0, 0));

	std::uniform_int_distribution<int> offsetDist(-radius, radius);
	for (int i = 1; i < count; ++i) {
		bool placed = false;
		for (int attempt = 0; attempt < 20; ++attempt) {
			int dx = offsetDist(m_rng);
			int dy = offsetDist(m_rng);
			if (dx == 0 && dy == 0) continue;

			bool tooClose = false;
			for (const auto& center : centers) {
				int dist = std::max(std::abs(center.x - dx), std::abs(center.y - dy));
				if (dist < minDist) {
					tooClose = true;
					break;
				}
			}
			if (tooClose) continue;

			centers.push_back(Position(dx, dy, 0));
			placed = true;
			break;
		}
		if (!placed) {
			// If we can't place more centers, stop early
			break;
		}
	}

	for (const auto& center : centers) {
		if (!appendCompositeAt(basePos + center)) {
			return false;
		}
	}

	// Add border items on top of all placed positions
	for (const auto& pos : placedPositions) {
		addBorderItem(pos);
	}

	return !outItems.empty();
}

bool DecorationEngine::checkSpacingForPlacement(const std::vector<PreviewItem>& placementItems) const {
	for (const auto& item : placementItems) {
		if (!checkSpacing(item.position, item.itemId)) {
			return false;
		}
	}
	return true;
}

void DecorationEngine::commitPlacement(const std::vector<PreviewItem>& placementItems) {
	for (const auto& item : placementItems) {
		m_previewState.items.push_back(item);
		m_spatialHash.insert(item.position, m_previewState.items.size() - 1);
		m_previewState.totalItemsPlaced++;
		m_previewState.itemCountById[item.itemId]++;
	}
	if (!placementItems.empty()) {
		m_previewState.placementsByRule[placementItems.front().sourceRule]++;
	}
}

const ItemEntry* DecorationEngine::selectItemFromRule(const FloorRule* rule) {
	if (!rule || rule->items.empty()) return nullptr;

	int totalWeight = 0;
	for (const auto& item : rule->items) {
		totalWeight += item.weight;
	}

	if (totalWeight <= 0) return nullptr;

	std::uniform_int_distribution<int> dist(0, totalWeight - 1);
	int roll = dist(m_rng);

	int cumulative = 0;
	for (const auto& item : rule->items) {
		cumulative += item.weight;
		if (roll < cumulative) {
			return &item;
		}
	}

	return &rule->items.back();
}

void DecorationEngine::generatePureRandom(const std::vector<std::pair<Position, uint16_t>>& tiles) {
	std::unordered_map<const FloorRule*, int> rulePlacements;
	std::vector<const FloorRule*> matchingRules;

	std::vector<size_t> indices(tiles.size());
	std::iota(indices.begin(), indices.end(), 0);
	std::shuffle(indices.begin(), indices.end(), m_rng);

	for (size_t idx : indices) {
		if (m_preset.maxItemsTotal >= 0 &&
		    m_previewState.totalItemsPlaced >= m_preset.maxItemsTotal) {
			m_previewWasCapped = true;
			break;
		}

		const Position& pos = tiles[idx].first;
		uint16_t groundId = tiles[idx].second;

		m_preset.getMatchingRules(groundId, matchingRules);
		if (matchingRules.empty()) continue;

		for (const FloorRule* rule : matchingRules) {
			if (m_preset.maxItemsTotal >= 0 &&
			    m_previewState.totalItemsPlaced >= m_preset.maxItemsTotal) {
				m_previewWasCapped = true;
				return;
			}

			if (rule->maxPlacements >= 0 &&
			    rulePlacements[rule] >= rule->maxPlacements) {
				continue;
			}

			float adjustedDensity = applyFriendBias(rule, pos, rule->density);
			std::uniform_real_distribution<float> densityDist(0.0f, 1.0f);
			if (densityDist(m_rng) > adjustedDensity) {
				continue;
			}

			const ItemEntry* selected = selectItemFromRule(rule);
			if (!selected) continue;

			bool isClusterEntry = selected->isClusterEntry();
			if (isClusterEntry) {
				int radius = std::max(0, selected->clusterRadius);
				int minDist = std::max(0, selected->clusterMinDistance);
				int centerMinDist = radius + minDist;
				if (!checkClusterCenterSpacing(pos, centerMinDist)) {
					continue;
				}
			}

			std::vector<PreviewItem> placementItems;
			if (!buildPlacementItems(pos, *selected, rule, placementItems)) {
				continue;
			}

			if (m_preset.maxItemsTotal >= 0 &&
			    m_previewState.totalItemsPlaced + static_cast<int>(placementItems.size()) > m_preset.maxItemsTotal) {
				continue;
			}

			if (!checkSpacingForPlacement(placementItems)) {
				continue;
			}

			commitPlacement(placementItems);
			if (isClusterEntry) {
				m_clusterCenters.push_back(pos);
			}
			rulePlacements[rule]++;
		}
	}
}

void DecorationEngine::generateClustered(const std::vector<std::pair<Position, uint16_t>>& tiles) {
	if (tiles.empty()) return;

	const DistributionConfig& distConfig = m_preset.distribution;

	std::vector<Position> clusterCenters;
	int numClusters = std::min(distConfig.clusterCount, static_cast<int>(tiles.size()));

	std::uniform_int_distribution<size_t> centerDist(0, tiles.size() - 1);
	for (int i = 0; i < numClusters; ++i) {
		clusterCenters.push_back(tiles[centerDist(m_rng)].first);
	}

	std::vector<std::pair<float, size_t>> tileScores;
	for (size_t i = 0; i < tiles.size(); ++i) {
		float minDist = std::numeric_limits<float>::max();
		for (const auto& center : clusterCenters) {
			float dx = static_cast<float>(tiles[i].first.x - center.x);
			float dy = static_cast<float>(tiles[i].first.y - center.y);
			float dist = std::sqrt(dx * dx + dy * dy);
			minDist = std::min(minDist, dist);
		}
		tileScores.push_back({minDist, i});
	}

	std::sort(tileScores.begin(), tileScores.end());

	std::unordered_map<const FloorRule*, int> rulePlacements;
	std::vector<const FloorRule*> matchingRules;

	for (const auto& scorePair : tileScores) {
		if (m_preset.maxItemsTotal >= 0 &&
		    m_previewState.totalItemsPlaced >= m_preset.maxItemsTotal) {
			m_previewWasCapped = true;
			break;
		}

		size_t idx = scorePair.second;
		float distanceScore = scorePair.first;

		const Position& pos = tiles[idx].first;
		uint16_t groundId = tiles[idx].second;

		m_preset.getMatchingRules(groundId, matchingRules);
		if (matchingRules.empty()) continue;

		for (const FloorRule* rule : matchingRules) {
			if (m_preset.maxItemsTotal >= 0 &&
			    m_previewState.totalItemsPlaced >= m_preset.maxItemsTotal) {
				m_previewWasCapped = true;
				return;
			}

			if (rule->maxPlacements >= 0 &&
			    rulePlacements[rule] >= rule->maxPlacements) {
				continue;
			}

			float adjustedDensity = rule->density;
			if (distanceScore > 0) {
				float falloff = std::exp(-distanceScore * distConfig.clusterStrength * 0.1f);
				adjustedDensity *= falloff;
			}
			adjustedDensity = applyFriendBias(rule, pos, adjustedDensity);

			std::uniform_real_distribution<float> densityDist(0.0f, 1.0f);
			if (densityDist(m_rng) > adjustedDensity) {
				continue;
			}

			const ItemEntry* selected = selectItemFromRule(rule);
			if (!selected) continue;

			bool isClusterEntry = selected->isClusterEntry();
			if (isClusterEntry) {
				int radius = std::max(0, selected->clusterRadius);
				int minDist = std::max(0, selected->clusterMinDistance);
				int centerMinDist = radius + minDist;
				if (!checkClusterCenterSpacing(pos, centerMinDist)) {
					continue;
				}
			}

			std::vector<PreviewItem> placementItems;
			if (!buildPlacementItems(pos, *selected, rule, placementItems)) {
				continue;
			}

			if (m_preset.maxItemsTotal >= 0 &&
			    m_previewState.totalItemsPlaced + static_cast<int>(placementItems.size()) > m_preset.maxItemsTotal) {
				continue;
			}

			if (!checkSpacingForPlacement(placementItems)) {
				continue;
			}

			commitPlacement(placementItems);
			if (isClusterEntry) {
				m_clusterCenters.push_back(pos);
			}
			rulePlacements[rule]++;
		}
	}
}

void DecorationEngine::generateGridBased(const std::vector<std::pair<Position, uint16_t>>& tiles) {
	if (tiles.empty()) return;

	const SpacingConfig& spacing = m_preset.spacing;
	const DistributionConfig& distConfig = m_preset.distribution;

	std::unordered_map<uint64_t, std::pair<Position, uint16_t>> tileMap;
	Position minPos = tiles[0].first;
	Position maxPos = tiles[0].first;

	for (const auto& tile : tiles) {
		uint64_t hash = (static_cast<uint64_t>(tile.first.x & 0xFFFF) << 32) |
		                (static_cast<uint64_t>(tile.first.y & 0xFFFF) << 16) |
		                static_cast<uint64_t>(tile.first.z & 0xFFFF);
		tileMap[hash] = tile;

		minPos.x = std::min(minPos.x, tile.first.x);
		minPos.y = std::min(minPos.y, tile.first.y);
		maxPos.x = std::max(maxPos.x, tile.first.x);
		maxPos.y = std::max(maxPos.y, tile.first.y);
	}

	std::unordered_map<const FloorRule*, int> rulePlacements;
	std::vector<const FloorRule*> matchingRules;

	std::uniform_int_distribution<int> jitterDistX(-distConfig.gridJitter, distConfig.gridJitter);
	std::uniform_int_distribution<int> jitterDistY(-distConfig.gridJitter, distConfig.gridJitter);

	for (int gx = minPos.x; gx <= maxPos.x; gx += distConfig.gridSpacingX) {
		for (int gy = minPos.y; gy <= maxPos.y; gy += distConfig.gridSpacingY) {
			if (m_preset.maxItemsTotal >= 0 &&
			    m_previewState.totalItemsPlaced >= m_preset.maxItemsTotal) {
				m_previewWasCapped = true;
				return;
			}

			int px = gx + jitterDistX(m_rng);
			int py = gy + jitterDistY(m_rng);
			int pz = minPos.z;

			uint64_t hash = (static_cast<uint64_t>(px & 0xFFFF) << 32) |
			                (static_cast<uint64_t>(py & 0xFFFF) << 16) |
			                static_cast<uint64_t>(pz & 0xFFFF);

			auto it = tileMap.find(hash);
			if (it == tileMap.end()) continue;

			const Position& pos = it->second.first;
			uint16_t groundId = it->second.second;

			m_preset.getMatchingRules(groundId, matchingRules);
			if (matchingRules.empty()) continue;

			for (const FloorRule* rule : matchingRules) {
				if (m_preset.maxItemsTotal >= 0 &&
				    m_previewState.totalItemsPlaced >= m_preset.maxItemsTotal) {
					m_previewWasCapped = true;
					return;
				}

				if (rule->maxPlacements >= 0 &&
				    rulePlacements[rule] >= rule->maxPlacements) {
					continue;
				}

				float adjustedDensity = applyFriendBias(rule, pos, rule->density);
				std::uniform_real_distribution<float> densityDist(0.0f, 1.0f);
				if (densityDist(m_rng) > adjustedDensity) {
					continue;
				}

				const ItemEntry* selected = selectItemFromRule(rule);
				if (!selected) continue;

				bool isClusterEntry = selected->isClusterEntry();
				if (isClusterEntry) {
					int radius = std::max(0, selected->clusterRadius);
					int minDist = std::max(0, selected->clusterMinDistance);
					int centerMinDist = radius + minDist;
					if (!checkClusterCenterSpacing(pos, centerMinDist)) {
						continue;
					}
				}

				std::vector<PreviewItem> placementItems;
				if (!buildPlacementItems(pos, *selected, rule, placementItems)) {
					continue;
				}

				if (m_preset.maxItemsTotal >= 0 &&
				    m_previewState.totalItemsPlaced + static_cast<int>(placementItems.size()) > m_preset.maxItemsTotal) {
					continue;
				}

				if (!checkSpacingForPlacement(placementItems)) {
					continue;
				}

				commitPlacement(placementItems);
				if (isClusterEntry) {
					m_clusterCenters.push_back(pos);
				}
				rulePlacements[rule]++;
			}
		}
	}
}

// Helper: check if a map tile contains ALL the item IDs from a cluster tile definition.
// The map tile may have more items, but must contain at least all items in the pattern.
static bool tileMatchesClusterPattern(Tile* mapTile, const CompositeTile& clusterTile) {
	if (!mapTile) return false;
	if (clusterTile.itemIds.empty()) return true; // Empty pattern matches any tile

	// Build a multiset of item IDs on the map tile (ground + items)
	std::vector<uint16_t> mapItemIds;
	if (mapTile->ground) {
		mapItemIds.push_back(mapTile->ground->getID());
	}
	for (const auto& item : mapTile->items) {
		if (item) {
			mapItemIds.push_back(item->getID());
		}
	}

	// Check that every item in the cluster pattern exists on the map tile
	// Use a copy so we can remove matched items (handles duplicates)
	std::vector<uint16_t> remaining = mapItemIds;
	for (uint16_t patternId : clusterTile.itemIds) {
		if (patternId == 0) continue;
		auto it = std::find(remaining.begin(), remaining.end(), patternId);
		if (it == remaining.end()) {
			return false; // Pattern item not found on map tile
		}
		remaining.erase(it); // Remove matched item to handle duplicates
	}
	return true;
}

void DecorationEngine::generateClusterCentered(const std::vector<std::pair<Position, uint16_t>>& tiles,
                                                const FloorRule& rule) {
	if (rule.clusterTiles.empty() || tiles.empty() || rule.items.empty()) return;
	if (!rule.hasCenterPoint) return; // Centered mode requires a center point
	if (!m_editor && !m_virtualPreview) return;

	// The cluster is a POSITION TEMPLATE that defines an EXACT pattern of items.
	// We search the map for places where this exact pattern exists.
	// In centered mode, items from rule.items are placed ONLY at the center tile position.
	Position centerOff = rule.centerOffset;

	// Build a set of valid tile positions for fast lookup
	std::unordered_set<uint64_t> validPositions;
	auto posHash = [](int x, int y, int z) -> uint64_t {
		return (static_cast<uint64_t>(x & 0xFFFF) << 32) |
		       (static_cast<uint64_t>(y & 0xFFFF) << 16) |
		       static_cast<uint64_t>(z & 0xFFFF);
	};
	for (const auto& tile : tiles) {
		validPositions.insert(posHash(tile.first.x, tile.first.y, tile.first.z));
	}

	Map* map = m_virtualPreview ? nullptr : &m_editor->map;

	// Build list of candidate center positions where the entire cluster pattern
	// matches EXACTLY on the map (every cluster tile's items found on the corresponding map tile)
	std::vector<Position> candidatePositions;
	for (const auto& tile : tiles) {
		const Position& pos = tile.first;

		bool allMatch = true;
		for (const auto& ct : rule.clusterTiles) {
			Position absPos;
			absPos.x = pos.x + (ct.offset.x - centerOff.x);
			absPos.y = pos.y + (ct.offset.y - centerOff.y);
			absPos.z = pos.z + (ct.offset.z - centerOff.z);

			// Must be within the area
			if (validPositions.find(posHash(absPos.x, absPos.y, absPos.z)) == validPositions.end()) {
				allMatch = false;
				break;
			}

			// Must match the cluster pattern exactly on the map
			if (map) {
				Tile* mapTile = map->getTile(absPos);
				if (!ct.itemIds.empty() && !tileMatchesClusterPattern(mapTile, ct)) {
					allMatch = false;
					break;
				}
			}
		}
		if (allMatch) {
			candidatePositions.push_back(pos);
		}
	}

	if (candidatePositions.empty()) return;

	// Shuffle candidates for random selection
	std::shuffle(candidatePositions.begin(), candidatePositions.end(), m_rng);

	// Track placed instance centers for min distance enforcement
	std::vector<Position> instanceCenters;
	int instancesPlaced = 0;

	for (const auto& centerPos : candidatePositions) {
		if (instancesPlaced >= rule.instanceCount) break;

		if (m_preset.maxItemsTotal >= 0 &&
		    m_previewState.totalItemsPlaced >= m_preset.maxItemsTotal) {
			m_previewWasCapped = true;
			break;
		}

		// Check minimum distance to other cluster instance centers
		bool tooClose = false;
		if (rule.instanceMinDistance > 0) {
			for (const auto& existing : instanceCenters) {
				if (existing.z != centerPos.z) continue;
				int dx = std::abs(existing.x - centerPos.x);
				int dy = std::abs(existing.y - centerPos.y);
				int dist = std::max(dx, dy);
				if (dist < rule.instanceMinDistance) {
					tooClose = true;
					break;
				}
			}
		}
		if (tooClose) continue;

		// The center position is where the center tile maps to in the world
		// Items from rule.items are placed at this center position
		const ItemEntry* selected = selectItemFromRule(&rule);
		if (!selected) continue;

		// For composite items with their own center point, adjust the base position
		// so that the item's center tile aligns with the floor cluster's center position.
		// buildPlacementItems places items at: basePos + compositeTile.offset
		// We want the item's center to land at centerPos, so:
		//   basePos + itemCenterOffset = centerPos
		//   basePos = centerPos - itemCenterOffset
		Position basePos = centerPos;
		if (selected->isCompositeEntry() && selected->hasCenterPoint) {
			basePos.x -= selected->centerOffset.x;
			basePos.y -= selected->centerOffset.y;
			basePos.z -= selected->centerOffset.z;
		}

		// Validate placement at base position
		if (!m_virtualPreview) {
			uint16_t checkId = selected->isCompositeEntry() ? selected->getRepresentativeItemId() : selected->itemId;
			if (!validateTilePlacement(basePos, checkId)) {
				continue;
			}
		}

		// Build placement items at adjusted base position
		std::vector<PreviewItem> placementItems;
		if (!buildPlacementItems(basePos, *selected, &rule, placementItems)) {
			continue;
		}

		if (placementItems.empty()) continue;

		// Check max items cap
		if (m_preset.maxItemsTotal >= 0 &&
		    m_previewState.totalItemsPlaced + static_cast<int>(placementItems.size()) > m_preset.maxItemsTotal) {
			continue;
		}

		// Check spacing
		if (!checkSpacingForPlacement(placementItems)) {
			continue;
		}

		// Commit placement
		commitPlacement(placementItems);
		instanceCenters.push_back(centerPos);
		instancesPlaced++;
	}
}

void DecorationEngine::generateClusterRandom(const std::vector<std::pair<Position, uint16_t>>& tiles,
                                              const FloorRule& rule) {
	if (rule.clusterTiles.empty() || tiles.empty() || rule.items.empty()) return;
	if (!m_editor && !m_virtualPreview) return;

	// The cluster is a POSITION TEMPLATE that defines an EXACT pattern of items.
	// We search the map for places where this exact pattern exists.
	// In random mode, items from rule.items are scattered across all matched cluster positions.

	// Build a set of valid tile positions for fast lookup
	std::unordered_set<uint64_t> validPositions;
	auto posHash = [](int x, int y, int z) -> uint64_t {
		return (static_cast<uint64_t>(x & 0xFFFF) << 32) |
		       (static_cast<uint64_t>(y & 0xFFFF) << 16) |
		       static_cast<uint64_t>(z & 0xFFFF);
	};
	for (const auto& tile : tiles) {
		validPositions.insert(posHash(tile.first.x, tile.first.y, tile.first.z));
	}

	Map* map = m_virtualPreview ? nullptr : &m_editor->map;

	// Use the first cluster tile as an anchor
	Position anchorOff = rule.clusterTiles[0].offset;

	// Build list of candidate anchor positions where the entire cluster pattern
	// matches EXACTLY on the map
	std::vector<Position> candidatePositions;
	for (const auto& tile : tiles) {
		const Position& pos = tile.first;

		bool allMatch = true;
		for (const auto& ct : rule.clusterTiles) {
			Position absPos;
			absPos.x = pos.x + (ct.offset.x - anchorOff.x);
			absPos.y = pos.y + (ct.offset.y - anchorOff.y);
			absPos.z = pos.z + (ct.offset.z - anchorOff.z);

			// Must be within the area
			if (validPositions.find(posHash(absPos.x, absPos.y, absPos.z)) == validPositions.end()) {
				allMatch = false;
				break;
			}

			// Must match the cluster pattern exactly on the map
			if (map) {
				Tile* mapTile = map->getTile(absPos);
				if (!ct.itemIds.empty() && !tileMatchesClusterPattern(mapTile, ct)) {
					allMatch = false;
					break;
				}
			}
		}
		if (allMatch) {
			candidatePositions.push_back(pos);
		}
	}

	if (candidatePositions.empty()) return;

	// Shuffle candidates for random selection
	std::shuffle(candidatePositions.begin(), candidatePositions.end(), m_rng);

	// Track placed instance anchors for min distance enforcement
	std::vector<Position> instanceAnchors;
	int instancesPlaced = 0;
	std::uniform_real_distribution<float> densityDist(0.0f, 1.0f);

	for (const auto& anchorPos : candidatePositions) {
		if (instancesPlaced >= rule.instanceCount) break;

		if (m_preset.maxItemsTotal >= 0 &&
		    m_previewState.totalItemsPlaced >= m_preset.maxItemsTotal) {
			m_previewWasCapped = true;
			break;
		}

		// Check minimum distance to other cluster instances
		bool tooClose = false;
		if (rule.instanceMinDistance > 0) {
			for (const auto& existing : instanceAnchors) {
				if (existing.z != anchorPos.z) continue;
				int dx = std::abs(existing.x - anchorPos.x);
				int dy = std::abs(existing.y - anchorPos.y);
				int dist = std::max(dx, dy);
				if (dist < rule.instanceMinDistance) {
					tooClose = true;
					break;
				}
			}
		}
		if (tooClose) continue;

		// For each cluster tile position, try to place an item from rule.items
		std::vector<PreviewItem> placementItems;
		bool anyValid = false;

		for (const auto& ct : rule.clusterTiles) {
			Position absPos;
			absPos.x = anchorPos.x + (ct.offset.x - anchorOff.x);
			absPos.y = anchorPos.y + (ct.offset.y - anchorOff.y);
			absPos.z = anchorPos.z + (ct.offset.z - anchorOff.z);

			// Density check per tile
			if (densityDist(m_rng) > rule.density) {
				continue;
			}

			// Pick a random item from rule.items
			const ItemEntry* selected = selectItemFromRule(&rule);
			if (!selected) continue;

			// For composite items with their own center point, adjust the base position
			// so that the item's center tile aligns with this cluster tile position.
			Position basePos = absPos;
			if (selected->isCompositeEntry() && selected->hasCenterPoint) {
				basePos.x -= selected->centerOffset.x;
				basePos.y -= selected->centerOffset.y;
				basePos.z -= selected->centerOffset.z;
			}

			// Validate placement
			if (!m_virtualPreview) {
				uint16_t checkId = selected->isCompositeEntry() ? selected->getRepresentativeItemId() : selected->itemId;
				if (!validateTilePlacement(basePos, checkId)) {
					continue;
				}
			}

			// Build placement items at this cluster tile position
			std::vector<PreviewItem> tileItems;
			if (!buildPlacementItems(basePos, *selected, &rule, tileItems)) {
				continue;
			}

			for (auto& pi : tileItems) {
				placementItems.push_back(std::move(pi));
			}
			anyValid = true;
		}

		if (!anyValid || placementItems.empty()) continue;

		// Check max items cap
		if (m_preset.maxItemsTotal >= 0 &&
		    m_previewState.totalItemsPlaced + static_cast<int>(placementItems.size()) > m_preset.maxItemsTotal) {
			continue;
		}

		// Commit placement
		commitPlacement(placementItems);
		instanceAnchors.push_back(anchorPos);
		instancesPlaced++;
	}
}

bool DecorationEngine::applyPreview() {
	if (!m_previewState.isValid || m_previewState.items.empty()) {
		m_lastError = "No valid preview to apply";
		return false;
	}

	if (!m_editor) {
		m_lastError = "No editor available";
		return false;
	}

	Map& map = m_editor->map;

	auto batch = m_editor->actionQueue->createBatch(ACTION_DRAW);
	auto action = m_editor->actionQueue->createAction(batch.get());

	std::unordered_map<uint64_t, std::vector<const PreviewItem*>> itemsByPos;
	for (const auto& item : m_previewState.items) {
		uint64_t hash = (static_cast<uint64_t>(item.position.x & 0xFFFF) << 32) |
		                (static_cast<uint64_t>(item.position.y & 0xFFFF) << 16) |
		                static_cast<uint64_t>(item.position.z & 0xFFFF);
		itemsByPos[hash].push_back(&item);
	}

	for (const auto& pair : itemsByPos) {
		const Position& pos = pair.second[0]->position;

		Tile* tile = map.getTile(pos);
		if (!tile) continue;

		auto newTile = TileOperations::deepCopy(tile, map);
		bool addedAny = false;

		for (const PreviewItem* previewItem : pair.second) {
			auto newItem = Item::Create(previewItem->itemId);
			if (!newItem) continue;

			newTile->addItem(std::move(newItem));
			addedAny = true;
		}

		if (addedAny) {
			action->addChange(std::make_unique<Change>(std::move(newTile)));
		}
	}

	if (action->size() == 0) {
		m_lastError = "No changes were applied";
		return false;
	}

	batch->addAndCommitAction(std::move(action));
	m_editor->addBatch(std::move(batch));

	m_lastAppliedItems.clear();
	m_lastAppliedItems.reserve(m_previewState.items.size());
	for (const auto& item : m_previewState.items) {
		m_lastAppliedItems.push_back({item.position, item.itemId});
	}

	clearPreview();

	return true;
}

bool DecorationEngine::removeLastApplied() {
	if (!m_editor) {
		m_lastError = "No editor available";
		return false;
	}

	if (m_lastAppliedItems.empty()) {
		m_lastError = "No applied items to remove";
		return false;
	}

	Map& map = m_editor->map;

	struct RemovalBucket {
		Position pos;
		std::unordered_map<uint16_t, int> counts;
	};

	std::unordered_map<uint64_t, RemovalBucket> buckets;
	buckets.reserve(m_lastAppliedItems.size());

	auto positionHash = [](const Position& pos) {
		return (static_cast<uint64_t>(pos.x & 0xFFFF) << 32) |
		       (static_cast<uint64_t>(pos.y & 0xFFFF) << 16) |
		       static_cast<uint64_t>(pos.z & 0xFFFF);
	};

	for (const auto& item : m_lastAppliedItems) {
		uint64_t hash = positionHash(item.position);
		auto& bucket = buckets[hash];
		bucket.pos = item.position;
		bucket.counts[item.itemId]++;
	}

	auto batch = m_editor->actionQueue->createBatch(ACTION_DELETE_TILES);
	auto action = m_editor->actionQueue->createAction(batch.get());
	bool anyChange = false;

	for (auto& entry : buckets) {
		RemovalBucket& bucket = entry.second;
		Tile* tile = map.getTile(bucket.pos);
		if (!tile) continue;

		auto newTile = TileOperations::deepCopy(tile, map);
		bool changed = false;

		for (auto& pair : bucket.counts) {
			uint16_t id = pair.first;
			int count = pair.second;
			if (count <= 0) continue;

			if (newTile->ground && newTile->ground->getID() == id && count > 0) {
				newTile->ground.reset();
				count--;
				changed = true;
			}

			for (auto it = newTile->items.rbegin(); it != newTile->items.rend() && count > 0; ) {
				if (*it && (*it)->getID() == id) {
					auto baseIt = std::next(it).base();
					it = std::make_reverse_iterator(newTile->items.erase(baseIt));
					count--;
					changed = true;
				} else {
					++it;
				}
			}
		}

		if (changed) {
			action->addChange(std::make_unique<Change>(std::move(newTile)));
			anyChange = true;
		}
	}

	if (!anyChange) {
		m_lastError = "No items from last apply were found to remove";
		return false;
	}

	batch->addAndCommitAction(std::move(action));
	m_editor->addBatch(std::move(batch));

	m_lastAppliedItems.clear();
	return true;
}

//=============================================================================
// DecorationPreset Serialization
//=============================================================================

bool DecorationPreset::saveToFile(const std::string& filepath) const {
	pugi::xml_document doc;

	pugi::xml_node root = doc.append_child("decoration_preset");
	root.append_attribute("name") = name.c_str();
	root.append_attribute("version") = "1.0";

	// Spacing config
	pugi::xml_node spacingNode = root.append_child("spacing");
	spacingNode.append_attribute("min_distance") = spacing.minDistance;
	spacingNode.append_attribute("same_item_distance") = spacing.minSameItemDistance;
	spacingNode.append_attribute("check_diagonals") = spacing.checkDiagonals;

	// Distribution config
	pugi::xml_node distNode = root.append_child("distribution");
	distNode.append_attribute("mode") = static_cast<int>(distribution.mode);
	distNode.append_attribute("cluster_strength") = distribution.clusterStrength;
	distNode.append_attribute("cluster_count") = distribution.clusterCount;
	distNode.append_attribute("grid_spacing_x") = distribution.gridSpacingX;
	distNode.append_attribute("grid_spacing_y") = distribution.gridSpacingY;
	distNode.append_attribute("grid_jitter") = distribution.gridJitter;

	// General settings
	pugi::xml_node settingsNode = root.append_child("settings");
	settingsNode.append_attribute("max_items_total") = maxItemsTotal;
	settingsNode.append_attribute("skip_blocked") = skipBlockedTiles;
	settingsNode.append_attribute("default_seed") = std::to_string(defaultSeed).c_str();

	if (hasArea) {
		pugi::xml_node areaNode = root.append_child("area");
		areaNode.append_attribute("type") = static_cast<int>(area.type);
		areaNode.append_attribute("rect_min_x") = area.rectMin.x;
		areaNode.append_attribute("rect_min_y") = area.rectMin.y;
		areaNode.append_attribute("rect_min_z") = area.rectMin.z;
		areaNode.append_attribute("rect_max_x") = area.rectMax.x;
		areaNode.append_attribute("rect_max_y") = area.rectMax.y;
		areaNode.append_attribute("rect_max_z") = area.rectMax.z;
		areaNode.append_attribute("flood_origin_x") = area.floodOrigin.x;
		areaNode.append_attribute("flood_origin_y") = area.floodOrigin.y;
		areaNode.append_attribute("flood_origin_z") = area.floodOrigin.z;
		areaNode.append_attribute("flood_target_ground") = area.floodTargetGround;
		areaNode.append_attribute("flood_max_radius") = area.floodMaxRadius;
	}
	// Floor rules
	pugi::xml_node rulesNode = root.append_child("floor_rules");
	for (const auto& rule : floorRules) {
		pugi::xml_node ruleNode = rulesNode.append_child("rule");
		ruleNode.append_attribute("name") = rule.name.c_str();

		// Write rule_mode attribute
		if (rule.ruleMode == RuleMode::Cluster) {
			ruleNode.append_attribute("rule_mode") = "cluster";
		} else if (rule.ruleMode == RuleMode::FloorRange) {
			ruleNode.append_attribute("rule_mode") = "range";
		}
		// SingleFloor is the default, omit for backward compat

		ruleNode.append_attribute("floor_id") = rule.floorId;
		ruleNode.append_attribute("from_floor_id") = rule.fromFloorId;
		ruleNode.append_attribute("to_floor_id") = rule.toFloorId;
		ruleNode.append_attribute("density") = rule.density;
		ruleNode.append_attribute("max_placements") = rule.maxPlacements;
		ruleNode.append_attribute("priority") = rule.priority;
		ruleNode.append_attribute("enabled") = rule.enabled;
		ruleNode.append_attribute("border_item_id") = rule.borderItemId;

		// Cluster-specific attributes
		if (rule.isClusterRule()) {
			ruleNode.append_attribute("has_center") = rule.hasCenterPoint;
			ruleNode.append_attribute("center_x") = rule.centerOffset.x;
			ruleNode.append_attribute("center_y") = rule.centerOffset.y;
			ruleNode.append_attribute("center_z") = rule.centerOffset.z;
			ruleNode.append_attribute("instance_count") = rule.instanceCount;
			ruleNode.append_attribute("instance_min_distance") = rule.instanceMinDistance;
			ruleNode.append_attribute("require_ground") = rule.requireGround;
		}

		if (rule.isFriendRange()) {
			ruleNode.append_attribute("friend_floor_id") = 0;
			ruleNode.append_attribute("friend_from_floor_id") = rule.friendFromFloorId;
			ruleNode.append_attribute("friend_to_floor_id") = rule.friendToFloorId;
		} else {
			ruleNode.append_attribute("friend_floor_id") = rule.friendFloorId;
			ruleNode.append_attribute("friend_from_floor_id") = 0;
			ruleNode.append_attribute("friend_to_floor_id") = 0;
		}
		ruleNode.append_attribute("friend_chance") = rule.friendChance;
		ruleNode.append_attribute("friend_strength") = rule.friendStrength;

		// Cluster tiles (for cluster rules)
		if (rule.isClusterRule()) {
			for (const auto& ct : rule.clusterTiles) {
				if (ct.itemIds.empty()) continue;
				pugi::xml_node ctNode = ruleNode.append_child("cluster_tile");
				ctNode.append_attribute("x") = ct.offset.x;
				ctNode.append_attribute("y") = ct.offset.y;
				ctNode.append_attribute("z") = ct.offset.z;
				for (uint16_t id : ct.itemIds) {
					if (id == 0) continue;
					pugi::xml_node itemNode = ctNode.append_child("item");
					itemNode.append_attribute("id") = id;
				}
			}
		}

		// Regular items (for non-cluster rules)
		pugi::xml_node itemsNode = ruleNode.append_child("items");
		for (const auto& item : rule.items) {
			if (item.isCompositeEntry()) {
				pugi::xml_node compNode = itemsNode.append_child(item.isClusterEntry() ? "cluster" : "composite");
				compNode.append_attribute("weight") = item.weight;
				if (item.isClusterEntry()) {
					compNode.append_attribute("count") = item.clusterCount;
					compNode.append_attribute("radius") = item.clusterRadius;
					compNode.append_attribute("min_distance") = item.clusterMinDistance;
				}
				for (const auto& tile : item.compositeTiles) {
					if (tile.itemIds.empty()) continue;
					pugi::xml_node tileNode = compNode.append_child("tile");
					tileNode.append_attribute("x") = tile.offset.x;
					tileNode.append_attribute("y") = tile.offset.y;
					tileNode.append_attribute("z") = tile.offset.z;
					for (uint16_t id : tile.itemIds) {
						if (id == 0) continue;
						pugi::xml_node itemNode = tileNode.append_child("item");
						itemNode.append_attribute("id") = id;
					}
				}
			} else {
				pugi::xml_node itemNode = itemsNode.append_child("item");
				itemNode.append_attribute("id") = item.itemId;
				itemNode.append_attribute("weight") = item.weight;
			}
		}
	}

	return doc.save_file(filepath.c_str());
}

bool DecorationPreset::loadFromFile(const std::string& filepath) {
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filepath.c_str());

	if (!result) {
		return false;
	}

	pugi::xml_node root = doc.child("decoration_preset");
	if (!root) return false;

	name = root.attribute("name").as_string("Unnamed Preset");

	// Spacing config
	pugi::xml_node spacingNode = root.child("spacing");
	if (spacingNode) {
		spacing.minDistance = spacingNode.attribute("min_distance").as_int(1);
		spacing.minSameItemDistance = spacingNode.attribute("same_item_distance").as_int(2);
		spacing.checkDiagonals = spacingNode.attribute("check_diagonals").as_bool(true);
	}

	// Distribution config
	pugi::xml_node distNode = root.child("distribution");
	if (distNode) {
		distribution.mode = static_cast<DistributionMode>(distNode.attribute("mode").as_int(0));
		distribution.clusterStrength = distNode.attribute("cluster_strength").as_float(0.5f);
		distribution.clusterCount = distNode.attribute("cluster_count").as_int(3);
		distribution.gridSpacingX = distNode.attribute("grid_spacing_x").as_int(3);
		distribution.gridSpacingY = distNode.attribute("grid_spacing_y").as_int(3);
		distribution.gridJitter = distNode.attribute("grid_jitter").as_int(1);
	}

	// General settings
	pugi::xml_node settingsNode = root.child("settings");
	if (settingsNode) {
		maxItemsTotal = settingsNode.attribute("max_items_total").as_int(-1);
		skipBlockedTiles = settingsNode.attribute("skip_blocked").as_bool(true);
		const char* seedStr = settingsNode.attribute("default_seed").as_string("0");
		defaultSeed = std::strtoull(seedStr, nullptr, 10);
	}

	// Area settings
	hasArea = false;
	pugi::xml_node areaNode = root.child("area");
	if (areaNode) {
		hasArea = true;
		area.type = static_cast<AreaDefinition::Type>(areaNode.attribute("type").as_int(0));
		area.rectMin = Position(areaNode.attribute("rect_min_x").as_int(0),
		                        areaNode.attribute("rect_min_y").as_int(0),
		                        areaNode.attribute("rect_min_z").as_int(0));
		area.rectMax = Position(areaNode.attribute("rect_max_x").as_int(0),
		                        areaNode.attribute("rect_max_y").as_int(0),
		                        areaNode.attribute("rect_max_z").as_int(0));
		area.floodOrigin = Position(areaNode.attribute("flood_origin_x").as_int(0),
		                            areaNode.attribute("flood_origin_y").as_int(0),
		                            areaNode.attribute("flood_origin_z").as_int(0));
		area.floodTargetGround = areaNode.attribute("flood_target_ground").as_uint(0);
		area.floodMaxRadius = areaNode.attribute("flood_max_radius").as_int(100);
	}

	// Floor rules
	floorRules.clear();
	pugi::xml_node rulesNode = root.child("floor_rules");
	for (pugi::xml_node ruleNode = rulesNode.child("rule"); ruleNode; ruleNode = ruleNode.next_sibling("rule")) {
		FloorRule rule;
		rule.name = ruleNode.attribute("name").as_string("Rule");
		rule.floorId = ruleNode.attribute("floor_id").as_uint(0);
		rule.fromFloorId = ruleNode.attribute("from_floor_id").as_uint(0);
		rule.toFloorId = ruleNode.attribute("to_floor_id").as_uint(0);
		rule.density = ruleNode.attribute("density").as_float(0.3f);
		rule.maxPlacements = ruleNode.attribute("max_placements").as_int(-1);
		rule.priority = ruleNode.attribute("priority").as_int(0);
		rule.enabled = ruleNode.attribute("enabled").as_bool(true);
		rule.borderItemId = ruleNode.attribute("border_item_id").as_uint(0);

		// Read rule_mode with backward compatibility
		std::string ruleModeStr = ruleNode.attribute("rule_mode").as_string("");
		if (ruleModeStr == "cluster") {
			rule.ruleMode = RuleMode::Cluster;
		} else if (ruleModeStr == "range") {
			rule.ruleMode = RuleMode::FloorRange;
		} else {
			// Backward compat: determine mode from existing fields
			if (rule.fromFloorId > 0 && rule.toFloorId > 0) {
				rule.ruleMode = RuleMode::FloorRange;
			} else {
				rule.ruleMode = RuleMode::SingleFloor;
			}
		}

		// Read cluster-specific attributes
		if (rule.isClusterRule()) {
			rule.hasCenterPoint = ruleNode.attribute("has_center").as_bool(false);
			rule.centerOffset = Position(
				ruleNode.attribute("center_x").as_int(0),
				ruleNode.attribute("center_y").as_int(0),
				ruleNode.attribute("center_z").as_int(0));
			rule.instanceCount = ruleNode.attribute("instance_count").as_int(1);
			rule.instanceMinDistance = ruleNode.attribute("instance_min_distance").as_int(5);
			rule.requireGround = ruleNode.attribute("require_ground").as_bool(true);

			// Parse cluster_tile nodes
			for (pugi::xml_node ctNode = ruleNode.child("cluster_tile"); ctNode;
			     ctNode = ctNode.next_sibling("cluster_tile")) {
				CompositeTile ct;
				ct.offset = Position(
					ctNode.attribute("x").as_int(0),
					ctNode.attribute("y").as_int(0),
					ctNode.attribute("z").as_int(0));
				for (pugi::xml_node idNode = ctNode.child("item"); idNode;
				     idNode = idNode.next_sibling("item")) {
					uint16_t id = idNode.attribute("id").as_uint(0);
					if (id > 0) {
						ct.itemIds.push_back(id);
					}
				}
				if (!ct.itemIds.empty()) {
					rule.clusterTiles.push_back(ct);
				}
			}
		}

		uint16_t friendFrom = ruleNode.attribute("friend_from_floor_id").as_uint(0);
		uint16_t friendTo = ruleNode.attribute("friend_to_floor_id").as_uint(0);
		if (friendFrom > 0 && friendTo > 0) {
			rule.friendFloorId = 0;
			rule.friendFromFloorId = friendFrom;
			rule.friendToFloorId = friendTo;
		} else {
			rule.friendFloorId = ruleNode.attribute("friend_floor_id").as_uint(0);
			rule.friendFromFloorId = 0;
			rule.friendToFloorId = 0;
		}
		rule.friendChance = ruleNode.attribute("friend_chance").as_int(0);
		rule.friendStrength = ruleNode.attribute("friend_strength").as_int(0);

		pugi::xml_node itemsNode = ruleNode.child("items");
		for (pugi::xml_node itemNode = itemsNode.first_child(); itemNode; itemNode = itemNode.next_sibling()) {
			std::string nodeName = itemNode.name();
			if (nodeName == "item") {
				ItemEntry entry;
				entry.itemId = itemNode.attribute("id").as_uint(0);
				entry.weight = itemNode.attribute("weight").as_int(100);
				if (entry.itemId > 0) {
					rule.items.push_back(entry);
				}
			} else if (nodeName == "composite" || nodeName == "cluster") {
				int weight = itemNode.attribute("weight").as_int(0);
				if (weight <= 0) {
					weight = itemNode.attribute("chance").as_int(100);
				}

				std::vector<CompositeTile> tiles;
				for (pugi::xml_node tileNode = itemNode.child("tile"); tileNode; tileNode = tileNode.next_sibling("tile")) {
					CompositeTile tile;
					tile.offset = Position(tileNode.attribute("x").as_int(0),
					                       tileNode.attribute("y").as_int(0),
					                       tileNode.attribute("z").as_int(0));
					for (pugi::xml_node idNode = tileNode.child("item"); idNode; idNode = idNode.next_sibling("item")) {
						uint16_t id = idNode.attribute("id").as_uint(0);
						if (id > 0) {
							tile.itemIds.push_back(id);
						}
					}
					if (!tile.itemIds.empty()) {
						tiles.push_back(tile);
					}
				}

				if (!tiles.empty()) {
					if (nodeName == "cluster") {
						int count = itemNode.attribute("count").as_int(3);
						int radius = itemNode.attribute("radius").as_int(3);
						int minDistance = itemNode.attribute("min_distance").as_int(2);
						rule.items.push_back(ItemEntry::MakeCluster(tiles, weight, count, radius, minDistance));
					} else {
						rule.items.push_back(ItemEntry::MakeComposite(tiles, weight));
					}
				}
			}
		}

		floorRules.push_back(rule);
	}

	return true;
}

std::string DecorationPreset::toXmlString() const {
	pugi::xml_document doc;

	pugi::xml_node root = doc.append_child("decoration_preset");
	root.append_attribute("name") = name.c_str();

	// Same serialization as saveToFile, but to string
	pugi::xml_node spacingNode = root.append_child("spacing");
	spacingNode.append_attribute("min_distance") = spacing.minDistance;
	spacingNode.append_attribute("same_item_distance") = spacing.minSameItemDistance;
	spacingNode.append_attribute("check_diagonals") = spacing.checkDiagonals;

	pugi::xml_node distNode = root.append_child("distribution");
	distNode.append_attribute("mode") = static_cast<int>(distribution.mode);
	distNode.append_attribute("cluster_strength") = distribution.clusterStrength;
	distNode.append_attribute("cluster_count") = distribution.clusterCount;
	distNode.append_attribute("grid_spacing_x") = distribution.gridSpacingX;
	distNode.append_attribute("grid_spacing_y") = distribution.gridSpacingY;
	distNode.append_attribute("grid_jitter") = distribution.gridJitter;

	pugi::xml_node settingsNode = root.append_child("settings");
	settingsNode.append_attribute("max_items_total") = maxItemsTotal;
	settingsNode.append_attribute("skip_blocked") = skipBlockedTiles;
	settingsNode.append_attribute("default_seed") = std::to_string(defaultSeed).c_str();

	if (hasArea) {
		pugi::xml_node areaNode = root.append_child("area");
		areaNode.append_attribute("type") = static_cast<int>(area.type);
		areaNode.append_attribute("rect_min_x") = area.rectMin.x;
		areaNode.append_attribute("rect_min_y") = area.rectMin.y;
		areaNode.append_attribute("rect_min_z") = area.rectMin.z;
		areaNode.append_attribute("rect_max_x") = area.rectMax.x;
		areaNode.append_attribute("rect_max_y") = area.rectMax.y;
		areaNode.append_attribute("rect_max_z") = area.rectMax.z;
		areaNode.append_attribute("flood_origin_x") = area.floodOrigin.x;
		areaNode.append_attribute("flood_origin_y") = area.floodOrigin.y;
		areaNode.append_attribute("flood_origin_z") = area.floodOrigin.z;
		areaNode.append_attribute("flood_target_ground") = area.floodTargetGround;
		areaNode.append_attribute("flood_max_radius") = area.floodMaxRadius;
	}

	pugi::xml_node rulesNode = root.append_child("floor_rules");
	for (const auto& rule : floorRules) {
		pugi::xml_node ruleNode = rulesNode.append_child("rule");
		ruleNode.append_attribute("name") = rule.name.c_str();

		// Write rule_mode attribute
		if (rule.ruleMode == RuleMode::Cluster) {
			ruleNode.append_attribute("rule_mode") = "cluster";
		} else if (rule.ruleMode == RuleMode::FloorRange) {
			ruleNode.append_attribute("rule_mode") = "range";
		}

		ruleNode.append_attribute("floor_id") = rule.floorId;
		ruleNode.append_attribute("from_floor_id") = rule.fromFloorId;
		ruleNode.append_attribute("to_floor_id") = rule.toFloorId;
		ruleNode.append_attribute("density") = rule.density;
		ruleNode.append_attribute("max_placements") = rule.maxPlacements;
		ruleNode.append_attribute("priority") = rule.priority;
		ruleNode.append_attribute("enabled") = rule.enabled;
		ruleNode.append_attribute("border_item_id") = rule.borderItemId;

		// Cluster-specific attributes
		if (rule.isClusterRule()) {
			ruleNode.append_attribute("has_center") = rule.hasCenterPoint;
			ruleNode.append_attribute("center_x") = rule.centerOffset.x;
			ruleNode.append_attribute("center_y") = rule.centerOffset.y;
			ruleNode.append_attribute("center_z") = rule.centerOffset.z;
			ruleNode.append_attribute("instance_count") = rule.instanceCount;
			ruleNode.append_attribute("instance_min_distance") = rule.instanceMinDistance;
			ruleNode.append_attribute("require_ground") = rule.requireGround;
		}

		if (rule.isFriendRange()) {
			ruleNode.append_attribute("friend_floor_id") = 0;
			ruleNode.append_attribute("friend_from_floor_id") = rule.friendFromFloorId;
			ruleNode.append_attribute("friend_to_floor_id") = rule.friendToFloorId;
		} else {
			ruleNode.append_attribute("friend_floor_id") = rule.friendFloorId;
			ruleNode.append_attribute("friend_from_floor_id") = 0;
			ruleNode.append_attribute("friend_to_floor_id") = 0;
		}
		ruleNode.append_attribute("friend_chance") = rule.friendChance;
		ruleNode.append_attribute("friend_strength") = rule.friendStrength;

		// Cluster tiles (for cluster rules)
		if (rule.isClusterRule()) {
			for (const auto& ct : rule.clusterTiles) {
				if (ct.itemIds.empty()) continue;
				pugi::xml_node ctNode = ruleNode.append_child("cluster_tile");
				ctNode.append_attribute("x") = ct.offset.x;
				ctNode.append_attribute("y") = ct.offset.y;
				ctNode.append_attribute("z") = ct.offset.z;
				for (uint16_t id : ct.itemIds) {
					if (id == 0) continue;
					pugi::xml_node itemNode = ctNode.append_child("item");
					itemNode.append_attribute("id") = id;
				}
			}
		}

		pugi::xml_node itemsNode = ruleNode.append_child("items");
		for (const auto& item : rule.items) {
			if (item.isCompositeEntry()) {
				pugi::xml_node compNode = itemsNode.append_child(item.isClusterEntry() ? "cluster" : "composite");
				compNode.append_attribute("weight") = item.weight;
				if (item.isClusterEntry()) {
					compNode.append_attribute("count") = item.clusterCount;
					compNode.append_attribute("radius") = item.clusterRadius;
					compNode.append_attribute("min_distance") = item.clusterMinDistance;
				}
				for (const auto& tile : item.compositeTiles) {
					if (tile.itemIds.empty()) continue;
					pugi::xml_node tileNode = compNode.append_child("tile");
					tileNode.append_attribute("x") = tile.offset.x;
					tileNode.append_attribute("y") = tile.offset.y;
					tileNode.append_attribute("z") = tile.offset.z;
					for (uint16_t id : tile.itemIds) {
						if (id == 0) continue;
						pugi::xml_node itemNode = tileNode.append_child("item");
						itemNode.append_attribute("id") = id;
					}
				}
			} else {
				pugi::xml_node itemNode = itemsNode.append_child("item");
				itemNode.append_attribute("id") = item.itemId;
				itemNode.append_attribute("weight") = item.weight;
			}
		}
	}

	std::ostringstream stream;
	doc.save(stream);
	return stream.str();
}

bool DecorationPreset::fromXmlString(const std::string& xml) {
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_buffer(xml.c_str(), xml.size());

	if (!result) return false;

	// Re-use the same parsing logic
	pugi::xml_node root = doc.child("decoration_preset");
	if (!root) return false;

	name = root.attribute("name").as_string("Unnamed Preset");

	pugi::xml_node spacingNode = root.child("spacing");
	if (spacingNode) {
		spacing.minDistance = spacingNode.attribute("min_distance").as_int(1);
		spacing.minSameItemDistance = spacingNode.attribute("same_item_distance").as_int(2);
		spacing.checkDiagonals = spacingNode.attribute("check_diagonals").as_bool(true);
	}

	pugi::xml_node distNode = root.child("distribution");
	if (distNode) {
		distribution.mode = static_cast<DistributionMode>(distNode.attribute("mode").as_int(0));
		distribution.clusterStrength = distNode.attribute("cluster_strength").as_float(0.5f);
		distribution.clusterCount = distNode.attribute("cluster_count").as_int(3);
		distribution.gridSpacingX = distNode.attribute("grid_spacing_x").as_int(3);
		distribution.gridSpacingY = distNode.attribute("grid_spacing_y").as_int(3);
		distribution.gridJitter = distNode.attribute("grid_jitter").as_int(1);
	}

	pugi::xml_node settingsNode = root.child("settings");
	if (settingsNode) {
		maxItemsTotal = settingsNode.attribute("max_items_total").as_int(-1);
		skipBlockedTiles = settingsNode.attribute("skip_blocked").as_bool(true);
		const char* seedStr = settingsNode.attribute("default_seed").as_string("0");
		defaultSeed = std::strtoull(seedStr, nullptr, 10);
	}

	hasArea = false;
	pugi::xml_node areaNode = root.child("area");
	if (areaNode) {
		hasArea = true;
		area.type = static_cast<AreaDefinition::Type>(areaNode.attribute("type").as_int(0));
		area.rectMin = Position(areaNode.attribute("rect_min_x").as_int(0),
		                        areaNode.attribute("rect_min_y").as_int(0),
		                        areaNode.attribute("rect_min_z").as_int(0));
		area.rectMax = Position(areaNode.attribute("rect_max_x").as_int(0),
		                        areaNode.attribute("rect_max_y").as_int(0),
		                        areaNode.attribute("rect_max_z").as_int(0));
		area.floodOrigin = Position(areaNode.attribute("flood_origin_x").as_int(0),
		                            areaNode.attribute("flood_origin_y").as_int(0),
		                            areaNode.attribute("flood_origin_z").as_int(0));
		area.floodTargetGround = areaNode.attribute("flood_target_ground").as_uint(0);
		area.floodMaxRadius = areaNode.attribute("flood_max_radius").as_int(100);
	}

	floorRules.clear();
	pugi::xml_node rulesNode = root.child("floor_rules");
	for (pugi::xml_node ruleNode = rulesNode.child("rule"); ruleNode; ruleNode = ruleNode.next_sibling("rule")) {
		FloorRule rule;
		rule.name = ruleNode.attribute("name").as_string("Rule");
		rule.floorId = ruleNode.attribute("floor_id").as_uint(0);
		rule.fromFloorId = ruleNode.attribute("from_floor_id").as_uint(0);
		rule.toFloorId = ruleNode.attribute("to_floor_id").as_uint(0);
		rule.density = ruleNode.attribute("density").as_float(0.3f);
		rule.maxPlacements = ruleNode.attribute("max_placements").as_int(-1);
		rule.priority = ruleNode.attribute("priority").as_int(0);
		rule.enabled = ruleNode.attribute("enabled").as_bool(true);
		rule.borderItemId = ruleNode.attribute("border_item_id").as_uint(0);

		// Read rule_mode with backward compatibility
		std::string ruleModeStr = ruleNode.attribute("rule_mode").as_string("");
		if (ruleModeStr == "cluster") {
			rule.ruleMode = RuleMode::Cluster;
		} else if (ruleModeStr == "range") {
			rule.ruleMode = RuleMode::FloorRange;
		} else {
			// Backward compat: determine mode from existing fields
			if (rule.fromFloorId > 0 && rule.toFloorId > 0) {
				rule.ruleMode = RuleMode::FloorRange;
			} else {
				rule.ruleMode = RuleMode::SingleFloor;
			}
		}

		// Read cluster-specific attributes
		if (rule.isClusterRule()) {
			rule.hasCenterPoint = ruleNode.attribute("has_center").as_bool(false);
			rule.centerOffset = Position(
				ruleNode.attribute("center_x").as_int(0),
				ruleNode.attribute("center_y").as_int(0),
				ruleNode.attribute("center_z").as_int(0));
			rule.instanceCount = ruleNode.attribute("instance_count").as_int(1);
			rule.instanceMinDistance = ruleNode.attribute("instance_min_distance").as_int(5);
			rule.requireGround = ruleNode.attribute("require_ground").as_bool(true);

			// Parse cluster_tile nodes
			for (pugi::xml_node ctNode = ruleNode.child("cluster_tile"); ctNode;
			     ctNode = ctNode.next_sibling("cluster_tile")) {
				CompositeTile ct;
				ct.offset = Position(
					ctNode.attribute("x").as_int(0),
					ctNode.attribute("y").as_int(0),
					ctNode.attribute("z").as_int(0));
				for (pugi::xml_node idNode = ctNode.child("item"); idNode;
				     idNode = idNode.next_sibling("item")) {
					uint16_t id = idNode.attribute("id").as_uint(0);
					if (id > 0) {
						ct.itemIds.push_back(id);
					}
				}
				if (!ct.itemIds.empty()) {
					rule.clusterTiles.push_back(ct);
				}
			}
		}

		uint16_t friendFrom = ruleNode.attribute("friend_from_floor_id").as_uint(0);
		uint16_t friendTo = ruleNode.attribute("friend_to_floor_id").as_uint(0);
		if (friendFrom > 0 && friendTo > 0) {
			rule.friendFloorId = 0;
			rule.friendFromFloorId = friendFrom;
			rule.friendToFloorId = friendTo;
		} else {
			rule.friendFloorId = ruleNode.attribute("friend_floor_id").as_uint(0);
			rule.friendFromFloorId = 0;
			rule.friendToFloorId = 0;
		}
		rule.friendChance = ruleNode.attribute("friend_chance").as_int(0);
		rule.friendStrength = ruleNode.attribute("friend_strength").as_int(0);

		pugi::xml_node itemsNode = ruleNode.child("items");
		for (pugi::xml_node itemNode = itemsNode.first_child(); itemNode; itemNode = itemNode.next_sibling()) {
			std::string nodeName = itemNode.name();
			if (nodeName == "item") {
				ItemEntry entry;
				entry.itemId = itemNode.attribute("id").as_uint(0);
				entry.weight = itemNode.attribute("weight").as_int(100);
				if (entry.itemId > 0) {
					rule.items.push_back(entry);
				}
			} else if (nodeName == "composite" || nodeName == "cluster") {
				int weight = itemNode.attribute("weight").as_int(0);
				if (weight <= 0) {
					weight = itemNode.attribute("chance").as_int(100);
				}

				std::vector<CompositeTile> tiles;
				for (pugi::xml_node tileNode = itemNode.child("tile"); tileNode; tileNode = tileNode.next_sibling("tile")) {
					CompositeTile tile;
					tile.offset = Position(tileNode.attribute("x").as_int(0),
					                       tileNode.attribute("y").as_int(0),
					                       tileNode.attribute("z").as_int(0));
					for (pugi::xml_node idNode = tileNode.child("item"); idNode; idNode = idNode.next_sibling("item")) {
						uint16_t id = idNode.attribute("id").as_uint(0);
						if (id > 0) {
							tile.itemIds.push_back(id);
						}
					}
					if (!tile.itemIds.empty()) {
						tiles.push_back(tile);
					}
				}

				if (!tiles.empty()) {
					if (nodeName == "cluster") {
						int count = itemNode.attribute("count").as_int(3);
						int radius = itemNode.attribute("radius").as_int(3);
						int minDistance = itemNode.attribute("min_distance").as_int(2);
						rule.items.push_back(ItemEntry::MakeCluster(tiles, weight, count, radius, minDistance));
					} else {
						rule.items.push_back(ItemEntry::MakeComposite(tiles, weight));
					}
				}
			}
		}

		floorRules.push_back(rule);
	}

	return true;
}

//=============================================================================
// PresetManager Implementation
//=============================================================================

PresetManager& PresetManager::getInstance() {
	static PresetManager instance;
	return instance;
}

std::string PresetManager::getPresetsDirectory() const {
	wxString dataDir = FileSystem::GetDataDirectory();
	wxString presetsBaseDir = dataDir + "/presets";
	wxString presetsDir = presetsBaseDir + "/decoration";

	// Create parent directory first if it doesn't exist
	if (!wxDirExists(presetsBaseDir)) {
		wxMkdir(presetsBaseDir);
	}

	// Create decoration subdirectory if it doesn't exist
	if (!wxDirExists(presetsDir)) {
		wxMkdir(presetsDir);
	}

	return presetsDir.ToStdString();
}

bool PresetManager::loadPresets() {
	m_presets.clear();

	std::string dir = getPresetsDirectory();
	wxDir wxdir(dir);

	if (!wxdir.IsOpened()) {
		return false;
	}

	wxString filename;
	bool cont = wxdir.GetFirst(&filename, "*.xml", wxDIR_FILES);

	while (cont) {
		std::string filepath = dir + "/" + filename.ToStdString();
		DecorationPreset preset;
		if (preset.loadFromFile(filepath)) {
			m_presets[preset.name] = preset;
		}
		cont = wxdir.GetNext(&filename);
	}

	m_loaded = true;
	return true;
}

bool PresetManager::savePresets() {
	std::string dir = getPresetsDirectory();

	for (const auto& pair : m_presets) {
		std::string filename = pair.first;
		// Sanitize filename
		for (char& c : filename) {
			if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
				c = '_';
			}
		}
		std::string filepath = dir + "/" + filename + ".xml";
		pair.second.saveToFile(filepath);
	}

	return true;
}

std::vector<std::string> PresetManager::getPresetNames() const {
	std::vector<std::string> names;
	for (const auto& pair : m_presets) {
		names.push_back(pair.first);
	}
	std::sort(names.begin(), names.end());
	return names;
}

const DecorationPreset* PresetManager::getPreset(const std::string& name) const {
	auto it = m_presets.find(name);
	if (it != m_presets.end()) {
		return &it->second;
	}
	return nullptr;
}

bool PresetManager::addPreset(const DecorationPreset& preset) {
	if (preset.name.empty()) return false;

	m_presets[preset.name] = preset;

	// Save immediately
	std::string dir = getPresetsDirectory();
	std::string filename = preset.name;
	for (char& c : filename) {
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
			c = '_';
		}
	}
	std::string filepath = dir + "/" + filename + ".xml";
	return preset.saveToFile(filepath);
}

bool PresetManager::removePreset(const std::string& name) {
	auto it = m_presets.find(name);
	if (it == m_presets.end()) return false;

	// Delete file
	std::string dir = getPresetsDirectory();
	std::string filename = name;
	for (char& c : filename) {
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
			c = '_';
		}
	}
	std::string filepath = dir + "/" + filename + ".xml";
	wxRemoveFile(filepath);

	m_presets.erase(it);
	return true;
}

bool PresetManager::renamePreset(const std::string& oldName, const std::string& newName) {
	if (oldName == newName) return true;
	if (newName.empty()) return false;

	auto it = m_presets.find(oldName);
	if (it == m_presets.end()) return false;

	// Check if new name already exists
	if (m_presets.find(newName) != m_presets.end()) return false;

	DecorationPreset preset = it->second;
	preset.name = newName;

	// Remove old
	removePreset(oldName);

	// Add new
	return addPreset(preset);
}

} // namespace AreaDecoration
