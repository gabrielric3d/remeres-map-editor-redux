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

	// wall_thickness == 0  -> solid line (entire footprint filled per Bresenham point)
	// wall_thickness >= 1  -> hollow line (only the two outer bands of the footprint perpendicular
	//                         to the dominant axis are drawn, with the given band thickness).
	//                         Falls back to solid when the footprint is 1x1 (no middle to empty).
	static void GetLineTiles(const Position& a, const Position& b,
		std::vector<Position>* tilestodraw,
		std::vector<Position>* tilestoborder,
		int wall_thickness = 0);

	// Returns the end-point b snapped to the nearest multiple of snap_degrees from a
	// (e.g. 45 => 8 directions, 90 => 4, 30 => 12). The snapped point is the
	// projection of (b - a) onto the chosen axis, so the line length tracks the
	// mouse instead of jumping.
	static Position SnapToAngle(const Position& a, const Position& b, int snap_degrees = 45);

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
