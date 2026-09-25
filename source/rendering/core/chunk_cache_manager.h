//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_CORE_CHUNK_CACHE_MANAGER_H_
#define RME_RENDERING_CORE_CHUNK_CACHE_MANAGER_H_

#include <glad/glad.h>
#include <glm/glm.hpp>
#include "rendering/core/shader_program.h"
#include "rendering/core/tile_instance.h"
#include "map/spatial_change_tracker.h"
#include <array>
#include <unordered_map>
#include <vector>

class Map;
class AtlasManager;
class GameSprite;
class GraphicManager;
struct RenderFrameContext;
struct SpritePatterns;

// Which of the per-floor passes a tile could not have baked, and therefore
// still has to go through the CPU tile renderer this frame.
enum ChunkDeferredPass : uint8_t {
	CHUNK_DEFER_GROUND = 1 << 0,
	CHUNK_DEFER_BORDERS = 1 << 1,
	CHUNK_DEFER_CONTENTS = 1 << 2,
};

// Por que uma fatia de tile ficou fora do bake. Contado por chunk para o profiler
// de frame (F9) dizer O QUE esta enchendo as passadas de CPU, e nao so quanto.
// A ordem tem de bater com RenderProfiler::Counter::DeferAnimated..DeferOther.
enum class ChunkDeferReason : uint8_t {
	Animated, // sprite animado (chao, borda ou item)
	Light, // "Show Light Strength" ligado e o sprite tem luz
	Technical, // casos especiais de "Show Technical Items" e fontes de luz
	Indicator, // portas, ganchos, pickupables e moveables destacados
	Selected,
	Marker, // criatura, spawn, waypoint, town, house exit, zona invalida, casa pulsando
	Other, // item invalido, podium, teleport, meta item, quadrado de zona, borda atipica
	Count
};

constexpr size_t kChunkDeferReasonCount = static_cast<size_t>(ChunkDeferReason::Count);

struct DeferredTileInfo {
	uint8_t rel_x = 0;
	uint8_t rel_y = 0;
	uint8_t pass_mask = 0;
};

struct CachedChunk {
	ChunkCoord coord;
	GLuint vbo = 0;
	size_t vbo_capacity = 0; // in bytes
	uint32_t instance_count = 0;
	// Instancias [0, contents_offset) sao chao e bordas; [contents_offset,
	// instance_count), o conteudo. As duas faixas sao desenhadas em momentos
	// diferentes do andar, com as passadas de CPU de chao e borda entre elas.
	uint32_t contents_offset = 0;
	uint64_t last_accessed_frame = 0;
	bool is_dirty = true;
	bool is_empty = false;
	// Consecutive re-bakes that still found a sprite without an atlas region.
	// Bounded so a sprite that never loads cannot pin the chunk to a re-bake
	// on every single frame.
	uint16_t pending_bake_retries = 0;
	std::vector<DeferredTileInfo> deferred_tiles;
	// Distinct sprites baked into this chunk, kept so their atlas LRU stamp
	// can be renewed while the chunk is on screen. Only valid as long as the
	// AtlasManager instance has not been replaced, which drops the cache.
	std::vector<GameSprite*> used_sprites;
	// Fatias deferidas no ultimo bake, por motivo (ChunkDeferReason).
	std::array<uint32_t, kChunkDeferReasonCount> defer_reasons {};

	CachedChunk() = default;
	~CachedChunk() {
		if (vbo != 0) {
			glDeleteBuffers(1, &vbo);
			vbo = 0;
		}
	}

	CachedChunk(CachedChunk&& other) noexcept :
		coord(other.coord),
		vbo(other.vbo),
		vbo_capacity(other.vbo_capacity),
		instance_count(other.instance_count),
		contents_offset(other.contents_offset),
		last_accessed_frame(other.last_accessed_frame),
		is_dirty(other.is_dirty),
		is_empty(other.is_empty),
		pending_bake_retries(other.pending_bake_retries),
		deferred_tiles(std::move(other.deferred_tiles)),
		used_sprites(std::move(other.used_sprites)),
		defer_reasons(other.defer_reasons) {
		other.vbo = 0;
		other.vbo_capacity = 0;
		other.instance_count = 0;
		other.contents_offset = 0;
	}

	CachedChunk& operator=(CachedChunk&& other) noexcept {
		if (this != &other) {
			if (vbo != 0) {
				glDeleteBuffers(1, &vbo);
			}
			coord = other.coord;
			vbo = other.vbo;
			vbo_capacity = other.vbo_capacity;
			instance_count = other.instance_count;
			contents_offset = other.contents_offset;
			last_accessed_frame = other.last_accessed_frame;
			is_dirty = other.is_dirty;
			is_empty = other.is_empty;
			pending_bake_retries = other.pending_bake_retries;
			deferred_tiles = std::move(other.deferred_tiles);
			used_sprites = std::move(other.used_sprites);
			defer_reasons = other.defer_reasons;
			other.vbo = 0;
			other.vbo_capacity = 0;
			other.instance_count = 0;
			other.contents_offset = 0;
		}
		return *this;
	}

	CachedChunk(const CachedChunk&) = delete;
	CachedChunk& operator=(const CachedChunk&) = delete;
};

/**
 * Per-chunk VBO cache for the static part of a floor.
 *
 * A 16x16 chunk keeps its ground and ground-border sprites in a GPU buffer of
 * its own, re-baked only when that chunk's tiles change. Geometry stores a
 * stable sprite id instead of atlas UVs and resolves them through an SSBO
 * (SpriteAtlasLUT), so a sprite finishing its background load -- or the atlas
 * repacking -- never invalidates a single chunk.
 *
 * All three per-floor passes are cached: ground, borders and contents (walls,
 * furniture, decoration). A tile only stays on the CPU when the bake cannot
 * express it -- a creature, an indicator, a marker, the pulsing house, a
 * technical overlay. Those are reported through forEachDeferredTile(), pass by
 * pass, in the same order the CPU renderer used to walk them.
 *
 * Sprite animado entra como sequencia de frames: a CPU publica o frame corrente
 * de cada Animator (updateAnimationClock) e o shader troca o sprite. Item
 * selecionado entra com o tint pela metade, e o retangulo de selecao em arrasto
 * e aplicado no shader -- o cache nao desliga mais durante o arrasto.
 *
 * Um andar e desenhado em quatro etapas, reproduzindo as tres passadas da CPU:
 * renderFloor() (chao + bordas dos chunks) -> CPU chao -> CPU bordas ->
 * renderFloorContents() (conteudo dos chunks) -> CPU conteudo. Assim um chao
 * deferido (agua animada) fica embaixo do conteudo cacheado do vizinho, e um
 * chao que transborda o tile (montanha 64x64) pode ir para o cache: ele e
 * emitido na faixa de conteudo, junto com as bordas e os itens do proprio tile,
 * exatamente onde o TileRenderer o desenharia.
 *
 * Because whole tiles drop out of the CPU walk, the side effects that walk
 * used to produce are gathered separately: LightGatherer for the light buffer
 * and TooltipCollector for the inspection tooltips.
 */
class ChunkCacheManager {
public:
	static constexpr int CHUNK_SIZE = 16;
	static constexpr uint64_t FAR_FLOOR_FRAME_THRESHOLD = 60; // 1.0 second at 60 FPS
	static constexpr uint64_t PRUNE_INTERVAL_FRAMES = 120; // 2.0 seconds at 60 FPS
	static constexpr size_t MAX_CACHED_CHUNKS = 65536; // High-water mark (~200 MB VRAM ceiling)
	static constexpr size_t TARGET_CACHED_CHUNKS = 49152; // Low-water mark (75%)
	static constexpr int VIEWPORT_MARGIN_CHUNKS = 32; // 512 tiles

	ChunkCacheManager();
	~ChunkCacheManager();

	ChunkCacheManager(const ChunkCacheManager&) = delete;
	ChunkCacheManager& operator=(const ChunkCacheManager&) = delete;

	bool initialize();
	void release();

	/**
	 * Synchronize dirty state from the map's SpatialChangeTracker.
	 */
	void updateDirtyState(SpatialChangeTracker& change_tracker);

	/**
	 * Re-bake everything when a drawing option that the bake depends on changed.
	 * Compares DrawingOptions::chunkBakeSignature() against the last frame's.
	 */
	void updateOptionsState(uint64_t bake_signature);

	/**
	 * React to the atlas underneath the cache changing: a sprite evicted by the
	 * texture GC leaves stale geometry behind (re-bake), and a brand new
	 * AtlasManager means the old GameSprite pointers are gone (drop everything).
	 */
	void updateAtlasState(const AtlasManager* atlas);

	/**
	 * Invalidate all cached chunks across all floors.
	 */
	void invalidateAll();

	/**
	 * Invalidate a specific chunk coordinate.
	 */
	void invalidateChunk(int32_t cx, int32_t cy, int32_t z);

	/**
	 * Render the cached ground and border geometry of every visible chunk on
	 * floor map_z, baking the ones that are dirty or missing.
	 */
	void renderFloor(
		int map_z,
		const Map& map,
		const RenderFrameContext& ctx,
		const glm::mat4& projection,
		AtlasManager& atlas
	);

	/**
	 * Desenha a faixa de conteudo dos chunks visitados pelo ultimo renderFloor().
	 * Vem depois das passadas de CPU de chao e borda do mesmo andar.
	 */
	void renderFloorContents(
		const RenderFrameContext& ctx,
		const glm::mat4& projection,
		AtlasManager& atlas
	);

	/**
	 * Visit the tiles of the floor last rendered by renderFloor() that still
	 * need the given CPU pass. func is called as func(int map_x, int map_y).
	 */
	template <typename Func>
	void forEachDeferredTile(ChunkDeferredPass pass, Func&& func) const {
		for (const CachedChunk* chunk_ptr : active_visible_chunks_) {
			if (!chunk_ptr || chunk_ptr->deferred_tiles.empty()) {
				continue;
			}
			const int base_x = chunk_ptr->coord.cx * CHUNK_SIZE;
			const int base_y = chunk_ptr->coord.cy * CHUNK_SIZE;
			for (const auto& dt : chunk_ptr->deferred_tiles) {
				if (dt.pass_mask & pass) {
					func(base_x + dt.rel_x, base_y + dt.rel_y);
				}
			}
		}
	}

	/**
	 * Advance frame counter and trigger periodic prune. Called once per frame.
	 */
	void advanceFrame(int current_floor);

	/**
	 * Evict distant/stale chunks outside the active floor range or viewport margin.
	 */
	void prune(int current_floor, int min_cx = 0, int max_cx = 0, int min_cy = 0, int max_cy = 0, bool has_bounds = false);

	[[nodiscard]] size_t getCachedChunkCount() const noexcept {
		return cached_chunks_.size();
	}
	[[nodiscard]] bool isValid() const noexcept {
		return vao_ != 0 && shader_initialized_;
	}

	/**
	 * Publica, uma vez por frame, o frame corrente de cada sprite animado que ja
	 * foi para o cache. E o mesmo Animator que o TileRenderer consulta, entao a
	 * GPU mostra exatamente o que a CPU desenharia -- inclusive parado, quando a
	 * animacao esta desligada. Chamar depois de updateAtlasState().
	 */
	void updateAnimationClock(GraphicManager& gfx);

private:
	// --- Animacao na GPU ------------------------------------------------------
	// Um sprite animado vai para o cache como uma sequencia: os sprite ids de
	// todos os frames daquela celula/pattern, em ordem. O vertex shader troca o
	// sprite pelo do frame corrente, publicado em anim_clock_.
	struct AnimSequenceGpu {
		uint32_t first_frame = 0; // indice em anim_frames_
		uint32_t frame_count = 0;
		uint32_t clock_slot = 0; // indice em anim_clock_
		uint32_t _pad = 0;
	};
	// Por client id, e nao por GameSprite*: recarregar os graficos pode devolver
	// outro sprite no mesmo endereco.
	struct AnimSequenceKey {
		uint32_t client_id = 0;
		int16_t cell_x = 0;
		int16_t cell_y = 0;
		int16_t layer = 0;
		int16_t subtype = 0;
		int16_t pattern_x = 0;
		int16_t pattern_y = 0;
		int16_t pattern_z = 0;
		bool operator==(const AnimSequenceKey&) const = default;
	};
	struct AnimSequenceKeyHash {
		size_t operator()(const AnimSequenceKey& key) const noexcept;
	};

	// Indice+1 da sequencia, ou 0 se algum frame ainda nao tem regiao no atlas
	// (pending vira true e o chunk tenta de novo no frame seguinte).
	uint32_t animSequenceFor(GameSprite* spr, int cell_x, int cell_y, int layer, const SpritePatterns& patterns, bool& pending);
	uint32_t animClockSlotFor(const GameSprite* spr);
	// Envia para a GPU as sequencias criadas desde o ultimo envio. Vem logo
	// depois de cada bake: o chunk recem-assado e desenhado em seguida.
	void flushAnimationTables();
	void resetAnimationTables();
	void bindAnimationTables() const;

	static constexpr GLuint ANIM_SEQUENCES_BINDING = 3;
	static constexpr GLuint ANIM_FRAMES_BINDING = 4;
	static constexpr GLuint ANIM_CLOCK_BINDING = 5;

	std::vector<AnimSequenceGpu> anim_sequences_;
	std::vector<uint32_t> anim_frames_;
	std::vector<uint32_t> anim_clock_;
	std::vector<uint32_t> anim_clock_ids_; // client id de cada slot do relogio
	std::unordered_map<AnimSequenceKey, uint32_t, AnimSequenceKeyHash> anim_sequence_of_;
	std::unordered_map<uint32_t, uint32_t> anim_clock_slot_of_;
	GLuint anim_sequences_ssbo_ = 0;
	GLuint anim_frames_ssbo_ = 0;
	GLuint anim_clock_ssbo_ = 0;
	size_t anim_sequences_on_gpu_ = 0; // entradas ja enviadas
	size_t anim_frames_on_gpu_ = 0;
	size_t anim_sequences_capacity_ = 0; // capacidade dos buffers, em entradas
	size_t anim_frames_capacity_ = 0;
	size_t anim_clock_capacity_ = 0;
	bool anim_clock_dirty_ = false;

	void bakeChunk(CachedChunk& chunk, const Map& map, const RenderFrameContext& ctx);
	void uploadChunk(CachedChunk& chunk, const std::vector<TileInstance>& instances);
	// Shader, uniforms, atlas e VAO prontos para desenhar os chunks do andar map_z.
	void bindForFloor(int map_z, const RenderFrameContext& ctx, const glm::mat4& projection, AtlasManager& atlas);
	CachedChunk& getOrCreateChunk(const ChunkCoord& coord);
	void evictOldest(size_t count_to_remove);

	GLuint vao_ = 0;
	ShaderProgram shader_;
	bool shader_initialized_ = false;

	std::unordered_map<ChunkCoord, CachedChunk, ChunkCoordHash> cached_chunks_;
	std::vector<TileInstance> bake_buffer_;
	uint64_t current_frame_ = 0;
	uint64_t last_bake_signature_ = 0;
	bool has_bake_signature_ = false;
	const AtlasManager* last_atlas_ = nullptr;
	uint64_t last_eviction_generation_ = 0;

	// The atlas LRU has a longevity of tens of seconds, so renewing the stamp
	// of the cached sprites a couple of times per second is plenty.
	static constexpr uint64_t SPRITE_TOUCH_INTERVAL_FRAMES = 30;

	// A couple of seconds' worth of frames is far more than the preloader needs;
	// past that, the sprite is simply not coming.
	static constexpr uint16_t MAX_PENDING_BAKE_RETRIES = 120;

	std::vector<CachedChunk*> active_visible_chunks_;
	int active_floor_ = -1;
};

#endif
