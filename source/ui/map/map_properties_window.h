#ifndef RME_UI_MAP_MAP_PROPERTIES_WINDOW_H_
#define RME_UI_MAP_MAP_PROPERTIES_WINDOW_H_

#include "app/main.h"
#include "map/position.h"
#include <wx/wx.h>
#include <wx/spinctrl.h>

class MapTab;
class Editor;

class MapPropertiesWindow : public wxDialog {
public:
	// How the current selection is carried over into a brand-new map.
	enum class CreateFromSelectionMode {
		None, // Start with an empty map
		KeepPositions, // Copy the selected tiles at their original coordinates
		MoveToOrigin // Legacy behaviour: paste the selection at (0, 0, 7)
	};

	MapPropertiesWindow(wxWindow* parent, MapTab* tab, Editor& editor, bool allow_create_from_selection = false, const Position& selection_min = Position(), const Position& selection_max = Position());
	virtual ~MapPropertiesWindow();

	bool ShouldCreateFromSelection() const;
	CreateFromSelectionMode GetCreateFromSelectionMode() const;
	bool ShouldCopyFromMap() const;
	Editor* GetCopySourceEditor() const;
	Position GetCopyFromPosition() const;
	Position GetCopyToPosition() const;

	void OnChangeVersion(wxCommandEvent&);
	void OnToggleSyncExternalFiles(wxCommandEvent&);
	void OnMapNameChanged(wxCommandEvent&);
	void OnBrowseSaveLocation(wxCommandEvent&);
	void OnSizePresetChanged(wxCommandEvent&);
	void OnDimensionsChanged(wxCommandEvent&);
	void OnDimensionsChangedSpin(wxSpinEvent&);
	void OnCopyFromMapChanged(wxCommandEvent&);
	void OnCreateFromSelectionChanged(wxCommandEvent&);

	void OnClickOK(wxCommandEvent&);
	void OnClickCancel(wxCommandEvent&);

protected:
	void UpdateProtocolList();
	void UpdateExternalFilenameControls();
	void UpdateAutoExternalFilenames();
	void UpdateCopyFromMapControls();
	void SyncSizePresetSelectionFromDimensions();
	void EnsureDimensionsFit(int required_width, int required_height);

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
	wxChoice* create_from_selection_choice;
	wxCheckBox* remember_save_location_checkbox;
	wxTextCtrl* description_ctrl;
	wxTextCtrl* house_filename_ctrl;
	wxTextCtrl* spawn_filename_ctrl;
	wxTextCtrl* waypoint_filename_ctrl;
	wxChoice* copy_from_map_choice;
	wxSpinCtrl* from_x_spin;
	wxSpinCtrl* from_y_spin;
	wxSpinCtrl* from_z_spin;
	wxSpinCtrl* to_x_spin;
	wxSpinCtrl* to_y_spin;
	wxSpinCtrl* to_z_spin;
	Position selection_min;
	Position selection_max;
	std::string default_house_filename;
	std::string default_spawn_filename;
	std::string default_waypoint_filename;
	bool updating_dimensions;
};

#endif
