#include "rendering/drawers/overlays/stair_direction_drawer.h"
#include <nanovg.h>
#include "rendering/core/render_view.h"
#include "editor/editor.h"
#include "map/tile.h"
#include "game/item.h"
#include "item_definitions/core/item_definition_store.h"

namespace {

// Arrow dimensions (unscaled)
constexpr float BODY_LENGTH = 22.0f;
constexpr float BODY_WIDTH = 14.0f;
constexpr float HEAD_LENGTH = 16.0f;
constexpr float HEAD_WIDTH = 26.0f;

struct StairInfo {
	float cx, cy;
	int direction; // 0=N, 1=S, 2=E, 3=W, 4=Down
};

void drawArrow(NVGcontext* vg, float cx, float cy, int direction, float scale, float outlineGrow) {
	float bl = (BODY_LENGTH + outlineGrow) * scale;
	float bw = (BODY_WIDTH + outlineGrow) * scale / 2.0f;
	float hl = (HEAD_LENGTH + outlineGrow) * scale;
	float hw = (HEAD_WIDTH + outlineGrow) * scale / 2.0f;
	float totalLen = bl + hl;
	float start = totalLen / 2.0f;

	nvgBeginPath(vg);

	switch (direction) {
		case 0: // North
			// Body
			nvgMoveTo(vg, cx - bw, cy + start);
			nvgLineTo(vg, cx + bw, cy + start);
			nvgLineTo(vg, cx + bw, cy + start - bl);
			nvgLineTo(vg, cx - bw, cy + start - bl);
			nvgClosePath(vg);
			nvgFill(vg);
			// Head
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx, cy - start);
			nvgLineTo(vg, cx - hw, cy + start - bl);
			nvgLineTo(vg, cx + hw, cy + start - bl);
			nvgClosePath(vg);
			nvgFill(vg);
			break;

		case 1: // South
		case 4: // Down (same visual)
			nvgMoveTo(vg, cx - bw, cy - start);
			nvgLineTo(vg, cx + bw, cy - start);
			nvgLineTo(vg, cx + bw, cy - start + bl);
			nvgLineTo(vg, cx - bw, cy - start + bl);
			nvgClosePath(vg);
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx, cy + start);
			nvgLineTo(vg, cx - hw, cy - start + bl);
			nvgLineTo(vg, cx + hw, cy - start + bl);
			nvgClosePath(vg);
			nvgFill(vg);
			break;

		case 2: // East
			nvgMoveTo(vg, cx - start, cy - bw);
			nvgLineTo(vg, cx - start, cy + bw);
			nvgLineTo(vg, cx - start + bl, cy + bw);
			nvgLineTo(vg, cx - start + bl, cy - bw);
			nvgClosePath(vg);
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx + start, cy);
			nvgLineTo(vg, cx - start + bl, cy - hw);
			nvgLineTo(vg, cx - start + bl, cy + hw);
			nvgClosePath(vg);
			nvgFill(vg);
			break;

		case 3: // West
			nvgMoveTo(vg, cx + start, cy - bw);
			nvgLineTo(vg, cx + start, cy + bw);
			nvgLineTo(vg, cx + start - bl, cy + bw);
			nvgLineTo(vg, cx + start - bl, cy - bw);
			nvgClosePath(vg);
			nvgFill(vg);
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx - start, cy);
			nvgLineTo(vg, cx + start - bl, cy - hw);
			nvgLineTo(vg, cx + start - bl, cy + hw);
			nvgClosePath(vg);
			nvgFill(vg);
			break;
	}
}

} // namespace

void StairDirectionDrawer::draw(NVGcontext* vg, const RenderView& view, Editor& editor) {
	if (!vg) return;

	nvgSave(vg);

	const float zoom = view.zoom;
	const float TILE = 32.0f / zoom;
	const float halfTile = TILE / 2.0f;
	const float scale = 1.0f / zoom;

	// Collect stairs
	std::vector<StairInfo> stairs;
	stairs.reserve(64);

	for (int y = view.start_y; y <= view.end_y; ++y) {
		for (int x = view.start_x; x <= view.end_x; ++x) {
			Tile* tile = editor.map.getTile(x, y, view.floor);
			if (!tile) continue;

			for (const auto& item : tile->items) {
				if (!item) continue;

				auto def = g_item_definitions.get(item->getID());
				if (!def || !def.isFloorChange()) continue;

				int ux, uy;
				if (!view.IsTileVisible(x, y, view.floor, ux, uy)) break;

				float sx = ux / zoom;
				float sy = uy / zoom;

				int direction = -1;
				if (def.hasFlag(ItemFlag::FloorChangeNorth)) direction = 0;
				else if (def.hasFlag(ItemFlag::FloorChangeSouth)) direction = 1;
				else if (def.hasFlag(ItemFlag::FloorChangeEast)) direction = 2;
				else if (def.hasFlag(ItemFlag::FloorChangeWest)) direction = 3;
				else if (def.hasFlag(ItemFlag::FloorChangeDown)) direction = 4;

				if (direction >= 0) {
					stairs.push_back({ sx + halfTile, sy + halfTile, direction });
				}
				break; // Only one arrow per tile
			}
		}
	}

	// Draw black outlines first
	nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
	for (const auto& stair : stairs) {
		drawArrow(vg, stair.cx, stair.cy, stair.direction, scale, 3.0f);
	}

	// Draw red fill on top
	nvgFillColor(vg, nvgRGBA(220, 50, 50, 255));
	for (const auto& stair : stairs) {
		drawArrow(vg, stair.cx, stair.cy, stair.direction, scale, 0.0f);
	}

	nvgRestore(vg);
}
