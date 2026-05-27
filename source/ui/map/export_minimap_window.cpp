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
#include "ui/map/export_minimap_window.h"

#include "editor/editor.h"
#include "io/iominimap.h"
#include "ui/gui.h"
#include "ui/gui_ids.h"
#include "ui/dialog_util.h"
#include "app/application.h"
#include "app/definitions.h"
#include "util/image_manager.h"

#include <wx/dirdlg.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>
#include <wx/checkbox.h>
#include <wx/statbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>

ExportMinimapWindow::ExportMinimapWindow(wxWindow* parent, Editor& editor) :
	wxDialog(parent, wxID_ANY, "Export Minimap", wxDefaultPosition, FROM_DIP(parent, wxSize(440, 470))),
	editor(editor) {
	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);
	wxSizer* tmpsizer;

	// Error field
	error_field = newd wxStaticText(this, wxID_VIEW_DETAILS, "", wxDefaultPosition, wxDefaultSize);
	error_field->SetForegroundColour(*wxRED);
	tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
	tmpsizer->Add(error_field, 0, wxALL, 5);
	sizer->Add(tmpsizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

	// Output folder
	directory_text_field = newd wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);
	directory_text_field->Bind(wxEVT_KEY_UP, &ExportMinimapWindow::OnDirectoryChanged, this);
	directory_text_field->SetValue(wxString(g_settings.getString(Config::MINIMAP_EXPORT_DIR)));
	tmpsizer = newd wxStaticBoxSizer(wxHORIZONTAL, this, "Output Folder");
	tmpsizer->Add(directory_text_field, 1, wxALL, 5);
	auto browseBtn = newd wxButton(this, MAP_WINDOW_FILE_BUTTON, "Browse");
	browseBtn->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_FOLDER_OPEN));
	browseBtn->SetToolTip("Browse for output directory");
	tmpsizer->Add(browseBtn, 0, wxALL, 5);
	sizer->Add(tmpsizer, 0, wxALL | wxEXPAND, 5);

	// File name + format
	wxString mapName(editor.map.getName().c_str(), wxConvUTF8);
	wxString defaultName = mapName.BeforeLast('.');
	if (defaultName.IsEmpty()) {
		defaultName = mapName.IsEmpty() ? wxString("minimap") : mapName;
	}
	file_name_text_field = newd wxTextCtrl(this, wxID_ANY, defaultName, wxDefaultPosition, wxDefaultSize);
	file_name_text_field->Bind(wxEVT_KEY_UP, &ExportMinimapWindow::OnFileNameChanged, this);
	tmpsizer = newd wxStaticBoxSizer(wxHORIZONTAL, this, "File Name");
	tmpsizer->Add(file_name_text_field, 1, wxALL, 5);

	wxArrayString format_choices;
	format_choices.Add(".otmm (Client Minimap)");
	format_choices.Add(".png (PNG Image)");
	format_choices.Add(".bmp (Bitmap Image)");
	format_options = newd wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, format_choices);
	format_options->SetSelection(0);
	format_options->Bind(wxEVT_CHOICE, &ExportMinimapWindow::OnFormatChange, this);
	tmpsizer->Add(format_options, 1, wxALL, 5);
	sizer->Add(tmpsizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

	// Area options
	wxArrayString choices;
	choices.Add("All Floors");
	choices.Add("Ground Floor");
	choices.Add("Specific Floor");
	if (editor.hasSelection()) {
		choices.Add("Selected Area");
	}
	choices.Add("Area View");

	tmpsizer = newd wxStaticBoxSizer(wxHORIZONTAL, this, "Area Options");
	floor_options = newd wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, choices);
	floor_number = newd wxSpinCtrl(this, wxID_ANY, std::to_string(GROUND_LAYER), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, MAP_MAX_LAYER, GROUND_LAYER);
	floor_number->Enable(false);
	floor_options->SetSelection(0);
	floor_options->Bind(wxEVT_CHOICE, &ExportMinimapWindow::OnExportTypeChange, this);
	tmpsizer->Add(floor_options, 1, wxALL, 5);
	tmpsizer->Add(floor_number, 0, wxALL, 5);
	sizer->Add(tmpsizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

	// Area view positions
	int map_w = static_cast<int>(editor.map.getWidth());
	int map_h = static_cast<int>(editor.map.getHeight());

	tmpsizer = newd wxStaticBoxSizer(wxHORIZONTAL, this, "From Position");
	tmpsizer->Add(newd wxStaticText(this, wxID_ANY, "X:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	from_x_spin = newd wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, map_w, 0);
	tmpsizer->Add(from_x_spin, 1, wxALL, 5);
	tmpsizer->Add(newd wxStaticText(this, wxID_ANY, "Y:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	from_y_spin = newd wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, map_h, 0);
	tmpsizer->Add(from_y_spin, 1, wxALL, 5);
	sizer->Add(tmpsizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

	tmpsizer = newd wxStaticBoxSizer(wxHORIZONTAL, this, "To Position");
	tmpsizer->Add(newd wxStaticText(this, wxID_ANY, "X:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	to_x_spin = newd wxSpinCtrl(this, wxID_ANY, std::to_string(map_w), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, map_w, map_w);
	tmpsizer->Add(to_x_spin, 1, wxALL, 5);
	tmpsizer->Add(newd wxStaticText(this, wxID_ANY, "Y:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	to_y_spin = newd wxSpinCtrl(this, wxID_ANY, std::to_string(map_h), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, map_h, map_h);
	tmpsizer->Add(to_y_spin, 1, wxALL, 5);
	sizer->Add(tmpsizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

	merge_floors_checkbox = newd wxCheckBox(this, wxID_ANY, "Merge floors into single image");
	merge_floors_checkbox->Enable(false);
	sizer->Add(merge_floors_checkbox, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

	uniform_bounds_checkbox = newd wxCheckBox(this, wxID_ANY, "Use same canvas size for all floors (for stacking)");
	uniform_bounds_checkbox->SetToolTip("Exports every floor with identical dimensions and alignment, so images can be stacked on top of each other without offsets.");
	sizer->Add(uniform_bounds_checkbox, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

	from_x_spin->Enable(false);
	from_y_spin->Enable(false);
	to_x_spin->Enable(false);
	to_y_spin->Enable(false);

	// OK/Cancel
	tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
	ok_button = newd wxButton(this, wxID_OK, "OK");
	ok_button->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_CHECK));
	ok_button->SetToolTip("Start export");
	tmpsizer->Add(ok_button, wxSizerFlags(1).Center());
	auto cancelBtn = newd wxButton(this, wxID_CANCEL, "Cancel");
	cancelBtn->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_XMARK));
	cancelBtn->SetToolTip("Cancel");
	tmpsizer->Add(cancelBtn, wxSizerFlags(1).Center());
	sizer->Add(tmpsizer, 0, wxCENTER, 10);

	SetSizer(sizer);
	Layout();
	Centre(wxBOTH);
	CheckValues();

	browseBtn->Bind(wxEVT_BUTTON, &ExportMinimapWindow::OnClickBrowse, this);
	ok_button->Bind(wxEVT_BUTTON, &ExportMinimapWindow::OnClickOK, this);
	cancelBtn->Bind(wxEVT_BUTTON, &ExportMinimapWindow::OnClickCancel, this);

	SetIcons(IMAGE_MANAGER.GetIconBundle(ICON_FILE_EXPORT));
}

ExportMinimapWindow::~ExportMinimapWindow() = default;

void ExportMinimapWindow::OnClickBrowse(wxCommandEvent& WXUNUSED(event)) {
	wxDirDialog dialog(nullptr, "Select the output folder", "", wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
	if (dialog.ShowModal() == wxID_OK) {
		const wxString& directory = dialog.GetPath();
		directory_text_field->ChangeValue(directory);
	}
	CheckValues();
}

void ExportMinimapWindow::OnDirectoryChanged(wxKeyEvent& event) {
	CheckValues();
	event.Skip();
}

void ExportMinimapWindow::OnFileNameChanged(wxKeyEvent& event) {
	CheckValues();
	event.Skip();
}

void ExportMinimapWindow::OnExportTypeChange(wxCommandEvent& WXUNUSED(event)) {
	bool isOtmm = format_options->GetSelection() == 0;
	wxString selected = floor_options->GetStringSelection();
	floor_number->Enable(selected == "Specific Floor");
	bool isAreaView = !isOtmm && selected == "Area View";
	from_x_spin->Enable(isAreaView);
	from_y_spin->Enable(isAreaView);
	to_x_spin->Enable(isAreaView);
	to_y_spin->Enable(isAreaView);
	merge_floors_checkbox->Enable(isAreaView);
	if (!isAreaView) {
		merge_floors_checkbox->SetValue(false);
	}
}

void ExportMinimapWindow::OnFormatChange(wxCommandEvent& WXUNUSED(event)) {
	// The .otmm format always serializes raw minimap tiles into a single
	// binary file. The "Area View" 2D projection and the image-only stacking
	// options do not apply to it, so disable them while OTMM is selected.
	bool isOtmm = format_options->GetSelection() == 0;
	uniform_bounds_checkbox->Enable(!isOtmm);
	if (isOtmm) {
		uniform_bounds_checkbox->SetValue(false);
		if (floor_options->GetStringSelection() == "Area View") {
			floor_options->SetSelection(0);
		}
	}
	wxCommandEvent dummy;
	OnExportTypeChange(dummy);
}

void ExportMinimapWindow::OnClickOK(wxCommandEvent& WXUNUSED(event)) {
	g_settings.setString(Config::MINIMAP_EXPORT_DIR, directory_text_field->GetValue().ToStdString());

	std::string directory = directory_text_field->GetValue().ToStdString();
	std::string filename = file_name_text_field->GetValue().ToStdString();

	MinimapExportFormat format = MinimapExportFormat::Otmm;
	switch (format_options->GetSelection()) {
		case 0:
			format = MinimapExportFormat::Otmm;
			break;
		case 1:
			format = MinimapExportFormat::Png;
			break;
		case 2:
			format = MinimapExportFormat::Bmp;
			break;
	}

	wxString modeStr = floor_options->GetStringSelection();
	MinimapExportMode mode = MinimapExportMode::AllFloors;
	if (modeStr == "Ground Floor") {
		mode = MinimapExportMode::GroundFloor;
	} else if (modeStr == "Specific Floor") {
		mode = MinimapExportMode::SpecificFloor;
	} else if (modeStr == "Selected Area") {
		mode = MinimapExportMode::SelectedArea;
	} else if (modeStr == "Area View") {
		mode = MinimapExportMode::AreaView;
	}

	int floor = floor_number->GetValue();

	g_gui.CreateLoadBar("Exporting minimap...");

	IOMinimap io(&editor, format, mode, true);

	if (mode == MinimapExportMode::AreaView) {
		Position from(from_x_spin->GetValue(), from_y_spin->GetValue(), 0);
		Position to(to_x_spin->GetValue(), to_y_spin->GetValue(), 0);
		io.setArea(from, to);
		io.setMergeFloors(merge_floors_checkbox->GetValue());
	}
	io.setUniformBounds(uniform_bounds_checkbox->GetValue());

	bool ok = io.saveMinimap(directory, filename, floor);

	g_gui.SetLoadDone(100);
	g_gui.DestroyLoadBar();

	if (!ok) {
		std::string err = io.getError();
		if (err.empty()) {
			err = "Failed to export minimap.";
		}
		DialogUtil::PopupDialog("Export Minimap", wxString(err.c_str(), wxConvUTF8), wxOK);
	} else {
		DialogUtil::PopupDialog("Export Minimap", "Minimap exported successfully.", wxOK);
	}

	EndModal(1);
}

void ExportMinimapWindow::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	EndModal(0);
}

void ExportMinimapWindow::CheckValues() {
	if (directory_text_field->IsEmpty()) {
		error_field->SetLabel("Type or select an output folder.");
		ok_button->Enable(false);
		return;
	}

	if (file_name_text_field->IsEmpty()) {
		error_field->SetLabel("Type a name for the file.");
		ok_button->Enable(false);
		return;
	}

	FileName directory(directory_text_field->GetValue());

	if (!directory.Exists()) {
		error_field->SetLabel("Output folder not found.");
		ok_button->Enable(false);
		return;
	}

	if (!directory.IsDirWritable()) {
		error_field->SetLabel("Output folder is not writable.");
		ok_button->Enable(false);
		return;
	}

	error_field->SetLabel(wxEmptyString);
	ok_button->Enable(true);
}
