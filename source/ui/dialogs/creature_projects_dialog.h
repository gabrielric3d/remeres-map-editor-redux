//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_CREATURE_PROJECTS_DIALOG_H_
#define RME_CREATURE_PROJECTS_DIALOG_H_

#include "app/main.h"
#include "game/creature_projects.h"

#include <vector>

class wxDirPickerCtrl;

/**
 * Edits the creature projects: which map folder belongs to which project and which
 * monster/NPC folders or files are imported when a map of that folder is opened.
 */
class CreatureProjectsDialog : public wxDialog {
public:
	explicit CreatureProjectsDialog(wxWindow* parent);
	~CreatureProjectsDialog() override = default;

private:
	void OnSelectProject(wxCommandEvent&);
	void OnAddProject(wxCommandEvent&);
	void OnRemoveProject(wxCommandEvent&);
	void OnNameChanged(wxCommandEvent&);
	void OnAddSourceFolder(wxCommandEvent&);
	void OnAddSourceFile(wxCommandEvent&);
	void OnRemoveSource(wxCommandEvent&);
	void OnMoveSource(int direction);
	void OnLoadNow(wxCommandEvent&);
	void OnClickOK(wxCommandEvent&);

	// Copies the editor controls into projects[current] / the selected project into the controls.
	void StoreCurrent();
	void ShowCurrent();
	void UpdateEnabledState();
	wxString ListLabel(const CreatureProject& project) const;

	std::vector<CreatureProject> projects;
	int current = wxNOT_FOUND;
	bool updating_controls = false;

	wxListBox* project_list;
	wxButton* remove_project_button;

	wxCheckBox* enabled_check;
	wxTextCtrl* name_text;
	wxDirPickerCtrl* map_folder_picker;
	wxListBox* source_list;
	wxButton* add_folder_button;
	wxButton* add_file_button;
	wxButton* remove_source_button;
	wxButton* source_up_button;
	wxButton* source_down_button;
	wxTextCtrl* exclude_text;
	wxButton* load_now_button;
};

#endif // RME_CREATURE_PROJECTS_DIALOG_H_
