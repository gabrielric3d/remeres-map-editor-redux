#ifndef RME_RENDERING_CORE_LIGHT_BUFFER_H_
#define RME_RENDERING_CORE_LIGHT_BUFFER_H_

#include <vector>
#include <cstdint>
#include <algorithm>
#include "rendering/core/sprite_light.h"
#include "app/definitions.h"

struct LightBuffer {
	struct Light {
		uint16_t map_x = 0;
		uint16_t map_y = 0;
		uint8_t color = 0;
		uint8_t intensity = 0;
	};

	// Blocking tile tracking (for shadow occlusion)
	struct BlockingGrid {
		int origin_x = 0, origin_y = 0; // map origin of the grid
		int width = 0, height = 0;
		std::vector<uint8_t> data; // 1 = blocking, 0 = not

		void resize(int ox, int oy, int w, int h);
		void clear();
		void setBlocking(int map_x, int map_y, bool blocking);
		bool isBlocking(int map_x, int map_y) const;
	};

	std::vector<Light> lights;
	BlockingGrid blocking_grid;

	void AddLight(int map_x, int map_y, int map_z, const SpriteLight& light);
	void Clear();
};

#endif
