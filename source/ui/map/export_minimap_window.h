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

#ifndef RME_UI_MAP_EXPORT_MINIMAP_WINDOW_H_
#define RME_UI_MAP_EXPORT_MINIMAP_WINDOW_H_

#include "app/main.h"

#include <wx/dialog.h>

class Editor;
class wxStaticText;
class wxTextCtrl;
class wxButton;
class wxChoice;
class wxSpinCtrl;
class wxCheckBox;

class ExportMinimapWindow : public wxDialog {
public:
	ExportMinimapWindow(wxWindow* parent, Editor& editor);
	virtual ~ExportMinimapWindow();

	void OnClickBrowse(wxCommandEvent&);
	void OnDirectoryChanged(wxKeyEvent&);
	void OnFileNameChanged(wxKeyEvent&);
	void OnClickOK(wxCommandEvent&);
	void OnClickCancel(wxCommandEvent&);
	void OnExportTypeChange(wxCommandEvent&);

protected:
	void CheckValues();

	Editor& editor;

	wxChoice* format_options;
	wxStaticText* error_field;
	wxTextCtrl* directory_text_field;
	wxTextCtrl* file_name_text_field;
	wxChoice* floor_options;
	wxSpinCtrl* floor_number;
	wxButton* ok_button;

	wxSpinCtrl* from_x_spin;
	wxSpinCtrl* from_y_spin;
	wxSpinCtrl* to_x_spin;
	wxSpinCtrl* to_y_spin;
	wxCheckBox* merge_floors_checkbox;
	wxCheckBox* uniform_bounds_checkbox;
};

#endif
