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

class wxStaticText;
class wxTextCtrl;
class wxButton;
class wxListBox;

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

protected:
	void CheckValues();
	void SwapSelected(int delta);

	wxStaticText* error_field;
	wxListBox* maps_list;
	wxTextCtrl* directory_text_field;
	wxTextCtrl* file_name_text_field;
	wxButton* ok_button;
};

#endif
