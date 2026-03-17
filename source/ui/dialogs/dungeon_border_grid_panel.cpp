//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/dialogs/dungeon_border_grid_panel.h"
#include "ui/gui.h"
#include "ui/theme.h"
#include "ui/find_item_window.h"
#include "rendering/core/graphics.h"
#include "item_definitions/core/item_definition_store.h"

#include <wx/dcbuffer.h>

std::string slotPositionToLabel(DungeonSlotPosition pos) {
	switch (pos) {
		case DSLOT_N: return "n";
		case DSLOT_S: return "s";
		case DSLOT_E: return "e";
		case DSLOT_W: return "w";
		case DSLOT_NW: return "nw";
		case DSLOT_NE: return "ne";
		case DSLOT_SW: return "sw";
		case DSLOT_SE: return "se";
		case DSLOT_INNER_NW: return "inw";
		case DSLOT_INNER_NE: return "ine";
		case DSLOT_INNER_SW: return "isw";
		case DSLOT_INNER_SE: return "ise";
		case DSLOT_PILLAR: return "pillar";
		default: return "";
	}
}

//=============================================================================
// DungeonSlotGridDropTarget
//=============================================================================

DungeonSlotGridDropTarget::DungeonSlotGridDropTarget(DungeonSlotGridPanel* grid)
	: m_grid(grid) {
}

bool DungeonSlotGridDropTarget::OnDropText(wxCoord x, wxCoord y, const wxString& data) {
	if (!m_grid) return false;

	wxString itemData = data;
	if (itemData.StartsWith("RME_ITEM:")) {
		itemData = itemData.Mid(9);
	} else if (itemData.StartsWith("ITEM_ID:")) {
		itemData = itemData.Mid(8);
	} else {
		return false;
	}

	unsigned long idVal = 0;
	if (!itemData.ToULong(&idVal) || idVal == 0 || idVal > 0xFFFF) {
		return false;
	}

	DungeonSlotPosition pos = m_grid->GetPositionFromCoordinates(x, y);
	if (pos == DSLOT_NONE) return false;

	m_grid->SetItemId(pos, static_cast<uint16_t>(idVal));
	return true;
}

//=============================================================================
// DungeonSlotGridPanel
//=============================================================================

wxBEGIN_EVENT_TABLE(DungeonSlotGridPanel, wxPanel)
	EVT_PAINT(DungeonSlotGridPanel::OnPaint)
	EVT_LEFT_UP(DungeonSlotGridPanel::OnLeftClick)
	EVT_RIGHT_UP(DungeonSlotGridPanel::OnRightClick)
wxEND_EVENT_TABLE()

DungeonSlotGridPanel::DungeonSlotGridPanel(wxWindow* parent, Mode mode, const wxString& title)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE)
	, m_mode(mode)
	, m_title(title)
	, m_selectedPosition(DSLOT_NONE) {

	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetDropTarget(new DungeonSlotGridDropTarget(this));

	// Height based on mode
	if (mode == MODE_WALLS) {
		SetMinSize(wxSize(500, 210)); // 3 sections: Straight 2x2, Corners 2x2, Pillar 1x1
	} else {
		SetMinSize(wxSize(500, 210)); // 3 sections: Straight 2x2, Corners 2x2, Inner 2x2
	}
}

void DungeonSlotGridPanel::SetItemId(DungeonSlotPosition pos, uint16_t id) {
	m_items[pos] = id;
	Refresh();
	if (m_onChange) m_onChange();
}

uint16_t DungeonSlotGridPanel::GetItemId(DungeonSlotPosition pos) const {
	auto it = m_items.find(pos);
	return it != m_items.end() ? it->second : 0;
}

DungeonSlotPosition DungeonSlotGridPanel::GetPositionFromCoordinates(int x, int y) const {
	for (const auto& sr : m_slotRects) {
		if (sr.rect.Contains(x, y)) {
			return sr.pos;
		}
	}
	return DSLOT_NONE;
}

void DungeonSlotGridPanel::OnLeftClick(wxMouseEvent& event) {
	DungeonSlotPosition pos = GetPositionFromCoordinates(event.GetX(), event.GetY());
	if (pos == DSLOT_NONE) return;

	m_selectedPosition = pos;

	FindItemDialog dlg(this, "Pick Item");
	if (dlg.ShowModal() == wxID_OK) {
		uint16_t id = dlg.getResultID();
		if (id > 0) {
			SetItemId(pos, id);
		}
	}

	m_selectedPosition = DSLOT_NONE;
	Refresh();
}

void DungeonSlotGridPanel::OnRightClick(wxMouseEvent& event) {
	DungeonSlotPosition pos = GetPositionFromCoordinates(event.GetX(), event.GetY());
	if (pos == DSLOT_NONE) return;

	SetItemId(pos, 0);
}

void DungeonSlotGridPanel::DrawSlot(wxDC& dc, DungeonSlotPosition pos, int x, int y,
                                     int cellSize, int padding) {
	int innerX = x + padding;
	int innerY = y + padding;
	int innerW = cellSize - 2 * padding;
	int innerH = cellSize - 2 * padding;

	// Selection highlight
	if (pos == m_selectedPosition) {
		dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
		dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Selected)));
		dc.DrawRectangle(x, y, cellSize, cellSize);
		dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
	}

	// Draw sprite or empty slot
	uint16_t itemId = GetItemId(pos);
	if (itemId > 0) {
		const auto itemDef = g_item_definitions.get(itemId);
		if (itemDef) {
			Sprite* spr = g_gui.gfx.getSprite(itemDef.clientId());
			if (spr) {
				spr->DrawTo(&dc, SPRITE_SIZE_32x32, innerX, innerY, innerW, innerH);
			}
		}
	} else {
		// Empty: draw subtle background
		dc.SetBrush(wxBrush(wxColour(30, 30, 40)));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(innerX, innerY, innerW, innerH);
	}

	// Label below sprite
	dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
	wxString label = slotPositionToLabel(pos);
	wxSize textSize = dc.GetTextExtent(label);
	dc.DrawText(label, innerX + (innerW - textSize.GetWidth()) / 2,
	            innerY + innerH - textSize.GetHeight());
}

void DungeonSlotGridPanel::OnPaint(wxPaintEvent& event) {
	wxAutoBufferedPaintDC dc(this);

	wxRect rect = GetClientRect();
	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	// Draw highlight border if this panel is the selected target
	if (m_highlighted) {
		dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRectangle(rect.Deflate(1, 1));
	}

	m_slotRects.clear();

	const int cellSize = 56;
	const int padding = 4;
	const int sectionGap = 20;
	const int labelHeight = 18;
	const int topMargin = 8;

	dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);

	// Calculate section positions
	int numSections = (m_mode == MODE_WALLS) ? 3 : 3;
	int gridWidth = 2 * cellSize;

	// Section titles
	wxString titles[3];
	if (m_mode == MODE_WALLS) {
		titles[0] = "Straight";
		titles[1] = "Corners";
		titles[2] = "Pillar";
	} else {
		titles[0] = "Straight";
		titles[1] = "Corners";
		titles[2] = "Inner Corners";
	}

	// Center sections horizontally
	int totalWidth = numSections * gridWidth + (numSections - 1) * sectionGap;
	if (m_mode == MODE_WALLS) {
		// Pillar section is smaller (1x1)
		totalWidth = 2 * gridWidth + cellSize + 2 * sectionGap;
	}
	int startX = (rect.GetWidth() - totalWidth) / 2;
	if (startX < 10) startX = 10;

	int gridTop = topMargin + labelHeight + 4;

	// Draw title if provided
	if (!m_title.empty()) {
		dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
		dc.SetTextForeground(Theme::Get(Theme::Role::Text));
		dc.DrawText(m_title, 8, 2);
	}

	dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
	dc.SetTextForeground(Theme::Get(Theme::Role::Text));

	auto drawSectionLabel = [&](const wxString& label, int x, int width) {
		wxSize ts = dc.GetTextExtent(label);
		dc.DrawText(label, x + (width - ts.GetWidth()) / 2, topMargin);
	};

	auto drawGrid = [&](int offsetX, int offsetY, int cols, int rows) {
		for (int i = 0; i <= cols; ++i) {
			dc.DrawLine(offsetX + i * cellSize, offsetY,
			            offsetX + i * cellSize, offsetY + rows * cellSize);
		}
		for (int i = 0; i <= rows; ++i) {
			dc.DrawLine(offsetX, offsetY + i * cellSize,
			            offsetX + cols * cellSize, offsetY + i * cellSize);
		}
	};

	auto addSlot = [&](DungeonSlotPosition pos, int x, int y) {
		m_slotRects.push_back({pos, wxRect(x, y, cellSize, cellSize)});
		DrawSlot(dc, pos, x, y, cellSize, padding);
	};

	int sx = startX;

	// Section 1: Straight (N/S/E/W) - 2x2
	drawSectionLabel(titles[0], sx, gridWidth);
	drawGrid(sx, gridTop, 2, 2);
	addSlot(DSLOT_N, sx, gridTop);
	addSlot(DSLOT_E, sx + cellSize, gridTop);
	addSlot(DSLOT_S, sx, gridTop + cellSize);
	addSlot(DSLOT_W, sx + cellSize, gridTop + cellSize);

	sx += gridWidth + sectionGap;

	// Section 2: Corners (NW/NE/SW/SE) - 2x2
	drawSectionLabel(titles[1], sx, gridWidth);
	drawGrid(sx, gridTop, 2, 2);
	addSlot(DSLOT_NW, sx, gridTop);
	addSlot(DSLOT_NE, sx + cellSize, gridTop);
	addSlot(DSLOT_SW, sx, gridTop + cellSize);
	addSlot(DSLOT_SE, sx + cellSize, gridTop + cellSize);

	sx += gridWidth + sectionGap;

	if (m_mode == MODE_WALLS) {
		// Section 3: Pillar - 1x1
		drawSectionLabel(titles[2], sx, cellSize);
		drawGrid(sx, gridTop, 1, 1);
		addSlot(DSLOT_PILLAR, sx, gridTop);
	} else {
		// Section 3: Inner Corners (iNW/iNE/iSW/iSE) - 2x2
		drawSectionLabel(titles[2], sx, gridWidth);
		drawGrid(sx, gridTop, 2, 2);
		addSlot(DSLOT_INNER_NW, sx, gridTop);
		addSlot(DSLOT_INNER_NE, sx + cellSize, gridTop);
		addSlot(DSLOT_INNER_SW, sx, gridTop + cellSize);
		addSlot(DSLOT_INNER_SE, sx + cellSize, gridTop + cellSize);
	}

	// Instruction text at bottom
	int instrY = gridTop + 2 * cellSize + 6;
	dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	dc.SetTextForeground(Theme::Get(Theme::Role::Accent));
	dc.DrawText("Click to pick  |  Right-click to clear  |  Drag from palette", 8, instrY);
}
