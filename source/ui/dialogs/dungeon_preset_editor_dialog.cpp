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
#include "brushes/brush.h"
#include "brushes/ground/ground_brush.h"
#include "brushes/ground/auto_border.h"
#include "brushes/wall/wall_brush.h"
#include "brushes/wall/wall_brush_items.h"
#include "brushes/brush_enums.h"
#include "brushes/doodad/doodad_brush.h"
#include "brushes/doodad/doodad_brush_items.h"
#include "game/item.h"
#include "editor/hotkey_manager.h"

#include <wx/statline.h>

namespace {

const int PREVIEW_SIZE = 32;

wxBitmap MakeItemBitmap(uint16_t itemId, int size) {
	wxBitmap bmp(size, size, 32);
	wxMemoryDC dc(bmp);
	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
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
	ID_LOAD_GROUND_BRUSH,
	ID_LOAD_GROUND_BRUSH_GENERAL,
	ID_LOAD_WALL_BRUSH,
	ID_LOAD_DOODAD_BRUSH,
	ID_ROOM_FLOOR_ADD_BTN,
	ID_ROOM_FLOOR_ADD_RANGE_BTN,
	ID_ROOM_FLOOR_REMOVE_BTN,
	ID_CORRIDOR_FLOOR_ADD_BTN,
	ID_CORRIDOR_FLOOR_ADD_RANGE_BTN,
	ID_CORRIDOR_FLOOR_REMOVE_BTN,
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
	EVT_BUTTON(ID_LOAD_GROUND_BRUSH, DungeonPresetEditorDialog::OnLoadGroundBrush)
	EVT_BUTTON(ID_LOAD_GROUND_BRUSH_GENERAL, DungeonPresetEditorDialog::OnLoadGroundBrushGeneral)
	EVT_BUTTON(ID_LOAD_WALL_BRUSH, DungeonPresetEditorDialog::OnLoadWallBrush)
	EVT_BUTTON(ID_LOAD_DOODAD_BRUSH, DungeonPresetEditorDialog::OnLoadDoodadBrush)
	EVT_BUTTON(ID_ROOM_FLOOR_ADD_BTN, DungeonPresetEditorDialog::OnAddRoomFloor)
	EVT_BUTTON(ID_ROOM_FLOOR_ADD_RANGE_BTN, DungeonPresetEditorDialog::OnAddRoomFloorRange)
	EVT_BUTTON(ID_ROOM_FLOOR_REMOVE_BTN, DungeonPresetEditorDialog::OnRemoveRoomFloor)
	EVT_BUTTON(ID_CORRIDOR_FLOOR_ADD_BTN, DungeonPresetEditorDialog::OnAddCorridorFloor)
	EVT_BUTTON(ID_CORRIDOR_FLOOR_ADD_RANGE_BTN, DungeonPresetEditorDialog::OnAddCorridorFloorRange)
	EVT_BUTTON(ID_CORRIDOR_FLOOR_REMOVE_BTN, DungeonPresetEditorDialog::OnRemoveCorridorFloor)
wxEND_EVENT_TABLE()

DungeonPresetEditorDialog::DungeonPresetEditorDialog(wxWindow* parent, const wxString& editingPreset,
                                                     std::function<void()> onSaveCallback)
	: wxDialog(parent, wxID_ANY,
	           editingPreset.empty() ? wxString("New Dungeon Preset") : wxString("Edit Preset: ") + editingPreset,
	           wxDefaultPosition, wxSize(850, 680), wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER | wxFRAME_FLOAT_ON_PARENT)
	, m_originalName(editingPreset)
	, m_isNewPreset(editingPreset.empty())
	, m_onSaveCallback(std::move(onSaveCallback))
	, m_currentDetailGroup(0)
	, m_detailImageList(nullptr)
	, m_hangableHImageList(nullptr)
	, m_hangableVImageList(nullptr)
	, m_wallBrushImageList(nullptr)
	, m_groundBrushImageList(nullptr)
	, m_roomFloorList(nullptr)
	, m_corridorFloorList(nullptr)
	, m_roomFloorImageList(nullptr)
	, m_corridorFloorImageList(nullptr)
	, m_borderTarget(TARGET_PATCH_BORDER)
	, m_borderTargetLabel(nullptr)
	, m_generalGroundBrushList(nullptr)
	, m_generalGroundBrushImageList(nullptr)
	, m_doodadBrushList(nullptr)
	, m_doodadBrushImageList(nullptr)
	, m_generalGroundSearch(nullptr)
	, m_wallBrushSearch(nullptr)
	, m_groundBrushSearch(nullptr)
	, m_doodadBrushSearch(nullptr) {

	LoadPresetData();
	CreateControls();
	Centre();
}

DungeonPresetEditorDialog::~DungeonPresetEditorDialog() {
	delete m_detailImageList;
	delete m_hangableHImageList;
	delete m_hangableVImageList;
	delete m_wallBrushImageList;
	delete m_groundBrushImageList;
	delete m_generalGroundBrushImageList;
	delete m_doodadBrushImageList;
	delete m_roomFloorImageList;
	delete m_corridorFloorImageList;
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
	m_editRoomFloors = m_preset.roomFloors.items;
	m_editCorridorFloors = m_preset.corridorFloors.items;
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
	hint->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
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
	wxBoxSizer* mainHSizer = new wxBoxSizer(wxHORIZONTAL);

	// ===== LEFT COLUMN: terrain + floor variations =====
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);

	// Terrain tiles
	wxStaticBoxSizer* terrainBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Terrain Tiles");
	wxBoxSizer* row1 = new wxBoxSizer(wxHORIZONTAL);
	m_groundField = new ItemFieldControl(panel, wxID_ANY, "Ground", m_preset.groundId);
	m_fillField = new ItemFieldControl(panel, wxID_ANY, "Fill", m_preset.fillId);
	row1->Add(m_groundField, 0, wxRIGHT, 20);
	row1->Add(m_fillField, 0);
	terrainBox->Add(row1, 0, wxALL, 8);
	leftSizer->Add(terrainBox, 0, wxALL | wxEXPAND, 5);

	// Room Floors
	wxStaticBoxSizer* roomFloorBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Room Floor Variations");
	m_roomFloorImageList = new wxImageList(PREVIEW_SIZE, PREVIEW_SIZE, true);
	m_roomFloorList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 100),
	                                  wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
	m_roomFloorList->SetImageList(m_roomFloorImageList, wxIMAGE_LIST_SMALL);
	m_roomFloorList->InsertColumn(0, "Item");
	m_roomFloorList->InsertColumn(1, "Chance");
	roomFloorBox->Add(m_roomFloorList, 1, wxEXPAND | wxALL, 3);
	wxBoxSizer* roomBtnRow = new wxBoxSizer(wxHORIZONTAL);
	roomBtnRow->Add(new wxButton(panel, ID_ROOM_FLOOR_ADD_BTN, "Add..."), 0, wxRIGHT, 5);
	roomBtnRow->Add(new wxButton(panel, ID_ROOM_FLOOR_ADD_RANGE_BTN, "Add Range..."), 0, wxRIGHT, 5);
	roomBtnRow->Add(new wxButton(panel, ID_ROOM_FLOOR_REMOVE_BTN, "Remove"), 0);
	roomFloorBox->Add(roomBtnRow, 0, wxALL, 3);
	leftSizer->Add(roomFloorBox, 1, wxALL | wxEXPAND, 5);

	// Corridor Floors
	wxStaticBoxSizer* corridorFloorBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Corridor Floor Variations");
	m_corridorFloorImageList = new wxImageList(PREVIEW_SIZE, PREVIEW_SIZE, true);
	m_corridorFloorList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 100),
	                                      wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
	m_corridorFloorList->SetImageList(m_corridorFloorImageList, wxIMAGE_LIST_SMALL);
	m_corridorFloorList->InsertColumn(0, "Item");
	m_corridorFloorList->InsertColumn(1, "Chance");
	corridorFloorBox->Add(m_corridorFloorList, 1, wxEXPAND | wxALL, 3);
	wxBoxSizer* corrBtnRow = new wxBoxSizer(wxHORIZONTAL);
	corrBtnRow->Add(new wxButton(panel, ID_CORRIDOR_FLOOR_ADD_BTN, "Add..."), 0, wxRIGHT, 5);
	corrBtnRow->Add(new wxButton(panel, ID_CORRIDOR_FLOOR_ADD_RANGE_BTN, "Add Range..."), 0, wxRIGHT, 5);
	corrBtnRow->Add(new wxButton(panel, ID_CORRIDOR_FLOOR_REMOVE_BTN, "Remove"), 0);
	corridorFloorBox->Add(corrBtnRow, 0, wxALL, 3);
	leftSizer->Add(corridorFloorBox, 1, wxALL | wxEXPAND, 5);

	RefreshFloorList(m_roomFloorList, m_roomFloorImageList, m_editRoomFloors);
	RefreshFloorList(m_corridorFloorList, m_corridorFloorImageList, m_editCorridorFloors);

	mainHSizer->Add(leftSizer, 3, wxEXPAND | wxALL, 2);

	// ===== RIGHT COLUMN: ground brush loader =====
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* loadBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Load from Ground Brush");

	m_generalGroundSearch = CreateSearchField(panel);
	m_generalGroundSearch->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { RebuildGeneralGroundBrushList(); });
	loadBox->Add(m_generalGroundSearch, 0, wxEXPAND | wxALL, 3);

	m_generalGroundBrushImageList = new wxImageList(PREVIEW_SIZE, PREVIEW_SIZE, true);
	m_generalGroundBrushList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 160),
	                                          wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
	m_generalGroundBrushList->SetImageList(m_generalGroundBrushImageList, wxIMAGE_LIST_SMALL);
	m_generalGroundBrushList->InsertColumn(0, "Name");

	loadBox->Add(m_generalGroundBrushList, 1, wxEXPAND | wxALL, 3);
	loadBox->Add(new wxButton(panel, ID_LOAD_GROUND_BRUSH_GENERAL, "Apply Ground Tile"), 0, wxALL | wxEXPAND, 3);

	RebuildGeneralGroundBrushList();

	rightSizer->Add(loadBox, 1, wxALL | wxEXPAND, 3);
	mainHSizer->Add(rightSizer, 2, wxEXPAND | wxALL, 2);

	panel->SetSizer(mainHSizer);
	return panel;
}

//=============================================================================
// Tab: Walls
//=============================================================================

wxPanel* DungeonPresetEditorDialog::CreateWallsTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook);

	// Helper to load wall grid from WallConfig
	auto loadWallGrid = [](DungeonSlotGridPanel* grid, const DungeonGen::WallConfig& cfg) {
		grid->SetItemId(DSLOT_N, cfg.primaryId(cfg.north));
		grid->SetItemId(DSLOT_S, cfg.primaryId(cfg.south));
		grid->SetItemId(DSLOT_E, cfg.primaryId(cfg.east));
		grid->SetItemId(DSLOT_W, cfg.primaryId(cfg.west));
		grid->SetItemId(DSLOT_NW, cfg.primaryId(cfg.nw));
		grid->SetItemId(DSLOT_NE, cfg.primaryId(cfg.ne));
		grid->SetItemId(DSLOT_SW, cfg.primaryId(cfg.sw));
		grid->SetItemId(DSLOT_SE, cfg.primaryId(cfg.se));
		grid->SetItemId(DSLOT_PILLAR, cfg.primaryId(cfg.pillar));
	};

	// Main horizontal layout: left = wall grids, right = brush loader
	wxBoxSizer* mainHSizer = new wxBoxSizer(wxHORIZONTAL);

	// ===== LEFT COLUMN: wall grids =====
	wxScrolledWindow* leftScroll = new wxScrolledWindow(panel, wxID_ANY);
	leftScroll->SetScrollRate(0, 10);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);

	// Room Walls
	wxStaticBoxSizer* roomBox = new wxStaticBoxSizer(wxVERTICAL, leftScroll, "Room Walls");
	m_wallsGrid = new DungeonSlotGridPanel(leftScroll, DungeonSlotGridPanel::MODE_WALLS);
	loadWallGrid(m_wallsGrid, m_preset.walls);
	m_wallsGrid->SetHighlighted(true);
	roomBox->Add(m_wallsGrid, 0, wxALL | wxEXPAND, 3);
	leftSizer->Add(roomBox, 0, wxALL | wxEXPAND, 3);

	// Corridor Walls
	wxStaticBoxSizer* corridorBox = new wxStaticBoxSizer(wxVERTICAL, leftScroll, "Corridor Walls (optional)");
	m_corridorWallsGrid = new DungeonSlotGridPanel(leftScroll, DungeonSlotGridPanel::MODE_WALLS);
	loadWallGrid(m_corridorWallsGrid, m_preset.corridorWalls);
	corridorBox->Add(m_corridorWallsGrid, 0, wxALL | wxEXPAND, 3);
	leftSizer->Add(corridorBox, 0, wxALL | wxEXPAND, 3);

	leftScroll->SetSizer(leftSizer);
	mainHSizer->Add(leftScroll, 3, wxEXPAND | wxALL, 2);

	// ===== RIGHT COLUMN: target selection + brush loader =====
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);

	// Target selection
	m_wallTarget = WALL_TARGET_ROOM;

	wxStaticBoxSizer* targetBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Apply to");
	wxRadioButton* rRoomWalls = new wxRadioButton(panel, wxID_ANY, "Room Walls", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	wxRadioButton* rCorridorWalls = new wxRadioButton(panel, wxID_ANY, "Corridor Walls");
	rRoomWalls->SetValue(true);

	rRoomWalls->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { UpdateWallTarget(WALL_TARGET_ROOM); });
	rCorridorWalls->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { UpdateWallTarget(WALL_TARGET_CORRIDOR); });

	targetBox->Add(rRoomWalls, 0, wxALL, 2);
	targetBox->Add(rCorridorWalls, 0, wxALL, 2);

	m_wallTargetLabel = new wxStaticText(panel, wxID_ANY, "Target: Room Walls");
	m_wallTargetLabel->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	targetBox->Add(m_wallTargetLabel, 0, wxALL, 4);

	rightSizer->Add(targetBox, 0, wxALL | wxEXPAND, 3);

	// Brush list
	wxStaticBoxSizer* loadBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Wall Brushes");

	m_wallBrushSearch = CreateSearchField(panel);
	m_wallBrushSearch->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { RebuildWallBrushList(); });
	loadBox->Add(m_wallBrushSearch, 0, wxEXPAND | wxALL, 3);

	m_wallBrushImageList = new wxImageList(PREVIEW_SIZE, PREVIEW_SIZE, true);
	m_wallBrushList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(-1, 120),
	                                  wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
	m_wallBrushList->SetImageList(m_wallBrushImageList, wxIMAGE_LIST_SMALL);
	m_wallBrushList->InsertColumn(0, "Name");

	loadBox->Add(m_wallBrushList, 1, wxEXPAND | wxALL, 3);

	RebuildWallBrushList();

	loadBox->Add(new wxButton(panel, ID_LOAD_WALL_BRUSH, "Apply Wall Brush"), 0, wxALL | wxEXPAND, 3);

	rightSizer->Add(loadBox, 1, wxALL | wxEXPAND, 3);
	mainHSizer->Add(rightSizer, 2, wxEXPAND | wxALL, 2);

	panel->SetSizer(mainHSizer);
	return panel;
}

//=============================================================================
// Tab: Borders
//=============================================================================

wxPanel* DungeonPresetEditorDialog::CreateBordersTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook);

	// Main horizontal layout: left = grids, right = brush loader
	wxBoxSizer* mainHSizer = new wxBoxSizer(wxHORIZONTAL);

	// ===== LEFT COLUMN: terrain fields + border grids =====
	wxScrolledWindow* leftScroll = new wxScrolledWindow(panel, wxID_ANY);
	leftScroll->SetScrollRate(0, 10);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);

	// Border terrain tiles
	wxStaticBoxSizer* terrainBox = new wxStaticBoxSizer(wxVERTICAL, leftScroll, "Border Terrain Tiles");
	wxBoxSizer* terrainRow = new wxBoxSizer(wxHORIZONTAL);
	m_patchField = new ItemFieldControl(leftScroll, wxID_ANY, "Patch", m_preset.patchId);
	m_brushField = new ItemFieldControl(leftScroll, wxID_ANY, "Brush", m_preset.brushId);
	terrainRow->Add(m_patchField, 0, wxRIGHT, 20);
	terrainRow->Add(m_brushField, 0);
	terrainBox->Add(terrainRow, 0, wxALL, 4);
	leftSizer->Add(terrainBox, 0, wxALL | wxEXPAND, 3);

	// Patch Borders grid
	wxStaticBoxSizer* patchBox = new wxStaticBoxSizer(wxVERTICAL, leftScroll, "Patch Borders");
	m_patchBordersGrid = new DungeonSlotGridPanel(leftScroll, DungeonSlotGridPanel::MODE_BORDERS);
	m_patchBordersGrid->SetItemId(DSLOT_N, m_preset.borders.north);
	m_patchBordersGrid->SetItemId(DSLOT_S, m_preset.borders.south);
	m_patchBordersGrid->SetItemId(DSLOT_E, m_preset.borders.east);
	m_patchBordersGrid->SetItemId(DSLOT_W, m_preset.borders.west);
	m_patchBordersGrid->SetItemId(DSLOT_NW, m_preset.borders.nw);
	m_patchBordersGrid->SetItemId(DSLOT_NE, m_preset.borders.ne);
	m_patchBordersGrid->SetItemId(DSLOT_SW, m_preset.borders.sw);
	m_patchBordersGrid->SetItemId(DSLOT_SE, m_preset.borders.se);
	m_patchBordersGrid->SetItemId(DSLOT_INNER_NW, m_preset.borders.inner_nw);
	m_patchBordersGrid->SetItemId(DSLOT_INNER_NE, m_preset.borders.inner_ne);
	m_patchBordersGrid->SetItemId(DSLOT_INNER_SW, m_preset.borders.inner_sw);
	m_patchBordersGrid->SetItemId(DSLOT_INNER_SE, m_preset.borders.inner_se);
	m_patchBordersGrid->SetHighlighted(true);
	patchBox->Add(m_patchBordersGrid, 0, wxEXPAND | wxALL, 2);
	leftSizer->Add(patchBox, 0, wxALL | wxEXPAND, 3);

	// Brush Borders grid
	wxStaticBoxSizer* brushBox = new wxStaticBoxSizer(wxVERTICAL, leftScroll, "Brush Borders");
	m_brushBordersGrid = new DungeonSlotGridPanel(leftScroll, DungeonSlotGridPanel::MODE_BORDERS);
	m_brushBordersGrid->SetItemId(DSLOT_N, m_preset.brushBorders.north);
	m_brushBordersGrid->SetItemId(DSLOT_S, m_preset.brushBorders.south);
	m_brushBordersGrid->SetItemId(DSLOT_E, m_preset.brushBorders.east);
	m_brushBordersGrid->SetItemId(DSLOT_W, m_preset.brushBorders.west);
	m_brushBordersGrid->SetItemId(DSLOT_NW, m_preset.brushBorders.nw);
	m_brushBordersGrid->SetItemId(DSLOT_NE, m_preset.brushBorders.ne);
	m_brushBordersGrid->SetItemId(DSLOT_SW, m_preset.brushBorders.sw);
	m_brushBordersGrid->SetItemId(DSLOT_SE, m_preset.brushBorders.se);
	m_brushBordersGrid->SetItemId(DSLOT_INNER_NW, m_preset.brushBorders.inner_nw);
	m_brushBordersGrid->SetItemId(DSLOT_INNER_NE, m_preset.brushBorders.inner_ne);
	m_brushBordersGrid->SetItemId(DSLOT_INNER_SW, m_preset.brushBorders.inner_sw);
	m_brushBordersGrid->SetItemId(DSLOT_INNER_SE, m_preset.brushBorders.inner_se);
	brushBox->Add(m_brushBordersGrid, 0, wxEXPAND | wxALL, 2);
	leftSizer->Add(brushBox, 0, wxALL | wxEXPAND, 3);

	leftScroll->SetSizer(leftSizer);
	mainHSizer->Add(leftScroll, 3, wxEXPAND | wxALL, 2);

	// ===== RIGHT COLUMN: target selection + brush list =====
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);

	// Target selection
	m_borderTarget = TARGET_PATCH_BORDER;

	wxStaticBoxSizer* targetBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Apply to");
	wxRadioButton* rPatchBorder = new wxRadioButton(panel, wxID_ANY, "Patch Borders", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	wxRadioButton* rBrushBorder = new wxRadioButton(panel, wxID_ANY, "Brush Borders");
	wxRadioButton* rPatchTerrain = new wxRadioButton(panel, wxID_ANY, "Patch Terrain");
	wxRadioButton* rBrushTerrain = new wxRadioButton(panel, wxID_ANY, "Brush Terrain");
	rPatchBorder->SetValue(true);

	rPatchBorder->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { UpdateBorderTarget(TARGET_PATCH_BORDER); });
	rBrushBorder->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { UpdateBorderTarget(TARGET_BRUSH_BORDER); });
	rPatchTerrain->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { UpdateBorderTarget(TARGET_PATCH_TERRAIN); });
	rBrushTerrain->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) { UpdateBorderTarget(TARGET_BRUSH_TERRAIN); });

	targetBox->Add(rPatchBorder, 0, wxALL, 2);
	targetBox->Add(rBrushBorder, 0, wxALL, 2);
	targetBox->Add(rPatchTerrain, 0, wxALL, 2);
	targetBox->Add(rBrushTerrain, 0, wxALL, 2);

	m_borderTargetLabel = new wxStaticText(panel, wxID_ANY, "Target: Patch Borders");
	m_borderTargetLabel->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	targetBox->Add(m_borderTargetLabel, 0, wxALL, 4);

	rightSizer->Add(targetBox, 0, wxALL | wxEXPAND, 3);

	// Brush list
	wxStaticBoxSizer* loadBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Ground Brushes");

	m_groundBrushSearch = CreateSearchField(panel);
	m_groundBrushSearch->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { RebuildGroundBrushList(); });
	loadBox->Add(m_groundBrushSearch, 0, wxEXPAND | wxALL, 3);

	m_groundBrushImageList = new wxImageList(PREVIEW_SIZE, PREVIEW_SIZE, true);
	m_groundBrushList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                                    wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
	m_groundBrushList->SetImageList(m_groundBrushImageList, wxIMAGE_LIST_SMALL);
	m_groundBrushList->InsertColumn(0, "Name");

	loadBox->Add(m_groundBrushList, 1, wxEXPAND | wxALL, 3);
	loadBox->Add(new wxButton(panel, ID_LOAD_GROUND_BRUSH, "Apply from Brush"), 0, wxALL | wxEXPAND, 3);

	rightSizer->Add(loadBox, 1, wxALL | wxEXPAND, 3);

	mainHSizer->Add(rightSizer, 1, wxEXPAND | wxALL, 2);

	panel->SetSizer(mainHSizer);

	RebuildGroundBrushList();

	return panel;
}

//=============================================================================
// Tab: Details
//=============================================================================

wxPanel* DungeonPresetEditorDialog::CreateDetailsTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook);
	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

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
	m_detailPlacementChoice->Append("room_only");
	m_detailPlacementChoice->Append("corridor_only");
	m_detailPlacementChoice->SetStringSelection(
		DungeonGen::DetailGroup::placementToString(m_editDetails[0].placement));
	groupBox->Add(m_detailPlacementChoice, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

	topSizer->Add(groupBox, 0, wxALL | wxEXPAND, 5);

	// Horizontal layout: items left, doodad brushes right
	wxBoxSizer* hSizer = new wxBoxSizer(wxHORIZONTAL);

	// Left: current group items
	wxStaticBoxSizer* itemsBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Items");
	m_detailImageList = new wxImageList(PREVIEW_SIZE, PREVIEW_SIZE, true);
	m_detailItemsList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                                    wxLC_ICON | wxLC_SINGLE_SEL);
	m_detailItemsList->SetImageList(m_detailImageList, wxIMAGE_LIST_NORMAL);
	itemsBox->Add(m_detailItemsList, 1, wxEXPAND | wxALL, 3);

	wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
	btnRow->Add(new wxButton(panel, ID_DETAIL_ADD_BTN, "Add Item..."), 0, wxRIGHT, 5);
	btnRow->Add(new wxButton(panel, ID_DETAIL_REMOVE_BTN, "Remove Selected"), 0);
	itemsBox->Add(btnRow, 0, wxALL, 3);

	hSizer->Add(itemsBox, 3, wxEXPAND | wxRIGHT, 5);

	// Right: doodad brush list
	wxStaticBoxSizer* doodadBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Load from Doodad Brush");

	m_doodadBrushSearch = CreateSearchField(panel);
	m_doodadBrushSearch->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { RebuildDoodadBrushList(); });
	doodadBox->Add(m_doodadBrushSearch, 0, wxEXPAND | wxALL, 3);

	m_doodadBrushImageList = new wxImageList(PREVIEW_SIZE, PREVIEW_SIZE, true);
	m_doodadBrushList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
	                                    wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
	m_doodadBrushList->SetImageList(m_doodadBrushImageList, wxIMAGE_LIST_SMALL);
	m_doodadBrushList->InsertColumn(0, "Name");

	doodadBox->Add(m_doodadBrushList, 1, wxEXPAND | wxALL, 3);

	RebuildDoodadBrushList();
	doodadBox->Add(new wxButton(panel, ID_LOAD_DOODAD_BRUSH, "Add Items from Brush"), 0, wxALL | wxEXPAND, 3);

	hSizer->Add(doodadBox, 1, wxEXPAND);

	topSizer->Add(hSizer, 1, wxALL | wxEXPAND, 5);

	panel->SetSizer(topSizer);
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

void DungeonPresetEditorDialog::OnLoadDoodadBrush(wxCommandEvent& event) {
	long sel = m_doodadBrushList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0) {
		wxMessageBox("Select a doodad brush first.", "Info", wxICON_INFORMATION | wxOK, this);
		return;
	}
	wxString selected = m_doodadBrushList->GetItemText(sel);

	Brush* brush = g_brushes.getBrush(selected.ToStdString());
	if (!brush) return;

	DoodadBrush* doodadBrush = brush->as<DoodadBrush>();
	if (!doodadBrush) return;

	// Extract all single item IDs from all alternatives
	int added = 0;
	const auto& alternatives = doodadBrush->getItems().getAlternatives();
	for (const auto& alt : alternatives) {
		if (!alt) continue;
		for (const auto& single : alt->single_items) {
			if (single.item) {
				uint16_t id = single.item->getID();
				if (id > 0) {
					// Avoid duplicates
					auto& ids = m_editDetails[m_currentDetailGroup].itemIds;
					if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
						ids.push_back(id);
						++added;
					}
				}
			}
		}
	}

	RefreshDetailGrid();

	if (added > 0) {
		wxMessageBox(wxString::Format("Added %d items from '%s'.", added, selected),
		             "Loaded", wxICON_INFORMATION | wxOK, this);
	} else {
		wxMessageBox("No new items to add.", "Info", wxICON_INFORMATION | wxOK, this);
	}
}

void DungeonPresetEditorDialog::OnSave(wxCommandEvent& event) {
	SavePresetData();
}

void DungeonPresetEditorDialog::OnCancel(wxCommandEvent& event) {
	if (IsModal()) EndModal(wxID_CANCEL);
	else Destroy();
}

void DungeonPresetEditorDialog::OnCloseWindow(wxCloseEvent& event) {
	if (IsModal()) EndModal(wxID_CANCEL);
	else Destroy();
}

void DungeonPresetEditorDialog::OnLoadGroundBrushGeneral(wxCommandEvent& event) {
	long sel = m_generalGroundBrushList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0) {
		wxMessageBox("Select a ground brush first.", "Info", wxICON_INFORMATION | wxOK, this);
		return;
	}
	wxString selected = m_generalGroundBrushList->GetItemText(sel);

	Brush* brush = g_brushes.getBrush(selected.ToStdString());
	if (!brush) return;

	GroundBrush* groundBrush = brush->as<GroundBrush>();
	if (!groundBrush) return;

	uint16_t groundItemId = groundBrush->getFirstGroundItemId();
	if (groundItemId > 0) {
		m_groundField->SetItemId(groundItemId);
	}
}

wxTextCtrl* DungeonPresetEditorDialog::CreateSearchField(wxWindow* parent) {
	wxTextCtrl* search = new wxTextCtrl(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
	                                     wxTE_PROCESS_ENTER);
	search->SetHint("Search...");

	// Disable hotkeys when search field has focus
	search->Bind(wxEVT_SET_FOCUS, [](wxFocusEvent& evt) {
		g_hotkeys.DisableHotkeys();
		evt.Skip();
	});
	search->Bind(wxEVT_KILL_FOCUS, [](wxFocusEvent& evt) {
		g_hotkeys.EnableHotkeys();
		evt.Skip();
	});

	return search;
}

void DungeonPresetEditorDialog::RebuildGeneralGroundBrushList() {
	if (!m_generalGroundBrushList || !m_generalGroundBrushImageList) return;

	wxString filter = m_generalGroundSearch ? m_generalGroundSearch->GetValue().Lower() : wxString();

	m_generalGroundBrushList->ClearAll();
	m_generalGroundBrushImageList->RemoveAll();
	m_generalGroundBrushList->InsertColumn(0, "Name");

	std::vector<std::pair<std::string, uint16_t>> brushes;
	for (const auto& [name, brush] : g_brushes.getMap()) {
		if (auto* gb = brush->as<GroundBrush>()) {
			if (!filter.empty() && wxString(name).Lower().Find(filter) == wxNOT_FOUND) continue;
			brushes.push_back({name, gb->getFirstGroundItemId()});
		}
	}
	std::sort(brushes.begin(), brushes.end());

	int idx = 0;
	for (const auto& [name, previewId] : brushes) {
		m_generalGroundBrushImageList->Add(MakeItemBitmap(previewId, PREVIEW_SIZE));
		m_generalGroundBrushList->InsertItem(idx, name, idx);
		++idx;
	}
	m_generalGroundBrushList->SetColumnWidth(0, wxLIST_AUTOSIZE);
}

void DungeonPresetEditorDialog::RebuildWallBrushList() {
	if (!m_wallBrushList || !m_wallBrushImageList) return;

	wxString filter = m_wallBrushSearch ? m_wallBrushSearch->GetValue().Lower() : wxString();

	m_wallBrushList->ClearAll();
	m_wallBrushImageList->RemoveAll();
	m_wallBrushList->InsertColumn(0, "Name");

	std::vector<std::pair<std::string, uint16_t>> brushes;
	for (const auto& [name, brush] : g_brushes.getMap()) {
		if (auto* wb = brush->as<WallBrush>()) {
			if (!filter.empty() && wxString(name).Lower().Find(filter) == wxNOT_FOUND) continue;
			uint16_t previewId = wb->items.getRandomWallId(WALL_HORIZONTAL);
			if (previewId == 0) previewId = wb->items.getRandomWallId(WALL_VERTICAL);
			brushes.push_back({name, previewId});
		}
	}
	std::sort(brushes.begin(), brushes.end());

	int idx = 0;
	for (const auto& [name, previewId] : brushes) {
		m_wallBrushImageList->Add(MakeItemBitmap(previewId, PREVIEW_SIZE));
		m_wallBrushList->InsertItem(idx, name, idx);
		++idx;
	}
	m_wallBrushList->SetColumnWidth(0, wxLIST_AUTOSIZE);
}

void DungeonPresetEditorDialog::RebuildDoodadBrushList() {
	if (!m_doodadBrushList || !m_doodadBrushImageList) return;

	wxString filter = m_doodadBrushSearch ? m_doodadBrushSearch->GetValue().Lower() : wxString();

	m_doodadBrushList->ClearAll();
	m_doodadBrushImageList->RemoveAll();
	m_doodadBrushList->InsertColumn(0, "Name");

	std::vector<std::pair<std::string, uint16_t>> brushes;
	for (const auto& [name, brush] : g_brushes.getMap()) {
		if (auto* db = brush->as<DoodadBrush>()) {
			if (!filter.empty() && wxString(name).Lower().Find(filter) == wxNOT_FOUND) continue;
			uint16_t lookId = 0;
			const auto& alts = db->getItems().getAlternatives();
			if (!alts.empty() && !alts[0]->single_items.empty() && alts[0]->single_items[0].item) {
				lookId = alts[0]->single_items[0].item->getID();
			}
			brushes.push_back({name, lookId});
		}
	}
	std::sort(brushes.begin(), brushes.end());

	int idx = 0;
	for (const auto& [name, previewId] : brushes) {
		m_doodadBrushImageList->Add(MakeItemBitmap(previewId, PREVIEW_SIZE));
		m_doodadBrushList->InsertItem(idx, name, idx);
		++idx;
	}
	m_doodadBrushList->SetColumnWidth(0, wxLIST_AUTOSIZE);
}

void DungeonPresetEditorDialog::UpdateBorderTarget(BorderTarget target) {
	m_borderTarget = target;

	// Update highlights
	m_patchBordersGrid->SetHighlighted(target == TARGET_PATCH_BORDER);
	m_brushBordersGrid->SetHighlighted(target == TARGET_BRUSH_BORDER);

	// Update label
	switch (target) {
		case TARGET_PATCH_BORDER: m_borderTargetLabel->SetLabel("Target: Patch Borders"); break;
		case TARGET_BRUSH_BORDER: m_borderTargetLabel->SetLabel("Target: Brush Borders"); break;
		case TARGET_PATCH_TERRAIN: m_borderTargetLabel->SetLabel("Target: Patch Terrain"); break;
		case TARGET_BRUSH_TERRAIN: m_borderTargetLabel->SetLabel("Target: Brush Terrain"); break;
	}

	// Rebuild list with different previews
	RebuildGroundBrushList();
}

void DungeonPresetEditorDialog::RebuildGroundBrushList() {
	if (!m_groundBrushList || !m_groundBrushImageList) return;

	wxString filter = m_groundBrushSearch ? m_groundBrushSearch->GetValue().Lower() : wxString();

	m_groundBrushList->ClearAll();
	m_groundBrushImageList->RemoveAll();
	m_groundBrushList->InsertColumn(0, "Name");

	bool showGroundPreview = (m_borderTarget == TARGET_PATCH_TERRAIN || m_borderTarget == TARGET_BRUSH_TERRAIN);

	std::vector<std::pair<std::string, uint16_t>> brushes;
	for (const auto& [name, brush] : g_brushes.getMap()) {
		if (auto* gb = brush->as<GroundBrush>()) {
			if (!filter.empty() && wxString(name).Lower().Find(filter) == wxNOT_FOUND) continue;
			if (showGroundPreview) {
				// Show all grounds with ground tile preview
				uint16_t previewId = gb->getFirstGroundItemId();
				if (previewId > 0) {
					brushes.push_back({name, previewId});
				}
			} else {
				// Show only brushes with borders, using border tile preview
				if (gb->hasOuterBorder() || gb->hasInnerBorder()) {
					const AutoBorder* ab = gb->getFirstOuterAutoBorder();
					uint16_t previewId = 0;
					if (ab) {
						previewId = static_cast<uint16_t>(ab->tiles[NORTH_HORIZONTAL]);
						if (previewId == 0) previewId = static_cast<uint16_t>(ab->tiles[EAST_HORIZONTAL]);
					}
					if (previewId == 0) previewId = gb->getFirstGroundItemId();
					brushes.push_back({name, previewId});
				}
			}
		}
	}
	std::sort(brushes.begin(), brushes.end());

	int idx = 0;
	for (const auto& [name, previewId] : brushes) {
		m_groundBrushImageList->Add(MakeItemBitmap(previewId, PREVIEW_SIZE));
		m_groundBrushList->InsertItem(idx, name, idx);
		++idx;
	}
	m_groundBrushList->SetColumnWidth(0, wxLIST_AUTOSIZE);
}

void DungeonPresetEditorDialog::OnLoadGroundBrush(wxCommandEvent& event) {
	long sel = m_groundBrushList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0) {
		wxMessageBox("Select a ground brush first.", "Info", wxICON_INFORMATION | wxOK, this);
		return;
	}
	wxString selected = m_groundBrushList->GetItemText(sel);

	Brush* brush = g_brushes.getBrush(selected.ToStdString());
	if (!brush) return;

	GroundBrush* groundBrush = brush->as<GroundBrush>();
	if (!groundBrush) return;

	if (m_borderTarget == TARGET_PATCH_TERRAIN || m_borderTarget == TARGET_BRUSH_TERRAIN) {
		// Apply ground tile to the Patch or Brush terrain field
		uint16_t groundItemId = groundBrush->getFirstGroundItemId();
		if (groundItemId > 0) {
			if (m_borderTarget == TARGET_PATCH_TERRAIN) {
				m_patchField->SetItemId(groundItemId);
			} else {
				m_brushField->SetItemId(groundItemId);
			}
		}
	} else {
		// Apply borders to the selected border grid
		DungeonSlotGridPanel* targetGrid = (m_borderTarget == TARGET_PATCH_BORDER)
			? m_patchBordersGrid : m_brushBordersGrid;

		const AutoBorder* ab = groundBrush->getFirstOuterAutoBorder();
		if (ab) {
			// Straights: AutoBorder N/S/E/W are swapped relative to dungeon convention
			// AutoBorder "n" = piece on north side of terrain = dungeon "south" border
			targetGrid->SetItemId(DSLOT_N, static_cast<uint16_t>(ab->tiles[SOUTH_HORIZONTAL]));
			targetGrid->SetItemId(DSLOT_S, static_cast<uint16_t>(ab->tiles[NORTH_HORIZONTAL]));
			targetGrid->SetItemId(DSLOT_E, static_cast<uint16_t>(ab->tiles[WEST_HORIZONTAL]));
			targetGrid->SetItemId(DSLOT_W, static_cast<uint16_t>(ab->tiles[EAST_HORIZONTAL]));
			// Corners: same swap logic
			targetGrid->SetItemId(DSLOT_NW, static_cast<uint16_t>(ab->tiles[SOUTHEAST_DIAGONAL]));
			targetGrid->SetItemId(DSLOT_NE, static_cast<uint16_t>(ab->tiles[SOUTHWEST_DIAGONAL]));
			targetGrid->SetItemId(DSLOT_SW, static_cast<uint16_t>(ab->tiles[NORTHEAST_DIAGONAL]));
			targetGrid->SetItemId(DSLOT_SE, static_cast<uint16_t>(ab->tiles[NORTHWEST_DIAGONAL]));
			targetGrid->SetItemId(DSLOT_INNER_NW, static_cast<uint16_t>(ab->tiles[SOUTHEAST_CORNER]));
			targetGrid->SetItemId(DSLOT_INNER_NE, static_cast<uint16_t>(ab->tiles[SOUTHWEST_CORNER]));
			targetGrid->SetItemId(DSLOT_INNER_SW, static_cast<uint16_t>(ab->tiles[NORTHEAST_CORNER]));
			targetGrid->SetItemId(DSLOT_INNER_SE, static_cast<uint16_t>(ab->tiles[NORTHWEST_CORNER]));
		}
	}
}

void DungeonPresetEditorDialog::LoadWallBrushIntoConfig(DungeonGen::WallConfig& config, DungeonSlotGridPanel* grid) {
	long sel = m_wallBrushList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0) {
		wxMessageBox("Select a wall brush first.", "Info", wxICON_INFORMATION | wxOK, this);
		return;
	}
	wxString selected = m_wallBrushList->GetItemText(sel);

	Brush* brush = g_brushes.getBrush(selected.ToStdString());
	if (!brush) return;

	WallBrush* wallBrush = brush->as<WallBrush>();
	if (!wallBrush) return;

	// Helper: load all items from a wall node (with chances/variations)
	auto loadAll = [&](int alignment) -> std::vector<DungeonGen::WallItem> {
		std::vector<DungeonGen::WallItem> result;
		const auto& node = wallBrush->items.getWallNode(alignment);
		for (const auto& item : node.items) {
			result.push_back({item.id, item.chance});
		}
		return result;
	};

	auto getFirst = [&](int alignment) -> uint16_t {
		const auto& node = wallBrush->items.getWallNode(alignment);
		if (!node.items.empty()) return node.items[0].id;
		return 0;
	};

	// Load all variations into config
	config.north = loadAll(WALL_HORIZONTAL);
	config.south = loadAll(WALL_HORIZONTAL);
	config.east = loadAll(WALL_VERTICAL);
	config.west = loadAll(WALL_VERTICAL);
	config.nw = loadAll(WALL_NORTHWEST_DIAGONAL);
	config.ne = loadAll(WALL_NORTHEAST_DIAGONAL);
	config.sw = loadAll(WALL_SOUTHWEST_DIAGONAL);
	config.se = loadAll(WALL_SOUTHEAST_DIAGONAL);
	config.pillar = loadAll(WALL_POLE);

	// Update grid (shows primary only)
	grid->SetItemId(DSLOT_N, getFirst(WALL_HORIZONTAL));
	grid->SetItemId(DSLOT_S, getFirst(WALL_HORIZONTAL));
	grid->SetItemId(DSLOT_E, getFirst(WALL_VERTICAL));
	grid->SetItemId(DSLOT_W, getFirst(WALL_VERTICAL));
	grid->SetItemId(DSLOT_NW, getFirst(WALL_NORTHWEST_DIAGONAL));
	grid->SetItemId(DSLOT_NE, getFirst(WALL_NORTHEAST_DIAGONAL));
	grid->SetItemId(DSLOT_SW, getFirst(WALL_SOUTHWEST_DIAGONAL));
	grid->SetItemId(DSLOT_SE, getFirst(WALL_SOUTHEAST_DIAGONAL));
	grid->SetItemId(DSLOT_PILLAR, getFirst(WALL_POLE));

	int totalVariations = 0;
	for (const auto* items : {&config.north, &config.south, &config.east, &config.west,
	                           &config.nw, &config.ne, &config.sw, &config.se, &config.pillar}) {
		totalVariations += static_cast<int>(items->size());
	}

	wxMessageBox(wxString::Format("Loaded wall brush '%s' (%d total tile variations).",
	             selected, totalVariations), "Loaded", wxICON_INFORMATION | wxOK, this);
}

void DungeonPresetEditorDialog::OnLoadWallBrush(wxCommandEvent& event) {
	if (m_wallTarget == WALL_TARGET_CORRIDOR) {
		LoadWallBrushIntoConfig(m_preset.corridorWalls, m_corridorWallsGrid);
	} else {
		LoadWallBrushIntoConfig(m_preset.walls, m_wallsGrid);
	}
}

void DungeonPresetEditorDialog::UpdateWallTarget(WallTarget target) {
	m_wallTarget = target;
	switch (target) {
		case WALL_TARGET_ROOM:
			m_wallTargetLabel->SetLabel("Target: Room Walls");
			m_wallsGrid->SetHighlighted(true);
			m_corridorWallsGrid->SetHighlighted(false);
			break;
		case WALL_TARGET_CORRIDOR:
			m_wallTargetLabel->SetLabel("Target: Corridor Walls");
			m_wallsGrid->SetHighlighted(false);
			m_corridorWallsGrid->SetHighlighted(true);
			break;
	}
}

//=============================================================================
// Save preset
//=============================================================================

//=============================================================================
// Floor variation helpers
//=============================================================================

void DungeonPresetEditorDialog::RefreshFloorList(wxListCtrl* list, wxImageList* imgList,
                                                  const std::vector<DungeonGen::WallItem>& items) {
	list->ClearAll();
	imgList->RemoveAll();
	list->InsertColumn(0, "Item");
	list->InsertColumn(1, "Chance");

	for (size_t i = 0; i < items.size(); ++i) {
		imgList->Add(MakeItemBitmap(items[i].id, PREVIEW_SIZE));
		long idx = list->InsertItem(static_cast<long>(i), wxString::Format("%d", items[i].id), static_cast<int>(i));
		list->SetItem(idx, 1, wxString::Format("%d", items[i].chance));
	}

	list->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER);
	list->SetColumnWidth(1, 60);
}

void DungeonPresetEditorDialog::AddFloorRange(std::vector<DungeonGen::WallItem>& items,
                                                wxListCtrl* list, wxImageList* imgList) {
	wxDialog dlg(this, wxID_ANY, "Add Item Range", wxDefaultPosition, wxDefaultSize,
	             wxCAPTION | wxCLOSE_BOX | wxSTAY_ON_TOP);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* row1 = new wxBoxSizer(wxHORIZONTAL);
	row1->Add(new wxStaticText(&dlg, wxID_ANY, "From ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	wxSpinCtrl* fromSpin = new wxSpinCtrl(&dlg, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1),
	                                       wxSP_ARROW_KEYS, 1, 65535, 1);
	row1->Add(fromSpin, 0);
	row1->Add(new wxStaticText(&dlg, wxID_ANY, "  To ID:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
	wxSpinCtrl* toSpin = new wxSpinCtrl(&dlg, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1),
	                                     wxSP_ARROW_KEYS, 1, 65535, 1);
	row1->Add(toSpin, 0);
	sizer->Add(row1, 0, wxALL, 10);

	wxBoxSizer* row2 = new wxBoxSizer(wxHORIZONTAL);
	row2->Add(new wxStaticText(&dlg, wxID_ANY, "Chance:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	wxSpinCtrl* chanceSpin = new wxSpinCtrl(&dlg, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1),
	                                         wxSP_ARROW_KEYS, 1, 10000, 100);
	row2->Add(chanceSpin, 0);
	sizer->Add(row2, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

	wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
	btnRow->Add(new wxButton(&dlg, wxID_OK, "Add"), 0, wxRIGHT, 5);
	btnRow->Add(new wxButton(&dlg, wxID_CANCEL, "Cancel"), 0);
	sizer->Add(btnRow, 0, wxALL | wxALIGN_RIGHT, 10);

	dlg.SetSizerAndFit(sizer);
	dlg.CentreOnParent();

	if (dlg.ShowModal() == wxID_OK) {
		int from = fromSpin->GetValue();
		int to = toSpin->GetValue();
		int chance = chanceSpin->GetValue();

		if (from > to) std::swap(from, to);

		int count = 0;
		for (int id = from; id <= to; ++id) {
			items.push_back({static_cast<uint16_t>(id), chance});
			++count;
		}

		if (count > 0) {
			RefreshFloorList(list, imgList, items);
		}
	}
}

void DungeonPresetEditorDialog::OnAddRoomFloor(wxCommandEvent& event) {
	FindItemDialog dlg(this, "Pick Floor Item");
	if (dlg.ShowModal() == wxID_OK) {
		uint16_t id = dlg.getResultID();
		if (id > 0) {
			m_editRoomFloors.push_back({id, 100});
			RefreshFloorList(m_roomFloorList, m_roomFloorImageList, m_editRoomFloors);
		}
	}
}

void DungeonPresetEditorDialog::OnAddRoomFloorRange(wxCommandEvent& event) {
	AddFloorRange(m_editRoomFloors, m_roomFloorList, m_roomFloorImageList);
}

void DungeonPresetEditorDialog::OnRemoveRoomFloor(wxCommandEvent& event) {
	long sel = m_roomFloorList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel >= 0 && sel < static_cast<long>(m_editRoomFloors.size())) {
		m_editRoomFloors.erase(m_editRoomFloors.begin() + sel);
		RefreshFloorList(m_roomFloorList, m_roomFloorImageList, m_editRoomFloors);
	}
}

void DungeonPresetEditorDialog::OnAddCorridorFloor(wxCommandEvent& event) {
	FindItemDialog dlg(this, "Pick Floor Item");
	if (dlg.ShowModal() == wxID_OK) {
		uint16_t id = dlg.getResultID();
		if (id > 0) {
			m_editCorridorFloors.push_back({id, 100});
			RefreshFloorList(m_corridorFloorList, m_corridorFloorImageList, m_editCorridorFloors);
		}
	}
}

void DungeonPresetEditorDialog::OnAddCorridorFloorRange(wxCommandEvent& event) {
	AddFloorRange(m_editCorridorFloors, m_corridorFloorList, m_corridorFloorImageList);
}

void DungeonPresetEditorDialog::OnRemoveCorridorFloor(wxCommandEvent& event) {
	long sel = m_corridorFloorList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel >= 0 && sel < static_cast<long>(m_editCorridorFloors.size())) {
		m_editCorridorFloors.erase(m_editCorridorFloors.begin() + sel);
		RefreshFloorList(m_corridorFloorList, m_corridorFloorImageList, m_editCorridorFloors);
	}
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

	// Walls (from grid panel) - sync primary IDs, preserve variations
	auto syncWallGrid = [](DungeonGen::WallConfig& cfg, DungeonSlotGridPanel* grid) {
		auto syncDir = [](std::vector<DungeonGen::WallItem>& items, uint16_t gridId) {
			if (gridId == 0) {
				items.clear();
			} else if (items.empty()) {
				items.push_back({gridId, 100});
			} else {
				items.front().id = gridId;
			}
		};
		syncDir(cfg.north, grid->GetItemId(DSLOT_N));
		syncDir(cfg.south, grid->GetItemId(DSLOT_S));
		syncDir(cfg.east, grid->GetItemId(DSLOT_E));
		syncDir(cfg.west, grid->GetItemId(DSLOT_W));
		syncDir(cfg.nw, grid->GetItemId(DSLOT_NW));
		syncDir(cfg.ne, grid->GetItemId(DSLOT_NE));
		syncDir(cfg.sw, grid->GetItemId(DSLOT_SW));
		syncDir(cfg.se, grid->GetItemId(DSLOT_SE));
		syncDir(cfg.pillar, grid->GetItemId(DSLOT_PILLAR));
	};

	syncWallGrid(m_preset.walls, m_wallsGrid);
	syncWallGrid(m_preset.corridorWalls, m_corridorWallsGrid);

	// Borders (from grid panel)
	m_preset.borders.north = m_patchBordersGrid->GetItemId(DSLOT_N);
	m_preset.borders.south = m_patchBordersGrid->GetItemId(DSLOT_S);
	m_preset.borders.east = m_patchBordersGrid->GetItemId(DSLOT_E);
	m_preset.borders.west = m_patchBordersGrid->GetItemId(DSLOT_W);
	m_preset.borders.nw = m_patchBordersGrid->GetItemId(DSLOT_NW);
	m_preset.borders.ne = m_patchBordersGrid->GetItemId(DSLOT_NE);
	m_preset.borders.sw = m_patchBordersGrid->GetItemId(DSLOT_SW);
	m_preset.borders.se = m_patchBordersGrid->GetItemId(DSLOT_SE);
	m_preset.borders.inner_nw = m_patchBordersGrid->GetItemId(DSLOT_INNER_NW);
	m_preset.borders.inner_ne = m_patchBordersGrid->GetItemId(DSLOT_INNER_NE);
	m_preset.borders.inner_sw = m_patchBordersGrid->GetItemId(DSLOT_INNER_SW);
	m_preset.borders.inner_se = m_patchBordersGrid->GetItemId(DSLOT_INNER_SE);

	// Brush Borders (from grid panel)
	m_preset.brushBorders.north = m_brushBordersGrid->GetItemId(DSLOT_N);
	m_preset.brushBorders.south = m_brushBordersGrid->GetItemId(DSLOT_S);
	m_preset.brushBorders.east = m_brushBordersGrid->GetItemId(DSLOT_E);
	m_preset.brushBorders.west = m_brushBordersGrid->GetItemId(DSLOT_W);
	m_preset.brushBorders.nw = m_brushBordersGrid->GetItemId(DSLOT_NW);
	m_preset.brushBorders.ne = m_brushBordersGrid->GetItemId(DSLOT_NE);
	m_preset.brushBorders.sw = m_brushBordersGrid->GetItemId(DSLOT_SW);
	m_preset.brushBorders.se = m_brushBordersGrid->GetItemId(DSLOT_SE);
	m_preset.brushBorders.inner_nw = m_brushBordersGrid->GetItemId(DSLOT_INNER_NW);
	m_preset.brushBorders.inner_ne = m_brushBordersGrid->GetItemId(DSLOT_INNER_NE);
	m_preset.brushBorders.inner_sw = m_brushBordersGrid->GetItemId(DSLOT_INNER_SW);
	m_preset.brushBorders.inner_se = m_brushBordersGrid->GetItemId(DSLOT_INNER_SE);

	// Details
	m_preset.details.clear();
	for (int i = 0; i < 6; ++i) {
		const auto& grp = m_editDetails[i];
		if (!grp.itemIds.empty() || grp.chance > 0.0f) {
			m_preset.details.push_back(grp);
		}
	}

	// Floor variations
	m_preset.roomFloors.items = m_editRoomFloors;
	m_preset.corridorFloors.items = m_editCorridorFloors;

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

	// Copy callback before potential Destroy
	auto callback = m_onSaveCallback;

	if (IsModal()) {
		EndModal(wxID_OK);
	} else {
		Destroy();
	}

	// Call after destroy so parent window refreshes with latest data
	if (callback) {
		callback();
	}
}
