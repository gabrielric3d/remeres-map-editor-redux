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

	// Event handlers
	void OnPresetChanged(wxCommandEvent& event);
	void OnGenerate(wxCommandEvent& event);
	void OnNewPreset(wxCommandEvent& event);
	void OnEditPreset(wxCommandEvent& event);
	void OnDuplicatePreset(wxCommandEvent& event);
	void OnDeletePreset(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);

	// Preset controls
	wxChoice* m_presetChoice;

	// Generation parameters
	wxSpinCtrl* m_widthSpin;
	wxSpinCtrl* m_heightSpin;
	wxSpinCtrl* m_pathWidthSpin;

	// Preview panels
	wxListCtrl* m_terrainList;
	wxListCtrl* m_wallsList;
	wxListCtrl* m_bordersList;
	wxListCtrl* m_detailsList;
	wxListCtrl* m_hangablesList;

	// Image list for item sprites
	wxImageList* m_imageList;

	// Current config
	DungeonGen::DungeonConfig m_config;

	wxDECLARE_EVENT_TABLE();
};

#endif // RME_DUNGEON_GENERATOR_DIALOG_H
