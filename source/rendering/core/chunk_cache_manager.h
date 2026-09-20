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
#include <unordered_map>
#include <vector>

class Map;
class AtlasManager;
class GameSprite;
struct RenderFrameContext;

// Which of the per-floor passes a tile could not have baked, and therefore
// still has to go through the CPU tile renderer this frame.
enum ChunkDeferredPass : uint8_t {
	CHUNK_DEFER_GROUND = 1 << 0,
	CHUNK_DEFER_BORDERS = 1 << 1,
	CHUNK_DEFER_CONTENTS = 1 << 2,
};

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
		last_accessed_frame(other.last_accessed_frame),
		is_dirty(other.is_dirty),
		is_empty(other.is_empty),
		pending_bake_retries(other.pending_bake_retries),
		deferred_tiles(std::move(other.deferred_tiles)),
		used_sprites(std::move(other.used_sprites)) {
		other.vbo = 0;
		other.vbo_capacity = 0;
		other.instance_count = 0;
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
			last_accessed_frame = other.last_accessed_frame;
			is_dirty = other.is_dirty;
			is_empty = other.is_empty;
			pending_bake_retries = other.pending_bake_retries;
			deferred_tiles = std::move(other.deferred_tiles);
			used_sprites = std::move(other.used_sprites);
			other.vbo = 0;
			other.vbo_capacity = 0;
			other.instance_count = 0;
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
 * All three per-floor passes are cached: ground, borders and the static part
 * of contents (walls, furniture, decoration). A tile only stays on the CPU
 * when something about it actually changes per frame -- an animated sprite, a
 * creature, a selection, an indicator, a marker, the highlighted house. Those
 * are reported through forEachDeferredTile(), pass by pass, in the same order
 * the CPU renderer used to walk them.
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

private:
	void bakeChunk(CachedChunk& chunk, const Map& map, const RenderFrameContext& ctx);
	void uploadChunk(CachedChunk& chunk, const std::vector<TileInstance>& instances);
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
