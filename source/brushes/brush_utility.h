//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_BRUSH_UTILITY_H_
#define RME_BRUSH_UTILITY_H_

#include "map/position.h"
#include <vector>

class Map;
class GroundBrush;

struct FillArea {
	int width = 100;
	int height = 100;
};

class BrushUtility {
public:
	static void GetTilesToDraw(int mouse_map_x, int mouse_map_y, int floor, std::vector<Position>* tilestodraw, std::vector<Position>* tilestoborder, bool fill = false, const FillArea& fill_area = {});

private:
	static bool FloodFill(Map* map, const Position& center, int x, int y, int fill_width, int fill_height, GroundBrush* brush, std::vector<Position>* positions);

	static int fill_width;
	static int fill_height;

	static inline int GetFillIndex(int x, int y) {
		return x + fill_width * y;
	}

	static std::vector<bool> processed;
	static int countMaxFills;
};

#endif
