//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"

// glut include removed

#include "rendering/drawers/tiles/floor_drawer.h"
#include "rendering/drawers/entities/item_drawer.h"
#include "rendering/drawers/entities/sprite_drawer.h"
#include "rendering/drawers/entities/creature_drawer.h"
#include "rendering/core/render_view.h"
#include "rendering/core/drawing_options.h"
#include "editor/editor.h"
#include "map/tile.h"

#include <algorithm>

FloorDrawer::FloorDrawer() {
}

FloorDrawer::~FloorDrawer() {
}

void FloorDrawer::draw(SpriteBatch& sprite_batch, ItemDrawer* item_drawer, SpriteDrawer* sprite_drawer, CreatureDrawer* creature_drawer, const RenderView& view, const DrawingOptions& options, Editor& editor) {
	// Parallax shift of the floor being edited; every ghost floor is placed relative
	// to it, one tile per floor of distance (same rule the normal passes follow, but
	// written this way it also holds when a ghost crosses the surface/underground line).
	const int current_offset = view.getFloorAdjustment();
	auto offsetFor = [&](int map_z) {
		return current_offset + TILE_SIZE * (view.floor - map_z);
	};

	// Ghost Floors (radial wheel): N floors below first, deepest to nearest, then N
	// floors above, nearest to farthest, so the painter's order matches the camera.
	int ghost_above = 0;
	int ghost_below = 0;
	if (options.ghost_floors_enabled) {
		ghost_above = options.ghost_floors_above;
		ghost_below = options.ghost_floors_below;
	}

	auto alphaFor = [&](int distance, int count) {
		if (!options.ghost_floors_fade || count <= 1) {
			return options.ghost_floors_alpha;
		}
		// Linear fade down to roughly a third of the base alpha for the farthest floor.
		const float t = static_cast<float>(distance - 1) / static_cast<float>(count - 1);
		const int alpha = static_cast<int>(options.ghost_floors_alpha * (1.0f - 0.65f * t));
		return std::max(8, alpha);
	};

	// The fade is spread over the floors that actually exist, so "all floors" from
	// the surface still reaches the faintest step on floor 0.
	if (ghost_below > 0) {
		const int last_z = std::min(MAP_MAX_LAYER, view.floor + ghost_below);
		const int drawn = last_z - view.floor;
		for (int map_z = last_z; map_z > view.floor; --map_z) {
			drawGhostFloor(sprite_batch, item_drawer, sprite_drawer, creature_drawer, view, options, editor, map_z, offsetFor(map_z), alphaFor(map_z - view.floor, drawn));
		}
	}

	if (ghost_above > 0) {
		const int last_z = std::max(0, view.floor - ghost_above);
		const int drawn = view.floor - last_z;
		for (int map_z = view.floor - 1; map_z >= last_z; --map_z) {
			drawGhostFloor(sprite_batch, item_drawer, sprite_drawer, creature_drawer, view, options, editor, map_z, offsetFor(map_z), alphaFor(view.floor - map_z, drawn));
		}
	}

	// Ghost Higher Floors (Ctrl+L): the original single-floor pass. Skipped when Ghost
	// Floors already drew the floor above, so it is not blended twice.
	if (ghost_above == 0 && view.floor != 8 && view.floor != 0 && options.transparent_floors) {
		const int map_z = view.floor - 1;
		drawGhostFloor(sprite_batch, item_drawer, sprite_drawer, creature_drawer, view, options, editor, map_z, offsetFor(map_z), 96);
	}
}

void FloorDrawer::drawGhostFloor(SpriteBatch& sprite_batch, ItemDrawer* item_drawer, SpriteDrawer* sprite_drawer, CreatureDrawer* creature_drawer, const RenderView& view, const DrawingOptions& options, Editor& editor, int map_z, int draw_offset, int alpha) {
	if (map_z < 0 || map_z > MAP_MAX_LAYER) {
		return;
	}

	for (int map_x = view.start_x; map_x <= view.end_x; map_x++) {
		for (int map_y = view.start_y; map_y <= view.end_y; map_y++) {
			Tile* tile = editor.map.getTile(map_x, map_y, map_z);
			if (!tile) {
				continue;
			}

			// BlitItem takes the position by reference and shifts it by the sprite's
			// elevation, so items stack on the ground; keep the unshifted tile origin
			// for the "on top" pass, exactly like the TileRenderer does.
			const int tile_draw_x = ((map_x * TILE_SIZE) - view.view_scroll_x) - draw_offset;
			const int tile_draw_y = ((map_y * TILE_SIZE) - view.view_scroll_y) - draw_offset;
			int draw_x = tile_draw_x;
			int draw_y = tile_draw_y;

			if (tile->ground) {
				BlitItemParams params(tile, tile->ground.get(), options);
				params.alpha = alpha;
				if (tile->isPZ()) {
					params.red = 128;
					params.green = 255;
					params.blue = 128;
				} else {
					params.red = 255;
					params.green = 255;
					params.blue = 255;
				}
				item_drawer->BlitItem(sprite_batch, sprite_drawer, creature_drawer, draw_x, draw_y, params);
			}
			// Inclusive: aqui o teste sempre foi `zoom <= 10.0`, um passo depois do
			// que o TileRenderer usa.
			if (!options.drawLooseItemsInclusive()) {
				continue;
			}
			// Mesma ordem do TileRenderer/do client: os itens "on top"
			// (top order 3) sao guardados antes dos comuns na pilha do
			// tile, mas desenhados por ultimo (Tile::drawTop).
			bool has_top_items = false;
			for (const auto& item : tile->items) {
				if (options.show_only_grounds && !item->isBorder() && !item->isOptionalBorder()) {
					continue;
				}
				if (item->isAlwaysOnBottom() && item->getTopOrder() == 3) {
					has_top_items = true;
					continue;
				}
				BlitItemParams params(tile, item.get(), options);
				params.alpha = alpha;
				item_drawer->BlitItem(sprite_batch, sprite_drawer, creature_drawer, draw_x, draw_y, params);
			}
			if (has_top_items) {
				for (const auto& item : tile->items) {
					if (!item->isAlwaysOnBottom() || item->getTopOrder() != 3) {
						continue;
					}
					if (options.show_only_grounds && !item->isBorder() && !item->isOptionalBorder()) {
						continue;
					}
					BlitItemParams params(tile, item.get(), options);
					params.alpha = alpha;
					int top_draw_x = tile_draw_x;
					int top_draw_y = tile_draw_y;
					item_drawer->BlitItem(sprite_batch, sprite_drawer, creature_drawer, top_draw_x, top_draw_y, params);
				}
			}
		}
	}
}
