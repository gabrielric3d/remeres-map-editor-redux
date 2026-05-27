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
#include "ui/map/merge_maps_minimap_window.h"

#include "io/iominimap.h"
#include "ui/gui.h"
#include "ui/gui_ids.h"
#include "ui/dialog_util.h"
#include "app/settings.h"
#include "util/image_manager.h"

#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/textctrl.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/button.h>

#include <vector>

MergeMapsMinimapWindow::MergeMapsMinimapWindow(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Merge Maps to Minimap", wxDefaultPosition, FROM_DIP(parent, wxSize(480, 480))) {
	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);
	wxSizer* tmpsizer;

	// Error field
	error_field = newd wxStaticText(this, wxID_VIEW_DETAILS, "", wxDefaultPosition, wxDefaultSize);
	error_field->SetForegroundColour(*wxRED);
	tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
	tmpsizer->Add(error_field, 0, wxALL, 5);
	sizer->Add(tmpsizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

	// Maps list + management buttons. Order matters: tiles sharing a position
	// are overwritten by maps further down the list (the last one wins).
	tmpsizer = newd wxStaticBoxSizer(wxHORIZONTAL, this, "Maps to Merge (lower entries override overlapping tiles)");
	maps_list = newd wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	tmpsizer->Add(maps_list, 1, wxALL | wxEXPAND, 5);

	wxSizer* btnsizer = newd wxBoxSizer(wxVERTICAL);
	auto addBtn = newd wxButton(this, wxID_ANY, "Add...");
	auto removeBtn = newd wxButton(this, wxID_ANY, "Remove");
	auto upBtn = newd wxButton(this, wxID_ANY, "Move Up");
	auto downBtn = newd wxButton(this, wxID_ANY, "Move Down");
	btnsizer->Add(addBtn, 0, wxALL | wxEXPAND, 3);
	btnsizer->Add(removeBtn, 0, wxALL | wxEXPAND, 3);
	btnsizer->Add(upBtn, 0, wxALL | wxEXPAND, 3);
	btnsizer->Add(downBtn, 0, wxALL | wxEXPAND, 3);
	tmpsizer->Add(btnsizer, 0, wxALL, 5);
	sizer->Add(tmpsizer, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

	// Output folder
	directory_text_field = newd wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);
	directory_text_field->Bind(wxEVT_KEY_UP, &MergeMapsMinimapWindow::OnDirectoryChanged, this);
	directory_text_field->SetValue(wxString(g_settings.getString(Config::MINIMAP_EXPORT_DIR)));
	tmpsizer = newd wxStaticBoxSizer(wxHORIZONTAL, this, "Output Folder");
	tmpsizer->Add(directory_text_field, 1, wxALL, 5);
	auto browseBtn = newd wxButton(this, wxID_ANY, "Browse");
	browseBtn->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_FOLDER_OPEN));
	browseBtn->SetToolTip("Browse for output directory");
	tmpsizer->Add(browseBtn, 0, wxALL, 5);
	sizer->Add(tmpsizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

	// File name
	file_name_text_field = newd wxTextCtrl(this, wxID_ANY, "merged_minimap", wxDefaultPosition, wxDefaultSize);
	file_name_text_field->Bind(wxEVT_KEY_UP, &MergeMapsMinimapWindow::OnFileNameChanged, this);
	tmpsizer = newd wxStaticBoxSizer(wxHORIZONTAL, this, "Output File Name (.otmm)");
	tmpsizer->Add(file_name_text_field, 1, wxALL, 5);
	sizer->Add(tmpsizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

	// OK/Cancel
	tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
	ok_button = newd wxButton(this, wxID_OK, "Merge");
	ok_button->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_CHECK));
	ok_button->SetToolTip("Start merge");
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

	addBtn->Bind(wxEVT_BUTTON, &MergeMapsMinimapWindow::OnClickAddMaps, this);
	removeBtn->Bind(wxEVT_BUTTON, &MergeMapsMinimapWindow::OnClickRemoveMap, this);
	upBtn->Bind(wxEVT_BUTTON, &MergeMapsMinimapWindow::OnClickMoveUp, this);
	downBtn->Bind(wxEVT_BUTTON, &MergeMapsMinimapWindow::OnClickMoveDown, this);
	browseBtn->Bind(wxEVT_BUTTON, &MergeMapsMinimapWindow::OnClickBrowse, this);
	ok_button->Bind(wxEVT_BUTTON, &MergeMapsMinimapWindow::OnClickOK, this);
	cancelBtn->Bind(wxEVT_BUTTON, &MergeMapsMinimapWindow::OnClickCancel, this);

	SetIcons(IMAGE_MANAGER.GetIconBundle(ICON_FILE_EXPORT));
}

MergeMapsMinimapWindow::~MergeMapsMinimapWindow() = default;

void MergeMapsMinimapWindow::OnClickAddMaps(wxCommandEvent& WXUNUSED(event)) {
	wxFileDialog dialog(this, "Select maps to merge", "", "",
		"OTBM Maps (*.otbm)|*.otbm|All Files (*.*)|*.*",
		wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
	if (dialog.ShowModal() == wxID_OK) {
		wxArrayString paths;
		dialog.GetPaths(paths);
		for (size_t i = 0; i < paths.GetCount(); ++i) {
			if (maps_list->FindString(paths[i]) == wxNOT_FOUND) {
				maps_list->Append(paths[i]);
			}
		}
	}
	CheckValues();
}

void MergeMapsMinimapWindow::OnClickRemoveMap(wxCommandEvent& WXUNUSED(event)) {
	int sel = maps_list->GetSelection();
	if (sel != wxNOT_FOUND) {
		maps_list->Delete(sel);
		if (maps_list->GetCount() > 0) {
			maps_list->SetSelection(std::min(sel, static_cast<int>(maps_list->GetCount()) - 1));
		}
	}
	CheckValues();
}

void MergeMapsMinimapWindow::SwapSelected(int delta) {
	int sel = maps_list->GetSelection();
	if (sel == wxNOT_FOUND) {
		return;
	}
	int target = sel + delta;
	if (target < 0 || target >= static_cast<int>(maps_list->GetCount())) {
		return;
	}
	wxString a = maps_list->GetString(sel);
	wxString b = maps_list->GetString(target);
	maps_list->SetString(sel, b);
	maps_list->SetString(target, a);
	maps_list->SetSelection(target);
}

void MergeMapsMinimapWindow::OnClickMoveUp(wxCommandEvent& WXUNUSED(event)) {
	SwapSelected(-1);
}

void MergeMapsMinimapWindow::OnClickMoveDown(wxCommandEvent& WXUNUSED(event)) {
	SwapSelected(1);
}

void MergeMapsMinimapWindow::OnClickBrowse(wxCommandEvent& WXUNUSED(event)) {
	wxDirDialog dialog(nullptr, "Select the output folder", "", wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
	if (dialog.ShowModal() == wxID_OK) {
		directory_text_field->ChangeValue(dialog.GetPath());
	}
	CheckValues();
}

void MergeMapsMinimapWindow::OnDirectoryChanged(wxKeyEvent& event) {
	CheckValues();
	event.Skip();
}

void MergeMapsMinimapWindow::OnFileNameChanged(wxKeyEvent& event) {
	CheckValues();
	event.Skip();
}

void MergeMapsMinimapWindow::OnClickOK(wxCommandEvent& WXUNUSED(event)) {
	g_settings.setString(Config::MINIMAP_EXPORT_DIR, directory_text_field->GetValue().ToStdString());

	std::string directory = directory_text_field->GetValue().ToStdString();
	std::string filename = file_name_text_field->GetValue().ToStdString();

	std::vector<std::string> otbm_files;
	for (unsigned int i = 0; i < maps_list->GetCount(); ++i) {
		otbm_files.push_back(maps_list->GetString(i).ToStdString());
	}

	g_gui.CreateLoadBar("Merging maps into minimap...");

	IOMinimap io(nullptr, MinimapExportFormat::Otmm, MinimapExportMode::AllFloors, true);
	bool ok = io.mergeMaps(otbm_files, directory, filename);

	g_gui.SetLoadDone(100);
	g_gui.DestroyLoadBar();

	if (!ok) {
		std::string err = io.getError();
		if (err.empty()) {
			err = "Failed to merge maps.";
		}
		DialogUtil::PopupDialog("Merge Maps to Minimap", wxString(err.c_str(), wxConvUTF8), wxOK);
	} else {
		DialogUtil::PopupDialog("Merge Maps to Minimap", "Merged minimap exported successfully.", wxOK);
		EndModal(1);
	}
}

void MergeMapsMinimapWindow::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	EndModal(0);
}

void MergeMapsMinimapWindow::CheckValues() {
	if (maps_list->GetCount() == 0) {
		error_field->SetLabel("Add at least one .otbm map.");
		ok_button->Enable(false);
		return;
	}

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
