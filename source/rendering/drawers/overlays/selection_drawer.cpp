#include "app/main.h"

// glut include removed

#include "rendering/drawers/overlays/selection_drawer.h"
#include "rendering/core/primitive_renderer.h"
#include <glm/glm.hpp>
#include "rendering/core/render_view.h"
#include "rendering/core/drawing_options.h"
#include "rendering/ui/map_display.h"
#include "rendering/core/graphics.h"
#include "ui/gui.h"
#include <algorithm>
#include <cmath>

void SelectionDrawer::draw(PrimitiveRenderer& primitive_renderer, const RenderView& view, const MapCanvas* canvas, const DrawingOptions& options) {
	if (options.ingame) {
		return;
	}

	// Draw bounding box
	// View coordinates (after zoom, relative to viewport)
	const float last_click_rx = static_cast<float>(canvas->last_click_abs_x - view.view_scroll_x);
	const float last_click_ry = static_cast<float>(canvas->last_click_abs_y - view.view_scroll_y);
	const float cursor_rx = static_cast<float>(canvas->cursor_x * view.zoom);
	const float cursor_ry = static_cast<float>(canvas->cursor_y * view.zoom);

	const float x = std::min(last_click_rx, cursor_rx);
	const float y = std::min(last_click_ry, cursor_ry);
	const float w = std::abs(cursor_rx - last_click_rx);
	const float h = std::abs(cursor_ry - last_click_ry);

	// Restore axis-aligned feedback by removing w/h requirement
	// w=0 or h=0 will just draw lines

	const glm::vec4 rect(x, y, w, h);

	// Draw semi-transparent fill
	primitive_renderer.drawRect(rect, glm::vec4(1.0f, 1.0f, 1.0f, 0.2f));

	// Draw white outline (physical 1px thickness)
	// thickness is in logical units, so thickness=view.zoom results in 1 physical pixel
	primitive_renderer.drawBox(rect, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), view.zoom);
}

void SelectionDrawer::drawLasso(PrimitiveRenderer& primitive_renderer, const RenderView& view, const std::vector<wxPoint>& screen_points, int cursor_x, int cursor_y) {
	if (screen_points.empty()) {
		return;
	}

	const glm::vec4 line_color(1.0f, 1.0f, 1.0f, 0.8f);
	const glm::vec4 point_color(1.0f, 1.0f, 1.0f, 1.0f);
	const glm::vec4 fill_color(1.0f, 1.0f, 1.0f, 0.1f);

	// Draw lines connecting all points (in screen coordinates scaled by zoom)
	for (size_t i = 0; i + 1 < screen_points.size(); ++i) {
		glm::vec2 p1(screen_points[i].x * view.zoom, screen_points[i].y * view.zoom);
		glm::vec2 p2(screen_points[i + 1].x * view.zoom, screen_points[i + 1].y * view.zoom);
		primitive_renderer.drawLine(p1, p2, line_color);
	}

	// Draw line from last point to current cursor position
	if (!screen_points.empty()) {
		glm::vec2 last_pt(screen_points.back().x * view.zoom, screen_points.back().y * view.zoom);
		glm::vec2 cursor_pt(cursor_x * view.zoom, cursor_y * view.zoom);
		primitive_renderer.drawLine(last_pt, cursor_pt, glm::vec4(1.0f, 1.0f, 1.0f, 0.4f));
	}

	// Draw closing line from last point back to first (preview the polygon)
	if (screen_points.size() >= 2) {
		glm::vec2 first_pt(screen_points.front().x * view.zoom, screen_points.front().y * view.zoom);
		glm::vec2 cursor_pt(cursor_x * view.zoom, cursor_y * view.zoom);
		primitive_renderer.drawLine(cursor_pt, first_pt, glm::vec4(1.0f, 1.0f, 1.0f, 0.3f));
	}

	// Draw control points at each lasso point
	float point_size = 2.0f * view.zoom;
	for (const wxPoint& pt : screen_points) {
		float px = pt.x * view.zoom - point_size;
		float py = pt.y * view.zoom - point_size;
		primitive_renderer.drawRect(glm::vec4(px, py, point_size * 2.0f, point_size * 2.0f), point_color);
	}

	// Draw semi-transparent fill for the polygon (using triangle fan from first point)
	if (screen_points.size() >= 3) {
		glm::vec2 first(screen_points[0].x * view.zoom, screen_points[0].y * view.zoom);
		for (size_t i = 1; i + 1 < screen_points.size(); ++i) {
			glm::vec2 p1(screen_points[i].x * view.zoom, screen_points[i].y * view.zoom);
			glm::vec2 p2(screen_points[i + 1].x * view.zoom, screen_points[i + 1].y * view.zoom);
			primitive_renderer.drawTriangle(first, p1, p2, fill_color);
		}
	}
}
