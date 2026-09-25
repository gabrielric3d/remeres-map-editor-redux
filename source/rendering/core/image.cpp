#include "rendering/core/image.h"
#include "ui/gui.h" // For g_gui
#include "rendering/core/graphics.h" // For GraphicManager
#include "rendering/utilities/render_profiler.h"

Image::Image() :
	isGLLoaded(false),
	lastaccess(0) {
}

void Image::visit() const {
	lastaccess.store(static_cast<int64_t>(g_gui.gfx.getCachedTime()), std::memory_order_relaxed);
}

void Image::clean(time_t time, int longevity) {
	// Base implementation does nothing
}

const AtlasRegion* Image::EnsureAtlasSprite(uint32_t sprite_id, std::unique_ptr<uint8_t[]> preloaded_data, ImageDimensions dimensions) {
	if (g_gui.gfx.ensureAtlasManager()) {
		AtlasManager* atlas_mgr = g_gui.gfx.getAtlasManager();

		// 1. Check if already loaded
		const AtlasRegion* region = atlas_mgr->getRegion(sprite_id);
		if (region) {
			// CRITICAL FIX: Check if the region we found is marked INVALID (from double-allocation fix)
			// or belongs to another sprite (mismatch).
			if (region->debug_sprite_id == AtlasRegion::INVALID_SENTINEL || (region->debug_sprite_id != 0 && region->debug_sprite_id != sprite_id)) {
				spdlog::warn("STALE/INVALID MAP ENTRY DETECTED: Sprite {} maps to region owned by {}. Clearing mapping.", sprite_id, region->debug_sprite_id);
				// SAFETY: Only call clearMapping to avoid freeing shared slots owned by others.
				// removeSprite() is only for explicit destruction.
				atlas_mgr->clearMapping(sprite_id);
				region = nullptr; // Force reload
			} else {
				return region;
			}
		}

		// 2. Load data
		// Sem dado do preloader, a leitura do arquivo e a decodificacao acontecem
		// aqui mesmo, no thread principal e no meio do desenho do frame.
		const bool synchronous = !preloaded_data;
		RenderProfiler::Count(synchronous ? RenderProfiler::Counter::SyncSpriteLoads : RenderProfiler::Counter::PreloadUploads);
		const RenderProfiler::Scope sync_load_scope(RenderProfiler::Section::SyncSpriteLoad, synchronous);

		std::unique_ptr<uint8_t[]> rgba;
		if (preloaded_data) {
			rgba = std::move(preloaded_data);
		} else {
			rgba = getRGBAData();
		}

		if (!rgba) {
			// Fallback: Create a magenta texture to distinguish failure from garbage.
			// O placeholder e sempre uma celula 32x32, independente do tamanho que
			// o sprite teria tido.
			constexpr int RGBA_COMPONENTS = 4;
			dimensions = {};
			const int pixel_count = static_cast<int>(dimensions.pixelCount());
			rgba = std::make_unique<uint8_t[]>(static_cast<size_t>(pixel_count) * RGBA_COMPONENTS);
			std::span<uint8_t> buffer(rgba.get(), static_cast<size_t>(pixel_count) * RGBA_COMPONENTS);
			for (int i : std::views::iota(0, pixel_count)) {
				buffer[i * RGBA_COMPONENTS + 0] = 255;
				buffer[i * RGBA_COMPONENTS + 1] = 0;
				buffer[i * RGBA_COMPONENTS + 2] = 255;
				buffer[i * RGBA_COMPONENTS + 3] = 255;
			}
			spdlog::warn("getRGBAData returned null for sprite_id={} - using fallback", sprite_id);
		}

		// 3. Add to Atlas
		region = atlas_mgr->addSprite(sprite_id, rgba.get(), dimensions.width, dimensions.height);

		if (region) {
			if (!isGLLoaded) {
				isGLLoaded = true;
				g_gui.gfx.resident_images.push_back(this); // Add to resident set
			}
			g_gui.gfx.collector.NotifyTextureLoaded();
			return region;
		} else {
			spdlog::warn("Atlas addSprite failed for sprite_id={}", sprite_id);
		}
	} else {
		spdlog::error("AtlasManager not available for sprite_id={}", sprite_id);
	}
	return nullptr;
}
