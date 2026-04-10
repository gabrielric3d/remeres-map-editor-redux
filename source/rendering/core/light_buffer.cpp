#include "light_buffer.h"

void LightBuffer::AddLight(int map_x, int map_y, int map_z, const SpriteLight& light) {
	if (map_z <= GROUND_LAYER) {
		map_x -= (GROUND_LAYER - map_z);
		map_y -= (GROUND_LAYER - map_z);
	}

	if (map_x <= 0 || map_x >= MAP_MAX_WIDTH || map_y <= 0 || map_y >= MAP_MAX_HEIGHT) {
		return;
	}

	uint8_t intensity = std::min(light.intensity, static_cast<uint8_t>(255)); // Assumed max

	if (!lights.empty()) {
		Light& previous = lights.back();
		if (previous.map_x == map_x && previous.map_y == map_y && previous.color == light.color) {
			previous.intensity = std::max(previous.intensity, intensity);
			return;
		}
	}

	lights.push_back(Light { static_cast<uint16_t>(map_x), static_cast<uint16_t>(map_y), light.color, intensity });
}

void LightBuffer::Clear() {
	lights.clear();
	blocking_grid.clear();
}

// ============================================================================
// BlockingGrid
// ============================================================================

void LightBuffer::BlockingGrid::resize(int ox, int oy, int w, int h) {
	origin_x = ox;
	origin_y = oy;
	width = w;
	height = h;
	data.assign(static_cast<size_t>(w) * h, 0);
}

void LightBuffer::BlockingGrid::clear() {
	if (!data.empty()) {
		std::fill(data.begin(), data.end(), static_cast<uint8_t>(0));
	}
}

void LightBuffer::BlockingGrid::setBlocking(int map_x, int map_y, bool blocking) {
	int lx = map_x - origin_x;
	int ly = map_y - origin_y;
	if (lx >= 0 && lx < width && ly >= 0 && ly < height) {
		data[ly * width + lx] = blocking ? 1 : 0;
	}
}

bool LightBuffer::BlockingGrid::isBlocking(int map_x, int map_y) const {
	int lx = map_x - origin_x;
	int ly = map_y - origin_y;
	if (lx < 0 || lx >= width || ly < 0 || ly >= height) {
		return false;
	}
	return data[ly * width + lx] != 0;
}
