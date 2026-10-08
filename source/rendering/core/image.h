#ifndef RME_RENDERING_CORE_IMAGE_H_
#define RME_RENDERING_CORE_IMAGE_H_

#include <atomic>
#include <memory>
#include <cstdint>
#include "rendering/core/atlas_manager.h" // For AtlasRegion

// Tamanho em pixels de um sprite. O .spr classico e sempre 32x32, mas os
// assets 12+/13 trazem folhas com sprites 32x64, 64x32 e 64x64.
struct ImageDimensions {
	uint16_t width = 32;
	uint16_t height = 32;

	[[nodiscard]] size_t pixelCount() const {
		return static_cast<size_t>(width) * static_cast<size_t>(height);
	}
};

class Image {
public:
	Image();
	virtual ~Image() = default;

	bool isGLLoaded = false;
	mutable std::atomic<int64_t> lastaccess;
	uint32_t generation_id = 0;

	// Marca o acesso para o LRU do atlas. A versao sem argumento le o relogio
	// cacheado do GraphicManager e por isso mora no .cpp (dependeria de gui.h aqui).
	void visit() const;

	// Mesma coisa, com o instante ja em maos: o fast path de
	// GameSprite::getAtlasRegion chama isto uma vez por sprite simples desenhado,
	// e o build nao usa LTCG -- fora de linha, seria a chamada nao inlinavel que o
	// resto deste trabalho esta justamente removendo do laco.
	void visit(int64_t now) const {
		lastaccess.store(now, std::memory_order_relaxed);
	}
	// Tamanho deste sprite na fonte. 32x32 salvo nas folhas 12+/13.
	[[nodiscard]] virtual ImageDimensions getDimensions() const {
		return {};
	}

	virtual void clean(time_t time, int longevity);

	virtual std::unique_ptr<uint8_t[]> getRGBData() = 0;
	virtual std::unique_ptr<uint8_t[]> getRGBAData() = 0;

	virtual bool isNormalImage() const {
		return false;
	}

protected:
	// Os pixels que sobem para o atlas do mapa quando nao veio nada do preloader.
	// Por padrao os mesmos de getRGBAData(), na escala do editor. Quem le a arte
	// na resolucao da folha (NormalImage/TemplateImage com o conjunto dobrado do
	// battle royale) corrige dimensions e asset_scale.
	virtual std::unique_ptr<uint8_t[]> getAtlasRGBAData(ImageDimensions& dimensions, int& asset_scale) {
		(void)dimensions;
		(void)asset_scale;
		return getRGBAData();
	}

	// Helper to handle atlas interactions
	// dimensions descreve o que ha em preloaded_data (ou o que getRGBAData vai
	// devolver). 32x32 para o .spr classico; as folhas 12+/13 tambem trazem
	// 32x64, 64x32 e 64x64. asset_scale: pixels por unidade do mundo nesses
	// dados (2 na arte de 64 px por casa) -- vira o draw_width/height da regiao.
	const AtlasRegion* EnsureAtlasSprite(uint32_t sprite_id, std::unique_ptr<uint8_t[]> preloaded_data = nullptr, ImageDimensions dimensions = {}, int asset_scale = 1);
};

#endif
