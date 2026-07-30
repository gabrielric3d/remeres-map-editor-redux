//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/drawers/overlays/zone_label_drawer.h"

#include "app/definitions.h" // TILE_SIZE
#include <nanovg.h>

#include <algorithm>
#include <cmath>

namespace {
	// Label size, in WINDOW pixels. Deliberately independent of the zoom: the text
	// reads the same at 100% or zoomed all the way out, so it works as a region
	// title instead of growing into the screen.
	//
	// This is the size you get on any zone big enough to hold it -- tweak this one
	// number to make every label bigger or smaller.
	constexpr float BASE_FONT = 44.0f;
	// Floor, so a tiny zone still gets something readable.
	constexpr float MIN_FONT = 14.0f;
	// A zone smaller than the text would be swallowed by its own label, so shrink
	// (never grow) to this fraction of the zone's smaller side.
	constexpr float SMALL_ZONE_RATIO = 0.35f;
	// Keep the label this far from the window edge when it has to be pulled inside.
	constexpr float EDGE_MARGIN = 8.0f;
	// Icon box, and the gap between it and the text, both relative to the font.
	constexpr float ICON_SCALE = 0.95f;
	constexpr float ICON_GAP = 0.22f;

	// --- icons -------------------------------------------------------------
	// All of them draw inside a box of `size` centered on (cx, cy), so the caller
	// only has to decide where the box goes.

	// Eighth note: round head, stem on the right, flag curling off the top.
	void drawMusicIcon(NVGcontext* vg, float cx, float cy, float size, NVGcolor color) {
		const float head_r = size * 0.21f;
		const float stem_w = std::max(1.0f, size * 0.085f);
		const float head_x = cx - size * 0.10f;
		const float head_y = cy + size * 0.26f;
		const float stem_x = head_x + head_r - stem_w * 0.5f;
		const float top_y = cy - size * 0.45f;

		nvgFillColor(vg, color);
		nvgStrokeColor(vg, color);

		// head (slightly squashed, like a real note head)
		nvgBeginPath(vg);
		nvgEllipse(vg, head_x, head_y, head_r * 1.15f, head_r * 0.9f);
		nvgFill(vg);

		// stem
		nvgBeginPath(vg);
		nvgRect(vg, stem_x, top_y, stem_w, (head_y - top_y));
		nvgFill(vg);

		// flag
		nvgBeginPath(vg);
		nvgMoveTo(vg, stem_x + stem_w, top_y);
		nvgBezierTo(vg,
			stem_x + stem_w + size * 0.30f, top_y + size * 0.06f,
			stem_x + stem_w + size * 0.26f, top_y + size * 0.26f,
			stem_x + stem_w, top_y + size * 0.34f);
		nvgLineTo(vg, stem_x + stem_w, top_y + size * 0.22f);
		nvgBezierTo(vg,
			stem_x + stem_w + size * 0.16f, top_y + size * 0.18f,
			stem_x + stem_w + size * 0.18f, top_y + size * 0.08f,
			stem_x + stem_w, top_y);
		nvgClosePath(vg);
		nvgFill(vg);
	}

	// Three stacked squares: the metaphor for N parallel copies of the same area.
	// The back ones are dimmer so the shape reads even at small sizes.
	void drawInstanceIcon(NVGcontext* vg, float cx, float cy, float size, NVGcolor color) {
		const float box = size * 0.56f;
		const float step = size * 0.16f;
		const float radius = std::max(1.0f, box * 0.16f);
		const float left = cx - size * 0.5f + step * 0.5f;
		const float top = cy - size * 0.5f + step * 0.5f;

		// back to front, so the front square sits on top of the others
		for (int i = 2; i >= 0; --i) {
			const float x = left + step * static_cast<float>(i);
			const float y = top + step * static_cast<float>(i);
			NVGcolor c = color;
			c.a = color.a * (i == 0 ? 1.0f : (i == 1 ? 0.62f : 0.38f));

			nvgBeginPath(vg);
			nvgRoundedRect(vg, x, y, box, box, radius);
			if (i == 0) {
				// front one outlined so it stays distinct from the ones behind
				nvgFillColor(vg, c);
				nvgFill(vg);
			} else {
				nvgStrokeColor(vg, c);
				nvgStrokeWidth(vg, std::max(1.0f, size * 0.07f));
				nvgStroke(vg);
			}
		}
	}

	void drawIcon(NVGcontext* vg, ZoneLabelIcon icon, float cx, float cy, float size, NVGcolor color) {
		switch (icon) {
			case ZoneLabelIcon::Music:
				drawMusicIcon(vg, cx, cy, size, color);
				break;
			case ZoneLabelIcon::Instance:
				drawInstanceIcon(vg, cx, cy, size, color);
				break;
			case ZoneLabelIcon::None:
			default:
				break;
		}
	}
} // namespace

void ZoneLabelDrawer::draw(NVGcontext* vg, const RenderView& view, int floor, const std::vector<ZoneLabel>& labels) {
	if (!vg || labels.empty()) {
		return;
	}

	const float win_w = static_cast<float>(view.screensize_x);
	const float win_h = static_cast<float>(view.screensize_y);
	const float zoom = (view.zoom > 0.0f) ? view.zoom : 1.0f;

	nvgFontFace(vg, "sans");
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

	for (const ZoneLabel& label : labels) {
		if (label.text.empty()) {
			continue;
		}

		// Both corners go through the map->screen transform, so the text stays glued
		// to the terrain and scrolls with it instead of floating in the window.
		int map_left = 0, map_top = 0, map_right = 0, map_bottom = 0;
		view.getScreenPosition(label.min_x, label.min_y, floor, map_left, map_top);
		view.getScreenPosition(label.max_x, label.max_y, floor, map_right, map_bottom);

		// CAREFUL: despite the name, RenderView::getScreenPosition returns MAP space
		// -- it subtracts the scroll but never divides by the zoom (render_view.cpp:95).
		// NanoVG draws in window pixels, so the two only agree at zoom 1.0.
		const float left = static_cast<float>(map_left) / zoom;
		const float top = static_cast<float>(map_top) / zoom;
		const float right = static_cast<float>(map_right) / zoom;
		const float bottom = static_cast<float>(map_bottom) / zoom;

		// right/bottom are the top-left of the LAST tile, so add one tile to reach
		// the far edge -- otherwise the label sits half a tile off-center.
		const float tile_on_screen = static_cast<float>(TILE_SIZE) / zoom;
		const float area_w = (right - left) + tile_on_screen;
		const float area_h = (bottom - top) + tile_on_screen;

		float center_x = left + area_w * 0.5f;
		float center_y = top + area_h * 0.5f;

		// Size comes from the zone's extent in MAP space (tiles), never from how big
		// it looks on screen right now. That is what keeps the label identical in
		// window pixels at every zoom. Position follows the map; size does not.
		const float span_w = static_cast<float>(label.max_x - label.min_x + 1) * static_cast<float>(TILE_SIZE);
		const float span_h = static_cast<float>(label.max_y - label.min_y + 1) * static_cast<float>(TILE_SIZE);

		float font = std::min(BASE_FONT, std::min(span_w, span_h) * SMALL_ZONE_RATIO);
		font = std::max(font, MIN_FONT);
		nvgFontSize(vg, font);

		const bool has_icon = (label.icon != ZoneLabelIcon::None);

		// Shrink until name + icon fit the zone's width -- also measured in map space,
		// so a long name on a narrow zone behaves the same at any zoom.
		float text_bounds[4];
		nvgTextBounds(vg, 0.0f, 0.0f, label.text.c_str(), nullptr, text_bounds);
		float text_w = text_bounds[2] - text_bounds[0];
		float block_w = text_w + (has_icon ? font * (ICON_SCALE + ICON_GAP) : 0.0f);
		const float usable_w = span_w * 0.92f;
		if (block_w > usable_w && block_w > 0.0f) {
			font = std::max(MIN_FONT, font * (usable_w / block_w));
			nvgFontSize(vg, font);
			nvgTextBounds(vg, 0.0f, 0.0f, label.text.c_str(), nullptr, text_bounds);
			text_w = text_bounds[2] - text_bounds[0];
			block_w = text_w + (has_icon ? font * (ICON_SCALE + ICON_GAP) : 0.0f);
		}

		const float icon_size = font * ICON_SCALE;
		const float gap = has_icon ? font * ICON_GAP : 0.0f;
		const float text_h = text_bounds[3] - text_bounds[1];

		// Zoomed in far enough and the zone's true center can be off-screen, which
		// would hide the label exactly when you are working inside the zone. Pull the
		// whole block back inside the window -- still map-anchored, just clamped.
		const float pad_x = block_w * 0.5f + EDGE_MARGIN;
		const float pad_y = std::max(text_h, icon_size) * 0.5f + EDGE_MARGIN;
		if (win_w > pad_x * 2.0f) {
			center_x = std::clamp(center_x, pad_x, win_w - pad_x);
		}
		if (win_h > pad_y * 2.0f) {
			center_y = std::clamp(center_y, pad_y, win_h - pad_y);
		}

		// Icon on the left, text on the right, the pair centered as one block.
		const float block_left = center_x - block_w * 0.5f;
		const float icon_cx = block_left + icon_size * 0.5f;
		const float text_cx = has_icon ? (block_left + icon_size + gap + text_w * 0.5f) : center_x;

		const NVGcolor shadow = nvgRGBA(0, 0, 0, 190);
		const NVGcolor tint = nvgRGBA(label.r, label.g, label.b, 235);

		// Dark outline first, then the fill: keeps it legible over bright ground.
		const float outline = std::max(1.0f, font * 0.03f);
		nvgFillColor(vg, shadow);
		for (int dx = -1; dx <= 1; ++dx) {
			for (int dy = -1; dy <= 1; ++dy) {
				if (dx == 0 && dy == 0) {
					continue;
				}
				nvgText(vg, text_cx + dx * outline * 2.0f, center_y + dy * outline * 2.0f, label.text.c_str(), nullptr);
			}
		}
		nvgFillColor(vg, tint);
		nvgText(vg, text_cx, center_y, label.text.c_str(), nullptr);

		if (has_icon) {
			// Same trick for the icon: a dark copy behind it, offset, then the real one.
			drawIcon(vg, label.icon, icon_cx + outline, center_y + outline, icon_size, shadow);
			drawIcon(vg, label.icon, icon_cx, center_y, icon_size, tint);
			// nvgStrokeWidth is sticky, and the text path below does not set it.
			nvgStrokeWidth(vg, 1.0f);
		}

		if (!label.subtext.empty()) {
			const float sub_font = std::max(MIN_FONT * 0.75f, font * 0.32f);
			const float sub_y = center_y + font * 0.72f;
			nvgFontSize(vg, sub_font);
			nvgFillColor(vg, shadow);
			nvgText(vg, center_x + 1.0f, sub_y + 1.0f, label.subtext.c_str(), nullptr);
			nvgFillColor(vg, nvgRGBA(label.r, label.g, label.b, 200));
			nvgText(vg, center_x, sub_y, label.subtext.c_str(), nullptr);
		}
	}
}
