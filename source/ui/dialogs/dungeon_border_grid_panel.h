//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_DUNGEON_BORDER_GRID_PANEL_H
#define RME_DUNGEON_BORDER_GRID_PANEL_H

#include <wx/wx.h>
#include <wx/dnd.h>
#include <map>
#include <string>
#include <functional>
#include <cstdint>

class DungeonSlotGridPanel;

//=============================================================================
// Slot positions for a border/wall set
//=============================================================================
enum DungeonSlotPosition {
	DSLOT_NONE = -1,
	// Straight
	DSLOT_N = 0,
	DSLOT_S,
	DSLOT_E,
	DSLOT_W,
	// Corners
	DSLOT_NW,
	DSLOT_NE,
	DSLOT_SW,
	DSLOT_SE,
	// Inner corners (borders only)
	DSLOT_INNER_NW,
	DSLOT_INNER_NE,
	DSLOT_INNER_SW,
	DSLOT_INNER_SE,
	// Pillar (walls only)
	DSLOT_PILLAR,
	DSLOT_COUNT
};

std::string slotPositionToLabel(DungeonSlotPosition pos);

//=============================================================================
// Drop target for the grid
//=============================================================================
class DungeonSlotGridDropTarget : public wxTextDropTarget {
public:
	explicit DungeonSlotGridDropTarget(DungeonSlotGridPanel* grid);
	bool OnDropText(wxCoord x, wxCoord y, const wxString& data) override;
private:
	DungeonSlotGridPanel* m_grid;
};

//=============================================================================
// DungeonSlotGridPanel - Visual grid panel with sprite slots
//   Supports: click to pick, right-click to clear, drag from palette
//=============================================================================
class DungeonSlotGridPanel : public wxPanel {
	friend class DungeonSlotGridDropTarget;
public:
	enum Mode {
		MODE_WALLS,   // N/S/E/W + NW/NE/SW/SE + Pillar (9 slots)
		MODE_BORDERS  // N/S/E/W + NW/NE/SW/SE + iNW/iNE/iSW/iSE (12 slots)
	};

	DungeonSlotGridPanel(wxWindow* parent, Mode mode, const wxString& title = "");

	void SetItemId(DungeonSlotPosition pos, uint16_t id);
	uint16_t GetItemId(DungeonSlotPosition pos) const;

	// Highlight border (for selection as target)
	void SetHighlighted(bool highlighted) { m_highlighted = highlighted; Refresh(); }
	bool IsHighlighted() const { return m_highlighted; }

	// Callback when any slot changes
	void SetOnChangeCallback(std::function<void()> cb) { m_onChange = std::move(cb); }

	DungeonSlotPosition GetPositionFromCoordinates(int x, int y) const;

private:
	void OnPaint(wxPaintEvent& event);
	void OnLeftClick(wxMouseEvent& event);
	void OnRightClick(wxMouseEvent& event);

	void DrawSlot(wxDC& dc, DungeonSlotPosition pos, int x, int y, int cellSize, int padding);

	Mode m_mode;
	wxString m_title;
	std::map<DungeonSlotPosition, uint16_t> m_items;
	DungeonSlotPosition m_selectedPosition;
	bool m_highlighted = false;
	std::function<void()> m_onChange;

	// Layout cache (computed in OnPaint)
	struct SlotRect {
		DungeonSlotPosition pos;
		wxRect rect;
	};
	std::vector<SlotRect> m_slotRects;

	wxDECLARE_EVENT_TABLE();
};

#endif // RME_DUNGEON_BORDER_GRID_PANEL_H
