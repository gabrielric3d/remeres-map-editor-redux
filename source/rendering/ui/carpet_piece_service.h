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
// Single border pieces of a carpet brush (every "tiny border" is a carpet brush).
//
// Shared by the canvas piece picker (Alt + right click) and the palette piece
// strip. Both put one piece in hand as a RAW brush WITHOUT touching the palette,
// so the Terrain tab stays on the tileset the carpet came from.
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_UI_CARPET_PIECE_SERVICE_H_
#define RME_RENDERING_UI_CARPET_PIECE_SERVICE_H_

#include "brushes/brush_enums.h"

#include <wx/string.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

class BaseMap;
class Brush;
class CarpetBrush;
class Position;

namespace CarpetPieces {

	struct Piece {
		BorderType align = BORDER_NONE;
		uint16_t itemId = 0;
		int alternate = 0; // index inside the alignment group
		int alternateCount = 1; // items in the alignment group
	};

	// The carpet the brush belongs to: the carpet brush itself, or the carpet that owns
	// the item of a RAW brush (a carpet item has exactly one owner brush). nullptr otherwise.
	CarpetBrush* ResolveSource(Brush* brush);

	// Every piece in CARPET_ALIGN_ORDER (sides, corners, diagonals, center), alternates included.
	std::vector<Piece> ListPieces(const CarpetBrush& carpet);

	// First item of an alignment; nullopt when the carpet has nothing there.
	std::optional<Piece> FirstPiece(const CarpetBrush& carpet, BorderType align);

	// The piece in hand: the current brush is the RAW brush of one of this carpet's items.
	std::optional<Piece> CurrentPiece(const CarpetBrush& carpet);

	// Puts the piece in hand as a RAW brush, palette untouched.
	// False when the item has no RAW brush.
	bool SelectPiece(const CarpetBrush& carpet, const Piece& piece);

	// Back to the automatic carpet brush (goes through the palette, which re-syncs it).
	void SelectSource(CarpetBrush* carpet);

	// "grass (tiny): 4544 - South edge (s)"
	wxString Describe(const CarpetBrush& carpet, const Piece& piece);
	// "grass (tiny) - auto brush"
	wxString DescribeSource(const CarpetBrush& carpet);

	// --- Fit with the pieces already around a tile ------------------------------
	// Judged on the sprites themselves, the way the eye does it: along every edge the
	// tile shares with a piece of the same carpet, the drawing (opaque pixels) has to
	// cross the edge where the neighbour's does. No naming convention involved, so it
	// holds for tiny borders authored as carpets and for real carpets alike.

	// Sides, in this order everywhere below.
	enum EdgeSide {
		EDGE_NORTH = 0,
		EDGE_EAST = 1,
		EDGE_SOUTH = 2,
		EDGE_WEST = 3,
	};

	// What the orthogonal neighbours of a tile say: for each side, the edge profile of
	// the neighbour's pieces of the carpet facing the tile, or nullopt when that
	// neighbour holds no piece of it. A profile is 8 bins along the edge, bit set where
	// the drawing crosses the edge.
	struct NeighbourEdges {
		std::array<std::optional<uint8_t>, 4> facing;

		bool any() const {
			for (const auto& side : facing) {
				if (side) {
					return true;
				}
			}
			return false;
		}
	};

	NeighbourEdges ReadNeighbourEdges(const CarpetBrush& carpet, BaseMap& map, const Position& position);

	// The piece continues every known neighbour edge. Pieces whose sprite cannot be
	// read, and tiles without neighbours of the carpet, always fit.
	bool FitsNeighbours(uint16_t itemId, const NeighbourEdges& neighbours);

} // namespace CarpetPieces

#endif
