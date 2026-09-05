//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_GHOST_FLOORS_DIALOG_H_
#define RME_GHOST_FLOORS_DIALOG_H_

#include "app/main.h"

class wxSpinCtrl;
class wxSlider;

/**
 * Settings for the "Ghost Floors" toggle found in the radial wheel: which floors
 * above/below the current one are drawn translucent, how many, and how faint.
 * Every change is applied to the map view right away so it can be tuned while
 * the mode is active; Cancel puts the previous values back.
 */
class GhostFloorsDialog : public wxDialog {
public:
	explicit GhostFloorsDialog(wxWindow* parent);
	~GhostFloorsDialog() override = default;

private:
	void OnSettingChanged(wxCommandEvent&);
	void OnAlphaChanged(wxCommandEvent&);
	void OnClickOK(wxCommandEvent&);
	void OnClickCancel(wxCommandEvent&);

	// Greys out the controls whose master toggle is off.
	void UpdateEnabledState();
	// Writes the controls into g_settings and repaints the map.
	void ApplyToSettings();
	void RefreshAlphaLabel();

	struct Snapshot {
		int enabled, above_enabled, below_enabled, above_count, below_count, alpha, fade;
	};
	Snapshot original;

	wxCheckBox* enabled_check;
	wxCheckBox* above_check;
	wxSpinCtrl* above_count;
	wxCheckBox* above_all_check;
	wxCheckBox* below_check;
	wxSpinCtrl* below_count;
	wxCheckBox* below_all_check;
	wxSlider* alpha_slider;
	wxStaticText* alpha_label;
	wxCheckBox* fade_check;
};

#endif // RME_GHOST_FLOORS_DIALOG_H_
