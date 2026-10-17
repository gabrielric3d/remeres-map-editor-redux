//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ai/claude_tools.h"

#include "ai/assistant_live.h"
#include "app/settings.h"
#include "brushes/brush.h"
#include "brushes/carpet/carpet_brush.h"
#include "brushes/creature/creature_brush.h"
#include "brushes/doodad/doodad_brush.h"
#include "brushes/door/door_brush.h"
#include "brushes/ground/auto_border.h"
#include "brushes/ground/ground_brush.h"
#include "brushes/house/house_brush.h"
#include "brushes/raw/raw_brush.h"
#include "brushes/spawn/spawn_brush.h"
#include "brushes/table/table_brush.h"
#include "brushes/wall/wall_brush.h"
#include "editor/action.h"
#include "editor/action_queue.h"
#include "editor/editor.h"
#include "editor/selection.h"
#include "game/creature.h"
#include "game/item.h"
#include "item_definitions/core/item_definition_store.h"
#include "lua/lua_deferred_edits.h"
#include "lua/lua_engine.h"
#include "lua/lua_script_manager.h"
#include "map/map.h"
#include "map/tile.h"
#include "map/tile_operations.h"
#include "rendering/postprocess/post_process_manager.h"
#include "rendering/ui/map_display.h"
#include "rendering/ui/screenshot_controller.h"
#include "ui/gui.h"
#include "ui/main_frame.h"
#include "ui/main_menubar.h"
#include "ui/map_tab.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <wx/base64.h>
#include <wx/image.h>
#include <wx/mstream.h>

using Json = nlohmann::json;

namespace {
	constexpr int MAX_REGION_SIDE = 64;
	constexpr size_t MAX_PAINT_TILES = 250000;
	constexpr size_t MAX_LUA_OUTPUT = 12000;
	constexpr int MAX_SCREENSHOT_WIDTH = 1280;
	constexpr int MAX_RAW_SIDE = 32;
	constexpr int MAX_RAW_STACK_LINES = 900;
	constexpr size_t MAX_PLACE_TILES = 5000;
	constexpr size_t MAX_PLACE_ITEMS = 24;
	// Zoom range for set_view and screenshot: 0.25 = 128 px per tile, 8 = 4 px.
	constexpr double MIN_VIEW_ZOOM = 0.25;
	constexpr double MAX_VIEW_ZOOM = 8.0;

	ClaudeToolResult error(const std::string& message) {
		ClaudeToolResult r;
		r.is_error = true;
		r.text = message;
		r.summary = message;
		return r;
	}

	std::string brushType(const Brush* brush) {
		if (!brush) {
			return "none";
		}
		if (brush->is<RAWBrush>()) {
			return "raw";
		}
		if (brush->is<DoodadBrush>()) {
			return "doodad";
		}
		if (brush->is<GroundBrush>()) {
			return "ground";
		}
		if (brush->is<WallBrush>()) {
			return "wall";
		}
		if (brush->is<TableBrush>()) {
			return "table";
		}
		if (brush->is<CarpetBrush>()) {
			return "carpet";
		}
		if (brush->is<DoorBrush>()) {
			return "door";
		}
		if (brush->is<CreatureBrush>()) {
			return "creature";
		}
		if (brush->is<SpawnBrush>()) {
			return "spawn";
		}
		if (brush->is<HouseBrush>()) {
			return "house";
		}
		if (brush->is<EraserBrush>()) {
			return "eraser";
		}
		return "other";
	}

	int intArg(const Json& input, const char* key, int fallback) {
		if (input.contains(key) && input[key].is_number()) {
			return input[key].get<int>();
		}
		return fallback;
	}

	std::string strArg(const Json& input, const char* key, const std::string& fallback = "") {
		if (input.contains(key) && input[key].is_string()) {
			return input[key].get<std::string>();
		}
		return fallback;
	}

	MapCanvas* currentCanvas() {
		MapTab* tab = g_gui.GetCurrentMapTab();
		return tab ? tab->GetCanvas() : nullptr;
	}

	double round2(double value) {
		return std::round(value * 100.0) / 100.0;
	}

	// How many tiles the user's view shows across and down at its zoom.
	bool visibleTiles(int& tiles_w, int& tiles_h) {
		MapCanvas* canvas = currentCanvas();
		if (!canvas) {
			return false;
		}
		int vsx, vsy, sw, sh;
		canvas->GetViewBox(&vsx, &vsy, &sw, &sh);
		tiles_w = static_cast<int>(sw * canvas->GetZoom() / TILE_SIZE);
		tiles_h = static_cast<int>(sh * canvas->GetZoom() / TILE_SIZE);
		return sw > 0 && sh > 0;
	}

	// ---- get_editor_state -------------------------------------------------
	ClaudeToolResult getEditorState() {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			return error("No map is open.");
		}
		Json out;
		out["map"] = {
			{ "name", editor->map.getName() },
			{ "width", editor->map.getWidth() },
			{ "height", editor->map.getHeight() },
			{ "tiles", editor->map.getTileCount() },
		};
		MapTab* tab = g_gui.GetCurrentMapTab();
		if (tab) {
			const Position center = tab->GetScreenCenterPosition();
			out["view"] = { { "x", center.x }, { "y", center.y }, { "z", center.z }, { "zoom", round2(g_gui.GetCurrentZoom()) } };
			int tiles_w = 0, tiles_h = 0;
			if (visibleTiles(tiles_w, tiles_h)) {
				out["view"]["visible_tiles"] = { { "width", tiles_w }, { "height", tiles_h } };
			}
		}
		out["grid"] = g_settings.getBoolean(Config::SHOW_GRID);
		out["floor"] = g_gui.GetCurrentFloor();
		Brush* brush = g_gui.GetCurrentBrush();
		out["current_brush"] = brush ? Json { { "name", brush->getName() }, { "type", brushType(brush) } } : Json(nullptr);
		out["auto_border"] = g_settings.getInteger(Config::USE_AUTOMAGIC) != 0;
		out["selection_tiles"] = editor->selection.size();
		if (!editor->selection.empty()) {
			const Position mn = editor->selection.minPosition();
			const Position mx = editor->selection.maxPosition();
			out["selection_bounds"] = {
				{ "min", { { "x", mn.x }, { "y", mn.y }, { "z", mn.z } } },
				{ "max", { { "x", mx.x }, { "y", mx.y }, { "z", mx.z } } },
			};
		}
		out["can_undo"] = editor->actionQueue->canUndo();
		ClaudeToolResult r;
		r.text = out.dump(2);
		r.summary = "editor state";
		return r;
	}

	// ---- list_brushes -----------------------------------------------------
	ClaudeToolResult listBrushes(const Json& input) {
		const std::string type = strArg(input, "type");
		std::string filter = strArg(input, "filter");
		std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
		const int limit = std::clamp(intArg(input, "limit", 200), 1, 2000);

		std::ostringstream out;
		int shown = 0;
		int total = 0;
		for (const auto& [name, brush] : g_brushes.getMap()) {
			if (!brush) {
				continue;
			}
			const std::string btype = brushType(brush.get());
			if (!type.empty() && type != "all" && btype != type) {
				continue;
			}
			if (!filter.empty()) {
				std::string lower = name;
				std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
				if (lower.find(filter) == std::string::npos) {
					continue;
				}
			}
			++total;
			if (shown < limit) {
				out << name << " [" << btype << "]\n";
				++shown;
			}
		}
		if (total == 0) {
			out << "(no brushes match)\n";
		} else if (total > shown) {
			out << "... " << (total - shown) << " more; narrow with `filter`.\n";
		}
		ClaudeToolResult r;
		r.text = out.str();
		r.summary = std::to_string(shown) + " brush(es) listed";
		return r;
	}

	bool boolArg(const Json& input, const char* key, bool fallback = false) {
		if (input.contains(key) && input[key].is_boolean()) {
			return input[key].get<bool>();
		}
		return fallback;
	}

	std::string itemName(uint16_t id) {
		if (!g_item_definitions.exists(id)) {
			return "?";
		}
		return std::string(g_item_definitions.get(id).name());
	}

	// ---- border catalog ---------------------------------------------------
	// Every border set the ground brushes use (the shared ones from borders.xml
	// and the ones declared inline in a ground brush), who uses it, and a
	// reverse index from piece item id to (set, edge). Rebuilt on every call:
	// it is cheap and brushes can be reloaded at any time.
	const char* const BORDER_EDGE_NAMES[13] = { "", "n", "e", "s", "w", "cnw", "cne", "csw", "cse", "dnw", "dne", "dse", "dsw" };

	struct BorderEntry {
		const AutoBorder* border = nullptr;
		std::string label; // the set id, or "inline:<brush>" for a set declared inside a ground brush
		std::vector<std::string> uses; // "dirt (outer, to none)"
	};

	struct BorderPieceRef {
		size_t entry;
		int edge;
	};

	struct BorderCatalog {
		std::vector<BorderEntry> entries;
		std::unordered_map<uint16_t, std::vector<BorderPieceRef>> by_item;
	};

	BorderCatalog buildBorderCatalog() {
		BorderCatalog catalog;
		std::unordered_map<const AutoBorder*, size_t> index;

		std::vector<const AutoBorder*> shared;
		for (const auto& [id, border] : g_brushes.getBorders()) {
			if (border) {
				shared.push_back(border.get());
			}
		}
		std::sort(shared.begin(), shared.end(), [](const AutoBorder* a, const AutoBorder* b) {
			return a->id < b->id;
		});
		for (const AutoBorder* border : shared) {
			index[border] = catalog.entries.size();
			catalog.entries.push_back({ border, std::to_string(border->id), {} });
		}

		std::unordered_map<uint32_t, std::string> brush_names;
		for (const auto& [name, brush] : g_brushes.getMap()) {
			if (brush) {
				brush_names.emplace(brush->getID(), name);
			}
		}
		const auto target = [&](uint32_t to) -> std::string {
			if (to == 0) {
				return "none";
			}
			if (to == 0xFFFFFFFF) {
				return "all";
			}
			auto it = brush_names.find(to);
			return it != brush_names.end() ? it->second : "brush " + std::to_string(to);
		};

		std::vector<GroundBrush::BorderUse> uses;
		for (const auto& [name, brush] : g_brushes.getMap()) {
			if (!brush || !brush->is<GroundBrush>()) {
				continue;
			}
			uses.clear();
			brush->as<GroundBrush>()->getBorderUses(uses);
			for (const auto& use : uses) {
				size_t at;
				auto it = index.find(use.border);
				if (it == index.end()) {
					at = catalog.entries.size();
					index[use.border] = at;
					catalog.entries.push_back({ use.border, "inline:" + name, {} });
				} else {
					at = it->second;
				}
				const std::string text = name + " (" + (use.optional ? "optional" : use.outer ? "outer" : "inner") + ", to " + target(use.to) + ")";
				auto& list = catalog.entries[at].uses;
				if (std::find(list.begin(), list.end(), text) == list.end()) {
					list.push_back(text);
				}
			}
		}

		for (size_t i = 0; i < catalog.entries.size(); ++i) {
			const AutoBorder* border = catalog.entries[i].border;
			for (int edge = 1; edge <= 12; ++edge) {
				for (const auto& piece : border->tiles[edge]) {
					if (piece.id != 0) {
						catalog.by_item[piece.id].push_back({ i, edge });
					}
				}
			}
		}
		return catalog;
	}

	// "border 12 n, border 30 dnw" for a border piece, "" for anything else.
	std::string pieceLabel(const BorderCatalog& catalog, uint16_t id) {
		auto it = catalog.by_item.find(id);
		if (it == catalog.by_item.end()) {
			return "";
		}
		std::string out;
		for (size_t i = 0; i < it->second.size() && i < 3; ++i) {
			const BorderPieceRef& ref = it->second[i];
			out += (i ? ", border " : "border ") + catalog.entries[ref.entry].label + " " + BORDER_EDGE_NAMES[ref.edge];
		}
		if (it->second.size() > 3) {
			out += ", ...";
		}
		return out;
	}

	// One legend line: id, name, and what it is for the editor.
	std::string describeItem(const BorderCatalog& catalog, uint16_t id) {
		std::string out = std::to_string(id) + " \"" + itemName(id) + "\"";
		const std::string piece = pieceLabel(catalog, id);
		if (!piece.empty()) {
			out += " = " + piece;
		}
		if (g_item_definitions.exists(id)) {
			const ItemEditorData& data = g_item_definitions.editorData(id);
			if (data.brush && piece.empty()) {
				out += " [" + brushType(data.brush) + ": " + data.brush->getName() + "]";
			}
			if (data.doodad_brush && data.doodad_brush != data.brush) {
				out += " [doodad: " + data.doodad_brush->getName() + "]";
			}
		}
		return out;
	}

	// ---- list_borders -----------------------------------------------------
	ClaudeToolResult listBorders(const Json& input) {
		const BorderCatalog catalog = buildBorderCatalog();
		std::string filter = strArg(input, "filter");
		std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
		const int border_id = intArg(input, "border_id", -1);
		const int item = intArg(input, "item", -1);
		const int limit = std::clamp(intArg(input, "limit", 20), 1, 200);

		const auto lower = [](std::string s) {
			std::transform(s.begin(), s.end(), s.begin(), ::tolower);
			return s;
		};
		const auto matches = [&](size_t i) {
			const BorderEntry& entry = catalog.entries[i];
			if (border_id >= 0 && entry.label != std::to_string(border_id)) {
				return false;
			}
			if (item > 0) {
				auto it = catalog.by_item.find(static_cast<uint16_t>(item));
				if (it == catalog.by_item.end() || std::none_of(it->second.begin(), it->second.end(), [i](const BorderPieceRef& ref) { return ref.entry == i; })) {
					return false;
				}
			}
			if (!filter.empty()) {
				bool found = lower(entry.label).find(filter) != std::string::npos;
				for (const std::string& use : entry.uses) {
					found = found || lower(use).find(filter) != std::string::npos;
				}
				for (int edge = 1; edge <= 12 && !found; ++edge) {
					for (const auto& piece : entry.border->tiles[edge]) {
						if (lower(itemName(piece.id)).find(filter) != std::string::npos) {
							found = true;
							break;
						}
					}
				}
				if (!found) {
					return false;
				}
			}
			return true;
		};

		std::ostringstream out;
		int shown = 0;
		int total = 0;
		for (size_t i = 0; i < catalog.entries.size(); ++i) {
			if (!matches(i)) {
				continue;
			}
			++total;
			if (shown >= limit) {
				continue;
			}
			++shown;
			const BorderEntry& entry = catalog.entries[i];
			out << "border " << entry.label << " (group " << entry.border->group << ")";
			if (!entry.uses.empty()) {
				out << ", used by: ";
				for (size_t u = 0; u < entry.uses.size() && u < 6; ++u) {
					out << (u ? "; " : "") << entry.uses[u];
				}
				if (entry.uses.size() > 6) {
					out << "; ...";
				}
			}
			out << "\n";
			const int rows[3][4] = { { 1, 2, 3, 4 }, { 5, 6, 7, 8 }, { 9, 10, 11, 12 } };
			for (const auto& row : rows) {
				out << " ";
				for (int edge : row) {
					out << " " << BORDER_EDGE_NAMES[edge] << " ";
					const auto& pieces = entry.border->tiles[edge];
					if (pieces.empty()) {
						out << "-";
					}
					for (size_t p = 0; p < pieces.size(); ++p) {
						out << (p ? "/" : "") << pieces[p].id;
						if (pieces.size() > 1) {
							out << "(" << pieces[p].chance << ")";
						}
					}
					out << " |";
				}
				out << "\n";
			}
		}
		if (total == 0) {
			out << "(no border set matches)\n";
		} else if (total > shown) {
			out << "... " << (total - shown) << " more; narrow with filter, border_id or item.\n";
		}
		ClaudeToolResult r;
		r.text = out.str();
		r.summary = std::to_string(shown) + " border set(s) listed";
		return r;
	}

	// ---- read_region (raw) ------------------------------------------------
	// Exactly what is on the tiles, border pieces included: how the mapper
	// built an area, piece by piece.
	ClaudeToolResult readRegionRaw(const Json& input) {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			return error("No map is open.");
		}
		const int x = intArg(input, "x", -1);
		const int y = intArg(input, "y", -1);
		const int z = intArg(input, "z", g_gui.GetCurrentFloor());
		const int w = intArg(input, "width", 16);
		const int h = intArg(input, "height", 16);
		if (x < 0 || y < 0 || z < 0 || z > MAP_MAX_LAYER) {
			return error("x, y and z are required (z between 0 and 15).");
		}
		if (w < 1 || h < 1 || w > MAX_RAW_SIDE || h > MAX_RAW_SIDE) {
			return error("With raw=true width/height must be between 1 and " + std::to_string(MAX_RAW_SIDE) + "; read bigger areas in several calls.");
		}

		const BorderCatalog catalog = buildBorderCatalog();
		const std::string symbols = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
		std::vector<uint16_t> ground_ids;
		std::unordered_map<uint16_t, char> ground_symbol;
		std::vector<uint16_t> item_ids;
		std::unordered_set<uint16_t> item_seen;
		std::ostringstream grid;
		std::ostringstream stacks;
		int stack_lines = 0;
		bool truncated = false;

		for (int ty = y; ty < y + h; ++ty) {
			for (int tx = x; tx < x + w; ++tx) {
				Tile* tile = editor->map.getTile(tx, ty, z);
				if (!tile || !tile->ground) {
					grid << '.';
				} else {
					const uint16_t gid = tile->ground->getID();
					auto it = ground_symbol.find(gid);
					if (it != ground_symbol.end()) {
						grid << it->second;
					} else if (ground_ids.size() < symbols.size()) {
						const char c = symbols[ground_ids.size()];
						ground_symbol[gid] = c;
						ground_ids.push_back(gid);
						grid << c;
					} else {
						grid << '?';
					}
				}
				if (!tile || (tile->items.empty() && !tile->creature && !tile->spawn)) {
					continue;
				}
				if (stack_lines >= MAX_RAW_STACK_LINES) {
					truncated = true;
					continue;
				}
				stacks << tx << "," << ty << ":";
				for (const auto& item : tile->items) {
					const uint16_t id = item->getID();
					stacks << ' ' << id;
					if (item_seen.insert(id).second) {
						item_ids.push_back(id);
					}
				}
				if (tile->creature) {
					stacks << " creature:" << tile->creature->getName();
				}
				if (tile->spawn) {
					stacks << " spawn";
				}
				stacks << "\n";
				++stack_lines;
			}
			grid << "\n";
		}

		std::ostringstream out;
		out << "Raw region x=" << x << " y=" << y << " z=" << z << " width=" << w << " height=" << h << "\n";
		out << "Ground grid (row = y from " << y << ", column = x from " << x << "; '.' = no ground):\n" << grid.str();
		out << "Ground legend:\n";
		for (uint16_t gid : ground_ids) {
			out << "  " << ground_symbol[gid] << " = " << describeItem(catalog, gid) << "\n";
		}
		out << "Stacks (x,y: item ids bottom -> top, ground not repeated):\n" << (stack_lines ? stacks.str() : "  (none)\n");
		if (truncated) {
			out << "  ... stack list truncated; read a smaller region for the rest.\n";
		}
		if (!item_ids.empty()) {
			out << "Item legend:\n";
			std::vector<size_t> sets;
			for (uint16_t id : item_ids) {
				out << "  " << describeItem(catalog, id) << "\n";
				auto it = catalog.by_item.find(id);
				if (it != catalog.by_item.end() && std::find(sets.begin(), sets.end(), it->second.front().entry) == sets.end()) {
					sets.push_back(it->second.front().entry);
				}
			}
			if (!sets.empty()) {
				out << "Border sets seen (list_borders shows all their pieces):\n";
				for (size_t index : sets) {
					const BorderEntry& entry = catalog.entries[index];
					out << "  border " << entry.label << ": ";
					for (size_t u = 0; u < entry.uses.size() && u < 4; ++u) {
						out << (u ? "; " : "") << entry.uses[u];
					}
					if (entry.uses.empty()) {
						out << "(no ground brush uses it)";
					}
					out << "\n";
				}
			}
		}
		ClaudeToolResult r;
		r.text = out.str();
		r.summary = "raw read " + std::to_string(w) + "x" + std::to_string(h) + " at " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
		return r;
	}

	// ---- read_region ------------------------------------------------------
	ClaudeToolResult readRegion(const Json& input) {
		if (boolArg(input, "raw")) {
			return readRegionRaw(input);
		}
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			return error("No map is open.");
		}
		const int x = intArg(input, "x", -1);
		const int y = intArg(input, "y", -1);
		const int z = intArg(input, "z", g_gui.GetCurrentFloor());
		const int w = intArg(input, "width", 32);
		const int h = intArg(input, "height", 32);
		if (x < 0 || y < 0 || z < 0 || z > MAP_MAX_LAYER) {
			return error("x, y and z are required (z between 0 and 15).");
		}
		if (w < 1 || h < 1 || w > MAX_REGION_SIDE || h > MAX_REGION_SIDE) {
			return error("width/height must be between 1 and " + std::to_string(MAX_REGION_SIDE) + "; read bigger areas in several calls.");
		}

		// Legend: one letter per ground brush, '.' for no tile, '_' for ground without brush.
		std::vector<std::string> legend_names;
		std::unordered_map<std::string, char> legend;
		const std::string symbols = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
		auto symbolFor = [&](const std::string& name) -> char {
			auto it = legend.find(name);
			if (it != legend.end()) {
				return it->second;
			}
			if (legend.size() >= symbols.size()) {
				return '?';
			}
			const char c = symbols[legend.size()];
			legend[name] = c;
			legend_names.push_back(name);
			return c;
		};

		std::ostringstream grid;
		std::ostringstream items;
		int item_lines = 0;
		constexpr int MAX_ITEM_LINES = 400;
		for (int ty = y; ty < y + h; ++ty) {
			for (int tx = x; tx < x + w; ++tx) {
				Tile* tile = editor->map.getTile(tx, ty, z);
				if (!tile) {
					grid << '.';
					continue;
				}
				GroundBrush* ground = tile->getGroundBrush();
				if (ground) {
					grid << symbolFor(ground->getName());
				} else if (tile->ground) {
					grid << symbolFor("item " + std::to_string(tile->ground->getID()) + " (no brush)");
				} else {
					grid << '.';
				}

				// Non-border items: brush name (or item id/name) grouped by kind.
				std::vector<std::string> parts;
				for (const auto& item : tile->items) {
					if (item->isBorder()) {
						continue;
					}
					Brush* b = item->getBrush();
					Brush* doodad = item->getDoodadBrush();
					if (item->isWall() && item->getWallBrush()) {
						parts.push_back("wall:" + item->getWallBrush()->getName());
					} else if (doodad) {
						parts.push_back("doodad:" + doodad->getName());
					} else if (b) {
						parts.push_back(brushType(b) + ":" + b->getName());
					} else {
						parts.push_back("item:" + std::to_string(item->getID()) + "(" + std::string(item->getName()) + ")");
					}
				}
				if (tile->creature) {
					parts.push_back("creature:" + tile->creature->getName());
				}
				if (tile->spawn) {
					parts.push_back("spawn");
				}
				if (!parts.empty() && item_lines < MAX_ITEM_LINES) {
					items << tx << "," << ty << ": ";
					for (size_t i = 0; i < parts.size(); ++i) {
						if (i) {
							items << ", ";
						}
						items << parts[i];
					}
					items << "\n";
					++item_lines;
				}
			}
			grid << "\n";
		}

		std::ostringstream out;
		out << "Region x=" << x << " y=" << y << " z=" << z << " width=" << w << " height=" << h << "\n";
		out << "Ground legend ('.' = no tile / no ground):\n";
		for (const auto& name : legend_names) {
			out << "  " << legend[name] << " = " << name << "\n";
		}
		out << "Grid (row = y from " << y << ", column = x from " << x << "):\n" << grid.str();
		out << "Items (x,y: kind:brush):\n" << (item_lines ? items.str() : "  (none)\n");
		if (item_lines >= MAX_ITEM_LINES) {
			out << "  ... item list truncated; read a smaller region for the rest.\n";
		}
		ClaudeToolResult r;
		r.text = out.str();
		r.summary = "read " + std::to_string(w) + "x" + std::to_string(h) + " at " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
		return r;
	}

	// One undo step for everything drawn while it lives (a doodad paint makes
	// one batch per tile), kept apart from the user's next stroke.
	struct UndoGroup {
		ActionQueue& queue;
		explicit UndoGroup(ActionQueue& q) :
			queue(q) {
			queue.beginGroup();
		}
		~UndoGroup() {
			queue.endGroup();
			queue.resetTimer();
		}
		UndoGroup(const UndoGroup&) = delete;
		UndoGroup& operator=(const UndoGroup&) = delete;
	};

	// ---- paint ------------------------------------------------------------
	struct PaintPlan {
		Editor* editor = nullptr;
		Brush* brush = nullptr;
		std::string brush_name; // the name it was looked up by
		std::string display_name;
		std::string type;
		std::string shape;
		bool erase = false;
		int z = GROUND_LAYER;
		std::vector<Position> positions;
	};

	// Validates the call and collects the tiles; returns "" or the error.
	std::string planPaint(const Json& input, PaintPlan& plan) {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			return "No map is open.";
		}
		const std::string brush_name = strArg(input, "brush");
		Brush* brush = g_brushes.getBrush(brush_name);
		if (!brush) {
			return "Unknown brush '" + brush_name + "'. Use list_brushes to find the exact name.";
		}
		const bool erase = strArg(input, "mode", "draw") == "erase";
		const std::string shape = strArg(input, "shape", "rect");
		const int z = intArg(input, "z", g_gui.GetCurrentFloor());
		if (z < 0 || z > MAP_MAX_LAYER) {
			return "z must be between 0 and 15.";
		}

		std::vector<Position>& positions = plan.positions;
		std::unordered_set<uint64_t> seen;
		const auto key = [](int px, int py) -> uint64_t {
			return (static_cast<uint64_t>(static_cast<uint32_t>(py)) << 32) | static_cast<uint32_t>(px);
		};
		const auto add = [&](int px, int py) {
			const Position p(px, py, z);
			if (p.isValid() && seen.insert(key(px, py)).second) {
				positions.push_back(p);
			}
		};

		if (shape == "rect") {
			const int x = intArg(input, "x", -1), y = intArg(input, "y", -1);
			const int w = intArg(input, "width", 1), h = intArg(input, "height", 1);
			if (x < 0 || y < 0 || w < 1 || h < 1) {
				return "rect needs x, y, width, height.";
			}
			if (static_cast<size_t>(w) * static_cast<size_t>(h) > MAX_PAINT_TILES) {
				return "Area too large (max " + std::to_string(MAX_PAINT_TILES) + " tiles).";
			}
			for (int py = y; py < y + h; ++py) {
				for (int px = x; px < x + w; ++px) {
					add(px, py);
				}
			}
		} else if (shape == "circle") {
			const int cx = intArg(input, "x", -1), cy = intArg(input, "y", -1);
			const int radius = intArg(input, "radius", -1);
			if (cx < 0 || cy < 0 || radius < 0 || radius > 300) {
				return "circle needs x, y (center) and radius (0-300).";
			}
			for (int py = cy - radius; py <= cy + radius; ++py) {
				for (int px = cx - radius; px <= cx + radius; ++px) {
					const double dx = px - cx, dy = py - cy;
					if (dx * dx + dy * dy <= static_cast<double>(radius) * radius + 0.5) {
						add(px, py);
					}
				}
			}
		} else if (shape == "points") {
			if (!input.contains("points") || !input["points"].is_array()) {
				return "points needs a `points` array of [x, y] pairs.";
			}
			for (const auto& p : input["points"]) {
				if (p.is_array() && p.size() >= 2 && p[0].is_number() && p[1].is_number()) {
					add(p[0].get<int>(), p[1].get<int>());
				}
			}
		} else if (shape == "selection") {
			for (Tile* tile : editor->selection) {
				const Position p = tile->getPosition();
				if (seen.insert(key(p.x, p.y) ^ (static_cast<uint64_t>(p.z) << 56)).second) {
					positions.push_back(p);
				}
			}
			if (positions.empty()) {
				return "Nothing is selected.";
			}
		} else {
			return "shape must be rect, circle, points or selection.";
		}
		if (positions.empty()) {
			return "No valid positions.";
		}
		if (positions.size() > MAX_PAINT_TILES) {
			return "Too many tiles (max " + std::to_string(MAX_PAINT_TILES) + ").";
		}

		if (!g_gui.SelectBrush(brush)) {
			// Some palettes refuse the switch; painting still needs it to be current.
			if (g_gui.GetCurrentBrush() != brush) {
				return "Could not activate brush '" + brush_name + "'.";
			}
		}

		plan.editor = editor;
		plan.brush = brush;
		plan.brush_name = brush_name;
		plan.display_name = brush->getName();
		plan.type = brushType(brush);
		plan.shape = shape;
		plan.erase = erase;
		plan.z = z;
		return "";
	}

	// Paints positions [begin, end) of the plan: one editor batch per call.
	// Returns "" or why it could not.
	std::string applyPaint(const PaintPlan& plan, size_t begin, size_t end) {
		// Brushes can be reloaded while an animation is still running.
		if (g_brushes.getBrush(plan.brush_name) != plan.brush) {
			return "the brush '" + plan.brush_name + "' is gone (brushes were reloaded)";
		}
		// The draw pipeline paints with the current brush, and the user may have
		// picked another one between two animation frames.
		if (g_gui.GetCurrentBrush() != plan.brush) {
			g_gui.SelectBrush(plan.brush);
			if (g_gui.GetCurrentBrush() != plan.brush) {
				return "could not activate brush '" + plan.brush_name + "'";
			}
		}
		Editor* editor = plan.editor;
		Brush* brush = plan.brush;
		const PositionVector positions(plan.positions.begin() + begin, plan.positions.begin() + end);

		if (brush->is<DoodadBrush>()) {
			// One composite per tile with re-rolls, like smearing the brush.
			for (const Position& pos : positions) {
				if (plan.erase) {
					PositionVector one { pos };
					editor->undraw(one, false);
				} else {
					editor->draw(pos, false);
				}
				g_gui.FillDoodadPreviewBuffer();
			}
		} else if (brush->needBorders() || brush->is<WallBrush>() || brush->is<DoorBrush>() || brush->is<EraserBrush>()) {
			PositionVector toborder;
			std::unordered_set<uint64_t> border_seen;
			const auto key = [](int px, int py) -> uint64_t {
				return (static_cast<uint64_t>(static_cast<uint32_t>(py)) << 32) | static_cast<uint32_t>(px);
			};
			for (const Position& p : positions) {
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						const Position n(p.x + dx, p.y + dy, p.z);
						if (n.isValid() && border_seen.insert(key(n.x, n.y) ^ (static_cast<uint64_t>(n.z) << 56)).second) {
							toborder.push_back(n);
						}
					}
				}
			}
			if (plan.erase) {
				editor->undraw(positions, toborder, false);
			} else {
				editor->draw(positions, toborder, false);
			}
		} else {
			if (plan.erase) {
				editor->undraw(positions, false);
			} else {
				editor->draw(positions, false);
			}
		}
		return "";
	}

	ClaudeToolResult paintResult(const PaintPlan& plan, size_t applied, const std::string& interruption) {
		ClaudeToolResult r;
		const std::string count = std::to_string(plan.positions.size());
		if (interruption.empty()) {
			r.text = std::string(plan.erase ? "Erased" : "Painted") + " " + count + " tile(s) with '" + plan.display_name + "' (" + plan.type + ") on floor " + std::to_string(plan.z) + ". This is one undo step.";
			r.summary = std::string(plan.erase ? "erased " : "painted ") + count + " tiles with " + plan.display_name;
		} else {
			r.is_error = true;
			r.text = "Stopped after " + std::to_string(applied) + " of " + count + " tile(s) with '" + plan.display_name + "': " + interruption + ". What was already painted is one undo step; look at the area again before continuing.";
			r.summary = "stopped after " + std::to_string(applied) + "/" + count + " tiles: " + interruption;
		}
		return r;
	}

	// Order in which an animated paint lays the tiles down: row by row like a
	// brush going back and forth, a circle from the center outwards, and
	// points in the order the model gave them.
	void orderForAnimation(PaintPlan& plan, const Json& input) {
		std::vector<Position>& p = plan.positions;
		if (plan.shape == "points") {
			return;
		}
		if (plan.shape == "circle") {
			const int cx = intArg(input, "x", 0);
			const int cy = intArg(input, "y", 0);
			std::stable_sort(p.begin(), p.end(), [cx, cy](const Position& a, const Position& b) {
				const int da = (a.x - cx) * (a.x - cx) + (a.y - cy) * (a.y - cy);
				const int db = (b.x - cx) * (b.x - cx) + (b.y - cy) * (b.y - cy);
				if (da != db) {
					return da < db;
				}
				return std::atan2(a.y - cy, a.x - cx) < std::atan2(b.y - cy, b.x - cx);
			});
			return;
		}
		std::stable_sort(p.begin(), p.end(), [](const Position& a, const Position& b) {
			if (a.z != b.z) {
				return a.z > b.z;
			}
			if (a.y != b.y) {
				return a.y < b.y;
			}
			return (a.y & 1) ? a.x > b.x : a.x < b.x;
		});
	}

	void bounds(const std::vector<Position>& positions, Position& min, Position& max) {
		min = max = positions.front();
		for (const Position& p : positions) {
			min.x = std::min(min.x, p.x);
			min.y = std::min(min.y, p.y);
			max.x = std::max(max.x, p.x);
			max.y = std::max(max.y, p.y);
		}
	}

	ClaudeToolResult paint(const Json& input) {
		PaintPlan plan;
		const std::string problem = planPaint(input, plan);
		if (!problem.empty()) {
			return error(problem);
		}
		std::string why;
		{
			UndoGroup group(*plan.editor->actionQueue);
			why = applyPaint(plan, 0, plan.positions.size());
		}
		g_gui.RefreshView();
		return paintResult(plan, why.empty() ? plan.positions.size() : 0, why);
	}

	// Watchable paint: the camera goes there first, then the tiles land a
	// chunk per frame (or all at once when animation is off).
	void paintLive(const Json& input, AssistantLive::Done done) {
		auto plan = std::make_shared<PaintPlan>();
		const std::string problem = planPaint(input, *plan);
		if (!problem.empty()) {
			done(error(problem));
			return;
		}
		AssistantLive& live = AssistantLive::get();
		Position min, max;
		bounds(plan->positions, min, max);
		live.showActivity(plan->editor, min, max, std::string(plan->erase ? "erasing " : "painting ") + plan->display_name, true);

		auto finish = [plan](size_t applied, const std::string& interruption) {
			return paintResult(*plan, applied, interruption);
		};
		if (AssistantLive::animationEnabled() && plan->positions.size() > 1) {
			orderForAnimation(*plan, input);
			live.animateEdit(
				plan->editor, plan->positions,
				[plan](size_t begin, size_t end) {
					return applyPaint(*plan, begin, end);
				},
				finish, std::move(done)
			);
			return;
		}
		live.bringIntoView(min, max, [plan, finish, done]() {
			if (g_gui.GetCurrentEditor() != plan->editor) {
				done(finish(0, "the map tab was switched or closed"));
				return;
			}
			std::string why;
			try {
				UndoGroup group(*plan->editor->actionQueue);
				why = applyPaint(*plan, 0, plan->positions.size());
			} catch (const std::exception& e) {
				why = e.what();
			} catch (...) {
				why = "unknown error";
			}
			g_gui.RefreshView();
			done(finish(why.empty() ? plan->positions.size() : 0, why));
		});
	}

	// ---- place (raw) ------------------------------------------------------
	// Exactly the listed items on the listed tiles: no auto-border, no brush
	// logic. The mapper's style (broken, layered borders) only comes out this way.
	struct PlaceTile {
		Position pos;
		std::string clear; // "", "all", "items", "borders"
		bool set_ground = false;
		uint16_t ground = 0;
		std::vector<uint16_t> remove;
		std::vector<uint16_t> add;
	};

	struct PlacePlan {
		Editor* editor = nullptr;
		std::vector<PlaceTile> tiles;
		std::string description;
		size_t grounds = 0;
		size_t added = 0;
	};

	// Reads an array of item ids; `must_exist` rejects ids the item list does not know.
	std::string readIdList(const Json& value, bool must_exist, const std::string& where, std::vector<uint16_t>& out) {
		if (!value.is_array()) {
			return where + " must be an array of item ids.";
		}
		for (const auto& v : value) {
			if (!v.is_number_integer()) {
				return where + " must only contain integer item ids.";
			}
			const int id = v.get<int>();
			if (id <= 0 || id > 0xFFFF || (must_exist && !g_item_definitions.exists(static_cast<uint16_t>(id)))) {
				return where + ": unknown item id " + std::to_string(id) + ".";
			}
			out.push_back(static_cast<uint16_t>(id));
		}
		if (out.size() > MAX_PLACE_ITEMS) {
			return where + ": at most " + std::to_string(MAX_PLACE_ITEMS) + " ids per tile.";
		}
		return "";
	}

	// Validates the whole call up front (nothing is applied if any tile is
	// wrong); returns "" or the problem.
	std::string planPlace(const Json& input, PlacePlan& plan) {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			return "No map is open.";
		}
		if (!input.contains("tiles") || !input["tiles"].is_array() || input["tiles"].empty()) {
			return "`tiles` must be a non-empty array.";
		}
		if (input["tiles"].size() > MAX_PLACE_TILES) {
			return "Too many tiles (max " + std::to_string(MAX_PLACE_TILES) + " per call); split the work in several calls.";
		}
		const int default_z = intArg(input, "z", g_gui.GetCurrentFloor());
		size_t index = 0;
		for (const auto& t : input["tiles"]) {
			const std::string where = "tiles[" + std::to_string(index++) + "]";
			if (!t.is_object()) {
				return where + " must be an object.";
			}
			PlaceTile spec;
			const int tx = intArg(t, "x", -1), ty = intArg(t, "y", -1), tz = intArg(t, "z", default_z);
			spec.pos = Position(tx, ty, tz);
			if (tx < 0 || ty < 0 || tz < 0 || tz > MAP_MAX_LAYER || !spec.pos.isValid()) {
				return where + ": invalid position (x, y required; z 0-15).";
			}
			spec.clear = strArg(t, "clear");
			if (!spec.clear.empty() && spec.clear != "all" && spec.clear != "items" && spec.clear != "borders") {
				return where + ": clear must be \"all\", \"items\" or \"borders\".";
			}
			if (t.contains("ground")) {
				if (!t["ground"].is_number_integer()) {
					return where + ": ground must be an item id (0 removes the ground).";
				}
				const int g = t["ground"].get<int>();
				if (g != 0 && (g < 0 || g > 0xFFFF || !g_item_definitions.exists(static_cast<uint16_t>(g)))) {
					return where + ": unknown ground id " + std::to_string(g) + ".";
				}
				if (g != 0 && !g_item_definitions.get(static_cast<uint16_t>(g)).isGroundTile()) {
					return where + ": " + std::to_string(g) + " (\"" + itemName(static_cast<uint16_t>(g)) + "\") is not a ground item; put it in `add`.";
				}
				spec.set_ground = true;
				spec.ground = static_cast<uint16_t>(g);
			}
			if (t.contains("remove")) {
				const std::string problem = readIdList(t["remove"], false, where + ".remove", spec.remove);
				if (!problem.empty()) {
					return problem;
				}
			}
			if (t.contains("add")) {
				const std::string problem = readIdList(t["add"], true, where + ".add", spec.add);
				if (!problem.empty()) {
					return problem;
				}
			}
			if (spec.clear.empty() && !spec.set_ground && spec.remove.empty() && spec.add.empty()) {
				return where + " does nothing (give ground, add, remove or clear).";
			}
			plan.grounds += (spec.set_ground && spec.ground != 0) ? 1 : 0;
			plan.added += spec.add.size();
			plan.tiles.push_back(std::move(spec));
		}
		plan.editor = editor;
		plan.description = strArg(input, "description");
		return "";
	}

	void applyPlaceTile(Tile* tile, const PlaceTile& spec) {
		if (spec.clear == "all") {
			tile->ground.reset();
			tile->items.clear();
		} else if (spec.clear == "items") {
			tile->items.clear();
		} else if (spec.clear == "borders") {
			std::erase_if(tile->items, [](const std::unique_ptr<Item>& item) {
				return item->isBorder();
			});
		}
		if (spec.set_ground) {
			if (spec.ground == 0) {
				tile->ground.reset();
			} else {
				tile->setGround(Item::Create(spec.ground));
			}
		}
		for (uint16_t id : spec.remove) {
			std::erase_if(tile->items, [id](const std::unique_ptr<Item>& item) {
				return item->getID() == id;
			});
			if (tile->ground && tile->ground->getID() == id) {
				tile->ground.reset();
			}
		}
		// Tile::addItem keeps the client's stack order (borders under other
		// items); pieces of the same kind stay in the order given.
		for (uint16_t id : spec.add) {
			if (std::unique_ptr<Item> item = Item::Create(id)) {
				tile->addItem(std::move(item));
			}
		}
		TileOperations::update(tile);
		tile->modify();
	}

	// Applies tiles [begin, end) of the plan as one editor batch.
	std::string applyPlace(const PlacePlan& plan, size_t begin, size_t end) {
		Editor* editor = plan.editor;
		std::unique_ptr<BatchAction> batch = editor->actionQueue->createBatch(ACTION_DRAW);
		std::unique_ptr<Action> action = editor->actionQueue->createAction(batch.get());
		// A tile listed twice in the same chunk must build on its own copy, not
		// on the map's (the chunk is committed only at the end).
		std::map<Position, Tile*> touched;
		for (size_t i = begin; i < end; ++i) {
			const PlaceTile& spec = plan.tiles[i];
			Tile* target = nullptr;
			auto it = touched.find(spec.pos);
			if (it != touched.end()) {
				target = it->second;
			} else {
				Tile* current = editor->map.getTile(spec.pos);
				std::unique_ptr<Tile> copy = current ? TileOperations::deepCopy(current, editor->map) : editor->map.allocator(editor->map.createTileL(spec.pos));
				target = copy.get();
				touched[spec.pos] = target;
				action->addChange(std::make_unique<Change>(std::move(copy)));
			}
			applyPlaceTile(target, spec);
		}
		batch->addAndCommitAction(std::move(action));
		editor->addBatch(std::move(batch));
		return "";
	}

	ClaudeToolResult placeResult(const PlacePlan& plan, size_t applied, const std::string& interruption) {
		ClaudeToolResult r;
		const std::string total = std::to_string(plan.tiles.size());
		if (interruption.empty()) {
			r.text = "Placed " + total + " tile(s): " + std::to_string(plan.grounds) + " ground(s) set, " + std::to_string(plan.added) + " item(s) added. Raw, no auto-border. One undo step.";
			r.summary = "placed " + total + " tiles (raw)";
		} else {
			r.is_error = true;
			r.text = "Stopped after " + std::to_string(applied) + " of " + total + " tile(s): " + interruption + ". What was already placed is one undo step; read the area again (raw) before continuing.";
			r.summary = "stopped after " + std::to_string(applied) + "/" + total + " tiles: " + interruption;
		}
		return r;
	}

	ClaudeToolResult place(const Json& input) {
		PlacePlan plan;
		const std::string problem = planPlace(input, plan);
		if (!problem.empty()) {
			return error(problem);
		}
		std::string why;
		{
			UndoGroup group(*plan.editor->actionQueue);
			why = applyPlace(plan, 0, plan.tiles.size());
		}
		g_gui.RefreshView();
		return placeResult(plan, why.empty() ? plan.tiles.size() : 0, why);
	}

	// Watchable raw placement: the camera goes there, then the tiles appear
	// one by one in the order the model listed them.
	void placeLive(const Json& input, AssistantLive::Done done) {
		auto plan = std::make_shared<PlacePlan>();
		const std::string problem = planPlace(input, *plan);
		if (!problem.empty()) {
			done(error(problem));
			return;
		}
		std::vector<Position> positions;
		positions.reserve(plan->tiles.size());
		for (const PlaceTile& spec : plan->tiles) {
			positions.push_back(spec.pos);
		}
		AssistantLive& live = AssistantLive::get();
		Position min, max;
		bounds(positions, min, max);
		live.showActivity(plan->editor, min, max, plan->description.empty() ? "placing " + std::to_string(positions.size()) + " tiles" : plan->description, true);

		auto finish = [plan](size_t applied, const std::string& interruption) {
			return placeResult(*plan, applied, interruption);
		};
		if (AssistantLive::animationEnabled() && positions.size() > 1) {
			live.animateEdit(
				plan->editor, std::move(positions),
				[plan](size_t begin, size_t end) {
					return applyPlace(*plan, begin, end);
				},
				finish, std::move(done)
			);
			return;
		}
		live.bringIntoView(min, max, [plan, finish, done]() {
			if (g_gui.GetCurrentEditor() != plan->editor) {
				done(finish(0, "the map tab was switched or closed"));
				return;
			}
			std::string why;
			try {
				UndoGroup group(*plan->editor->actionQueue);
				why = applyPlace(*plan, 0, plan->tiles.size());
			} catch (const std::exception& e) {
				why = e.what();
			} catch (...) {
				why = "unknown error";
			}
			g_gui.RefreshView();
			done(finish(why.empty() ? plan->tiles.size() : 0, why));
		});
	}

	// ---- border_pieces ----------------------------------------------------
	// The standard pieces of one border set around (or inside) a patch of
	// ground, worked out like the editor's auto-border but handed back as place
	// tiles: the model starts from a complete border and breaks or layers it on
	// purpose, instead of forgetting corners and diagonals.
	const int NEIGHBOUR_DX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
	const int NEIGHBOUR_DY[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
	const uint32_t NEIGHBOUR_BIT[8] = { TILE_NORTHWEST, TILE_NORTH, TILE_NORTHEAST, TILE_WEST, TILE_EAST, TILE_SOUTHWEST, TILE_SOUTH, TILE_SOUTHEAST };

	uint64_t xyKey(int x, int y) {
		return (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32) | static_cast<uint32_t>(x);
	}

	ClaudeToolResult borderPieces(const Json& input) {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			return error("No map is open.");
		}
		std::string label;
		if (input.contains("border_id")) {
			if (input["border_id"].is_string()) {
				label = input["border_id"].get<std::string>();
			} else if (input["border_id"].is_number_integer()) {
				label = std::to_string(input["border_id"].get<int>());
			}
		}
		const BorderCatalog catalog = buildBorderCatalog();
		const BorderEntry* entry = nullptr;
		for (const BorderEntry& candidate : catalog.entries) {
			if (candidate.label == label) {
				entry = &candidate;
				break;
			}
		}
		if (!entry) {
			return error("Unknown border_id '" + label + "'. Use list_borders to find the set.");
		}
		const int z = intArg(input, "z", g_gui.GetCurrentFloor());
		if (z < 0 || z > MAP_MAX_LAYER) {
			return error("z must be between 0 and 15.");
		}
		const bool inner = strArg(input, "align", "outer") == "inner";

		// The patch: explicit points, or the tiles of the area whose ground is one of `ground`.
		std::unordered_set<uint64_t> patch;
		int min_x = std::numeric_limits<int>::max(), min_y = std::numeric_limits<int>::max();
		int max_x = std::numeric_limits<int>::min(), max_y = std::numeric_limits<int>::min();
		const auto addToPatch = [&](int px, int py) {
			patch.insert(xyKey(px, py));
			min_x = std::min(min_x, px);
			min_y = std::min(min_y, py);
			max_x = std::max(max_x, px);
			max_y = std::max(max_y, py);
		};
		if (input.contains("points") && input["points"].is_array()) {
			for (const auto& p : input["points"]) {
				if (p.is_array() && p.size() >= 2 && p[0].is_number_integer() && p[1].is_number_integer()) {
					addToPatch(p[0].get<int>(), p[1].get<int>());
				}
			}
		} else {
			const int x = intArg(input, "x", -1), y = intArg(input, "y", -1);
			const int w = intArg(input, "width", 0), h = intArg(input, "height", 0);
			if (x < 0 || y < 0 || w < 1 || h < 1 || w > MAX_REGION_SIDE || h > MAX_REGION_SIDE) {
				return error("Give the patch as `points`, or as an area (x, y, width, height up to " + std::to_string(MAX_REGION_SIDE) + ") plus the `ground` ids that form it.");
			}
			std::unordered_set<uint16_t> grounds;
			if (input.contains("ground") && input["ground"].is_array()) {
				for (const auto& g : input["ground"]) {
					if (g.is_number_integer()) {
						grounds.insert(static_cast<uint16_t>(g.get<int>()));
					}
				}
			} else if (input.contains("ground") && input["ground"].is_number_integer()) {
				grounds.insert(static_cast<uint16_t>(input["ground"].get<int>()));
			}
			if (grounds.empty()) {
				return error("`ground` (item id or array of ids) is required with an area: the patch is the tiles whose ground is one of them.");
			}
			for (int ty = y; ty < y + h; ++ty) {
				for (int tx = x; tx < x + w; ++tx) {
					const Tile* tile = editor->map.getTile(tx, ty, z);
					if (tile && tile->ground && grounds.count(tile->ground->getID())) {
						addToPatch(tx, ty);
					}
				}
			}
		}
		if (patch.empty()) {
			return error("The patch is empty: no tile in the area has one of those ground ids.");
		}
		if (patch.size() > 4096) {
			return error("The patch is too big (max 4096 tiles); do it in parts.");
		}

		std::unordered_set<std::string> keep;
		if (input.contains("edges") && input["edges"].is_array()) {
			for (const auto& e : input["edges"]) {
				if (e.is_string()) {
					keep.insert(e.get<std::string>());
				}
			}
		}
		const auto inPatch = [&](int px, int py) {
			return patch.count(xyKey(px, py)) != 0;
		};

		Json place_tiles = Json::array();
		std::ostringstream listing;
		size_t pieces = 0;
		int already_bordered = 0;
		for (int ty = min_y - 1; ty <= max_y + 1; ++ty) {
			for (int tx = min_x - 1; tx <= max_x + 1; ++tx) {
				if (tx < 0 || ty < 0) {
					continue;
				}
				const bool member = inPatch(tx, ty);
				// outer: the tiles around the patch get the pieces; inner: the patch's own edge.
				if (inner ? !member : member) {
					continue;
				}
				uint32_t tiledata = 0;
				for (int i = 0; i < 8; ++i) {
					const bool neighbour = inPatch(tx + NEIGHBOUR_DX[i], ty + NEIGHBOUR_DY[i]);
					if (inner ? !neighbour : neighbour) {
						tiledata |= NEIGHBOUR_BIT[i];
					}
				}
				if (tiledata == 0) {
					continue;
				}
				Json add = Json::array();
				std::string names;
				for (uint32_t combo = GroundBrush::border_types[tiledata]; combo != 0; combo >>= 8) {
					const int edge = static_cast<int>(combo & 0xFF);
					if (edge < 1 || edge > 12 || (!keep.empty() && !keep.count(BORDER_EDGE_NAMES[edge]))) {
						continue;
					}
					const uint32_t id = entry->border->getRandomTileId(edge);
					if (id == 0) {
						continue;
					}
					add.push_back(id);
					names += (names.empty() ? "" : ", ") + std::string(BORDER_EDGE_NAMES[edge]) + " " + std::to_string(id);
				}
				if (add.empty()) {
					continue;
				}
				pieces += add.size();
				place_tiles.push_back({ { "x", tx }, { "y", ty }, { "z", z }, { "add", add } });
				listing << tx << "," << ty << ": " << names << "\n";
				const Tile* tile = editor->map.getTile(tx, ty, z);
				if (tile && std::any_of(tile->items.begin(), tile->items.end(), [](const std::unique_ptr<Item>& item) { return item->isBorder(); })) {
					++already_bordered;
				}
			}
		}

		std::ostringstream out;
		out << "Border " << entry->label << " (" << (inner ? "inner" : "outer") << ") for a patch of " << patch.size() << " tile(s): " << place_tiles.size() << " tile(s), " << pieces << " piece(s).\n";
		if (place_tiles.empty()) {
			out << "Nothing to place (check `edges` and `align`).\n";
		} else {
			out << listing.str();
			if (already_bordered) {
				out << already_bordered << " of these tiles already carry border pieces; use clear:\"borders\" in place if you are redoing their border.\n";
			}
			out << "Ready for place (the complete standard border; break, drop or layer pieces by hand where the style asks):\n" << place_tiles.dump() << "\n";
		}
		ClaudeToolResult r;
		r.text = out.str();
		r.summary = std::to_string(pieces) + " border piece(s) worked out on " + std::to_string(place_tiles.size()) + " tile(s)";
		return r;
	}

	// ---- check_region -----------------------------------------------------
	// A quick review of an area before calling it done: the mistakes that are
	// easy to miss in a screenshot.
	struct CompositeRef {
		const CompositeTileList* composite;
		Position offset;
	};

	// Items that only exist as one piece of a doodad spanning several tiles.
	std::unordered_map<uint16_t, std::vector<CompositeRef>> multiTilePieces() {
		std::unordered_map<uint16_t, std::vector<CompositeRef>> index;
		std::unordered_set<uint16_t> standalone;
		for (const auto& [name, brush] : g_brushes.getMap()) {
			if (!brush || !brush->is<DoodadBrush>()) {
				continue;
			}
			for (const auto& alternative : brush->as<DoodadBrush>()->getItems().getAlternatives()) {
				if (!alternative) {
					continue;
				}
				for (const auto& single : alternative->single_items) {
					if (single.item) {
						standalone.insert(single.item->getID());
					}
				}
				for (const auto& block : alternative->composite_items) {
					for (const auto& [offset, pieces] : block.items) {
						for (const auto& item : pieces) {
							if (!item) {
								continue;
							}
							if (block.items.size() < 2) {
								standalone.insert(item->getID());
							} else {
								index[item->getID()].push_back({ &block.items, offset });
							}
						}
					}
				}
			}
		}
		for (uint16_t id : standalone) {
			index.erase(id);
		}
		return index;
	}

	bool tileHasItem(const Tile* tile, uint16_t id) {
		if (!tile) {
			return false;
		}
		if (tile->ground && tile->ground->getID() == id) {
			return true;
		}
		return std::any_of(tile->items.begin(), tile->items.end(), [id](const std::unique_ptr<Item>& item) { return item->getID() == id; });
	}

	bool tileHasBorder(const Tile* tile) {
		return tile && std::any_of(tile->items.begin(), tile->items.end(), [](const std::unique_ptr<Item>& item) { return item->isBorder(); });
	}

	// Sides of its tile a border piece draws along (bit 1 north, 2 east, 4 south, 8 west);
	// the small corners cover no side.
	unsigned pieceSides(int edge) {
		switch (edge) {
			case NORTH_HORIZONTAL: return 1;
			case EAST_HORIZONTAL: return 2;
			case SOUTH_HORIZONTAL: return 4;
			case WEST_HORIZONTAL: return 8;
			case NORTHWEST_DIAGONAL: return 1 | 8;
			case NORTHEAST_DIAGONAL: return 1 | 2;
			case SOUTHEAST_DIAGONAL: return 4 | 2;
			case SOUTHWEST_DIAGONAL: return 4 | 8;
			default: return 0;
		}
	}

	unsigned coveredSides(const Tile* tile, const BorderCatalog& catalog) {
		unsigned sides = 0;
		if (!tile) {
			return 0;
		}
		for (const auto& item : tile->items) {
			if (!item->isBorder()) {
				continue;
			}
			auto it = catalog.by_item.find(item->getID());
			if (it == catalog.by_item.end()) {
				sides |= 15; // a border the catalog does not know: give it the benefit of the doubt
				continue;
			}
			for (const BorderPieceRef& ref : it->second) {
				sides |= pieceSides(ref.edge);
			}
		}
		return sides;
	}

	bool sameGround(const Tile* a, const Tile* b) {
		const GroundBrush* ga = a->getGroundBrush();
		const GroundBrush* gb = b->getGroundBrush();
		if (ga || gb) {
			return ga == gb;
		}
		return a->ground->getID() == b->ground->getID();
	}

	std::string groundName(const Tile* tile) {
		if (const GroundBrush* brush = tile->getGroundBrush()) {
			return brush->getName();
		}
		return tile->ground ? itemName(tile->ground->getID()) : "no ground";
	}

	ClaudeToolResult checkRegion(const Json& input) {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			return error("No map is open.");
		}
		const int x = intArg(input, "x", -1), y = intArg(input, "y", -1);
		const int z = intArg(input, "z", g_gui.GetCurrentFloor());
		const int w = intArg(input, "width", 0), h = intArg(input, "height", 0);
		if (x < 0 || y < 0 || z < 0 || z > MAP_MAX_LAYER || w < 1 || h < 1 || w > MAX_REGION_SIDE || h > MAX_REGION_SIDE) {
			return error("x, y, z, width and height are required (up to " + std::to_string(MAX_REGION_SIDE) + "x" + std::to_string(MAX_REGION_SIDE) + ").");
		}
		constexpr int MAX_LINES = 30;
		constexpr int STRAIGHT_RUN = 6;
		Map& map = editor->map;
		const auto groundTile = [&](int tx, int ty) -> const Tile* {
			const Tile* tile = map.getTile(tx, ty, z);
			return tile && tile->ground ? tile : nullptr;
		};

		const BorderCatalog catalog = buildBorderCatalog();
		// 1. Different grounds touching with no border piece along that side on either tile.
		std::ostringstream hard;
		int hard_count = 0;
		// 2. Border pieces on a tile whose neighbours all share its ground.
		std::ostringstream floating;
		int floating_count = 0;
		for (int ty = y; ty < y + h; ++ty) {
			for (int tx = x; tx < x + w; ++tx) {
				const Tile* tile = groundTile(tx, ty);
				if (!tile) {
					continue;
				}
				const int nx[2] = { tx + 1, tx };
				const int ny[2] = { ty, ty + 1 };
				for (int i = 0; i < 2; ++i) {
					if (nx[i] >= x + w || ny[i] >= y + h) {
						continue;
					}
					const Tile* other = groundTile(nx[i], ny[i]);
					// i == 0: other is east (this tile's east side, its west side);
					// i == 1: other is south (this tile's south side, its north side).
					const unsigned mine = i == 0 ? 2u : 4u;
					const unsigned theirs = i == 0 ? 8u : 1u;
					if (other && !sameGround(tile, other) && !(coveredSides(tile, catalog) & mine) && !(coveredSides(other, catalog) & theirs)) {
						if (hard_count++ < MAX_LINES) {
							hard << "  " << tx << "," << ty << " (" << groundName(tile) << ") | " << nx[i] << "," << ny[i] << " (" << groundName(other) << ")\n";
						}
					}
				}
				if (tileHasBorder(tile)) {
					bool borders_something = false;
					for (int i = 0; i < 8 && !borders_something; ++i) {
						const Tile* other = groundTile(tx + NEIGHBOUR_DX[i], ty + NEIGHBOUR_DY[i]);
						borders_something = !other || !sameGround(tile, other);
					}
					if (!borders_something && floating_count++ < MAX_LINES) {
						floating << "  " << tx << "," << ty << ":";
						for (const auto& item : tile->items) {
							if (item->isBorder()) {
								floating << ' ' << item->getID();
							}
						}
						floating << " (all neighbours are " << groundName(tile) << ")\n";
					}
				}
			}
		}

		// 3. Multi-tile objects missing pieces.
		const auto pieces_index = multiTilePieces();
		std::ostringstream incomplete;
		int incomplete_count = 0;
		std::unordered_set<std::string> reported;
		for (int ty = y; ty < y + h; ++ty) {
			for (int tx = x; tx < x + w; ++tx) {
				const Tile* tile = map.getTile(tx, ty, z);
				if (!tile) {
					continue;
				}
				for (const auto& item : tile->items) {
					auto it = pieces_index.find(item->getID());
					if (it == pieces_index.end()) {
						continue;
					}
					std::string best_missing;
					size_t best_count = std::numeric_limits<size_t>::max();
					Position best_origin;
					for (const CompositeRef& ref : it->second) {
						const Position origin(tx - ref.offset.x, ty - ref.offset.y, z - ref.offset.z);
						std::string missing;
						size_t count = 0;
						for (const auto& [offset, pieces] : *ref.composite) {
							const Position at(origin.x + offset.x, origin.y + offset.y, origin.z + offset.z);
							const Tile* there = map.getTile(at);
							for (const auto& piece : pieces) {
								if (piece && !tileHasItem(there, piece->getID())) {
									missing += (missing.empty() ? "" : ", ") + std::to_string(piece->getID()) + " at " + std::to_string(at.x) + "," + std::to_string(at.y);
									++count;
								}
							}
						}
						if (count < best_count) {
							best_count = count;
							best_missing = missing;
							best_origin = origin;
						}
						if (count == 0) {
							break;
						}
					}
					if (best_count == 0) {
						continue;
					}
					const std::string key = std::to_string(best_origin.x) + "," + std::to_string(best_origin.y) + ":" + best_missing;
					if (reported.insert(key).second && incomplete_count++ < MAX_LINES) {
						incomplete << "  " << item->getID() << " \"" << itemName(item->getID()) << "\" at " << tx << "," << ty << ": missing " << best_missing << "\n";
					}
				}
			}
		}

		// 4. Straight edges between grounds (the style wants irregular outlines).
		std::ostringstream straight;
		int straight_count = 0;
		struct Side {
			const char* name;
			int dx, dy; // where the other ground is
			bool rows; // runs go along x (north/south) or along y (west/east)
		};
		const Side sides[4] = { { "north", 0, -1, true }, { "south", 0, 1, true }, { "west", -1, 0, false }, { "east", 1, 0, false } };
		for (const Side& side : sides) {
			const int outer_count = side.rows ? h : w;
			const int inner_count = side.rows ? w : h;
			for (int a = 0; a < outer_count; ++a) {
				int run = 0;
				int run_start = 0;
				std::string run_ground;
				for (int b = 0; b <= inner_count; ++b) {
					bool edge = false;
					std::string this_ground;
					if (b < inner_count) {
						const int tx = side.rows ? x + b : x + a;
						const int ty = side.rows ? y + a : y + b;
						const Tile* tile = groundTile(tx, ty);
						const Tile* other = groundTile(tx + side.dx, ty + side.dy);
						edge = tile && other && !sameGround(tile, other);
						if (edge) {
							this_ground = groundName(tile);
						}
					}
					if (edge && (run == 0 || this_ground == run_ground)) {
						if (run == 0) {
							run_start = b;
							run_ground = this_ground;
						}
						++run;
						continue;
					}
					if (run >= STRAIGHT_RUN && straight_count++ < MAX_LINES) {
						if (side.rows) {
							straight << "  " << side.name << " edge of " << run_ground << ", " << run << " tiles, x " << x + run_start << "-" << x + run_start + run - 1 << ", y " << y + a << "\n";
						} else {
							straight << "  " << side.name << " edge of " << run_ground << ", " << run << " tiles, x " << x + a << ", y " << y + run_start << "-" << y + run_start + run - 1 << "\n";
						}
					}
					run = edge ? 1 : 0;
					run_start = b;
					run_ground = this_ground;
				}
			}
		}

		std::ostringstream out;
		out << "Check of x=" << x << " y=" << y << " z=" << z << " " << w << "x" << h << ":\n";
		const auto section = [&](const std::string& title, int count, const std::ostringstream& lines) {
			out << title << ": " << count << "\n";
			if (count) {
				out << lines.str();
				if (count > MAX_LINES) {
					out << "  ... " << (count - MAX_LINES) << " more\n";
				}
			}
		};
		section("Hard edges (different grounds touching, no border piece along that side on either tile)", hard_count, hard);
		section("Floating border pieces (a border on a tile whose neighbours all share its ground)", floating_count, floating);
		section("Multi-tile objects missing pieces", incomplete_count, incomplete);
		section("Straight edges of " + std::to_string(STRAIGHT_RUN) + "+ tiles between grounds", straight_count, straight);
		const int total = hard_count + floating_count + incomplete_count + straight_count;
		if (total == 0) {
			out << "Nothing to fix here. Still look at it with screenshot zoom 1 before calling it done.\n";
		} else {
			out << "Fix these unless one is a deliberate style choice, then check again.\n";
		}
		ClaudeToolResult r;
		r.text = out.str();
		r.summary = total == 0 ? "check: clean" : "check: " + std::to_string(hard_count) + " hard edge(s), " + std::to_string(floating_count) + " floating, " + std::to_string(incomplete_count) + " incomplete, " + std::to_string(straight_count) + " straight";
		return r;
	}

	// ---- run_lua ----------------------------------------------------------
	ClaudeToolResult runLua(const Json& input) {
		const std::string code = strArg(input, "code");
		if (code.empty()) {
			return error("`code` is required.");
		}
		if (!g_luaScripts.isInitialized()) {
			return error("The Lua engine is not initialized.");
		}
		if (!g_gui.GetCurrentEditor()) {
			return error("No map is open.");
		}
		LuaEngine& engine = g_luaScripts.getEngine();

		std::string output;
		const LuaEngine::PrintCallback previous = engine.getPrintCallback();
		engine.setPrintCallback([&output, previous](const std::string& line) {
			if (output.size() < MAX_LUA_OUTPUT) {
				output += line;
				output += '\n';
			}
			if (previous) {
				previous(line);
			}
		});

		// Map edits must run inside app.transaction to be undoable; wrap the
		// snippet when the model forgot, otherwise trust its own transaction.
		std::string wrapped = code;
		const std::string description = strArg(input, "description", "Claude script");
		if (code.find("app.transaction") == std::string::npos) {
			std::string escaped;
			for (char c : description) {
				escaped += (c == '"' || c == '\\') ? '\'' : c;
			}
			wrapped = "app.transaction(\"" + escaped + "\", function()\n" + code + "\nend)";
		}

		const bool ok = engine.executeString(wrapped, "claude");
		const std::string err = ok ? "" : engine.getLastError();
		engine.setPrintCallback(previous);
		g_gui.RefreshView();

		ClaudeToolResult r;
		if (output.size() >= MAX_LUA_OUTPUT) {
			output += "\n... output truncated\n";
		}
		if (!ok) {
			r.is_error = true;
			r.text = "Lua error: " + err + (output.empty() ? "" : "\nOutput before the error:\n" + output);
			r.summary = "Lua error: " + err.substr(0, 120);
		} else {
			r.text = output.empty() ? "OK (no output). Use print() to report what the script did." : output;
			r.summary = "Lua script ran (" + std::to_string(std::count(output.begin(), output.end(), '\n')) + " output lines)";
		}
		return r;
	}

	// ---- screenshot -------------------------------------------------------
	double zoomArg(const Json& input) {
		if (input.contains("zoom") && input["zoom"].is_number()) {
			return input["zoom"].get<double>();
		}
		return 0.0;
	}

	// zoom > 0: the picture is taken at that zoom (1 = the game's own scale,
	// 32 px per tile) and the user's zoom comes back right after. width/height
	// > 0: only that many tiles around the center are kept. ground_only and
	// grid switch Shift+B and Shift+G for this picture only.
	ClaudeToolResult captureView(const Json& input) {
		MapCanvas* canvas = currentCanvas();
		if (!canvas) {
			return error("No map view.");
		}
		const double zoom = zoomArg(input);
		const int crop_w = intArg(input, "width", 0);
		const int crop_h = intArg(input, "height", 0);

		const bool user_ground_only = g_settings.getBoolean(Config::SHOW_ONLY_GROUNDS);
		const bool user_grid = g_settings.getBoolean(Config::SHOW_GRID);
		const bool shot_ground_only = boolArg(input, "ground_only", user_ground_only);
		const bool shot_grid = boolArg(input, "grid", user_grid);
		g_settings.setInteger(Config::SHOW_ONLY_GROUNDS, shot_ground_only ? 1 : 0);
		g_settings.setInteger(Config::SHOW_GRID, shot_grid ? 1 : 0);
		const double user_zoom = canvas->zoom;
		const bool rezoom = zoom > 0.0 && std::abs(zoom - user_zoom) > 0.001;
		if (rezoom) {
			canvas->SetZoom(std::clamp(zoom, MIN_VIEW_ZOOM, MAX_VIEW_ZOOM));
		}
		const double shot_zoom = canvas->zoom;
		wxImage image = canvas->screenshot_controller->CaptureImage();
		MapTab* tab = g_gui.GetCurrentMapTab();
		const Position center = tab ? tab->GetScreenCenterPosition() : Position();
		const int view_floor = canvas->GetFloor();
		int vsx, vsy, sw, sh;
		canvas->GetViewBox(&vsx, &vsy, &sw, &sh);
		if (rezoom) {
			canvas->SetZoom(user_zoom);
		}
		g_settings.setInteger(Config::SHOW_ONLY_GROUNDS, user_ground_only ? 1 : 0);
		g_settings.setInteger(Config::SHOW_GRID, user_grid ? 1 : 0);
		if (shot_ground_only != user_ground_only || shot_grid != user_grid) {
			canvas->Refresh();
		}
		if (!image.IsOk() || sw <= 0 || sh <= 0) {
			return error("Could not capture the map view.");
		}

		int tiles_w = static_cast<int>(sw * shot_zoom / TILE_SIZE);
		int tiles_h = static_cast<int>(sh * shot_zoom / TILE_SIZE);
		// Pixels per tile in the captured image (the canvas may be DPI-scaled).
		double tile_px = TILE_SIZE / shot_zoom * (static_cast<double>(image.GetWidth()) / sw);
		// Map position of the picture's top-left corner, in tiles (the inverse of
		// RenderView::getScreenPosition for the current floor).
		const double floor_offset = view_floor <= GROUND_LAYER ? GROUND_LAYER - view_floor : 0;
		double left = static_cast<double>(vsx) / TILE_SIZE + floor_offset;
		double top = static_cast<double>(vsy) / TILE_SIZE + floor_offset;
		if (crop_w > 0 || crop_h > 0) {
			const int cw = crop_w > 0 ? std::clamp(static_cast<int>(std::lround(crop_w * tile_px)), 1, image.GetWidth()) : image.GetWidth();
			const int ch = crop_h > 0 ? std::clamp(static_cast<int>(std::lround(crop_h * tile_px)), 1, image.GetHeight()) : image.GetHeight();
			const int crop_x = (image.GetWidth() - cw) / 2;
			const int crop_y = (image.GetHeight() - ch) / 2;
			image = image.GetSubImage(wxRect(crop_x, crop_y, cw, ch));
			left += crop_x / tile_px;
			top += crop_y / tile_px;
			tiles_w = crop_w > 0 ? std::min(crop_w, tiles_w) : tiles_w;
			tiles_h = crop_h > 0 ? std::min(crop_h, tiles_h) : tiles_h;
		}
		if (image.GetWidth() > MAX_SCREENSHOT_WIDTH) {
			const double scale = static_cast<double>(MAX_SCREENSHOT_WIDTH) / image.GetWidth();
			image = image.Scale(MAX_SCREENSHOT_WIDTH, std::max(1, static_cast<int>(std::lround(image.GetHeight() * scale))), wxIMAGE_QUALITY_BOX_AVERAGE);
			tile_px *= scale;
		}
		wxMemoryOutputStream stream;
		if (!image.SaveFile(stream, wxBITMAP_TYPE_PNG)) {
			return error("PNG encoding failed.");
		}
		wxStreamBuffer* buffer = stream.GetOutputStreamBuffer();
		const wxString b64 = wxBase64Encode(buffer->GetBufferStart(), buffer->GetBufferSize());

		// First tile whose top-left corner is inside the picture: with it and the
		// tile size every tile's pixel box is exact (a grid cell is one tile).
		const int ref_x = static_cast<int>(std::ceil(left - 1e-6));
		const int ref_y = static_cast<int>(std::ceil(top - 1e-6));
		const long ref_px = std::lround((ref_x - left) * tile_px);
		const long ref_py = std::lround((ref_y - top) * tile_px);

		ClaudeToolResult r;
		r.image_png_base64 = std::string(b64.mb_str());
		std::ostringstream text;
		text << "Screenshot centered at x=" << center.x << " y=" << center.y << " z=" << center.z << ", about " << tiles_w << "x" << tiles_h << " tiles, " << round2(tile_px) << " px per tile in this image (zoom " << round2(shot_zoom) << ").";
		text << " Tile x=" << ref_x << " y=" << ref_y << " has its top-left corner at pixel (" << ref_px << ", " << ref_py << ") of this picture and every tile is " << round2(tile_px) << " px, which places any other tile exactly.";
		if (shot_grid) {
			text << " The grid lines are the tile edges.";
		}
		if (tile_px < 24.0) {
			text << " At this size missing border pieces and small details are hard to see; for a close check use zoom 1 (or 0.5) with width/height around the area.";
		}
		r.text = text.str();
		r.summary = "screenshot at " + std::to_string(center.x) + "," + std::to_string(center.y) + "," + std::to_string(center.z) + (tile_px >= 24.0 ? " (close-up)" : "") + (shot_grid ? " with grid" : "");
		return r;
	}

	ClaudeToolResult screenshot(const Json& input) {
		if (!currentCanvas()) {
			return error("No map view.");
		}
		if (input.contains("x") && input.contains("y")) {
			const int z = intArg(input, "z", g_gui.GetCurrentFloor());
			g_gui.SetScreenCenterPosition(Position(intArg(input, "x", 0), intArg(input, "y", 0), z));
		}
		return captureView(input);
	}

	// ---- set_view ---------------------------------------------------------
	struct ViewTarget {
		bool move = false; // x, y or z given; otherwise only the zoom changes
		int x = 0, y = 0, z = 0;
		double zoom = 0.0; // 0: the user's zoom stays
	};

	// Where set_view takes the camera. x/y default to the current center and z
	// to the current floor; the zoom is given, worked out so that width x height
	// tiles fill the view, or left alone. Returns "" or what is wrong.
	std::string viewTarget(const Json& input, ViewTarget& target) {
		MapTab* tab = g_gui.GetCurrentMapTab();
		MapCanvas* canvas = currentCanvas();
		if (!tab || !canvas) {
			return "No map view.";
		}
		const Position center = tab->GetScreenCenterPosition();
		target.move = input.contains("x") || input.contains("y") || input.contains("z");
		target.x = intArg(input, "x", center.x);
		target.y = intArg(input, "y", center.y);
		target.z = intArg(input, "z", g_gui.GetCurrentFloor());
		if (target.x < 0 || target.y < 0 || target.z < 0 || target.z > MAP_MAX_LAYER) {
			return "x and y cannot be negative and z must be between 0 and 15.";
		}
		const int w = intArg(input, "width", 0);
		const int h = intArg(input, "height", 0);
		if (input.contains("zoom") && input["zoom"].is_number()) {
			target.zoom = input["zoom"].get<double>();
			if (target.zoom <= 0.0) {
				return "zoom must be above 0: 1 = the game's scale, 0.5 = twice as close, 2 = twice as far out.";
			}
		} else if (w > 0 || h > 0) {
			int vsx, vsy, sw, sh;
			canvas->GetViewBox(&vsx, &vsy, &sw, &sh);
			if (sw <= 0 || sh <= 0) {
				return "The map view has no size.";
			}
			// A tile of margin on each side.
			target.zoom = std::max(w > 0 ? (w + 2.0) * TILE_SIZE / sw : 0.0, h > 0 ? (h + 2.0) * TILE_SIZE / sh : 0.0);
		}
		if (target.zoom > 0.0) {
			target.zoom = std::clamp(target.zoom, MIN_VIEW_ZOOM, MAX_VIEW_ZOOM);
		}
		return "";
	}

	// What the user's view shows once set_view is done.
	ClaudeToolResult viewReport() {
		MapTab* tab = g_gui.GetCurrentMapTab();
		if (!tab) {
			return error("No map view.");
		}
		const Position center = tab->GetScreenCenterPosition();
		const double zoom = round2(g_gui.GetCurrentZoom());
		std::ostringstream text;
		text << "View centered at " << center.x << "," << center.y << "," << center.z << ", zoom " << zoom << " (" << std::lround(TILE_SIZE / g_gui.GetCurrentZoom()) << " px per tile on the user's screen";
		int tiles_w = 0, tiles_h = 0;
		if (visibleTiles(tiles_w, tiles_h)) {
			text << ", about " << tiles_w << "x" << tiles_h << " tiles visible";
		}
		text << ").";
		std::ostringstream summary;
		summary << "view at " << center.x << "," << center.y << "," << center.z << ", zoom " << zoom;
		ClaudeToolResult r;
		r.text = text.str();
		r.summary = summary.str();
		return r;
	}

	ClaudeToolResult setView(const Json& input) {
		ViewTarget target;
		const std::string problem = viewTarget(input, target);
		if (!problem.empty()) {
			return error(problem);
		}
		// Zoom first: SetZoom keeps the old center, then the new one is set.
		if (target.zoom > 0.0) {
			g_gui.SetCurrentZoom(target.zoom);
		}
		if (target.move) {
			g_gui.SetScreenCenterPosition(Position(target.x, target.y, target.z));
		}
		g_gui.RefreshView();
		return viewReport();
	}

	// ---- set_display ------------------------------------------------------
	// Flat Colors (the screen shader that collapses every colour family into one flat
	// tone) and Shift+B are the mapper's grounding view. Claude turns them on while it
	// lays grounds/borders and off for details; the user's own shader comes back on "off".
	std::string shader_before_flat;
	std::string last_flat_shader = ShaderNames::FLAT_COLORS_STRONG;

	bool isFlatShader(const std::string& name) {
		return name == ShaderNames::FLAT_COLORS || name == ShaderNames::FLAT_COLORS_STRONG || name == ShaderNames::FLAT_COLORS_EXTREME;
	}

	ClaudeToolResult setDisplay(const Json& input) {
		if (!currentCanvas()) {
			return error("No map view.");
		}
		std::string changed;
		if (input.contains("flat_colors")) {
			const Json& value = input["flat_colors"];
			const std::string mode = value.is_boolean() ? (value.get<bool>() ? "on" : "off") : value.is_string() ? value.get<std::string>() : "";
			const std::string current = g_settings.getString(Config::SCREEN_SHADER);
			if (isFlatShader(current)) {
				last_flat_shader = current;
			}
			std::string target;
			if (mode == "off") {
				target = isFlatShader(current) ? (shader_before_flat.empty() ? std::string(ShaderNames::NONE) : shader_before_flat) : current;
			} else if (mode == "on") {
				target = last_flat_shader;
			} else if (mode == "normal") {
				target = ShaderNames::FLAT_COLORS;
			} else if (mode == "strong") {
				target = ShaderNames::FLAT_COLORS_STRONG;
			} else if (mode == "extreme") {
				target = ShaderNames::FLAT_COLORS_EXTREME;
			} else {
				return error("flat_colors must be on, off, normal, strong or extreme.");
			}
			if (!isFlatShader(current) && isFlatShader(target)) {
				shader_before_flat = current;
			}
			if (isFlatShader(target)) {
				last_flat_shader = target;
			}
			g_settings.setString(Config::SCREEN_SHADER, target);
			changed += isFlatShader(target) ? "flat colors on (" + target + ")" : "flat colors off";
		}
		if (input.contains("ground_only") && input["ground_only"].is_boolean()) {
			const bool on = input["ground_only"].get<bool>();
			g_settings.setInteger(Config::SHOW_ONLY_GROUNDS, on ? 1 : 0);
			changed += std::string(changed.empty() ? "" : ", ") + (on ? "ground only on" : "ground only off");
		}
		if (input.contains("grid") && input["grid"].is_boolean()) {
			const bool on = input["grid"].get<bool>();
			g_settings.setInteger(Config::SHOW_GRID, on ? 1 : 0);
			g_gui.SetStatusText(on ? "Grid: On" : "Grid: Off"); // as Shift+G does
			changed += std::string(changed.empty() ? "" : ", ") + (on ? "grid on" : "grid off");
		}
		// Keep the View menu's check marks in step, or the next view toggle writes them back.
		if (g_gui.root && g_gui.root->GetMainMenuBar()) {
			MainMenuBar* menu = g_gui.root->GetMainMenuBar();
			menu->CheckItem(MenuBar::SHOW_ONLY_GROUNDS, g_settings.getBoolean(Config::SHOW_ONLY_GROUNDS));
			menu->CheckItem(MenuBar::SHOW_GRID, g_settings.getBoolean(Config::SHOW_GRID));
		}
		g_gui.RefreshView();

		const std::string shader = g_settings.getString(Config::SCREEN_SHADER);
		const std::string state = "Flat colors: " + (isFlatShader(shader) ? shader : std::string("off")) + "; ground only (Shift+B): " + (g_settings.getBoolean(Config::SHOW_ONLY_GROUNDS) ? "on" : "off") + "; grid: " + (g_settings.getBoolean(Config::SHOW_GRID) ? "on" : "off") + ".";
		ClaudeToolResult r;
		r.text = (changed.empty() ? "" : "Display: " + changed + ". ") + "Now: " + state + " The user's view and your screenshots both use it.";
		r.summary = changed.empty() ? "display state" : "display: " + changed;
		return r;
	}

	// ---- undo -------------------------------------------------------------
	ClaudeToolResult undo(const Json& input) {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			return error("No map is open.");
		}
		const int steps = std::clamp(intArg(input, "steps", 1), 1, 50);
		int done = 0;
		for (int i = 0; i < steps && editor->actionQueue->canUndo(); ++i) {
			editor->actionQueue->undo();
			++done;
		}
		g_gui.RefreshView();
		g_gui.UpdateMenubar();
		ClaudeToolResult r;
		r.text = "Undid " + std::to_string(done) + " step(s).";
		r.summary = r.text;
		return r;
	}

	// ---- live versions (see AssistantLive) --------------------------------
	// Continuation for a camera glide: runs `body` and hands its result to
	// `done`, never letting an exception escape into the timer event.
	std::function<void()> guarded(AssistantLive::Done done, std::function<ClaudeToolResult()> body) {
		return [done = std::move(done), body = std::move(body)]() {
			try {
				done(body());
			} catch (const std::exception& e) {
				done(error(std::string("Tool failed: ") + e.what()));
			} catch (...) {
				done(error("Tool failed with an unknown error."));
			}
		};
	}

	// Bounds of what a batch changed, on the floor where most of it happened.
	bool changedArea(const BatchAction* batch, Position& min, Position& max) {
		std::vector<Position> changed;
		batch->collectTilePositions(changed);
		int per_floor[MAP_LAYERS] = {};
		for (const Position& p : changed) {
			if (p.z >= 0 && p.z < MAP_LAYERS) {
				++per_floor[p.z];
			}
		}
		const int z = static_cast<int>(std::max_element(per_floor, per_floor + MAP_LAYERS) - per_floor);
		bool found = false;
		for (const Position& p : changed) {
			if (p.z != z) {
				continue;
			}
			if (!found) {
				min = max = p;
				found = true;
				continue;
			}
			min.x = std::min(min.x, p.x);
			min.y = std::min(min.y, p.y);
			max.x = std::max(max.x, p.x);
			max.y = std::max(max.y, p.y);
		}
		return found;
	}

	// Animated: the script runs with its transactions deferred (nothing is drawn
	// while it runs), the map is put back, and the tiles it changed are laid down
	// again one by one, in the order the script first touched them.
	void runLuaAnimated(Editor* editor, const Json& input, AssistantLive::Done done) {
		LuaAPI::beginDeferredEdits(editor);
		auto result = std::make_shared<ClaudeToolResult>();
		try {
			*result = runLua(input);
		} catch (...) {
			LuaAPI::finishDeferredEdits(); // never leave the map half-way
			throw;
		}
		auto edits = std::make_shared<std::vector<LuaAPI::DeferredTileEdit>>(LuaAPI::finishDeferredEdits());
		if (edits->empty()) {
			done(*result);
			return;
		}

		std::vector<Position> positions;
		positions.reserve(edits->size());
		for (const auto& edit : *edits) {
			positions.push_back(edit.pos);
		}
		Position min, max;
		bounds(positions, min, max);
		const std::string label = strArg(input, "description", "Claude script");
		AssistantLive& live = AssistantLive::get();
		live.showActivity(editor, min, max, label, true);

		const size_t total = edits->size();
		live.animateEdit(
			editor, std::move(positions),
			[editor, edits, label](size_t begin, size_t end) -> std::string {
				// The second half of a Lua commit, a few tiles at a time.
				std::unique_ptr<BatchAction> batch = editor->actionQueue->createBatch(ACTION_LUA_SCRIPT);
				batch->setLabel(label);
				std::unique_ptr<Action> action = editor->actionQueue->createAction(ACTION_LUA_SCRIPT);
				for (size_t i = begin; i < end; ++i) {
					LuaAPI::DeferredTileEdit& edit = (*edits)[i];
					action->addChange(std::make_unique<Change>(std::move(edit.tile), edit.pos));
				}
				batch->addAndCommitAction(std::move(action));
				editor->addBatch(std::move(batch));
				return "";
			},
			[result, total](size_t applied, const std::string& interruption) {
				if (interruption.empty()) {
					return *result;
				}
				ClaudeToolResult r = *result;
				r.is_error = true;
				r.text = "The script ran, but " + interruption + " while its " + std::to_string(total) + " tile(s) were being laid down: only " + std::to_string(applied) + " made it to the map (one undo step). Read the area again before continuing.\nScript output:\n" + result->text;
				r.summary = "script stopped after " + std::to_string(applied) + "/" + std::to_string(total) + " tiles: " + interruption;
				return r;
			},
			std::move(done)
		);
	}

	// Not animated: the script lands in one go, then the camera shows where.
	void runLuaLive(const Json& input, AssistantLive::Done done) {
		Editor* editor = g_gui.GetCurrentEditor();
		if (editor && AssistantLive::animationEnabled()) {
			runLuaAnimated(editor, input, std::move(done));
			return;
		}
		const BatchAction* before = editor ? editor->actionQueue->lastBatch() : nullptr;
		const size_t before_size = editor ? editor->actionQueue->getSize() : 0;
		auto result = std::make_shared<ClaudeToolResult>(runLua(input));
		if (!editor || g_gui.GetCurrentEditor() != editor) {
			done(*result);
			return;
		}
		const BatchAction* after = editor->actionQueue->lastBatch();
		Position min, max;
		if (!after || (after == before && editor->actionQueue->getSize() == before_size) || !changedArea(after, min, max)) {
			done(*result);
			return;
		}
		AssistantLive& live = AssistantLive::get();
		live.showActivity(editor, min, max, "ran a script", true);
		live.bringIntoView(min, max, guarded(std::move(done), [result]() { return *result; }));
	}

	void readRegionLive(const Json& input, AssistantLive::Done done) {
		ClaudeToolResult result = readRegion(input);
		if (!result.is_error) {
			const int x = intArg(input, "x", 0), y = intArg(input, "y", 0);
			const int z = intArg(input, "z", g_gui.GetCurrentFloor());
			const int side = boolArg(input, "raw") ? 16 : 32; // readRegion's defaults
			const int w = intArg(input, "width", side), h = intArg(input, "height", side);
			AssistantLive::get().showActivity(g_gui.GetCurrentEditor(), Position(x, y, z), Position(x + w - 1, y + h - 1, z), "reading", false);
		}
		done(std::move(result));
	}

	void setViewLive(const Json& input, AssistantLive::Done done) {
		ViewTarget target;
		const std::string problem = viewTarget(input, target);
		if (!problem.empty()) {
			done(error(problem));
			return;
		}
		auto then = guarded(std::move(done), []() {
			g_gui.RefreshView();
			return viewReport();
		});
		if (target.move) {
			AssistantLive::get().glideTo(target.x + 0.5, target.y + 0.5, target.z, std::move(then), target.zoom);
		} else {
			AssistantLive::get().zoomTo(target.zoom, std::move(then));
		}
	}

	void screenshotLive(const Json& input, AssistantLive::Done done) {
		if (!currentCanvas()) {
			done(error("No map view."));
			return;
		}
		if (input.contains("x") && input.contains("y")) {
			const int z = intArg(input, "z", g_gui.GetCurrentFloor());
			AssistantLive::get().glideTo(intArg(input, "x", 0) + 0.5, intArg(input, "y", 0) + 0.5, z, guarded(std::move(done), [input]() { return captureView(input); }));
			return;
		}
		done(captureView(input));
	}
}

namespace {
	// Tool definitions as JSON text, one literal per tool, parsed once. Written
	// as nested nlohmann initializer lists they made MSVC run out of memory
	// compiling this file.
	const char* const TOOL_DEFINITIONS[] = {
		R"json({
			"name": "get_editor_state",
			"description": "Map name and size, current floor, view center, current brush, auto-border state, selection bounds and tile count. Call this first to know where the user is looking.",
			"input_schema": { "type": "object", "properties": {}, "additionalProperties": false }
		})json",
		R"json({
			"name": "list_brushes",
			"description": "Lists brush names by type (ground, wall, doodad, carpet, table, door, raw, creature, spawn, house, eraser, all) with an optional case-insensitive substring filter. Brush names must match exactly when painting.",
			"input_schema": {
				"type": "object",
				"properties": { "type": { "type": "string" }, "filter": { "type": "string" }, "limit": { "type": "integer" } },
				"additionalProperties": false
			}
		})json",
		R"json({
			"name": "read_region",
			"description": "Reads a rectangular region of one floor. Default (max 64x64): a ground-brush letter grid with a legend plus every tile's non-border items by brush name, for an overview. raw=true (max 32x32): exactly what is there, by item id: the ground ids as a letter grid, then every tile's stack bottom -> top INCLUDING border pieces, with a legend that says what each id is (border set + piece name, brush). Use raw to study how the mapper built an area piece by piece and to check your own place calls.",
			"input_schema": {
				"type": "object",
				"properties": {
					"x": { "type": "integer" }, "y": { "type": "integer" }, "z": { "type": "integer" },
					"width": { "type": "integer" }, "height": { "type": "integer" },
					"raw": { "type": "boolean" }
				},
				"required": ["x", "y", "z", "width", "height"],
				"additionalProperties": false
			}
		})json",
		R"json({
			"name": "paint",
			"description": "Paints (or erases, mode=\"erase\") with a brush using the editor's real brush pipeline, so grounds/walls/carpets get automatic borders when auto-border is on and doodads follow their composites. shape: rect (x,y,width,height), circle (x,y center + radius), points ([[x,y],...]) or selection (the user's current selection). One call is one undo step. The borders come out uniform, so only use it when the mapper asks for brushes; the normal way to build is `place`.",
			"input_schema": {
				"type": "object",
				"properties": {
					"brush": { "type": "string" },
					"mode": { "type": "string", "enum": ["draw", "erase"] },
					"shape": { "type": "string", "enum": ["rect", "circle", "points", "selection"] },
					"x": { "type": "integer" }, "y": { "type": "integer" }, "z": { "type": "integer" },
					"width": { "type": "integer" }, "height": { "type": "integer" }, "radius": { "type": "integer" },
					"points": { "type": "array", "items": { "type": "array", "items": { "type": "integer" } } }
				},
				"required": ["brush"],
				"additionalProperties": false
			}
		})json",
		R"json({
			"name": "place",
			"description": "Raw editing by item id, the main building tool: exactly the items you list end up on the tiles, with no auto-border and no brush logic, so hand-made (broken, layered) borders stay as you put them. Grounds, border pieces (see list_borders), walls, mountains and details all go in with it. Per tile: x, y (z defaults to the call's z, else the current floor). In this order: `clear` (\"all\" = ground and items, \"items\", \"borders\"); `ground` sets the ground item id (0 removes it); `remove` takes items with those ids off the stack; `add` puts items on it in the order given (the editor keeps the client's stack order, borders under other items; among pieces of the same kind a later one draws on top). Tiles are applied in the order listed while the mapper watches, and one call is one undo step. Up to 5000 tiles per call; the whole call is rejected if an id is unknown or a ground id is not a ground.",
			"input_schema": {
				"type": "object",
				"properties": {
					"tiles": {
						"type": "array",
						"items": {
							"type": "object",
							"properties": {
								"x": { "type": "integer" }, "y": { "type": "integer" }, "z": { "type": "integer" },
								"ground": { "type": "integer" },
								"add": { "type": "array", "items": { "type": "integer" } },
								"remove": { "type": "array", "items": { "type": "integer" } },
								"clear": { "type": "string", "enum": ["all", "items", "borders"] }
							},
							"required": ["x", "y"],
							"additionalProperties": false
						}
					},
					"z": { "type": "integer" },
					"description": { "type": "string", "description": "Short label shown on the map while it is placed, e.g. 'dirt road border'" }
				},
				"required": ["tiles"],
				"additionalProperties": false
			}
		})json",
		R"json({
			"name": "list_borders",
			"description": "Border sets and the item id of every piece, for building borders by hand with `place` instead of guessing ids. Find sets by ground brush or item name (`filter`), by set id (`border_id`), or the set a piece belongs to (`item`). Piece names: n/e/s/w = the edge runs along that side of the tile (for the usual outer borders the ground that owns the set lies on that side); cnw/cne/csw/cse = small corner, the owning ground only touches that diagonal neighbour; dnw/dne/dse/dsw = the owning ground lies on both sides of that corner (dnw: north and west). A tile can carry several pieces, even from different sets. 'used by' tells which ground brushes place the set: outer = on the neighbouring tiles, inner = on its own edge tiles, to = the ground it borders against. Pieces with several ids list their chances.",
			"input_schema": {
				"type": "object",
				"properties": { "filter": { "type": "string" }, "border_id": { "type": "integer" }, "item": { "type": "integer" }, "limit": { "type": "integer" } },
				"additionalProperties": false
			}
		})json",
		R"json({
			"name": "border_pieces",
			"description": "Works out the complete standard border of one set (border_id from list_borders) around a patch of ground, piece by piece (sides, corners and diagonals), like the editor's auto-border would, and returns it ready for `place` without placing anything. The patch is either `points` ([[x,y],...]) or an area (x, y, width, height, max 64x64) plus `ground`: the ground item id(s) that form it. align \"outer\" (default) puts the pieces on the tiles around the patch, \"inner\" on the patch's own edge. `edges` keeps only some piece names. A piece is named after where the patch lies seen from its tile, so the tiles along the patch's south-west side carry e, n, dne and cne: [\"e\",\"n\",\"dne\",\"cne\"] gives a cliff edge along the west and south sides only. Start from this so no piece is forgotten, then break, drop or layer pieces by hand where the style asks.",
			"input_schema": {
				"type": "object",
				"properties": {
					"border_id": { "type": ["string", "integer"] },
					"x": { "type": "integer" }, "y": { "type": "integer" }, "z": { "type": "integer" },
					"width": { "type": "integer" }, "height": { "type": "integer" },
					"ground": { "type": "array", "items": { "type": "integer" } },
					"points": { "type": "array", "items": { "type": "array", "items": { "type": "integer" } } },
					"align": { "type": "string", "enum": ["outer", "inner"] },
					"edges": { "type": "array", "items": { "type": "string", "enum": ["n", "e", "s", "w", "cnw", "cne", "csw", "cse", "dnw", "dne", "dse", "dsw"] } }
				},
				"required": ["border_id"],
				"additionalProperties": false
			}
		})json",
		R"json({
			"name": "check_region",
			"description": "Reviews an area (max 64x64) for the mistakes that are easy to miss in a screenshot: different grounds touching with no border piece drawn along that side on either tile (the square tile edge shows; corners do not count, a piece on another side does not count), border pieces floating in the middle of a single ground, multi-tile objects (coffins, beds, big statues...) missing one of their pieces, and straight ground edges of 6+ tiles (the style wants irregular outlines). Run it on everything you built before you say you are done, and fix what it reports unless it is a deliberate style choice.",
			"input_schema": {
				"type": "object",
				"properties": {
					"x": { "type": "integer" }, "y": { "type": "integer" }, "z": { "type": "integer" },
					"width": { "type": "integer" }, "height": { "type": "integer" }
				},
				"required": ["x", "y", "z", "width", "height"],
				"additionalProperties": false
			}
		})json",
		R"json({
			"name": "run_lua",
			"description": "Runs a Lua snippet inside the editor with the scripting API described in the system prompt (app.map, tiles, items, geo, noise, algo, Brushes...). Map edits are automatically wrapped in app.transaction (one undo step) unless the code calls it itself. print() output comes back as the result; errors come back as errors. Use it for procedural generation (noise, cellular automata, scatter) and for anything the other tools do not cover.",
			"input_schema": {
				"type": "object",
				"properties": {
					"code": { "type": "string" },
					"description": { "type": "string", "description": "Short undo label, e.g. 'Forest north of town'" }
				},
				"required": ["code"],
				"additionalProperties": false
			}
		})json",
		R"json({
			"name": "screenshot",
			"description": "Returns a PNG of the map view so you can look at the result. Optional x, y, z center the view there first. zoom takes this one picture at another zoom (1 = the game's own scale, 32 px per tile; 0.5 = twice as close; 2 = twice as far out) and gives the user's zoom back afterwards (to leave the user's view zoomed, use set_view); width/height (tiles) crop it around the center. ground_only hides everything but grounds and border pieces (like the mapper's Shift+B), the way to check borders without trees and rocks in front. grid true/false draws or hides the tile grid (Shift+G) in this picture only: with it on every grid cell is one tile, and the result gives the pixel where a known tile starts, so you can tell exactly which tile a detail is on. A whole-screen picture is scaled down so much that missing border pieces and small details disappear: to check your work, use zoom 1 with width/height around the area you changed (e.g. 30x20), part by part if it is big.",
			"input_schema": {
				"type": "object",
				"properties": {
					"x": { "type": "integer" }, "y": { "type": "integer" }, "z": { "type": "integer" },
					"zoom": { "type": "number" },
					"ground_only": { "type": "boolean" },
					"grid": { "type": "boolean" },
					"width": { "type": "integer" }, "height": { "type": "integer" }
				},
				"additionalProperties": false
			}
		})json",
		R"json({
			"name": "set_view",
			"description": "Moves and zooms the user's map view (the camera glides there while the mapper watches). x, y, z center it on a tile (each defaults to where the view is now). zoom sets how close it is: 1 = the game's own scale (32 px per tile), below 1 zooms in (0.5 = 64 px per tile, 0.25 is the closest), above 1 zooms out (2 = 16 px per tile, 8 is the farthest). Instead of zoom, width/height (tiles) pick the zoom at which that area fills the screen. The zoom stays until changed, so later screenshots use it too. Zoom in to look closely at small details or borders, zoom out to see a whole area at once; get_editor_state tells the current zoom and how many tiles are visible.",
			"input_schema": {
				"type": "object",
				"properties": {
					"x": { "type": "integer" }, "y": { "type": "integer" }, "z": { "type": "integer" },
					"zoom": { "type": "number" },
					"width": { "type": "integer" }, "height": { "type": "integer" }
				},
				"additionalProperties": false
			}
		})json",
		R"json({
			"name": "set_display",
			"description": "Switches the mapper's grounding view, for the user's screen and your screenshots alike. flat_colors turns on the Flat Colors screen shader (every colour family collapsed into one flat tone, so each ground, its border pieces and every seam, straight edge or missing corner stand out); \"on\" restores the last variant used, or pick \"normal\", \"strong\" or \"extreme\"; \"off\" gives the user's own shader back. ground_only is Shift+B (only grounds and border pieces drawn). grid is Shift+G: it draws the tile grid for the user and in your screenshots until turned off, so each tile's position in a close-up is exact (the screenshot result gives the pixel where a known tile starts); screenshot also takes grid for one picture. Turn flat_colors and ground_only on while you lay grounds and borders (GROUNDING) and turn them off when you move on to details (nature, objects, decoration). With no arguments it reports the current state.",
			"input_schema": {
				"type": "object",
				"properties": {
					"flat_colors": { "type": "string", "enum": ["on", "off", "normal", "strong", "extreme"] },
					"ground_only": { "type": "boolean" },
					"grid": { "type": "boolean" }
				},
				"additionalProperties": false
			}
		})json",
		R"json({
			"name": "undo",
			"description": "Undoes the last N editor steps (default 1). Use it to revert your own place/paint/run_lua calls when the result is wrong.",
			"input_schema": { "type": "object", "properties": { "steps": { "type": "integer" } }, "additionalProperties": false }
		})json",
	};
}

Json ClaudeTools::definitions() {
	static const Json defs = [] {
		Json list = Json::array();
		for (const char* text : TOOL_DEFINITIONS) {
			list.push_back(Json::parse(text));
		}
		return list;
	}();
	return defs;
}

ClaudeToolResult ClaudeTools::execute(const std::string& name, const Json& input) {
	try {
		if (name == "get_editor_state") {
			return getEditorState();
		}
		if (name == "list_brushes") {
			return listBrushes(input);
		}
		if (name == "read_region") {
			return readRegion(input);
		}
		if (name == "paint") {
			return paint(input);
		}
		if (name == "run_lua") {
			return runLua(input);
		}
		if (name == "place") {
			return place(input);
		}
		if (name == "list_borders") {
			return listBorders(input);
		}
		if (name == "border_pieces") {
			return borderPieces(input);
		}
		if (name == "check_region") {
			return checkRegion(input);
		}
		if (name == "screenshot") {
			return screenshot(input);
		}
		if (name == "set_view") {
			return setView(input);
		}
		if (name == "set_display") {
			return setDisplay(input);
		}
		if (name == "undo") {
			return undo(input);
		}
		return error("Unknown tool '" + name + "'.");
	} catch (const std::exception& e) {
		return error(std::string("Tool '") + name + "' failed: " + e.what());
	} catch (...) {
		return error(std::string("Tool '") + name + "' failed with an unknown error.");
	}
}

void ClaudeTools::executeAsync(const std::string& name, const Json& input, std::function<void(ClaudeToolResult)> done) {
	AssistantLive::get().enqueue(
		[name, input](AssistantLive::Done finish) {
			// `finish` is passed by copy: the catch below may still need it.
			try {
				if (name == "paint") {
					paintLive(input, finish);
				} else if (name == "place") {
					placeLive(input, finish);
				} else if (name == "run_lua") {
					runLuaLive(input, finish);
				} else if (name == "read_region") {
					readRegionLive(input, finish);
				} else if (name == "set_view") {
					setViewLive(input, finish);
				} else if (name == "screenshot") {
					screenshotLive(input, finish);
				} else {
					finish(ClaudeTools::execute(name, input));
				}
			} catch (const std::exception& e) {
				finish(error(std::string("Tool '") + name + "' failed: " + e.what()));
			} catch (...) {
				finish(error(std::string("Tool '") + name + "' failed with an unknown error."));
			}
		},
		std::move(done)
	);
}
