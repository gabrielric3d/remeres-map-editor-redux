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
#include "ui/dialogs/carpet_editor_panel.h"
#include "ui/dialogs/carpet_scan_dialog.h"
#include "ui/find_item_window.h"
#include "ui/gui.h"
#include "ui/theme.h"
#include "rendering/core/graphics.h"
#include "rendering/core/game_sprite.h"
#include "item_definitions/core/item_definition_store.h"
#include "brushes/carpet/carpet_brush.h"
#include "brushes/ground/auto_border.h"
#include "ext/pugixml.hpp"

#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/dcbuffer.h>
#include <wx/dnd.h>

#include <algorithm>

// ============================================================================
// Alignment names
// ============================================================================

const BorderType CARPET_ALIGN_ORDER[CARPET_ALIGN_COUNT - 1] = {
	NORTH_HORIZONTAL, EAST_HORIZONTAL, SOUTH_HORIZONTAL, WEST_HORIZONTAL,
	NORTHWEST_CORNER, NORTHEAST_CORNER, SOUTHEAST_CORNER, SOUTHWEST_CORNER,
	NORTHWEST_DIAGONAL, NORTHEAST_DIAGONAL, SOUTHEAST_DIAGONAL, SOUTHWEST_DIAGONAL,
	CARPET_CENTER
};

const char* carpetAlignToString(BorderType align) {
	switch (align) {
		case NORTH_HORIZONTAL: return "n";
		case EAST_HORIZONTAL: return "e";
		case SOUTH_HORIZONTAL: return "s";
		case WEST_HORIZONTAL: return "w";
		case NORTHWEST_CORNER: return "cnw";
		case NORTHEAST_CORNER: return "cne";
		case SOUTHEAST_CORNER: return "cse";
		case SOUTHWEST_CORNER: return "csw";
		case NORTHWEST_DIAGONAL: return "dnw";
		case NORTHEAST_DIAGONAL: return "dne";
		case SOUTHEAST_DIAGONAL: return "dse";
		case SOUTHWEST_DIAGONAL: return "dsw";
		case CARPET_CENTER: return "center";
		default: return "";
	}
}

BorderType carpetAlignFromString(const std::string& text) {
	// Same parsing as CarpetBrushLoader: the 12 border edge names plus "center".
	if (text == "center") {
		return CARPET_CENTER;
	}
	const int id = AutoBorder::edgeNameToID(text);
	if (id <= BORDER_NONE || id >= CARPET_CENTER) {
		return BORDER_NONE;
	}
	return static_cast<BorderType>(id);
}

const char* carpetAlignLabel(BorderType align) {
	switch (align) {
		case NORTH_HORIZONTAL: return "North edge";
		case EAST_HORIZONTAL: return "East edge";
		case SOUTH_HORIZONTAL: return "South edge";
		case WEST_HORIZONTAL: return "West edge";
		case NORTHWEST_CORNER: return "Northwest corner";
		case NORTHEAST_CORNER: return "Northeast corner";
		case SOUTHEAST_CORNER: return "Southeast corner";
		case SOUTHWEST_CORNER: return "Southwest corner";
		case NORTHWEST_DIAGONAL: return "Northwest diagonal (inner corner)";
		case NORTHEAST_DIAGONAL: return "Northeast diagonal (inner corner)";
		case SOUTHEAST_DIAGONAL: return "Southeast diagonal (inner corner)";
		case SOUTHWEST_DIAGONAL: return "Southwest diagonal (inner corner)";
		case CARPET_CENTER: return "Center";
		default: return "";
	}
}

namespace {

	enum {
		ID_CARPET_ADD_ITEM = wxID_HIGHEST + 900,
		ID_CARPET_BROWSE_ITEM,
		ID_CARPET_UPDATE_CHANCE,
		ID_CARPET_CLEAR_ALIGN,
		ID_CARPET_SCAN
	};

	bool IsValidAlign(BorderType align) {
		return align > BORDER_NONE && align < CARPET_ALIGN_COUNT;
	}

	// Palette drags carry "ITEM_ID:<n>" or "RME_ITEM:<n>"; anything else is ignored.
	uint16_t ParseDraggedItemId(const wxString& data) {
		wxString payload;
		if (data.StartsWith("ITEM_ID:")) {
			payload = data.Mid(8);
		} else if (data.StartsWith("RME_ITEM:")) {
			payload = data.Mid(9);
		} else {
			return 0;
		}
		unsigned long value = 0;
		if (!payload.ToULong(&value) || value == 0 || value > 0xFFFF) {
			return 0;
		}
		return static_cast<uint16_t>(value);
	}

	CarpetEditorPanel* FindCarpetEditor(wxWindow* child) {
		wxWindow* parent = child ? child->GetParent() : nullptr;
		while (parent && !dynamic_cast<CarpetEditorPanel*>(parent)) {
			parent = parent->GetParent();
		}
		return dynamic_cast<CarpetEditorPanel*>(parent);
	}

	// Dropping on the items grid adds to the slot being edited.
	class CarpetItemsDropTarget : public wxTextDropTarget {
	public:
		explicit CarpetItemsDropTarget(CarpetAlignItemsPanel* panel) : m_panel(panel) { }

		bool OnDropText(wxCoord /*x*/, wxCoord /*y*/, const wxString& data) override {
			const uint16_t itemId = ParseDraggedItemId(data);
			if (itemId == 0) return false;
			CarpetEditorPanel* editor = FindCarpetEditor(m_panel);
			if (!editor) return false;
			editor->AddItemById(itemId);
			return true;
		}

	private:
		CarpetAlignItemsPanel* m_panel;
	};

	// Dropping straight on an alignment cell adds to that slot and selects it, so a
	// whole carpet can be assembled by dragging 13 items from the palette.
	class CarpetGridDropTarget : public wxTextDropTarget {
	public:
		explicit CarpetGridDropTarget(CarpetAlignGridPanel* grid) : m_grid(grid) { }

		bool OnDropText(wxCoord x, wxCoord y, const wxString& data) override {
			const uint16_t itemId = ParseDraggedItemId(data);
			if (itemId == 0) return false;
			const BorderType align = m_grid->HitTest(x, y);
			if (!IsValidAlign(align)) return false;
			CarpetEditorPanel* editor = FindCarpetEditor(m_grid);
			if (!editor) return false;
			editor->SelectAlignment(align);
			editor->AddItemById(itemId);
			return true;
		}

	private:
		CarpetAlignGridPanel* m_grid;
	};

	void DrawItemSprite(wxDC& dc, uint16_t itemId, int x, int y, int size) {
		const auto itemDef = g_item_definitions.get(itemId);
		if (!itemDef) return;
		Sprite* sprite = g_gui.gfx.getSprite(itemDef.clientId());
		if (sprite) {
			sprite->DrawTo(&dc, SPRITE_SIZE_32x32, x, y, size, size);
		}
	}

} // namespace

// ============================================================================
// Event tables
// ============================================================================

BEGIN_EVENT_TABLE(CarpetEditorPanel, wxPanel)
	EVT_BUTTON(ID_CARPET_ADD_ITEM, CarpetEditorPanel::OnAddItem)
	EVT_BUTTON(ID_CARPET_BROWSE_ITEM, CarpetEditorPanel::OnBrowseItem)
	EVT_BUTTON(ID_CARPET_UPDATE_CHANCE, CarpetEditorPanel::OnUpdateChance)
	EVT_BUTTON(ID_CARPET_CLEAR_ALIGN, CarpetEditorPanel::OnClearAlignment)
	EVT_BUTTON(ID_CARPET_SCAN, CarpetEditorPanel::OnScan)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(CarpetAlignGridPanel, wxPanel)
	EVT_PAINT(CarpetAlignGridPanel::OnPaint)
	EVT_LEFT_UP(CarpetAlignGridPanel::OnMouseClick)
	EVT_MOTION(CarpetAlignGridPanel::OnMotion)
	EVT_LEAVE_WINDOW(CarpetAlignGridPanel::OnLeave)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(CarpetAlignItemsPanel, wxPanel)
	EVT_PAINT(CarpetAlignItemsPanel::OnPaint)
	EVT_LEFT_UP(CarpetAlignItemsPanel::OnMouseClick)
	EVT_SIZE(CarpetAlignItemsPanel::OnSize)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(CarpetPreviewPanel, wxPanel)
	EVT_PAINT(CarpetPreviewPanel::OnPaint)
END_EVENT_TABLE()

// ============================================================================
// CarpetEditorPanel
// ============================================================================

CarpetEditorPanel::CarpetEditorPanel(wxWindow* parent) :
	wxPanel(parent, wxID_ANY) {
	CreateGUIControls();
	RefreshAll();
}

void CarpetEditorPanel::CreateGUIControls() {
	wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

	// --- Left: alignment grid ---
	wxStaticBoxSizer* gridSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Alignments");

	m_grid = new CarpetAlignGridPanel(this);
	m_grid->SetDropTarget(new CarpetGridDropTarget(m_grid));
	m_grid->SetToolTip("Click a slot to edit it. Drop an item from the palette on a slot to add it there.");
	gridSizer->Add(m_grid, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);

	wxStaticText* hint = new wxStaticText(this, wxID_ANY,
		"The 3x3 block is the carpet itself (corners, edges, center).\n"
		"Diagonals are the inner-corner pieces used where two\n"
		"carpet areas meet in an L. Every slot may hold several\n"
		"items; one is picked by chance when painting.");
	hint->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	gridSizer->Add(hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

	m_summaryLabel = new wxStaticText(this, wxID_ANY, "");
	m_summaryLabel->SetForegroundColour(wxColour(100, 100, 200));
	gridSizer->Add(m_summaryLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

	// Scan: classify a set of candidate items (an id range, or the map selection) into
	// the 13 slots by comparing each piece against the rest of its family.
	m_scanButton = new wxButton(this, ID_CARPET_SCAN, "Scan...");
	m_scanButton->SetToolTip("Detect the alignment of a batch of carpet pieces (e.g. an ID range) and fill the slots.\n"
							 "Scan one carpet family at a time: every piece is compared against the others in the batch.");
	if (g_gui.gfx.isUnloaded()) {
		m_scanButton->Enable(false);
		m_scanButton->SetToolTip("Requires a loaded client (sprites).");
	}
	gridSizer->Add(m_scanButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	mainSizer->Add(gridSizer, 0, wxEXPAND | wxALL, 5);

	// --- Center: items of the selected slot ---
	wxStaticBoxSizer* itemsSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Slot items");

	m_itemsCaption = new wxStaticText(this, wxID_ANY, "Items for Center:");
	wxFont captionFont = m_itemsCaption->GetFont();
	captionFont.SetWeight(wxFONTWEIGHT_BOLD);
	m_itemsCaption->SetFont(captionFont);
	itemsSizer->Add(m_itemsCaption, 0, wxLEFT | wxRIGHT | wxTOP, 5);

	m_itemsPanel = new CarpetAlignItemsPanel(this);
	m_itemsPanel->SetDropTarget(new CarpetItemsDropTarget(m_itemsPanel));
	m_itemsPanel->SetToolTip("Drag items from the palette here, or use Add. Click an item to edit its chance; X removes it.");
	itemsSizer->Add(m_itemsPanel, 1, wxEXPAND | wxALL, 5);

	wxBoxSizer* controls = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* idSizer = new wxBoxSizer(wxVERTICAL);
	idSizer->Add(new wxStaticText(this, wxID_ANY, "Item ID:"), 0);
	m_itemIdCtrl = new wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, wxSize(100, -1), wxSP_ARROW_KEYS, 0, 65535);
	idSizer->Add(m_itemIdCtrl, 0, wxEXPAND | wxTOP, 2);
	controls->Add(idSizer, 0, wxRIGHT, 10);

	wxBoxSizer* chanceSizer = new wxBoxSizer(wxVERTICAL);
	chanceSizer->Add(new wxStaticText(this, wxID_ANY, "Chance:"), 0);
	m_itemChanceCtrl = new wxSpinCtrl(this, wxID_ANY, "10", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1, 10000);
	chanceSizer->Add(m_itemChanceCtrl, 0, wxEXPAND | wxTOP, 2);
	controls->Add(chanceSizer, 0, wxRIGHT, 10);

	wxBoxSizer* buttons = new wxBoxSizer(wxVERTICAL);
	buttons->AddStretchSpacer();
	wxBoxSizer* buttonRow = new wxBoxSizer(wxHORIZONTAL);
	buttonRow->Add(new wxButton(this, ID_CARPET_BROWSE_ITEM, "Browse...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 5);
	buttonRow->Add(new wxButton(this, ID_CARPET_ADD_ITEM, "Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 5);
	wxButton* updateBtn = new wxButton(this, ID_CARPET_UPDATE_CHANCE, "Update Chance", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	updateBtn->SetToolTip("Applies the Chance value to the selected item of this slot.");
	buttonRow->Add(updateBtn, 0, wxRIGHT, 5);
	wxButton* clearBtn = new wxButton(this, ID_CARPET_CLEAR_ALIGN, "Clear Slot", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	clearBtn->SetToolTip("Removes every item of this slot.");
	buttonRow->Add(clearBtn, 0);
	buttons->Add(buttonRow, 0);
	controls->Add(buttons, 0, wxEXPAND);

	itemsSizer->Add(controls, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	mainSizer->Add(itemsSizer, 1, wxEXPAND | wxALL, 5);

	// --- Right: preview ---
	wxStaticBoxSizer* previewSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Preview");
	m_preview = new CarpetPreviewPanel(this);
	m_preview->SetToolTip("A sample carpet painted with the current slots, resolved with the same rules the map uses. Empty slots show their code.");
	previewSizer->Add(m_preview, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);
	mainSizer->Add(previewSizer, 0, wxEXPAND | wxALL, 5);

	SetSizer(mainSizer);
}

void CarpetEditorPanel::Clear() {
	for (auto& slot : m_items) {
		slot.clear();
	}
	if (m_itemIdCtrl) m_itemIdCtrl->SetValue(0);
	if (m_itemChanceCtrl) m_itemChanceCtrl->SetValue(10);
	if (m_grid) m_grid->SetSelected(CARPET_CENTER);
	RefreshAll();
}

void CarpetEditorPanel::LoadFromBrushNode(pugi::xml_node brushNode) {
	for (auto& slot : m_items) {
		slot.clear();
	}

	for (pugi::xml_node carpetNode = brushNode.child("carpet"); carpetNode; carpetNode = carpetNode.next_sibling("carpet")) {
		const BorderType align = carpetAlignFromString(carpetNode.attribute("align").as_string());
		if (!IsValidAlign(align)) {
			continue;
		}

		// Same precedence as the loader: <item> children win over the id attribute.
		bool hasItemChildren = false;
		for (pugi::xml_node itemNode = carpetNode.child("item"); itemNode; itemNode = itemNode.next_sibling("item")) {
			hasItemChildren = true;
			const uint16_t id = static_cast<uint16_t>(itemNode.attribute("id").as_uint());
			if (id == 0) continue;
			const int chance = itemNode.attribute("chance") ? itemNode.attribute("chance").as_int() : 10;
			m_items[align].push_back(CarpetItemEntry(id, std::max(1, chance)));
		}

		if (!hasItemChildren) {
			const uint16_t id = static_cast<uint16_t>(carpetNode.attribute("id").as_uint());
			if (id != 0) {
				m_items[align].push_back(CarpetItemEntry(id, 1));
			}
		}
	}

	if (m_grid) m_grid->SetSelected(CARPET_CENTER);
	RefreshAll();
}

void CarpetEditorPanel::WriteCarpetNodes(pugi::xml_node brushNode) const {
	for (BorderType align : CARPET_ALIGN_ORDER) {
		const auto& slot = m_items[align];
		if (slot.empty()) continue;

		pugi::xml_node carpetNode = brushNode.append_child("carpet");
		carpetNode.append_attribute("align").set_value(carpetAlignToString(align));

		if (slot.size() == 1) {
			// The short form the loader reads as chance 1 — same as the shipped files.
			carpetNode.append_attribute("id").set_value(slot[0].itemId);
			continue;
		}

		for (const CarpetItemEntry& entry : slot) {
			pugi::xml_node itemNode = carpetNode.append_child("item");
			itemNode.append_attribute("id").set_value(entry.itemId);
			itemNode.append_attribute("chance").set_value(entry.chance);
		}
	}
}

bool CarpetEditorPanel::HasAnyItems() const {
	return AlignmentsWithItems() > 0;
}

int CarpetEditorPanel::AlignmentsWithItems() const {
	int count = 0;
	for (BorderType align : CARPET_ALIGN_ORDER) {
		if (!m_items[align].empty()) ++count;
	}
	return count;
}

BorderType CarpetEditorPanel::FindAlignmentOfItem(uint16_t itemId) const {
	if (itemId == 0) return BORDER_NONE;
	for (BorderType align : CARPET_ALIGN_ORDER) {
		for (const CarpetItemEntry& entry : m_items[align]) {
			if (entry.itemId == itemId) return align;
		}
	}
	return BORDER_NONE;
}

BorderType CarpetEditorPanel::CurrentAlignment() const {
	return m_grid ? m_grid->GetSelected() : CARPET_CENTER;
}

void CarpetEditorPanel::SelectAlignment(BorderType align) {
	if (!IsValidAlign(align)) return;
	if (m_grid) m_grid->SetSelected(align);
	RefreshItemsPanel();
}

const std::vector<CarpetItemEntry>& CarpetEditorPanel::ItemsFor(BorderType align) const {
	static const std::vector<CarpetItemEntry> empty;
	return IsValidAlign(align) ? m_items[align] : empty;
}

void CarpetEditorPanel::AddItemById(uint16_t itemId) {
	const int chance = m_itemChanceCtrl ? m_itemChanceCtrl->GetValue() : 10;
	AddItemToAlignment(CurrentAlignment(), itemId, chance);
}

void CarpetEditorPanel::AddItemToAlignment(BorderType align, uint16_t itemId, int chance) {
	if (!IsValidAlign(align) || itemId == 0) return;

	// One entry per item and slot: re-dropping the same sprite is a no-op rather than
	// a duplicate that would silently double its chance.
	auto& slot = m_items[align];
	const bool present = std::any_of(slot.begin(), slot.end(),
		[itemId](const CarpetItemEntry& entry) { return entry.itemId == itemId; });
	if (present) {
		return;
	}

	slot.push_back(CarpetItemEntry(itemId, std::max(1, chance)));
	if (m_itemIdCtrl) m_itemIdCtrl->SetValue(itemId);
	RefreshAll();
}

void CarpetEditorPanel::RemoveItemAt(int index) {
	auto& slot = m_items[CurrentAlignment()];
	if (index < 0 || index >= static_cast<int>(slot.size())) return;
	slot.erase(slot.begin() + index);
	RefreshAll();
}

void CarpetEditorPanel::SelectItemAt(int index) {
	const auto& slot = m_items[CurrentAlignment()];
	if (index < 0 || index >= static_cast<int>(slot.size())) return;
	if (m_itemIdCtrl) m_itemIdCtrl->SetValue(slot[index].itemId);
	if (m_itemChanceCtrl) m_itemChanceCtrl->SetValue(slot[index].chance);
}

void CarpetEditorPanel::OnAddItem(wxCommandEvent& WXUNUSED(event)) {
	const uint16_t itemId = static_cast<uint16_t>(m_itemIdCtrl->GetValue());
	if (itemId == 0) {
		wxMessageBox("Enter an item ID or use Browse.", "Carpet", wxOK | wxICON_ERROR, this);
		return;
	}
	AddItemById(itemId);
}

void CarpetEditorPanel::OnBrowseItem(wxCommandEvent& WXUNUSED(event)) {
	FindItemDialog dialog(this, "Select Item");
	if (dialog.ShowModal() == wxID_OK) {
		const uint16_t itemId = dialog.getResultID();
		if (itemId > 0) {
			m_itemIdCtrl->SetValue(itemId);
		}
	}
}

void CarpetEditorPanel::OnUpdateChance(wxCommandEvent& WXUNUSED(event)) {
	auto& slot = m_items[CurrentAlignment()];
	const int index = m_itemsPanel ? m_itemsPanel->GetSelectedIndex() : -1;
	if (index < 0 || index >= static_cast<int>(slot.size())) {
		wxMessageBox("Select an item of this slot first.", "Carpet", wxOK | wxICON_INFORMATION, this);
		return;
	}
	slot[index].chance = std::max(1, m_itemChanceCtrl->GetValue());
	RefreshItemsPanel();
}

void CarpetEditorPanel::OnClearAlignment(wxCommandEvent& WXUNUSED(event)) {
	m_items[CurrentAlignment()].clear();
	RefreshAll();
}

void CarpetEditorPanel::OnScan(wxCommandEvent& WXUNUSED(event)) {
	// Re-check: sprites may have been unloaded after the button was created.
	if (g_gui.gfx.isUnloaded()) {
		wxMessageBox("Requires a loaded client (sprites).", "Carpet Scan", wxICON_WARNING, this);
		return;
	}

	CarpetScanDialog dlg(this);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	// APPEND semantics: a slot may hold several variants, so scanned items are added
	// to what is already there. Items the slot already lists are skipped so re-running
	// the scan never duplicates them.
	int added = 0;
	int skipped = 0;
	for (const auto& [align, itemIds] : dlg.GetAlignAssignments()) {
		if (!IsValidAlign(align)) continue;
		std::vector<CarpetItemEntry>& slot = m_items[align];
		for (uint16_t itemId : itemIds) {
			const bool present = std::any_of(slot.begin(), slot.end(),
				[itemId](const CarpetItemEntry& entry) { return entry.itemId == itemId; });
			if (present) {
				++skipped;
				continue;
			}
			slot.push_back(CarpetItemEntry(itemId, 10));
			++added;
		}
	}

	RefreshAll();

	wxString message = wxString::Format("Added %d item(s) from the scan.", added);
	if (skipped > 0) {
		message += wxString::Format("\n%d item(s) were already in their slot and were skipped.", skipped);
	}
	if (added > 0) {
		message += "\n\nCheck the preview, then Save to File to write the brush.";
	}
	wxMessageBox(message, "Carpet Scan", wxICON_INFORMATION, this);
}

void CarpetEditorPanel::RefreshGridPreviews() {
	if (!m_grid) return;
	m_grid->ClearPreviews();
	for (BorderType align : CARPET_ALIGN_ORDER) {
		const auto& slot = m_items[align];
		if (!slot.empty()) {
			m_grid->SetCellPreview(align, slot[0].itemId, static_cast<int>(slot.size()));
		}
	}
	m_grid->Refresh();

	if (m_summaryLabel) {
		const int filled = AlignmentsWithItems();
		wxString text = wxString::Format("%d of %d slots filled", filled, CARPET_ALIGN_COUNT - 1);
		if (filled > 0 && m_items[CARPET_CENTER].empty()) {
			text << " - no center item";
		}
		m_summaryLabel->SetLabel(text);
	}
}

void CarpetEditorPanel::RefreshItemsPanel() {
	const BorderType align = CurrentAlignment();
	if (m_itemsCaption) {
		m_itemsCaption->SetLabel(wxString::Format("Items for %s (%s):", carpetAlignLabel(align), carpetAlignToString(align)));
	}
	if (m_itemsPanel) {
		m_itemsPanel->SetItems(m_items[align]);
	}
}

void CarpetEditorPanel::RefreshPreview() {
	if (!m_preview) return;
	uint16_t firstItems[CARPET_ALIGN_COUNT] = { 0 };
	for (BorderType align : CARPET_ALIGN_ORDER) {
		if (!m_items[align].empty()) {
			firstItems[align] = m_items[align][0].itemId;
		}
	}
	m_preview->SetSlotItems(firstItems);
}

void CarpetEditorPanel::RefreshAll() {
	RefreshGridPreviews();
	RefreshItemsPanel();
	RefreshPreview();
}

// ============================================================================
// CarpetAlignGridPanel
// ============================================================================

namespace {
	// Where each slot sits: section 0 is the 3x3 carpet block, section 1 the 2x2
	// diagonal block to its right.
	struct CellPlacement {
		int section;
		int col;
		int row;
	};

	CellPlacement PlacementFor(BorderType align) {
		switch (align) {
			case NORTHWEST_CORNER: return { 0, 0, 0 };
			case NORTH_HORIZONTAL: return { 0, 1, 0 };
			case NORTHEAST_CORNER: return { 0, 2, 0 };
			case WEST_HORIZONTAL: return { 0, 0, 1 };
			case CARPET_CENTER: return { 0, 1, 1 };
			case EAST_HORIZONTAL: return { 0, 2, 1 };
			case SOUTHWEST_CORNER: return { 0, 0, 2 };
			case SOUTH_HORIZONTAL: return { 0, 1, 2 };
			case SOUTHEAST_CORNER: return { 0, 2, 2 };
			case NORTHWEST_DIAGONAL: return { 1, 0, 0 };
			case NORTHEAST_DIAGONAL: return { 1, 1, 0 };
			case SOUTHWEST_DIAGONAL: return { 1, 0, 1 };
			case SOUTHEAST_DIAGONAL: return { 1, 1, 1 };
			default: return { -1, 0, 0 };
		}
	}
}

CarpetAlignGridPanel::CarpetAlignGridPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);

	const int step = CELL_SIZE + CELL_MARGIN;
	const int width = CELL_MARGIN + 3 * step + SECTION_GAP + 2 * step;
	const int height = LABEL_HEIGHT + CELL_MARGIN + 3 * step;
	SetMinSize(wxSize(width, height));
	SetSize(wxSize(width, height));
}

wxRect CarpetAlignGridPanel::CellRectFor(BorderType align) const {
	const CellPlacement place = PlacementFor(align);
	if (place.section < 0) {
		return wxRect();
	}
	const int step = CELL_SIZE + CELL_MARGIN;
	int x = CELL_MARGIN + place.col * step;
	if (place.section == 1) {
		x += 3 * step + SECTION_GAP;
	}
	const int y = LABEL_HEIGHT + CELL_MARGIN + place.row * step;
	return wxRect(x, y, CELL_SIZE, CELL_SIZE);
}

void CarpetAlignGridPanel::SetSelected(BorderType align) {
	if (!IsValidAlign(align)) return;
	m_selected = align;
	Refresh();
}

void CarpetAlignGridPanel::SetCellPreview(BorderType align, uint16_t itemId, int itemCount) {
	if (!IsValidAlign(align)) return;
	m_cells[align].itemId = itemId;
	m_cells[align].itemCount = itemCount;
}

void CarpetAlignGridPanel::ClearPreviews() {
	for (auto& cell : m_cells) {
		cell = CellInfo();
	}
}

BorderType CarpetAlignGridPanel::HitTest(int x, int y) const {
	for (BorderType align : CARPET_ALIGN_ORDER) {
		if (CellRectFor(align).Contains(x, y)) {
			return align;
		}
	}
	return BORDER_NONE;
}

void CarpetAlignGridPanel::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	// Section labels
	dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
	dc.SetTextForeground(Theme::Get(Theme::Role::Text));
	const int step = CELL_SIZE + CELL_MARGIN;
	dc.DrawText("Carpet", CELL_MARGIN, 2);
	dc.DrawText("Diagonals", CELL_MARGIN + 3 * step + SECTION_GAP, 2);

	const int SPRITE_PADDING = 6;
	const int spriteSize = CELL_SIZE - 2 * SPRITE_PADDING;

	for (BorderType align : CARPET_ALIGN_ORDER) {
		const wxRect cell = CellRectFor(align);
		const CellInfo& info = m_cells[align];
		const bool selected = (align == m_selected);
		const bool hovered = (align == m_hovered);

		if (selected) {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Selected)));
		} else if (hovered) {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 1));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
		} else {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
		}
		dc.DrawRoundedRectangle(cell, 3);

		if (info.itemId != 0) {
			DrawItemSprite(dc, info.itemId, cell.x + SPRITE_PADDING, cell.y + SPRITE_PADDING, spriteSize);
			// Tiny alignment code in the corner so filled cells stay identifiable.
			dc.SetFont(wxFont(6, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
			dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
			dc.DrawText(carpetAlignToString(align), cell.x + 3, cell.y + 1);
		} else {
			// Empty slot: the alignment code, dimmed, so the gaps are obvious at a glance.
			dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
			dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
			const wxString code(carpetAlignToString(align));
			const wxSize ts = dc.GetTextExtent(code);
			dc.DrawText(code, cell.x + (CELL_SIZE - ts.GetWidth()) / 2, cell.y + (CELL_SIZE - ts.GetHeight()) / 2);
		}

		if (info.itemCount > 1) {
			const wxString badge = wxString::Format("x%d", info.itemCount);
			dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
			const wxSize bs = dc.GetTextExtent(badge);
			const int bx = cell.x + CELL_SIZE - bs.GetWidth() - 4;
			const int by = cell.y + CELL_SIZE - bs.GetHeight() - 2;
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(wxColour(20, 20, 30)));
			dc.DrawRectangle(bx - 2, by - 1, bs.GetWidth() + 4, bs.GetHeight() + 2);
			dc.SetTextForeground(wxColour(255, 200, 0));
			dc.DrawText(badge, bx, by);
		}
	}
}

void CarpetAlignGridPanel::OnMouseClick(wxMouseEvent& event) {
	const BorderType align = HitTest(event.GetX(), event.GetY());
	if (!IsValidAlign(align)) {
		event.Skip();
		return;
	}
	if (CarpetEditorPanel* editor = FindCarpetEditor(this)) {
		editor->SelectAlignment(align);
	} else {
		SetSelected(align);
	}
}

void CarpetAlignGridPanel::OnMotion(wxMouseEvent& event) {
	const BorderType align = HitTest(event.GetX(), event.GetY());
	if (align != m_hovered) {
		m_hovered = align;
		Refresh();
		if (IsValidAlign(align)) {
			SetToolTip(wxString::Format("%s (%s)", carpetAlignLabel(align), carpetAlignToString(align)));
		}
	}
	event.Skip();
}

void CarpetAlignGridPanel::OnLeave(wxMouseEvent& WXUNUSED(event)) {
	if (m_hovered != BORDER_NONE) {
		m_hovered = BORDER_NONE;
		Refresh();
	}
}

// ============================================================================
// CarpetAlignItemsPanel
// ============================================================================

CarpetAlignItemsPanel::CarpetAlignItemsPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxSize(-1, 2 * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN), wxBORDER_NONE) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(-1, 2 * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN));
}

void CarpetAlignItemsPanel::SetItems(const std::vector<CarpetItemEntry>& items) {
	m_items = items;
	if (m_selectedIndex >= static_cast<int>(m_items.size())) {
		m_selectedIndex = -1;
	}
	RecalcLayout();
	Refresh();
}

void CarpetAlignItemsPanel::Clear() {
	m_items.clear();
	m_cells.clear();
	m_selectedIndex = -1;
	Refresh();
}

void CarpetAlignItemsPanel::RecalcLayout() {
	m_cells.clear();

	int clientWidth = GetClientSize().GetWidth();
	if (clientWidth <= 0) clientWidth = CELL_SIZE + 2 * CELL_MARGIN;

	const int perRow = std::max(1, (clientWidth - CELL_MARGIN) / (CELL_SIZE + CELL_MARGIN));

	int x = CELL_MARGIN;
	int y = CELL_MARGIN;
	int col = 0;
	for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
		CellRect cell;
		cell.index = i;
		cell.bounds = wxRect(x, y, CELL_SIZE, CELL_SIZE);
		cell.closeBtn = wxRect(x + CELL_SIZE - CLOSE_BTN_SIZE - 2, y + 2, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
		m_cells.push_back(cell);

		if (++col >= perRow) {
			col = 0;
			x = CELL_MARGIN;
			y += CELL_SIZE + CELL_MARGIN;
		} else {
			x += CELL_SIZE + CELL_MARGIN;
		}
	}
}

void CarpetAlignItemsPanel::OnSize(wxSizeEvent& event) {
	RecalcLayout();
	Refresh();
	event.Skip();
}

void CarpetAlignItemsPanel::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	if (m_items.empty()) {
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
		dc.DrawText("No items in this slot. Drag from the palette or use Add.", CELL_MARGIN, CELL_MARGIN + 4);
		return;
	}

	const int SPRITE_PADDING = 4;
	const int spriteArea = CELL_SIZE - 2 * SPRITE_PADDING;

	int totalChance = 0;
	for (const auto& item : m_items) totalChance += item.chance;
	if (totalChance <= 0) totalChance = 1;

	for (const auto& cell : m_cells) {
		const auto& item = m_items[cell.index];
		const bool selected = (cell.index == m_selectedIndex);
		if (selected) {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Selected)));
		} else {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
		}
		dc.DrawRoundedRectangle(cell.bounds, 3);

		DrawItemSprite(dc, item.itemId, cell.bounds.x + SPRITE_PADDING, cell.bounds.y + SPRITE_PADDING, spriteArea);

		dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		const int pct = static_cast<int>((static_cast<double>(item.chance) / totalChance) * 100.0 + 0.5);
		const wxString chanceLabel = wxString::Format("%d (%d%%)", item.chance, pct);
		const wxSize ts = dc.GetTextExtent(chanceLabel);
		dc.DrawText(chanceLabel,
			cell.bounds.x + (CELL_SIZE - ts.GetWidth()) / 2,
			cell.bounds.y + CELL_SIZE - ts.GetHeight() - 2);

		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(wxColour(180, 50, 50)));
		dc.DrawRoundedRectangle(cell.closeBtn, 2);
		dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
		dc.SetTextForeground(*wxWHITE);
		const wxSize xs = dc.GetTextExtent("X");
		dc.DrawText("X",
			cell.closeBtn.x + (cell.closeBtn.width - xs.GetWidth()) / 2,
			cell.closeBtn.y + (cell.closeBtn.height - xs.GetHeight()) / 2);
	}
}

void CarpetAlignItemsPanel::OnMouseClick(wxMouseEvent& event) {
	const int mx = event.GetX();
	const int my = event.GetY();
	CarpetEditorPanel* editor = FindCarpetEditor(this);

	for (const auto& cell : m_cells) {
		if (cell.closeBtn.Contains(mx, my)) {
			if (editor) editor->RemoveItemAt(cell.index);
			return;
		}
		if (cell.bounds.Contains(mx, my)) {
			m_selectedIndex = cell.index;
			if (editor) editor->SelectItemAt(cell.index);
			Refresh();
			return;
		}
	}

	m_selectedIndex = -1;
	Refresh();
}

// ============================================================================
// CarpetPreviewPanel
// ============================================================================

namespace {
	// A rounded blob with a one-tile hole: the rounded ends yield the four corner
	// pieces, the straight runs the edges, the hole's neighbours the four inner
	// diagonals, and everything else is center — all 13 slots in one picture.
	constexpr const char* PREVIEW_SHAPE[CarpetPreviewPanel::SHAPE_H] = {
		".XXXXX.",
		"XXXXXXX",
		"XXXXXXX",
		"XXX.XXX",
		"XXXXXXX",
		".XXXXX.",
	};

	bool ShapeHas(int x, int y) {
		if (x < 0 || y < 0 || x >= CarpetPreviewPanel::SHAPE_W || y >= CarpetPreviewPanel::SHAPE_H) {
			return false;
		}
		return PREVIEW_SHAPE[y][x] == 'X';
	}

	// Same bit order as CarpetBorderCalculator: NW, N, NE, W, E, SW, S, SE.
	uint8_t NeighbourMask(int x, int y) {
		static constexpr int offsets[8][2] = {
			{ -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 }
		};
		uint8_t mask = 0;
		for (int i = 0; i < 8; ++i) {
			if (ShapeHas(x + offsets[i][0], y + offsets[i][1])) {
				mask |= static_cast<uint8_t>(1u << i);
			}
		}
		return mask;
	}
}

CarpetPreviewPanel::CarpetPreviewPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition,
		wxSize(SHAPE_W * TILE_SIZE + 2, SHAPE_H * TILE_SIZE + 2), wxBORDER_SIMPLE) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(SHAPE_W * TILE_SIZE + 2, SHAPE_H * TILE_SIZE + 2));
}

void CarpetPreviewPanel::SetSlotItems(const uint16_t (&firstItems)[CARPET_ALIGN_COUNT]) {
	for (int i = 0; i < CARPET_ALIGN_COUNT; ++i) {
		m_firstItems[i] = firstItems[i];
	}
	Refresh();
}

void CarpetPreviewPanel::Clear() {
	for (auto& id : m_firstItems) id = 0;
	Refresh();
}

void CarpetPreviewPanel::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);
	dc.SetBackground(wxBrush(wxColour(30, 30, 40)));
	dc.Clear();

	for (int y = 0; y < SHAPE_H; ++y) {
		for (int x = 0; x < SHAPE_W; ++x) {
			const int px = x * TILE_SIZE;
			const int py = y * TILE_SIZE;

			dc.SetPen(wxPen(wxColour(50, 50, 60)));
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRectangle(px, py, TILE_SIZE, TILE_SIZE);

			if (!ShapeHas(x, y)) continue;

			const BorderType align = CarpetBrush::alignmentForNeighbours(NeighbourMask(x, y));
			const uint16_t itemId = IsValidAlign(align) ? m_firstItems[align] : 0;
			if (itemId != 0) {
				DrawItemSprite(dc, itemId, px, py, TILE_SIZE);
				continue;
			}

			// Missing slot: tint the tile and print the code it would need.
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(wxColour(70, 50, 50)));
			dc.DrawRectangle(px + 1, py + 1, TILE_SIZE - 2, TILE_SIZE - 2);
			dc.SetFont(wxFont(6, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
			dc.SetTextForeground(wxColour(220, 160, 160));
			const wxString code(IsValidAlign(align) ? carpetAlignToString(align) : "?");
			const wxSize ts = dc.GetTextExtent(code);
			dc.DrawText(code, px + (TILE_SIZE - ts.GetWidth()) / 2, py + (TILE_SIZE - ts.GetHeight()) / 2);
		}
	}
}
