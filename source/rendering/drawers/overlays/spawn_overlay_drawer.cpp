#include "app/main.h"
#include "rendering/drawers/overlays/spawn_overlay_drawer.h"
#include "rendering/core/primitive_renderer.h"
#include "rendering/core/render_view.h"
#include "editor/editor.h"
#include "map/tile.h"
#include "game/spawn.h"
#include "game/creature.h"
#include "app/definitions.h"
#include <nanovg.h>
#include <glm/glm.hpp>

SpawnOverlayDrawer::SpawnOverlayDrawer() {
}

SpawnOverlayDrawer::~SpawnOverlayDrawer() {
}

void SpawnOverlayDrawer::clear() {
	overlays.clear();
}

void SpawnOverlayDrawer::collect(Editor& editor, const RenderView& view, const DrawingOptions& options) {
	clear();

	if (!options.show_spawns) {
		return;
	}

	Map& map = editor.map;
	int current_floor = view.floor;

	for (const Position& pos : map.spawns) {
		if (pos.z != current_floor) {
			continue;
		}

		Tile* tile = map.getTile(pos);
		if (!tile || !tile->spawn) {
			continue;
		}

		int radius = tile->spawn->getSize();

		// Count creatures within spawn radius
		int creature_count = 0;
		for (int dy = -radius; dy <= radius; ++dy) {
			for (int dx = -radius; dx <= radius; ++dx) {
				Tile* creature_tile = map.getTile(pos.x + dx, pos.y + dy, pos.z);
				if (creature_tile && creature_tile->creature) {
					++creature_count;
				}
			}
		}

		overlays.push_back({
			pos,
			radius,
			creature_count,
			tile->spawn->isSelected()
		});
	}
}

void SpawnOverlayDrawer::draw(PrimitiveRenderer& renderer, const RenderView& view, const DrawingOptions& options) {
	if (!options.show_spawns || overlays.empty()) {
		return;
	}

	// Pink/magenta color matching BT_MAPEDITORv3: (220, 0, 220)
	const glm::vec4 border_color(220.0f / 255.0f, 0.0f, 220.0f / 255.0f, 0.86f);
	const glm::vec4 center_fill(220.0f / 255.0f, 0.0f, 220.0f / 255.0f, 0.86f);

	for (const auto& data : overlays) {
		int start_map_x = data.center.x - data.radius;
		int start_map_y = data.center.y - data.radius;
		int end_map_x = data.center.x + data.radius + 1;
		int end_map_y = data.center.y + data.radius + 1;

		int draw_start_x, draw_start_y;
		view.getScreenPosition(start_map_x, start_map_y, data.center.z, draw_start_x, draw_start_y);

		int draw_end_x, draw_end_y;
		view.getScreenPosition(end_map_x, end_map_y, data.center.z, draw_end_x, draw_end_y);

		float x = static_cast<float>(draw_start_x);
		float y = static_cast<float>(draw_start_y);
		float w = static_cast<float>(draw_end_x - draw_start_x);
		float h = static_cast<float>(draw_end_y - draw_start_y);

		// Draw thin border (no fill inside)
		float thickness = 2.0f;
		renderer.drawBox(glm::vec4(x, y, w, h), border_color, thickness);

		// Draw center tile filled with pink
		int center_draw_x, center_draw_y;
		view.getScreenPosition(data.center.x, data.center.y, data.center.z, center_draw_x, center_draw_y);

		float cx = static_cast<float>(center_draw_x);
		float cy = static_cast<float>(center_draw_y);
		float tile_size = static_cast<float>(TILE_SIZE);
		renderer.drawRect(glm::vec4(cx, cy, tile_size, tile_size), center_fill);
	}
}

void SpawnOverlayDrawer::drawLabels(NVGcontext* vg, const RenderView& view) {
	if (!vg || overlays.empty()) {
		return;
	}

	float zoom = view.zoom;
	float tile_size_screen = 32.0f / zoom;

	nvgFontSize(vg, 13.0f);
	nvgFontFace(vg, "sans");
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

	for (const auto& data : overlays) {
		if (data.center.z != view.camera_pos.z) {
			continue;
		}

		int unscaled_x, unscaled_y;
		view.getScreenPosition(data.center.x, data.center.y, data.center.z, unscaled_x, unscaled_y);

		float screen_x = static_cast<float>(unscaled_x) / zoom;
		float screen_y = static_cast<float>(unscaled_y) / zoom;

		// Center of the tile
		float labelX = screen_x + tile_size_screen / 2.0f;
		float labelY = screen_y + tile_size_screen / 2.0f;

		std::string text = std::to_string(data.creature_count);

		// Draw text outline (black) for readability
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
		const float offsets[][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
		for (const auto& off : offsets) {
			nvgText(vg, labelX + off[0], labelY + off[1], text.c_str(), nullptr);
		}

		// Draw text (white)
		nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
		nvgText(vg, labelX, labelY, text.c_str(), nullptr);
	}
}
