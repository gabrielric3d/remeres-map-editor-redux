//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_MAP_PROFILES_DIALOG_H_
#define RME_MAP_PROFILES_DIALOG_H_

#include "app/main.h"
#include "editor/persistence/map_profiles.h"

#include <vector>

class wxCheckListBox;
class wxFilePickerCtrl;
class wxFileDirPickerEvent;
class wxSpinCtrl;

/**
 * Edits the map profiles (several .otbm files opened together as one map, optionally read from
 * a server maps.xml). Closing it with "Open" returns wxID_OPEN; GetProfileToOpen() tells which one.
 */
class MapProfilesDialog : public wxDialog {
public:
	explicit MapProfilesDialog(wxWindow* parent);
	~MapProfilesDialog() override = default;

	int GetProfileToOpen() const {
		return profile_to_open;
	}

private:
	void OnSelectProfile(wxCommandEvent&);
	void OnAddProfile(wxCommandEvent&);
	void OnRemoveProfile(wxCommandEvent&);
	void OnNameChanged(wxCommandEvent&);
	void OnMapsXmlChanged(wxFileDirPickerEvent&);
	void OnReadMapsXml(wxCommandEvent&);
	void OnSelectMap(wxCommandEvent&);
	void OnToggleMap(wxCommandEvent&);
	void OnCheckAll(bool checked);
	void OnAddMaps(wxCommandEvent&);
	void OnRemoveMap(wxCommandEvent&);
	void OnMoveMap(int direction);
	void OnMapFieldChanged();
	void OnOpenProfile(wxCommandEvent&);
	void OnClickOK(wxCommandEvent&);

	// Re-reads the linked maps.xml of the current profile; reports errors when asked to.
	bool SyncCurrent(bool report_errors);
	void ShowCurrent();
	void FillMapList();
	void ShowSelectedMap();
	void UpdateEnabledState();
	void StoreAndSave();
	wxString ProfileLabel(const MapProfile& profile) const;
	wxString MapLabel(const MapProfileEntry& entry) const;
	bool IsLinked() const;

	std::vector<MapProfile> profiles;
	int current = wxNOT_FOUND;
	int profile_to_open = wxNOT_FOUND;
	bool updating_controls = false;

	wxListBox* profile_list;
	wxButton* remove_profile_button;

	wxTextCtrl* name_text;
	wxFilePickerCtrl* maps_xml_picker;
	wxButton* read_xml_button;
	wxCheckListBox* map_list;
	wxButton* add_maps_button;
	wxButton* remove_map_button;
	wxButton* map_up_button;
	wxButton* map_down_button;
	wxButton* check_all_button;
	wxButton* check_none_button;
	wxSpinCtrl* offset_x_spin;
	wxSpinCtrl* offset_y_spin;
	wxCheckBox* houses_check;
	wxStaticText* linked_hint;
	wxButton* open_button;
};

#endif // RME_MAP_PROFILES_DIALOG_H_
