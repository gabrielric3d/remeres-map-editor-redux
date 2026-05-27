#include "rendering/drawers/overlays/stair_direction_drawer.h"
#include <nanovg.h>
#include "rendering/core/render_view.h"
#include "editor/editor.h"
#include "map/tile.h"
#include "game/item.h"
#include "game/stair_overlays.h"
#include "item_definitions/core/item_definition_store.h"

namespace {

// Arrow dimensions (unscaled)
constexpr float BODY_LENGTH = 22.0f;
constexpr float BODY_WIDTH = 14.0f;
constexpr float HEAD_LENGTH = 16.0f;
constexpr float HEAD_WIDTH = 26.0f;

// Direction codes: 0=N, 1=S, 2=E, 3=W, 4=Down, 5=UpDouble, 6=DownDouble
struct StairInfo {
	float cx, cy;
	int direction;
};

int resolveDirection(const Item* item) {
	if (!item) return -1;
	const uint16_t id = item->getID();
	auto def = g_item_definitions.get(id);
	if (def && def.isFloorChange()) {
		if (def.hasFlag(ItemFlag::FloorChangeNorth)) return 0;
		if (def.hasFlag(ItemFlag::FloorChangeSouth)) return 1;
		if (def.hasFlag(ItemFlag::FloorChangeEast))  return 2;
		if (def.hasFlag(ItemFlag::FloorChangeWest))  return 3;
		if (def.hasFlag(ItemFlag::FloorChangeDown))  return 4;
	}
	return g_stair_overlays.getDirection(id);
}

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

		case 5: { // Up double (normal stairs that go up a floor)
			// Compact body + two stacked arrowheads pointing north.
			float shortBody = bl * 0.45f;
			float headGap = hl * 0.95f;
			// Body (bottom half of the tile)
			nvgMoveTo(vg, cx - bw, cy + start);
			nvgLineTo(vg, cx + bw, cy + start);
			nvgLineTo(vg, cx + bw, cy + start - shortBody);
			nvgLineTo(vg, cx - bw, cy + start - shortBody);
			nvgClosePath(vg);
			nvgFill(vg);
			// First head (lower)
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx, cy + start - shortBody - hl);
			nvgLineTo(vg, cx - hw, cy + start - shortBody);
			nvgLineTo(vg, cx + hw, cy + start - shortBody);
			nvgClosePath(vg);
			nvgFill(vg);
			// Second head (upper, offset up)
			float h2Bottom = cy + start - shortBody - headGap;
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx, h2Bottom - hl);
			nvgLineTo(vg, cx - hw, h2Bottom);
			nvgLineTo(vg, cx + hw, h2Bottom);
			nvgClosePath(vg);
			nvgFill(vg);
			break;
		}

		case 6: { // Down double (normal stairs that go down a floor)
			// Compact body + two stacked arrowheads pointing south.
			float shortBody = bl * 0.45f;
			float headGap = hl * 0.95f;
			// Body
			nvgMoveTo(vg, cx - bw, cy - start);
			nvgLineTo(vg, cx + bw, cy - start);
			nvgLineTo(vg, cx + bw, cy - start + shortBody);
			nvgLineTo(vg, cx - bw, cy - start + shortBody);
			nvgClosePath(vg);
			nvgFill(vg);
			// First head (upper)
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx, cy - start + shortBody + hl);
			nvgLineTo(vg, cx - hw, cy - start + shortBody);
			nvgLineTo(vg, cx + hw, cy - start + shortBody);
			nvgClosePath(vg);
			nvgFill(vg);
			// Second head (lower, offset down)
			float h2Top = cy - start + shortBody + headGap;
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx, h2Top + hl);
			nvgLineTo(vg, cx - hw, h2Top);
			nvgLineTo(vg, cx + hw, h2Top);
			nvgClosePath(vg);
			nvgFill(vg);
			break;
		}
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

			int direction = resolveDirection(tile->ground.get());
			if (direction < 0) {
				for (const auto& item : tile->items) {
					direction = resolveDirection(item.get());
					if (direction >= 0) break;
				}
			}

			if (direction < 0) continue;

			int ux, uy;
			if (!view.IsTileVisible(x, y, view.floor, ux, uy)) continue;

			float sx = ux / zoom;
			float sy = uy / zoom;

			stairs.push_back({ sx + halfTile, sy + halfTile, direction });
		}
	}

	// Draw black outlines first
	nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
	for (const auto& stair : stairs) {
		drawArrow(vg, stair.cx, stair.cy, stair.direction, scale, 3.0f);
	}

	// Fill: red for auto floor-change (directions 0..4), blue for normal stairs (5/6)
	for (const auto& stair : stairs) {
		const bool isNormalStair = stair.direction >= 5;
		nvgFillColor(vg, isNormalStair ? nvgRGBA(60, 140, 230, 255) : nvgRGBA(220, 50, 50, 255));
		drawArrow(vg, stair.cx, stair.cy, stair.direction, scale, 0.0f);
	}

	nvgRestore(vg);
}
