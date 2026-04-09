#include "rendering/drawers/overlays/item_indicator_drawer.h"
#include <nanovg.h>
#include "rendering/core/render_view.h"
#include "rendering/utilities/icon_renderer.h"
#include "util/image_manager.h"

ItemIndicatorDrawer::ItemIndicatorDrawer() {
	requests.reserve(200);
}

ItemIndicatorDrawer::~ItemIndicatorDrawer() = default;

void ItemIndicatorDrawer::addIndicator(const Position& pos, IndicatorType type, bool isHouseTile) {
	requests.push_back({ pos, type, isHouseTile });
}

void ItemIndicatorDrawer::clear() {
	requests.clear();
}

void ItemIndicatorDrawer::draw(NVGcontext* vg, const RenderView& view) {
	if (requests.empty() || !vg) {
		return;
	}

	nvgSave(vg);

	// Colors matching BT_MAPEDITORv3 style
	const NVGcolor colorPickupable = nvgRGBA(0, 200, 255, 230);   // Cyan-blue
	const NVGcolor colorMoveable = nvgRGBA(255, 165, 0, 230);     // Orange
	const NVGcolor colorBoth = nvgRGBA(180, 100, 255, 230);       // Purple (both)
	const NVGcolor colorHousePickupable = nvgRGBA(255, 80, 80, 230);  // Red for house tiles
	const NVGcolor colorHouseMoveable = nvgRGBA(255, 80, 80, 230);    // Red for house tiles
	const NVGcolor colorHouseBoth = nvgRGBA(255, 80, 80, 230);        // Red for house tiles

	const float zoomFactor = 1.0f / view.zoom;
	const float iconSize = 14.0f * zoomFactor;
	const float outlineOffset = 1.0f * zoomFactor;

	for (const auto& request : requests) {
		if (request.pos.z != view.floor) {
			continue;
		}

		int unscaled_x, unscaled_y;
		if (!view.IsTileVisible(request.pos.x, request.pos.y, request.pos.z, unscaled_x, unscaled_y)) {
			continue;
		}

		const float zoom = view.zoom;
		const float x = unscaled_x / zoom;
		const float y = unscaled_y / zoom;
		const float TILE_SIZE = 32.0f / zoom;

		// Position in the bottom-right corner of the tile
		float iconX = x + TILE_SIZE - iconSize * 0.7f;
		float iconY = y + TILE_SIZE - iconSize * 0.7f;

		switch (request.type) {
			case IndicatorType::Pickupable: {
				const NVGcolor& color = request.isHouseTile ? colorHousePickupable : colorPickupable;
				IconRenderer::DrawIconWithBorder(vg, iconX, iconY, iconSize, outlineOffset, ICON_HAND_POINTER_SOLID, color);
				break;
			}
			case IndicatorType::Moveable: {
				const NVGcolor& color = request.isHouseTile ? colorHouseMoveable : colorMoveable;
				IconRenderer::DrawIconWithBorder(vg, iconX, iconY, iconSize, outlineOffset, ICON_ARROWS_UP_DOWN_LEFT_RIGHT, color);
				break;
			}
			case IndicatorType::PickupableAndMoveable: {
				const NVGcolor& color = request.isHouseTile ? colorHouseBoth : colorBoth;
				// Draw both icons side by side
				float offsetX = iconSize * 0.55f;
				IconRenderer::DrawIconWithBorder(vg, iconX - offsetX, iconY, iconSize, outlineOffset, ICON_HAND_POINTER_SOLID, color);
				IconRenderer::DrawIconWithBorder(vg, iconX + offsetX, iconY, iconSize, outlineOffset, ICON_ARROWS_UP_DOWN_LEFT_RIGHT, color);
				break;
			}
		}
	}

	nvgRestore(vg);
}
