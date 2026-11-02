//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/dialogs/creature_projects_dialog.h"

#include "map/map.h"
#include "ui/dialog_util.h"
#include "ui/gui.h"
#include "util/image_manager.h"

#include <wx/filename.h>
#include <wx/filepicker.h>
#include <wx/statline.h>

// ============================================================================
// Creature Projects Dialog
// A project = map folder + the monster/NPC sources imported when one of its maps opens.

CreatureProjectsDialog::CreatureProjectsDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Creature Projects", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	projects(CreatureProjects::Load()) {
	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	wxStaticText* intro = newd wxStaticText(this, wxID_ANY,
		"When a map inside a project's map folder (at any depth) is opened, the monster and NPC\n"
		"files of that project are loaded automatically. The deepest folder wins when projects are nested.");
	sizer->Add(intro, 0, wxALL, 12);

	wxSizer* body_sizer = newd wxBoxSizer(wxHORIZONTAL);

	// ---- Project list ----
	wxSizer* list_sizer = newd wxBoxSizer(wxVERTICAL);
	project_list = newd wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(200, 260));
	list_sizer->Add(project_list, 1, wxEXPAND | wxBOTTOM, 6);
	wxSizer* list_buttons = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* add_project_button = newd wxButton(this, wxID_ANY, "Add");
	add_project_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_PLUS, wxSize(16, 16)));
	list_buttons->Add(add_project_button, 1, wxRIGHT, 4);
	remove_project_button = newd wxButton(this, wxID_ANY, "Remove");
	remove_project_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_TRASH_CAN, wxSize(16, 16)));
	list_buttons->Add(remove_project_button, 1);
	list_sizer->Add(list_buttons, 0, wxEXPAND);
	body_sizer->Add(list_sizer, 0, wxEXPAND | wxRIGHT, 12);

	// ---- Selected project ----
	wxStaticBoxSizer* project_box = newd wxStaticBoxSizer(wxVERTICAL, this, "Project");
	wxWindow* box = project_box->GetStaticBox();

	enabled_check = newd wxCheckBox(box, wxID_ANY, "Load this project's creatures when one of its maps is opened");
	project_box->Add(enabled_check, 0, wxALL, 6);

	wxFlexGridSizer* fields = newd wxFlexGridSizer(2, 6, 6);
	fields->AddGrowableCol(1);
	fields->Add(newd wxStaticText(box, wxID_ANY, "Name:"), 0, wxALIGN_CENTER_VERTICAL);
	name_text = newd wxTextCtrl(box, wxID_ANY);
	fields->Add(name_text, 1, wxEXPAND);
	fields->Add(newd wxStaticText(box, wxID_ANY, "Map folder:"), 0, wxALIGN_CENTER_VERTICAL);
	map_folder_picker = newd wxDirPickerCtrl(box, wxID_ANY, "", "Select the folder that holds the project's maps",
		wxDefaultPosition, wxSize(360, -1), wxDIRP_DEFAULT_STYLE | wxDIRP_USE_TEXTCTRL);
	map_folder_picker->SetToolTip("Every map saved inside this folder, or any folder below it, belongs to the project.");
	fields->Add(map_folder_picker, 1, wxEXPAND);
	project_box->Add(fields, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	project_box->Add(newd wxStaticText(box, wxID_ANY,
		"Monster / NPC sources (folders are scanned recursively for .xml and .json;\n"
		"when a creature shows up twice, the source lower in the list wins):"), 0, wxLEFT | wxRIGHT | wxTOP, 6);
	wxSizer* sources_sizer = newd wxBoxSizer(wxHORIZONTAL);
	source_list = newd wxListBox(box, wxID_ANY, wxDefaultPosition, wxSize(-1, 120));
	sources_sizer->Add(source_list, 1, wxEXPAND | wxRIGHT, 6);
	wxSizer* source_buttons = newd wxBoxSizer(wxVERTICAL);
	add_folder_button = newd wxButton(box, wxID_ANY, "Add folder...");
	add_folder_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_FOLDER_PLUS, wxSize(16, 16)));
	source_buttons->Add(add_folder_button, 0, wxEXPAND | wxBOTTOM, 4);
	add_file_button = newd wxButton(box, wxID_ANY, "Add file...");
	add_file_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_FILE_CIRCLE_PLUS, wxSize(16, 16)));
	source_buttons->Add(add_file_button, 0, wxEXPAND | wxBOTTOM, 4);
	remove_source_button = newd wxButton(box, wxID_ANY, "Remove");
	remove_source_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_TRASH_CAN, wxSize(16, 16)));
	source_buttons->Add(remove_source_button, 0, wxEXPAND | wxBOTTOM, 4);
	source_up_button = newd wxButton(box, wxID_ANY, "Up");
	source_up_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_ARROW_UP, wxSize(16, 16)));
	source_buttons->Add(source_up_button, 0, wxEXPAND | wxBOTTOM, 4);
	source_down_button = newd wxButton(box, wxID_ANY, "Down");
	source_down_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_ARROW_DOWN, wxSize(16, 16)));
	source_buttons->Add(source_down_button, 0, wxEXPAND);
	sources_sizer->Add(source_buttons, 0);
	project_box->Add(sources_sizer, 1, wxEXPAND | wxALL, 6);

	wxSizer* exclude_sizer = newd wxBoxSizer(wxHORIZONTAL);
	exclude_sizer->Add(newd wxStaticText(box, wxID_ANY, "Skip sub-folders named:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	exclude_text = newd wxTextCtrl(box, wxID_ANY);
	exclude_text->SetToolTip("Folder names ignored while scanning the source folders, separated by commas\n(for example: backup, old). Old copies there would otherwise override the real files.");
	exclude_sizer->Add(exclude_text, 1, wxEXPAND);
	project_box->Add(exclude_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	load_now_button = newd wxButton(box, wxID_ANY, "Load now");
	load_now_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_ROTATE, wxSize(16, 16)));
	load_now_button->SetToolTip("Import this project's creatures right away, e.g. after editing its monster files.");
	project_box->Add(load_now_button, 0, wxALL, 6);

	body_sizer->Add(project_box, 1, wxEXPAND);
	sizer->Add(body_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);

	// ---- OK/Cancel ----
	wxSizer* button_sizer = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* ok_button = newd wxButton(this, wxID_OK, "OK");
	ok_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_CHECK, wxSize(16, 16)));
	button_sizer->Add(ok_button, wxSizerFlags(1).Center());
	wxButton* cancel_button = newd wxButton(this, wxID_CANCEL, "Cancel");
	cancel_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_XMARK, wxSize(16, 16)));
	button_sizer->Add(cancel_button, wxSizerFlags(1).Center());
	sizer->Add(button_sizer, 0, wxALL | wxCENTER, 12);

	SetSizerAndFit(sizer);
	Centre(wxBOTH);

	for (const CreatureProject& project : projects) {
		project_list->Append(ListLabel(project));
	}
	if (!projects.empty()) {
		current = 0;
		project_list->SetSelection(0);
	}
	ShowCurrent();

	project_list->Bind(wxEVT_LISTBOX, &CreatureProjectsDialog::OnSelectProject, this);
	add_project_button->Bind(wxEVT_BUTTON, &CreatureProjectsDialog::OnAddProject, this);
	remove_project_button->Bind(wxEVT_BUTTON, &CreatureProjectsDialog::OnRemoveProject, this);
	name_text->Bind(wxEVT_TEXT, &CreatureProjectsDialog::OnNameChanged, this);
	enabled_check->Bind(wxEVT_CHECKBOX, &CreatureProjectsDialog::OnNameChanged, this);
	source_list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { UpdateEnabledState(); });
	add_folder_button->Bind(wxEVT_BUTTON, &CreatureProjectsDialog::OnAddSourceFolder, this);
	add_file_button->Bind(wxEVT_BUTTON, &CreatureProjectsDialog::OnAddSourceFile, this);
	remove_source_button->Bind(wxEVT_BUTTON, &CreatureProjectsDialog::OnRemoveSource, this);
	source_up_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnMoveSource(-1); });
	source_down_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnMoveSource(1); });
	load_now_button->Bind(wxEVT_BUTTON, &CreatureProjectsDialog::OnLoadNow, this);
	ok_button->Bind(wxEVT_BUTTON, &CreatureProjectsDialog::OnClickOK, this);

	wxIcon icon;
	icon.CopyFromBitmap(IMAGE_MANAGER.GetBitmap(ICON_DRAGON, wxSize(32, 32)));
	SetIcon(icon);
}

wxString CreatureProjectsDialog::ListLabel(const CreatureProject& project) const {
	wxString label = project.name.empty() ? wxString("(unnamed)") : project.name;
	if (!project.enabled) {
		label += " (off)";
	}
	return label;
}

void CreatureProjectsDialog::StoreCurrent() {
	if (current == wxNOT_FOUND) {
		return;
	}
	CreatureProject& project = projects[current];
	project.enabled = enabled_check->GetValue();
	project.name = name_text->GetValue().Trim().Trim(false);
	project.map_folder = map_folder_picker->GetPath();
	project.exclude_folders = exclude_text->GetValue();
	project.sources.clear();
	for (unsigned int i = 0; i < source_list->GetCount(); ++i) {
		project.sources.push_back(source_list->GetString(i));
	}
}

void CreatureProjectsDialog::ShowCurrent() {
	updating_controls = true;
	source_list->Clear();
	if (current == wxNOT_FOUND) {
		enabled_check->SetValue(false);
		name_text->ChangeValue("");
		map_folder_picker->SetPath("");
		exclude_text->ChangeValue("");
	} else {
		const CreatureProject& project = projects[current];
		enabled_check->SetValue(project.enabled);
		name_text->ChangeValue(project.name);
		map_folder_picker->SetPath(project.map_folder);
		exclude_text->ChangeValue(project.exclude_folders);
		for (const wxString& source : project.sources) {
			source_list->Append(source);
		}
	}
	updating_controls = false;
	UpdateEnabledState();
}

void CreatureProjectsDialog::UpdateEnabledState() {
	const bool has_project = current != wxNOT_FOUND;
	const int source = source_list->GetSelection();
	remove_project_button->Enable(has_project);
	enabled_check->Enable(has_project);
	name_text->Enable(has_project);
	map_folder_picker->Enable(has_project);
	source_list->Enable(has_project);
	add_folder_button->Enable(has_project);
	add_file_button->Enable(has_project);
	exclude_text->Enable(has_project);
	load_now_button->Enable(has_project && source_list->GetCount() > 0);
	remove_source_button->Enable(has_project && source != wxNOT_FOUND);
	source_up_button->Enable(has_project && source != wxNOT_FOUND && source > 0);
	source_down_button->Enable(has_project && source != wxNOT_FOUND && source + 1 < static_cast<int>(source_list->GetCount()));
}

void CreatureProjectsDialog::OnSelectProject(wxCommandEvent&) {
	StoreCurrent();
	current = project_list->GetSelection();
	ShowCurrent();
}

void CreatureProjectsDialog::OnAddProject(wxCommandEvent&) {
	StoreCurrent();

	CreatureProject project;
	project.name = wxString::Format("Project %zu", projects.size() + 1);
	// Start from the folder of the map being edited, the usual reason to create a project.
	if (g_gui.IsEditorOpen() && g_gui.GetCurrentMap().hasFile()) {
		project.map_folder = wxFileName(wxstr(g_gui.GetCurrentMap().getFilename())).GetPath();
	}
	projects.push_back(project);
	project_list->Append(ListLabel(project));

	current = static_cast<int>(projects.size()) - 1;
	project_list->SetSelection(current);
	ShowCurrent();
	name_text->SetFocus();
	name_text->SelectAll();
}

void CreatureProjectsDialog::OnRemoveProject(wxCommandEvent&) {
	if (current == wxNOT_FOUND) {
		return;
	}
	projects.erase(projects.begin() + current);
	project_list->Delete(current);
	if (projects.empty()) {
		current = wxNOT_FOUND;
	} else {
		current = std::min(current, static_cast<int>(projects.size()) - 1);
		project_list->SetSelection(current);
	}
	ShowCurrent();
}

void CreatureProjectsDialog::OnNameChanged(wxCommandEvent&) {
	if (updating_controls || current == wxNOT_FOUND) {
		return;
	}
	StoreCurrent();
	project_list->SetString(current, ListLabel(projects[current]));
}

void CreatureProjectsDialog::OnAddSourceFolder(wxCommandEvent&) {
	wxString start = map_folder_picker->GetPath();
	wxDirDialog dlg(this, "Select a monster or NPC folder", start, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		source_list->Append(dlg.GetPath());
		source_list->SetSelection(source_list->GetCount() - 1);
		UpdateEnabledState();
	}
}

void CreatureProjectsDialog::OnAddSourceFile(wxCommandEvent&) {
	wxFileDialog dlg(this, "Select monster/NPC files", map_folder_picker->GetPath(), "",
		"Monster/NPC files (*.xml;*.json)|*.xml;*.json|All files (*.*)|*.*", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		wxArrayString paths;
		dlg.GetPaths(paths);
		for (const wxString& path : paths) {
			source_list->Append(path);
		}
		source_list->SetSelection(source_list->GetCount() - 1);
		UpdateEnabledState();
	}
}

void CreatureProjectsDialog::OnRemoveSource(wxCommandEvent&) {
	const int selection = source_list->GetSelection();
	if (selection == wxNOT_FOUND) {
		return;
	}
	source_list->Delete(selection);
	if (source_list->GetCount() > 0) {
		source_list->SetSelection(std::min(selection, static_cast<int>(source_list->GetCount()) - 1));
	}
	UpdateEnabledState();
}

void CreatureProjectsDialog::OnMoveSource(int direction) {
	const int selection = source_list->GetSelection();
	const int target = selection + direction;
	if (selection == wxNOT_FOUND || target < 0 || target >= static_cast<int>(source_list->GetCount())) {
		return;
	}
	const wxString path = source_list->GetString(selection);
	source_list->Delete(selection);
	source_list->Insert(path, target);
	source_list->SetSelection(target);
	UpdateEnabledState();
}

void CreatureProjectsDialog::OnLoadNow(wxCommandEvent&) {
	if (current == wxNOT_FOUND) {
		return;
	}
	StoreCurrent();
	const CreatureProject& project = projects[current];

	const CreatureImportResult result = CreatureProjects::LoadProject(project);
	g_gui.RefreshPalettes();
	g_gui.RefreshView();

	wxMessageBox(CreatureProjects::Describe(result), project.name + " - Creatures loaded", wxOK | wxICON_INFORMATION, this);
	if (!result.warnings.empty()) {
		DialogUtil::ListDialog("Monster loader warnings", result.warnings);
	}
}

void CreatureProjectsDialog::OnClickOK(wxCommandEvent&) {
	StoreCurrent();
	for (size_t i = 0; i < projects.size(); ++i) {
		if (projects[i].name.empty()) {
			projects[i].name = wxString::Format("Project %zu", i + 1);
		}
	}
	CreatureProjects::Save(projects);
	EndModal(wxID_OK);
}
