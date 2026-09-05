#ifndef RME_EDITOR_OPERATIONS_DRAW_OPERATIONS_H
#define RME_EDITOR_OPERATIONS_DRAW_OPERATIONS_H

#include "app/rme_forward_declarations.h"
#include "map/position.h"
// We don't want partial define of Editor, so forward declare or include?
// Ideally forward declare to avoid circular dependency if Editor includes this.
class Editor;

class DrawOperations {
public:
	static void draw(Editor& editor, Position offset, bool alt, bool dodraw);
	static void draw(Editor& editor, const PositionVector& tilestodraw, bool alt, bool dodraw);
	static void draw(Editor& editor, const PositionVector& tilestodraw, PositionVector& tilestoborder, bool alt, bool dodraw);

	// Erases the ground (and its auto-borders) at every given position, then re-borderizes
	// the erased tiles and their neighbors so the surrounding grounds reform their borders
	// against the now-open hole. Always behaves as if auto-border is enabled, regardless
	// of the global USE_AUTOMAGIC setting. Used by the "hold C to open the floor above"
	// canvas shortcut, which passes the current brush footprint. Positions without ground
	// are skipped; no-op if none of them has ground.
	// With whole_tile, the whole tile is wiped (ground + items, eraser semantics, so
	// ERASER_LEAVE_UNIQUE still protects complex items) instead of only the ground, and
	// tiles that only carry items (mountain walls, decoration) are erased too.
	// Positions may span several floors; borders are recomputed per floor.
	static void eraseGroundWithBorders(Editor& editor, const PositionVector& positions, bool whole_tile = false);

	// True when at least one of the "erase extra floors" toggles is on with a floor count > 0.
	static bool extraFloorEraseEnabled();

	// Projects the given footprint (tiles of the floor being drawn on) onto the floors
	// configured by the ERASE_FLOORS_* settings and erases them in one undo step.
	// No-op when both toggles are off. Never touches the footprint's own floor — the
	// brush itself already handles that.
	static void eraseExtraFloors(Editor& editor, const PositionVector& footprint);
};

#endif
