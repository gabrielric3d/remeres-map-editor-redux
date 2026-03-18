#ifndef RME_UI_MAP_MAP_PROPERTIES_WINDOW_H_
#define RME_UI_MAP_MAP_PROPERTIES_WINDOW_H_

#include "app/main.h"
#include <wx/wx.h>
#include <wx/spinctrl.h>

class MapTab;
class Editor;

class MapPropertiesWindow : public wxDialog {
public:
	MapPropertiesWindow(wxWindow* parent, MapTab* tab, Editor& editor, bool allow_create_from_selection = false);
	virtual ~MapPropertiesWindow();

	bool ShouldCreateFromSelection() const;

	void OnChangeVersion(wxCommandEvent&);
	void OnToggleSyncExternalFiles(wxCommandEvent&);
	void OnMapNameChanged(wxCommandEvent&);
	void OnBrowseSaveLocation(wxCommandEvent&);
	void OnSizePresetChanged(wxCommandEvent&);
	void OnDimensionsChanged(wxCommandEvent&);
	void OnDimensionsChangedSpin(wxSpinEvent&);

	void OnClickOK(wxCommandEvent&);
	void OnClickCancel(wxCommandEvent&);

protected:
	void UpdateProtocolList();
	void UpdateExternalFilenameControls();
	void UpdateAutoExternalFilenames();
	void SyncSizePresetSelectionFromDimensions();

	MapTab* view;
	Editor& editor;
	wxSpinCtrl* height_spin;
	wxSpinCtrl* width_spin;
	wxTextCtrl* map_name_ctrl;
	wxTextCtrl* save_location_ctrl;
	wxChoice* size_preset_choice;
	wxChoice* version_choice;
	wxChoice* protocol_choice;
	wxCheckBox* sync_external_files_checkbox;
	wxCheckBox* create_from_selection_checkbox;
	wxCheckBox* remember_save_location_checkbox;
	wxTextCtrl* description_ctrl;
	wxTextCtrl* house_filename_ctrl;
	wxTextCtrl* spawn_filename_ctrl;
	wxTextCtrl* waypoint_filename_ctrl;
	std::string default_house_filename;
	std::string default_spawn_filename;
	std::string default_waypoint_filename;
	bool updating_dimensions;
};

#endif
