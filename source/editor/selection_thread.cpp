//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "app/main.h"

#include "editor/selection_thread.h"
#include "editor/editor.h"
#include "map/map.h"
#include "map/tile.h"
#include "game/creature.h"
#include "game/spawn.h"
#include "app/settings.h"
#include "editor/action.h"

SelectionThread::SelectionThread(Editor& editor, Position start, Position end, bool creatures_only) :
	editor(editor),
	start(start),
	end(end),
	selection(editor),
	result(nullptr),
	creatures_only(creatures_only) {
	////
}

SelectionThread::~SelectionThread() {
	////
}

void SelectionThread::Start() {
	thread = std::thread(&SelectionThread::Work, this);
}

void SelectionThread::Wait() {
	if (thread.joinable()) {
		thread.join();
	}
}

void SelectionThread::Work() {
	selection.start(Selection::SUBTHREAD);
	for (int z = start.z; z >= end.z; --z) {
		for (int x = start.x; x <= end.x; ++x) {
			for (int y = start.y; y <= end.y; ++y) {
				Tile* tile = editor.map.getTile(x, y, z);
				if (!tile) {
					continue;
				}

				if (creatures_only) {
					if (tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS) && (!tile->creature || !g_settings.getInteger(Config::SHOW_CREATURES))) {
						selection.add(tile, tile->spawn.get());
					}
					if (tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
						selection.add(tile, tile->creature.get());
					}
				} else {
					selection.add(tile);
				}
			}
		}
		if (z <= GROUND_LAYER && g_settings.getInteger(Config::COMPENSATED_SELECT)) {
			++start.x;
			++start.y;
			++end.x;
			++end.y;
		}
	}
	// Access wrapper to get subsession
	// Since SelectionThread is friend of Selection, we can access private members of selection instance
	result = std::move(selection.subsession);
	selection.finish(Selection::SUBTHREAD);
}
