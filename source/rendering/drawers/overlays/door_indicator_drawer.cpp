#include "rendering/drawers/overlays/door_indicator_drawer.h"
#include <nanovg.h>
#include "rendering/core/render_view.h"
#include "util/image_manager.h"

DoorIndicatorDrawer::DoorIndicatorDrawer() {
	requests.reserve(100);
}

DoorIndicatorDrawer::~DoorIndicatorDrawer() = default;

void DoorIndicatorDrawer::addDoor(const Position& pos, bool locked, bool south, bool east) {
	requests.push_back({ pos, locked, south, east });
}

void DoorIndicatorDrawer::clear() {
	requests.clear();
}

void DoorIndicatorDrawer::draw(NVGcontext* vg, const RenderView& view) {
	if (requests.empty() || !vg) {
		return;
	}

	nvgSave(vg);

	const NVGcolor colorLocked = nvgRGBA(255, 0, 0, 255); // Red
	const NVGcolor colorUnlocked = nvgRGBA(102, 255, 0, 255); // Green (#66ff00)
	const wxColour wxBlack(0, 0, 0);

	const float zoomFactor = 1.0f / view.zoom;
	const float iconSize = 12.0f * zoomFactor;
	const float outlineOffset = 1.0f * zoomFactor;

	auto drawIconWithBorder = [&](float cx, float cy, const std::string& iconMacro, const NVGcolor& tintColor) {
		const wxColour wxTint(tintColor.r * 255, tintColor.g * 255, tintColor.b * 255);
		int imgBlack = IMAGE_MANAGER.GetNanoVGImage(vg, iconMacro, wxBlack);
		int imgTint = IMAGE_MANAGER.GetNanoVGImage(vg, iconMacro, wxTint);

		if (imgBlack > 0 && imgTint > 0) {
			// Draw outline (4 directions)
			float offsets[4][2] = {
				{ -outlineOffset, 0 }, { outlineOffset, 0 }, { 0, -outlineOffset }, { 0, outlineOffset }
			};

			for (int i = 0; i < 4; ++i) {
				float ox = cx + offsets[i][0];
				float oy = cy + offsets[i][1];
				NVGpaint paint = nvgImagePattern(vg, ox - iconSize / 2.0f, oy - iconSize / 2.0f, iconSize, iconSize, 0, imgBlack, 1.0f);
				nvgBeginPath(vg);
				nvgRect(vg, ox - iconSize / 2.0f, oy - iconSize / 2.0f, iconSize, iconSize);
				nvgFillPaint(vg, paint);
				nvgFill(vg);
			}

			// Draw main icon
			NVGpaint paint = nvgImagePattern(vg, cx - iconSize / 2.0f, cy - iconSize / 2.0f, iconSize, iconSize, 0, imgTint, 1.0f);
			nvgBeginPath(vg);
			nvgRect(vg, cx - iconSize / 2.0f, cy - iconSize / 2.0f, iconSize, iconSize);
			nvgFillPaint(vg, paint);
			nvgFill(vg);
		}
	};

	for (const auto& request : requests) {
		// Only render doors on the current floor
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
		const float tileSize = 32.0f / zoom;

		const std::string icon = request.locked ? ICON_LOCK : ICON_LOCK_OPEN;
		const NVGcolor color = request.locked ? colorLocked : colorUnlocked;

		if (request.south) {
			// Center of WEST border
			drawIconWithBorder(x, y + tileSize / 2.0f, icon, color);
		}
		if (request.east) {
			// Center of NORTH border
			drawIconWithBorder(x + tileSize / 2.0f, y, icon, color);
		}
	}

	nvgRestore(vg);
}
