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
#include "rendering/utilities/render_profiler.h"
#include "rendering/core/render_order.h"

#include <algorithm>

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
	{
		RENDER_PROFILE_SCOPE(LightGather);
		LightGatherer::GatherFloor(editor->map, view, options, map_z, light_buffer);
	}

	// Common lambda to collect the tiles of a node
	auto gatherNode = [&](MapNode* nd, int nd_map_x, int nd_map_y, bool live) {
		int node_draw_x = nd_map_x * TILE_SIZE + base_screen_x;
		int node_draw_y = nd_map_y * TILE_SIZE + base_screen_y;

		// Node level culling
		if (!view.IsRectVisible(node_draw_x, node_draw_y, 4 * TILE_SIZE, 4 * TILE_SIZE, PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS)) {
			return;
		}

		if (live && !nd->isVisible(map_z > GROUND_LAYER)) {
			if (!nd->isRequested(map_z > GROUND_LAYER)) {
				// Request the node
				if (editor->live_manager.GetClient()) {
					editor->live_manager.GetClient()->queryNode(nd_map_x, nd_map_y, map_z > GROUND_LAYER);
				}
				nd->setRequested(map_z > GROUND_LAYER, true);
			}
			grid_drawer->DrawNodeLoadingPlaceholder(sprite_batch, nd_map_x, nd_map_y, view);
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
				// Tile vazio: DrawTile so voltaria na primeira linha dele.
				if (!location->get()) {
					continue;
				}

				// Culling: Skip tiles that are far outside the viewport.
				if (!fully_inside && !view.IsPixelVisible(draw_x_base, draw_y, PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS)) {
					continue;
				}

				ordered_tiles_.push_back(OrderedTile { location, draw_x_base, draw_y, nd_map_x + map_x, nd_map_y + map_y });
			}
		}
	};

	// Three passes per floor, mirroring the client: every ground first, then
	// every ground border, then the remaining contents. This keeps sprites
	// that overhang a neighbouring tile (draw offsets, big sprites) from
	// being covered by that neighbour's lower-order sprites. Dentro de cada
	// passada os tiles saem na ordem do cliente (linha no Black Talon, diagonal
	// no Battle Royale), e nao mais na ordem em que a grade espacial os guarda.
	auto drawPass = [&](TileRenderPass pass) {
		for (const OrderedTile& entry : ordered_tiles_) {
			RenderProfiler::Count(RenderProfiler::Counter::CpuTiles);
			tile_renderer->DrawTile(sprite_batch, entry.location, view, options, options.current_house_id, entry.draw_x, entry.draw_y, pass);
		}
	};

	// The chunk cache only works for the plain rendering modes: the special ones
	// paint squares instead of sprites, and a live client may not even hold the
	// tiles yet. O retangulo de selecao em arrasto NAO desliga mais o cache: o
	// shader dele escurece o que cai no retangulo, como o BlitItem.
	const bool use_chunk_cache = options.use_chunk_cache && chunk_cache && ctx && chunk_cache->isValid() && !live_client
		&& !options.show_as_minimap && !options.show_only_colors && !options.show_only_modified;

	const bool battle_royale = options.render_order == RenderOrderProfile::BattleRoyale;

	if (!use_chunk_cache) {
		RENDER_PROFILE_SCOPE(CpuTilePasses);
		ordered_tiles_.clear();
		if (live_client) {
			for (int nd_map_x = nd_start_x; nd_map_x <= nd_end_x; nd_map_x += 4) {
				for (int nd_map_y = nd_start_y; nd_map_y <= nd_end_y; nd_map_y += 4) {
					MapNode* nd = editor->map.getLeaf(nd_map_x, nd_map_y);
					if (!nd) {
						nd = editor->map.createLeaf(nd_map_x, nd_map_y);
						nd->setVisible(false, false);
					}
					gatherNode(nd, nd_map_x, nd_map_y, true);
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
				gatherNode(nd, nd_map_x, nd_map_y, false);
			});
		}
		// Nos modos de quadrado colorido a ordem nao muda nada na tela.
		if (!options.show_as_minimap && !options.show_only_colors) {
			sortOrderedTiles(battle_royale);
		}
		drawPass(TileRenderPass::Ground);
		drawPass(TileRenderPass::Borders);
		drawPass(TileRenderPass::Contents);
		return;
	}

	// Everything queued so far belongs under this floor's cached geometry,
	// which goes straight to the framebuffer instead of into the batch.
	{
		RENDER_PROFILE_SCOPE(FloorBatchFlush);
		sprite_batch.flush(ctx->atlas);
	}
	{
		RENDER_PROFILE_SCOPE(ChunkRender);
		chunk_cache->renderFloor(map_z, editor->map, *ctx, view.projectionMatrix, ctx->atlas);
	}

	// Tiles the bake had to skip -- animated grounds, selected borders,
	// overhang-free but interactive items -- still run through the CPU
	// renderer, in the very same pass order. Os deferidos de todos os chunks
	// visiveis entram numa lista so, ordenada pela chave do perfil: a ordem
	// global do andar, e nao chunk a chunk.
	auto drawDeferredPass = [&](ChunkDeferredPass mask, TileRenderPass pass, RenderProfiler::Counter counter) {
		deferred_tiles_.clear();
		chunk_cache->forEachDeferredTile(mask, [&](int map_x, int map_y) {
			deferred_tiles_.push_back(DeferredTile { RenderOrder::tileKey(options.render_order, map_x, map_y), map_x, map_y });
		});
		std::sort(deferred_tiles_.begin(), deferred_tiles_.end(), [](const DeferredTile& a, const DeferredTile& b) {
			return a.key < b.key;
		});

		for (const DeferredTile& deferred : deferred_tiles_) {
			TileLocation* location = editor->map.getTileL(deferred.map_x, deferred.map_y, map_z);
			if (!location || !location->get()) {
				continue;
			}

			const int draw_x = deferred.map_x * TILE_SIZE + base_screen_x;
			const int draw_y = deferred.map_y * TILE_SIZE + base_screen_y;
			if (!view.IsPixelVisible(draw_x, draw_y, PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS)) {
				continue;
			}

			RenderProfiler::Count(counter);
			tile_renderer->DrawTile(sprite_batch, location, view, options, options.current_house_id, draw_x, draw_y, pass);
		}
	};

	{
		RENDER_PROFILE_SCOPE(DeferredGround);
		drawDeferredPass(CHUNK_DEFER_GROUND, TileRenderPass::Ground, RenderProfiler::Counter::DeferredGround);
	}
	{
		RENDER_PROFILE_SCOPE(DeferredBorders);
		drawDeferredPass(CHUNK_DEFER_BORDERS, TileRenderPass::Borders, RenderProfiler::Counter::DeferredBorders);
	}

	// O conteudo cacheado vem so agora, depois das passadas de CPU de chao e
	// borda: um chao deferido (agua animada, por exemplo) fica embaixo do
	// conteudo dos vizinhos -- inclusive das montanhas, que moram nessa faixa.
	// E a mesma ordem das tres passadas sem cache.
	{
		RENDER_PROFILE_SCOPE(FloorBatchFlush);
		sprite_batch.flush(ctx->atlas);
	}
	{
		RENDER_PROFILE_SCOPE(ChunkRenderContents);
		chunk_cache->renderFloorContents(*ctx, view.projectionMatrix, ctx->atlas);
	}

	// This is where the frame time actually goes: with the contents baked too,
	// only the tiles that really change per frame are walked on the CPU.
	{
		RENDER_PROFILE_SCOPE(DeferredContents);
		drawDeferredPass(CHUNK_DEFER_CONTENTS, TileRenderPass::Contents, RenderProfiler::Counter::DeferredContents);
	}
}

void MapLayerDrawer::sortOrderedTiles(bool battle_royale) {
	if (ordered_tiles_.size() < 2) {
		return;
	}

	// Counting sort estavel por uma chave inteira pequena (a faixa visivel).
	auto countingSort = [this](std::vector<OrderedTile>& from, std::vector<OrderedTile>& to, auto&& key) {
		int lo = key(from.front());
		int hi = lo;
		for (const OrderedTile& entry : from) {
			const int k = key(entry);
			lo = std::min(lo, k);
			hi = std::max(hi, k);
		}
		sort_counts_.assign(static_cast<size_t>(hi - lo) + 2, 0u);
		for (const OrderedTile& entry : from) {
			++sort_counts_[static_cast<size_t>(key(entry) - lo) + 1];
		}
		for (size_t i = 1; i < sort_counts_.size(); ++i) {
			sort_counts_[i] += sort_counts_[i - 1];
		}
		to.resize(from.size());
		for (const OrderedTile& entry : from) {
			to[sort_counts_[static_cast<size_t>(key(entry) - lo)]++] = entry;
		}
	};

	// Secundaria primeiro (x crescente), depois a primaria, estavel: a mesma
	// ordem de RenderOrder::tileKey().
	countingSort(ordered_tiles_, ordered_scratch_, [](const OrderedTile& entry) { return entry.map_x; });
	if (battle_royale) {
		countingSort(ordered_scratch_, ordered_tiles_, [](const OrderedTile& entry) { return entry.map_x + entry.map_y; });
	} else {
		countingSort(ordered_scratch_, ordered_tiles_, [](const OrderedTile& entry) { return entry.map_y; });
	}
}
