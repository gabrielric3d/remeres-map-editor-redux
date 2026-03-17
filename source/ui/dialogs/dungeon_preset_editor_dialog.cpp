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
#include "ui/dialogs/dungeon_preset_editor_dialog.h"
#include "ui/gui.h"
#include "ui/find_item_window.h"
#include "rendering/core/graphics.h"
#include "item_definitions/core/item_definition_store.h"
#include "ui/theme.h"

#include <wx/statline.h>

namespace {

const int PREVIEW_SIZE = 32;

wxBitmap MakeItemBitmap(uint16_t itemId, int size) {
	wxBitmap bmp(size, size, 32);
	wxMemoryDC dc(bmp);
	dc.SetBackground(wxBrush(wxColour(0x0C, 0x14, 0x2A)));
	dc.Clear();

	if (itemId > 0) {
		const auto itemDef = g_item_definitions.get(itemId);
		Sprite* spr = nullptr;
		if (itemDef) {
			spr = g_gui.gfx.getSprite(itemDef.clientId());
		}
		if (spr) {
			spr->DrawTo(&dc, SPRITE_SIZE_32x32, 0, 0, size, size);
		} else {
			dc.SetTextForeground(*wxWHITE);
			dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
			dc.DrawText(wxString::Format("%d", itemId), 2, size / 2 - 6);
		}
	}

	dc.SelectObject(wxNullBitmap);
	return bmp;
}

enum {
	ID_SAVE_BTN = wxID_HIGHEST + 4000,
	ID_CANCEL_BTN,
	ID_DETAIL_GROUP_CHOICE,
	ID_DETAIL_ADD_BTN,
	ID_DETAIL_REMOVE_BTN,
	ID_HANGABLE_H_ADD_BTN,
	ID_HANGABLE_H_REMOVE_BTN,
	ID_HANGABLE_V_ADD_BTN,
	ID_HANGABLE_V_REMOVE_BTN,
};

} // anonymous namespace

//=============================================================================
// ItemFieldDropTarget - accepts RME_ITEM:xxx from palette drag
//=============================================================================

ItemFieldDropTarget::ItemFieldDropTarget(ItemFieldControl* field)
	: m_field(field) {
}

bool ItemFieldDropTarget::OnDropText(wxCoord x, wxCoord y, const wxString& data) {
	if (!m_field) return false;

	wxString itemData = data;

	// Accept both formats from different drag sources
	if (itemData.StartsWith("RME_ITEM:")) {
		itemData = itemData.Mid(9);
	} else if (itemData.StartsWith("ITEM_ID:")) {
		itemData = itemData.Mid(8);
	} else {
		return false;
	}

	unsigned long idVal = 0;
	if (itemData.ToULong(&idVal) && idVal > 0 && idVal <= 0xFFFF) {
		m_field->SetItemId(static_cast<uint16_t>(idVal));
		return true;
	}

	return false;
}

//=============================================================================
// ItemFieldControl - [sprite 32x32] [spin ID] [... pick]
//   Accepts drag & drop from palette
//=============================================================================

ItemFieldControl::ItemFieldControl(wxWindow* parent, wxWindowID id, const wxString& label,
                                   uint16_t initialId, int spinWidth)
	: wxPanel(parent, id)
	, m_itemId(initialId) {

	wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

	// Label
	m_label = new wxStaticText(this, wxID_ANY, label, wxDefaultPosition, wxSize(40, -1));
	sizer->Add(m_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	// Sprite preview (drag target)
	m_previewPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(PREVIEW_SIZE, PREVIEW_SIZE));
	m_previewPanel->SetMinSize(wxSize(PREVIEW_SIZE, PREVIEW_SIZE));
	m_previewPanel->SetToolTip("Drag an item from the palette here");
	m_previewPanel->SetDropTarget(new ItemFieldDropTarget(this));
	sizer->Add(m_previewPanel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	// Spin control
	m_spin = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
	                         wxSize(spinWidth, -1), wxSP_ARROW_KEYS, 0, 65535, initialId);
	sizer->Add(m_spin, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);

	// Pick button
	wxButton* pickBtn = new wxButton(this, wxID_ANY, "...", wxDefaultPosition, wxSize(26, 26));
	pickBtn->SetToolTip("Pick item from database");
	sizer->Add(pickBtn, 0, wxALIGN_CENTER_VERTICAL);

	SetSizer(sizer);

	// Events
	m_spin->Bind(wxEVT_SPINCTRL, &ItemFieldControl::OnSpinChanged, this);
	pickBtn->Bind(wxEVT_BUTTON, &ItemFieldControl::OnPickItem, this);
	m_previewPanel->Bind(wxEVT_PAINT, &ItemFieldControl::OnPreviewPaint, this);
}

uint16_t ItemFieldControl::GetItemId() const {
	return m_itemId;
}

void ItemFieldControl::SetItemId(uint16_t id) {
	m_itemId = id;
	m_spin->SetValue(id);
	UpdatePreview();
}

void ItemFieldControl::OnSpinChanged(wxSpinEvent& event) {
	m_itemId = static_cast<uint16_t>(m_spin->GetValue());
	UpdatePreview();
}

void ItemFieldControl::OnPickItem(wxCommandEvent& event) {
	FindItemDialog dlg(this, "Pick Item");
	if (dlg.ShowModal() == wxID_OK) {
		uint16_t id = dlg.getResultID();
		if (id > 0) {
			SetItemId(id);
		}
	}
}

void ItemFieldControl::OnPreviewPaint(wxPaintEvent& event) {
	wxPaintDC dc(m_previewPanel);
	wxBitmap bmp = MakeItemBitmap(m_itemId, PREVIEW_SIZE);
	dc.DrawBitmap(bmp, 0, 0);
}

void ItemFieldControl::UpdatePreview() {
	m_previewPanel->Refresh();
}

//=============================================================================
// DungeonPresetEditorDialog
//=============================================================================

wxBEGIN_EVENT_TABLE(DungeonPresetEditorDialog, wxDialog)
	EVT_CLOSE(DungeonPresetEditorDialog::OnCloseWindow)
	EVT_BUTTON(ID_SAVE_BTN, DungeonPresetEditorDialog::OnSave)
	EVT_BUTTON(ID_CANCEL_BTN, DungeonPresetEditorDialog::OnCancel)
	EVT_CHOICE(ID_DETAIL_GROUP_CHOICE, DungeonPresetEditorDialog::OnDetailGroupChanged)
	EVT_BUTTON(ID_DETAIL_ADD_BTN, DungeonPresetEditorDialog::OnAddDetailItem)
	EVT_BUTTON(ID_DETAIL_REMOVE_BTN, DungeonPresetEditorDialog::OnRemoveDetailItem)
	EVT_BUTTON(ID_HANGABLE_H_ADD_BTN, DungeonPresetEditorDialog::OnAddHangableH)
	EVT_BUTTON(ID_HANGABLE_H_REMOVE_BTN, DungeonPresetEditorDialog::OnRemoveHangableH)
	EVT_BUTTON(ID_HANGABLE_V_ADD_BTN, DungeonPresetEditorDialog::OnAddHangableV)
	EVT_BUTTON(ID_HANGABLE_V_REMOVE_BTN, DungeonPresetEditorDialog::OnRemoveHangableV)
wxEND_EVENT_TABLE()

DungeonPresetEditorDialog::DungeonPresetEditorDialog(wxWindow* parent, const wxString& editingPreset,
                                                     std::function<void()> onSaveCallback)
	: wxDialog(parent, wxID_ANY,
	           editingPreset.empty() ? wxString("New Dungeon Preset") : wxString("Edit Preset: ") + editingPreset,
	           wxDefaultPosition, wxSize(620, 660), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_originalName(editingPreset)
	, m_isNewPreset(editingPreset.empty())
	, m_onSaveCallback(std::move(onSaveCallback))
	, m_currentDetailGroup(0)
	, m_detailImageList(nullptr)
	, m_hangableHImageList(nullptr)
	, m_hangableVImageList(nullptr) {

	LoadPresetData();
	CreateControls();
	Centre();
}

DungeonPresetEditorDialog::~DungeonPresetEditorDialog() {
	delete m_detailImageList;
	delete m_hangableHImageList;
	delete m_hangableVImageList;
}

void DungeonPresetEditorDialog::LoadPresetData() {
	if (m_isNewPreset) {
		m_preset = DungeonGen::DungeonPreset();
		m_preset.name = "New Preset";
		m_editDetails.resize(6);
	} else {
		auto& mgr = DungeonGen::PresetManager::getInstance();
		const auto* existing = mgr.getPreset(m_originalName.ToStdString());
		if (existing) {
			m_preset = *existing;
		}
		m_editDetails.resize(6);
		for (size_t i = 0; i < m_preset.details.size() && i < 6; ++i) {
			m_editDetails[i] = m_preset.details[i];
		}
	}

	m_editHorizontalIds = m_preset.hangables.horizontalIds;
	m_editVerticalIds = m_preset.hangables.verticalIds;
}

void DungeonPresetEditorDialog::CreateControls() {
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// Preset name
	wxStaticBoxSizer* nameBox = new wxStaticBoxSizer(wxHORIZONTAL, this, "Preset Name");
	m_nameInput = new wxTextCtrl(this, wxID_ANY, m_preset.name);
	nameBox->Add(m_nameInput, 1, wxALL | wxEXPAND, 5);
	mainSizer->Add(nameBox, 0, wxALL | wxEXPAND, 5);

	// Notebook with tabs
	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);
	notebook->AddPage(CreateGeneralTab(notebook), "General");
	notebook->AddPage(CreateWallsTab(notebook), "Walls");
	notebook->AddPage(CreateBordersTab(notebook), "Borders");
	notebook->AddPage(CreateDetailsTab(notebook), "Details");
	notebook->AddPage(CreateHangablesTab(notebook), "Wall Items");
	mainSizer->Add(notebook, 1, wxALL | wxEXPAND, 5);

	// Hint text
	wxStaticText* hint = new wxStaticText(this, wxID_ANY,
		"Tip: Drag items from the palette directly onto any sprite slot.");
	hint->SetForegroundColour(wxColour(140, 140, 140));
	mainSizer->Add(hint, 0, wxLEFT | wxRIGHT, 10);

	// Buttons
	wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
	btnSizer->AddStretchSpacer();
	btnSizer->Add(new wxButton(this, ID_CANCEL_BTN, "Cancel"), 0, wxRIGHT, 5);
	wxButton* saveBtn = new wxButton(this, ID_SAVE_BTN, "Save Preset");
	saveBtn->SetDefault();
	btnSizer->Add(saveBtn, 0);
	mainSizer->Add(btnSizer, 0, wxALL | wxEXPAND, 8);

	SetSizer(mainSizer);
}

//=============================================================================
// Tab: General (terrain tiles)
//=============================================================================

wxPanel* DungeonPresetEditorDialog::CreateGeneralTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* terrainBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Terrain Tiles");

	// Row 1: Ground + Patch
	wxBoxSizer* row1 = new wxBoxSizer(wxHORIZONTAL);
	m_groundField = new ItemFieldControl(panel, wxID_ANY, "Ground", m_preset.groundId);
	m_patchField = new ItemFieldControl(panel, wxID_ANY, "Patch", m_preset.patchId);
	row1->Add(m_groundField, 0, wxRIGHT, 20);
	row1->Add(m_patchField, 0);
	terrainBox->Add(row1, 0, wxALL, 8);

	// Row 2: Fill + Brush
	wxBoxSizer* row2 = new wxBoxSizer(wxHORIZONTAL);
	m_fillField = new ItemFieldControl(panel, wxID_ANY, "Fill", m_preset.fillId);
	m_brushField = new ItemFieldControl(panel, wxID_ANY, "Brush", m_preset.brushId);
	row2->Add(m_fillField, 0, wxRIGHT, 20);
	row2->Add(m_brushField, 0);
	terrainBox->Add(row2, 0, wxALL, 8);

	sizer->Add(terrainBox, 0, wxALL | wxEXPAND, 8);
	panel->SetSizer(sizer);
	return panel;
}

//=============================================================================
// Tab: Walls
//=============================================================================

wxPanel* DungeonPresetEditorDialog::CreateWallsTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	// Straight walls
	wxStaticBoxSizer* straightBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Straight Walls");
	wxBoxSizer* sRow1 = new wxBoxSizer(wxHORIZONTAL);
	m_wallN = new ItemFieldControl(panel, wxID_ANY, "N", m_preset.walls.north);
	m_wallS = new ItemFieldControl(panel, wxID_ANY, "S", m_preset.walls.south);
	sRow1->Add(m_wallN, 0, wxRIGHT, 15);
	sRow1->Add(m_wallS, 0);
	straightBox->Add(sRow1, 0, wxALL, 6);

	wxBoxSizer* sRow2 = new wxBoxSizer(wxHORIZONTAL);
	m_wallE = new ItemFieldControl(panel, wxID_ANY, "E", m_preset.walls.east);
	m_wallW = new ItemFieldControl(panel, wxID_ANY, "W", m_preset.walls.west);
	sRow2->Add(m_wallE, 0, wxRIGHT, 15);
	sRow2->Add(m_wallW, 0);
	straightBox->Add(sRow2, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
	sizer->Add(straightBox, 0, wxALL | wxEXPAND, 6);

	// Corners
	wxStaticBoxSizer* cornerBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Corners");
	wxBoxSizer* cRow1 = new wxBoxSizer(wxHORIZONTAL);
	m_wallNW = new ItemFieldControl(panel, wxID_ANY, "NW", m_preset.walls.nw);
	m_wallNE = new ItemFieldControl(panel, wxID_ANY, "NE", m_preset.walls.ne);
	cRow1->Add(m_wallNW, 0, wxRIGHT, 15);
	cRow1->Add(m_wallNE, 0);
	cornerBox->Add(cRow1, 0, wxALL, 6);

	wxBoxSizer* cRow2 = new wxBoxSizer(wxHORIZONTAL);
	m_wallSW = new ItemFieldControl(panel, wxID_ANY, "SW", m_preset.walls.sw);
	m_wallSE = new ItemFieldControl(panel, wxID_ANY, "SE", m_preset.walls.se);
	cRow2->Add(m_wallSW, 0, wxRIGHT, 15);
	cRow2->Add(m_wallSE, 0);
	cornerBox->Add(cRow2, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
	sizer->Add(cornerBox, 0, wxALL | wxEXPAND, 6);

	// Pillar
	wxStaticBoxSizer* pillarBox = new wxStaticBoxSizer(wxHORIZONTAL, panel, "Pillar");
	m_wallPillar = new ItemFieldControl(panel, wxID_ANY, "Pillar", m_preset.walls.pillar);
	pillarBox->Add(m_wallPillar, 0, wxALL, 6);
	sizer->Add(pillarBox, 0, wxALL | wxEXPAND, 6);

	panel->SetSizer(sizer);
	return panel;
}

//=============================================================================
// Tab: Borders
//=============================================================================

wxPanel* DungeonPresetEditorDialog::CreateBordersTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook);
	wxScrolledWindow* scroll = new wxScrolledWindow(panel, wxID_ANY);
	scroll->SetScrollRate(0, 10);

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	// --- Patch Borders ---
	wxStaticBoxSizer* patchBox = new wxStaticBoxSizer(wxVERTICAL, scroll, "Patch Borders");

	wxBoxSizer* pRow1 = new wxBoxSizer(wxHORIZONTAL);
	m_borderN = new ItemFieldControl(scroll, wxID_ANY, "N", m_preset.borders.north);
	m_borderS = new ItemFieldControl(scroll, wxID_ANY, "S", m_preset.borders.south);
	m_borderE = new ItemFieldControl(scroll, wxID_ANY, "E", m_preset.borders.east);
	m_borderW = new ItemFieldControl(scroll, wxID_ANY, "W", m_preset.borders.west);
	pRow1->Add(m_borderN, 0, wxRIGHT, 8);
	pRow1->Add(m_borderS, 0, wxRIGHT, 8);
	pRow1->Add(m_borderE, 0, wxRIGHT, 8);
	pRow1->Add(m_borderW, 0);
	patchBox->Add(pRow1, 0, wxALL, 4);

	wxBoxSizer* pRow2 = new wxBoxSizer(wxHORIZONTAL);
	m_borderNW = new ItemFieldControl(scroll, wxID_ANY, "NW", m_preset.borders.nw);
	m_borderNE = new ItemFieldControl(scroll, wxID_ANY, "NE", m_preset.borders.ne);
	m_borderSW = new ItemFieldControl(scroll, wxID_ANY, "SW", m_preset.borders.sw);
	m_borderSE = new ItemFieldControl(scroll, wxID_ANY, "SE", m_preset.borders.se);
	pRow2->Add(m_borderNW, 0, wxRIGHT, 8);
	pRow2->Add(m_borderNE, 0, wxRIGHT, 8);
	pRow2->Add(m_borderSW, 0, wxRIGHT, 8);
	pRow2->Add(m_borderSE, 0);
	patchBox->Add(pRow2, 0, wxALL, 4);

	wxBoxSizer* pRow3 = new wxBoxSizer(wxHORIZONTAL);
	m_borderInnerNW = new ItemFieldControl(scroll, wxID_ANY, "iNW", m_preset.borders.inner_nw);
	m_borderInnerNE = new ItemFieldControl(scroll, wxID_ANY, "iNE", m_preset.borders.inner_ne);
	m_borderInnerSW = new ItemFieldControl(scroll, wxID_ANY, "iSW", m_preset.borders.inner_sw);
	m_borderInnerSE = new ItemFieldControl(scroll, wxID_ANY, "iSE", m_preset.borders.inner_se);
	pRow3->Add(m_borderInnerNW, 0, wxRIGHT, 8);
	pRow3->Add(m_borderInnerNE, 0, wxRIGHT, 8);
	pRow3->Add(m_borderInnerSW, 0, wxRIGHT, 8);
	pRow3->Add(m_borderInnerSE, 0);
	patchBox->Add(pRow3, 0, wxALL, 4);

	sizer->Add(patchBox, 0, wxALL | wxEXPAND, 5);

	// --- Brush Borders ---
	wxStaticBoxSizer* brushBox = new wxStaticBoxSizer(wxVERTICAL, scroll, "Brush Borders");

	wxBoxSizer* bRow1 = new wxBoxSizer(wxHORIZONTAL);
	m_bbN = new ItemFieldControl(scroll, wxID_ANY, "N", m_preset.brushBorders.north);
	m_bbS = new ItemFieldControl(scroll, wxID_ANY, "S", m_preset.brushBorders.south);
	m_bbE = new ItemFieldControl(scroll, wxID_ANY, "E", m_preset.brushBorders.east);
	m_bbW = new ItemFieldControl(scroll, wxID_ANY, "W", m_preset.brushBorders.west);
	bRow1->Add(m_bbN, 0, wxRIGHT, 8);
	bRow1->Add(m_bbS, 0, wxRIGHT, 8);
	bRow1->Add(m_bbE, 0, wxRIGHT, 8);
	bRow1->Add(m_bbW, 0);
	brushBox->Add(bRow1, 0, wxALL, 4);

	wxBoxSizer* bRow2 = new wxBoxSizer(wxHORIZONTAL);
	m_bbNW = new ItemFieldControl(scroll, wxID_ANY, "NW", m_preset.brushBorders.nw);
	m_bbNE = new ItemFieldControl(scroll, wxID_ANY, "NE", m_preset.brushBorders.ne);
	m_bbSW = new ItemFieldControl(scroll, wxID_ANY, "SW", m_preset.brushBorders.sw);
	m_bbSE = new ItemFieldControl(scroll, wxID_ANY, "SE", m_preset.brushBorders.se);
	bRow2->Add(m_bbNW, 0, wxRIGHT, 8);
	bRow2->Add(m_bbNE, 0, wxRIGHT, 8);
	bRow2->Add(m_bbSW, 0, wxRIGHT, 8);
	bRow2->Add(m_bbSE, 0);
	brushBox->Add(bRow2, 0, wxALL, 4);

	wxBoxSizer* bRow3 = new wxBoxSizer(wxHORIZONTAL);
	m_bbInnerNW = new ItemFieldControl(scroll, wxID_ANY, "iNW", m_preset.brushBorders.inner_nw);
	m_bbInnerNE = new ItemFieldControl(scroll, wxID_ANY, "iNE", m_preset.brushBorders.inner_ne);
	m_bbInnerSW = new ItemFieldControl(scroll, wxID_ANY, "iSW", m_preset.brushBorders.inner_sw);
	m_bbInnerSE = new ItemFieldControl(scroll, wxID_ANY, "iSE", m_preset.brushBorders.inner_se);
	bRow3->Add(m_bbInnerNW, 0, wxRIGHT, 8);
	bRow3->Add(m_bbInnerNE, 0, wxRIGHT, 8);
	bRow3->Add(m_bbInnerSW, 0, wxRIGHT, 8);
	bRow3->Add(m_bbInnerSE, 0);
	brushBox->Add(bRow3, 0, wxALL, 4);

	sizer->Add(brushBox, 0, wxALL | wxEXPAND, 5);

	scroll->SetSizer(sizer);

	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(scroll, 1, wxEXPAND);
	panel->SetSizer(panelSizer);
	return panel;
}

//=============================================================================
// Tab: Details
//=============================================================================

wxPanel* DungeonPresetEditorDialog::CreateDetailsTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	// Group selector row
	wxStaticBoxSizer* groupBox = new wxStaticBoxSizer(wxHORIZONTAL, panel, "Detail Group");

	m_detailGroupChoice = new wxChoice(panel, ID_DETAIL_GROUP_CHOICE);
	for (int i = 1; i <= 6; ++i) {
		m_detailGroupChoice->Append(wxString::Format("Group %d", i));
	}
	m_detailGroupChoice->SetSelection(0);
	groupBox->Add(m_detailGroupChoice, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

	groupBox->Add(new wxStaticText(panel, wxID_ANY, "Chance:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
	m_detailChanceSpin = new wxSpinCtrlDouble(panel, wxID_ANY, wxEmptyString, wxDefaultPosition,
	                                           wxSize(75, -1), wxSP_ARROW_KEYS, 0.0, 1.0,
	                                           m_editDetails[0].chance, 0.005);
	m_detailChanceSpin->SetDigits(3);
	groupBox->Add(m_detailChanceSpin, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

	groupBox->Add(new wxStaticText(panel, wxID_ANY, "Placement:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
	m_detailPlacementChoice = new wxChoice(panel, wxID_ANY);
	m_detailPlacementChoice->Append("anywhere");
	m_detailPlacementChoice->Append("near_wall");
	m_detailPlacementChoice->Append("north_wall");
	m_detailPlacementChoice->Append("west_wall");
	m_detailPlacementChoice->Append("center");
	m_detailPlacementChoice->SetStringSelection(
		DungeonGen::DetailGroup::placementToString(m_editDetails[0].placement));
	groupBox->Add(m_detailPlacementChoice, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

	sizer->Add(groupBox, 0, wxALL | wxEXPAND, 5);

	// Item list
	wxStaticBoxSizer* itemsBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Items");
	m_detailImageList = new wxImageList(PREVIEW_SIZE, PREVIEW_SIZE, true);
	m_detailItemsList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 160),
	                                    wxLC_ICON | wxLC_SINGLE_SEL);
	m_detailItemsList->SetImageList(m_detailImageList, wxIMAGE_LIST_NORMAL);
	itemsBox->Add(m_detailItemsList, 1, wxEXPAND | wxALL, 3);

	wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
	btnRow->Add(new wxButton(panel, ID_DETAIL_ADD_BTN, "Add Item..."), 0, wxRIGHT, 5);
	btnRow->Add(new wxButton(panel, ID_DETAIL_REMOVE_BTN, "Remove Selected"), 0);
	itemsBox->Add(btnRow, 0, wxALL, 5);

	sizer->Add(itemsBox, 1, wxALL | wxEXPAND, 5);

	panel->SetSizer(sizer);
	RefreshDetailGrid();
	return panel;
}

//=============================================================================
// Tab: Hangables
//=============================================================================

wxPanel* DungeonPresetEditorDialog::CreateHangablesTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	// Settings
	wxStaticBoxSizer* settingsBox = new wxStaticBoxSizer(wxHORIZONTAL, panel, "Settings");
	settingsBox->Add(new wxStaticText(panel, wxID_ANY, "Chance:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_hangableChanceSpin = new wxSpinCtrlDouble(panel, wxID_ANY, wxEmptyString, wxDefaultPosition,
	                                             wxSize(75, -1), wxSP_ARROW_KEYS, 0.0, 1.0,
	                                             m_preset.hangables.chance, 0.01);
	m_hangableChanceSpin->SetDigits(3);
	settingsBox->Add(m_hangableChanceSpin, 0, wxALL, 5);

	m_hangableVerticalCheck = new wxCheckBox(panel, wxID_ANY, "Enable Vertical Walls");
	m_hangableVerticalCheck->SetValue(m_preset.hangables.enableVertical);
	settingsBox->Add(m_hangableVerticalCheck, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 15);
	sizer->Add(settingsBox, 0, wxALL | wxEXPAND, 5);

	// Horizontal
	wxStaticBoxSizer* hBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Horizontal (N/S Walls)");
	m_hangableHImageList = new wxImageList(PREVIEW_SIZE, PREVIEW_SIZE, true);
	m_hangableHList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 90),
	                                  wxLC_ICON | wxLC_SINGLE_SEL);
	m_hangableHList->SetImageList(m_hangableHImageList, wxIMAGE_LIST_NORMAL);
	hBox->Add(m_hangableHList, 1, wxEXPAND | wxALL, 3);

	wxBoxSizer* hBtnRow = new wxBoxSizer(wxHORIZONTAL);
	hBtnRow->Add(new wxButton(panel, ID_HANGABLE_H_ADD_BTN, "Add..."), 0, wxRIGHT, 5);
	hBtnRow->Add(new wxButton(panel, ID_HANGABLE_H_REMOVE_BTN, "Remove"), 0);
	hBox->Add(hBtnRow, 0, wxALL, 3);
	sizer->Add(hBox, 1, wxALL | wxEXPAND, 5);

	// Vertical
	wxStaticBoxSizer* vBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Vertical (W/E Walls)");
	m_hangableVImageList = new wxImageList(PREVIEW_SIZE, PREVIEW_SIZE, true);
	m_hangableVList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 90),
	                                  wxLC_ICON | wxLC_SINGLE_SEL);
	m_hangableVList->SetImageList(m_hangableVImageList, wxIMAGE_LIST_NORMAL);
	vBox->Add(m_hangableVList, 1, wxEXPAND | wxALL, 3);

	wxBoxSizer* vBtnRow = new wxBoxSizer(wxHORIZONTAL);
	vBtnRow->Add(new wxButton(panel, ID_HANGABLE_V_ADD_BTN, "Add..."), 0, wxRIGHT, 5);
	vBtnRow->Add(new wxButton(panel, ID_HANGABLE_V_REMOVE_BTN, "Remove"), 0);
	vBox->Add(vBtnRow, 0, wxALL, 3);
	sizer->Add(vBox, 1, wxALL | wxEXPAND, 5);

	panel->SetSizer(sizer);
	RefreshHangableGrid("h");
	RefreshHangableGrid("v");
	return panel;
}

//=============================================================================
// Detail/Hangable grid helpers
//=============================================================================

void DungeonPresetEditorDialog::RefreshDetailGrid() {
	m_detailItemsList->ClearAll();
	m_detailImageList->RemoveAll();

	const auto& group = m_editDetails[m_currentDetailGroup];
	int imgIdx = 0;
	for (uint16_t id : group.itemIds) {
		if (id > 0) {
			m_detailImageList->Add(MakeItemBitmap(id, PREVIEW_SIZE));
			m_detailItemsList->InsertItem(m_detailItemsList->GetItemCount(),
			                               wxString::Format("%d", id), imgIdx++);
		}
	}
}

void DungeonPresetEditorDialog::RefreshHangableGrid(const wxString& key) {
	if (key == "h") {
		m_hangableHList->ClearAll();
		m_hangableHImageList->RemoveAll();
		int idx = 0;
		for (uint16_t id : m_editHorizontalIds) {
			m_hangableHImageList->Add(MakeItemBitmap(id, PREVIEW_SIZE));
			m_hangableHList->InsertItem(m_hangableHList->GetItemCount(),
			                             wxString::Format("%d", id), idx++);
		}
	} else {
		m_hangableVList->ClearAll();
		m_hangableVImageList->RemoveAll();
		int idx = 0;
		for (uint16_t id : m_editVerticalIds) {
			m_hangableVImageList->Add(MakeItemBitmap(id, PREVIEW_SIZE));
			m_hangableVList->InsertItem(m_hangableVList->GetItemCount(),
			                             wxString::Format("%d", id), idx++);
		}
	}
}

//=============================================================================
// Event handlers
//=============================================================================

void DungeonPresetEditorDialog::OnDetailGroupChanged(wxCommandEvent& event) {
	// Save current group before switching
	m_editDetails[m_currentDetailGroup].chance = static_cast<float>(m_detailChanceSpin->GetValue());
	m_editDetails[m_currentDetailGroup].placement = DungeonGen::DetailGroup::placementFromString(
		m_detailPlacementChoice->GetStringSelection().ToStdString());

	m_currentDetailGroup = m_detailGroupChoice->GetSelection();

	// Load new group
	m_detailChanceSpin->SetValue(m_editDetails[m_currentDetailGroup].chance);
	m_detailPlacementChoice->SetStringSelection(
		DungeonGen::DetailGroup::placementToString(m_editDetails[m_currentDetailGroup].placement));

	RefreshDetailGrid();
}

void DungeonPresetEditorDialog::OnAddDetailItem(wxCommandEvent& event) {
	FindItemDialog dlg(this, "Pick Detail Item");
	if (dlg.ShowModal() == wxID_OK) {
		uint16_t id = dlg.getResultID();
		if (id > 0) {
			m_editDetails[m_currentDetailGroup].itemIds.push_back(id);
			RefreshDetailGrid();
		}
	}
}

void DungeonPresetEditorDialog::OnRemoveDetailItem(wxCommandEvent& event) {
	long sel = m_detailItemsList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel >= 0 && sel < static_cast<long>(m_editDetails[m_currentDetailGroup].itemIds.size())) {
		m_editDetails[m_currentDetailGroup].itemIds.erase(
			m_editDetails[m_currentDetailGroup].itemIds.begin() + sel);
		RefreshDetailGrid();
	}
}

void DungeonPresetEditorDialog::OnAddHangableH(wxCommandEvent& event) {
	FindItemDialog dlg(this, "Pick Horizontal Hangable");
	if (dlg.ShowModal() == wxID_OK) {
		uint16_t id = dlg.getResultID();
		if (id > 0) {
			m_editHorizontalIds.push_back(id);
			RefreshHangableGrid("h");
		}
	}
}

void DungeonPresetEditorDialog::OnRemoveHangableH(wxCommandEvent& event) {
	long sel = m_hangableHList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel >= 0 && sel < static_cast<long>(m_editHorizontalIds.size())) {
		m_editHorizontalIds.erase(m_editHorizontalIds.begin() + sel);
		RefreshHangableGrid("h");
	}
}

void DungeonPresetEditorDialog::OnAddHangableV(wxCommandEvent& event) {
	FindItemDialog dlg(this, "Pick Vertical Hangable");
	if (dlg.ShowModal() == wxID_OK) {
		uint16_t id = dlg.getResultID();
		if (id > 0) {
			m_editVerticalIds.push_back(id);
			RefreshHangableGrid("v");
		}
	}
}

void DungeonPresetEditorDialog::OnRemoveHangableV(wxCommandEvent& event) {
	long sel = m_hangableVList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel >= 0 && sel < static_cast<long>(m_editVerticalIds.size())) {
		m_editVerticalIds.erase(m_editVerticalIds.begin() + sel);
		RefreshHangableGrid("v");
	}
}

void DungeonPresetEditorDialog::OnSave(wxCommandEvent& event) {
	SavePresetData();
}

void DungeonPresetEditorDialog::OnCancel(wxCommandEvent& event) {
	Destroy();
}

void DungeonPresetEditorDialog::OnCloseWindow(wxCloseEvent& event) {
	Destroy();
}

//=============================================================================
// Save preset
//=============================================================================

void DungeonPresetEditorDialog::SavePresetData() {
	wxString newName = m_nameInput->GetValue().Trim().Trim(false);
	if (newName.empty()) {
		wxMessageBox("Preset name cannot be empty.", "Error", wxICON_WARNING | wxOK, this);
		return;
	}

	// Save current detail group
	m_editDetails[m_currentDetailGroup].chance = static_cast<float>(m_detailChanceSpin->GetValue());
	m_editDetails[m_currentDetailGroup].placement = DungeonGen::DetailGroup::placementFromString(
		m_detailPlacementChoice->GetStringSelection().ToStdString());

	// Build preset
	m_preset.name = newName.ToStdString();

	// General
	m_preset.groundId = m_groundField->GetItemId();
	m_preset.patchId = m_patchField->GetItemId();
	m_preset.fillId = m_fillField->GetItemId();
	m_preset.brushId = m_brushField->GetItemId();

	// Walls
	m_preset.walls.north = m_wallN->GetItemId();
	m_preset.walls.south = m_wallS->GetItemId();
	m_preset.walls.east = m_wallE->GetItemId();
	m_preset.walls.west = m_wallW->GetItemId();
	m_preset.walls.nw = m_wallNW->GetItemId();
	m_preset.walls.ne = m_wallNE->GetItemId();
	m_preset.walls.sw = m_wallSW->GetItemId();
	m_preset.walls.se = m_wallSE->GetItemId();
	m_preset.walls.pillar = m_wallPillar->GetItemId();

	// Borders
	m_preset.borders.north = m_borderN->GetItemId();
	m_preset.borders.south = m_borderS->GetItemId();
	m_preset.borders.east = m_borderE->GetItemId();
	m_preset.borders.west = m_borderW->GetItemId();
	m_preset.borders.nw = m_borderNW->GetItemId();
	m_preset.borders.ne = m_borderNE->GetItemId();
	m_preset.borders.sw = m_borderSW->GetItemId();
	m_preset.borders.se = m_borderSE->GetItemId();
	m_preset.borders.inner_nw = m_borderInnerNW->GetItemId();
	m_preset.borders.inner_ne = m_borderInnerNE->GetItemId();
	m_preset.borders.inner_sw = m_borderInnerSW->GetItemId();
	m_preset.borders.inner_se = m_borderInnerSE->GetItemId();

	// Brush Borders
	m_preset.brushBorders.north = m_bbN->GetItemId();
	m_preset.brushBorders.south = m_bbS->GetItemId();
	m_preset.brushBorders.east = m_bbE->GetItemId();
	m_preset.brushBorders.west = m_bbW->GetItemId();
	m_preset.brushBorders.nw = m_bbNW->GetItemId();
	m_preset.brushBorders.ne = m_bbNE->GetItemId();
	m_preset.brushBorders.sw = m_bbSW->GetItemId();
	m_preset.brushBorders.se = m_bbSE->GetItemId();
	m_preset.brushBorders.inner_nw = m_bbInnerNW->GetItemId();
	m_preset.brushBorders.inner_ne = m_bbInnerNE->GetItemId();
	m_preset.brushBorders.inner_sw = m_bbInnerSW->GetItemId();
	m_preset.brushBorders.inner_se = m_bbInnerSE->GetItemId();

	// Details
	m_preset.details.clear();
	for (int i = 0; i < 6; ++i) {
		const auto& grp = m_editDetails[i];
		if (!grp.itemIds.empty() || grp.chance > 0.0f) {
			m_preset.details.push_back(grp);
		}
	}

	// Hangables
	m_preset.hangables.horizontalIds = m_editHorizontalIds;
	m_preset.hangables.verticalIds = m_editVerticalIds;
	m_preset.hangables.chance = static_cast<float>(m_hangableChanceSpin->GetValue());
	m_preset.hangables.enableVertical = m_hangableVerticalCheck->GetValue();

	// Save
	auto& mgr = DungeonGen::PresetManager::getInstance();

	if (!m_isNewPreset && m_originalName != newName) {
		mgr.removePreset(m_originalName.ToStdString());
	}

	mgr.addPreset(m_preset);

	if (m_onSaveCallback) {
		m_onSaveCallback();
	}

	Destroy();
}
