//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/drawers/overlays/br_loot_overlay_drawer.h"

#include "app/definitions.h" // TILE_SIZE, GROUND_LAYER
#include "editor/editor.h"
#include "game/br_loot_catalog.h"
#include "game/br_loot_zones.h"
#include "map/tile.h"
#include "rendering/core/drawing_options.h"
#include "rendering/core/sprite_batch.h"
#include "rendering/drawers/entities/item_drawer.h"
#include "rendering/drawers/entities/sprite_drawer.h"
#include "rendering/drawers/overlays/zone_label_drawer.h"
#include "rendering/drawers/tiles/tile_color_calculator.h"

#include <nanovg.h>

#include <algorithm>
#include <map>
#include <string>

namespace {
	// Past this zoom the window holds too many tiles to walk every frame. The tint is
	// baked with the tiles and stays; the outline, the fill and the labels give way.
	constexpr float MAX_GRID_ZOOM = 5.0f;
	// Window pixels, so the line reads the same at any zoom.
	constexpr float OUTLINE_WIDTH = 2.0f;
	constexpr uint8_t OUTLINE_SHADOW_ALPHA = 150;
	// A little under opaque: the terrain ghosting through is what tells where you are.
	constexpr uint8_t SOLID_ALPHA = 230;
	constexpr float LABEL_MAX_FONT = 30.0f;
	// The amount/chance badge of a placed item needs a tile big enough to be read.
	constexpr float ITEM_BADGE_MAX_ZOOM = 1.5f;
	// The placed item: its sprite slightly warm and translucent, so it never reads as
	// a real item of the map.
	constexpr uint8_t ITEM_R = 255, ITEM_G = 236, ITEM_B = 190, ITEM_ALPHA = 215;

	struct TileWindow {
		int x0 = 0, y0 = 0, x1 = -1, y1 = -1;
	};

	int floorOffset(int floor) {
		// Same shift as RenderView::IsTileVisible for the camera floor itself.
		return (floor <= GROUND_LAYER) ? (GROUND_LAYER - floor) * TILE_SIZE : 0;
	}

	TileWindow visibleWindow(const RenderView& view, int floor) {
		const float zoom = (view.zoom > 0.0f) ? view.zoom : 1.0f;
		const int offset = floorOffset(floor);
		const int width = static_cast<int>(static_cast<float>(view.screensize_x) * zoom);
		const int height = static_cast<int>(static_cast<float>(view.screensize_y) * zoom);
		TileWindow window;
		window.x0 = std::max(0, (view.view_scroll_x + offset) / TILE_SIZE - 1);
		window.y0 = std::max(0, (view.view_scroll_y + offset) / TILE_SIZE - 1);
		window.x1 = (view.view_scroll_x + offset + width) / TILE_SIZE + 1;
		window.y1 = (view.view_scroll_y + offset + height) / TILE_SIZE + 1;
		return window;
	}

	uint32_t packColor(uint8_t r, uint8_t g, uint8_t b) {
		return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
	}

	void zoneColor(const BRLootZones& zones, uint32_t zone_id, uint8_t& r, uint8_t& g, uint8_t& b) {
		TileColorCalculator::GetBRLootZoneColor(zones.tierOf(zone_id), zone_id, r, g, b);
	}

	void drawBadge(NVGcontext* vg, float right, float bottom, const std::string& text) {
		nvgFontSize(vg, 12.0f);
		float bounds[4];
		nvgTextBounds(vg, 0.0f, 0.0f, text.c_str(), nullptr, bounds);
		const float w = (bounds[2] - bounds[0]) + 6.0f;
		const float h = 14.0f;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, right - w, bottom - h, w, h, 3.0f);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 180));
		nvgFill(vg);
		nvgFillColor(vg, nvgRGBA(255, 230, 170, 255));
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgText(vg, right - w * 0.5f, bottom - h * 0.5f, text.c_str(), nullptr);
	}
} // namespace

void BRLootOverlayDrawer::drawItemSprites(SpriteBatch& sprite_batch, SpriteDrawer* sprite_drawer, ItemDrawer* item_drawer, const RenderView& view, Editor& editor) {
	const BRLootZones& zones = editor.map.br_loot_zones;
	if (!item_drawer || zones.getItems().empty()) {
		return;
	}
	g_br_loot_catalog.refresh();
	const int floor = view.floor;
	for (const BRLootItem& item : zones.getItems()) {
		if (item.pos.z != floor) {
			continue;
		}
		const uint16_t server_id = g_br_loot_catalog.serverIdOf(item.name);
		if (server_id == 0) {
			continue; // no sprite to show; the NanoVG pass marks the tile instead
		}
		int sx = 0, sy = 0;
		if (!view.IsTileVisible(item.pos.x, item.pos.y, floor, sx, sy)) {
			continue;
		}
		item_drawer->DrawRawBrush(sprite_batch, sprite_drawer, sx, sy, server_id, ITEM_R, ITEM_G, ITEM_B, ITEM_ALPHA);
	}
}

void BRLootOverlayDrawer::draw(NVGcontext* vg, const RenderView& view, const DrawingOptions& options, Editor& editor, ZoneLabelDrawer& label_drawer) {
	BRLootZones& zones = editor.map.br_loot_zones;
	if (!vg || zones.empty()) {
		return;
	}
	g_br_loot_catalog.refresh();

	const float zoom = (view.zoom > 0.0f) ? view.zoom : 1.0f;
	const float tile_px = static_cast<float>(TILE_SIZE) / zoom;
	const int floor = view.floor;
	const int offset = floorOffset(floor);
	const TileWindow window = visibleWindow(view, floor);

	// Map tile -> window pixel of its top-left corner.
	auto screenX = [&](int x) {
		return static_cast<float>(x * TILE_SIZE - view.view_scroll_x - offset) / zoom;
	};
	auto screenY = [&](int y) {
		return static_cast<float>(y * TILE_SIZE - view.view_scroll_y - offset) / zoom;
	};

	nvgSave(vg);

	if (zoom <= MAX_GRID_ZOOM && window.x1 >= window.x0 && window.y1 >= window.y0) {
		// ---- one read of the window (plus a ring for the neighbour test) ----
		const int gx0 = window.x0 - 1;
		const int gy0 = window.y0 - 1;
		const int gw = (window.x1 - window.x0) + 3;
		const int gh = (window.y1 - window.y0) + 3;
		grid.assign(static_cast<size_t>(gw) * static_cast<size_t>(gh), 0u);
		bool any = false;
		for (int gy = 0; gy < gh; ++gy) {
			const int y = gy0 + gy;
			if (y < 0) {
				continue;
			}
			for (int gx = 0; gx < gw; ++gx) {
				const int x = gx0 + gx;
				if (x < 0) {
					continue;
				}
				const Tile* tile = editor.map.getTile(x, y, floor);
				if (!tile || !tile->isBRLootZoneTile()) {
					continue;
				}
				const uint32_t id = tile->getBRLootZoneId();
				grid[static_cast<size_t>(gy) * gw + gx] = id;
				any = true;
				// The painted box only ever grows; tiles painted since the load are fed
				// here, where they are on screen anyway.
				zones.feedBounds(id, x, y, floor);
			}
		}
		auto idAt = [&](int x, int y) -> uint32_t {
			const int gx = x - gx0;
			const int gy = y - gy0;
			if (gx < 0 || gy < 0 || gx >= gw || gy >= gh) {
				return 0;
			}
			return grid[static_cast<size_t>(gy) * gw + gx];
		};

		// ---- solid fill: one rect per horizontal run of the same zone ----
		if (any && options.solid_br_loot_zones) {
			for (int y = window.y0; y <= window.y1; ++y) {
				int x = window.x0;
				while (x <= window.x1) {
					const uint32_t id = idAt(x, y);
					if (id == 0) {
						++x;
						continue;
					}
					int end = x;
					while (end + 1 <= window.x1 && idAt(end + 1, y) == id) {
						++end;
					}
					uint8_t r, g, b;
					zoneColor(zones, id, r, g, b);
					nvgBeginPath(vg);
					nvgRect(vg, screenX(x), screenY(y), tile_px * static_cast<float>(end - x + 1), tile_px);
					nvgFillColor(vg, nvgRGBA(r, g, b, SOLID_ALPHA));
					nvgFill(vg);
					x = end + 1;
				}
			}
		}

		// ---- outline: tile edges where the zone changes, inside each zone ----
		if (any) {
			// segments grouped by color: one stroke per color instead of per edge
			std::map<uint32_t, std::vector<float>> segments;
			const float inset = OUTLINE_WIDTH * 0.5f + 0.5f;
			for (int y = window.y0; y <= window.y1; ++y) {
				for (int x = window.x0; x <= window.x1; ++x) {
					const uint32_t id = idAt(x, y);
					if (id == 0) {
						continue;
					}
					const bool west = idAt(x - 1, y) != id;
					const bool east = idAt(x + 1, y) != id;
					const bool north = idAt(x, y - 1) != id;
					const bool south = idAt(x, y + 1) != id;
					if (!west && !east && !north && !south) {
						continue;
					}
					uint8_t r, g, b;
					zoneColor(zones, id, r, g, b);
					std::vector<float>& list = segments[packColor(r, g, b)];
					const float left = screenX(x);
					const float top = screenY(y);
					const float right = left + tile_px;
					const float bottom = top + tile_px;
					if (west) {
						list.insert(list.end(), { left + inset, top, left + inset, bottom });
					}
					if (east) {
						list.insert(list.end(), { right - inset, top, right - inset, bottom });
					}
					if (north) {
						list.insert(list.end(), { left, top + inset, right, top + inset });
					}
					if (south) {
						list.insert(list.end(), { left, bottom - inset, right, bottom - inset });
					}
				}
			}

			nvgLineCap(vg, NVG_SQUARE);
			auto strokeAll = [&](const std::vector<float>& list) {
				nvgBeginPath(vg);
				for (size_t i = 0; i + 3 < list.size(); i += 4) {
					nvgMoveTo(vg, list[i], list[i + 1]);
					nvgLineTo(vg, list[i + 2], list[i + 3]);
				}
				nvgStroke(vg);
			};
			// A dark halo first, so the line holds on light ground and on its own tint.
			nvgStrokeWidth(vg, OUTLINE_WIDTH + 2.0f);
			nvgStrokeColor(vg, nvgRGBA(0, 0, 0, OUTLINE_SHADOW_ALPHA));
			for (const auto& entry : segments) {
				strokeAll(entry.second);
			}
			nvgStrokeWidth(vg, OUTLINE_WIDTH);
			for (const auto& entry : segments) {
				const uint32_t c = entry.first;
				nvgStrokeColor(vg, nvgRGBA(static_cast<uint8_t>(c >> 16), static_cast<uint8_t>((c >> 8) & 0xFF), static_cast<uint8_t>(c & 0xFF), 255));
				strokeAll(entry.second);
			}
			nvgStrokeWidth(vg, 1.0f);
		}

		// ---- the id of each zone, over its painted box on this floor ----
		std::vector<ZoneLabel> labels;
		for (const BRLootZone* zone : zones.getOrdered()) {
			const ZoneBounds* b = zone->boundsForFloor(floor);
			if (!b || b->max_x < window.x0 || b->min_x > window.x1 || b->max_y < window.y0 || b->min_y > window.y1) {
				continue;
			}
			ZoneLabel label;
			label.text = std::to_string(zone->tier) + "-" + std::to_string(zone->id);
			const std::string tier_label = g_br_loot_catalog.tierLabel(zone->tier);
			label.subtext = tier_label.empty() ? ("tier " + std::to_string(zone->tier)) : tier_label;
			label.min_x = b->min_x;
			label.min_y = b->min_y;
			label.max_x = b->max_x;
			label.max_y = b->max_y;
			TileColorCalculator::GetBRLootZoneColor(zone->tier, zone->id, label.r, label.g, label.b);
			label.icon = ZoneLabelIcon::Loot;
			label.follow_zoom = true;
			label.max_font = LABEL_MAX_FONT;
			labels.push_back(std::move(label));
		}
		nvgRestore(vg);
		label_drawer.draw(vg, view, floor, labels);
		nvgSave(vg);
	}

	// ---- items placed by hand: a frame around the tile, and what it rolls ----
	nvgFontFace(vg, "sans");
	for (const BRLootItem& item : zones.getItems()) {
		if (item.pos.z != floor) {
			continue;
		}
		if (item.pos.x < window.x0 || item.pos.x > window.x1 || item.pos.y < window.y0 || item.pos.y > window.y1) {
			continue;
		}
		const float left = screenX(item.pos.x);
		const float top = screenY(item.pos.y);
		const bool known = g_br_loot_catalog.serverIdOf(item.name) != 0;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, left + 1.5f, top + 1.5f, tile_px - 3.0f, tile_px - 3.0f, std::min(4.0f, tile_px * 0.15f));
		nvgStrokeWidth(vg, 2.0f);
		nvgStrokeColor(vg, known ? nvgRGBA(255, 205, 80, 230) : nvgRGBA(235, 70, 70, 230));
		nvgStroke(vg);

		if (zoom > ITEM_BADGE_MAX_ZOOM) {
			continue;
		}
		std::string badge;
		if (!known) {
			// Not in the loot catalog of this client: the server would refuse it.
			badge = "?";
		} else {
			if (item.max > item.min) {
				badge = std::to_string(item.min) + "-" + std::to_string(item.max);
			} else if (item.min > 1) {
				badge = "x" + std::to_string(item.min);
			}
			if (item.chance < 100) {
				badge += (badge.empty() ? "" : " ") + std::to_string(item.chance) + "%";
			}
		}
		if (!badge.empty()) {
			drawBadge(vg, left + tile_px - 1.0f, top + tile_px - 1.0f, badge);
		}
	}
	nvgStrokeWidth(vg, 1.0f);

	nvgRestore(vg);
}
