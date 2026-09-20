//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "app/definitions.h"
#include "rendering/drawers/map_layer_drawer.h"
#include "rendering/drawers/tiles/tile_renderer.h"
#include "rendering/drawers/overlays/grid_drawer.h"
#include "editor/editor.h"
#include "live/live_client.h"
#include "map/map.h"
#include "map/map_region.h"
#include "rendering/core/render_view.h"
#include "rendering/core/drawing_options.h"
#include "rendering/core/light_buffer.h"
#include "rendering/core/sprite_batch.h"
#include "rendering/core/primitive_renderer.h"
#include "rendering/core/sprite_preloader.h"
#include "rendering/core/chunk_cache_manager.h"
#include "rendering/core/render_frame_context.h"
#include "rendering/core/atlas_manager.h"
#include "rendering/core/light_gatherer.h"

MapLayerDrawer::MapLayerDrawer(TileRenderer* tile_renderer, GridDrawer* grid_drawer, Editor* editor) :
	tile_renderer(tile_renderer),
	grid_drawer(grid_drawer),
	editor(editor) {
}

MapLayerDrawer::~MapLayerDrawer() {
}

void MapLayerDrawer::Draw(SpriteBatch& sprite_batch, int map_z, bool live_client, const RenderView& view, const DrawingOptions& options, LightBuffer& light_buffer, ChunkCacheManager* chunk_cache, const RenderFrameContext* ctx) {
	int nd_start_x = view.start_x & ~3;
	int nd_start_y = view.start_y & ~3;
	int nd_end_x = (view.end_x & ~3) + 4;
	int nd_end_y = (view.end_y & ~3) + 4;

	// Optimization: Pre-calculate offset and base coordinates
	// IsTileVisible does this for every tile, but it's constant per layer/frame.
	// We also skip IsTileVisible because visitLeaves already bounds us to the visible area (with 4-tile alignment),
	// which is well within IsTileVisible's 6-tile margin.
	int offset = (map_z <= GROUND_LAYER)
		? (GROUND_LAYER - map_z) * TILE_SIZE
		: TILE_SIZE * (view.floor - map_z);

	int base_screen_x = -view.view_scroll_x - offset;
	int base_screen_y = -view.view_scroll_y - offset;

	// The light buffer used to be filled by DrawTile as it walked the tiles.
	// With the chunk cache that walk skips most tiles, so the lights come from
	// a dedicated pass that runs whether or not the cache is on -- one code
	// path, one behaviour.
	LightGatherer::GatherFloor(editor->map, view, options, map_z, light_buffer);

	// Common lambda to draw a node
	auto drawNode = [&](MapNode* nd, int nd_map_x, int nd_map_y, bool live, TileRenderPass pass) {
		int node_draw_x = nd_map_x * TILE_SIZE + base_screen_x;
		int node_draw_y = nd_map_y * TILE_SIZE + base_screen_y;

		// Node level culling
		if (!view.IsRectVisible(node_draw_x, node_draw_y, 4 * TILE_SIZE, 4 * TILE_SIZE, PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS)) {
			return;
		}

		if (live && !nd->isVisible(map_z > GROUND_LAYER)) {
			// Only the first pass requests the node and draws the placeholder.
			if (pass == TileRenderPass::Ground || pass == TileRenderPass::All) {
				if (!nd->isRequested(map_z > GROUND_LAYER)) {
					// Request the node
					if (editor->live_manager.GetClient()) {
						editor->live_manager.GetClient()->queryNode(nd_map_x, nd_map_y, map_z > GROUND_LAYER);
					}
					nd->setRequested(map_z > GROUND_LAYER, true);
				}
				grid_drawer->DrawNodeLoadingPlaceholder(sprite_batch, nd_map_x, nd_map_y, view);
			}
			return;
		}

		bool fully_inside = view.IsRectFullyInside(node_draw_x, node_draw_y, 4 * TILE_SIZE, 4 * TILE_SIZE);

		Floor* floor = nd->getFloor(map_z);
		if (!floor) {
			return;
		}

		TileLocation* location = floor->locs.data();
		int draw_x_base = node_draw_x;
		for (int map_x = 0; map_x < 4; ++map_x, draw_x_base += TILE_SIZE) {
			int draw_y = node_draw_y;
			for (int map_y = 0; map_y < 4; ++map_y, ++location, draw_y += TILE_SIZE) {
				// Tile vazio: DrawTile so voltaria na primeira linha dele. Testar aqui
				// evita a chamada, e como sao tres passadas por andar o desconto vale
				// tres vezes.
				if (!location->get()) {
					continue;
				}

				// Culling: Skip tiles that are far outside the viewport.
				if (!fully_inside && !view.IsPixelVisible(draw_x_base, draw_y, PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS)) {
					continue;
				}

				tile_renderer->DrawTile(sprite_batch, location, view, options, options.current_house_id, draw_x_base, draw_y, pass);
			}
		}
	};

	// Three passes per floor, mirroring the client: every ground first, then
	// every ground border, then the remaining contents. This keeps sprites
	// that overhang a neighbouring tile (draw offsets, big sprites) from
	// being covered by that neighbour's lower-order sprites.
	auto drawPass = [&](TileRenderPass pass) {
		if (live_client) {
			for (int nd_map_x = nd_start_x; nd_map_x <= nd_end_x; nd_map_x += 4) {
				for (int nd_map_y = nd_start_y; nd_map_y <= nd_end_y; nd_map_y += 4) {
					MapNode* nd = editor->map.getLeaf(nd_map_x, nd_map_y);
					if (!nd) {
						nd = editor->map.createLeaf(nd_map_x, nd_map_y);
						nd->setVisible(false, false);
					}
					drawNode(nd, nd_map_x, nd_map_y, true, pass);
				}
			}
		} else {
			// Use SpatialHashGrid::visitLeaves which handles O(1) viewport query internally
			// Expand the query range slightly to handle the 4-tile alignment and safety margin
			int safe_start_x = nd_start_x - PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS / TILE_SIZE;
			int safe_start_y = nd_start_y - PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS / TILE_SIZE;
			int safe_end_x = nd_end_x + PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS / TILE_SIZE;
			int safe_end_y = nd_end_y + PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS / TILE_SIZE;

			editor->map.visitLeaves(safe_start_x, safe_start_y, safe_end_x, safe_end_y, [&](MapNode* nd, int nd_map_x, int nd_map_y) {
				drawNode(nd, nd_map_x, nd_map_y, false, pass);
			});
		}
	};

	// The chunk cache only replaces the ground and border passes, and only for
	// the plain rendering modes: the special ones paint squares instead of
	// sprites, and a live client may not even hold the tiles yet.
	const bool use_chunk_cache = options.use_chunk_cache && chunk_cache && ctx && chunk_cache->isValid() && !live_client
		&& !options.show_as_minimap && !options.show_only_colors && !options.show_only_modified
		&& !options.transient_selection_bounds.has_value();

	if (!use_chunk_cache) {
		drawPass(TileRenderPass::Ground);
		drawPass(TileRenderPass::Borders);
		drawPass(TileRenderPass::Contents);
		return;
	}

	// Everything queued so far belongs under this floor's cached geometry,
	// which goes straight to the framebuffer instead of into the batch.
	sprite_batch.flush(ctx->atlas);
	chunk_cache->renderFloor(map_z, editor->map, *ctx, view.projectionMatrix, ctx->atlas);

	// Tiles the bake had to skip -- animated grounds, selected borders,
	// overhang-free but interactive items -- still run through the CPU
	// renderer, in the very same pass order.
	auto drawDeferredTile = [&](int map_x, int map_y, TileRenderPass pass) {
		TileLocation* location = editor->map.getTileL(map_x, map_y, map_z);
		if (!location || !location->get()) {
			return;
		}

		const int draw_x = map_x * TILE_SIZE + base_screen_x;
		const int draw_y = map_y * TILE_SIZE + base_screen_y;
		if (!view.IsPixelVisible(draw_x, draw_y, PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS)) {
			return;
		}

		tile_renderer->DrawTile(sprite_batch, location, view, options, options.current_house_id, draw_x, draw_y, pass);
	};

	chunk_cache->forEachDeferredTile(CHUNK_DEFER_GROUND, [&](int map_x, int map_y) {
		drawDeferredTile(map_x, map_y, TileRenderPass::Ground);
	});
	chunk_cache->forEachDeferredTile(CHUNK_DEFER_BORDERS, [&](int map_x, int map_y) {
		drawDeferredTile(map_x, map_y, TileRenderPass::Borders);
	});
	// This is where the frame time actually goes: with the contents baked too,
	// only the tiles that really change per frame are walked on the CPU.
	chunk_cache->forEachDeferredTile(CHUNK_DEFER_CONTENTS, [&](int map_x, int map_y) {
		drawDeferredTile(map_x, map_y, TileRenderPass::Contents);
	});
}
