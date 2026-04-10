//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
//
// NOTE: This is a CPU fallback reference implementation for shadow occlusion.
// It is NOT currently integrated into the rendering pipeline.
// The GPU shader in light_drawer.cpp handles shadow occlusion via SSBO + Bresenham in GLSL.
// This class is kept as reference for a potential future CPU fallback path.
//

#ifndef RME_RENDERING_CORE_SHADOW_CASTER_H_
#define RME_RENDERING_CORE_SHADOW_CASTER_H_

#include <vector>
#include <cstdint>

class ShadowCaster {
public:
	// Initialize with map dimensions for the visible area
	void resize(int width, int height);
	void clear();

	// Mark a tile as blocking light
	void setBlocking(int tileX, int tileY, bool blocking);
	bool isBlocking(int tileX, int tileY) const;

	// Check if ray from light source to target is blocked
	// Returns visibility factor 0.0 (fully blocked) to 1.0 (fully visible)
	// Uses multi-sample (5 samples: center + 4 corners) for soft penumbra
	float getVisibility(int fromTileX, int fromTileY, int toTileX, int toTileY) const;

	int getWidth() const { return width_; }
	int getHeight() const { return height_; }
	const std::vector<uint8_t>& getData() const { return blocking_; }

private:
	bool rayBlocked(int x0, int y0, int x1, int y1) const;

	int width_ = 0;
	int height_ = 0;
	std::vector<uint8_t> blocking_; // flat grid [y * width + x]
};

#endif
