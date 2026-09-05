//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/dialogs/erase_floors_dialog.h"
#include "app/settings.h"
#include "ui/gui.h"
#include "util/image_manager.h"

#include <wx/spinctrl.h>
#include <wx/statline.h>
#include <algorithm>

// ============================================================================
// Erase Floors Dialog
// Configures the "Erase Above" / "Erase Below" toggles of the radial wheel:
// how many floors each one reaches and whether the whole tile is wiped.

EraseFloorsDialog::EraseFloorsDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Erase Floors", wxDefaultPosition, wxDefaultSize) {
	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	wxStaticText* intro = newd wxStaticText(this, wxID_ANY,
		"While erasing (Ctrl + brush), the brush footprint is also wiped\n"
		"on the floors selected here. Both directions can be on at once.");
	sizer->Add(intro, 0, wxALL, 12);

	// ---- Floors above ----
	wxSizer* above_sizer = newd wxBoxSizer(wxHORIZONTAL);
	above_check = newd wxCheckBox(this, wxID_ANY, "Erase floors above");
	above_check->SetToolTip("Also erase the floors on top of the one you are drawing on (lower Z).");
	above_sizer->Add(above_check, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	above_sizer->Add(newd wxStaticText(this, wxID_ANY, "Floors:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	above_count = newd wxSpinCtrl(this, wxID_ANY, "1", wxDefaultPosition, wxSize(70, -1),
		wxSP_ARROW_KEYS, 1, MAP_MAX_LAYER, 1);
	above_sizer->Add(above_count, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(above_sizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

	// ---- Floors below ----
	wxSizer* below_sizer = newd wxBoxSizer(wxHORIZONTAL);
	below_check = newd wxCheckBox(this, wxID_ANY, "Erase floors below");
	below_check->SetToolTip("Also erase the floors under the one you are drawing on (higher Z) —\nuse it to take out the mountain sitting below the ground you remove.");
	below_sizer->Add(below_check, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	below_sizer->Add(newd wxStaticText(this, wxID_ANY, "Floors:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	below_count = newd wxSpinCtrl(this, wxID_ANY, "1", wxDefaultPosition, wxSize(70, -1),
		wxSP_ARROW_KEYS, 1, MAP_MAX_LAYER, 1);
	below_sizer->Add(below_count, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(below_sizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

	sizer->Add(newd wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

	// ---- What gets erased ----
	whole_tile_check = newd wxCheckBox(this, wxID_ANY, "Erase whole tile (items and walls, not just ground)");
	whole_tile_check->SetToolTip(
		"Off: only the ground and its auto-borders are removed, then the neighbors re-borderize.\n"
		"On: the tile is wiped like the Eraser brush would (items, walls and decoration included).\n"
		"The \"Eraser leaves unique items\" setting still applies."
	);
	sizer->Add(whole_tile_check, 0, wxALL, 12);

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

	// Load the current settings.
	above_check->SetValue(g_settings.getBoolean(Config::ERASE_FLOORS_ABOVE_ENABLED));
	below_check->SetValue(g_settings.getBoolean(Config::ERASE_FLOORS_BELOW_ENABLED));
	above_count->SetValue(std::clamp(g_settings.getInteger(Config::ERASE_FLOORS_ABOVE_COUNT), 1, MAP_MAX_LAYER));
	below_count->SetValue(std::clamp(g_settings.getInteger(Config::ERASE_FLOORS_BELOW_COUNT), 1, MAP_MAX_LAYER));
	whole_tile_check->SetValue(g_settings.getBoolean(Config::ERASE_FLOORS_WHOLE_TILE));
	UpdateEnabledState();

	above_check->Bind(wxEVT_CHECKBOX, &EraseFloorsDialog::OnToggleAbove, this);
	below_check->Bind(wxEVT_CHECKBOX, &EraseFloorsDialog::OnToggleBelow, this);
	ok_button->Bind(wxEVT_BUTTON, &EraseFloorsDialog::OnClickOK, this);
	cancel_button->Bind(wxEVT_BUTTON, &EraseFloorsDialog::OnClickCancel, this);

	wxIcon icon;
	icon.CopyFromBitmap(IMAGE_MANAGER.GetBitmap(ICON_ERASER, wxSize(32, 32)));
	SetIcon(icon);
}

void EraseFloorsDialog::UpdateEnabledState() {
	above_count->Enable(above_check->GetValue());
	below_count->Enable(below_check->GetValue());
}

void EraseFloorsDialog::OnToggleAbove(wxCommandEvent&) {
	UpdateEnabledState();
}

void EraseFloorsDialog::OnToggleBelow(wxCommandEvent&) {
	UpdateEnabledState();
}

void EraseFloorsDialog::OnClickOK(wxCommandEvent&) {
	g_settings.setInteger(Config::ERASE_FLOORS_ABOVE_ENABLED, above_check->GetValue() ? 1 : 0);
	g_settings.setInteger(Config::ERASE_FLOORS_BELOW_ENABLED, below_check->GetValue() ? 1 : 0);
	g_settings.setInteger(Config::ERASE_FLOORS_ABOVE_COUNT, above_count->GetValue());
	g_settings.setInteger(Config::ERASE_FLOORS_BELOW_COUNT, below_count->GetValue());
	g_settings.setInteger(Config::ERASE_FLOORS_WHOLE_TILE, whole_tile_check->GetValue() ? 1 : 0);
	g_settings.save();
	EndModal(1);
}

void EraseFloorsDialog::OnClickCancel(wxCommandEvent&) {
	EndModal(0);
}
