#ifndef RME_PREVIEW_DRAWER_H_
#define RME_PREVIEW_DRAWER_H_

#include "rendering/core/render_view.h"
#include "rendering/core/drawing_options.h"
#include <cstdint>
#include <vector>

class Tile;
class MapCanvas;
class Editor;
class ItemDrawer;
class SpriteDrawer;
class CreatureDrawer;
class SpriteBatch;

class PrimitiveRenderer;

class PreviewDrawer {
	// Um tile do buffer com o lugar que ele ocupa no mapa, para poder ordenar antes
	// de desenhar.
	struct PreviewTile {
		int map_x;
		int map_y;
		Tile* tile;
	};

	// Reaproveitado entre chamadas (isto roda uma vez por andar, todo frame). Membro
	// e nao global: um global seria compartilhado por todas as instancias e ficaria
	// guardando Tile* de buffers ja destruidos entre um frame e outro.
	std::vector<PreviewTile> visible_tiles;

public:
	PreviewDrawer();
	~PreviewDrawer();

	void draw(SpriteBatch& sprite_batch, MapCanvas* canvas, const RenderView& view, int map_z, const DrawingOptions& options, Editor& editor, ItemDrawer* item_drawer, SpriteDrawer* sprite_drawer, CreatureDrawer* creature_drawer, uint32_t current_house_id);
};

#endif
