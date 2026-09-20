#ifndef RME_RENDERING_CORE_NORMAL_IMAGE_H_
#define RME_RENDERING_CORE_NORMAL_IMAGE_H_

#include "rendering/core/image.h"

class GameSprite;

class NormalImage : public Image {
public:
	NormalImage();
	~NormalImage() override;

	bool isNormalImage() const override {
		return true;
	}

	const AtlasRegion* getAtlasRegion();

	// We use the sprite id as key
	uint32_t id;
	const AtlasRegion* atlas_region;

	// Marca de "ja existe um preload deste sprite na fila": 0 = nenhum, senao o
	// epoch do SpritePreloader em que foi enfileirado (guardado +1 para que 0
	// continue significando "nenhum").
	//
	// E o epoch, e nao um bool, porque SpritePreloader::clear() joga as filas fora
	// sem conseguir alcancar as imagens marcadas -- com o epoch, um clear invalida
	// todas as marcas de uma vez so, ao inves de deixa-las presas em true e o
	// sprite sem nunca mais ser pedido.
	uint64_t preload_epoch = 0;

	// This contains the pixel data
	uint16_t size;
	std::unique_ptr<uint8_t[]> dump;

	void clean(time_t time, int longevity) override;

	std::unique_ptr<uint8_t[]> getRGBData() override;
	std::unique_ptr<uint8_t[]> getRGBAData() override;

	// 32x32 no .spr; nas folhas 12+/13 pode ser 32x64, 64x32 ou 64x64.
	[[nodiscard]] ImageDimensions sourceDimensions() const;

	[[nodiscard]] ImageDimensions getDimensions() const override {
		return sourceDimensions();
	}

	void fulfillPreload(std::unique_ptr<uint8_t[]> preloaded_data, ImageDimensions dimensions = {});

	GameSprite* parent = nullptr;
};

#endif
