#include "rendering/core/normal_image.h"
#include "rendering/core/game_sprite.h"
#include "app/settings.h"
#include "rendering/core/sprite_archive.h"
#include "ui/gui.h"
#include <spdlog/spdlog.h>

constexpr int RGB_COMPONENTS = 3;

namespace {
	bool loadDumpFromArchive(uint32_t id, std::unique_ptr<uint8_t[]>& dump, uint16_t& size) {
		const auto archive = g_gui.gfx.getSpriteArchive();
		return archive && archive->readCompressed(id, dump, size);
	}
}

// Tamanho deste sprite na fonte. O .spr classico e sempre 32x32; um catalogo
// 12+/13 responde conforme a folha em que o sprite mora.
ImageDimensions NormalImage::sourceDimensions() const {
	const auto archive = g_gui.gfx.getSpriteArchive();
	if (archive && archive->isProtobuf()) {
		return archive->spriteDimensions(id);
	}
	return {};
}

NormalImage::NormalImage() :
	id(0),
	atlas_region(nullptr),
	size(0),
	dump(nullptr) {
}

NormalImage::~NormalImage() {
	// dump auto-deleted
	if (isGLLoaded) {
		if (g_gui.gfx.hasAtlasManager()) {
			g_gui.gfx.getAtlasManager()->removeSprite(id);
		}
	}
}

void NormalImage::fulfillPreload(std::unique_ptr<uint8_t[]> data, ImageDimensions dimensions) {
	atlas_region = EnsureAtlasSprite(id, std::move(data), dimensions);
	preload_epoch = 0;
	// Um sprite que acabou de subir para o atlas foi pedido AGORA: sem marcar,
	// entra no atlas ja com lastaccess velho e pode ser o proximo a ser despejado.
	visit();
}

void NormalImage::clean(time_t time, int longevity) {
	// Evict from atlas if expired
	if (longevity == -1) {
		longevity = g_settings.getInteger(Config::TEXTURE_LONGEVITY);
	}
	if (isGLLoaded && time - static_cast<time_t>(lastaccess.load(std::memory_order_relaxed)) > longevity) {
		if (g_gui.gfx.hasAtlasManager()) {
			g_gui.gfx.getAtlasManager()->removeSprite(id);
		}
		if (parent) {
			parent->invalidateCache(atlas_region);
		}

		isGLLoaded = false;
		atlas_region = nullptr;

		// Invalidate any pending preloads for this sprite ID
		generation_id++;

		// O bump acima ja descarta o resultado em voo; liberar a marca deixa o
		// proximo frame reenfileirar o sprite sem esperar aquele descarte.
		preload_epoch = 0;

		g_gui.gfx.collector.NotifyTextureUnloaded();
	}

	if (time - static_cast<time_t>(lastaccess.load(std::memory_order_relaxed)) > 5 && !g_settings.getInteger(Config::USE_MEMCACHED_SPRITES)) { // We keep dumps around for 5 seconds.
		dump.reset();
	}
}
std::unique_ptr<uint8_t[]> NormalImage::getRGBData() {
	if (id == 0) {
		const int pixels_data_size = SPRITE_PIXELS * SPRITE_PIXELS * RGB_COMPONENTS;
		return std::make_unique<uint8_t[]>(pixels_data_size); // Value-initialized (zeroed)
	}

	// Folhas 12+/13 nao tem blob RLE para descomprimir, e o sprite pode ser maior
	// que 32x32. Sem este desvio o caminho abaixo devolveria um buffer de 32x32
	// preenchido com a cor de mascara, e quem desenha o icone leria a folha com o
	// stride errado -- o sprite sai em listras diagonais.
	if (const auto archive = g_gui.gfx.getSpriteArchive(); archive && archive->isProtobuf()) {
		std::unique_ptr<uint8_t[]> rgba;
		ImageDimensions dimensions;
		if (!archive->readRGBA(id, rgba, dimensions)) {
			return nullptr;
		}

		const size_t pixel_count = dimensions.pixelCount();
		auto converted = std::make_unique<uint8_t[]>(pixel_count * RGB_COMPONENTS);
		for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
			const size_t source = pixel * 4;
			const size_t destination = pixel * RGB_COMPONENTS;
			if (rgba[source + 3] == 0) {
				// Magenta e a cor de mascara que os iconos usam como transparencia.
				converted[destination + 0] = 0xFF;
				converted[destination + 1] = 0x00;
				converted[destination + 2] = 0xFF;
				continue;
			}
			converted[destination + 0] = rgba[source + 0];
			converted[destination + 1] = rgba[source + 1];
			converted[destination + 2] = rgba[source + 2];
		}
		return converted;
	}

	if (!dump) {
		if (!loadDumpFromArchive(id, dump, size)) {
			return nullptr;
		}
	}

	const int pixels_data_size = SPRITE_PIXELS * SPRITE_PIXELS * RGB_COMPONENTS;
	auto data = std::make_unique<uint8_t[]>(pixels_data_size);
	uint8_t bpp = g_gui.gfx.hasTransparency() ? 4 : RGB_COMPONENTS;
	size_t write = 0;
	size_t read = 0;

	// decompress pixels
	while (read < size && write < static_cast<size_t>(pixels_data_size)) {
		if (read + 1 >= size) {
			spdlog::warn("NormalImage::getRGBData: Transparency header truncated (read={}, size={})", read, size);
			break;
		}
		int transparent = dump[read] | dump[read + 1] << 8;
		read += 2;
		for (int cnt = 0; cnt < transparent && write < static_cast<size_t>(pixels_data_size); ++cnt) {
			data[write + 0] = 0xFF; // red
			data[write + 1] = 0x00; // green
			data[write + 2] = 0xFF; // blue
			write += RGB_COMPONENTS;
		}

		if (read + 1 >= size) {
			spdlog::warn("NormalImage::getRGBData: Colored header truncated (read={}, size={})", read, size);
			break;
		}

		int colored = dump[read] | dump[read + 1] << 8;
		read += 2;

		if (read + static_cast<size_t>(colored) * bpp > size) {
			spdlog::warn("NormalImage::getRGBData: Read buffer overrun (colored={}, bpp={}, read={}, size={})", colored, bpp, read, size);
			break;
		}

		for (int cnt = 0; cnt < colored && write < static_cast<size_t>(pixels_data_size); ++cnt) {
			data[write + 0] = dump[read + 0]; // red
			data[write + 1] = dump[read + 1]; // green
			data[write + 2] = dump[read + 2]; // blue
			write += RGB_COMPONENTS;
			read += bpp;
		}
	}

	// fill remaining pixels
	while (write < static_cast<size_t>(pixels_data_size)) {
		data[write + 0] = 0xFF; // red
		data[write + 1] = 0x00; // green
		data[write + 2] = 0xFF; // blue
		write += RGB_COMPONENTS;
	}
	return data;
}

std::unique_ptr<uint8_t[]> NormalImage::getRGBAData() {
	// Robust ID 0 handling
	if (id == 0) {
		const int pixels_data_size = SPRITE_PIXELS_SIZE * 4;
		return std::make_unique<uint8_t[]>(pixels_data_size); // Value-initialized (zeroed)
	}

	// Folhas 12+/13 ja entregam RGBA: nao ha blob RLE para descomprimir.
	if (const auto archive = g_gui.gfx.getSpriteArchive(); archive && archive->isProtobuf()) {
		std::unique_ptr<uint8_t[]> rgba;
		ImageDimensions dimensions;
		if (!archive->readRGBA(id, rgba, dimensions)) {
			return nullptr;
		}
		return rgba;
	}

	if (!dump) {
		if (!loadDumpFromArchive(id, dump, size)) {
			// This is the only case where we return nullptr for non-zero ID
			// effectively warning the caller that the sprite is missing from file
			return nullptr;
		}
	}

	return GameSprite::Decompress(std::span { dump.get(), size }, g_gui.gfx.hasTransparency(), id);
}

const AtlasRegion* NormalImage::getAtlasRegion() {
	if (isGLLoaded && atlas_region) {
		// Self-Healing: Check for stale atlas region pointer (e.g. from memory reuse)
		// Force reload if Owner is INVALID or DOES NOT MATCH
		if (atlas_region->debug_sprite_id == AtlasRegion::INVALID_SENTINEL || (atlas_region->debug_sprite_id != 0 && atlas_region->debug_sprite_id != id)) {
			spdlog::warn("STALE ATLAS REGION DETECTED: NormalImage {} held region owned by {}. Force reloading.", id, atlas_region->debug_sprite_id);
			isGLLoaded = false;
			atlas_region = nullptr;
		} else {
			visit();
			return atlas_region;
		}
	}

	if (!isGLLoaded) {
		atlas_region = EnsureAtlasSprite(id, nullptr, sourceDimensions());
	}
	visit();
	return atlas_region;
}
