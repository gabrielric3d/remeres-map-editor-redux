#include "rendering/drawers/overlays/mountain_overlay_drawer.h"
#include <nanovg.h>
#include "rendering/core/render_view.h"
#include "editor/editor.h"
#include "map/tile.h"
#include "brushes/ground/ground_brush.h"

static constexpr int MOUNTAIN_Z_ORDER_THRESHOLD = 9000;

static bool isMountainTile(const Tile* t) {
	if (!t || !t->ground) return false;
	GroundBrush* gb = t->ground->getGroundBrush();
	return gb && gb->getZ() >= MOUNTAIN_Z_ORDER_THRESHOLD;
}

void MountainOverlayDrawer::draw(NVGcontext* vg, const RenderView& view, Editor& editor) {
	if (!vg) return;

	nvgSave(vg);

	const float zoom = view.zoom;
	const float TILE = 32.0f / zoom;
	const NVGcolor overlayColor = nvgRGBA(0, 0, 0, 128);

	for (int y = view.start_y; y <= view.end_y; ++y) {
		for (int x = view.start_x; x <= view.end_x; ++x) {
			Tile* tile = editor.map.getTile(x, y, view.floor);
			if (!tile || !isMountainTile(tile)) continue;

			// Mountain ground at (x,y) renders visually at (x+1, y+1) due to isometric offset
			int draw_x = x + 1;
			int draw_y = y + 1;

			int ux, uy;
			if (!view.IsTileVisible(draw_x, draw_y, view.floor, ux, uy)) continue;

			float sx = ux / zoom;
			float sy = uy / zoom;

			nvgBeginPath(vg);
			nvgRect(vg, sx, sy, TILE, TILE);
			nvgFillColor(vg, overlayColor);
			nvgFill(vg);
		}
	}

	nvgRestore(vg);
}
