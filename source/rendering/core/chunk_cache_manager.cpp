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
#include "rendering/utilities/render_profiler.h"
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
layout(location = 6) in uint aTileXY;
layout(location = 7) in uint aAnimSeq;

struct SpriteLUTEntry {
	vec4 uv_rect;
	float layer;
	float valid;
	vec2 _pad;
};

layout(std430, binding = 2) readonly buffer AtlasLUT {
	SpriteLUTEntry lutEntries[];
};

// Animacao: cada sequencia aponta para os sprite ids dos seus frames e para o
// slot do relogio (frame corrente do Animator daquele sprite, publicado pela CPU).
struct AnimSequence {
	uint first_frame;
	uint frame_count;
	uint clock_slot;
	uint _pad;
};

layout(std430, binding = 3) readonly buffer AnimSequences {
	AnimSequence animSequences[];
};

layout(std430, binding = 4) readonly buffer AnimFrames {
	uint animFrameSprites[];
};

layout(std430, binding = 5) readonly buffer AnimClock {
	uint animClock[];
};

uniform mat4 uMVP;
uniform vec4 uGlobalTint;
// Retangulo de selecao em arrasto (tiles, inclusivo) -- o mesmo teste que o
// BlitItem faz com transient_selection_bounds.
uniform int uSelectionActive;
uniform vec4 uSelectionRect;

out vec3 vTexCoord;
out vec4 vColor;

void main() {
	vec2 worldPos = aRect.xy + aPos * aRect.zw;
	gl_Position = uMVP * vec4(worldPos, 0.0, 1.0);

	uint spriteId = aSpriteId;
	if (aAnimSeq != 0u) {
		AnimSequence seq = animSequences[aAnimSeq - 1u];
		uint frame = animClock[seq.clock_slot] % max(seq.frame_count, 1u);
		spriteId = animFrameSprites[seq.first_frame + frame];
	}

	SpriteLUTEntry entry = lutEntries[spriteId];
	vec2 uv = mix(entry.uv_rect.xy, entry.uv_rect.zw, aTexCoord);
	vTexCoord = vec3(uv, entry.layer);

	vec4 tint = aTint;
	if (uSelectionActive != 0 && (aFlags & 256u) == 0u) {
		vec2 tile = vec2(float(aTileXY & 65535u), float(aTileXY >> 16u));
		if (tile.x >= uSelectionRect.x && tile.x <= uSelectionRect.z && tile.y >= uSelectionRect.y && tile.y <= uSelectionRect.w) {
			tint.rgb *= 0.5;
		}
	}
	vColor = tint * uGlobalTint * entry.valid;
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

	// Resposta de itemBakeRefusal() para "pode ir para o cache".
	constexpr ChunkDeferReason kBakeable = ChunkDeferReason::Count;

	static_assert(
		static_cast<size_t>(RenderProfiler::Counter::DeferOther) - static_cast<size_t>(RenderProfiler::Counter::DeferAnimated) + 1 == kChunkDeferReasonCount,
		"RenderProfiler::Counter::DeferAnimated..DeferOther tem de acompanhar ChunkDeferReason, na mesma ordem"
	);
	static_assert(TILE_INSTANCE_SELECTED == 256u, "CHUNK_VERT_SHADER testa o bit 256 de aFlags");

	// Can this item be frozen into a chunk buffer at all? Everything BlitItem
	// decides per frame -- indicators, selection tint, animation, technical
	// overlays -- disqualifies it, and its tile goes back to the CPU path.
	// Devolve o motivo da recusa, ou kBakeable.
	ChunkDeferReason itemBakeRefusal(const Item* item, const ItemDefinitionView& it, const GameSprite* spr, const DrawingOptions& options) {
		if (!spr || !it || it.isMetaItem()) {
			return ChunkDeferReason::Other;
		}
		if (item->isInvalidOTBMItem()) {
			return ChunkDeferReason::Other;
		}
		// Sprite animado vai para o cache como sequencia de frames (o shader troca
		// o frame), e item selecionado vai com o tint pela metade, exatamente o
		// que o BlitItem faz -- trocar a selecao marca o chunk sujo. Nenhum dos
		// dois e motivo de CPU; ChunkDeferReason::Animated/Selected ficam em zero.
		if (!options.show_items && it.hasFlag(ItemFlag::Pickupable)) {
			return ChunkDeferReason::Other;
		}
		if (it.isPodium() || item->isTeleport()) {
			return ChunkDeferReason::Other;
		}
		if (options.highlight_locked_doors && !options.ingame && it.isDoor()) {
			return ChunkDeferReason::Indicator;
		}
		if (options.show_hooks && !options.ingame && (it.hasFlag(ItemFlag::HookSouth) || it.hasFlag(ItemFlag::HookEast))) {
			return ChunkDeferReason::Indicator;
		}
		if (!options.ingame && ((options.show_pickupables && it.hasFlag(ItemFlag::Pickupable)) || (options.show_moveables && it.hasFlag(ItemFlag::Moveable)))) {
			return ChunkDeferReason::Indicator;
		}
		if (options.show_light_str && !options.ingame && spr->hasLight() && spr->getLight().intensity > 0) {
			return ChunkDeferReason::Light;
		}
		if (options.show_tech_items && !options.ingame) {
			const ClientItemId cid = it.clientId();
			if (isTechnicalSpecialCase(cid) || LightSourceManager::instance().isLightSource(cid)) {
				return ChunkDeferReason::Technical;
			}
		}
		return kBakeable;
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

	// Loc 6: aTileXY (uint: tile x | tile y << 16)
	glEnableVertexArrayAttrib(vao_, 6);
	glVertexArrayAttribIFormat(vao_, 6, 1, GL_UNSIGNED_INT, offsetof(TileInstance, tile_xy));
	glVertexArrayAttribBinding(vao_, 6, 1);

	// Loc 7: aAnimSeq (uint: 0 = estatico, senao indice+1 da sequencia)
	glEnableVertexArrayAttrib(vao_, 7);
	glVertexArrayAttribIFormat(vao_, 7, 1, GL_UNSIGNED_INT, offsetof(TileInstance, anim_seq));
	glVertexArrayAttribBinding(vao_, 7, 1);

	// Os tres buffers da animacao existem desde ja, mesmo vazios: o shader so os
	// le quando aAnimSeq != 0, mas um binding sem buffer nenhum e indefinido.
	glCreateBuffers(1, &anim_sequences_ssbo_);
	glCreateBuffers(1, &anim_frames_ssbo_);
	glCreateBuffers(1, &anim_clock_ssbo_);
	resetAnimationTables();

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
	for (GLuint* buffer : { &anim_sequences_ssbo_, &anim_frames_ssbo_, &anim_clock_ssbo_ }) {
		if (*buffer != 0) {
			glDeleteBuffers(1, buffer);
			*buffer = 0;
		}
	}
	anim_sequences_.clear();
	anim_frames_.clear();
	anim_clock_.clear();
	anim_clock_ids_.clear();
	anim_sequence_of_.clear();
	anim_clock_slot_of_.clear();
	anim_sequences_on_gpu_ = 0;
	anim_frames_on_gpu_ = 0;
	anim_sequences_capacity_ = 0;
	anim_frames_capacity_ = 0;
	anim_clock_capacity_ = 0;
	anim_clock_dirty_ = false;
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
		RenderProfiler::Count(RenderProfiler::Counter::InvalidateTracker);
		invalidateAll();
		change_tracker.clearDirty();
		return;
	}

	auto dirty_chunks = change_tracker.takeDirtyChunks();
	RenderProfiler::Count(RenderProfiler::Counter::DirtyChunks, static_cast<int64_t>(dirty_chunks.size()));
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
		RenderProfiler::Count(RenderProfiler::Counter::InvalidateOptions);
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
		resetAnimationTables();
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
		RenderProfiler::Count(RenderProfiler::Counter::InvalidateAtlas);
		invalidateAll();
		// As sequencias guardam sprite ids, e um AtlasManager novo no MESMO
		// endereco (graficos recarregados) so aparece aqui, pela geracao. Todo
		// chunk vai ser re-assado antes de ser desenhado, entao remontar as
		// tabelas junto nao custa nada alem do proprio bake.
		resetAnimationTables();
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
	RENDER_PROFILE_SCOPE(ChunkBake);
	RenderProfiler::Count(RenderProfiler::Counter::ChunksBaked);

	bake_buffer_.clear();
	chunk.deferred_tiles.clear();
	chunk.used_sprites.clear();
	chunk.defer_reasons.fill(0);
	chunk.contents_offset = 0;

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
	//
	// tile_x/tile_y: o tile dono do item, que o shader compara com o retangulo de
	// selecao em arrasto. selected: item selecionado -- tint pela metade, como o
	// BlitItem faz. Sprite animado sai com a sequencia de frames da celula
	// (anim_seq), e o shader troca o frame.
	auto emitSprite = [&](GameSprite* spr, const SpritePatterns& pat, int screen_x, int screen_y, uint8_t r, uint8_t g, uint8_t b, int alpha, int tile_x, int tile_y, bool selected) {
		if (std::find(chunk.used_sprites.begin(), chunk.used_sprites.end(), spr) == chunk.used_sprites.end()) {
			chunk.used_sprites.push_back(spr);
		}

		if (selected) {
			r = static_cast<uint8_t>(r >> 1);
			g = static_cast<uint8_t>(g >> 1);
			b = static_cast<uint8_t>(b >> 1);
		}

		const float rf = static_cast<float>(r) * INV_255;
		const float gf = static_cast<float>(g) * INV_255;
		const float bf = static_cast<float>(b) * INV_255;
		const float af = static_cast<float>(alpha) * INV_255;
		const uint32_t tile_xy = (static_cast<uint32_t>(tile_x) & 0xFFFFu) | (static_cast<uint32_t>(tile_y) << 16);
		const uint32_t flags = selected ? TILE_INSTANCE_SELECTED : 0u;
		const bool animated = spr->isAnimated();

		auto push = [&](int cx, int cy, int cf, int px, int py) {
			const AtlasRegion* region = spr->getAtlasRegion(cx, cy, cf, pat.subtype, pat.x, pat.y, pat.z, pat.frame);
			if (!region || region->debug_sprite_id == AtlasRegion::INVALID_SENTINEL) {
				// Still streaming in, or its slot was just freed: try again next frame.
				sprite_pending = true;
				return;
			}
			if (region->debug_sprite_id >= SpriteAtlasLUT::MAX_SUPPORTED_SPRITES) {
				// Out of the LUT's range for good -- retrying would never help.
				return;
			}
			uint32_t anim_seq = 0;
			if (animated) {
				anim_seq = animSequenceFor(spr, cx, cy, cf, pat, sprite_pending);
				if (anim_seq == 0) {
					return; // algum frame ainda sem regiao: sprite_pending ja diz para tentar de novo
				}
			}
			TileInstance inst;
			inst.x = static_cast<float>(px);
			inst.y = static_cast<float>(py);
			// Tamanho real: as folhas 12+/13 trazem sprites de 64px, e um quad
			// de 32 recortaria o sprite pela metade.
			inst.w = static_cast<float>(region->pixel_width);
			inst.h = static_cast<float>(region->pixel_height);
			inst.sprite_id = region->debug_sprite_id;
			inst.flags = flags;
			inst.r = rf;
			inst.g = gf;
			inst.b = bf;
			inst.a = af;
			inst.tile_xy = tile_xy;
			inst.anim_seq = anim_seq;
			bake_buffer_.push_back(inst);
		};

		if (spr->width == 1 && spr->height == 1 && spr->layers == 1) {
			push(0, 0, 0, screen_x, screen_y);
			return;
		}
		// As celulas de um sprite composto continuam 32px apartadas: o passo e
		// da grade do tile, nao do tamanho do recorte.
		for (int cx = 0; cx != spr->width; ++cx) {
			for (int cy = 0; cy != spr->height; ++cy) {
				for (int cf = 0; cf != spr->layers; ++cf) {
					push(cx, cy, cf, screen_x - cx * TILE_SIZE, screen_y - cy * TILE_SIZE);
				}
			}
		}
	};

	// O BlitItem so escurece a selecao fora do modo ingame.
	const bool show_selection = !options.ingame;

	// Negocio por tile que muda a cada frame ou que o bake nao expressa: uma
	// criatura, o pulso da casa selecionada, uma zona invalida, um marcador.
	//
	// Spawn so pelo CENTRO (tile->spawn), que e onde o MarkerDrawer desenha a
	// chama. O raio do spawn nao muda nada no desenho do tile -- o Calculate nem
	// usa o spawn_count --, e mandar o raio inteiro para a CPU enchia a passada de
	// conteudo em area de hunt. Criatura so conta se for desenhada.
	auto tileNeedsCpu = [&](const Tile* tile, const TileLocation* loc) {
		return (tile->creature && options.show_creatures)
			|| (options.show_invalid_zones && tile->hasInvalidZones())
			|| (options.show_houses && tile->isHouseTile() && tile->getHouseID() == options.current_house_id)
			|| tile->spawn || loc->getWaypointCount() > 0
			|| loc->getTownCount() > 0 || loc->getHouseExits() != nullptr;
	};

	// The invalid-item overlay is drawn even past the LOD threshold, so it is
	// tested before the gate.
	auto tileHasInvalidOverlay = [&](const Tile* tile) {
		if (!options.show_invalid_tiles) {
			return false;
		}
		if (tile->ground && tile->ground->isInvalidOTBMItem()) {
			return true;
		}
		for (const auto& item : tile->items) {
			if (item->isInvalidOTBMItem()) {
				return true;
			}
		}
		return false;
	};

	// Same filters the CPU pass applies, in the same order. with_borders: the
	// ground borders (top order 1) belong to this slice too -- the overhanging
	// ground case, which TileRenderer draws whole in the contents pass.
	auto skipItem = [&](const Item* item, bool with_borders) {
		const int top_order = item->isAlwaysOnBottom() ? item->getTopOrder() : 0;
		if (top_order == 1 && !with_borders) {
			return true; // ground border, handled by the border pass
		}
		return options.show_only_grounds && !item->isBorder() && !item->isOptionalBorder();
	};

	// First refusal among the items the slice shows, or kBakeable. Invalid items
	// are invisible here: with show_invalid_tiles the tile was already deferred.
	auto itemsRefusal = [&](const Tile* tile, bool with_borders) {
		for (const auto& item : tile->items) {
			if (skipItem(item.get(), with_borders) || item->isInvalidOTBMItem()) {
				continue;
			}
			const ChunkDeferReason refusal = itemBakeRefusal(item.get(), item->getDefinition(), item->getSprite(), options);
			if (refusal != kBakeable) {
				return refusal;
			}
		}
		return kBakeable;
	};

	// The alpha BlitItem gives a ground: the two transparency options and the
	// mountain overlay ghost.
	auto groundAlpha = [&](const Tile* tile, const ItemDefinitionView& ground_it, const GameSprite* ground_sprite) {
		int alpha = applyTransparency(255, ground_it, ground_sprite, options);
		if (!options.ingame && options.show_mountain_overlay && ground_it.isGroundTile()) {
			const GroundBrush* gb = tile->ground->getGroundBrush();
			if (gb && gb->getZ() >= MountainOverlayDrawer::Z_ORDER_THRESHOLD) {
				alpha = std::min(alpha, 80);
			}
		}
		return alpha;
	};

	// Emits the items of a tile the way TileRenderer::DrawTile does: stack order
	// with the elevation stacking ItemDrawer applies, then the on-top items (top
	// order 3) last and without elevation. The caller already made sure every
	// one of them is bakeable.
	auto emitTileItems = [&](const Tile* tile, const Position& position, int x, int y, uint8_t r, uint8_t g, uint8_t b, bool with_borders, int elevation) {
		// The house tint is stable; only its pulse is not, and a pulsing tile
		// never gets here (tileNeedsCpu).
		uint8_t house_r = 255, house_g = 255, house_b = 255;
		const bool calculate_house_color = options.extended_house_shader && options.show_houses && tile->isHouseTile();
		if (calculate_house_color) {
			TileColorCalculator::GetHouseColor(tile->getHouseID(), house_r, house_g, house_b);
		}

		auto emitItem = [&](const Item* item, int item_elevation) {
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
			emitSprite(spr, patterns, x * TILE_SIZE - item_elevation - item_off_x, y * TILE_SIZE - item_elevation - item_off_y, ir, ig, ib, alpha, x, y, show_selection && item->isSelected());
		};

		bool has_top_items = false;
		for (const auto& item : tile->items) {
			if (skipItem(item.get(), with_borders) || item->isInvalidOTBMItem()) {
				continue;
			}
			// Top order 3 closes the tile, above the common items, and without
			// the elevation they accumulated -- Tile::drawTop draws at dest.
			if (item->isAlwaysOnBottom() && item->getTopOrder() == 3) {
				has_top_items = true;
				continue;
			}
			emitItem(item.get(), elevation);
			elevation += item->getSprite()->draw_height;
		}

		if (has_top_items) {
			for (const auto& item : tile->items) {
				if (skipItem(item.get(), with_borders) || item->isInvalidOTBMItem()) {
					continue;
				}
				if (!item->isAlwaysOnBottom() || item->getTopOrder() != 3) {
					continue;
				}
				emitItem(item.get(), 0);
			}
		}
	};

	// Bakes the contents slice of one tile: every item that is not a ground
	// border, in stack order.
	//
	// All or nothing. Splitting a tile's contents between the GPU and the CPU
	// would break the elevation chain and the stack order, so a single item
	// the bake cannot express sends the whole slice back to TileRenderer.
	auto bakeTileContents = [&](const Tile* tile, const TileLocation* loc, const Position& position, int x, int y, uint8_t r, uint8_t g, uint8_t b, auto&& defer) {
		if (tileNeedsCpu(tile, loc)) {
			defer(CHUNK_DEFER_CONTENTS, ChunkDeferReason::Marker);
			return;
		}
		if (tileHasInvalidOverlay(tile)) {
			defer(CHUNK_DEFER_CONTENTS, ChunkDeferReason::Other);
			return;
		}
		// Past the LOD threshold the contents pass blits nothing else.
		if (!options.drawLooseItems()) {
			return;
		}
		if (const ChunkDeferReason refusal = itemsRefusal(tile, false); refusal != kBakeable) {
			defer(CHUNK_DEFER_CONTENTS, refusal);
			return;
		}
		emitTileItems(tile, position, x, y, r, g, b, false, 0);
	};

	// Um chao maior que o tile (montanha 64x64) ou com deslocamento negativo
	// transborda para os vizinhos. O TileRenderer desenha chao, bordas e itens
	// desse tile juntos, na passada de conteudo, para que ele cubra o que ja foi
	// desenhado a noroeste -- e o bake faz o mesmo, na faixa de conteudo do chunk.
	// A ordem se mantem: dentro do chunk o conteudo sai coluna a coluna (x, depois
	// y) e os chunks sao visitados linha a linha, entao tudo que fica a noroeste
	// ja foi emitido antes. Antes disto o tile inteiro ia para a CPU -- num mapa de
	// montanha, praticamente o andar todo, a cada frame.
	auto bakeOverhangTile = [&](const Tile* tile, const TileLocation* loc, const Position& position, int x, int y, uint8_t r, uint8_t g, uint8_t b, GameSprite* ground_sprite, auto&& defer) {
		const ItemDefinitionView ground_it = tile->ground->getDefinition();
		if (const ChunkDeferReason refusal = itemBakeRefusal(tile->ground.get(), ground_it, ground_sprite, options); refusal != kBakeable) {
			defer(CHUNK_DEFER_CONTENTS, refusal);
			return;
		}
		if (tileNeedsCpu(tile, loc)) {
			defer(CHUNK_DEFER_CONTENTS, ChunkDeferReason::Marker);
			return;
		}
		if (tileHasInvalidOverlay(tile)) {
			defer(CHUNK_DEFER_CONTENTS, ChunkDeferReason::Other);
			return;
		}
		// Borders are loose items: past the LOD threshold only the ground shows.
		const bool loose_items = options.drawLooseItems();
		if (loose_items) {
			if (const ChunkDeferReason refusal = itemsRefusal(tile, true); refusal != kBakeable) {
				defer(CHUNK_DEFER_CONTENTS, refusal);
				return;
			}
		}

		const SpritePatterns patterns = PatternCalculator::Calculate(ground_sprite, ground_it, tile->ground.get(), tile, position);
		if (!ground_sprite->isSimpleAndLoaded()) {
			rme::collectTileSprites(ground_sprite, patterns.x, patterns.y, patterns.z, patterns.frame);
		}
		const auto [ground_off_x, ground_off_y] = ground_sprite->getDrawOffset();
		emitSprite(ground_sprite, patterns, x * TILE_SIZE - ground_off_x, y * TILE_SIZE - ground_off_y, r, g, b, groundAlpha(tile, ground_it, ground_sprite), x, y, show_selection && tile->ground->isSelected());

		if (loose_items) {
			// No TileRenderer chao, bordas e itens deste tile dividem o mesmo
			// draw_x/draw_y, entao a elevacao continua a partir da do chao.
			emitTileItems(tile, position, x, y, r, g, b, true, ground_sprite->draw_height);
		}
	};

	// Posicao de cada tile em chunk.deferred_tiles, mais um (0 = nao deferido).
	// Troca a busca linear que o defer() fazia a cada chamada.
	std::array<uint16_t, CHUNK_SIZE * CHUNK_SIZE> deferred_slot {};
	size_t contents_start = 0;

	// One sweep per pass over the whole chunk, in the order the CPU renderer
	// uses: every ground, then every border, then the contents. That is what
	// keeps a ground from covering a neighbour's border.
	for (int pass = 0; pass < 3; ++pass) {
		const bool ground_pass = (pass == 0);
		const bool contents_pass = (pass == 2);

		// Daqui em diante e a faixa de conteudo (renderFloorContents).
		if (contents_pass) {
			contents_start = bake_buffer_.size();
		}

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

				uint16_t& slot = deferred_slot[tx * CHUNK_SIZE + ty];
				auto defer = [&](uint8_t mask, ChunkDeferReason reason) {
					++chunk.defer_reasons[static_cast<size_t>(reason)];
					if (slot != 0) {
						chunk.deferred_tiles[slot - 1].pass_mask |= mask;
						return;
					}
					chunk.deferred_tiles.push_back(DeferredTileInfo { static_cast<uint8_t>(tx), static_cast<uint8_t>(ty), mask });
					slot = static_cast<uint16_t>(chunk.deferred_tiles.size());
				};

				// Same test as TileRenderer: a hidden invalid ground is not drawn,
				// so it overhangs nothing.
				GameSprite* ground_sprite = tile->ground ? tile->ground->getSprite() : nullptr;
				const bool hidden_invalid_ground = tile->ground && tile->ground->isInvalidOTBMItem() && !options.show_invalid_tiles;
				const bool ground_overhangs = !hidden_invalid_ground && ground_sprite && ground_sprite->overhangsTile();
				if (ground_overhangs) {
					if (contents_pass) {
						bakeOverhangTile(tile, loc, position, x, y, r, g, b, ground_sprite, defer);
					}
					continue;
				}

				if (ground_pass) {
					if (!tile->ground) {
						// A groundless tile still paints a zone square on the CPU,
						// and its borders have to follow it so they stay on top.
						if (options.always_show_zones && (r != 255 || g != 255 || b != 255)) {
							defer(CHUNK_DEFER_GROUND | CHUNK_DEFER_BORDERS, ChunkDeferReason::Other);
						}
						continue;
					}

					// O chao (e as bordas) da casa selecionada pulsam: o tint vem do
					// TileColorCalculator com o highlight_pulse do frame.
					if (options.show_houses && tile->isHouseTile() && tile->getHouseID() == options.current_house_id) {
						defer(CHUNK_DEFER_GROUND | CHUNK_DEFER_BORDERS, ChunkDeferReason::Marker);
						continue;
					}

					const ItemDefinitionView ground_it = tile->ground->getDefinition();
					if (const ChunkDeferReason refusal = itemBakeRefusal(tile->ground.get(), ground_it, ground_sprite, options); refusal != kBakeable) {
						// An animated or interactive ground has to keep its borders
						// on the CPU too, or they would be drawn underneath it.
						defer(CHUNK_DEFER_GROUND | CHUNK_DEFER_BORDERS, refusal);
						continue;
					}

					const SpritePatterns patterns = PatternCalculator::Calculate(ground_sprite, ground_it, tile->ground.get(), tile, position);
					if (!ground_sprite->isSimpleAndLoaded()) {
						rme::collectTileSprites(ground_sprite, patterns.x, patterns.y, patterns.z, patterns.frame);
					}

					const auto [ground_off_x, ground_off_y] = ground_sprite->getDrawOffset();
					emitSprite(ground_sprite, patterns, x * TILE_SIZE - ground_off_x, y * TILE_SIZE - ground_off_y, r, g, b, groundAlpha(tile, ground_it, ground_sprite), x, y, show_selection && tile->ground->isSelected());
					continue;
				}

				if (contents_pass) {
					// Um chao ou uma borda deferidos nao arrastam mais o conteudo do
					// tile para a CPU: a faixa de conteudo e desenhada DEPOIS das
					// passadas de CPU de chao e borda, entao ja fica por cima deles.
					bakeTileContents(tile, loc, position, x, y, r, g, b, defer);
					continue;
				}

				// Border pass: ground borders only (always-on-bottom, top order 1).
				// Borders are loose items, so the LOD gate hides them wholesale.
				if (!options.drawLooseItems()) {
					continue;
				}

				if (slot != 0 && (chunk.deferred_tiles[slot - 1].pass_mask & CHUNK_DEFER_BORDERS)) {
					continue;
				}

				// Two sweeps: the first one decides whether every border of this
				// tile can be baked, the second emits them. All or nothing keeps
				// their order and stacked elevation identical to the CPU path.
				ChunkDeferReason border_refusal = kBakeable;
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
							// Com a opcao ligada a CPU desenha a borda invalida.
							border_refusal = ChunkDeferReason::Other;
							break;
						}
						continue; // invisible, exactly as the CPU pass treats it
					}
					has_border = true;
					// A top-order-1 item that is not a border takes the house tint
					// and its pulse, which changes every frame.
					if (!item->isBorder()) {
						border_refusal = ChunkDeferReason::Other;
						break;
					}
					border_refusal = itemBakeRefusal(item.get(), item->getDefinition(), item->getSprite(), options);
					if (border_refusal != kBakeable) {
						break;
					}
				}

				if (border_refusal != kBakeable) {
					defer(CHUNK_DEFER_BORDERS, border_refusal);
					continue;
				}
				if (!has_border) {
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

					emitSprite(spr, patterns, screen_x, screen_y, r, g, b, alpha, x, y, show_selection && item->isSelected());

					elevation += spr->draw_height;
				}
			}
		}
	}

	// A passada de CPU de conteudo segue a ordem desta lista, e um tile deferido
	// ja na passada de chao entrava nela antes de vizinhos a noroeste que so foram
	// deferidos no conteudo. Coluna a coluna, como o proprio bake emite.
	std::sort(chunk.deferred_tiles.begin(), chunk.deferred_tiles.end(), [](const DeferredTileInfo& a, const DeferredTileInfo& b) {
		return a.rel_x != b.rel_x ? a.rel_x < b.rel_x : a.rel_y < b.rel_y;
	});

	uploadChunk(chunk, bake_buffer_);
	chunk.contents_offset = static_cast<uint32_t>(contents_start);

	if (sprite_pending && chunk.pending_bake_retries < MAX_PENDING_BAKE_RETRIES) {
		RenderProfiler::Count(RenderProfiler::Counter::ChunkBakesPending);
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

	// A mesma margem que o caminho de CPU usa (MapLayerDrawer alarga a consulta
	// em PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS): um sprite grande de um tile
	// logo fora da tela ainda pinta dentro dela, e sem a margem o chunk dele
	// nao seria visitado -- a borda da viewport perderia justamente montanhas
	// e paredes, que sao os sprites que mais transbordam.
	const ViewBounds bounds = ctx.view.getBoundsForFloor(map_z, PAINTERS_ALGORITHM_SAFETY_MARGIN_PIXELS / TILE_SIZE);
	const int min_cx = bounds.start_x >> 4;
	const int max_cx = (bounds.end_x + 15) >> 4;
	const int min_cy = bounds.start_y >> 4;
	const int max_cy = (bounds.end_y + 15) >> 4;

	bindForFloor(map_z, ctx, projection, atlas);

	const bool profiling = RenderProfiler::IsEnabled();
	const bool touch_sprites = (current_frame_ % SPRITE_TOUCH_INTERVAL_FRAMES) == 0;
	const int64_t now = static_cast<int64_t>(ctx.gfx.getCachedTime());

	// Sparse query: touches only populated chunks on map_z.
	map.visitPopulatedChunks(min_cx, min_cy, max_cx, max_cy, map_z, [&](int cx, int cy) {
		const ChunkCoord coord { cx, cy, map_z };
		CachedChunk& chunk = getOrCreateChunk(coord);
		if (chunk.is_dirty) {
			bakeChunk(chunk, map, ctx);
			// Sequencias de animacao criadas por este bake tem de estar na GPU
			// antes do draw logo abaixo.
			flushAnimationTables();
		}
		chunk.last_accessed_frame = current_frame_;

		// Keep the atlas LRU aware that these sprites are still on screen, even
		// though nothing looked up their atlas region this frame.
		if (touch_sprites) {
			for (GameSprite* spr : chunk.used_sprites) {
				spr->touchAtlasAccess(now);
			}
		}

		if (profiling) {
			RenderProfiler::Count(RenderProfiler::Counter::ChunksVisited);
			for (size_t reason = 0; reason < kChunkDeferReasonCount; ++reason) {
				RenderProfiler::Count(static_cast<RenderProfiler::Counter>(static_cast<size_t>(RenderProfiler::Counter::DeferAnimated) + reason), chunk.defer_reasons[reason]);
			}
		}

		// So a faixa de chao + bordas. O conteudo sai em renderFloorContents(),
		// depois das passadas de CPU de chao e borda deste andar.
		if (!chunk.is_empty && chunk.contents_offset > 0 && chunk.vbo != 0) {
			RenderProfiler::Count(RenderProfiler::Counter::ChunkInstances, chunk.contents_offset);
			glVertexArrayVertexBuffer(vao_, 1, chunk.vbo, 0, sizeof(TileInstance));
			glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(chunk.contents_offset));
		}

		active_visible_chunks_.push_back(&chunk);
	});

	glBindVertexArray(0);
	shader_.Unuse();
}

void ChunkCacheManager::renderFloorContents(
	const RenderFrameContext& ctx,
	const glm::mat4& projection,
	AtlasManager& atlas
) {
	if (!isValid() || active_floor_ < 0 || active_visible_chunks_.empty()) {
		return;
	}

	bindForFloor(active_floor_, ctx, projection, atlas);

	// Mesma ordem de visita do renderFloor(): linha a linha de chunks, o que mantem
	// o que transborda para noroeste por cima dos vizinhos ja desenhados.
	for (const CachedChunk* chunk : active_visible_chunks_) {
		if (!chunk || chunk->is_empty || chunk->vbo == 0 || chunk->instance_count <= chunk->contents_offset) {
			continue;
		}
		const uint32_t count = chunk->instance_count - chunk->contents_offset;
		RenderProfiler::Count(RenderProfiler::Counter::ChunkInstances, count);
		const GLintptr first_byte = static_cast<GLintptr>(chunk->contents_offset) * static_cast<GLintptr>(sizeof(TileInstance));
		glVertexArrayVertexBuffer(vao_, 1, chunk->vbo, first_byte, sizeof(TileInstance));
		glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(count));
	}

	glBindVertexArray(0);
	shader_.Unuse();
}

void ChunkCacheManager::bindForFloor(int map_z, const RenderFrameContext& ctx, const glm::mat4& projection, AtlasManager& atlas) {
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

	// Retangulo de selecao em arrasto: o shader escurece o que cai nele, como o
	// BlitItem. Antes o cache era DESLIGADO durante o arrasto, e o mapa inteiro
	// voltava para as tres passadas de CPU enquanto o botao estivesse apertado.
	const std::optional<MapBounds>& selection = ctx.options.transient_selection_bounds;
	const bool selection_active = selection.has_value() && !ctx.options.ingame;
	shader_.SetInt("uSelectionActive", selection_active ? 1 : 0);
	if (selection_active) {
		shader_.SetVec4("uSelectionRect", glm::vec4(
			static_cast<float>(selection->x1), static_cast<float>(selection->y1),
			static_cast<float>(selection->x2), static_cast<float>(selection->y2)
		));
	}

	atlas.bind(0);
	atlas.bindLUT(SpriteAtlasLUT::SSBO_BINDING_INDEX);
	bindAnimationTables();

	glBindVertexArray(vao_);
}

size_t ChunkCacheManager::AnimSequenceKeyHash::operator()(const AnimSequenceKey& key) const noexcept {
	uint64_t hash = 0xCBF29CE484222325ull ^ key.client_id;
	for (const int16_t value : { key.cell_x, key.cell_y, key.layer, key.subtype, key.pattern_x, key.pattern_y, key.pattern_z }) {
		hash = (hash ^ static_cast<uint16_t>(value)) * 0x100000001B3ull;
	}
	return static_cast<size_t>(hash ^ (hash >> 32));
}

uint32_t ChunkCacheManager::animClockSlotFor(const GameSprite* spr) {
	const auto [it, inserted] = anim_clock_slot_of_.try_emplace(spr->getId(), static_cast<uint32_t>(anim_clock_ids_.size()));
	if (inserted) {
		anim_clock_ids_.push_back(spr->getId());
		// O frame de agora, nao zero: o chunk recem-assado e desenhado ainda neste
		// quadro, antes do proximo updateAnimationClock().
		anim_clock_.push_back(spr->animator ? static_cast<uint32_t>(std::max(0, spr->animator->getFrame())) : 0u);
		anim_clock_dirty_ = true;
	}
	return it->second;
}

uint32_t ChunkCacheManager::animSequenceFor(GameSprite* spr, int cell_x, int cell_y, int layer, const SpritePatterns& patterns, bool& pending) {
	const AnimSequenceKey key {
		.client_id = spr->getId(),
		.cell_x = static_cast<int16_t>(cell_x),
		.cell_y = static_cast<int16_t>(cell_y),
		.layer = static_cast<int16_t>(layer),
		.subtype = static_cast<int16_t>(patterns.subtype),
		.pattern_x = static_cast<int16_t>(patterns.x),
		.pattern_y = static_cast<int16_t>(patterns.y),
		.pattern_z = static_cast<int16_t>(patterns.z),
	};

	// Sequencia conhecida: os frames dela entraram no atlas quando ela foi criada,
	// e continuam la -- o chunk renova o LRU de todas as imagens do sprite
	// (touchAtlasAccess), e qualquer eviction zera estas tabelas antes do re-bake.
	if (const auto it = anim_sequence_of_.find(key); it != anim_sequence_of_.end()) {
		return it->second + 1;
	}

	// Todos os frames precisam de regiao no atlas: o shader pode pedir qualquer um.
	const int frame_count = std::max(1, static_cast<int>(spr->frames));
	std::vector<uint32_t> frame_sprites;
	frame_sprites.reserve(static_cast<size_t>(frame_count));
	for (int frame = 0; frame < frame_count; ++frame) {
		const AtlasRegion* region = spr->getAtlasRegion(cell_x, cell_y, layer, patterns.subtype, patterns.x, patterns.y, patterns.z, frame);
		if (!region || region->debug_sprite_id == AtlasRegion::INVALID_SENTINEL) {
			pending = true;
			return 0;
		}
		if (region->debug_sprite_id >= SpriteAtlasLUT::MAX_SUPPORTED_SPRITES) {
			return 0;
		}
		frame_sprites.push_back(region->debug_sprite_id);
	}

	AnimSequenceGpu sequence;
	sequence.first_frame = static_cast<uint32_t>(anim_frames_.size());
	sequence.frame_count = static_cast<uint32_t>(frame_sprites.size());
	sequence.clock_slot = animClockSlotFor(spr);
	anim_frames_.insert(anim_frames_.end(), frame_sprites.begin(), frame_sprites.end());

	const uint32_t index = static_cast<uint32_t>(anim_sequences_.size());
	anim_sequences_.push_back(sequence);
	anim_sequence_of_.emplace(key, index);
	return index + 1;
}

void ChunkCacheManager::updateAnimationClock(GraphicManager& gfx) {
	for (size_t slot = 0; slot < anim_clock_ids_.size(); ++slot) {
		// Pelo id a cada frame, e nao por ponteiro guardado: o sprite pode ter
		// sido destruido por um recarregamento dos graficos.
		GameSprite* spr = gfx.getGameSprite(static_cast<int>(anim_clock_ids_[slot]));
		const uint32_t frame = (spr && spr->animator) ? static_cast<uint32_t>(std::max(0, spr->animator->getFrame())) : 0u;
		if (anim_clock_[slot] != frame) {
			anim_clock_[slot] = frame;
			anim_clock_dirty_ = true;
		}
	}
	if (anim_clock_dirty_) {
		flushAnimationTables();
	}
}

void ChunkCacheManager::flushAnimationTables() {
	if (anim_sequences_ssbo_ == 0 || anim_frames_ssbo_ == 0 || anim_clock_ssbo_ == 0) {
		return;
	}

	bool reallocated = false;
	// So a parte nova sobe; se nao cabe, o buffer cresce com folga e tudo e
	// reenviado (glNamedBufferData troca o armazenamento).
	auto append = [&reallocated](GLuint buffer, const void* data, size_t element_size, size_t count, size_t& on_gpu, size_t& capacity) {
		if (count == on_gpu) {
			return;
		}
		const auto* bytes = static_cast<const uint8_t*>(data);
		if (count > capacity) {
			capacity = std::max<size_t>(count * 2, 256);
			glNamedBufferData(buffer, static_cast<GLsizeiptr>(capacity * element_size), nullptr, GL_DYNAMIC_DRAW);
			glNamedBufferSubData(buffer, 0, static_cast<GLsizeiptr>(count * element_size), bytes);
			reallocated = true;
		} else {
			glNamedBufferSubData(buffer, static_cast<GLintptr>(on_gpu * element_size), static_cast<GLsizeiptr>((count - on_gpu) * element_size), bytes + on_gpu * element_size);
		}
		on_gpu = count;
	};
	append(anim_sequences_ssbo_, anim_sequences_.data(), sizeof(AnimSequenceGpu), anim_sequences_.size(), anim_sequences_on_gpu_, anim_sequences_capacity_);
	append(anim_frames_ssbo_, anim_frames_.data(), sizeof(uint32_t), anim_frames_.size(), anim_frames_on_gpu_, anim_frames_capacity_);

	// O relogio muda inteiro a cada troca de frame; sao poucas dezenas de uints.
	if (anim_clock_dirty_ && !anim_clock_.empty()) {
		if (anim_clock_.size() > anim_clock_capacity_) {
			anim_clock_capacity_ = std::max<size_t>(anim_clock_.size() * 2, 256);
			glNamedBufferData(anim_clock_ssbo_, static_cast<GLsizeiptr>(anim_clock_capacity_ * sizeof(uint32_t)), nullptr, GL_DYNAMIC_DRAW);
			reallocated = true;
		}
		glNamedBufferSubData(anim_clock_ssbo_, 0, static_cast<GLsizeiptr>(anim_clock_.size() * sizeof(uint32_t)), anim_clock_.data());
	}
	anim_clock_dirty_ = false;

	if (reallocated) {
		bindAnimationTables();
	}
}

void ChunkCacheManager::resetAnimationTables() {
	anim_sequences_.clear();
	anim_frames_.clear();
	anim_clock_.clear();
	anim_clock_ids_.clear();
	anim_sequence_of_.clear();
	anim_clock_slot_of_.clear();
	anim_sequences_on_gpu_ = 0;
	anim_frames_on_gpu_ = 0;
	anim_clock_dirty_ = false;

	// Os buffers mantem o tamanho que ja tinham. Na primeira vez recebem um
	// minimo, para o binding nunca apontar para um buffer sem armazenamento.
	auto ensureStorage = [](GLuint buffer, size_t& capacity, size_t element_size) {
		if (buffer != 0 && capacity == 0) {
			capacity = 256;
			glNamedBufferData(buffer, static_cast<GLsizeiptr>(capacity * element_size), nullptr, GL_DYNAMIC_DRAW);
		}
	};
	ensureStorage(anim_sequences_ssbo_, anim_sequences_capacity_, sizeof(AnimSequenceGpu));
	ensureStorage(anim_frames_ssbo_, anim_frames_capacity_, sizeof(uint32_t));
	ensureStorage(anim_clock_ssbo_, anim_clock_capacity_, sizeof(uint32_t));
}

void ChunkCacheManager::bindAnimationTables() const {
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ANIM_SEQUENCES_BINDING, anim_sequences_ssbo_);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ANIM_FRAMES_BINDING, anim_frames_ssbo_);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ANIM_CLOCK_BINDING, anim_clock_ssbo_);
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
