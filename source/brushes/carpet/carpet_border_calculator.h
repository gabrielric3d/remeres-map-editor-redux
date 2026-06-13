//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_CARPET_BORDER_CALCULATOR_H
#define RME_CARPET_BORDER_CALCULATOR_H

#include <cstdint>

class BaseMap;
class Tile;
class CarpetBrush;

class CarpetBorderCalculator {
public:
	// When onlyBrush is non-null, only carpets belonging to that brush are
	// recalculated; carpets from other brushes on the tile are left untouched
	// (used by the "disable carpet interaction" mode).
	static void calculate(BaseMap* map, Tile* tile, CarpetBrush* onlyBrush = nullptr);
};

#endif
