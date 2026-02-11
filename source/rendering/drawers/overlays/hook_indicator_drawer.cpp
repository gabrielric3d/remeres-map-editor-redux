#include "rendering/drawers/overlays/hook_indicator_drawer.h"
#include <nanovg.h>
#include "rendering/core/render_view.h"
#include "app/definitions.h"
#include "util/image_manager.h"

HookIndicatorDrawer::HookIndicatorDrawer() {
	requests.reserve(100);
}

HookIndicatorDrawer::~HookIndicatorDrawer() = default;

void HookIndicatorDrawer::addHook(const Position& pos, bool south, bool east) {
	requests.push_back({ pos, south, east });
}

void HookIndicatorDrawer::clear() {
	requests.clear();
}

void HookIndicatorDrawer::draw(NVGcontext* vg, const RenderView& view) {
	if (requests.empty() || !vg) {
		return;
	}

	nvgSave(vg);

	// Style
	const NVGcolor tintColor = nvgRGBA(204, 255, 0, 255); // Fluorescent Yellow-Green (#ccff00)
	const wxColour wxTint(tintColor.r * 255, tintColor.g * 255, tintColor.b * 255);
	const wxColour wxBlack(0, 0, 0);

	const float zoomFactor = 1.0f / view.zoom;
	const float iconSize = 24.0f * zoomFactor;
	const float outlineOffset = 1.0f * zoomFactor;

	for (const auto& request : requests) {
		// Only render hooks on the current floor
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

		auto drawIconWithBorder = [&](float cx, float cy, const std::string& iconMacro) {
			int imgBlack = IMAGE_MANAGER.GetNanoVGImage(vg, iconMacro, wxBlack);
			int imgGreen = IMAGE_MANAGER.GetNanoVGImage(vg, iconMacro, wxTint);

			if (imgBlack > 0 && imgGreen > 0) {
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
				NVGpaint paint = nvgImagePattern(vg, cx - iconSize / 2.0f, cy - iconSize / 2.0f, iconSize, iconSize, 0, imgGreen, 1.0f);
				nvgBeginPath(vg);
				nvgRect(vg, cx - iconSize / 2.0f, cy - iconSize / 2.0f, iconSize, iconSize);
				nvgFillPaint(vg, paint);
				nvgFill(vg);
			}
		};

		if (request.south) {
			// Center of WEST border, pointing NORTH (towards corner)
			drawIconWithBorder(x, y + tileSize / 2.0f, ICON_ANGLE_UP);
		}

		if (request.east) {
			// Center of NORTH border, pointing WEST (towards corner)
			drawIconWithBorder(x + tileSize / 2.0f, y, ICON_ANGLE_LEFT);
		}
	}

	nvgRestore(vg);
}
