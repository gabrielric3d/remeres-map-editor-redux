#ifndef RME_SELECTION_OPERATIONS_H
#define RME_SELECTION_OPERATIONS_H

#include "map/position.h"
#include <list>

class Editor;
class Tile;
class Item;
class Brush;
class DoodadBrush;

class SelectionOperations {
public:
	static void moveSelection(Editor& editor, Position offset);
	static void rotateSelection(Editor& editor, int quarterTurns);
	static void destroySelection(Editor& editor);
	static void borderizeSelection(Editor& editor);
	static void randomizeSelection(Editor& editor);
	// "Paint bucket" for the selection: draws the current brush on every selected tile
	// (each on its own floor) as if the user had painted all of them in one stroke,
	// then keeps the area selected so it can be filled again with another brush.
	// Ground/wall/carpet/table brushes re-border the perimeter like a normal stroke;
	// doodad brushes stamp a fresh random composite per tile (same as smearing).
	// Returns false (with a status-bar hint) when there is nothing to fill or the
	// brush cannot fill an area (house exit, waypoint, camera path).
	static bool fillSelection(Editor& editor);

	// What a magic wand click on a tile would select, decided from the tile's
	// content, topmost first: a doodad item -> every item of that doodad brush on the
	// floor; a wall item -> the connected run of that wall brush; otherwise the ground
	// brush -> the contiguous ground patch plus its borders. `anchor` is the clicked
	// item of that brush (ground item for Ground), used to tell "re-select" from "drag".
	struct MagicWandTarget {
		enum class Kind {
			None,
			Ground,
			Wall,
			Doodad,
		};
		Kind kind = Kind::None;
		Brush* brush = nullptr;
		Item* anchor = nullptr;
	};
	static MagicWandTarget magicWandTarget(Tile* tile);

	// Magic wand selection from the tile at `origin` (see magicWandTarget):
	//  - Ground: the 4-connected patch of tiles with the same ground brush (same floor)
	//    plus that brush's border pieces, on the patch and on the ring around it.
	//  - Wall: the 4-connected run of tiles carrying that wall brush; wall segments,
	//    doors and windows of the brush are selected, nothing else on those tiles.
	//  - Doodad: every item of that doodad brush on the whole floor (scattered trees
	//    are one click), capped at MAGIC_WAND_MAX_TILES tiles.
	// Only items of the brush are selected, never the rest of the tile. With
	// add_to_selection the result joins the current selection; otherwise it replaces
	// it. Returns the number of tiles touched, 0 when there is nothing to select
	// (caller falls back to the normal click).
	static size_t magicWandSelect(Editor& editor, const Position& origin, bool add_to_selection);

	// Upper bound on the wand's flood fill so a click on an ocean stays responsive.
	static constexpr size_t MAGIC_WAND_MAX_TILES = 250000;

	// Helper functions used by Editor::drawInternal
	static void removeDuplicateWalls(Tile* buffer_tile, Tile* new_tile);
	static void doSurroundingBorders(DoodadBrush* doodad_brush, std::list<Position>& tilestoborder, Tile* buffer_tile, Tile* new_tile);
};

#endif
