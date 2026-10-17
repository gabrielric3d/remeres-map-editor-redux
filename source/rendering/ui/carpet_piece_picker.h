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
// Border piece picker drawn over the map canvas (NanoVG). Opened with Alt + right
// click (Preferences > Hotkeys > Border Piece Picker Modifier) while a carpet brush,
// or one of its pieces, is in hand.
//
// Every piece sits where it belongs around the clicked spot (north above, corners on
// the corners, inner diagonals outside the 3x3) and picking one puts it in hand as a
// RAW brush (the canvas also places it on the clicked tile); the center cell goes back
// to the automatic carpet brush.
//
// Press, drag onto a piece and release to pick. A quick tap leaves the picker open
// and the next left click picks. ESC, a right click or a click off the cells closes it.
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_UI_CARPET_PIECE_PICKER_H_
#define RME_RENDERING_UI_CARPET_PIECE_PICKER_H_

#include "rendering/ui/carpet_piece_service.h"

#include <wx/string.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

struct NVGcontext;
class CarpetBrush;

class CarpetPiecePicker {
public:
	// tile_pixels: size of one map tile on screen; the cells follow it (clamped).
	// neighbours: the carpet's pieces around the clicked tile. Pieces that would not
	// continue them are dimmed (still pickable); with alternates, a cell shows the
	// first one that fits.
	bool Open(CarpetBrush* carpet, const CarpetPieces::NeighbourEdges& neighbours, int mouse_x, int mouse_y, int canvas_width, int canvas_height, float tile_pixels);
	void Close();
	bool IsOpen() const {
		return m_open;
	}

	// The button that opened the picker is still down.
	bool IsHeld() const {
		return m_held;
	}
	void EndHold() {
		m_held = false;
	}
	// The mouse went more than a few pixels away from where the picker opened.
	bool HasMovedSinceOpen() const {
		return m_moved;
	}

	void UpdateMouse(int mouse_x, int mouse_y);
	bool HasHoveredCell() const {
		return m_hovered >= 0;
	}

	enum class PickResult {
		None, // nothing picked (no cell under the cursor: still open)
		Source, // center cell: the automatic carpet brush is in hand
		Piece, // a single piece is in hand (RAW brush)
	};
	// Picks the cell under the cursor and closes (unless nothing was hovered).
	PickResult Confirm();

	// Status bar / label text for the hovered cell.
	wxString HoveredDescription() const;

	void Draw(NVGcontext* vg, int canvas_width, int canvas_height);

private:
	struct Cell {
		int col = 0;
		int row = 0;
		bool source = false; // center cell: the automatic carpet brush
		bool empty = true; // the carpet has no item for this alignment
		bool fits = true; // continues the pieces already around the clicked tile
		CarpetPieces::Piece piece;
	};

	// Closes the picker when the brush in hand no longer belongs to its carpet (another
	// brush was picked elsewhere, or the brushes were reloaded and the pointer is gone).
	bool CheckStillValid();

	void Layout(int mouse_x, int mouse_y, int canvas_width, int canvas_height);
	float BlockSize() const;
	float CellX(const Cell& cell) const;
	float CellY(const Cell& cell) const;
	int HitTest(int x, int y) const;

	int GetItemImage(NVGcontext* vg, uint16_t itemId);
	int GetLookImage(NVGcontext* vg, uint16_t clientId);
	void ReleaseImages(NVGcontext* vg);

	bool m_open = false;
	bool m_held = false;
	bool m_moved = false;
	int m_open_x = 0;
	int m_open_y = 0;
	int m_hovered = -1;

	CarpetBrush* m_carpet = nullptr;
	std::vector<Cell> m_cells;
	// What was in hand when the picker opened, to outline it.
	BorderType m_current_align = BORDER_NONE;
	bool m_source_in_hand = false;

	float m_cell = 40.0f;
	float m_gap = 3.0f;
	float m_origin_x = 0.0f;
	float m_origin_y = 0.0f;

	// Sprite images live in the canvas's NanoVG context. They are rebuilt on every open
	// (brushes or the client may have been reloaded meanwhile), lazily in Draw(), where
	// the GL context is current.
	NVGcontext* m_images_context = nullptr;
	bool m_images_stale = false;
	std::unordered_map<uint64_t, int> m_images;
};

#endif
