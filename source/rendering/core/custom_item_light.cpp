//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "rendering/core/custom_item_light.h"
#include "rendering/core/lua_table_parser.h"

#include <spdlog/spdlog.h>

// ============================================================================
// CustomItemLight
// ============================================================================

uint32_t CustomItemLight::getTotalPatternDuration() const {
	uint32_t total = 0;
	for (const auto& step : pattern) {
		total += step.duration_ms;
	}
	return total;
}

// ============================================================================
// CustomItemLightManager
// ============================================================================

CustomItemLightManager& CustomItemLightManager::instance() {
	static CustomItemLightManager inst;
	return inst;
}

void CustomItemLightManager::load(const std::string& filepath) {
	clear();

	auto data = LuaTableParser::parseFile(filepath);
	if (data.empty()) {
		spdlog::warn("[CustomItemLightManager] No data parsed from: {}", filepath);
		return;
	}

	// Look for CUSTOM_ITEM_LIGHTS table
	auto it = data.find("CUSTOM_ITEM_LIGHTS");
	if (it == data.end()) {
		spdlog::warn("[CustomItemLightManager] CUSTOM_ITEM_LIGHTS table not found in: {}", filepath);
		return;
	}

	const auto& lightsTable = it->second;
	if (lightsTable.type != LuaTableParser::LuaValue::TABLE) {
		spdlog::warn("[CustomItemLightManager] CUSTOM_ITEM_LIGHTS is not a table");
		return;
	}

	// Each entry is [clientId] = { color = N, intensity = N, pattern = { ... } }
	for (const auto& [key, val] : lightsTable.named_fields) {
		if (val.type != LuaTableParser::LuaValue::TABLE) {
			continue;
		}

		uint16_t clientId = 0;
		try {
			clientId = static_cast<uint16_t>(std::stoi(key));
		} catch (...) {
			continue;
		}

		if (clientId == 0) {
			continue;
		}

		CustomItemLight light;
		light.color = static_cast<uint8_t>(LuaTableParser::getInt(val, "color", 0));
		light.intensity = static_cast<uint8_t>(LuaTableParser::getInt(val, "intensity", 0));

		// Parse pattern
		const auto* patternField = LuaTableParser::findField(val, "pattern");
		if (patternField && patternField->type == LuaTableParser::LuaValue::TABLE) {
			for (const auto& step : patternField->array_fields) {
				if (step.type == LuaTableParser::LuaValue::TABLE && step.array_fields.size() >= 2) {
					CustomItemLightStep s;
					s.intensity = static_cast<uint8_t>(step.array_fields[0].number);
					s.duration_ms = static_cast<uint16_t>(step.array_fields[1].number);
					light.pattern.push_back(s);
				}
			}
			if (!light.pattern.empty()) {
				hasAnimated_ = true;
			}
		}

		lights_[clientId] = std::move(light);
	}

	loaded_ = true;
	spdlog::info("[CustomItemLightManager] Loaded {} custom item lights from {}", lights_.size(), filepath);
}

void CustomItemLightManager::clear() {
	lights_.clear();
	loaded_ = false;
	hasAnimated_ = false;
}

const CustomItemLight* CustomItemLightManager::find(uint16_t clientId) const {
	auto it = lights_.find(clientId);
	if (it != lights_.end()) {
		return &it->second;
	}
	return nullptr;
}

bool CustomItemLightManager::hasCustomLight(uint16_t clientId) const {
	return lights_.count(clientId) > 0;
}

uint8_t CustomItemLightManager::getCurrentIntensity(const CustomItemLight& light, uint32_t elapsedMs, uint32_t randomOffset) const {
	if (light.pattern.empty()) {
		return light.intensity;
	}

	uint32_t totalDuration = light.getTotalPatternDuration();
	if (totalDuration == 0) {
		return light.intensity;
	}

	uint32_t time = (elapsedMs + randomOffset) % totalDuration;
	uint32_t accumulated = 0;

	for (const auto& step : light.pattern) {
		accumulated += step.duration_ms;
		if (time < accumulated) {
			return step.intensity;
		}
	}

	// Fallback (shouldn't happen)
	return light.intensity;
}
