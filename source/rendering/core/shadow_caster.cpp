//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
//
// NOTE: This is a CPU fallback reference implementation for shadow occlusion.
// It is NOT currently integrated into the rendering pipeline.
// The GPU shader in light_drawer.cpp handles shadow occlusion via SSBO + Bresenham in GLSL.
// This class is kept as reference for a potential future CPU fallback path.
//

#include "rendering/core/shadow_caster.h"

#include <cmath>
#include <algorithm>

void ShadowCaster::resize(int width, int height) {
	width_ = width;
	height_ = height;
	blocking_.assign(static_cast<size_t>(width) * height, 0);
}

void ShadowCaster::clear() {
	std::fill(blocking_.begin(), blocking_.end(), static_cast<uint8_t>(0));
}

void ShadowCaster::setBlocking(int tileX, int tileY, bool blocking) {
	if (tileX >= 0 && tileX < width_ && tileY >= 0 && tileY < height_) {
		blocking_[tileY * width_ + tileX] = blocking ? 1 : 0;
	}
}

bool ShadowCaster::isBlocking(int tileX, int tileY) const {
	if (tileX < 0 || tileX >= width_ || tileY < 0 || tileY >= height_) {
		return false;
	}
	return blocking_[tileY * width_ + tileX] != 0;
}

bool ShadowCaster::rayBlocked(int x0, int y0, int x1, int y1) const {
	// Bresenham line algorithm
	int dx = std::abs(x1 - x0);
	int dy = std::abs(y1 - y0);
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int err = dx - dy;
	int x = x0;
	int y = y0;
	bool first = true;

	while (x != x1 || y != y1) {
		if (!first && isBlocking(x, y)) {
			return true;
		}
		first = false;

		int e2 = 2 * err;
		if (e2 > -dy) {
			err -= dy;
			x += sx;
		}
		if (e2 < dx) {
			err += dx;
			y += sy;
		}
	}
	return false;
}

float ShadowCaster::getVisibility(int fromTileX, int fromTileY, int toTileX, int toTileY) const {
	if (width_ == 0 || height_ == 0) {
		return 1.0f;
	}

	// Multi-sample with 5 points for soft penumbra
	// We use half-tile offsets in a doubled coordinate space
	int fx2 = fromTileX * 2 + 1; // center of from tile (doubled)
	int fy2 = fromTileY * 2 + 1;
	int tx2 = toTileX * 2 + 1; // center of to tile (doubled)
	int ty2 = toTileY * 2 + 1;

	// 5 sample points around the target (center + 4 offset directions)
	struct Sample {
		int ox, oy;
	};
	static const Sample samples[] = {
		{ 0,  0}, // center
		{-1, -1}, // top-left offset
		{ 1, -1}, // top-right offset
		{-1,  1}, // bottom-left offset
		{ 1,  1}, // bottom-right offset
	};

	int visible = 0;
	for (const auto& s : samples) {
		// Convert doubled coords back to tile coords
		int sx = (tx2 + s.ox) / 2;
		int sy = (ty2 + s.oy) / 2;
		if (!rayBlocked(fromTileX, fromTileY, sx, sy)) {
			visible++;
		}
	}

	return static_cast<float>(visible) / 5.0f;
}
