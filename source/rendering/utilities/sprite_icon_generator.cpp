//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/utilities/sprite_icon_generator.h"
#include "app/settings.h"
#include "ui/gui.h"
#include <algorithm>
#include <ranges>
#include <span>

namespace {
	// No .spr classico cada sprite da lista e 32x32; nas folhas 12+/13 ele pode ser
	// 32x64, 64x32 ou 64x64. O icone tem de montar a grade com a celula real,
	// senao a imagem e lida com o stride errado e sai em listras diagonais.
	ImageDimensions spriteCell(const GameSprite* sprite) {
		for (const NormalImage* image : sprite->spriteList) {
			if (image != nullptr) {
				return image->getDimensions();
			}
		}
		return {};
	}

	// O tamanho sai da propria imagem de origem, e nao da celula da grade: dois
	// sprites do mesmo objeto podem vir de folhas de tamanhos diferentes, e um
	// wxImage maior que o buffer leria fora dele.
	void pasteCell(wxImage& target, const Image* source, const std::unique_ptr<uint8_t[]>& data, int x, int y) {
		if (!data || source == nullptr) {
			return;
		}
		const ImageDimensions dimensions = source->getDimensions();
		wxImage cell_image(dimensions.width, dimensions.height, data.get(), true);
		cell_image.SetMaskColour(0xFF, 0x00, 0xFF);
		target.Paste(cell_image, x, y);
	}
}

wxBitmap SpriteIconGenerator::Generate(GameSprite* sprite, SpriteSize size, bool rescale) {
	ASSERT(sprite->width >= 1 && sprite->height >= 1);

	// Value is a grayscale shade 0-255
	const unsigned char bgshade = static_cast<unsigned char>(g_settings.getInteger(Config::ICON_BACKGROUND));

	const ImageDimensions cell = spriteCell(sprite);
	const int image_size = std::max(sprite->width * cell.width, sprite->height * cell.height);
	wxImage image(image_size, image_size);
	image.Create(image_size, image_size);
	image.InitAlpha();

	unsigned char* rawData = image.GetData();
	unsigned char* rawAlpha = image.GetAlpha();
	int count = image_size * image_size;

	std::span<unsigned char> bgData(rawData, static_cast<size_t>(count) * 3);
	std::span<unsigned char> alphaData(rawAlpha, count);

	for (int i : std::views::iota(0, count)) {
		bgData[i * 3 + 0] = bgshade;
		bgData[i * 3 + 1] = bgshade;
		bgData[i * 3 + 2] = bgshade;
	}
	std::ranges::fill(alphaData, 255);

	for (uint8_t l = 0; l < sprite->layers; l++) {
		for (uint8_t w = 0; w < sprite->width; w++) {
			for (uint8_t h = 0; h < sprite->height; h++) {
				const int i = sprite->getIndex(w, h, l, 0, 0, 0, 0);
				NormalImage* source = sprite->spriteList[i];
				pasteCell(image, source, source->getRGBData(), (sprite->width - w - 1) * cell.width, (sprite->height - h - 1) * cell.height);
			}
		}
	}

	// Now comes the resizing / antialiasing
	if (rescale && (size == SPRITE_SIZE_16x16 || size == SPRITE_SIZE_64x64 || image.GetWidth() > SPRITE_PIXELS || image.GetHeight() > SPRITE_PIXELS)) {
		int new_size = 32;
		if (size == SPRITE_SIZE_16x16) {
			new_size = 16;
		} else if (size == SPRITE_SIZE_64x64) {
			new_size = 64;
		}
		image.Rescale(new_size, new_size, wxIMAGE_QUALITY_HIGH);
	}

	return wxBitmap(image);
}

wxBitmap SpriteIconGenerator::Generate(GameSprite* sprite, SpriteSize size, const Outfit& outfit, bool rescale, Direction direction) {
	ASSERT(sprite->width >= 1 && sprite->height >= 1);

	// Value is a grayscale shade 0-255
	const unsigned char bgshade = static_cast<unsigned char>(g_settings.getInteger(Config::ICON_BACKGROUND));

	const ImageDimensions cell = spriteCell(sprite);
	const int image_size = std::max(sprite->width * cell.width, sprite->height * cell.height);
	wxImage image(image_size, image_size);
	image.Create(image_size, image_size);
	image.InitAlpha();

	unsigned char* rawData = image.GetData();
	unsigned char* rawAlpha = image.GetAlpha();
	int count = image_size * image_size;

	std::span<unsigned char> bgData(rawData, static_cast<size_t>(count) * 3);
	std::span<unsigned char> alphaData(rawAlpha, count);

	for (int i : std::views::iota(0, count)) {
		bgData[i * 3 + 0] = bgshade;
		bgData[i * 3 + 1] = bgshade;
		bgData[i * 3 + 2] = bgshade;
	}
	std::ranges::fill(alphaData, 255);

	int frame_index = 0;
	if (sprite->pattern_x == 4) {
		frame_index = direction;
	}

	// Mounts
	int pattern_z = 0;
	if (outfit.lookMount != 0) {
		if (GameSprite* mountSpr = g_gui.gfx.getCreatureSprite(outfit.lookMount)) {
			// Mount outfit
			Outfit mountOutfit;
			mountOutfit.lookType = outfit.lookMount;
			mountOutfit.lookHead = outfit.lookMountHead;
			mountOutfit.lookBody = outfit.lookMountBody;
			mountOutfit.lookLegs = outfit.lookMountLegs;
			mountOutfit.lookFeet = outfit.lookMountFeet;

			// We need to render the mount
			// Simplified rendering: just render base frame 0 for mount (or south)
			int mount_frame_index = 0;
			if (mountSpr->pattern_x == 4) {
				mount_frame_index = direction;
			}

			for (uint8_t l = 0; l < mountSpr->layers; l++) {
				for (uint8_t w = 0; w < mountSpr->width; w++) {
					for (uint8_t h = 0; h < mountSpr->height; h++) {
						std::unique_ptr<uint8_t[]> data = nullptr;
						const Image* source = nullptr;
						// Handle mount sprite layers/templates similar to main sprite
						// (Usually mounts are standard creatures)
						if (mountSpr->layers == 2) {
							if (l == 1) {
								continue;
							}
							TemplateImage* template_image = mountSpr->getTemplateImage(mountSpr->getIndex(w, h, 0, mount_frame_index, 0, 0, 0), mountOutfit);
							source = template_image;
							data = template_image->getRGBData();
						} else {
							// Standard mount
							NormalImage* mount_image = mountSpr->spriteList[mountSpr->getIndex(w, h, l, mount_frame_index, 0, 0, 0)];
							source = mount_image;
							data = mount_image->getRGBData();
						}

						const ImageDimensions mount_cell = spriteCell(mountSpr);
						const int mount_x = (sprite->width - w - 1) * mount_cell.width - mountSpr->getDrawOffset().first;
						const int mount_y = (sprite->height - h - 1) * mount_cell.height - mountSpr->getDrawOffset().second;
						pasteCell(image, source, data, mount_x, mount_y);
					}
				}
			}
			pattern_z = std::min<int>(1, sprite->pattern_z - 1);
		}
	}

	for (int pattern_y = 0; pattern_y < sprite->pattern_y; pattern_y++) {
		if (pattern_y > 0) {
			if ((pattern_y - 1 >= 31) || !(outfit.lookAddon & (1 << (pattern_y - 1)))) {
				continue;
			}
		}

		for (uint8_t l = 0; l < sprite->layers; l++) {
			for (uint8_t w = 0; w < sprite->width; w++) {
				for (uint8_t h = 0; h < sprite->height; h++) {
					std::unique_ptr<uint8_t[]> data = nullptr;
					const Image* source = nullptr;

					const auto takeTemplate = [&](int layer) {
						TemplateImage* template_image = sprite->getTemplateImage(sprite->getIndex(w, h, layer, frame_index, pattern_y, pattern_z, 0), outfit);
						source = template_image;
						data = template_image->getRGBData();
					};

					if (sprite->layers == 2) {
						if (l == 1) {
							continue;
						}
						takeTemplate(0);
					} else if (sprite->layers == 4) {
						if (l == 1 || l == 3) {
							continue;
						}
						if (l == 0 || l == 2) {
							takeTemplate(l);
						}
					} else {
						NormalImage* layer_image = sprite->spriteList[sprite->getIndex(w, h, l, frame_index, pattern_y, pattern_z, 0)];
						source = layer_image;
						data = layer_image->getRGBData();
					}

					pasteCell(image, source, data, (sprite->width - w - 1) * cell.width, (sprite->height - h - 1) * cell.height);
				}
			}
		}
	}

	// Now comes the resizing / antialiasing
	if (rescale && (size == SPRITE_SIZE_16x16 || size == SPRITE_SIZE_64x64 || image.GetWidth() > SPRITE_PIXELS || image.GetHeight() > SPRITE_PIXELS)) {
		int new_size = 32;
		if (size == SPRITE_SIZE_16x16) {
			new_size = 16;
		} else if (size == SPRITE_SIZE_64x64) {
			new_size = 64;
		}
		image.Rescale(new_size, new_size, wxIMAGE_QUALITY_HIGH);
	}

	return wxBitmap(image);
}
