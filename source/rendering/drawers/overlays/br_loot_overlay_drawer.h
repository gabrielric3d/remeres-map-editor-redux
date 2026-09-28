//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// Battle Royale: everything the loot zones draw on top of the map, besides the
// tint (that one is baked with the tiles, TileColorCalculator).
//
//   GL pass (sprite batch)  -- the items placed by hand, as their own sprite.
//   NanoVG pass             -- solid fill (optional), the zone outline, the zone id
//                              ("3-231", with a chest), and a frame around each
//                              placed item with its amount/chance.
//
// The outline is what the script version could not do well: it follows the tile
// edges where the zone changes, in the tier color, drawn INSIDE each zone -- two
// touching zones get two parallel lines, one of each color, instead of one line
// that belongs to nobody. It is sized in window pixels, so it reads at any zoom.

#ifndef RME_BR_LOOT_OVERLAY_DRAWER_H_
#define RME_BR_LOOT_OVERLAY_DRAWER_H_

#include "rendering/core/render_view.h"

#include <cstdint>
#include <vector>

class Editor;
class ItemDrawer;
class SpriteBatch;
class SpriteDrawer;
class ZoneLabelDrawer;
struct DrawingOptions;
struct NVGcontext;

class BRLootOverlayDrawer {
public:
	BRLootOverlayDrawer() = default;
	~BRLootOverlayDrawer() = default;

	void drawItemSprites(SpriteBatch& sprite_batch, SpriteDrawer* sprite_drawer, ItemDrawer* item_drawer, const RenderView& view, Editor& editor);
	void draw(NVGcontext* vg, const RenderView& view, const DrawingOptions& options, Editor& editor, ZoneLabelDrawer& label_drawer);

private:
	// Zone id of every tile of the visible window (plus a one-tile ring for the
	// neighbour test), read once per frame and shared by the passes.
	std::vector<uint32_t> grid;
};

#endif
