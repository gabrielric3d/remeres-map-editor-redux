//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/ui/selection_controller.h"
#include "editor/selection_thread.h"
#include "rendering/ui/map_display.h"
#include "editor/editor.h"
#include "editor/action_queue.h"
#include "map/map.h"
#include "map/tile.h"
#include "game/item.h"
#include "game/spawn.h"
#include "game/creature.h"
#include "app/settings.h"
#include "ui/gui.h"
#include "rendering/ui/brush_selector.h"
#include "brushes/creature/creature_brush.h"
#include "brushes/raw/raw_brush.h"
#include "ui/dialog_helper.h"
#include "ui/dialogs/structure_manager_window.h"

namespace {
	bool IsPointInPolygon(const std::vector<wxPoint>& polygon, double x, double y) {
		if (polygon.size() < 3) {
			return false;
		}

		bool inside = false;
		size_t count = polygon.size();
		for (size_t i = 0, j = count - 1; i < count; j = i++) {
			double xi = polygon[i].x;
			double yi = polygon[i].y;
			double xj = polygon[j].x;
			double yj = polygon[j].y;

			bool intersect = ((yi > y) != (yj > y)) &&
				(x < (xj - xi) * (y - yi) / (yj - yi) + xi);
			if (intersect) {
				inside = !inside;
			}
		}
		return inside;
	}
}

SelectionController::SelectionController(MapCanvas* canvas, Editor& editor) :
	canvas(canvas),
	editor(editor),
	dragging(false),
	boundbox_selection(false),
	lasso_active(false),
	drag_start_pos(Position()) {
}

SelectionController::~SelectionController() {
}

bool SelectionController::IsLassoEnabled() const {
	return g_settings.getBoolean(Config::SELECTION_LASSO);
}

bool SelectionController::HasLassoSelection() const {
	return lasso_active && lasso_map_points.size() >= 3;
}

void SelectionController::ClearLassoSelection() {
	lasso_screen_points.clear();
	lasso_map_points.clear();
	lasso_active = false;
}

void SelectionController::StartLassoSelection(int screen_x, int screen_y, int map_x, int map_y) {
	ClearLassoSelection();
	lasso_active = true;
	AddLassoPoint(screen_x, screen_y, map_x, map_y);
}

void SelectionController::AddLassoPoint(int screen_x, int screen_y, int map_x, int map_y) {
	if (!lasso_screen_points.empty()) {
		const wxPoint& last = lasso_screen_points.back();
		int dx = screen_x - last.x;
		int dy = screen_y - last.y;
		if (dx * dx + dy * dy < LASSO_MIN_DISTANCE_SQ) {
			return;
		}
	}

	lasso_screen_points.push_back(wxPoint(screen_x, screen_y));
	lasso_map_points.push_back(wxPoint(map_x, map_y));
}

void SelectionController::HandleClick(const Position& mouse_map_pos, bool shift_down, bool ctrl_down, bool alt_down) {
	if (ctrl_down && alt_down) {
		Tile* tile = editor.map.getTile(mouse_map_pos);
		if (tile && tile->size() > 0) {
			// Select visible creature
			if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
				CreatureBrush* brush = tile->creature->getBrush();
				if (brush) {
					g_gui.SelectBrush(brush, TILESET_CREATURE);
					return;
				}
			}
			// Fall back to item selection
			Item* item = tile->getTopItem();
			if (item && item->getRAWBrush()) {
				g_gui.SelectBrush(item->getRAWBrush(), TILESET_RAW);
			}
		}
	} else if (g_gui.IsSelectionMode()) {
		if (canvas->isPasting()) {
			// Set paste to false (no rendering etc.)
			canvas->EndPasting();

			// Paste to the map
			editor.copybuffer.paste(editor, mouse_map_pos);

			if (StructureManagerDialog::IsKeepPasteActive()) {
				// Re-enter pasting mode immediately
				g_gui.PreparePaste();
			} else {
				// Start dragging
				dragging = true;
				drag_start_pos = mouse_map_pos;
			}
		} else {
			boundbox_selection = false;
			if (shift_down) {
				boundbox_selection = true;

				if (IsLassoEnabled()) {
					int screen_x = canvas->cursor_x;
					int screen_y = canvas->cursor_y;
					StartLassoSelection(screen_x, screen_y, mouse_map_pos.x, mouse_map_pos.y);
				}

				if (!ctrl_down) {
					editor.selection.start(); // Start selection session
					editor.selection.clear(); // Clear out selection
					editor.selection.finish(); // End selection session
					editor.selection.updateSelectionCount();
				}
			} else if (ctrl_down) {
				Tile* tile = editor.map.getTile(mouse_map_pos);
				if (tile) {
					if (tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
						editor.selection.start(); // Start selection session
						if (tile->spawn->isSelected()) {
							editor.selection.remove(tile, tile->spawn.get());
						} else {
							editor.selection.add(tile, tile->spawn.get());
						}
						editor.selection.finish(); // Finish selection session
						editor.selection.updateSelectionCount();
					} else if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
						editor.selection.start(); // Start selection session
						if (tile->creature->isSelected()) {
							editor.selection.remove(tile, tile->creature.get());
						} else {
							editor.selection.add(tile, tile->creature.get());
						}
						editor.selection.finish(); // Finish selection session
						editor.selection.updateSelectionCount();
					} else {
						Item* item = tile->getTopItem();
						if (item) {
							editor.selection.start(); // Start selection session
							if (item->isSelected()) {
								editor.selection.remove(tile, item);
							} else {
								editor.selection.add(tile, item);
							}
							editor.selection.finish(); // Finish selection session
							editor.selection.updateSelectionCount();
						}
					}
				}
			} else {
				ClearLassoSelection();
				Tile* tile = editor.map.getTile(mouse_map_pos);
				if (!tile) {
					editor.selection.start(); // Start selection session
					editor.selection.clear(); // Clear out selection
					editor.selection.finish(); // End selection session
					editor.selection.updateSelectionCount();
				} else if (tile->isSelected()) {
					dragging = true;
					drag_start_pos = mouse_map_pos;
				} else {
					editor.selection.start(); // Start a selection session
					editor.selection.clear();
					editor.selection.commit();
					if (tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
						editor.selection.add(tile, tile->spawn.get());
						dragging = true;
						drag_start_pos = mouse_map_pos;
					} else if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
						editor.selection.add(tile, tile->creature.get());
						dragging = true;
						drag_start_pos = mouse_map_pos;
					} else {
						Item* item = tile->getTopItem();
						if (item) {
							editor.selection.add(tile, item);
							dragging = true;
							drag_start_pos = mouse_map_pos;
						}
					}
					editor.selection.finish(); // Finish the selection session
					editor.selection.updateSelectionCount();
				}
			}
		}
	}
}

void SelectionController::HandleDrag(const Position& mouse_map_pos, bool shift_down, bool ctrl_down, bool alt_down) {
	if (g_gui.IsSelectionMode()) {
		if (canvas->isPasting()) {
			canvas->Refresh();
		} else if (dragging) {
			wxString ss;

			int move_x = drag_start_pos.x - mouse_map_pos.x;
			int move_y = drag_start_pos.y - mouse_map_pos.y;
			int move_z = drag_start_pos.z - mouse_map_pos.z;
			ss << "Dragging " << -move_x << "," << -move_y << "," << -move_z;
			g_gui.SetStatusText(ss);

			canvas->Refresh();
		} else if (boundbox_selection) {
			if (IsLassoEnabled() && lasso_active) {
				int screen_x = canvas->cursor_x;
				int screen_y = canvas->cursor_y;
				AddLassoPoint(screen_x, screen_y, mouse_map_pos.x, mouse_map_pos.y);

				wxString ss;
				ss << "Lasso Selection (" << lasso_map_points.size() << " points)";
				g_gui.SetStatusText(ss);
			} else {
				// Calculate selection size
				int move_x = std::abs(canvas->last_click_map_x - mouse_map_pos.x);
				int move_y = std::abs(canvas->last_click_map_y - mouse_map_pos.y);
				wxString ss;
				ss << "Selection " << move_x + 1 << ":" << move_y + 1;
				g_gui.SetStatusText(ss);
			}

			canvas->Refresh();
		}
	}
}

void SelectionController::HandleRelease(const Position& mouse_map_pos, bool shift_down, bool ctrl_down, bool alt_down) {
	int move_x = canvas->last_click_map_x - mouse_map_pos.x;
	int move_y = canvas->last_click_map_y - mouse_map_pos.y;
	int move_z = canvas->last_click_map_z - mouse_map_pos.z;

	if (g_gui.IsSelectionMode()) {
		if (dragging && (move_x != 0 || move_y != 0 || move_z != 0)) {
			editor.moveSelection(Position(move_x, move_y, move_z));
		} else {
			if (boundbox_selection) {
				if (IsLassoEnabled() && lasso_active) {
					// Add final point
					AddLassoPoint(canvas->cursor_x, canvas->cursor_y, mouse_map_pos.x, mouse_map_pos.y);

					if (lasso_map_points.size() >= 3) {
						ExecuteLassoSelection(mouse_map_pos.z);
					}
					ClearLassoSelection();
				} else if (mouse_map_pos.x == canvas->last_click_map_x && mouse_map_pos.y == canvas->last_click_map_y && ctrl_down) {
					// Mouse hasn't moved, do control+shift thingy!
					Tile* tile = editor.map.getTile(mouse_map_pos);
					if (tile) {
						editor.selection.start(); // Start a selection session
						if (tile->isSelected()) {
							editor.selection.remove(tile);
						} else {
							editor.selection.add(tile);
						}
						editor.selection.finish(); // Finish the selection session
						editor.selection.updateSelectionCount();
					}
				} else {
					ExecuteBoundboxSelection(Position(canvas->last_click_map_x, canvas->last_click_map_y, canvas->last_click_map_z), mouse_map_pos, mouse_map_pos.z);
				}
			} else if (ctrl_down) {
				////
			} else {
				// User hasn't moved anything, meaning selection/deselection
				Tile* tile = editor.map.getTile(mouse_map_pos);
				if (tile) {
					if (tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
						if (!tile->spawn->isSelected()) {
							editor.selection.start(); // Start a selection session
							editor.selection.add(tile, tile->spawn.get());
							editor.selection.finish(); // Finish the selection session
							editor.selection.updateSelectionCount();
						}
					} else if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
						if (!tile->creature->isSelected()) {
							editor.selection.start(); // Start a selection session
							editor.selection.add(tile, tile->creature.get());
							editor.selection.finish(); // Finish the selection session
							editor.selection.updateSelectionCount();
						}
					} else {
						Item* item = tile->getTopItem();
						if (item && !item->isSelected()) {
							editor.selection.start(); // Start a selection session
							editor.selection.add(tile, item);
							editor.selection.finish(); // Finish the selection session
							editor.selection.updateSelectionCount();
						}
					}
				}
			}
		}
		editor.actionQueue->resetTimer();
		dragging = false;
		boundbox_selection = false;
	}
}

void SelectionController::HandlePropertiesClick(const Position& mouse_map_pos, bool shift_down, bool ctrl_down, bool alt_down) {
	Tile* tile = editor.map.getTile(mouse_map_pos);

	if (g_gui.IsDrawingMode()) {
		g_gui.SetSelectionMode();
	}

	canvas->EndPasting();

	boundbox_selection = false;
	if (shift_down) {
		boundbox_selection = true;

		if (IsLassoEnabled()) {
			int screen_x = canvas->cursor_x;
			int screen_y = canvas->cursor_y;
			StartLassoSelection(screen_x, screen_y, mouse_map_pos.x, mouse_map_pos.y);
		}

		if (!ctrl_down) {
			editor.selection.start(); // Start selection session
			editor.selection.clear(); // Clear out selection
			editor.selection.finish(); // End selection session
			editor.selection.updateSelectionCount();
		}
	} else if (!tile) {
		editor.selection.start(); // Start selection session
		editor.selection.clear(); // Clear out selection
		editor.selection.finish(); // End selection session
		editor.selection.updateSelectionCount();
	} else if (tile->isSelected()) {
		// Do nothing!
	} else {
		editor.selection.start(); // Start a selection session
		editor.selection.clear();
		editor.selection.commit();
		if (tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
			editor.selection.add(tile, tile->spawn.get());
		} else if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
			editor.selection.add(tile, tile->creature.get());
		} else {
			Item* item = tile->getTopItem();
			if (item) {
				editor.selection.add(tile, item);
			}
		}
		editor.selection.finish(); // Finish the selection session
		editor.selection.updateSelectionCount();
	}
}

void SelectionController::HandlePropertiesRelease(const Position& mouse_map_pos, bool shift_down, bool ctrl_down, bool alt_down) {
	if (g_gui.IsDrawingMode()) {
		g_gui.SetSelectionMode();
	}

	if (boundbox_selection) {
		if (IsLassoEnabled() && lasso_active) {
			AddLassoPoint(canvas->cursor_x, canvas->cursor_y, mouse_map_pos.x, mouse_map_pos.y);

			if (lasso_map_points.size() >= 3) {
				ExecuteLassoSelection(mouse_map_pos.z);
			}
			ClearLassoSelection();
		} else if (mouse_map_pos.x == canvas->last_click_map_x && mouse_map_pos.y == canvas->last_click_map_y && ctrl_down) {
			// Mouse hasn't move, do control+shift thingy!
			Tile* tile = editor.map.getTile(mouse_map_pos);
			if (tile) {
				editor.selection.start(); // Start a selection session
				if (tile->isSelected()) {
					editor.selection.remove(tile);
				} else {
					editor.selection.add(tile);
				}
				editor.selection.finish(); // Finish the selection session
				editor.selection.updateSelectionCount();
			}
		} else {
			ExecuteBoundboxSelection(Position(canvas->last_click_map_x, canvas->last_click_map_y, canvas->last_click_map_z), mouse_map_pos, mouse_map_pos.z);
		}
	} else if (ctrl_down) {
		// Nothing
	}

	editor.actionQueue->resetTimer();
	dragging = false;
	boundbox_selection = false;
}

void SelectionController::HandleDoubleClick(const Position& mouse_map_pos) {
	if (g_settings.getInteger(Config::DOUBLECLICK_PROPERTIES)) {
		Tile* tile = editor.map.getTile(mouse_map_pos);
		if (tile) {
			DialogHelper::OpenProperties(editor, tile);
		}
	}
}

void SelectionController::ExecuteBoundboxSelection(const Position& start_pos, const Position& end_pos, int floor) {
	int start_x = start_pos.x;
	int start_y = start_pos.y;
	int end_x = end_pos.x;
	int end_y = end_pos.y;

	if (start_x > end_x) {
		std::swap(start_x, end_x);
	}
	if (start_y > end_y) {
		std::swap(start_y, end_y);
	}

	int numtiles = 0;
	int threadcount = std::max(g_settings.getInteger(Config::WORKER_THREADS), 1);

	int s_x = 0, s_y = 0, s_z = 0;
	int e_x = 0, e_y = 0, e_z = 0;

	switch (g_settings.getInteger(Config::SELECTION_TYPE)) {
		case SELECT_CURRENT_FLOOR: {
			s_z = e_z = floor;
			s_x = start_x;
			s_y = start_y;
			e_x = end_x;
			e_y = end_y;
			break;
		}
		case SELECT_ALL_FLOORS: {
			s_x = start_x;
			s_y = start_y;
			s_z = MAP_MAX_LAYER;
			e_x = end_x;
			e_y = end_y;
			e_z = floor;

			if (g_settings.getInteger(Config::COMPENSATED_SELECT)) {
				s_x -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
				s_y -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);

				e_x -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
				e_y -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
			}

			numtiles = (s_z - e_z) * (e_x - s_x) * (e_y - s_y);
			break;
		}
		case SELECT_VISIBLE_FLOORS: {
			s_x = start_x;
			s_y = start_y;
			if (floor <= GROUND_LAYER) {
				s_z = GROUND_LAYER;
			} else {
				s_z = std::min(MAP_MAX_LAYER, floor + 2);
			}
			e_x = end_x;
			e_y = end_y;
			e_z = floor;

			if (g_settings.getInteger(Config::COMPENSATED_SELECT)) {
				s_x -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
				s_y -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);

				e_x -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
				e_y -= (floor < GROUND_LAYER ? GROUND_LAYER - floor : 0);
			}
			break;
		}
	}

	if (numtiles < 500) {
		// No point in threading for such a small set.
		threadcount = 1;
	}
	// Subdivide the selection area
	// We know it's a square, just split it into several areas
	int width = e_x - s_x;
	if (width < threadcount) {
		threadcount = std::min(1, width);
	}
	// Let's divide!
	int remainder = width;
	int cleared = 0;
	std::vector<std::unique_ptr<SelectionThread>> threads;
	if (width == 0) {
		threads.push_back(std::make_unique<SelectionThread>(editor, Position(s_x, s_y, s_z), Position(s_x, e_y, e_z)));
	} else {
		for (int i = 0; i < threadcount; ++i) {
			int chunksize = width / threadcount;
			// The last threads takes all the remainder
			if (i == threadcount - 1) {
				chunksize = remainder;
			}
			threads.push_back(std::make_unique<SelectionThread>(editor, Position(s_x + cleared, s_y, s_z), Position(s_x + cleared + chunksize, e_y, e_z)));
			cleared += chunksize;
			remainder -= chunksize;
		}
	}
	ASSERT(cleared == width);
	ASSERT(remainder == 0);

	editor.selection.start(); // Start a selection session
	for (auto& thread : threads) {
		thread->Start();
	}
	for (auto& thread : threads) {
		editor.selection.join(std::move(thread));
	}
	editor.selection.finish(); // Finish the selection session
	editor.selection.updateSelectionCount();
}

void SelectionController::ExecuteLassoSelection(int floor) {
	if (lasso_map_points.size() < 3) {
		return;
	}

	// Compute bounding box of the polygon
	int min_x = lasso_map_points.front().x;
	int max_x = lasso_map_points.front().x;
	int min_y = lasso_map_points.front().y;
	int max_y = lasso_map_points.front().y;
	for (const wxPoint& point : lasso_map_points) {
		min_x = std::min(min_x, point.x);
		max_x = std::max(max_x, point.x);
		min_y = std::min(min_y, point.y);
		max_y = std::max(max_y, point.y);
	}

	// Determine floor range based on selection type
	int start_z = floor;
	int end_z = floor;
	switch (g_settings.getInteger(Config::SELECTION_TYPE)) {
		case SELECT_ALL_FLOORS:
			start_z = MAP_MAX_LAYER;
			end_z = floor;
			break;
		case SELECT_VISIBLE_FLOORS:
			start_z = (floor <= GROUND_LAYER) ? GROUND_LAYER : std::min(MAP_MAX_LAYER, floor + 2);
			end_z = floor;
			break;
		case SELECT_CURRENT_FLOOR:
		default:
			start_z = end_z = floor;
			break;
	}

	const bool compensated = g_settings.getInteger(Config::COMPENSATED_SELECT) &&
		floor < GROUND_LAYER;

	editor.selection.start(); // Start a selection session

	for (int z = start_z; z >= end_z; --z) {
		int offset = 0;
		if (compensated) {
			if (z > GROUND_LAYER) {
				offset = floor - GROUND_LAYER;
			} else {
				offset = floor - z;
			}
		}

		const int sx = min_x + offset;
		const int ex = max_x + offset;
		const int sy = min_y + offset;
		const int ey = max_y + offset;

		for (int x = sx; x <= ex; ++x) {
			for (int y = sy; y <= ey; ++y) {
				double px = x + 0.5;
				double py = y + 0.5;
				if (IsPointInPolygon(lasso_map_points, px - offset, py - offset)) {
					Tile* tile = editor.map.getTile(x, y, z);
					if (tile) {
						editor.selection.add(tile);
					}
				}
			}
		}
	}

	editor.selection.finish(); // Finish the selection session
	editor.selection.updateSelectionCount();
}
