#ifndef RME_RENDERING_CORE_TEMPLATE_IMAGE_H_
#define RME_RENDERING_CORE_TEMPLATE_IMAGE_H_

#include "rendering/core/image.h"
#include "game/outfit.h"

class GameSprite;

class TemplateImage : public Image {
public:
	TemplateImage(GameSprite* parent, int v, const Outfit& outfit);
	~TemplateImage() override;

	void clean(time_t time, int longevity) override;

	virtual std::unique_ptr<uint8_t[]> getRGBData() override;
	virtual std::unique_ptr<uint8_t[]> getRGBAData() override;

	// O tamanho e o do sprite base: nas folhas 12+/13 ele pode passar de 32x32,
	// e quem desenha o icone precisa saber disso para ler o buffer certo.
	[[nodiscard]] ImageDimensions getDimensions() const override;

	const AtlasRegion* getAtlasRegion();
	const AtlasRegion* atlas_region;

protected:
	// O outfit colorido na resolucao da folha (base + mascara lidos por
	// NormalImage::readAtlasRGBA), para o atlas do mapa.
	std::unique_ptr<uint8_t[]> getAtlasRGBAData(ImageDimensions& dimensions, int& asset_scale) override;

public:

	uint32_t texture_id; // Unique ID for AtlasManager key
	GameSprite* parent;
	int sprite_index;
	uint8_t lookHead;
	uint8_t lookBody;
	uint8_t lookLegs;
	uint8_t lookFeet;
};

#endif
