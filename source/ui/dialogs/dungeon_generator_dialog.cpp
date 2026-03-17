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
#include "ui/dialogs/dungeon_generator_dialog.h"
#include "ui/dialogs/dungeon_preset_editor_dialog.h"
#include "editor/editor.h"
#include "editor/action_queue.h"
#include "editor/selection.h"
#include "ui/gui.h"
#include "map/map.h"
#include "map/tile.h"
#include "rendering/core/graphics.h"
#include "item_definitions/core/item_definition_store.h"
#include "ui/theme.h"

#include <wx/dcbuffer.h>
#include <wx/progdlg.h>

namespace {

const int ITEM_ICON_SIZE = 32;

enum {
	ID_PRESET_CHOICE = wxID_HIGHEST + 3000,
	ID_WIDTH_SPIN,
	ID_HEIGHT_SPIN,
	ID_ALGORITHM_CHOICE,
	ID_GENERATE_BTN,
	ID_NEW_PRESET_BTN,
	ID_EDIT_PRESET_BTN,
	ID_DUPLICATE_PRESET_BTN,
	ID_DELETE_PRESET_BTN,
	ID_REROLL_BTN,
	ID_REROLL_STRUCTURE_BTN,
	ID_REROLL_DETAILS_BTN,
	ID_SELECT_FROM_MAP,
	ID_USE_SELECTION,
};

wxBitmap CreateItemBitmap(uint16_t itemId, int size) {
	wxBitmap bmp(size, size, 32);
	wxMemoryDC dc(bmp);

	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	const auto itemDef = g_item_definitions.get(itemId);
	Sprite* spr = nullptr;
	if (itemDef) {
		spr = g_gui.gfx.getSprite(itemDef.clientId());
	}

	if (spr) {
		spr->DrawTo(&dc, SPRITE_SIZE_32x32, 0, 0, size, size);
	} else {
		dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Border)));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(2, 2, size - 4, size - 4);
		dc.SetTextForeground(Theme::Get(Theme::Role::Text));
		dc.DrawText("?", size / 2 - 4, size / 2 - 8);
	}

	dc.SelectObject(wxNullBitmap);
	return bmp;
}

} // anonymous namespace

wxBEGIN_EVENT_TABLE(DungeonGeneratorDialog, wxDialog)
	EVT_CHOICE(ID_PRESET_CHOICE, DungeonGeneratorDialog::OnPresetChanged)
	EVT_CHOICE(ID_ALGORITHM_CHOICE, DungeonGeneratorDialog::OnAlgorithmChanged)
	EVT_BUTTON(ID_GENERATE_BTN, DungeonGeneratorDialog::OnGenerate)
	EVT_BUTTON(ID_REROLL_BTN, DungeonGeneratorDialog::OnReroll)
	EVT_BUTTON(ID_REROLL_STRUCTURE_BTN, DungeonGeneratorDialog::OnRerollStructure)
	EVT_BUTTON(ID_REROLL_DETAILS_BTN, DungeonGeneratorDialog::OnRerollDetails)
	EVT_BUTTON(ID_SELECT_FROM_MAP, DungeonGeneratorDialog::OnSelectFromMap)
	EVT_BUTTON(ID_USE_SELECTION, DungeonGeneratorDialog::OnUseSelection)
	EVT_BUTTON(ID_NEW_PRESET_BTN, DungeonGeneratorDialog::OnNewPreset)
	EVT_BUTTON(ID_EDIT_PRESET_BTN, DungeonGeneratorDialog::OnEditPreset)
	EVT_BUTTON(ID_DUPLICATE_PRESET_BTN, DungeonGeneratorDialog::OnDuplicatePreset)
	EVT_BUTTON(ID_DELETE_PRESET_BTN, DungeonGeneratorDialog::OnDeletePreset)
	EVT_CLOSE(DungeonGeneratorDialog::OnClose)
wxEND_EVENT_TABLE()

DungeonGeneratorDialog::DungeonGeneratorDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "Dungeon Generator", wxDefaultPosition, wxSize(620, 750),
	           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_imageList(nullptr)
	, m_hasSelection(false)
	, m_pickStatusText(nullptr) {

	auto& presetMgr = DungeonGen::PresetManager::getInstance();
	if (presetMgr.getPresetNames().empty()) {
		presetMgr.loadPresets();
	}

	ReadSelectionBounds();

	CreateControls();
	PopulatePresetList();
	UpdatePreview();
	UpdateSelectionInfo();

	Centre();
}

bool DungeonGeneratorDialog::ReadSelectionBounds() {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		m_hasSelection = false;
		return false;
	}

	const Selection& sel = editor->selection;
	const auto& tiles = sel.getTiles();
	if (tiles.empty()) {
		m_hasSelection = false;
		return false;
	}

	Position minPos(99999, 99999, 7), maxPos(0, 0, 7);
	for (Tile* t : tiles) {
		Position p = t->getPosition();
		minPos.x = std::min(minPos.x, p.x);
		minPos.y = std::min(minPos.y, p.y);
		maxPos.x = std::max(maxPos.x, p.x);
		maxPos.y = std::max(maxPos.y, p.y);
		minPos.z = p.z;
	}

	m_config.width = maxPos.x - minPos.x + 1;
	m_config.height = maxPos.y - minPos.y + 1;
	m_config.center.x = (minPos.x + maxPos.x) / 2;
	m_config.center.y = (minPos.y + maxPos.y) / 2;
	m_config.center.z = minPos.z;
	m_hasSelection = true;
	return true;
}

void DungeonGeneratorDialog::UpdateSelectionInfo() {
	if (!m_selectionLabel) return;

	ReadSelectionBounds();

	if (m_hasSelection) {
		m_selectionLabel->SetLabel(wxString::Format(
			"Selection: %dx%d at (%d, %d, %d)",
			m_config.width, m_config.height,
			m_config.center.x, m_config.center.y, m_config.center.z));
		m_selectionLabel->SetForegroundColour(Theme::Get(Theme::Role::Success));
	} else {
		m_selectionLabel->SetLabel("No selection - please select an area on the map first");
		m_selectionLabel->SetForegroundColour(Theme::Get(Theme::Role::Error));
	}
}

DungeonGeneratorDialog::~DungeonGeneratorDialog() {
	delete m_imageList;
}

void DungeonGeneratorDialog::CreateControls() {
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// Area selection
	wxStaticBoxSizer* areaBox = new wxStaticBoxSizer(wxVERTICAL, this, "Generation Area");

	wxBoxSizer* areaBtnRow = new wxBoxSizer(wxHORIZONTAL);
	areaBtnRow->Add(new wxButton(this, ID_SELECT_FROM_MAP, "Select from Map..."), 0, wxRIGHT, 5);
	areaBtnRow->Add(new wxButton(this, ID_USE_SELECTION, "Use Current Selection"), 0);
	areaBox->Add(areaBtnRow, 0, wxALL, 5);

	m_pickStatusText = new wxStaticText(this, wxID_ANY, "");
	areaBox->Add(m_pickStatusText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

	m_selectionLabel = new wxStaticText(this, wxID_ANY, "");
	areaBox->Add(m_selectionLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

	mainSizer->Add(areaBox, 0, wxALL | wxEXPAND, 5);

	// Preset selection + management buttons
	wxStaticBoxSizer* presetBox = new wxStaticBoxSizer(wxVERTICAL, this, "Preset");

	m_presetChoice = new wxChoice(this, ID_PRESET_CHOICE);
	presetBox->Add(m_presetChoice, 0, wxALL | wxEXPAND, 3);

	wxBoxSizer* presetBtnRow = new wxBoxSizer(wxHORIZONTAL);
	presetBtnRow->Add(new wxButton(this, ID_NEW_PRESET_BTN, "New", wxDefaultPosition, wxSize(55, -1)), 0, wxRIGHT, 2);
	presetBtnRow->Add(new wxButton(this, ID_EDIT_PRESET_BTN, "Edit", wxDefaultPosition, wxSize(55, -1)), 0, wxRIGHT, 2);
	presetBtnRow->Add(new wxButton(this, ID_DUPLICATE_PRESET_BTN, "Copy", wxDefaultPosition, wxSize(55, -1)), 0, wxRIGHT, 2);
	presetBtnRow->AddStretchSpacer();
	presetBtnRow->Add(new wxButton(this, ID_DELETE_PRESET_BTN, "Del", wxDefaultPosition, wxSize(45, -1)), 0);
	presetBox->Add(presetBtnRow, 0, wxALL | wxEXPAND, 3);

	mainSizer->Add(presetBox, 0, wxALL | wxEXPAND, 5);

	// Algorithm selector + parameters + preview
	wxStaticBoxSizer* algoBox = new wxStaticBoxSizer(wxVERTICAL, this, "Algorithm");

	m_algorithmChoice = new wxChoice(this, ID_ALGORITHM_CHOICE);
	m_algorithmChoice->Append("Room Placement");
	m_algorithmChoice->Append("BSP");
	m_algorithmChoice->Append("Random Walk");
	m_algorithmChoice->SetSelection(0);
	algoBox->Add(m_algorithmChoice, 0, wxALL | wxEXPAND, 3);

	// Horizontal: params left, preview right
	wxBoxSizer* algoHSizer = new wxBoxSizer(wxHORIZONTAL);

	// Left: algorithm-specific parameter panels
	m_paramsSizer = new wxBoxSizer(wxVERTICAL);

	// --- Room Placement panel ---
	m_roomPlacementPanel = new wxPanel(this);
	wxFlexGridSizer* rpGrid = new wxFlexGridSizer(2, 4, 6);
	rpGrid->Add(new wxStaticText(m_roomPlacementPanel, wxID_ANY, "Rooms:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rpNumRooms = new wxSpinCtrl(m_roomPlacementPanel, wxID_ANY, "18", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 1, 500, 18);
	rpGrid->Add(m_rpNumRooms, 0);
	rpGrid->Add(new wxStaticText(m_roomPlacementPanel, wxID_ANY, "Min Size:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rpMinRoomSize = new wxSpinCtrl(m_roomPlacementPanel, wxID_ANY, "5", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 3, 200, 5);
	rpGrid->Add(m_rpMinRoomSize, 0);
	rpGrid->Add(new wxStaticText(m_roomPlacementPanel, wxID_ANY, "Max Size:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rpMaxRoomSize = new wxSpinCtrl(m_roomPlacementPanel, wxID_ANY, "11", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 3, 200, 11);
	rpGrid->Add(m_rpMaxRoomSize, 0);
	rpGrid->Add(new wxStaticText(m_roomPlacementPanel, wxID_ANY, "Path Width:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rpPathWidth = new wxSpinCtrl(m_roomPlacementPanel, wxID_ANY, "4", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 1, 100, 4);
	rpGrid->Add(m_rpPathWidth, 0);
	rpGrid->Add(new wxStaticText(m_roomPlacementPanel, wxID_ANY, "Min Distance:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rpMinDistance = new wxSpinCtrl(m_roomPlacementPanel, wxID_ANY, "8", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 1, 200, 8);
	rpGrid->Add(m_rpMinDistance, 0);
	m_rpUseMST = new wxCheckBox(m_roomPlacementPanel, wxID_ANY, "Use MST (better connections)");
	m_rpUseMST->SetValue(true);
	wxBoxSizer* rpSizer = new wxBoxSizer(wxVERTICAL);
	rpSizer->Add(rpGrid, 0, wxALL, 3);
	rpSizer->Add(m_rpUseMST, 0, wxALL, 3);
	m_roomPlacementPanel->SetSizer(rpSizer);

	// --- BSP panel ---
	m_bspPanel = new wxPanel(this);
	wxFlexGridSizer* bspGrid = new wxFlexGridSizer(2, 4, 6);
	bspGrid->Add(new wxStaticText(m_bspPanel, wxID_ANY, "Min Partition:"), 0, wxALIGN_CENTER_VERTICAL);
	m_bspMinPartition = new wxSpinCtrl(m_bspPanel, wxID_ANY, "12", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 4, 200, 12);
	bspGrid->Add(m_bspMinPartition, 0);
	bspGrid->Add(new wxStaticText(m_bspPanel, wxID_ANY, "Min Room:"), 0, wxALIGN_CENTER_VERTICAL);
	m_bspMinRoom = new wxSpinCtrl(m_bspPanel, wxID_ANY, "5", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 3, 200, 5);
	bspGrid->Add(m_bspMinRoom, 0);
	bspGrid->Add(new wxStaticText(m_bspPanel, wxID_ANY, "Padding:"), 0, wxALIGN_CENTER_VERTICAL);
	m_bspPadding = new wxSpinCtrl(m_bspPanel, wxID_ANY, "2", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 0, 50, 2);
	bspGrid->Add(m_bspPadding, 0);
	bspGrid->Add(new wxStaticText(m_bspPanel, wxID_ANY, "Corridor Width:"), 0, wxALIGN_CENTER_VERTICAL);
	m_bspCorridorWidth = new wxSpinCtrl(m_bspPanel, wxID_ANY, "3", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 1, 100, 3);
	bspGrid->Add(m_bspCorridorWidth, 0);
	bspGrid->Add(new wxStaticText(m_bspPanel, wxID_ANY, "Max Depth:"), 0, wxALIGN_CENTER_VERTICAL);
	m_bspMaxDepth = new wxSpinCtrl(m_bspPanel, wxID_ANY, "6", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 1, 20, 6);
	bspGrid->Add(m_bspMaxDepth, 0);
	m_bspPanel->SetSizer(bspGrid);

	// --- Random Walk panel ---
	m_randomWalkPanel = new wxPanel(this);
	wxFlexGridSizer* rwGrid = new wxFlexGridSizer(2, 4, 6);
	rwGrid->Add(new wxStaticText(m_randomWalkPanel, wxID_ANY, "Walkers:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rwWalkerCount = new wxSpinCtrl(m_randomWalkPanel, wxID_ANY, "3", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 1, 50, 3);
	rwGrid->Add(m_rwWalkerCount, 0);
	rwGrid->Add(new wxStaticText(m_randomWalkPanel, wxID_ANY, "Coverage:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rwCoverage = new wxSpinCtrlDouble(m_randomWalkPanel, wxID_ANY, "0.40", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 0.1, 0.8, 0.40, 0.05);
	m_rwCoverage->SetDigits(2);
	rwGrid->Add(m_rwCoverage, 0);
	rwGrid->Add(new wxStaticText(m_randomWalkPanel, wxID_ANY, "Turn Chance:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rwTurnChance = new wxSpinCtrlDouble(m_randomWalkPanel, wxID_ANY, "0.30", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 0.05, 0.9, 0.30, 0.05);
	m_rwTurnChance->SetDigits(2);
	rwGrid->Add(m_rwTurnChance, 0);
	rwGrid->Add(new wxStaticText(m_randomWalkPanel, wxID_ANY, "Room Chance %:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rwRoomChance = new wxSpinCtrl(m_randomWalkPanel, wxID_ANY, "10", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 0, 100, 10);
	rwGrid->Add(m_rwRoomChance, 0);
	rwGrid->Add(new wxStaticText(m_randomWalkPanel, wxID_ANY, "Min Room:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rwMinRoomSize = new wxSpinCtrl(m_randomWalkPanel, wxID_ANY, "3", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 2, 100, 3);
	rwGrid->Add(m_rwMinRoomSize, 0);
	rwGrid->Add(new wxStaticText(m_randomWalkPanel, wxID_ANY, "Max Room:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rwMaxRoomSize = new wxSpinCtrl(m_randomWalkPanel, wxID_ANY, "7", wxDefaultPosition, wxSize(65,-1), wxSP_ARROW_KEYS, 3, 100, 7);
	rwGrid->Add(m_rwMaxRoomSize, 0);
	m_randomWalkPanel->SetSizer(rwGrid);

	m_paramsSizer->Add(m_roomPlacementPanel, 0, wxEXPAND);
	m_paramsSizer->Add(m_bspPanel, 0, wxEXPAND);
	m_paramsSizer->Add(m_randomWalkPanel, 0, wxEXPAND);

	algoHSizer->Add(m_paramsSizer, 1, wxEXPAND);

	// Right: algorithm preview illustration
	m_algoPreviewPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(160, 150));
	m_algoPreviewPanel->SetMinSize(wxSize(160, 150));
	m_algoPreviewPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
	m_algoPreviewPanel->Bind(wxEVT_PAINT, &DungeonGeneratorDialog::OnAlgoPreviewPaint, this);
	algoHSizer->Add(m_algoPreviewPanel, 0, wxALL, 5);

	algoBox->Add(algoHSizer, 0, wxALL | wxEXPAND, 3);
	mainSizer->Add(algoBox, 0, wxALL | wxEXPAND, 5);

	// Show only the first panel initially
	ShowAlgorithmParams();

	// Preview notebook
	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);

	// Structure tab
	wxScrolledWindow* structPanel = new wxScrolledWindow(notebook, wxID_ANY);
	structPanel->SetScrollRate(0, 10);
	wxBoxSizer* structSizer = new wxBoxSizer(wxVERTICAL);

	m_imageList = new wxImageList(ITEM_ICON_SIZE, ITEM_ICON_SIZE, true);

	auto makeIconList = [&](wxWindow* parent, int height = 50) -> wxListCtrl* {
		wxListCtrl* list = new wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, height),
		                                   wxLC_ICON | wxLC_SINGLE_SEL);
		list->SetImageList(m_imageList, wxIMAGE_LIST_NORMAL);
		return list;
	};

	// Terrain
	structSizer->Add(new wxStaticText(structPanel, wxID_ANY, "Base Terrain"), 0, wxLEFT | wxTOP, 5);
	m_terrainList = makeIconList(structPanel);
	structSizer->Add(m_terrainList, 0, wxALL | wxEXPAND, 3);

	// Room Floors
	structSizer->Add(new wxStaticText(structPanel, wxID_ANY, "Room Floors"), 0, wxLEFT | wxTOP, 5);
	m_roomFloorsList = makeIconList(structPanel);
	structSizer->Add(m_roomFloorsList, 0, wxALL | wxEXPAND, 3);

	// Corridor Floors
	structSizer->Add(new wxStaticText(structPanel, wxID_ANY, "Corridor Floors"), 0, wxLEFT | wxTOP, 5);
	m_corridorFloorsList = makeIconList(structPanel);
	structSizer->Add(m_corridorFloorsList, 0, wxALL | wxEXPAND, 3);

	// Room Walls
	structSizer->Add(new wxStaticText(structPanel, wxID_ANY, "Room Walls"), 0, wxLEFT | wxTOP, 5);
	m_roomWallsList = makeIconList(structPanel);
	structSizer->Add(m_roomWallsList, 0, wxALL | wxEXPAND, 3);

	// Corridor Walls
	structSizer->Add(new wxStaticText(structPanel, wxID_ANY, "Corridor Walls"), 0, wxLEFT | wxTOP, 5);
	m_corridorWallsList = makeIconList(structPanel);
	structSizer->Add(m_corridorWallsList, 0, wxALL | wxEXPAND, 3);

	// Borders
	structSizer->Add(new wxStaticText(structPanel, wxID_ANY, "Borders"), 0, wxLEFT | wxTOP, 5);
	m_bordersList = makeIconList(structPanel);
	structSizer->Add(m_bordersList, 0, wxALL | wxEXPAND, 3);

	structPanel->SetSizer(structSizer);
	notebook->AddPage(structPanel, "Structure");

	// Decor tab
	wxPanel* decorPanel = new wxPanel(notebook);
	wxBoxSizer* decorSizer = new wxBoxSizer(wxVERTICAL);

	decorSizer->Add(new wxStaticText(decorPanel, wxID_ANY, "Floor Details"), 0, wxALL, 3);
	m_detailsList = new wxListCtrl(decorPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 80),
	                                wxLC_ICON | wxLC_SINGLE_SEL);
	m_detailsList->SetImageList(m_imageList, wxIMAGE_LIST_NORMAL);
	decorSizer->Add(m_detailsList, 1, wxALL | wxEXPAND, 3);

	decorSizer->Add(new wxStaticText(decorPanel, wxID_ANY, "Wall Items (Hangables)"), 0, wxALL, 3);
	m_hangablesList = new wxListCtrl(decorPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 80),
	                                  wxLC_ICON | wxLC_SINGLE_SEL);
	m_hangablesList->SetImageList(m_imageList, wxIMAGE_LIST_NORMAL);
	decorSizer->Add(m_hangablesList, 1, wxALL | wxEXPAND, 3);

	decorPanel->SetSizer(decorSizer);
	notebook->AddPage(decorPanel, "Decor");

	mainSizer->Add(notebook, 1, wxALL | wxEXPAND, 5);

	// Action buttons
	wxBoxSizer* actionRow1 = new wxBoxSizer(wxHORIZONTAL);
	wxButton* generateBtn = new wxButton(this, ID_GENERATE_BTN, "Generate Dungeon",
	                                      wxDefaultPosition, wxSize(-1, 35));
	wxButton* rerollBtn = new wxButton(this, ID_REROLL_BTN, "Reroll All",
	                                    wxDefaultPosition, wxSize(-1, 35));
	rerollBtn->SetToolTip("Undo the last generation and regenerate everything with a new random seed");
	actionRow1->Add(generateBtn, 1, wxRIGHT, 3);
	actionRow1->Add(rerollBtn, 1);
	mainSizer->Add(actionRow1, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 5);

	wxBoxSizer* actionRow2 = new wxBoxSizer(wxHORIZONTAL);
	wxButton* rerollStructBtn = new wxButton(this, ID_REROLL_STRUCTURE_BTN, "Reroll Structure",
	                                          wxDefaultPosition, wxSize(-1, 30));
	rerollStructBtn->SetToolTip("Undo and regenerate only the layout, floors and walls (keeps no details)");
	wxButton* rerollDetailBtn = new wxButton(this, ID_REROLL_DETAILS_BTN, "Reroll Details",
	                                          wxDefaultPosition, wxSize(-1, 30));
	rerollDetailBtn->SetToolTip("Undo and regenerate only patches, decorations and wall items (same structure)");
	actionRow2->Add(rerollStructBtn, 1, wxRIGHT, 3);
	actionRow2->Add(rerollDetailBtn, 1);
	mainSizer->Add(actionRow2, 0, wxALL | wxEXPAND, 5);

	SetSizer(mainSizer);
}

void DungeonGeneratorDialog::PopulatePresetList() {
	m_presetChoice->Clear();

	auto& presetMgr = DungeonGen::PresetManager::getInstance();
	auto names = presetMgr.getPresetNames();

	for (const auto& name : names) {
		m_presetChoice->Append(name);
	}

	if (!names.empty()) {
		int idx = m_presetChoice->FindString(m_config.presetName);
		if (idx == wxNOT_FOUND) idx = 0;
		m_presetChoice->SetSelection(idx);
		m_config.presetName = m_presetChoice->GetStringSelection().ToStdString();
	}
}

void DungeonGeneratorDialog::UpdatePreview() {
	m_terrainList->ClearAll();
	m_roomFloorsList->ClearAll();
	m_corridorFloorsList->ClearAll();
	m_roomWallsList->ClearAll();
	m_corridorWallsList->ClearAll();
	m_bordersList->ClearAll();
	m_detailsList->ClearAll();
	m_hangablesList->ClearAll();
	m_imageList->RemoveAll();

	auto& presetMgr = DungeonGen::PresetManager::getInstance();
	const DungeonGen::DungeonPreset* preset = presetMgr.getPreset(m_config.presetName);
	if (!preset) return;

	int imgIdx = 0;
	auto addItem = [&](wxListCtrl* list, uint16_t id, const wxString& label) {
		if (id == 0) return;
		m_imageList->Add(CreateItemBitmap(id, ITEM_ICON_SIZE));
		list->InsertItem(list->GetItemCount(), label, imgIdx++);
	};

	auto addWallItems = [&](wxListCtrl* list, const std::vector<DungeonGen::WallItem>& items, const wxString& label) {
		if (items.empty()) return;
		addItem(list, items.front().id, label);
		for (size_t i = 1; i < items.size(); ++i) {
			addItem(list, items[i].id, label + wxString::Format("+%zu", i));
		}
	};

	// Base Terrain
	addItem(m_terrainList, preset->groundId, "Ground");
	addItem(m_terrainList, preset->patchId, "Patch");
	addItem(m_terrainList, preset->fillId, "Fill");
	addItem(m_terrainList, preset->brushId, "Brush");

	// Room Floors
	for (size_t i = 0; i < preset->roomFloors.items.size(); ++i) {
		addItem(m_roomFloorsList, preset->roomFloors.items[i].id, wxString::Format("%d", preset->roomFloors.items[i].id));
	}

	// Corridor Floors
	for (size_t i = 0; i < preset->corridorFloors.items.size(); ++i) {
		addItem(m_corridorFloorsList, preset->corridorFloors.items[i].id, wxString::Format("%d", preset->corridorFloors.items[i].id));
	}

	// Room Walls
	const auto& w = preset->walls;
	addWallItems(m_roomWallsList, w.north, "N");
	addWallItems(m_roomWallsList, w.south, "S");
	addWallItems(m_roomWallsList, w.east, "E");
	addWallItems(m_roomWallsList, w.west, "W");
	addWallItems(m_roomWallsList, w.pillar, "Pillar");
	addWallItems(m_roomWallsList, w.nw, "NW");
	addWallItems(m_roomWallsList, w.ne, "NE");
	addWallItems(m_roomWallsList, w.sw, "SW");
	addWallItems(m_roomWallsList, w.se, "SE");

	// Corridor Walls
	const auto& cw = preset->corridorWalls;
	if (cw.isValid()) {
		addWallItems(m_corridorWallsList, cw.north, "N");
		addWallItems(m_corridorWallsList, cw.south, "S");
		addWallItems(m_corridorWallsList, cw.east, "E");
		addWallItems(m_corridorWallsList, cw.west, "W");
		addWallItems(m_corridorWallsList, cw.pillar, "Pillar");
		addWallItems(m_corridorWallsList, cw.nw, "NW");
		addWallItems(m_corridorWallsList, cw.ne, "NE");
		addWallItems(m_corridorWallsList, cw.sw, "SW");
		addWallItems(m_corridorWallsList, cw.se, "SE");
	}

	// Borders
	const auto& b = preset->borders;
	addItem(m_bordersList, b.north, "N");
	addItem(m_bordersList, b.south, "S");
	addItem(m_bordersList, b.east, "E");
	addItem(m_bordersList, b.west, "W");
	addItem(m_bordersList, b.nw, "NW");
	addItem(m_bordersList, b.ne, "NE");
	addItem(m_bordersList, b.sw, "SW");
	addItem(m_bordersList, b.se, "SE");

	// Details
	for (const auto& group : preset->details) {
		for (uint16_t id : group.itemIds) {
			addItem(m_detailsList, id, wxString::Format("%.0f%%", group.chance * 100));
		}
	}

	// Hangables
	for (uint16_t id : preset->hangables.horizontalIds) {
		addItem(m_hangablesList, id, "H");
	}
	for (uint16_t id : preset->hangables.verticalIds) {
		addItem(m_hangablesList, id, "V");
	}
}

void DungeonGeneratorDialog::RefreshPresetControls() {
	PopulatePresetList();
	UpdatePreview();
}

void DungeonGeneratorDialog::OnPresetChanged(wxCommandEvent& event) {
	m_config.presetName = m_presetChoice->GetStringSelection().ToStdString();
	UpdatePreview();
}

//=============================================================================
// Preset management buttons
//=============================================================================

void DungeonGeneratorDialog::OnNewPreset(wxCommandEvent& event) {
	auto* dlg = new DungeonPresetEditorDialog(this, "", [this]() {
		RefreshPresetControls();
	});
	dlg->Show();
}

void DungeonGeneratorDialog::OnEditPreset(wxCommandEvent& event) {
	wxString currentPreset = m_presetChoice->GetStringSelection();
	if (currentPreset.empty()) return;

	auto* dlg = new DungeonPresetEditorDialog(this, currentPreset, [this]() {
		RefreshPresetControls();
	});
	dlg->Show();
}

void DungeonGeneratorDialog::OnDuplicatePreset(wxCommandEvent& event) {
	wxString currentPreset = m_presetChoice->GetStringSelection();
	if (currentPreset.empty()) return;

	auto& mgr = DungeonGen::PresetManager::getInstance();
	const auto* existing = mgr.getPreset(currentPreset.ToStdString());
	if (!existing) return;

	// Generate unique name
	wxString newName = currentPreset + " (Copy)";
	int suffix = 2;
	while (mgr.getPreset(newName.ToStdString()) != nullptr) {
		newName = wxString::Format("%s (Copy %d)", currentPreset, suffix++);
	}

	DungeonGen::DungeonPreset copy = *existing;
	copy.name = newName.ToStdString();
	mgr.addPreset(copy);

	m_config.presetName = newName.ToStdString();
	RefreshPresetControls();
}

void DungeonGeneratorDialog::OnDeletePreset(wxCommandEvent& event) {
	auto& presetMgr = DungeonGen::PresetManager::getInstance();
	auto names = presetMgr.getPresetNames();

	if (names.size() <= 1) {
		wxMessageBox("Cannot delete the last preset.", "Error", wxICON_WARNING | wxOK, this);
		return;
	}

	wxString currentPreset = m_presetChoice->GetStringSelection();
	int confirm = wxMessageBox("Delete preset '" + currentPreset + "'?",
	                            "Confirm Delete", wxYES_NO | wxICON_QUESTION, this);
	if (confirm != wxYES) return;

	presetMgr.removePreset(currentPreset.ToStdString());
	m_config.presetName = presetMgr.getPresetNames().front();
	RefreshPresetControls();
}

//=============================================================================
// Generate
//=============================================================================

void DungeonGeneratorDialog::OnSelectFromMap(wxCommandEvent& event) {
	if (m_pickStatusText) {
		m_pickStatusText->SetForegroundColour(Theme::Get(Theme::Role::Accent));
		m_pickStatusText->SetLabel("Waiting for first corner click...");
	}

	g_gui.BeginRectanglePick(
		// On complete (second click)
		[this](const Position& first, const Position& second) {
			int minX = std::min(first.x, second.x);
			int minY = std::min(first.y, second.y);
			int maxX = std::max(first.x, second.x);
			int maxY = std::max(first.y, second.y);

			m_config.width = maxX - minX + 1;
			m_config.height = maxY - minY + 1;
			m_config.center.x = (minX + maxX) / 2;
			m_config.center.y = (minY + maxY) / 2;
			m_config.center.z = first.z;
			m_hasSelection = true;

			if (m_pickStatusText) {
				m_pickStatusText->SetForegroundColour(Theme::Get(Theme::Role::Success));
				m_pickStatusText->SetLabel(wxString::Format("Selection complete: (%d,%d,Z%d) to (%d,%d,Z%d)",
					first.x, first.y, first.z, second.x, second.y, second.z));
			}

			UpdateSelectionInfo();
		},
		// On cancel
		[this]() {
			if (m_pickStatusText) {
				m_pickStatusText->SetForegroundColour(Theme::Get(Theme::Role::Error));
				m_pickStatusText->SetLabel("Selection cancelled");
			}
		},
		// On first click
		[this](const Position& first) {
			if (m_pickStatusText) {
				m_pickStatusText->SetForegroundColour(Theme::Get(Theme::Role::Warning));
				m_pickStatusText->SetLabel(wxString::Format("First corner: (%d, %d, Z%d) - Click second corner...",
					first.x, first.y, first.z));
			}
		}
	);
}

void DungeonGeneratorDialog::OnUseSelection(wxCommandEvent& event) {
	if (ReadSelectionBounds()) {
		if (m_pickStatusText) {
			m_pickStatusText->SetForegroundColour(Theme::Get(Theme::Role::Success));
			m_pickStatusText->SetLabel("Using current editor selection");
		}
		UpdateSelectionInfo();
	} else {
		if (m_pickStatusText) {
			m_pickStatusText->SetForegroundColour(Theme::Get(Theme::Role::Error));
			m_pickStatusText->SetLabel("No tiles selected in the editor");
		}
		UpdateSelectionInfo();
	}
}

void DungeonGeneratorDialog::OnAlgorithmChanged(wxCommandEvent& event) {
	ShowAlgorithmParams();
}

void DungeonGeneratorDialog::ShowAlgorithmParams() {
	int sel = m_algorithmChoice->GetSelection();
	m_roomPlacementPanel->Show(sel == 0);
	m_bspPanel->Show(sel == 1);
	m_randomWalkPanel->Show(sel == 2);
	m_paramsSizer->Layout();
	if (m_algoPreviewPanel) m_algoPreviewPanel->Refresh();
	Layout();
}

void DungeonGeneratorDialog::OnGenerate(wxCommandEvent& event) {
	DoGenerate();
}

void DungeonGeneratorDialog::OnReroll(wxCommandEvent& event) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) return;

	if (m_lastGenerateSuccess) {
		editor->actionQueue->undo();
		g_gui.RefreshView();
		m_lastGenerateSuccess = false;
	}

	DoGenerate();
}

void DungeonGeneratorDialog::OnRerollStructure(wxCommandEvent& event) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) return;

	if (m_lastGenerateSuccess) {
		editor->actionQueue->undo();
		g_gui.RefreshView();
		m_lastGenerateSuccess = false;
	}

	DoGenerate();
}

void DungeonGeneratorDialog::OnRerollDetails(wxCommandEvent& event) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) return;

	if (m_lastStructureSeed == 0) {
		wxMessageBox("Generate a dungeon first before rerolling details.",
		             "No Structure", wxICON_WARNING | wxOK, this);
		return;
	}

	if (m_lastGenerateSuccess) {
		editor->actionQueue->undo();
		g_gui.RefreshView();
		m_lastGenerateSuccess = false;
	}

	// Regenerate with same structure seed — layout will be identical,
	// but details (patches, decorations, hangables) will vary because
	// they come after the structure phase which consumes a fixed amount of RNG
	DoGenerate(true);
}

void DungeonGeneratorDialog::DoGenerate(bool rerollDetailsOnly) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		wxMessageBox("No map open!", "Error", wxICON_ERROR | wxOK, this);
		return;
	}

	UpdateSelectionInfo();
	if (!m_hasSelection) {
		wxMessageBox("Please select an area on the map first.\n\nUse the selection tool to mark where the dungeon should be generated.",
		             "No Selection", wxICON_WARNING | wxOK, this);
		return;
	}

	m_config.presetName = m_presetChoice->GetStringSelection().ToStdString();

	// Read algorithm selection
	int algoSel = m_algorithmChoice->GetSelection();
	if (algoSel == 1) m_config.algorithm = DungeonGen::Algorithm::BSP;
	else if (algoSel == 2) m_config.algorithm = DungeonGen::Algorithm::RandomWalk;
	else m_config.algorithm = DungeonGen::Algorithm::RoomPlacement;

	// Read algorithm-specific params
	m_config.roomPlacement.numRooms = m_rpNumRooms->GetValue();
	m_config.roomPlacement.minRoomSize = m_rpMinRoomSize->GetValue();
	m_config.roomPlacement.maxRoomSize = m_rpMaxRoomSize->GetValue();
	m_config.roomPlacement.pathWidth = m_rpPathWidth->GetValue();
	m_config.roomPlacement.minRoomDistance = m_rpMinDistance->GetValue();
	m_config.roomPlacement.useMST = m_rpUseMST->GetValue();

	m_config.bsp.minPartitionSize = m_bspMinPartition->GetValue();
	m_config.bsp.minRoomSize = m_bspMinRoom->GetValue();
	m_config.bsp.roomPadding = m_bspPadding->GetValue();
	m_config.bsp.corridorWidth = m_bspCorridorWidth->GetValue();
	m_config.bsp.maxDepth = m_bspMaxDepth->GetValue();

	m_config.randomWalk.walkerCount = m_rwWalkerCount->GetValue();
	m_config.randomWalk.coverage = static_cast<float>(m_rwCoverage->GetValue());
	m_config.randomWalk.turnChance = static_cast<float>(m_rwTurnChance->GetValue());
	m_config.randomWalk.roomChance = m_rwRoomChance->GetValue();
	m_config.randomWalk.minRoomSize = m_rwMinRoomSize->GetValue();
	m_config.randomWalk.maxRoomSize = m_rwMaxRoomSize->GetValue();

	// Validate min/max room sizes
	if (m_config.algorithm == DungeonGen::Algorithm::RoomPlacement) {
		if (m_config.roomPlacement.minRoomSize > m_config.roomPlacement.maxRoomSize) {
			std::swap(m_config.roomPlacement.minRoomSize, m_config.roomPlacement.maxRoomSize);
		}
	} else if (m_config.algorithm == DungeonGen::Algorithm::RandomWalk) {
		if (m_config.randomWalk.minRoomSize > m_config.randomWalk.maxRoomSize) {
			std::swap(m_config.randomWalk.minRoomSize, m_config.randomWalk.maxRoomSize);
		}
	}

	// Seed: for reroll details, reuse the structure seed so layout is identical
	if (rerollDetailsOnly && m_lastStructureSeed != 0) {
		m_config.seed = m_lastStructureSeed;
	} else {
		m_config.seed = 0; // New random seed
	}

	auto& presetMgr = DungeonGen::PresetManager::getInstance();
	const DungeonGen::DungeonPreset* preset = presetMgr.getPreset(m_config.presetName);
	if (!preset) {
		wxMessageBox("Preset not found: " + m_config.presetName, "Error", wxICON_ERROR | wxOK, this);
		return;
	}

	DungeonGen::DungeonGenerator generator(editor);

	// Show progress dialog for large areas
	int area = m_config.width * m_config.height;
	wxGenericProgressDialog* progressDlg = nullptr;

	if (area > 5000) {
		progressDlg = new wxGenericProgressDialog(
			"Generating Dungeon",
			"Preparing...",
			100, this,
			wxPD_APP_MODAL | wxPD_SMOOTH | wxPD_AUTO_HIDE | wxPD_CAN_ABORT);
		progressDlg->Show();
	}

	// Disable action buttons during generation
	wxWindow* btn1 = FindWindowById(ID_GENERATE_BTN, this);
	wxWindow* btn2 = FindWindowById(ID_REROLL_BTN, this);
	wxWindow* btn3 = FindWindowById(ID_REROLL_STRUCTURE_BTN, this);
	wxWindow* btn4 = FindWindowById(ID_REROLL_DETAILS_BTN, this);
	if (btn1) btn1->Disable();
	if (btn2) btn2->Disable();
	if (btn3) btn3->Disable();
	if (btn4) btn4->Disable();

	bool cancelled = false;
	if (progressDlg) {
		generator.setProgressCallback([&](const std::string& step, int progress) -> bool {
			if (!progressDlg) return true;
			bool cont = progressDlg->Update(progress, step);
			wxSafeYield();
			if (!cont) {
				cancelled = true;
				return false;
			}
			return true;
		});
	}

	bool success = generator.generate(m_config, *preset);

	if (progressDlg) {
		progressDlg->Destroy();
	}

	if (btn1) btn1->Enable();
	if (btn2) btn2->Enable();
	if (btn3) btn3->Enable();
	if (btn4) btn4->Enable();

	if (success) {
		m_lastGenerateSuccess = true;
		// Save the structure seed for potential "Reroll Details" later
		if (!rerollDetailsOnly) {
			m_lastStructureSeed = generator.getLastSeed();
		}
		g_gui.RefreshView();
	} else if (cancelled) {
		m_lastGenerateSuccess = false;
	} else {
		m_lastGenerateSuccess = false;
		wxMessageBox("Generation failed: " + generator.getLastError(),
		             "Error", wxICON_ERROR | wxOK, this);
	}
}

void DungeonGeneratorDialog::OnAlgoPreviewPaint(wxPaintEvent& event) {
	wxAutoBufferedPaintDC dc(m_algoPreviewPanel);
	wxRect rect = m_algoPreviewPanel->GetClientRect();

	// Background
	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	int sel = m_algorithmChoice->GetSelection();
	int w = rect.GetWidth();
	int h = rect.GetHeight();
	int margin = 8;
	int iw = w - margin * 2;
	int ih = h - margin * 2 - 16; // reserve space for label

	// Label
	dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
	dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));

	wxString label;
	if (sel == 0) label = "Room Placement";
	else if (sel == 1) label = "BSP Partitioning";
	else label = "Random Walk";

	wxSize ts = dc.GetTextExtent(label);
	dc.DrawText(label, (w - ts.GetWidth()) / 2, 3);

	int ox = margin;
	int oy = margin + 14;

	// Border
	dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.DrawRectangle(ox, oy, iw, ih);

	if (sel == 0) {
		// Room Placement: random rectangles + connecting lines
		dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Success)));
		dc.SetPen(wxPen(Theme::Get(Theme::Role::Success)));

		struct MiniRoom { int x, y, w, h; };
		MiniRoom rooms[] = {
			{ox + 10, oy + 8, 28, 22},
			{ox + iw - 42, oy + 10, 32, 18},
			{ox + 15, oy + ih - 35, 25, 25},
			{ox + iw/2 - 12, oy + ih/2 - 10, 24, 20},
			{ox + iw - 38, oy + ih - 30, 28, 20},
			{ox + iw/2 + 15, oy + 12, 20, 16},
		};

		for (auto& r : rooms) {
			dc.DrawRectangle(r.x, r.y, r.w, r.h);
		}

		// Corridors
		dc.SetPen(wxPen(Theme::Get(Theme::Role::Success), 2));
		for (int i = 0; i + 1 < 6; ++i) {
			int cx1 = rooms[i].x + rooms[i].w/2;
			int cy1 = rooms[i].y + rooms[i].h/2;
			int cx2 = rooms[i+1].x + rooms[i+1].w/2;
			int cy2 = rooms[i+1].y + rooms[i+1].h/2;
			dc.DrawLine(cx1, cy1, cx2, cy1); // horizontal
			dc.DrawLine(cx2, cy1, cx2, cy2); // vertical
		}
	} else if (sel == 1) {
		// BSP: recursive subdivision lines + rooms in leaves
		dc.SetPen(wxPen(Theme::Get(Theme::Role::TextSubtle), 1, wxPENSTYLE_DOT));

		// Vertical split
		int splitX = ox + iw * 45 / 100;
		dc.DrawLine(splitX, oy, splitX, oy + ih);

		// Left horizontal split
		int splitY1 = oy + ih * 55 / 100;
		dc.DrawLine(ox, splitY1, splitX, splitY1);

		// Right horizontal split
		int splitY2 = oy + ih * 40 / 100;
		dc.DrawLine(splitX, splitY2, ox + iw, splitY2);

		// Right-top vertical split
		int splitX2 = splitX + (iw - splitX + ox) * 50 / 100;
		dc.DrawLine(splitX2, oy, splitX2, splitY2);

		// Rooms in leaves
		dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Accent)));
		dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent)));

		int pad = 4;
		// Top-left
		dc.DrawRectangle(ox + pad, oy + pad, splitX - ox - pad*2, splitY1 - oy - pad*2);
		// Bottom-left
		dc.DrawRectangle(ox + pad, splitY1 + pad, splitX - ox - pad*2, oy + ih - splitY1 - pad*2);
		// Top-right-left
		dc.DrawRectangle(splitX + pad, oy + pad, splitX2 - splitX - pad*2, splitY2 - oy - pad*2);
		// Top-right-right
		dc.DrawRectangle(splitX2 + pad, oy + pad, ox + iw - splitX2 - pad*2, splitY2 - oy - pad*2);
		// Bottom-right
		dc.DrawRectangle(splitX + pad, splitY2 + pad, iw - (splitX - ox) - pad*2, oy + ih - splitY2 - pad*2);

		// Corridors between rooms
		dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
		int midY1 = (oy + splitY1) / 2;
		int midY2 = (splitY1 + oy + ih) / 2;
		dc.DrawLine(ox + (splitX - ox)/2, midY1, ox + (splitX - ox)/2, midY2);

	} else {
		// Random Walk: organic cave-like blobs
		dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Warning)));
		dc.SetPen(*wxTRANSPARENT_PEN);

		// Draw an organic shape using small squares
		int cs = 4; // cell size
		int cx = iw / 2, cy = ih / 2;

		// Pre-baked organic cave pattern
		static const int pattern[][2] = {
			{0,0},{1,0},{-1,0},{0,1},{0,-1},{2,0},{-2,0},{0,2},{0,-2},
			{1,1},{-1,1},{1,-1},{-1,-1},{3,0},{-3,0},{0,3},{0,-3},
			{2,1},{1,2},{-2,1},{-1,2},{2,-1},{1,-2},{-2,-1},{-1,-2},
			{3,1},{3,-1},{-3,1},{-3,-1},{1,3},{-1,3},{1,-3},{-1,-3},
			{4,0},{-4,0},{0,4},{0,-4},{4,1},{-4,1},{4,-1},{-4,-1},
			{2,2},{-2,2},{2,-2},{-2,-2},{3,2},{2,3},{-3,2},{-2,3},
			{5,0},{-5,0},{5,1},{-5,1},{0,5},{0,-5},{3,3},{-3,-3},
			{4,2},{-4,2},{2,4},{-2,4},{5,2},{-5,2},{6,0},{-6,0},
			{6,1},{-6,1},{4,3},{-4,3},{3,4},{-3,4},{5,3},{-5,3},
			{7,0},{7,1},{7,-1},{-7,0},{-7,1},{6,2},{-6,2},
			{8,0},{8,1},{-8,0},{0,6},{0,-6},{1,6},{-1,6},
			{4,4},{-4,4},{5,4},{-5,4},{6,3},{-6,3},
			// Branch going right
			{9,0},{10,0},{10,1},{10,-1},{11,0},{11,1},{12,0},{12,1},
			{13,0},{13,1},{13,2},{14,1},{14,2},{15,1},{15,2},
			// Branch going down
			{0,7},{0,8},{1,8},{-1,8},{0,9},{1,9},{0,10},{1,10},
			{2,10},{0,11},{1,11},{2,11},{0,12},{1,12},
			// Branch going left
			{-8,1},{-9,0},{-9,1},{-10,0},{-10,1},{-10,2},{-11,1},{-11,2},
		};

		for (const auto& p : pattern) {
			int px = ox + cx + p[0] * cs;
			int py = oy + cy + p[1] * cs;
			if (px >= ox && px + cs <= ox + iw && py >= oy && py + cs <= oy + ih) {
				dc.DrawRectangle(px, py, cs, cs);
			}
		}
	}
}

void DungeonGeneratorDialog::OnClose(wxCloseEvent& event) {
	g_gui.DestroyDungeonGeneratorDialog();
}
