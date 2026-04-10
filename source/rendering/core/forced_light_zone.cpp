//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "rendering/core/forced_light_zone.h"
#include "rendering/core/lua_table_parser.h"

#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

// ============================================================================
// ForcedLightZone
// ============================================================================

bool ForcedLightZone::contains(const Position& pos) const {
	if (isCircular_) {
		if (pos.z != floor) {
			return false;
		}
		int dx = pos.x - center.x;
		int dy = pos.y - center.y;
		return (dx * dx + dy * dy) <= (radius * radius);
	} else {
		// Rectangular
		return pos.z == fromPos.z
			&& pos.x >= fromPos.x && pos.x <= toPos.x
			&& pos.y >= fromPos.y && pos.y <= toPos.y;
	}
}

Position ForcedLightZone::getBoundsMin() const {
	if (isCircular_) {
		return Position(center.x - radius, center.y - radius, floor);
	}
	return fromPos;
}

Position ForcedLightZone::getBoundsMax() const {
	if (isCircular_) {
		return Position(center.x + radius, center.y + radius, floor);
	}
	return toPos;
}

// ============================================================================
// ForcedLightZoneManager
// ============================================================================

ForcedLightZoneManager& ForcedLightZoneManager::instance() {
	static ForcedLightZoneManager inst;
	return inst;
}

static Position extractPosition(const LuaTableParser::LuaValue& val) {
	if (val.type == LuaTableParser::LuaValue::FUNCTION_CALL && val.func_name == "Position") {
		if (val.func_args.size() >= 3) {
			return Position(
				static_cast<int>(val.func_args[0]),
				static_cast<int>(val.func_args[1]),
				static_cast<int>(val.func_args[2]));
		}
	}
	return Position(0, 0, 0);
}

void ForcedLightZoneManager::load(const std::string& filepath) {
	clear();

	auto data = LuaTableParser::parseFile(filepath);
	if (data.empty()) {
		spdlog::warn("[ForcedLightZoneManager] No data parsed from: {}", filepath);
		return;
	}

	// Look for DARKNESS_ZONES table
	auto it = data.find("DARKNESS_ZONES");
	if (it == data.end()) {
		spdlog::warn("[ForcedLightZoneManager] DARKNESS_ZONES table not found in: {}", filepath);
		return;
	}

	const auto& zonesTable = it->second;
	if (zonesTable.type != LuaTableParser::LuaValue::TABLE) {
		spdlog::warn("[ForcedLightZoneManager] DARKNESS_ZONES is not a table");
		return;
	}

	// Each zone is an array entry in the table
	for (const auto& entry : zonesTable.array_fields) {
		if (entry.type != LuaTableParser::LuaValue::TABLE) {
			continue;
		}

		ForcedLightZone zone;
		zone.name = LuaTableParser::getString(entry, "name");
		zone.ambient = static_cast<uint8_t>(LuaTableParser::getInt(entry, "ambient", 0));
		zone.ambientColor = static_cast<uint8_t>(LuaTableParser::getInt(entry, "ambientColor", 0));

		// Check for circular zone (center + radius)
		const auto* centerField = LuaTableParser::findField(entry, "center");
		const auto* radiusField = LuaTableParser::findField(entry, "radius");

		if (centerField && radiusField) {
			// Circular zone
			zone.setCircular(true);
			zone.center = extractPosition(*centerField);
			zone.radius = LuaTableParser::getInt(entry, "radius", 0);
			zone.floor = LuaTableParser::getInt(entry, "floor", zone.center.z);
		} else {
			// Rectangular zone (fromPos + toPos)
			const auto* fromField = LuaTableParser::findField(entry, "fromPos");
			const auto* toField = LuaTableParser::findField(entry, "toPos");

			if (fromField && toField) {
				zone.fromPos = extractPosition(*fromField);
				zone.toPos = extractPosition(*toField);
			} else {
				spdlog::warn("[ForcedLightZoneManager] Zone '{}' has no valid position data, skipping", zone.name);
				continue;
			}
		}

		zones_.push_back(std::move(zone));
	}

	loaded_ = true;
	spdlog::info("[ForcedLightZoneManager] Loaded {} zones from {}", zones_.size(), filepath);
}

void ForcedLightZoneManager::clear() {
	zones_.clear();
	loaded_ = false;
}

const std::vector<ForcedLightZone>& ForcedLightZoneManager::getZones() const {
	return zones_;
}

const ForcedLightZone* ForcedLightZoneManager::getZoneAt(const Position& pos) const {
	for (const auto& zone : zones_) {
		if (zone.contains(pos)) {
			return &zone;
		}
	}
	return nullptr;
}

std::vector<const ForcedLightZone*> ForcedLightZoneManager::getZonesInArea(
	int minX, int minY, int maxX, int maxY, int floor) const {
	std::vector<const ForcedLightZone*> result;
	for (const auto& zone : zones_) {
		Position bmin = zone.getBoundsMin();
		Position bmax = zone.getBoundsMax();

		// Check floor
		if (zone.isCircular()) {
			if (zone.floor != floor) {
				continue;
			}
		} else {
			if (zone.fromPos.z != floor) {
				continue;
			}
		}

		// Check AABB overlap
		if (bmax.x < minX || bmin.x > maxX || bmax.y < minY || bmin.y > maxY) {
			continue;
		}

		result.push_back(&zone);
	}
	return result;
}
