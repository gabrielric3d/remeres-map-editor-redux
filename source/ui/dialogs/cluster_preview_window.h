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

#ifndef RME_CLUSTER_PREVIEW_WINDOW_H
#define RME_CLUSTER_PREVIEW_WINDOW_H

#include <wx/wx.h>
#include <functional>
#include "editor/area_decoration.h"

class ClusterPreviewWindow : public wxDialog {
public:
	ClusterPreviewWindow(wxWindow* parent, AreaDecoration::FloorRule& rule,
	                     std::function<void()> onChangeCallback = nullptr,
	                     const wxString& title = "Cluster Preview");
	virtual ~ClusterPreviewWindow();

private:
	AreaDecoration::FloorRule& m_rule;
	std::function<void()> m_onChangeCallback;

	wxPanel* m_gridPanel;        // Custom-painted grid canvas
	wxCheckBox* m_centerCheck;   // Enable/disable center point selection
	wxStaticText* m_infoText;    // "Grid: WxH | Items: N"
	wxStaticText* m_centerText;  // "Center: (x, y)" or "No center"

	int m_cellSize = 48;         // Pixels per grid cell (matches sprite size)
	Position m_hoverCell;
	bool m_hovering = false;

	// Grid bounds (calculated from cluster tiles)
	int m_gridMinX, m_gridMinY, m_gridMaxX, m_gridMaxY;
	int m_gridWidth, m_gridHeight;

	void CreateControls();
	void UpdateInfo();
	void RecalculateGrid();

	// Event handlers
	void OnPaintGrid(wxPaintEvent& event);
	void OnGridMouseMove(wxMouseEvent& event);
	void OnGridMouseLeave(wxMouseEvent& event);
	void OnGridLeftClick(wxMouseEvent& event);
	void OnCenterCheckChanged(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);

	// Cell size calculation helper
	int CalculateCellSize() const;

	// Rendering helpers
	void DrawCellSprites(wxDC& dc, int cellX, int cellY,
	                     const AreaDecoration::CompositeTile& tile);
	void DrawCenterHighlight(wxDC& dc, int cellX, int cellY);
	void DrawHoverHighlight(wxDC& dc, int cellX, int cellY);

	// Sprite helper
	wxBitmap GetItemBitmap(uint16_t itemId, int size = 32);

	wxDECLARE_EVENT_TABLE();
};

#endif // RME_CLUSTER_PREVIEW_WINDOW_H
