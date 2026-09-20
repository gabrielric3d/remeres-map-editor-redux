//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_CORE_LIGHT_GATHERER_H_
#define RME_RENDERING_CORE_LIGHT_GATHERER_H_

#include <cstdint>

class BaseMap;
struct RenderView;
struct DrawingOptions;
struct LightBuffer;

/**
 * Fills the LightBuffer by walking the map directly, instead of riding along
 * with the tile renderer.
 *
 * TileRenderer used to add every light while it blitted, which only worked
 * because it visited every visible tile. The chunk cache skips the tiles it
 * already baked, so the lights of those tiles would silently disappear. This
 * walk is the replacement: the same collection rules, on a pass of its own
 * that touches no sprite, no pattern and no atlas.
 *
 * It keeps everything the editor's own lighting features need -- .dat lights,
 * custom item lights and the blocking grid used by shadow occlusion and by
 * forced light zones.
 */
class LightGatherer {
public:
	/**
	 * Collect the lights of one floor into out_buffer.
	 * Does nothing when the options have lighting turned off.
	 */
	static void GatherFloor(
		const BaseMap& map,
		const RenderView& view,
		const DrawingOptions& options,
		int map_z,
		LightBuffer& out_buffer
	);
};

#endif
