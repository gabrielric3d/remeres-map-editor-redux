//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ERASE_FLOORS_DIALOG_H_
#define RME_ERASE_FLOORS_DIALOG_H_

#include "app/main.h"

class wxSpinCtrl;

/**
 * Settings for the "erase floors above/below" toggles found in the radial wheel.
 * While erasing with Ctrl + brush, the same footprint is wiped on the configured
 * number of floors above and/or below the one being drawn on.
 */
class EraseFloorsDialog : public wxDialog {
public:
	explicit EraseFloorsDialog(wxWindow* parent);
	~EraseFloorsDialog() override = default;

private:
	void OnToggleAbove(wxCommandEvent&);
	void OnToggleBelow(wxCommandEvent&);
	void OnClickOK(wxCommandEvent&);
	void OnClickCancel(wxCommandEvent&);

	// Greys out the floor-count spins whose toggle is off.
	void UpdateEnabledState();

	wxCheckBox* above_check;
	wxSpinCtrl* above_count;
	wxCheckBox* below_check;
	wxSpinCtrl* below_count;
	wxCheckBox* whole_tile_check;
};

#endif // RME_ERASE_FLOORS_DIALOG_H_
