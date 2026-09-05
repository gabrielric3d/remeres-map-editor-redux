#ifndef RME_RENDERING_CORE_IMAGE_H_
#define RME_RENDERING_CORE_IMAGE_H_

#include <atomic>
#include <memory>
#include <cstdint>
#include "rendering/core/atlas_manager.h" // For AtlasRegion

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
	virtual void clean(time_t time, int longevity);

	virtual std::unique_ptr<uint8_t[]> getRGBData() = 0;
	virtual std::unique_ptr<uint8_t[]> getRGBAData() = 0;

	virtual bool isNormalImage() const {
		return false;
	}

protected:
	// Helper to handle atlas interactions
	const AtlasRegion* EnsureAtlasSprite(uint32_t sprite_id, std::unique_ptr<uint8_t[]> preloaded_data = nullptr);
};

#endif
