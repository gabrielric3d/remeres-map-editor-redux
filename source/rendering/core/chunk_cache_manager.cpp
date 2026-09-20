//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/core/chunk_cache_manager.h"

#include "app/definitions.h"
#include "rendering/core/atlas_manager.h"
#include "rendering/core/drawing_options.h"
#include "rendering/core/game_sprite.h"
#include "rendering/core/graphics.h"
#include "rendering/core/render_frame_context.h"
#include "rendering/core/render_view.h"
#include "rendering/core/shared_geometry.h"
#include "rendering/core/sprite_atlas_lut.h"
#include "rendering/core/sprite_preloader.h"
#include "rendering/core/light_source_manager.h"
#include "rendering/drawers/overlays/mountain_overlay_drawer.h"
#include "rendering/drawers/tiles/tile_color_calculator.h"
#include "rendering/utilities/pattern_calculator.h"
#include "brushes/ground/ground_brush.h"
#include "game/item.h"
#include "map/map.h"
#include "map/tile.h"

#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {
	// Geometry stores a sprite id; the fragment's atlas UVs come from the
	// SpriteAtlasLUT SSBO, so a chunk never has to be re-baked because the
	// atlas moved a sprite around.
	constexpr const char* CHUNK_VERT_SHADER = R"(#version 430 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aRect;
layout(location = 3) in uint aSpriteId;
layout(location = 4) in uint aFlags;
layout(location = 5) in vec4 aTint;

struct SpriteLUTEntry {
	vec4 uv_rect;
	float layer;
	float valid;
	vec2 _pad;
};

layout(std430, binding = 2) readonly buffer AtlasLUT {
	SpriteLUTEntry lutEntries[];
};

uniform mat4 uMVP;
uniform vec4 uGlobalTint;

out vec3 vTexCoord;
out vec4 vColor;

void main() {
	vec2 worldPos = aRect.xy + aPos * aRect.zw;
	gl_Position = uMVP * vec4(worldPos, 0.0, 1.0);

	SpriteLUTEntry entry = lutEntries[aSpriteId];
	vec2 uv = mix(entry.uv_rect.xy, entry.uv_rect.zw, aTexCoord);
	vTexCoord = vec3(uv, entry.layer);
	vColor = aTint * uGlobalTint * entry.valid;
}
)";

	constexpr const char* CHUNK_FRAG_SHADER = R"(#version 430 core
in vec3 vTexCoord;
in vec4 vColor;
out vec4 FragColor;

uniform sampler2DArray uAtlas;

void main() {
	vec4 texColor = texture(uAtlas, vTexCoord);
	FragColor = texColor * vColor;
	if (FragColor.a < 0.01) {
		discard;
	}
}
)";

	constexpr float INV_255 = 1.0f / 255.0f;

	// Item ids that ItemDrawer::BlitItem paints as a flat colored square (or
	// swaps for the light-source sprite) instead of blitting the real sprite.
	// Those paths depend on live options, so their tiles stay on the CPU.
	bool isTechnicalSpecialCase(ClientItemId cid) noexcept {
		switch (cid) {
			case 469:
			case 470:
			case 2187:
			case 17970:
			case 20028:
			case 34168:
			case 39236:
			case 39367:
			case 39368:
				return true;
			default:
				break;
		}
		return cid >= 39092 && cid <= 39100;
	}

	// Can this item be frozen into a chunk buffer at all? Everything BlitItem
	// decides per frame -- indicators, selection tint, animation, technical
	// overlays -- disqualifies it, and its tile goes back to the CPU path.
	bool itemIsBakeable(const Item* item, const ItemDefinitionView& it, const GameSprite* spr, const DrawingOptions& options) {
		if (!spr || !it || it.isMetaItem()) {
			return false;
		}
		if (item->isInvalidOTBMItem()) {
			return false;
		}
		if (spr->isAnimated()) {
			return false;
		}
		if (item->isSelected()) {
			return false;
		}
		if (!options.show_items && it.hasFlag(ItemFlag::Pickupable)) {
			return false;
		}
		if (it.isPodium() || item->isTeleport()) {
			return false;
		}
		if (options.highlight_locked_doors && !options.ingame && it.isDoor()) {
			return false;
		}
		if (options.show_hooks && !options.ingame && (it.hasFlag(ItemFlag::HookSouth) || it.hasFlag(ItemFlag::HookEast))) {
			return false;
		}
		if (!options.ingame && ((options.show_pickupables && it.hasFlag(ItemFlag::Pickupable)) || (options.show_moveables && it.hasFlag(ItemFlag::Moveable)))) {
			return false;
		}
		if (options.show_light_str && !options.ingame && spr->hasLight() && spr->getLight().intensity > 0) {
			return false;
		}
		if (options.show_tech_items && !options.ingame) {
			const ClientItemId cid = it.clientId();
			if (isTechnicalSpecialCase(cid) || LightSourceManager::instance().isLightSource(cid)) {
				return false;
			}
		}
		return true;
	}

	// The alpha halving BlitItem applies for the two transparency options.
	int applyTransparency(int alpha, const ItemDefinitionView& it, const GameSprite* spr, const DrawingOptions& options) {
		const bool big = spr->width > 1 || spr->height > 1;
		if (options.transparent_items && (!it.isGroundTile() || big) && !it.isSplash() && (!it.hasFlag(ItemFlag::IsBorder) || big)) {
			return alpha >> 1;
		}
		if (options.transparent_grounds && (it.isGroundTile() || it.hasFlag(ItemFlag::IsBorder)) && !big && !it.isSplash()) {
			return alpha >> 1;
		}
		return alpha;
	}
}

ChunkCacheManager::ChunkCacheManager() {
	bake_buffer_.reserve(2048);
}

ChunkCacheManager::~ChunkCacheManager() {
	release();
}

bool ChunkCacheManager::initialize() {
	release();

	if (!SharedGeometry::Instance().initialize()) {
		spdlog::error("[ChunkCache] Failed to initialize shared geometry");
		return false;
	}

	if (!shader_.Load(CHUNK_VERT_SHADER, CHUNK_FRAG_SHADER)) {
		spdlog::error("[ChunkCache] Failed to compile chunk shader");
		return false;
	}
	shader_initialized_ = true;

	glCreateVertexArrays(1, &vao_);
	if (vao_ == 0) {
		spdlog::error("[ChunkCache] Failed to create VAO");
		release();
		return false;
	}

	// Binding 0: static unit quad geometry
	glVertexArrayVertexBuffer(vao_, 0, SharedGeometry::Instance().getQuadVBO(), 0, 4 * sizeof(float));
	glVertexArrayElementBuffer(vao_, SharedGeometry::Instance().getQuadEBO());

	// Loc 0: quad pos (vec2)
	glEnableVertexArrayAttrib(vao_, 0);
	glVertexArrayAttribFormat(vao_, 0, 2, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribBinding(vao_, 0, 0);

	// Loc 1: quad texcoord (vec2)
	glEnableVertexArrayAttrib(vao_, 1);
	glVertexArrayAttribFormat(vao_, 1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float));
	glVertexArrayAttribBinding(vao_, 1, 0);

	// Binding 1: instance data from the chunk VBO
	glVertexArrayBindingDivisor(vao_, 1, 1);

	// Loc 2: aRect (vec4: x, y, w, h)
	glEnableVertexArrayAttrib(vao_, 2);
	glVertexArrayAttribFormat(vao_, 2, 4, GL_FLOAT, GL_FALSE, offsetof(TileInstance, x));
	glVertexArrayAttribBinding(vao_, 2, 1);

	// Loc 3: aSpriteId (uint)
	glEnableVertexArrayAttrib(vao_, 3);
	glVertexArrayAttribIFormat(vao_, 3, 1, GL_UNSIGNED_INT, offsetof(TileInstance, sprite_id));
	glVertexArrayAttribBinding(vao_, 3, 1);

	// Loc 4: aFlags (uint)
	glEnableVertexArrayAttrib(vao_, 4);
	glVertexArrayAttribIFormat(vao_, 4, 1, GL_UNSIGNED_INT, offsetof(TileInstance, flags));
	glVertexArrayAttribBinding(vao_, 4, 1);

	// Loc 5: aTint (vec4: r, g, b, a)
	glEnableVertexArrayAttrib(vao_, 5);
	glVertexArrayAttribFormat(vao_, 5, 4, GL_FLOAT, GL_FALSE, offsetof(TileInstance, r));
	glVertexArrayAttribBinding(vao_, 5, 1);

	spdlog::info("[ChunkCache] Initialized (VAO: {}, max {} chunks, target {})", vao_, MAX_CACHED_CHUNKS, TARGET_CACHED_CHUNKS);
	return true;
}

void ChunkCacheManager::release() {
	if (vao_ != 0 || !cached_chunks_.empty()) {
		spdlog::info("[ChunkCache] Released ({} chunk VBOs, VAO {})", cached_chunks_.size(), vao_);
	}
	if (vao_ != 0) {
		glDeleteVertexArrays(1, &vao_);
		vao_ = 0;
	}
	cached_chunks_.clear();
	active_visible_chunks_.clear();
	active_floor_ = -1;
	shader_initialized_ = false;
	current_frame_ = 0;
	has_bake_signature_ = false;
	last_atlas_ = nullptr;
	last_eviction_generation_ = 0;
}

void ChunkCacheManager::updateDirtyState(SpatialChangeTracker& change_tracker) {
	if (change_tracker.isAllDirty()) {
		invalidateAll();
		change_tracker.clearDirty();
		return;
	}

	auto dirty_chunks = change_tracker.takeDirtyChunks();
	for (const auto& coord : dirty_chunks) {
		auto it = cached_chunks_.find(coord);
		if (it != cached_chunks_.end()) {
			it->second.is_dirty = true;
			it->second.pending_bake_retries = 0;
		}
	}
}

void ChunkCacheManager::updateOptionsState(uint64_t bake_signature) {
	if (!has_bake_signature_) {
		last_bake_signature_ = bake_signature;
		has_bake_signature_ = true;
		return;
	}
	if (bake_signature != last_bake_signature_) {
		last_bake_signature_ = bake_signature;
		invalidateAll();
	}
}

void ChunkCacheManager::updateAtlasState(const AtlasManager* atlas) {
	if (atlas != last_atlas_) {
		// A different AtlasManager means the sprites the cache was baked from
		// no longer exist. Drop the buffers rather than re-baking them.
		if (last_atlas_ != nullptr && !cached_chunks_.empty()) {
			spdlog::info("[ChunkCache] AtlasManager replaced, dropping {} cached chunk(s)", cached_chunks_.size());
		}
		cached_chunks_.clear();
		active_visible_chunks_.clear();
		last_atlas_ = atlas;
		last_eviction_generation_ = atlas ? atlas->getEvictionGeneration() : 0;
		return;
	}

	if (!atlas) {
		return;
	}

	const uint64_t generation = atlas->getEvictionGeneration();
	if (generation != last_eviction_generation_) {
		// The texture GC freed at least one atlas slot; anything baked against
		// it has to be rebuilt.
		last_eviction_generation_ = generation;
		invalidateAll();
	}
}

void ChunkCacheManager::invalidateAll() {
	for (auto& [coord, chunk] : cached_chunks_) {
		chunk.is_dirty = true;
		chunk.pending_bake_retries = 0;
	}
}

void ChunkCacheManager::invalidateChunk(int32_t cx, int32_t cy, int32_t z) {
	auto it = cached_chunks_.find(ChunkCoord { cx, cy, z });
	if (it != cached_chunks_.end()) {
		it->second.is_dirty = true;
		it->second.pending_bake_retries = 0;
	}
}

CachedChunk& ChunkCacheManager::getOrCreateChunk(const ChunkCoord& coord) {
	auto it = cached_chunks_.find(coord);
	if (it == cached_chunks_.end()) {
		CachedChunk chunk;
		chunk.coord = coord;
		chunk.is_dirty = true;
		chunk.is_empty = false;
		auto [new_it, _] = cached_chunks_.emplace(coord, std::move(chunk));
		return new_it->second;
	}
	return it->second;
}

void ChunkCacheManager::uploadChunk(CachedChunk& chunk, const std::vector<TileInstance>& instances) {
	if (instances.empty()) {
		chunk.instance_count = 0;
		chunk.is_empty = true;
		return;
	}

	chunk.is_empty = false;
	chunk.instance_count = static_cast<uint32_t>(instances.size());

	if (chunk.vbo == 0) {
		glCreateBuffers(1, &chunk.vbo);
		chunk.vbo_capacity = 0;
	}

	const size_t required_bytes = instances.size() * sizeof(TileInstance);
	if (required_bytes > chunk.vbo_capacity) {
		glNamedBufferData(chunk.vbo, static_cast<GLsizeiptr>(required_bytes), instances.data(), GL_STATIC_DRAW);
		chunk.vbo_capacity = required_bytes;
	} else {
		glNamedBufferSubData(chunk.vbo, 0, static_cast<GLsizeiptr>(required_bytes), instances.data());
	}
}

void ChunkCacheManager::bakeChunk(CachedChunk& chunk, const Map& map, const RenderFrameContext& ctx) {
	bake_buffer_.clear();
	chunk.deferred_tiles.clear();
	chunk.used_sprites.clear();

	const DrawingOptions& options = ctx.options;
	const int32_t base_x = chunk.coord.cx * CHUNK_SIZE;
	const int32_t base_y = chunk.coord.cy * CHUNK_SIZE;
	const int32_t z = chunk.coord.z;

	const int32_t cell_x = chunk.coord.cx >> 2;
	const int32_t cell_y = chunk.coord.cy >> 2;
	const int32_t chunk_ix = chunk.coord.cx & 3;
	const int32_t chunk_iy = chunk.coord.cy & 3;

	const Floor* floors[4][4] = {};
	bool any_floor = false;

	// Single lookup for the containing 64x64 cell
	const auto& grid = map.getGrid();
	const uint64_t cell_key = SpatialHashGrid::makeKeyFromCell(cell_x, cell_y);
	const size_t cell_idx = grid.findCellIndex(cell_key);

	if (cell_idx < grid.cellCount()) {
		if (const auto* cell_ptr = grid.getCell(cell_idx)) {
			const auto& cell = *cell_ptr;
			for (int ny = 0; ny < 4; ++ny) {
				const int node_y = (chunk_iy << 2) + ny;
				const int row_base = node_y << 4; // * 16
				for (int nx = 0; nx < 4; ++nx) {
					const int node_x = (chunk_ix << 2) + nx;
					if (const MapNode* nd = cell.nodes[row_base + node_x].get()) {
						floors[nx][ny] = nd->getFloor(z);
						if (floors[nx][ny]) {
							any_floor = true;
						}
					}
				}
			}
		}
	}

	if (!any_floor) {
		chunk.is_empty = true;
		chunk.is_dirty = false;
		chunk.instance_count = 0;
		return;
	}

	// A sprite still streaming in has no atlas region yet. Baking it now would
	// freeze the hole into the buffer, so the chunk stays dirty and is re-baked
	// next frame, once the preloader has caught up.
	bool sprite_pending = false;

	// Emits the quads of one sprite the same way ItemDrawer::BlitItem does:
	// a single region for plain 1x1 sprites, otherwise one per column/row/layer
	// laid out 32px apart towards the north-west.
	auto emitSprite = [&](GameSprite* spr, const SpritePatterns& pat, int screen_x, int screen_y, uint8_t r, uint8_t g, uint8_t b, int alpha) {
		if (std::find(chunk.used_sprites.begin(), chunk.used_sprites.end(), spr) == chunk.used_sprites.end()) {
			chunk.used_sprites.push_back(spr);
		}

		const float rf = static_cast<float>(r) * INV_255;
		const float gf = static_cast<float>(g) * INV_255;
		const float bf = static_cast<float>(b) * INV_255;
		const float af = static_cast<float>(alpha) * INV_255;

		auto push = [&](const AtlasRegion* region, int px, int py) {
			if (!region || region->debug_sprite_id == AtlasRegion::INVALID_SENTINEL) {
				// Still streaming in, or its slot was just freed: try again next frame.
				sprite_pending = true;
				return;
			}
			if (region->debug_sprite_id >= SpriteAtlasLUT::MAX_SUPPORTED_SPRITES) {
				// Out of the LUT's range for good -- retrying would never help.
				return;
			}
			TileInstance inst;
			inst.x = static_cast<float>(px);
			inst.y = static_cast<float>(py);
			// Tamanho real: as folhas 12+/13 trazem sprites de 64px, e um quad
			// de 32 recortaria o sprite pela metade.
			inst.w = static_cast<float>(region->pixel_width);
			inst.h = static_cast<float>(region->pixel_height);
			inst.sprite_id = region->debug_sprite_id;
			inst.flags = 0;
			inst.r = rf;
			inst.g = gf;
			inst.b = bf;
			inst.a = af;
			bake_buffer_.push_back(inst);
		};

		if (spr->width == 1 && spr->height == 1 && spr->layers == 1) {
			push(spr->getAtlasRegion(0, 0, 0, pat.subtype, pat.x, pat.y, pat.z, pat.frame), screen_x, screen_y);
			return;
		}
		// As celulas de um sprite composto continuam 32px apartadas: o passo e
		// da grade do tile, nao do tamanho do recorte.
		for (int cx = 0; cx != spr->width; ++cx) {
			for (int cy = 0; cy != spr->height; ++cy) {
				for (int cf = 0; cf != spr->layers; ++cf) {
					push(spr->getAtlasRegion(cx, cy, cf, pat.subtype, pat.x, pat.y, pat.z, pat.frame), screen_x - cx * TILE_SIZE, screen_y - cy * TILE_SIZE);
				}
			}
		}
	};

	// Bakes the contents slice of one tile: every item that is not a ground
	// border, in stack order, with the same elevation stacking and the same
	// on-top deferral (top order 3) ItemDrawer applies.
	//
	// All or nothing. Splitting a tile's contents between the GPU and the CPU
	// would break the elevation chain and the stack order, so a single item
	// the bake cannot express sends the whole slice back to TileRenderer.
	auto bakeTileContents = [&](const Tile* tile, const TileLocation* loc, const Position& position, int x, int y, uint8_t r, uint8_t g, uint8_t b, auto&& defer) {
		const bool is_house_tile = tile->isHouseTile();

		// Per-frame or unbakeable tile-level business: a creature, the pulsing
		// highlight of the selected house, a zone overlay, any marker.
		if (tile->creature
			|| (options.show_invalid_zones && tile->hasInvalidZones())
			|| (options.show_houses && is_house_tile && tile->getHouseID() == options.current_house_id)
			|| loc->getSpawnCount() > 0 || loc->getWaypointCount() > 0
			|| loc->getTownCount() > 0 || loc->getHouseExits() != nullptr) {
			defer(CHUNK_DEFER_CONTENTS);
			return;
		}

		// The invalid-item overlay is drawn even past the LOD threshold, so it
		// is tested before the gate below.
		if (options.show_invalid_tiles) {
			for (const auto& item : tile->items) {
				if (item->isInvalidOTBMItem()) {
					defer(CHUNK_DEFER_CONTENTS);
					return;
				}
			}
			if (tile->ground && tile->ground->isInvalidOTBMItem()) {
				defer(CHUNK_DEFER_CONTENTS);
				return;
			}
		}

		// Past the LOD threshold the contents pass blits nothing else.
		if (!options.drawLooseItems()) {
			return;
		}

		// The house tint is stable; only its pulse is not, and a pulsing tile
		// was already deferred just above.
		uint8_t house_r = 255, house_g = 255, house_b = 255;
		const bool calculate_house_color = options.extended_house_shader && options.show_houses && is_house_tile;
		if (calculate_house_color) {
			TileColorCalculator::GetHouseColor(tile->getHouseID(), house_r, house_g, house_b);
		}

		// Same filters the CPU pass applies, in the same order.
		auto skipItem = [&](const Item* item) {
			const int top_order = item->isAlwaysOnBottom() ? item->getTopOrder() : 0;
			if (top_order == 1) {
				return true; // ground border, handled by the border pass
			}
			return options.show_only_grounds && !item->isBorder() && !item->isOptionalBorder();
		};

		bool has_content = false;
		for (const auto& item : tile->items) {
			if (skipItem(item.get())) {
				continue;
			}
			if (item->isInvalidOTBMItem()) {
				continue; // invisible here; show_invalid_tiles already deferred the tile
			}
			has_content = true;
			if (!itemIsBakeable(item.get(), item->getDefinition(), item->getSprite(), options)) {
				defer(CHUNK_DEFER_CONTENTS);
				return;
			}
		}

		if (!has_content) {
			return;
		}

		auto emitItem = [&](const Item* item, int screen_base_x, int screen_base_y, int elevation) {
			const ItemDefinitionView it = item->getDefinition();
			GameSprite* spr = item->getSprite();

			const SpritePatterns patterns = PatternCalculator::Calculate(spr, it, item, tile, position);
			if (!spr->isSimpleAndLoaded()) {
				rme::collectTileSprites(spr, patterns.x, patterns.y, patterns.z, patterns.frame);
			}

			uint8_t ir = 255, ig = 255, ib = 255;
			if (item->isBorder()) {
				ir = r;
				ig = g;
				ib = b;
			} else if (calculate_house_color) {
				ir = house_r;
				ig = house_g;
				ib = house_b;
			}

			const int alpha = applyTransparency(255, it, spr, options);
			const auto [item_off_x, item_off_y] = spr->getDrawOffset();
			emitSprite(spr, patterns, screen_base_x - elevation - item_off_x, screen_base_y - elevation - item_off_y, ir, ig, ib, alpha);
		};

		const int tile_screen_x = x * TILE_SIZE;
		const int tile_screen_y = y * TILE_SIZE;

		int elevation = 0;
		bool has_top_items = false;
		for (const auto& item : tile->items) {
			if (skipItem(item.get()) || item->isInvalidOTBMItem()) {
				continue;
			}
			// Top order 3 closes the tile, above the common items, and without
			// the elevation they accumulated -- Tile::drawTop draws at dest.
			if (item->isAlwaysOnBottom() && item->getTopOrder() == 3) {
				has_top_items = true;
				continue;
			}
			emitItem(item.get(), tile_screen_x, tile_screen_y, elevation);
			elevation += item->getSprite()->draw_height;
		}

		if (has_top_items) {
			for (const auto& item : tile->items) {
				if (skipItem(item.get()) || item->isInvalidOTBMItem()) {
					continue;
				}
				if (!item->isAlwaysOnBottom() || item->getTopOrder() != 3) {
					continue;
				}
				emitItem(item.get(), tile_screen_x, tile_screen_y, 0);
			}
		}
	};

	// One sweep per pass over the whole chunk, in the order the CPU renderer
	// uses: every ground, then every border, then the contents. That is what
	// keeps a ground from covering a neighbour's border.
	for (int pass = 0; pass < 3; ++pass) {
		const bool ground_pass = (pass == 0);
		const bool border_pass = (pass == 1);

		for (int tx = 0; tx < CHUNK_SIZE; ++tx) {
			for (int ty = 0; ty < CHUNK_SIZE; ++ty) {
				const Floor* fl = floors[tx >> 2][ty >> 2];
				if (!fl) {
					continue;
				}

				const TileLocation* loc = &fl->locs[(tx & 3) * 4 + (ty & 3)];
				const Tile* tile = loc->get();
				if (!tile) {
					continue;
				}

				const int x = base_x + tx;
				const int y = base_y + ty;
				const Position position(x, y, z);

				uint8_t r = 255, g = 255, b = 255;
				if (options.hasTileColorModifiers()) {
					TileColorCalculator::Calculate(tile, options, options.current_house_id, loc->getSpawnCount(), r, g, b);
				}

				auto defer = [&](uint8_t mask) {
					for (auto& dt : chunk.deferred_tiles) {
						if (dt.rel_x == tx && dt.rel_y == ty) {
							dt.pass_mask |= mask;
							return;
						}
					}
					chunk.deferred_tiles.push_back(DeferredTileInfo { static_cast<uint8_t>(tx), static_cast<uint8_t>(ty), mask });
				};

				// A ground bigger than one tile, or with a negative draw offset,
				// overhangs its neighbours. TileRenderer draws such a tile's
				// ground AND its borders in the contents pass, so the whole tile
				// goes back to the CPU there and nothing of it is baked.
				GameSprite* ground_sprite = tile->ground ? tile->ground->getSprite() : nullptr;
				const bool ground_overhangs = ground_sprite && ground_sprite->overhangsTile();
				if (ground_overhangs) {
					if (ground_pass) {
						defer(CHUNK_DEFER_CONTENTS);
					}
					continue;
				}

				if (ground_pass) {
					if (!tile->ground) {
						// A groundless tile still paints a zone square on the CPU,
						// and its borders have to follow it so they stay on top.
						if (options.always_show_zones && (r != 255 || g != 255 || b != 255)) {
							defer(CHUNK_DEFER_GROUND | CHUNK_DEFER_BORDERS);
						}
						continue;
					}

					const ItemDefinitionView ground_it = tile->ground->getDefinition();
					if (!itemIsBakeable(tile->ground.get(), ground_it, ground_sprite, options)) {
						// An animated or interactive ground has to keep its borders
						// on the CPU too, or they would be drawn underneath it.
						defer(CHUNK_DEFER_GROUND | CHUNK_DEFER_BORDERS);
						continue;
					}

					const SpritePatterns patterns = PatternCalculator::Calculate(ground_sprite, ground_it, tile->ground.get(), tile, position);
					if (!ground_sprite->isSimpleAndLoaded()) {
						rme::collectTileSprites(ground_sprite, patterns.x, patterns.y, patterns.z, patterns.frame);
					}

					int alpha = applyTransparency(255, ground_it, ground_sprite, options);
					if (!options.ingame && options.show_mountain_overlay && ground_it.isGroundTile()) {
						const GroundBrush* gb = tile->ground->getGroundBrush();
						if (gb && gb->getZ() >= MountainOverlayDrawer::Z_ORDER_THRESHOLD) {
							alpha = std::min(alpha, 80);
						}
					}

					const auto [ground_off_x, ground_off_y] = ground_sprite->getDrawOffset();
					emitSprite(ground_sprite, patterns, x * TILE_SIZE - ground_off_x, y * TILE_SIZE - ground_off_y, r, g, b, alpha);
					continue;
				}

				if (!border_pass) {
					// If the ground or the borders of this tile went to the CPU,
					// its contents have to follow: the CPU draws after the GPU,
					// and it would otherwise paint them over geometry that is
					// supposed to sit underneath.
					uint8_t already_deferred = 0;
					for (const auto& dt : chunk.deferred_tiles) {
						if (dt.rel_x == tx && dt.rel_y == ty) {
							already_deferred = dt.pass_mask;
							break;
						}
					}
					if (already_deferred & (CHUNK_DEFER_GROUND | CHUNK_DEFER_BORDERS)) {
						defer(CHUNK_DEFER_CONTENTS);
						continue;
					}

					bakeTileContents(tile, loc, position, x, y, r, g, b, defer);
					continue;
				}

				// Border pass: ground borders only (always-on-bottom, top order 1).
				// Borders are loose items, so the LOD gate hides them wholesale.
				if (!options.drawLooseItems()) {
					continue;
				}

				bool tile_already_deferred = false;
				for (const auto& dt : chunk.deferred_tiles) {
					if (dt.rel_x == tx && dt.rel_y == ty && (dt.pass_mask & CHUNK_DEFER_BORDERS)) {
						tile_already_deferred = true;
						break;
					}
				}
				if (tile_already_deferred) {
					continue;
				}

				// Two sweeps: the first one decides whether every border of this
				// tile can be baked, the second emits them. All or nothing keeps
				// their order and stacked elevation identical to the CPU path.
				bool all_bakeable = true;
				bool has_border = false;
				for (const auto& item : tile->items) {
					if (!item->isAlwaysOnBottom() || item->getTopOrder() != 1) {
						continue;
					}
					if (options.show_only_grounds && !item->isBorder() && !item->isOptionalBorder()) {
						continue;
					}
					if (item->isInvalidOTBMItem()) {
						if (options.show_invalid_tiles) {
							all_bakeable = false;
							break;
						}
						continue; // invisible, exactly as the CPU pass treats it
					}
					has_border = true;
					// A top-order-1 item that is not a border takes the house tint
					// and its pulse, which changes every frame.
					if (!item->isBorder()) {
						all_bakeable = false;
						break;
					}
					if (!itemIsBakeable(item.get(), item->getDefinition(), item->getSprite(), options)) {
						all_bakeable = false;
						break;
					}
				}

				if (!has_border) {
					continue;
				}
				if (!all_bakeable) {
					defer(CHUNK_DEFER_BORDERS);
					continue;
				}

				int elevation = 0;
				for (const auto& item : tile->items) {
					if (!item->isAlwaysOnBottom() || item->getTopOrder() != 1) {
						continue;
					}
					if (options.show_only_grounds && !item->isBorder() && !item->isOptionalBorder()) {
						continue;
					}
					if (item->isInvalidOTBMItem()) {
						continue;
					}

					const ItemDefinitionView it = item->getDefinition();
					GameSprite* spr = item->getSprite();

					const SpritePatterns patterns = PatternCalculator::Calculate(spr, it, item.get(), tile, position);
					if (!spr->isSimpleAndLoaded()) {
						rme::collectTileSprites(spr, patterns.x, patterns.y, patterns.z, patterns.frame);
					}

					const int alpha = applyTransparency(255, it, spr, options);
					const auto [border_off_x, border_off_y] = spr->getDrawOffset();
					const int screen_x = x * TILE_SIZE - elevation - border_off_x;
					const int screen_y = y * TILE_SIZE - elevation - border_off_y;

					emitSprite(spr, patterns, screen_x, screen_y, r, g, b, alpha);

					elevation += spr->draw_height;
				}
			}
		}
	}

	uploadChunk(chunk, bake_buffer_);

	if (sprite_pending && chunk.pending_bake_retries < MAX_PENDING_BAKE_RETRIES) {
		++chunk.pending_bake_retries;
		chunk.is_dirty = true;
	} else {
		chunk.pending_bake_retries = 0;
		chunk.is_dirty = false;
	}
}

void ChunkCacheManager::advanceFrame(int current_floor) {
	++current_frame_;
	if (current_frame_ % PRUNE_INTERVAL_FRAMES == 0) {
		prune(current_floor);
	}
}

void ChunkCacheManager::renderFloor(
	int map_z,
	const Map& map,
	const RenderFrameContext& ctx,
	const glm::mat4& projection,
	AtlasManager& atlas
) {
	active_visible_chunks_.clear();
	active_floor_ = map_z;

	if (!isValid()) {
		return;
	}

	const ViewBounds bounds = ctx.view.getBoundsForFloor(map_z);
	const int min_cx = bounds.start_x >> 4;
	const int max_cx = (bounds.end_x + 15) >> 4;
	const int min_cy = bounds.start_y >> 4;
	const int max_cy = (bounds.end_y + 15) >> 4;

	const int offset = (map_z <= GROUND_LAYER)
		? (GROUND_LAYER - map_z) * TILE_SIZE
		: TILE_SIZE * (ctx.view.floor - map_z);
	const glm::vec3 translation(
		static_cast<float>(-ctx.view.view_scroll_x - offset),
		static_cast<float>(-ctx.view.view_scroll_y - offset),
		0.0f
	);
	const glm::mat4 floor_mvp = projection * glm::translate(glm::mat4(1.0f), translation);

	shader_.Use();
	shader_.SetMat4("uMVP", floor_mvp);
	shader_.SetInt("uAtlas", 0);
	shader_.SetVec4("uGlobalTint", glm::vec4(1.0f));

	atlas.bind(0);
	atlas.bindLUT(SpriteAtlasLUT::SSBO_BINDING_INDEX);

	glBindVertexArray(vao_);

	const bool touch_sprites = (current_frame_ % SPRITE_TOUCH_INTERVAL_FRAMES) == 0;
	const int64_t now = static_cast<int64_t>(ctx.gfx.getCachedTime());

	// Sparse query: touches only populated chunks on map_z.
	map.visitPopulatedChunks(min_cx, min_cy, max_cx, max_cy, map_z, [&](int cx, int cy) {
		const ChunkCoord coord { cx, cy, map_z };
		CachedChunk& chunk = getOrCreateChunk(coord);
		if (chunk.is_dirty) {
			bakeChunk(chunk, map, ctx);
		}
		chunk.last_accessed_frame = current_frame_;

		// Keep the atlas LRU aware that these sprites are still on screen, even
		// though nothing looked up their atlas region this frame.
		if (touch_sprites) {
			for (GameSprite* spr : chunk.used_sprites) {
				spr->touchAtlasAccess(now);
			}
		}

		if (!chunk.is_empty && chunk.instance_count > 0 && chunk.vbo != 0) {
			glVertexArrayVertexBuffer(vao_, 1, chunk.vbo, 0, sizeof(TileInstance));
			glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(chunk.instance_count));
		}

		active_visible_chunks_.push_back(&chunk);
	});

	glBindVertexArray(0);
	shader_.Unuse();
}

void ChunkCacheManager::prune(
	int current_floor,
	int min_cx, int max_cx,
	int min_cy, int max_cy,
	bool has_bounds
) {
	size_t empty_evicted = 0;

	// Floors 0 through GROUND_LAYER are rendered together, so they are never
	// "far" from one another.
	const bool is_surface_view = (current_floor <= GROUND_LAYER);

	for (auto it = cached_chunks_.begin(); it != cached_chunks_.end();) {
		const auto& [coord, chunk] = *it;
		const uint64_t age = current_frame_ - chunk.last_accessed_frame;

		bool is_far_floor = false;
		if (is_surface_view) {
			is_far_floor = (coord.z > GROUND_LAYER + 2);
		} else {
			is_far_floor = (coord.z <= GROUND_LAYER) || (std::abs(coord.z - current_floor) > 2);
		}

		// Stale empty chunks on far floors (negative cache cleanup)
		if (chunk.is_empty && is_far_floor && age > FAR_FLOOR_FRAME_THRESHOLD) {
			it = cached_chunks_.erase(it);
			++empty_evicted;
			continue;
		}

		++it;
	}

	size_t lru_evicted = 0;
	if (cached_chunks_.size() > MAX_CACHED_CHUNKS) {
		const size_t needed = cached_chunks_.size() - TARGET_CACHED_CHUNKS;
		evictOldest(needed);
		lru_evicted = needed;
	}

	const size_t total_evicted = empty_evicted + lru_evicted;
	if (total_evicted > 0) {
		spdlog::info("[ChunkCache] Prune (frame {}): evicted {} chunk(s) ({} empty, {} LRU) | remaining {}/{}",
			current_frame_, total_evicted, empty_evicted, lru_evicted, cached_chunks_.size(), MAX_CACHED_CHUNKS);
	}
}

void ChunkCacheManager::evictOldest(size_t count_to_remove) {
	if (count_to_remove == 0 || cached_chunks_.empty()) {
		return;
	}

	std::vector<std::pair<uint64_t, ChunkCoord>> candidates;
	candidates.reserve(cached_chunks_.size());

	// Never evict chunks touched in the current frame.
	for (const auto& [coord, chunk] : cached_chunks_) {
		if (chunk.last_accessed_frame < current_frame_) {
			candidates.emplace_back(chunk.last_accessed_frame, coord);
		}
	}

	if (candidates.empty()) {
		return;
	}

	const size_t num_evict = std::min(count_to_remove, candidates.size());
	std::partial_sort(
		candidates.begin(),
		candidates.begin() + num_evict,
		candidates.end(),
		[](const auto& a, const auto& b) { return a.first < b.first; }
	);

	for (size_t i = 0; i < num_evict; ++i) {
		cached_chunks_.erase(candidates[i].second);
	}

	spdlog::warn("[ChunkCache] High-water mark exceeded (>{} chunks), LRU evicted {} | remaining {}",
		MAX_CACHED_CHUNKS, num_evict, cached_chunks_.size());
}
