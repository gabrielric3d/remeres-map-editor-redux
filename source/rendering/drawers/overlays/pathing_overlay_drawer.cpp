#include "rendering/drawers/overlays/pathing_overlay_drawer.h"
#include <nanovg.h>
#include "rendering/core/render_view.h"
#include "editor/editor.h"
#include "map/tile.h"

void PathingOverlayDrawer::draw(NVGcontext* vg, const RenderView& view, Editor& editor) {
	if (!vg) return;

	nvgSave(vg);

	const float zoom = view.zoom;
	const float TILE = 32.0f / zoom;
	const float inset = 2.0f / zoom;
	const NVGcolor overlayColor = nvgRGBA(120, 16, 16, 80);
	const NVGcolor indicatorColor = nvgRGBA(255, 80, 60, 200);

	nvgFillColor(vg, overlayColor);
	nvgStrokeColor(vg, indicatorColor);
	nvgStrokeWidth(vg, 1.5f / zoom);

	for (int y = view.start_y; y <= view.end_y; ++y) {
		for (int x = view.start_x; x <= view.end_x; ++x) {
			Tile* tile = editor.map.getTile(x, y, view.floor);
			if (!tile || tile->size() == 0 || !tile->isBlocking()) continue;

			int ux, uy;
			if (!view.IsTileVisible(x, y, view.floor, ux, uy)) continue;

			float sx = ux / zoom;
			float sy = uy / zoom;

			nvgBeginPath(vg);
			nvgRect(vg, sx, sy, TILE, TILE);
			nvgFill(vg);

			// X pattern indicator at the tile position
			nvgBeginPath(vg);
			nvgMoveTo(vg, sx + inset, sy + inset);
			nvgLineTo(vg, sx + TILE - inset, sy + TILE - inset);
			nvgMoveTo(vg, sx + TILE - inset, sy + inset);
			nvgLineTo(vg, sx + inset, sy + TILE - inset);
			nvgStroke(vg);

			// Tile border
			nvgBeginPath(vg);
			nvgRect(vg, sx, sy, TILE, TILE);
			nvgStroke(vg);
		}
	}

	nvgRestore(vg);
}
