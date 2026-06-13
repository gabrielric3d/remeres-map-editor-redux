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

#ifndef RME_UI_MAP_MERGE_MAPS_MINIMAP_WINDOW_H_
#define RME_UI_MAP_MERGE_MAPS_MINIMAP_WINDOW_H_

#include "app/main.h"

#include <wx/dialog.h>

#include <string>
#include <vector>

class wxStaticText;
class wxTextCtrl;
class wxButton;
class wxListBox;
class wxChoice;

// Merges several .otbm map files into a single .otmm client minimap. Maps keep
// their absolute X/Y/Z coordinates; tiles sharing a position are overwritten
// by whichever map appears later in the list. This dialog does not require an
// open editor — each map is loaded standalone.
class MergeMapsMinimapWindow : public wxDialog {
public:
	explicit MergeMapsMinimapWindow(wxWindow* parent);
	virtual ~MergeMapsMinimapWindow();

	void OnClickAddMaps(wxCommandEvent&);
	void OnClickRemoveMap(wxCommandEvent&);
	void OnClickMoveUp(wxCommandEvent&);
	void OnClickMoveDown(wxCommandEvent&);
	void OnClickBrowse(wxCommandEvent&);
	void OnDirectoryChanged(wxKeyEvent&);
	void OnFileNameChanged(wxKeyEvent&);
	void OnClickOK(wxCommandEvent&);
	void OnClickCancel(wxCommandEvent&);

	void OnPresetSelected(wxCommandEvent&);
	void OnClickSavePreset(wxCommandEvent&);
	void OnClickSavePresetAs(wxCommandEvent&);
	void OnClickDeletePreset(wxCommandEvent&);

protected:
	// A saved configuration of the dialog: which maps (ordered), where to write
	// the .otmm and under which file name. Persisted to config.toml so the user
	// does not have to re-add maps every export.
	struct MergePreset {
		std::string name;
		std::string folder;
		std::string filename;
		std::vector<std::string> maps;
	};

	void CheckValues();
	void SwapSelected(int delta);

	void LoadPresetsFromSettings();
	void SavePresetsToSettings();
	void RebuildPresetChoice(int select_index);
	void ApplyPreset(const MergePreset& preset);
	MergePreset CaptureCurrentState(const std::string& name) const;
	int FindPresetByName(const std::string& name) const;

	wxStaticText* error_field;
	wxChoice* preset_choice;
	wxButton* delete_preset_button;
	wxListBox* maps_list;
	wxTextCtrl* directory_text_field;
	wxTextCtrl* file_name_text_field;
	wxButton* ok_button;

	std::vector<MergePreset> presets;
};

#endif
