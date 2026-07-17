#ifndef RME_RENDERING_TILE_RENDERER_H_
#define RME_RENDERING_TILE_RENDERER_H_

#include <memory>
#include <sstream>
#include <stdint.h>

class TileLocation;
class Tile;
class Item;
struct RenderView;
struct DrawingOptions;
class Editor;
class ItemDrawer;
class SpriteDrawer;
class CreatureDrawer;
class CreatureNameDrawer;
class FloorDrawer;
class MarkerDrawer;
class TooltipDrawer;
struct LightBuffer;
class SpriteBatch;
class PrimitiveRenderer;
struct SpritePatterns;
class ItemDefinitionView;

// The client renders each floor in layered passes: every ground first, then
// every ground border, then walls/items/creatures. Sprites that overhang a
// neighbouring tile (draw offsets, big sprites) must not be covered by that
// neighbour's lower-order sprites, so the map layer runs three passes per
// floor instead of drawing each tile completely in map order.
enum class TileRenderPass {
	All, // single pass: everything (legacy behavior)
	Ground, // ground item only
	Borders, // ground borders (always-on-bottom, top order 1) only
	Contents // everything else (walls, items, creatures, overlays)
};

class TileRenderer {
public:
	TileRenderer(ItemDrawer* id, SpriteDrawer* sd, CreatureDrawer* cd, CreatureNameDrawer* cnd, FloorDrawer* fd, MarkerDrawer* md, TooltipDrawer* td, Editor* ed);

	void DrawTile(SpriteBatch& sprite_batch, TileLocation* location, const RenderView& view, const DrawingOptions& options, uint32_t current_house_id, int in_draw_x = -1, int in_draw_y = -1, LightBuffer* light_buffer = nullptr, TileRenderPass pass = TileRenderPass::All);

private:
	void PreloadItem(const Tile* tile, Item* item, const ItemDefinitionView& definition, const SpritePatterns* patterns = nullptr);

	ItemDrawer* item_drawer;
	SpriteDrawer* sprite_drawer;
	CreatureDrawer* creature_drawer;
	FloorDrawer* floor_drawer;
	MarkerDrawer* marker_drawer;
	TooltipDrawer* tooltip_drawer;
	CreatureNameDrawer* creature_name_drawer;
	Editor* editor;
};

#endif
