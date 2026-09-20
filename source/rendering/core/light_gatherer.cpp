//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/core/light_gatherer.h"

#include "app/definitions.h"
#include "rendering/core/custom_item_light.h"
#include "rendering/core/drawing_options.h"
#include "rendering/core/light_buffer.h"
#include "rendering/core/render_view.h"
#include "game/item.h"
#include "map/basemap.h"
#include "map/map_region.h"
#include "map/tile.h"

namespace {
	// Same offset formula TileRenderer used, so a custom light keeps the exact
	// phase it had before this walk was split out.
	void addCustomItemLight(LightBuffer& buffer, const Position& position, uint16_t clientId, uint32_t elapsed) {
		const auto* custom_light = CustomItemLightManager::instance().find(clientId);
		if (!custom_light) {
			return;
		}
		const uint32_t total_duration = custom_light->getTotalPatternDuration();
		const uint32_t offset = total_duration > 0
			? (position.x * 7919u + position.y * 7927u + position.z * 7933u) % total_duration
			: 0;
		const uint8_t intensity = CustomItemLightManager::instance().getCurrentIntensity(*custom_light, elapsed, offset);
		if (intensity > 0) {
			SpriteLight light;
			light.color = custom_light->color;
			light.intensity = intensity;
			buffer.AddLight(position.x, position.y, position.z, light);
		}
	}
}

void LightGatherer::GatherFloor(
	const BaseMap& map,
	const RenderView& view,
	const DrawingOptions& options,
	int map_z,
	LightBuffer& out_buffer
) {
	const bool want_lights = options.isDrawLight() && view.zoom <= 10.0;
	const bool want_blocking = want_lights && (options.show_shadow_occlusion || options.show_forced_light_zones);
	if (!want_lights) {
		return;
	}

	const bool want_custom = options.show_custom_item_lights;
	const uint32_t elapsed = want_custom
		? static_cast<uint32_t>(wxGetLocalTimeMillis().GetValue())
		: 0u;

	// Same node range the layer drawer walks, so a light at the edge of the
	// viewport reaches the buffer exactly like it did before.
	const int nd_start_x = (view.start_x & ~3) - PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS / TILE_SIZE;
	const int nd_start_y = (view.start_y & ~3) - PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS / TILE_SIZE;
	const int nd_end_x = (view.end_x & ~3) + 4 + PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS / TILE_SIZE;
	const int nd_end_y = (view.end_y & ~3) + 4 + PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS / TILE_SIZE;

	map.visitLeaves(nd_start_x, nd_start_y, nd_end_x, nd_end_y, [&](const MapNode* nd, int nd_map_x, int nd_map_y) {
		const Floor* floor = nd->getFloor(map_z);
		if (!floor) {
			return;
		}

		for (int local = 0; local < 16; ++local) {
			const TileLocation& location = floor->locs[local];
			const Tile* tile = location.get();
			if (!tile) {
				continue;
			}

			const Position& position = location.getPosition();

			if (want_blocking && tile->isBlocking()) {
				out_buffer.blocking_grid.setBlocking(position.x, position.y, true);
			}

			// Ground light. hasLight() on the tile is the cheap gate the tile
			// renderer used before touching the ground at all.
			if (tile->hasLight() && tile->ground && tile->ground->hasLight()) {
				out_buffer.AddLight(position.x, position.y, position.z, tile->ground->getLight());
			}
			if (want_custom && tile->ground) {
				addCustomItemLight(out_buffer, position, tile->ground->getClientID(), elapsed);
			}

			for (const auto& item : tile->items) {
				if (item->hasLight()) {
					out_buffer.AddLight(position.x, position.y, position.z, item->getLight());
				}
				if (want_custom) {
					addCustomItemLight(out_buffer, position, item->getClientID(), elapsed);
				}
			}
		}
	});
}
