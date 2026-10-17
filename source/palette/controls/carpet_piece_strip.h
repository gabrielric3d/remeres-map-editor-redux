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
// Narrow pane docked against the map side of the palette: the carpet brush in
// hand on top ("AUTO") and every one of its pieces below (all alternates).
// Clicking a piece puts it in hand as a RAW brush without leaving the tileset;
// the outline follows the piece in hand, also when it was picked on the map with
// Alt + right click. Empty while the brush in hand is not a carpet.
// View > Border Piece Strip shows/hides it.
//////////////////////////////////////////////////////////////////////

#ifndef RME_PALETTE_CONTROLS_CARPET_PIECE_STRIP_H_
#define RME_PALETTE_CONTROLS_CARPET_PIECE_STRIP_H_

#include "util/virtual_item_grid.h"
#include "rendering/ui/carpet_piece_service.h"

#include <vector>

class CarpetBrush;

class CarpetPieceStrip : public VirtualItemGrid {
public:
	explicit CarpetPieceStrip(wxWindow* parent);
	~CarpetPieceStrip() override;

	size_t GetItemCount() const override;
	uint16_t GetItem(size_t index) const override;
	wxString GetItemName(size_t index) const override; // tooltip

	// Follows the brush in hand: rebuilds the list when the carpet changed, otherwise
	// only moves the outline.
	void SyncWithCurrentBrush();
	// Brushes or icons were reloaded: drop everything and re-sync once the reload is over.
	void Reload();

	// The single strip lives in its own AUI pane. DockPane() creates it the first time
	// and (re)docks it on the map side of the palette: same side and layer, the next
	// row inwards. Called by the layout manager whenever the palettes are (re)built.
	static void DockPane();
	static void SetPaneVisible(bool visible);
	static void SyncPane();
	static void ReloadPane();

protected:
	void OnNanoVGPaint(NVGcontext* vg, int width, int height) override;
	wxSize DoGetBestClientSize() const override;
	void OnMouseDown(wxMouseEvent& event) override;

private:
	int PaneWidth() const;
	void ApplySlotSize();
	// Safety net: the carpet pointer is only used while it is still the source of the
	// brush in hand (after a client/brush reload the old pointer is gone, and a palette
	// refresh can swap the brush without the brush-change hook). A stale strip drops
	// the pointer and re-syncs on the next idle.
	bool IsStale() const;
	bool DropIfStale();
	// 0 = the carpet itself, 1.. = sides, corners, diagonals, center (separator between them).
	int GroupOf(int index) const;

	CarpetBrush* m_carpet = nullptr;
	std::vector<CarpetPieces::Piece> m_pieces; // grid index i >= 1 is m_pieces[i - 1]

	static CarpetPieceStrip* s_pane;
};

#endif
