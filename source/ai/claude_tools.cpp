//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ai/claude_tools.h"

#include "app/settings.h"
#include "brushes/brush.h"
#include "brushes/carpet/carpet_brush.h"
#include "brushes/creature/creature_brush.h"
#include "brushes/doodad/doodad_brush.h"
#include "brushes/door/door_brush.h"
#include "brushes/ground/ground_brush.h"
#include "brushes/house/house_brush.h"
#include "brushes/raw/raw_brush.h"
#include "brushes/spawn/spawn_brush.h"
#include "brushes/table/table_brush.h"
#include "brushes/wall/wall_brush.h"
#include "editor/action_queue.h"
#include "editor/editor.h"
#include "editor/selection.h"
#include "game/creature.h"
#include "game/item.h"
#include "lua/lua_engine.h"
#include "lua/lua_script_manager.h"
#include "map/map.h"
#include "map/tile.h"
#include "rendering/ui/map_display.h"
#include "rendering/ui/screenshot_controller.h"
#include "ui/gui.h"
#include "ui/map_tab.h"

#include <algorithm>
#include <cmath>
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
			out["view"] = { { "x", center.x }, { "y", center.y }, { "z", center.z } };
		}
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

	// ---- read_region ------------------------------------------------------
	ClaudeToolResult readRegion(const Json& input) {
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

	// ---- paint ------------------------------------------------------------
	ClaudeToolResult paint(const Json& input) {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			return error("No map is open.");
		}
		const std::string brush_name = strArg(input, "brush");
		Brush* brush = g_brushes.getBrush(brush_name);
		if (!brush) {
			return error("Unknown brush '" + brush_name + "'. Use list_brushes to find the exact name.");
		}
		const bool erase = strArg(input, "mode", "draw") == "erase";
		const std::string shape = strArg(input, "shape", "rect");
		const int z = intArg(input, "z", g_gui.GetCurrentFloor());
		if (z < 0 || z > MAP_MAX_LAYER) {
			return error("z must be between 0 and 15.");
		}

		std::vector<Position> positions;
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
				return error("rect needs x, y, width, height.");
			}
			if (static_cast<size_t>(w) * static_cast<size_t>(h) > MAX_PAINT_TILES) {
				return error("Area too large (max " + std::to_string(MAX_PAINT_TILES) + " tiles).");
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
				return error("circle needs x, y (center) and radius (0-300).");
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
				return error("points needs a `points` array of [x, y] pairs.");
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
				return error("Nothing is selected.");
			}
		} else {
			return error("shape must be rect, circle, points or selection.");
		}
		if (positions.empty()) {
			return error("No valid positions.");
		}
		if (positions.size() > MAX_PAINT_TILES) {
			return error("Too many tiles (max " + std::to_string(MAX_PAINT_TILES) + ").");
		}

		if (!g_gui.SelectBrush(brush)) {
			// Some palettes refuse the switch; painting still needs it to be current.
			if (g_gui.GetCurrentBrush() != brush) {
				return error("Could not activate brush '" + brush_name + "'.");
			}
		}

		if (brush->is<DoodadBrush>()) {
			// One composite per tile with re-rolls, like smearing the brush.
			for (const Position& pos : positions) {
				if (erase) {
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
			if (erase) {
				editor->undraw(positions, toborder, false);
			} else {
				editor->draw(positions, toborder, false);
			}
		} else {
			if (erase) {
				editor->undraw(positions, false);
			} else {
				editor->draw(positions, false);
			}
		}
		g_gui.RefreshView();

		ClaudeToolResult r;
		r.text = std::string(erase ? "Erased" : "Painted") + " " + std::to_string(positions.size()) + " tile(s) with '" + brush->getName() + "' (" + brushType(brush) + ") on floor " + std::to_string(z) + ". This is one undo step.";
		r.summary = std::string(erase ? "erased " : "painted ") + std::to_string(positions.size()) + " tiles with " + brush->getName();
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
	ClaudeToolResult screenshot(const Json& input) {
		MapCanvas* canvas = currentCanvas();
		if (!canvas) {
			return error("No map view.");
		}
		if (input.contains("x") && input.contains("y")) {
			const int z = intArg(input, "z", g_gui.GetCurrentFloor());
			g_gui.SetScreenCenterPosition(Position(intArg(input, "x", 0), intArg(input, "y", 0), z));
		}
		wxImage image = canvas->screenshot_controller->CaptureImage();
		if (!image.IsOk()) {
			return error("Could not capture the map view.");
		}
		if (image.GetWidth() > MAX_SCREENSHOT_WIDTH) {
			const double scale = static_cast<double>(MAX_SCREENSHOT_WIDTH) / image.GetWidth();
			image = image.Scale(MAX_SCREENSHOT_WIDTH, std::max(1, static_cast<int>(std::lround(image.GetHeight() * scale))), wxIMAGE_QUALITY_BOX_AVERAGE);
		}
		wxMemoryOutputStream stream;
		if (!image.SaveFile(stream, wxBITMAP_TYPE_PNG)) {
			return error("PNG encoding failed.");
		}
		wxStreamBuffer* buffer = stream.GetOutputStreamBuffer();
		const wxString b64 = wxBase64Encode(buffer->GetBufferStart(), buffer->GetBufferSize());

		MapTab* tab = g_gui.GetCurrentMapTab();
		const Position center = tab ? tab->GetScreenCenterPosition() : Position();
		int vsx, vsy, sw, sh;
		canvas->GetViewBox(&vsx, &vsy, &sw, &sh);
		const int tiles_w = static_cast<int>(sw * canvas->zoom / TILE_SIZE);
		const int tiles_h = static_cast<int>(sh * canvas->zoom / TILE_SIZE);

		ClaudeToolResult r;
		r.image_png_base64 = std::string(b64.mb_str());
		r.text = "Screenshot of the current map view. Center x=" + std::to_string(center.x) + " y=" + std::to_string(center.y) + " z=" + std::to_string(center.z) + ", about " + std::to_string(tiles_w) + "x" + std::to_string(tiles_h) + " tiles visible (zoom " + std::to_string(canvas->zoom) + "). Top-left tile is roughly x=" + std::to_string(center.x - tiles_w / 2) + " y=" + std::to_string(center.y - tiles_h / 2) + ".";
		r.summary = "screenshot at " + std::to_string(center.x) + "," + std::to_string(center.y) + "," + std::to_string(center.z);
		return r;
	}

	// ---- set_view ---------------------------------------------------------
	ClaudeToolResult setView(const Json& input) {
		if (!g_gui.GetCurrentMapTab()) {
			return error("No map view.");
		}
		const int x = intArg(input, "x", -1), y = intArg(input, "y", -1);
		const int z = intArg(input, "z", g_gui.GetCurrentFloor());
		if (x < 0 || y < 0 || z < 0 || z > MAP_MAX_LAYER) {
			return error("x, y and z (0-15) are required.");
		}
		g_gui.SetScreenCenterPosition(Position(x, y, z));
		g_gui.RefreshView();
		ClaudeToolResult r;
		r.text = "View centered at " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + ".";
		r.summary = "view moved to " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
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
}

Json ClaudeTools::definitions() {
	return Json::array({
		{
			{ "name", "get_editor_state" },
			{ "description", "Map name and size, current floor, view center, current brush, auto-border state, selection bounds and tile count. Call this first to know where the user is looking." },
			{ "input_schema", { { "type", "object" }, { "properties", Json::object() }, { "additionalProperties", false } } },
		},
		{
			{ "name", "list_brushes" },
			{ "description", "Lists brush names by type (ground, wall, doodad, carpet, table, door, raw, creature, spawn, house, eraser, all) with an optional case-insensitive substring filter. Brush names must match exactly when painting." },
			{ "input_schema", { { "type", "object" }, { "properties", { { "type", { { "type", "string" } } }, { "filter", { { "type", "string" } } }, { "limit", { { "type", "integer" } } } } }, { "additionalProperties", false } } },
		},
		{
			{ "name", "read_region" },
			{ "description", "Reads a rectangular region (max 64x64) of one floor: a ground-brush letter grid with a legend plus every tile's non-border items (walls, doodads, creatures) by brush name. Use it to inspect what is already on the map before and after painting." },
			{ "input_schema", { { "type", "object" }, { "properties", { { "x", { { "type", "integer" } } }, { "y", { { "type", "integer" } } }, { "z", { { "type", "integer" } } }, { "width", { { "type", "integer" } } }, { "height", { { "type", "integer" } } } } }, { "required", Json::array({ "x", "y", "z", "width", "height" }) }, { "additionalProperties", false } } },
		},
		{
			{ "name", "paint" },
			{ "description", "Paints (or erases, mode=\"erase\") with a brush using the editor's real brush pipeline, so grounds/walls/carpets get automatic borders when auto-border is on and doodads follow their composites. shape: rect (x,y,width,height), circle (x,y center + radius), points ([[x,y],...]) or selection (the user's current selection). One call is one undo step. Prefer this over run_lua for terrain." },
			{ "input_schema", { { "type", "object" }, { "properties", { { "brush", { { "type", "string" } } }, { "mode", { { "type", "string" }, { "enum", Json::array({ "draw", "erase" }) } } }, { "shape", { { "type", "string" }, { "enum", Json::array({ "rect", "circle", "points", "selection" }) } } }, { "x", { { "type", "integer" } } }, { "y", { { "type", "integer" } } }, { "z", { { "type", "integer" } } }, { "width", { { "type", "integer" } } }, { "height", { { "type", "integer" } } }, { "radius", { { "type", "integer" } } }, { "points", { { "type", "array" }, { "items", { { "type", "array" }, { "items", { { "type", "integer" } } } } } } } } }, { "required", Json::array({ "brush" }) }, { "additionalProperties", false } } },
		},
		{
			{ "name", "run_lua" },
			{ "description", "Runs a Lua snippet inside the editor with the scripting API described in the system prompt (app.map, tiles, items, geo, noise, algo, Brushes...). Map edits are automatically wrapped in app.transaction (one undo step) unless the code calls it itself. print() output comes back as the result; errors come back as errors. Use it for procedural generation (noise, cellular automata, scatter) and for anything the other tools do not cover." },
			{ "input_schema", { { "type", "object" }, { "properties", { { "code", { { "type", "string" } } }, { "description", { { "type", "string" }, { "description", "Short undo label, e.g. 'Forest north of town'" } } } } }, { "required", Json::array({ "code" }) }, { "additionalProperties", false } } },
		},
		{
			{ "name", "screenshot" },
			{ "description", "Returns a PNG of the current map view so you can look at the result. Optional x, y, z center the view there first. Use it to check your work visually before telling the user you are done." },
			{ "input_schema", { { "type", "object" }, { "properties", { { "x", { { "type", "integer" } } }, { "y", { { "type", "integer" } } }, { "z", { { "type", "integer" } } } } }, { "additionalProperties", false } } },
		},
		{
			{ "name", "set_view" },
			{ "description", "Centers the user's map view on a position and floor." },
			{ "input_schema", { { "type", "object" }, { "properties", { { "x", { { "type", "integer" } } }, { "y", { { "type", "integer" } } }, { "z", { { "type", "integer" } } } } }, { "required", Json::array({ "x", "y" }) }, { "additionalProperties", false } } },
		},
		{
			{ "name", "undo" },
			{ "description", "Undoes the last N editor steps (default 1). Use it to revert your own paint/run_lua calls when the result is wrong." },
			{ "input_schema", { { "type", "object" }, { "properties", { { "steps", { { "type", "integer" } } } } }, { "additionalProperties", false } } },
		},
	});
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
		if (name == "screenshot") {
			return screenshot(input);
		}
		if (name == "set_view") {
			return setView(input);
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
