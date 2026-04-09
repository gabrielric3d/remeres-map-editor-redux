//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"

#include "brushes/brush_utility.h"
#include "ui/gui.h"
#include "editor/editor.h"
#include "brushes/brush.h"
#include "brushes/ground/ground_brush.h"
#include "map/map.h"
#include "map/tile.h"

#include <stack>

std::vector<bool> BrushUtility::processed;
int BrushUtility::countMaxFills = 0;
int BrushUtility::fill_width = 100;
int BrushUtility::fill_height = 100;

void BrushUtility::GetTilesToDraw(int mouse_map_x, int mouse_map_y, int floor, std::vector<Position>* tilestodraw, std::vector<Position>* tilestoborder, bool fill, const FillArea& fill_area) {
	if (fill) {
		Brush* brush = g_gui.GetCurrentBrush();
		if (!brush || !brush->is<GroundBrush>()) {
			return;
		}

		GroundBrush* newBrush = brush->as<GroundBrush>();
		Position position(mouse_map_x, mouse_map_y, floor);

		Tile* tile = g_gui.GetCurrentMap().getTile(position);
		GroundBrush* oldBrush = nullptr;
		if (tile) {
			oldBrush = tile->getGroundBrush();
		}

		if (oldBrush && oldBrush->getID() == newBrush->getID()) {
			return;
		}

		if ((tile && tile->ground && !oldBrush) || (!tile && oldBrush)) {
			return;
		}

		if (tile && oldBrush) {
			GroundBrush* groundBrush = tile->getGroundBrush();
			if (!groundBrush || groundBrush->getID() != oldBrush->getID()) {
				return;
			}
		}

		fill_width = fill_area.width;
		fill_height = fill_area.height;
		processed.assign(fill_width * fill_height, false);
		countMaxFills = 0;
		FloodFill(&g_gui.GetCurrentMap(), position, fill_width / 2, fill_height / 2, fill_width, fill_height, oldBrush, tilestodraw);

	} else {
		const BrushFootprint footprint = g_gui.GetBrushFootprint();
		for (int y = footprint.min_offset_y - 1; y <= footprint.max_offset_y + 1; ++y) {
			for (int x = footprint.min_offset_x - 1; x <= footprint.max_offset_x + 1; ++x) {
				if (footprint.containsOffset(x, y)) {
					if (tilestodraw) {
						tilestodraw->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
					}
				}

				for (int check_y = y - 1; check_y <= y + 1; ++check_y) {
					for (int check_x = x - 1; check_x <= x + 1; ++check_x) {
						if (!footprint.containsOffset(check_x, check_y)) {
							continue;
						}
						if (tilestoborder) {
							tilestoborder->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
						}
						check_y = y + 2;
						break;
					}
				}
			}
		}
	}
}

bool BrushUtility::FloodFill(Map* map, const Position& center, int start_x, int start_y, int fw, int fh, GroundBrush* brush, std::vector<Position>* positions) {
	struct Cell { int x, y; };
	std::stack<Cell> stack;
	stack.push({start_x, start_y});

	int maxFills = fw * fh;

	while (!stack.empty()) {
		auto [x, y] = stack.top();
		stack.pop();

		if (x <= 0 || y <= 0 || x >= fw || y >= fh) {
			continue;
		}

		if (processed[GetFillIndex(x, y)]) {
			continue;
		}

		processed[GetFillIndex(x, y)] = true;

		int px = (center.x + x) - (fw / 2);
		int py = (center.y + y) - (fh / 2);
		if (px <= 0 || py <= 0 || px >= map->getWidth() || py >= map->getHeight()) {
			continue;
		}

		Tile* tile = map->getTile(px, py, center.z);
		if ((tile && tile->ground && !brush) || (!tile && brush)) {
			continue;
		}

		if (tile && brush) {
			GroundBrush* groundBrush = tile->getGroundBrush();
			if (!groundBrush || groundBrush->getID() != brush->getID()) {
				continue;
			}
		}

		positions->push_back(Position(px, py, center.z));

		countMaxFills++;
		if (countMaxFills > maxFills) {
			return true;
		}

		stack.push({x - 1, y});
		stack.push({x, y - 1});
		stack.push({x + 1, y});
		stack.push({x, y + 1});
	}

	return false;
}
