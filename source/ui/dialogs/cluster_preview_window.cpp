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
#include "ui/dialogs/cluster_preview_window.h"
#include "ui/gui.h"
#include "rendering/core/graphics.h"
#include "item_definitions/core/item_definition_store.h"
#include <wx/dcbuffer.h>
#include <algorithm>

namespace {
	enum {
		ID_CLUSTER_GRID_PANEL = wxID_HIGHEST + 6000,
		ID_CLUSTER_CENTER_CHECK
	};
}

//=============================================================================
// ClusterPreviewWindow
//=============================================================================

wxBEGIN_EVENT_TABLE(ClusterPreviewWindow, wxDialog)
	EVT_CHECKBOX(ID_CLUSTER_CENTER_CHECK, ClusterPreviewWindow::OnCenterCheckChanged)
	EVT_CLOSE(ClusterPreviewWindow::OnClose)
wxEND_EVENT_TABLE()

ClusterPreviewWindow::ClusterPreviewWindow(wxWindow* parent, AreaDecoration::FloorRule& rule,
                                           std::function<void()> onChangeCallback,
                                           const wxString& title)
	: wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(450, 500),
	           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMINIMIZE_BOX),
	  m_rule(rule),
	  m_onChangeCallback(onChangeCallback),
	  m_gridPanel(nullptr),
	  m_centerCheck(nullptr),
	  m_infoText(nullptr),
	  m_centerText(nullptr),
	  m_hoverCell(0, 0, 0),
	  m_gridMinX(0), m_gridMinY(0),
	  m_gridMaxX(0), m_gridMaxY(0),
	  m_gridWidth(1), m_gridHeight(1)
{
	SetMinSize(wxSize(300, 300));
	CreateControls();
	RecalculateGrid();
	UpdateInfo();

	Centre(wxBOTH);
}

ClusterPreviewWindow::~ClusterPreviewWindow() {
}

void ClusterPreviewWindow::CreateControls() {
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// -- Grid panel (custom painted) --
	m_gridPanel = new wxPanel(this, ID_CLUSTER_GRID_PANEL, wxDefaultPosition, wxSize(400, 350));
	m_gridPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
	m_gridPanel->Bind(wxEVT_PAINT, &ClusterPreviewWindow::OnPaintGrid, this);
	m_gridPanel->Bind(wxEVT_MOTION, &ClusterPreviewWindow::OnGridMouseMove, this);
	m_gridPanel->Bind(wxEVT_LEAVE_WINDOW, &ClusterPreviewWindow::OnGridMouseLeave, this);
	m_gridPanel->Bind(wxEVT_LEFT_DOWN, &ClusterPreviewWindow::OnGridLeftClick, this);
	mainSizer->Add(m_gridPanel, 1, wxEXPAND | wxALL, 4);

	// -- Center point checkbox --
	wxBoxSizer* centerSizer = new wxBoxSizer(wxHORIZONTAL);
	m_centerCheck = new wxCheckBox(this, ID_CLUSTER_CENTER_CHECK, "Define Center Point");
	m_centerCheck->SetValue(m_rule.hasCenterPoint);
	centerSizer->Add(m_centerCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

	m_centerText = new wxStaticText(this, wxID_ANY, "No center");
	centerSizer->Add(m_centerText, 0, wxALIGN_CENTER_VERTICAL);
	mainSizer->Add(centerSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	// -- Info bar --
	m_infoText = new wxStaticText(this, wxID_ANY, "");
	mainSizer->Add(m_infoText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	// -- Close button --
	wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
	btnSizer->AddStretchSpacer();
	wxButton* closeBtn = new wxButton(this, wxID_CLOSE, "Close");
	closeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
	btnSizer->Add(closeBtn, 0, wxALL, 4);
	mainSizer->Add(btnSizer, 0, wxEXPAND);

	SetSizer(mainSizer);
	Layout();
}

void ClusterPreviewWindow::RecalculateGrid() {
	if (m_rule.clusterTiles.empty()) {
		m_gridMinX = 0;
		m_gridMinY = 0;
		m_gridMaxX = 0;
		m_gridMaxY = 0;
		m_gridWidth = 1;
		m_gridHeight = 1;
		return;
	}

	// Use the FloorRule helper to get bounds
	Position boundsMin, boundsMax;
	m_rule.getClusterBounds(boundsMin, boundsMax);

	// Add 1-tile padding around the bounds for context
	m_gridMinX = boundsMin.x - 1;
	m_gridMinY = boundsMin.y - 1;
	m_gridMaxX = boundsMax.x + 1;
	m_gridMaxY = boundsMax.y + 1;

	m_gridWidth = m_gridMaxX - m_gridMinX + 1;
	m_gridHeight = m_gridMaxY - m_gridMinY + 1;
}

void ClusterPreviewWindow::UpdateInfo() {
	// Count total tiles with items and total item count
	size_t tilesWithItems = 0;
	size_t totalItems = 0;
	for (const auto& tile : m_rule.clusterTiles) {
		if (!tile.itemIds.empty()) {
			tilesWithItems++;
			totalItems += tile.itemIds.size();
		}
	}

	wxString info = wxString::Format("Grid: %dx%d | Tiles: %zu | Items: %zu",
	                                 m_gridWidth - 2, m_gridHeight - 2,  // exclude padding
	                                 tilesWithItems, totalItems);

	if (m_rule.hasCenterPoint) {
		info += wxString::Format(" | Center: (%d, %d)", m_rule.centerOffset.x, m_rule.centerOffset.y);
		m_centerText->SetLabel(wxString::Format("Center: (%d, %d)",
		                                        m_rule.centerOffset.x, m_rule.centerOffset.y));
	} else {
		m_centerText->SetLabel("No center");
	}

	m_infoText->SetLabel(info);
}

wxBitmap ClusterPreviewWindow::GetItemBitmap(uint16_t itemId, int size) {
	wxBitmap bmp(size, size, 32);
	wxMemoryDC dc(bmp);

	// Fill with dark background
	dc.SetBackground(wxBrush(wxColour(0x0C, 0x14, 0x2A)));
	dc.Clear();

	// Get the item definition to find the client sprite ID
	const auto itemDef = g_item_definitions.get(itemId);
	Sprite* spr = nullptr;
	if (itemDef) {
		spr = g_gui.gfx.getSprite(itemDef.clientId());
	}

	if (spr) {
		spr->DrawTo(&dc, SPRITE_SIZE_32x32, 0, 0, size, size);
	} else {
		// Draw placeholder
		dc.SetBrush(wxBrush(wxColour(100, 100, 100)));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(2, 2, size - 4, size - 4);
		dc.SetTextForeground(*wxWHITE);
		dc.DrawText("?", size / 2 - 4, size / 2 - 8);
	}

	dc.SelectObject(wxNullBitmap);
	return bmp;
}

void ClusterPreviewWindow::OnPaintGrid(wxPaintEvent& event) {
	wxBufferedPaintDC dc(m_gridPanel);
	wxSize panelSize = m_gridPanel->GetSize();

	// Fill background
	dc.SetBackground(wxBrush(wxColour(30, 30, 40)));
	dc.Clear();

	if (m_rule.clusterTiles.empty()) {
		dc.SetTextForeground(wxColour(180, 180, 180));
		dc.DrawText("No cluster tiles defined", 10, panelSize.GetHeight() / 2 - 8);
		return;
	}

	// Calculate cell size to fit the grid into the panel, capped at m_cellSize
	int cellSz = CalculateCellSize();

	// Centering offset
	int totalW = m_gridWidth * cellSz;
	int totalH = m_gridHeight * cellSz;
	int offsetX = (panelSize.GetWidth() - totalW) / 2;
	int offsetY = (panelSize.GetHeight() - totalH) / 2;

	// Build a lookup map from offset -> CompositeTile for quick access
	std::map<std::pair<int, int>, const AreaDecoration::CompositeTile*> tileMap;
	for (const auto& tile : m_rule.clusterTiles) {
		tileMap[{tile.offset.x, tile.offset.y}] = &tile;
	}

	// Draw each cell
	for (int gy = m_gridMinY; gy <= m_gridMaxY; ++gy) {
		for (int gx = m_gridMinX; gx <= m_gridMaxX; ++gx) {
			int cellPxX = offsetX + (gx - m_gridMinX) * cellSz;
			int cellPxY = offsetY + (gy - m_gridMinY) * cellSz;

			auto it = tileMap.find({gx, gy});
			if (it != tileMap.end() && !it->second->itemIds.empty()) {
				// Cell has items - draw sprites
				DrawCellSprites(dc, cellPxX, cellPxY, *it->second);
			} else {
				// Empty cell - light gray background
				dc.SetBrush(wxBrush(wxColour(45, 45, 55)));
				dc.SetPen(wxPen(wxColour(60, 60, 75), 1));
				dc.DrawRectangle(cellPxX, cellPxY, cellSz, cellSz);
			}

			// Draw grid lines on top
			dc.SetPen(wxPen(wxColour(70, 70, 90), 1));
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRectangle(cellPxX, cellPxY, cellSz, cellSz);

			// Draw center highlight if this is the center cell
			if (m_rule.hasCenterPoint &&
			    gx == m_rule.centerOffset.x && gy == m_rule.centerOffset.y) {
				DrawCenterHighlight(dc, cellPxX, cellPxY);
			}

			// Draw hover highlight
			if (m_hovering && gx == m_hoverCell.x && gy == m_hoverCell.y) {
				DrawHoverHighlight(dc, cellPxX, cellPxY);
			}
		}
	}
}

int ClusterPreviewWindow::CalculateCellSize() const {
	wxSize panelSize = m_gridPanel->GetSize();
	int availW = panelSize.GetWidth() - 8;
	int availH = panelSize.GetHeight() - 8;
	int cellW = (m_gridWidth > 0) ? availW / m_gridWidth : m_cellSize;
	int cellH = (m_gridHeight > 0) ? availH / m_gridHeight : m_cellSize;
	int cellSz = std::min({cellW, cellH, m_cellSize});
	return (cellSz < 16) ? 16 : cellSz;
}

void ClusterPreviewWindow::DrawCellSprites(wxDC& dc, int cellX, int cellY,
                                           const AreaDecoration::CompositeTile& tile) {
	int cellSz = CalculateCellSize();

	// Dark background for item cells
	dc.SetBrush(wxBrush(wxColour(12, 20, 42)));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle(cellX, cellY, cellSz, cellSz);

	// Draw each item sprite stacked in the cell
	for (size_t i = 0; i < tile.itemIds.size(); ++i) {
		uint16_t itemId = tile.itemIds[i];
		if (itemId == 0) continue;

		const auto itemDef = g_item_definitions.get(itemId);
		Sprite* spr = nullptr;
		if (itemDef) {
			spr = g_gui.gfx.getSprite(itemDef.clientId());
		}

		if (spr) {
			spr->DrawTo(&dc, SPRITE_SIZE_32x32, cellX, cellY, cellSz, cellSz);
		} else {
			// Fallback: small colored square
			dc.SetBrush(wxBrush(wxColour(100, 80, 60)));
			dc.SetPen(*wxTRANSPARENT_PEN);
			int pad = cellSz / 6;
			dc.DrawRectangle(cellX + pad, cellY + pad, cellSz - 2 * pad, cellSz - 2 * pad);
		}
	}

	// Show item count if multiple items on this tile
	if (tile.itemIds.size() > 1) {
		dc.SetTextForeground(wxColour(255, 255, 100));
		wxFont smallFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
		dc.SetFont(smallFont);
		wxString countStr = wxString::Format("x%zu", tile.itemIds.size());
		wxSize textSize = dc.GetTextExtent(countStr);
		dc.DrawText(countStr, cellX + cellSz - textSize.GetWidth() - 2,
		            cellY + cellSz - textSize.GetHeight() - 1);
	}
}

void ClusterPreviewWindow::DrawCenterHighlight(wxDC& dc, int cellX, int cellY) {
	int cellSz = CalculateCellSize();

	// Bright yellow/gold border (3px thick)
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.SetPen(wxPen(wxColour(255, 215, 0), 3));
	dc.DrawRectangle(cellX + 1, cellY + 1, cellSz - 2, cellSz - 2);

	// Small crosshair overlay at center of cell
	int cx = cellX + cellSz / 2;
	int cy = cellY + cellSz / 2;
	int armLen = cellSz / 6;
	if (armLen < 3) armLen = 3;

	dc.SetPen(wxPen(wxColour(255, 215, 0), 2));
	dc.DrawLine(cx - armLen, cy, cx + armLen, cy);
	dc.DrawLine(cx, cy - armLen, cx, cy + armLen);
}

void ClusterPreviewWindow::DrawHoverHighlight(wxDC& dc, int cellX, int cellY) {
	int cellSz = CalculateCellSize();

	// Light blue hover overlay (solid color - wxDC does not support alpha on Windows)
	dc.SetBrush(wxBrush(wxColour(70, 100, 160)));
	dc.SetPen(wxPen(wxColour(100, 160, 255), 2));
	dc.DrawRectangle(cellX, cellY, cellSz, cellSz);
}

void ClusterPreviewWindow::OnGridMouseMove(wxMouseEvent& event) {
	wxSize panelSize = m_gridPanel->GetSize();
	int cellSz = CalculateCellSize();

	int totalW = m_gridWidth * cellSz;
	int totalH = m_gridHeight * cellSz;
	int offsetX = (panelSize.GetWidth() - totalW) / 2;
	int offsetY = (panelSize.GetHeight() - totalH) / 2;

	int mouseX = event.GetX();
	int mouseY = event.GetY();

	// Convert mouse position to grid coordinates
	int relX = mouseX - offsetX;
	int relY = mouseY - offsetY;

	if (relX >= 0 && relY >= 0 && relX < totalW && relY < totalH) {
		int gx = m_gridMinX + relX / cellSz;
		int gy = m_gridMinY + relY / cellSz;

		if (gx != m_hoverCell.x || gy != m_hoverCell.y || !m_hovering) {
			m_hoverCell.x = gx;
			m_hoverCell.y = gy;
			m_hovering = true;

			// Build tooltip text
			wxString tipText = wxString::Format("Offset: (%d, %d)", gx, gy);

			// Check if there are items at this position
			for (const auto& tile : m_rule.clusterTiles) {
				if (tile.offset.x == gx && tile.offset.y == gy && !tile.itemIds.empty()) {
					tipText += " | Items: [";
					for (size_t i = 0; i < tile.itemIds.size(); ++i) {
						if (i > 0) tipText += ", ";
						tipText += wxString::Format("%d", tile.itemIds[i]);
					}
					tipText += "]";
					break;
				}
			}

			m_gridPanel->SetToolTip(tipText);
			m_gridPanel->Refresh();
		}
	} else {
		if (m_hovering) {
			m_hovering = false;
			m_gridPanel->UnsetToolTip();
			m_gridPanel->Refresh();
		}
	}
}

void ClusterPreviewWindow::OnGridMouseLeave(wxMouseEvent& event) {
	if (m_hovering) {
		m_hovering = false;
		m_gridPanel->UnsetToolTip();
		m_gridPanel->Refresh();
	}
}

void ClusterPreviewWindow::OnGridLeftClick(wxMouseEvent& event) {
	if (!m_centerCheck->GetValue()) return;

	wxSize panelSize = m_gridPanel->GetSize();
	int cellSz = CalculateCellSize();

	int totalW = m_gridWidth * cellSz;
	int totalH = m_gridHeight * cellSz;
	int offsetX = (panelSize.GetWidth() - totalW) / 2;
	int offsetY = (panelSize.GetHeight() - totalH) / 2;

	int relX = event.GetX() - offsetX;
	int relY = event.GetY() - offsetY;

	if (relX < 0 || relY < 0 || relX >= totalW || relY >= totalH) return;

	int gx = m_gridMinX + relX / cellSz;
	int gy = m_gridMinY + relY / cellSz;

	// Only allow setting center on cells that have items
	bool hasItems = false;
	for (const auto& tile : m_rule.clusterTiles) {
		if (tile.offset.x == gx && tile.offset.y == gy && !tile.itemIds.empty()) {
			hasItems = true;
			break;
		}
	}

	if (!hasItems) return;

	// Set the center point
	m_rule.hasCenterPoint = true;
	m_rule.centerOffset = Position(gx, gy, 0);

	UpdateInfo();
	m_gridPanel->Refresh();

	if (m_onChangeCallback) {
		m_onChangeCallback();
	}
}

void ClusterPreviewWindow::OnCenterCheckChanged(wxCommandEvent& event) {
	bool enabled = m_centerCheck->GetValue();
	m_rule.hasCenterPoint = enabled;

	if (!enabled) {
		m_rule.centerOffset = Position(0, 0, 0);
	}

	UpdateInfo();
	m_gridPanel->Refresh();

	if (m_onChangeCallback) {
		m_onChangeCallback();
	}
}

void ClusterPreviewWindow::OnClose(wxCloseEvent& event) {
	// End the modal loop so the caller can read back m_rule changes before destroying
	if (IsModal()) {
		EndModal(wxID_CLOSE);
	} else {
		Destroy();
	}
}
