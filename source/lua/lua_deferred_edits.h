//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_LUA_DEFERRED_EDITS_H
#define RME_LUA_DEFERRED_EDITS_H

#include "map/position.h"

#include <memory>
#include <vector>

class Editor;
class Tile;

namespace LuaAPI {
	// One tile as a script left it; `tile` is null when the script removed it.
	struct DeferredTileEdit {
		Position pos;
		std::unique_ptr<Tile> tile;
	};

	// While deferring, app.transaction leaves its edits on the map without
	// adding them to the undo history, and remembers every tile as it was
	// before the first transaction touched it. finishDeferredEdits() puts the
	// map back as it was and hands over the final tiles in the order the script
	// first touched them, so the caller can commit them bit by bit (the Claude
	// assistant replays them on screen). GUI thread, around one synchronous run.
	void beginDeferredEdits(Editor* editor);
	std::vector<DeferredTileEdit> finishDeferredEdits();
}

#endif
