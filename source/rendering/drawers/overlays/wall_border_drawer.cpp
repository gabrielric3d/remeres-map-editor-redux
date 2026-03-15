#include "rendering/drawers/overlays/wall_border_drawer.h"
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

static bool isWallTile(const Tile* t) {
	return t && t->hasWall();
}

void WallBorderDrawer::draw(NVGcontext* vg, const RenderView& view, Editor& editor) {
	if (!vg) return;

	nvgSave(vg);

	const float zoom = view.zoom;
	const float TILE = 32.0f / zoom;
	const float halfTile = TILE / 2.0f;
	const NVGcolor borderColor = nvgRGBA(220, 220, 0, 200);

	nvgStrokeColor(vg, borderColor);
	nvgStrokeWidth(vg, 2.0f / zoom);

	for (int y = view.start_y; y <= view.end_y; ++y) {
		for (int x = view.start_x; x <= view.end_x; ++x) {
			Tile* tile = editor.map.getTile(x, y, view.floor);
			if (!tile) continue;

			bool mountain = isMountainTile(tile);
			bool wall = isWallTile(tile);
			if (!mountain && !wall) continue;

			int ux, uy;
			if (!view.IsTileVisible(x, y, view.floor, ux, uy)) continue;

			float sx = ux / zoom;
			float sy = uy / zoom;

			if (mountain) {
				bool nN = isMountainTile(editor.map.getTile(x, y - 1, view.floor));
				bool nS = isMountainTile(editor.map.getTile(x, y + 1, view.floor));
				bool nW = isMountainTile(editor.map.getTile(x - 1, y, view.floor));
				bool nE = isMountainTile(editor.map.getTile(x + 1, y, view.floor));

				if (!nN) { nvgBeginPath(vg); nvgMoveTo(vg, sx, sy); nvgLineTo(vg, sx + TILE, sy); nvgStroke(vg); }
				if (!nS) { nvgBeginPath(vg); nvgMoveTo(vg, sx, sy + TILE); nvgLineTo(vg, sx + TILE, sy + TILE); nvgStroke(vg); }
				if (!nW) { nvgBeginPath(vg); nvgMoveTo(vg, sx, sy); nvgLineTo(vg, sx, sy + TILE); nvgStroke(vg); }
				if (!nE) { nvgBeginPath(vg); nvgMoveTo(vg, sx + TILE, sy); nvgLineTo(vg, sx + TILE, sy + TILE); nvgStroke(vg); }
			} else if (wall) {
				bool wN = isWallTile(editor.map.getTile(x, y - 1, view.floor));
				bool wS = isWallTile(editor.map.getTile(x, y + 1, view.floor));
				bool wW = isWallTile(editor.map.getTile(x - 1, y, view.floor));
				bool wE = isWallTile(editor.map.getTile(x + 1, y, view.floor));

				float cx = sx + halfTile;
				float cy = sy + halfTile;

				nvgBeginPath(vg);
				if (wW) { nvgMoveTo(vg, sx, cy); nvgLineTo(vg, cx, cy); }
				if (wE) { nvgMoveTo(vg, cx, cy); nvgLineTo(vg, sx + TILE, cy); }
				if (wN) { nvgMoveTo(vg, cx, sy); nvgLineTo(vg, cx, cy); }
				if (wS) { nvgMoveTo(vg, cx, cy); nvgLineTo(vg, cx, sy + TILE); }
				if (!wN && !wS && !wW && !wE) {
					nvgMoveTo(vg, sx, cy);
					nvgLineTo(vg, sx + TILE, cy);
				}
				nvgStroke(vg);
			}
		}
	}

	nvgRestore(vg);
}
