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
// Carpet brush sub-editor, hosted by the Doodads tab of the Brushes Editor.
//
// A carpet brush (<brush type="carpet">) is a set of 13 alignment slots — the 12
// border alignments (n, e, s, w, cnw..csw, dnw..dsw) plus "center" — each holding
// one or more weighted items. This panel edits those slots; listing, loading and
// saving the brush stays with DoodadEditorDialog, which owns the file access.
//////////////////////////////////////////////////////////////////////

#ifndef RME_UI_DIALOGS_CARPET_EDITOR_PANEL_H_
#define RME_UI_DIALOGS_CARPET_EDITOR_PANEL_H_

#include <wx/panel.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/button.h>

#include <cstdint>
#include <string>
#include <vector>

#include "brushes/brush_enums.h" // BorderType

namespace pugi {
	class xml_node;
}

class CarpetAlignGridPanel;
class CarpetAlignItemsPanel;
class CarpetPreviewPanel;

// One weighted item of an alignment slot (<item id chance/> inside <carpet align>).
struct CarpetItemEntry {
	uint16_t itemId = 0;
	int chance = 10;

	CarpetItemEntry() = default;
	CarpetItemEntry(uint16_t id, int c) : itemId(id), chance(c) { }
};

// Slots are indexed by BorderType: 1 (NORTH_HORIZONTAL) .. 13 (CARPET_CENTER).
// Index 0 (BORDER_NONE) is never used.
constexpr int CARPET_ALIGN_COUNT = CARPET_CENTER + 1;

// The order carpet slots are written to XML (matches the shipped doodads.xml).
extern const BorderType CARPET_ALIGN_ORDER[CARPET_ALIGN_COUNT - 1];

const char* carpetAlignToString(BorderType align);           // "n", "cnw", "dse", "center"
BorderType carpetAlignFromString(const std::string& text);   // BORDER_NONE if unknown
const char* carpetAlignLabel(BorderType align);              // "North edge", "Center", ...

class CarpetEditorPanel : public wxPanel {
public:
	explicit CarpetEditorPanel(wxWindow* parent);

	void Clear();

	// Reads the <carpet> children of a <brush type="carpet"> node. Brush-level
	// attributes are the caller's business.
	void LoadFromBrushNode(pugi::xml_node brushNode);

	// Appends one <carpet> child per non-empty slot to `brushNode`, in canonical
	// order: the short form (align + id) for single items, the <item> list otherwise.
	void WriteCarpetNodes(pugi::xml_node brushNode) const;

	bool HasAnyItems() const;
	int AlignmentsWithItems() const;

	// Slot that holds `itemId`, or BORDER_NONE.
	BorderType FindAlignmentOfItem(uint16_t itemId) const;

	BorderType CurrentAlignment() const;
	void SelectAlignment(BorderType align);

	const std::vector<CarpetItemEntry>& ItemsFor(BorderType align) const;

	// Adds to the current slot with the chance from the spin (drop targets, Add button).
	void AddItemById(uint16_t itemId);
	void AddItemToAlignment(BorderType align, uint16_t itemId, int chance);
	// Both act on the current slot; called by the items panel.
	void RemoveItemAt(int index);
	void SelectItemAt(int index);

	void OnAddItem(wxCommandEvent& event);
	void OnBrowseItem(wxCommandEvent& event);
	void OnUpdateChance(wxCommandEvent& event);
	void OnClearAlignment(wxCommandEvent& event);
	// Opens the Carpet Scan dialog and appends its assignments to the slots.
	void OnScan(wxCommandEvent& event);

private:
	void CreateGUIControls();
	void RefreshGridPreviews();
	void RefreshItemsPanel();
	void RefreshPreview();
	void RefreshAll();

	std::vector<CarpetItemEntry> m_items[CARPET_ALIGN_COUNT];

	CarpetAlignGridPanel* m_grid = nullptr;
	wxStaticText* m_itemsCaption = nullptr;
	CarpetAlignItemsPanel* m_itemsPanel = nullptr;
	wxSpinCtrl* m_itemIdCtrl = nullptr;
	wxSpinCtrl* m_itemChanceCtrl = nullptr;
	wxStaticText* m_summaryLabel = nullptr;
	wxButton* m_scanButton = nullptr;
	CarpetPreviewPanel* m_preview = nullptr;

	DECLARE_EVENT_TABLE()
};

// The 13 alignment slots laid out as the carpet they form: a 3x3 block (corners,
// edges, center) plus a 2x2 block for the inner-corner "diagonal" pieces.
class CarpetAlignGridPanel : public wxPanel {
public:
	CarpetAlignGridPanel(wxWindow* parent, wxWindowID id = wxID_ANY);

	void SetSelected(BorderType align);
	BorderType GetSelected() const {
		return m_selected;
	}

	// Cell contents: the sprite of the first item and how many items the slot holds.
	void SetCellPreview(BorderType align, uint16_t itemId, int itemCount);
	void ClearPreviews();

	// Slot under the given client coordinates, or BORDER_NONE when outside.
	BorderType HitTest(int x, int y) const;

	void OnPaint(wxPaintEvent& event);
	void OnMouseClick(wxMouseEvent& event);
	void OnMotion(wxMouseEvent& event);
	void OnLeave(wxMouseEvent& event);

	static constexpr int CELL_SIZE = 52;
	static constexpr int CELL_MARGIN = 3;
	static constexpr int SECTION_GAP = 18;
	static constexpr int LABEL_HEIGHT = 18;

private:
	struct CellInfo {
		uint16_t itemId = 0;
		int itemCount = 0;
	};

	wxRect CellRectFor(BorderType align) const;

	BorderType m_selected = CARPET_CENTER;
	BorderType m_hovered = BORDER_NONE;
	CellInfo m_cells[CARPET_ALIGN_COUNT];

	DECLARE_EVENT_TABLE()
};

// Wrapping grid of sprite cells (chance label + X button) for the current slot.
// Same visual language as DoodadSingleItemsPanel / WallSegmentItemsPanel.
class CarpetAlignItemsPanel : public wxPanel {
public:
	CarpetAlignItemsPanel(wxWindow* parent, wxWindowID id = wxID_ANY);

	void SetItems(const std::vector<CarpetItemEntry>& items);
	void Clear();
	int GetSelectedIndex() const {
		return m_selectedIndex;
	}

	void OnPaint(wxPaintEvent& event);
	void OnMouseClick(wxMouseEvent& event);
	void OnSize(wxSizeEvent& event);

private:
	struct CellRect {
		wxRect bounds;
		wxRect closeBtn;
		int index;
	};

	std::vector<CarpetItemEntry> m_items;
	std::vector<CellRect> m_cells;
	int m_selectedIndex = -1;

	static constexpr int CELL_SIZE = 56;
	static constexpr int CELL_MARGIN = 4;
	static constexpr int CLOSE_BTN_SIZE = 14;

	void RecalcLayout();

	DECLARE_EVENT_TABLE()
};

// Paints a fixed carpet shape using the slot items, resolving each tile's
// alignment through the same table the map uses (CarpetBrush::alignmentForNeighbours),
// so what the preview shows is what painting the brush produces.
class CarpetPreviewPanel : public wxPanel {
public:
	CarpetPreviewPanel(wxWindow* parent, wxWindowID id = wxID_ANY);

	// First item of every slot (0 = slot empty), indexed by BorderType.
	void SetSlotItems(const uint16_t (&firstItems)[CARPET_ALIGN_COUNT]);
	void Clear();

	void OnPaint(wxPaintEvent& event);

	static constexpr int SHAPE_W = 7;
	static constexpr int SHAPE_H = 6;
	static constexpr int TILE_SIZE = 32;

private:
	uint16_t m_firstItems[CARPET_ALIGN_COUNT] = { 0 };

	DECLARE_EVENT_TABLE()
};

#endif // RME_UI_DIALOGS_CARPET_EDITOR_PANEL_H_
