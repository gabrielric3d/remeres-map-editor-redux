//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/drawers/overlays/zone_overlay_drawer.h"
#include "rendering/core/primitive_renderer.h"
#include "rendering/core/render_view.h"
#include "rendering/core/forced_light_zone.h"
#include "app/definitions.h"
#include "util/common.h"
#include "ui/theme.h"
#include <nanovg.h>
#include <glm/glm.hpp>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void ZoneOverlayDrawer::draw(PrimitiveRenderer& renderer, const RenderView& view, const DrawingOptions& options, int floor) {
	if (!options.show_zone_boundaries) {
		return;
	}

	auto& zoneMgr = ForcedLightZoneManager::instance();
	if (!zoneMgr.isLoaded()) {
		return;
	}

	// Get visible area in tile coordinates
	int minX = view.view_scroll_x / TILE_SIZE - 2;
	int minY = view.view_scroll_y / TILE_SIZE - 2;
	int maxX = static_cast<int>((view.view_scroll_x + view.screensize_x * view.zoom) / TILE_SIZE) + 2;
	int maxY = static_cast<int>((view.view_scroll_y + view.screensize_y * view.zoom) / TILE_SIZE) + 2;

	auto visibleZones = zoneMgr.getZonesInArea(minX, minY, maxX, maxY, floor);

	for (const auto* zone : visibleZones) {
		if (zone->isCircular()) {
			drawCircularZone(renderer, *zone, view);
		} else {
			drawRectangularZone(renderer, *zone, view);
		}
	}
}

void ZoneOverlayDrawer::drawRectangularZone(PrimitiveRenderer& renderer, const ForcedLightZone& zone, const RenderView& view) {
	Position bmin = zone.getBoundsMin();
	Position bmax = zone.getBoundsMax();

	int draw_start_x, draw_start_y;
	view.getScreenPosition(bmin.x, bmin.y, bmin.z, draw_start_x, draw_start_y);

	int draw_end_x, draw_end_y;
	view.getScreenPosition(bmax.x + 1, bmax.y + 1, bmax.z, draw_end_x, draw_end_y);

	float x = static_cast<float>(draw_start_x);
	float y = static_cast<float>(draw_start_y);
	float w = static_cast<float>(draw_end_x - draw_start_x);
	float h = static_cast<float>(draw_end_y - draw_start_y);

	// Get zone color from ambientColor
	wxColor zc = colorFromEightBit(zone.ambientColor);
	float cr = zc.Red() / 255.0f;
	float cg = zc.Green() / 255.0f;
	float cb = zc.Blue() / 255.0f;

	// Semi-transparent fill
	glm::vec4 fill_color(cr, cg, cb, 0.08f);
	renderer.drawRect(glm::vec4(x, y, w, h), fill_color);

	// Border
	glm::vec4 border_color(cr, cg, cb, 0.7f);
	renderer.drawBox(glm::vec4(x, y, w, h), border_color, 2.0f);
}

void ZoneOverlayDrawer::drawCircularZone(PrimitiveRenderer& renderer, const ForcedLightZone& zone, const RenderView& view) {
	// For circular zones, approximate with line segments
	int center_draw_x, center_draw_y;
	view.getScreenPosition(zone.center.x, zone.center.y, zone.floor, center_draw_x, center_draw_y);

	float cx = static_cast<float>(center_draw_x) + static_cast<float>(TILE_SIZE) * 0.5f;
	float cy = static_cast<float>(center_draw_y) + static_cast<float>(TILE_SIZE) * 0.5f;
	float radius_px = static_cast<float>(zone.radius * TILE_SIZE);

	wxColor zc = colorFromEightBit(zone.ambientColor);
	float cr = zc.Red() / 255.0f;
	float cg = zc.Green() / 255.0f;
	float cb = zc.Blue() / 255.0f;

	glm::vec4 border_color(cr, cg, cb, 0.7f);

	// Draw circle as line segments
	const int segments = 64;
	for (int i = 0; i < segments; ++i) {
		float angle1 = static_cast<float>(i) / segments * 2.0f * static_cast<float>(M_PI);
		float angle2 = static_cast<float>(i + 1) / segments * 2.0f * static_cast<float>(M_PI);

		glm::vec2 p1(cx + std::cos(angle1) * radius_px, cy + std::sin(angle1) * radius_px);
		glm::vec2 p2(cx + std::cos(angle2) * radius_px, cy + std::sin(angle2) * radius_px);

		renderer.drawLine(p1, p2, border_color);
	}

	// Semi-transparent fill using triangles (fan from center)
	glm::vec4 fill_color(cr, cg, cb, 0.08f);
	glm::vec2 center_pt(cx, cy);
	for (int i = 0; i < segments; ++i) {
		float angle1 = static_cast<float>(i) / segments * 2.0f * static_cast<float>(M_PI);
		float angle2 = static_cast<float>(i + 1) / segments * 2.0f * static_cast<float>(M_PI);

		glm::vec2 p1(cx + std::cos(angle1) * radius_px, cy + std::sin(angle1) * radius_px);
		glm::vec2 p2(cx + std::cos(angle2) * radius_px, cy + std::sin(angle2) * radius_px);

		renderer.drawTriangle(center_pt, p1, p2, fill_color);
	}
}

void ZoneOverlayDrawer::drawLabels(NVGcontext* vg, const RenderView& view, const DrawingOptions& options, int floor) {
	if (!vg || !options.show_zone_boundaries) {
		return;
	}

	auto& zoneMgr = ForcedLightZoneManager::instance();
	if (!zoneMgr.isLoaded()) {
		return;
	}

	int minX = view.view_scroll_x / TILE_SIZE - 2;
	int minY = view.view_scroll_y / TILE_SIZE - 2;
	int maxX = static_cast<int>((view.view_scroll_x + view.screensize_x * view.zoom) / TILE_SIZE) + 2;
	int maxY = static_cast<int>((view.view_scroll_y + view.screensize_y * view.zoom) / TILE_SIZE) + 2;

	auto visibleZones = zoneMgr.getZonesInArea(minX, minY, maxX, maxY, floor);

	nvgFontSize(vg, 14.0f);
	nvgFontFace(vg, "sans");
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);

	for (const auto* zone : visibleZones) {
		if (zone->name.empty()) {
			continue;
		}

		Position bmin = zone->getBoundsMin();

		int draw_x, draw_y;
		view.getScreenPosition(bmin.x, bmin.y, bmin.z, draw_x, draw_y);

		// Offset label slightly inside the zone
		float label_x = static_cast<float>(draw_x) + 4.0f;
		float label_y = static_cast<float>(draw_y) + 4.0f;

		wxColor zc = colorFromEightBit(zone->ambientColor);

		// Background
		float bounds[4];
		nvgTextBounds(vg, label_x, label_y, zone->name.c_str(), nullptr, bounds);
		float pad = 3.0f;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, bounds[0] - pad, bounds[1] - pad,
			bounds[2] - bounds[0] + pad * 2, bounds[3] - bounds[1] + pad * 2, 3.0f);
		auto bgColor = Theme::Get(Theme::Role::TooltipBg);
		nvgFillColor(vg, nvgRGBA(bgColor.Red(), bgColor.Green(), bgColor.Blue(), 160));
		nvgFill(vg);

		// Text
		nvgFillColor(vg, nvgRGBA(zc.Red(), zc.Green(), zc.Blue(), 220));
		nvgText(vg, label_x, label_y, zone->name.c_str(), nullptr);
	}
}
