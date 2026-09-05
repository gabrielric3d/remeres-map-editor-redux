//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_FLOOR_DRAWER_H_
#define RME_RENDERING_FLOOR_DRAWER_H_

class MapDrawer;
struct RenderView;
struct DrawingOptions;
class Editor;

class ItemDrawer;
class SpriteDrawer;

class CreatureDrawer;
class SpriteBatch;
class PrimitiveRenderer;

class FloorDrawer {
public:
	FloorDrawer();
	~FloorDrawer();

	// Draws the translucent "ghost" floors on top of the normal view: the single floor
	// above for Ghost Higher Floors (Ctrl+L) and/or the configured floors above and
	// below for the radial wheel's Ghost Floors.
	void draw(SpriteBatch& sprite_batch, ItemDrawer* item_drawer, SpriteDrawer* sprite_drawer, CreatureDrawer* creature_drawer, const RenderView& view, const DrawingOptions& options, Editor& editor);

private:
	// One floor, every visible tile, blended with `alpha`. `draw_offset` is the
	// parallax shift (in pixels) of that floor relative to the scroll origin.
	void drawGhostFloor(SpriteBatch& sprite_batch, ItemDrawer* item_drawer, SpriteDrawer* sprite_drawer, CreatureDrawer* creature_drawer, const RenderView& view, const DrawingOptions& options, Editor& editor, int map_z, int draw_offset, int alpha);
};

#endif
