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

#ifndef RME_IO_MINIMAP_H_
#define RME_IO_MINIMAP_H_

#include "map/position.h"
#include <string>

class Editor;

enum class MinimapExportFormat {
	Png,
	Bmp
};

enum class MinimapExportMode {
	AllFloors,
	GroundFloor,
	SpecificFloor,
	SelectedArea,
	AreaView
};

class IOMinimap {
public:
	IOMinimap(Editor* editor, MinimapExportFormat format, MinimapExportMode mode, bool updateLoadbar);

	bool saveMinimap(const std::string& directory, const std::string& name, int floor = -1);

	void setArea(const Position& from, const Position& to) {
		m_fromPos = from;
		m_toPos = to;
	}
	void setMergeFloors(bool merge) {
		m_mergeFloors = merge;
	}
	void setUniformBounds(bool uniform) {
		m_uniformBounds = uniform;
	}

	const std::string& getError() const noexcept {
		return m_error;
	}

private:
	bool exportMinimap(const std::string& directory, const std::string& name);
	bool exportSelection(const std::string& directory, const std::string& name);
	bool exportAreaView(const std::string& directory, const std::string& name);

	Editor* m_editor;
	MinimapExportFormat m_format;
	MinimapExportMode m_mode;
	bool m_updateLoadbar = false;
	bool m_mergeFloors = false;
	bool m_uniformBounds = false;
	int m_floor = -1;
	Position m_fromPos;
	Position m_toPos;
	std::string m_error;
};

#endif
