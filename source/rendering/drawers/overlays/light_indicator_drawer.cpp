#include "rendering/drawers/overlays/light_indicator_drawer.h"
#include <nanovg.h>
#include "rendering/core/render_view.h"
#include "rendering/utilities/icon_renderer.h"
#include "rendering/core/light_source_manager.h"
#include "util/image_manager.h"

LightIndicatorDrawer::LightIndicatorDrawer() {
	requests.reserve(100);
}

LightIndicatorDrawer::~LightIndicatorDrawer() = default;

void LightIndicatorDrawer::addLight(const Position& pos, uint16_t clientId) {
	requests.push_back({ pos, clientId });
}

void LightIndicatorDrawer::clear() {
	requests.clear();
}

void LightIndicatorDrawer::draw(NVGcontext* vg, const RenderView& view) {
	if (requests.empty() || !vg) {
		return;
	}

	nvgSave(vg);

	const auto& mgr = LightSourceManager::instance();
	const float zoomFactor = 1.0f / view.zoom;
	const float iconSize = 24.0f * zoomFactor;
	const float outlineOffset = 1.0f * zoomFactor;
	const float fontSize = 10.0f;

	for (const auto& request : requests) {
		if (request.pos.z != view.floor) {
			continue;
		}

		int unscaled_x, unscaled_y;
		if (!view.IsTileVisible(request.pos.x, request.pos.y, request.pos.z, unscaled_x, unscaled_y)) {
			continue;
		}

		const LightSourceEntry* entry = mgr.find(request.clientId);
		if (!entry) {
			continue;
		}

		const float zoom = view.zoom;
		const float x = unscaled_x / zoom;
		const float y = unscaled_y / zoom;
		const float TILE_SIZE = 32.0f / zoom;

		const NVGcolor color = nvgRGBA(entry->r, entry->g, entry->b, 255);
		const float centerX = x + TILE_SIZE / 2.0f;
		const float centerY = y + TILE_SIZE / 2.0f;

		// Draw lightbulb icon
		IconRenderer::DrawIconWithBorder(vg, centerX, centerY, iconSize, outlineOffset, ICON_LIGHTBULB_SOLID, color);

		// Draw label below icon
		if (!entry->label.empty()) {
			const float labelY = centerY + iconSize / 2.0f + 2.0f * zoomFactor;

			nvgFontSize(vg, fontSize);
			nvgFontFace(vg, "sans");
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

			float textBounds[4];
			nvgTextBounds(vg, 0, 0, entry->label.c_str(), nullptr, textBounds);
			float textWidth = textBounds[2] - textBounds[0];
			float textHeight = textBounds[3] - textBounds[1];

			float paddingX = 3.0f;
			float paddingY = 1.0f;

			// Background pill
			nvgBeginPath(vg);
			nvgRoundedRect(vg,
				centerX - textWidth / 2.0f - paddingX,
				labelY - paddingY,
				textWidth + paddingX * 2.0f,
				textHeight + paddingY * 2.0f,
				3.0f);
			nvgFillColor(vg, nvgRGBA(0, 0, 0, 180));
			nvgFill(vg);

			// Text in entry color
			nvgFillColor(vg, color);
			nvgText(vg, centerX, labelY, entry->label.c_str(), nullptr);
		}
	}

	nvgRestore(vg);
}
