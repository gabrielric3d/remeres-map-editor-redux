#ifndef RME_RENDERING_TILE_COLOR_CALCULATOR_H_
#define RME_RENDERING_TILE_COLOR_CALCULATOR_H_

#include <cstdint>

class Tile;
struct DrawingOptions;

class TileColorCalculator {
public:
	static void Calculate(const Tile* tile, const DrawingOptions& options, uint32_t current_house_id, int spawn_count, uint8_t& r, uint8_t& g, uint8_t& b);
	static void GetHouseColor(uint32_t house_id, uint8_t& r, uint8_t& g, uint8_t& b);
	// BlackTalon: deterministic distinct color per sound zone id (hash -> RGB),
	// same idea as GetHouseColor so each painted zone reads as its own color.
	static void GetSoundZoneColor(uint32_t zone_id, uint8_t& r, uint8_t& g, uint8_t& b);
	static void GetInstanceZoneColor(uint32_t zone_id, uint8_t& r, uint8_t& g, uint8_t& b);
	static void GetMinimapColor(const Tile* tile, uint8_t& r, uint8_t& g, uint8_t& b);
};

#endif
