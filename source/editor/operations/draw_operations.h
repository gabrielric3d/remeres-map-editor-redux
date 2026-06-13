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
	static void eraseGroundWithBorders(Editor& editor, const PositionVector& positions);
};

#endif
