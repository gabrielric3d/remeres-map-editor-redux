//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_CORE_CUSTOM_ITEM_LIGHT_H_
#define RME_RENDERING_CORE_CUSTOM_ITEM_LIGHT_H_

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct CustomItemLightStep {
	uint8_t intensity;    // 0-15
	uint16_t duration_ms; // milliseconds
};

struct CustomItemLight {
	uint8_t color;        // 8-bit color index
	uint8_t intensity;    // base intensity 1-15
	std::vector<CustomItemLightStep> pattern; // animation pattern (empty = static)

	// Get total pattern duration in ms
	uint32_t getTotalPatternDuration() const;
};

class CustomItemLightManager {
public:
	static CustomItemLightManager& instance();

	void load(const std::string& filepath);
	void clear();

	const CustomItemLight* find(uint16_t clientId) const;
	bool hasCustomLight(uint16_t clientId) const;

	// Get current intensity considering pattern animation
	// randomOffset is per-instance to desync animations
	uint8_t getCurrentIntensity(const CustomItemLight& light, uint32_t elapsedMs, uint32_t randomOffset) const;

	bool isLoaded() const { return loaded_; }
	bool hasAnimatedLights() const { return hasAnimated_; }
	size_t count() const { return lights_.size(); }

private:
	CustomItemLightManager() = default;
	std::unordered_map<uint16_t, CustomItemLight> lights_;
	bool loaded_ = false;
	bool hasAnimated_ = false;
};

#endif
