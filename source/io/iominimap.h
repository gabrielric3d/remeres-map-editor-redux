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
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Editor;
class Map;
class wxFileName;

enum class MinimapExportFormat {
	Otmm,
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

enum {
	MMBLOCK_SIZE = 64,
	OTMM_SIGNATURE = 0x4D4D544F, // "OTMM"
	OTMM_VERSION = 1
};

enum MinimapTileFlags {
	MinimapTileWasSeen = 1,
	MinimapTileNotPathable = 2,
	MinimapTileNotWalkable = 4
};

#pragma pack(push, 1) // disable memory alignment
struct MinimapTile {
	uint8_t flags = 0;
	uint8_t color = 0xFF; // INVALID_MINIMAP_COLOR
	uint8_t speed = 10;
};

class MinimapBlock {
public:
	void updateTile(int x, int y, const MinimapTile& tile) {
		m_tiles[getTileIndex(x, y)] = tile;
	}
	MinimapTile& getTile(int x, int y) {
		return m_tiles[getTileIndex(x, y)];
	}
	inline uint32_t getTileIndex(int x, int y) const noexcept {
		return ((y % MMBLOCK_SIZE) * MMBLOCK_SIZE) + (x % MMBLOCK_SIZE);
	}
	const std::array<MinimapTile, MMBLOCK_SIZE * MMBLOCK_SIZE>& getTiles() const noexcept {
		return m_tiles;
	}

private:
	std::array<MinimapTile, MMBLOCK_SIZE * MMBLOCK_SIZE> m_tiles;
};
#pragma pack(pop)

class IOMinimap {
public:
	IOMinimap(Editor* editor, MinimapExportFormat format, MinimapExportMode mode, bool updateLoadbar);

	bool saveMinimap(const std::string& directory, const std::string& name, int floor = -1);

	// Loads each .otbm file in turn, accumulating every tile into a single set
	// of minimap blocks (keyed by absolute X/Y/Z), then writes one merged
	// .otmm file. Tiles sharing a position are overwritten by whichever map
	// comes later in the list. The editor's currently-open map is untouched.
	bool mergeMaps(const std::vector<std::string>& otbm_files, const std::string& directory, const std::string& name);

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

	bool saveOtmm(const wxFileName& file);
	// Serializes the already-populated m_blocks into an .otmm file. Clears
	// m_blocks as it writes. Used by both saveOtmm() and mergeMaps().
	bool writeOtmmFile(const wxFileName& file);
	void readBlocks();
	// Accumulates the visible tiles of a single map into m_blocks. Called once
	// per map; never clears m_blocks, so repeated calls merge maps together.
	// When selectionOnly is true only selected tiles are read (SelectedArea).
	void readBlocksFromMap(Map& map, bool selectionOnly, int progressBase, int progressSpan);
	uint32_t getBlockIndex(const Position& pos) const {
		return ((pos.y / MMBLOCK_SIZE) * (65536 / MMBLOCK_SIZE)) + (pos.x / MMBLOCK_SIZE);
	}

	std::array<std::map<uint32_t, MinimapBlock>, MAP_LAYERS> m_blocks;

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
