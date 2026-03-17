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

#ifndef RME_DUNGEON_GENERATOR_DIALOG_H
#define RME_DUNGEON_GENERATOR_DIALOG_H

#include <wx/wx.h>
#include <wx/spinctrl.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>

#include "editor/dungeon_generator.h"

class Editor;

//=============================================================================
// DungeonGeneratorDialog - Main dialog for dungeon generation
//=============================================================================
class DungeonGeneratorDialog : public wxDialog {
public:
	DungeonGeneratorDialog(wxWindow* parent);
	~DungeonGeneratorDialog() override;

private:
	void CreateControls();
	void PopulatePresetList();
	void UpdatePreview();
	void RefreshPresetControls();
	void OnAlgorithmChanged(wxCommandEvent& event);
	void ShowAlgorithmParams();
	void UpdateSelectionInfo();
	bool ReadSelectionBounds();

	// Event handlers
	void OnPresetChanged(wxCommandEvent& event);
	void OnGenerate(wxCommandEvent& event);
	void OnReroll(wxCommandEvent& event);
	void OnRerollStructure(wxCommandEvent& event);
	void OnRerollDetails(wxCommandEvent& event);
	void DoGenerate(bool rerollDetailsOnly = false);
	void OnSelectFromMap(wxCommandEvent& event);
	void OnUseSelection(wxCommandEvent& event);
	void OnNewPreset(wxCommandEvent& event);
	void OnEditPreset(wxCommandEvent& event);
	void OnDuplicatePreset(wxCommandEvent& event);
	void OnDeletePreset(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);
	void OnAlgoPreviewPaint(wxPaintEvent& event);

	// Preset controls
	wxChoice* m_presetChoice;

	// Selection / area info
	wxStaticText* m_selectionLabel;
	wxStaticText* m_pickStatusText;
	bool m_hasSelection;

	// Algorithm selector
	wxChoice* m_algorithmChoice;
	wxPanel* m_algoPreviewPanel;

	// Algorithm parameter panels
	wxPanel* m_roomPlacementPanel;
	wxPanel* m_bspPanel;
	wxPanel* m_randomWalkPanel;
	wxBoxSizer* m_paramsSizer;

	// Room Placement params
	wxSpinCtrl* m_rpNumRooms;
	wxSpinCtrl* m_rpMinRoomSize;
	wxSpinCtrl* m_rpMaxRoomSize;
	wxSpinCtrl* m_rpPathWidth;
	wxSpinCtrl* m_rpMinDistance;
	wxCheckBox* m_rpUseMST;

	// BSP params
	wxSpinCtrl* m_bspMinPartition;
	wxSpinCtrl* m_bspMinRoom;
	wxSpinCtrl* m_bspPadding;
	wxSpinCtrl* m_bspCorridorWidth;
	wxSpinCtrl* m_bspMaxDepth;

	// Random Walk params
	wxSpinCtrl* m_rwWalkerCount;
	wxSpinCtrlDouble* m_rwCoverage;
	wxSpinCtrlDouble* m_rwTurnChance;
	wxSpinCtrl* m_rwRoomChance;
	wxSpinCtrl* m_rwMinRoomSize;
	wxSpinCtrl* m_rwMaxRoomSize;

	// Preview panels
	wxListCtrl* m_terrainList;
	wxListCtrl* m_roomFloorsList;
	wxListCtrl* m_corridorFloorsList;
	wxListCtrl* m_roomWallsList;
	wxListCtrl* m_corridorWallsList;
	wxListCtrl* m_bordersList;
	wxListCtrl* m_detailsList;
	wxListCtrl* m_hangablesList;
	wxImageList* m_imageList;

	// Current config
	DungeonGen::DungeonConfig m_config;
	bool m_lastGenerateSuccess = false;
	uint64_t m_lastStructureSeed = 0;  // Saved seed for rerolling details with same structure

	wxDECLARE_EVENT_TABLE();
};

#endif // RME_DUNGEON_GENERATOR_DIALOG_H
