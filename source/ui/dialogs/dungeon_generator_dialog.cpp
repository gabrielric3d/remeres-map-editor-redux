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
#include "editor/selection.h"
#include "ui/gui.h"
#include "map/map.h"
#include "map/tile.h"
#include "rendering/core/graphics.h"
#include "item_definitions/core/item_definition_store.h"
#include "ui/theme.h"

namespace {

const int ITEM_ICON_SIZE = 32;

enum {
	ID_PRESET_CHOICE = wxID_HIGHEST + 3000,
	ID_WIDTH_SPIN,
	ID_HEIGHT_SPIN,
	ID_PATH_WIDTH_SPIN,
	ID_GENERATE_BTN,
	ID_NEW_PRESET_BTN,
	ID_EDIT_PRESET_BTN,
	ID_DUPLICATE_PRESET_BTN,
	ID_DELETE_PRESET_BTN,
};

wxBitmap CreateItemBitmap(uint16_t itemId, int size) {
	wxBitmap bmp(size, size, 32);
	wxMemoryDC dc(bmp);

	dc.SetBackground(wxBrush(wxColour(0x0C, 0x14, 0x2A)));
	dc.Clear();

	const auto itemDef = g_item_definitions.get(itemId);
	Sprite* spr = nullptr;
	if (itemDef) {
		spr = g_gui.gfx.getSprite(itemDef.clientId());
	}

	if (spr) {
		spr->DrawTo(&dc, SPRITE_SIZE_32x32, 0, 0, size, size);
	} else {
		dc.SetBrush(wxBrush(wxColour(100, 100, 100)));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(2, 2, size - 4, size - 4);
		dc.SetTextForeground(*wxWHITE);
		dc.DrawText("?", size / 2 - 4, size / 2 - 8);
	}

	dc.SelectObject(wxNullBitmap);
	return bmp;
}

} // anonymous namespace

wxBEGIN_EVENT_TABLE(DungeonGeneratorDialog, wxDialog)
	EVT_CHOICE(ID_PRESET_CHOICE, DungeonGeneratorDialog::OnPresetChanged)
	EVT_BUTTON(ID_GENERATE_BTN, DungeonGeneratorDialog::OnGenerate)
	EVT_BUTTON(ID_NEW_PRESET_BTN, DungeonGeneratorDialog::OnNewPreset)
	EVT_BUTTON(ID_EDIT_PRESET_BTN, DungeonGeneratorDialog::OnEditPreset)
	EVT_BUTTON(ID_DUPLICATE_PRESET_BTN, DungeonGeneratorDialog::OnDuplicatePreset)
	EVT_BUTTON(ID_DELETE_PRESET_BTN, DungeonGeneratorDialog::OnDeletePreset)
	EVT_CLOSE(DungeonGeneratorDialog::OnClose)
wxEND_EVENT_TABLE()

DungeonGeneratorDialog::DungeonGeneratorDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "Dungeon Generator", wxDefaultPosition, wxSize(520, 620),
	           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_imageList(nullptr) {

	// Load presets
	auto& presetMgr = DungeonGen::PresetManager::getInstance();
	if (presetMgr.getPresetNames().empty()) {
		presetMgr.loadPresets();
	}

	// Auto-detect selection size
	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		const Selection& sel = editor->selection;
		const auto& tiles = sel.getTiles();
		if (!tiles.empty()) {
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
		}
	}

	if (m_config.width < 10) m_config.width = 70;
	if (m_config.height < 10) m_config.height = 70;
	if (m_config.center.x == 0) {
		m_config.center = Position(100, 100, 7);
	}

	CreateControls();
	PopulatePresetList();
	UpdatePreview();

	Centre();
}

DungeonGeneratorDialog::~DungeonGeneratorDialog() {
	delete m_imageList;
}

void DungeonGeneratorDialog::CreateControls() {
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// Preset selection + management buttons
	wxStaticBoxSizer* presetBox = new wxStaticBoxSizer(wxVERTICAL, this, "Preset");

	wxBoxSizer* presetRow = new wxBoxSizer(wxHORIZONTAL);
	m_presetChoice = new wxChoice(this, ID_PRESET_CHOICE);
	presetRow->Add(m_presetChoice, 1, wxALL | wxEXPAND, 3);
	presetBox->Add(presetRow, 0, wxEXPAND);

	wxBoxSizer* presetBtnRow = new wxBoxSizer(wxHORIZONTAL);
	presetBtnRow->Add(new wxButton(this, ID_NEW_PRESET_BTN, "New", wxDefaultPosition, wxSize(70, -1)), 0, wxRIGHT, 3);
	presetBtnRow->Add(new wxButton(this, ID_EDIT_PRESET_BTN, "Edit", wxDefaultPosition, wxSize(70, -1)), 0, wxRIGHT, 3);
	presetBtnRow->Add(new wxButton(this, ID_DUPLICATE_PRESET_BTN, "Duplicate", wxDefaultPosition, wxSize(70, -1)), 0, wxRIGHT, 3);
	presetBtnRow->AddStretchSpacer();
	presetBtnRow->Add(new wxButton(this, ID_DELETE_PRESET_BTN, "Delete", wxDefaultPosition, wxSize(70, -1)), 0);
	presetBox->Add(presetBtnRow, 0, wxALL | wxEXPAND, 3);

	mainSizer->Add(presetBox, 0, wxALL | wxEXPAND, 5);

	// Parameters
	wxStaticBoxSizer* paramBox = new wxStaticBoxSizer(wxVERTICAL, this, "Parameters");

	wxFlexGridSizer* paramGrid = new wxFlexGridSizer(2, 5, 5);
	paramGrid->AddGrowableCol(1);

	paramGrid->Add(new wxStaticText(this, wxID_ANY, "Width:"), 0, wxALIGN_CENTER_VERTICAL);
	m_widthSpin = new wxSpinCtrl(this, ID_WIDTH_SPIN, wxEmptyString, wxDefaultPosition,
	                              wxDefaultSize, wxSP_ARROW_KEYS, 10, 500, m_config.width);
	paramGrid->Add(m_widthSpin, 0, wxEXPAND);

	paramGrid->Add(new wxStaticText(this, wxID_ANY, "Height:"), 0, wxALIGN_CENTER_VERTICAL);
	m_heightSpin = new wxSpinCtrl(this, ID_HEIGHT_SPIN, wxEmptyString, wxDefaultPosition,
	                               wxDefaultSize, wxSP_ARROW_KEYS, 10, 500, m_config.height);
	paramGrid->Add(m_heightSpin, 0, wxEXPAND);

	paramGrid->Add(new wxStaticText(this, wxID_ANY, "Path Width:"), 0, wxALIGN_CENTER_VERTICAL);
	m_pathWidthSpin = new wxSpinCtrl(this, ID_PATH_WIDTH_SPIN, wxEmptyString, wxDefaultPosition,
	                                  wxDefaultSize, wxSP_ARROW_KEYS, 2, 20, m_config.pathWidth);
	paramGrid->Add(m_pathWidthSpin, 0, wxEXPAND);

	paramBox->Add(paramGrid, 0, wxALL | wxEXPAND, 5);
	mainSizer->Add(paramBox, 0, wxALL | wxEXPAND, 5);

	// Preview notebook
	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);

	// Structure tab
	wxPanel* structPanel = new wxPanel(notebook);
	wxBoxSizer* structSizer = new wxBoxSizer(wxVERTICAL);

	m_imageList = new wxImageList(ITEM_ICON_SIZE, ITEM_ICON_SIZE, true);

	structSizer->Add(new wxStaticText(structPanel, wxID_ANY, "Terrain"), 0, wxALL, 3);
	m_terrainList = new wxListCtrl(structPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 50),
	                                wxLC_ICON | wxLC_SINGLE_SEL);
	m_terrainList->SetImageList(m_imageList, wxIMAGE_LIST_NORMAL);
	structSizer->Add(m_terrainList, 0, wxALL | wxEXPAND, 3);

	structSizer->Add(new wxStaticText(structPanel, wxID_ANY, "Walls & Pillars"), 0, wxALL, 3);
	m_wallsList = new wxListCtrl(structPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 50),
	                              wxLC_ICON | wxLC_SINGLE_SEL);
	m_wallsList->SetImageList(m_imageList, wxIMAGE_LIST_NORMAL);
	structSizer->Add(m_wallsList, 0, wxALL | wxEXPAND, 3);

	structSizer->Add(new wxStaticText(structPanel, wxID_ANY, "Borders"), 0, wxALL, 3);
	m_bordersList = new wxListCtrl(structPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 50),
	                                wxLC_ICON | wxLC_SINGLE_SEL);
	m_bordersList->SetImageList(m_imageList, wxIMAGE_LIST_NORMAL);
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

	// Generate button
	wxButton* generateBtn = new wxButton(this, ID_GENERATE_BTN, "Generate Dungeon",
	                                      wxDefaultPosition, wxSize(-1, 35));
	mainSizer->Add(generateBtn, 0, wxALL | wxEXPAND, 5);

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
	m_wallsList->ClearAll();
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

	// Terrain
	addItem(m_terrainList, preset->groundId, "Ground");
	addItem(m_terrainList, preset->patchId, "Patch");
	addItem(m_terrainList, preset->fillId, "Fill");
	addItem(m_terrainList, preset->brushId, "Brush");

	// Walls
	const auto& w = preset->walls;
	addItem(m_wallsList, w.north, "N");
	if (w.south != w.north) addItem(m_wallsList, w.south, "S");
	if (w.east != w.north) addItem(m_wallsList, w.east, "E");
	if (w.west != w.north) addItem(m_wallsList, w.west, "W");
	addItem(m_wallsList, w.pillar, "Pillar");
	addItem(m_wallsList, w.nw, "NW");
	if (w.ne != w.nw) addItem(m_wallsList, w.ne, "NE");
	if (w.sw != w.nw) addItem(m_wallsList, w.sw, "SW");
	if (w.se != w.nw) addItem(m_wallsList, w.se, "SE");

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

void DungeonGeneratorDialog::OnGenerate(wxCommandEvent& event) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		wxMessageBox("No map open!", "Error", wxICON_ERROR | wxOK, this);
		return;
	}

	m_config.width = m_widthSpin->GetValue();
	m_config.height = m_heightSpin->GetValue();
	m_config.pathWidth = m_pathWidthSpin->GetValue();
	m_config.presetName = m_presetChoice->GetStringSelection().ToStdString();

	// Get center from selection if available
	const Selection& sel = editor->selection;
	const auto& tiles = sel.getTiles();
	if (!tiles.empty()) {
		Position minPos(99999, 99999, 7), maxPos(0, 0, 7);
		for (Tile* t : tiles) {
			Position p = t->getPosition();
			minPos.x = std::min(minPos.x, p.x);
			minPos.y = std::min(minPos.y, p.y);
			maxPos.x = std::max(maxPos.x, p.x);
			maxPos.y = std::max(maxPos.y, p.y);
			minPos.z = p.z;
		}
		m_config.center.x = (minPos.x + maxPos.x) / 2;
		m_config.center.y = (minPos.y + maxPos.y) / 2;
		m_config.center.z = minPos.z;
	}

	if (m_config.center.x == 0 && m_config.center.y == 0) {
		m_config.center = Position(100, 100, 7);
	}

	auto& presetMgr = DungeonGen::PresetManager::getInstance();
	const DungeonGen::DungeonPreset* preset = presetMgr.getPreset(m_config.presetName);
	if (!preset) {
		wxMessageBox("Preset not found: " + m_config.presetName, "Error", wxICON_ERROR | wxOK, this);
		return;
	}

	DungeonGen::DungeonGenerator generator(editor);
	if (generator.generate(m_config, *preset)) {
		g_gui.RefreshView();
		wxMessageBox(wxString::Format("Dungeon generated!\n%d tiles modified.",
		             generator.getTilesGenerated()),
		             "Success", wxICON_INFORMATION | wxOK, this);
	} else {
		wxMessageBox("Generation failed: " + generator.getLastError(),
		             "Error", wxICON_ERROR | wxOK, this);
	}
}

void DungeonGeneratorDialog::OnClose(wxCloseEvent& event) {
	g_gui.DestroyDungeonGeneratorDialog();
}
