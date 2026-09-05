//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/dialogs/ghost_floors_dialog.h"
#include "app/settings.h"
#include "ui/gui.h"
#include "util/image_manager.h"

#include <wx/spinctrl.h>
#include <wx/slider.h>
#include <wx/statline.h>
#include <algorithm>
#include <string>

// ============================================================================
// Ghost Floors Dialog
// Configures the "Ghost Floors" toggle of the radial wheel: like Ghost Higher
// Floors (Ctrl+L), but for N floors above and/or below the current one.

namespace {

// Spin range: 1..MAP_MAX_LAYER. MAP_MAX_LAYER reaches every floor there is, so the
// "All" checkbox simply pins the count to it.
constexpr int ALL_FLOORS = MAP_MAX_LAYER;

} // namespace

GhostFloorsDialog::GhostFloorsDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Ghost Floors", wxDefaultPosition, wxDefaultSize) {
	original.enabled = g_settings.getInteger(Config::GHOST_FLOORS_ENABLED);
	original.above_enabled = g_settings.getInteger(Config::GHOST_FLOORS_ABOVE_ENABLED);
	original.below_enabled = g_settings.getInteger(Config::GHOST_FLOORS_BELOW_ENABLED);
	original.above_count = g_settings.getInteger(Config::GHOST_FLOORS_ABOVE_COUNT);
	original.below_count = g_settings.getInteger(Config::GHOST_FLOORS_BELOW_COUNT);
	original.alpha = g_settings.getInteger(Config::GHOST_FLOORS_ALPHA);
	original.fade = g_settings.getInteger(Config::GHOST_FLOORS_FADE);

	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	wxStaticText* intro = newd wxStaticText(this, wxID_ANY,
		"Draws the floors around the one you are editing as translucent ghosts,\n"
		"like Ghost Higher Floors (Ctrl+L) but in both directions and as deep as\n"
		"you want. Changes apply to the map right away.");
	sizer->Add(intro, 0, wxALL, 12);

	enabled_check = newd wxCheckBox(this, wxID_ANY, "Ghost Floors active");
	enabled_check->SetToolTip("Same as the Ghost Floors toggle in the radial wheel.");
	sizer->Add(enabled_check, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

	sizer->Add(newd wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

	// ---- Floors above ----
	wxSizer* above_sizer = newd wxBoxSizer(wxHORIZONTAL);
	above_check = newd wxCheckBox(this, wxID_ANY, "Floors above");
	above_check->SetToolTip("Ghost the floors on top of the current one (lower Z).");
	above_sizer->Add(above_check, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	above_sizer->Add(newd wxStaticText(this, wxID_ANY, "Count:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	above_count = newd wxSpinCtrl(this, wxID_ANY, "1", wxDefaultPosition, wxSize(70, -1),
		wxSP_ARROW_KEYS, 1, ALL_FLOORS, 1);
	above_sizer->Add(above_count, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	above_all_check = newd wxCheckBox(this, wxID_ANY, "All");
	above_all_check->SetToolTip("Every floor above, up to the top of the map.");
	above_sizer->Add(above_all_check, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(above_sizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

	// ---- Floors below ----
	wxSizer* below_sizer = newd wxBoxSizer(wxHORIZONTAL);
	below_check = newd wxCheckBox(this, wxID_ANY, "Floors below");
	below_check->SetToolTip("Ghost the floors under the current one (higher Z), drawn over it so\nthey stay visible even where the current floor has ground.");
	below_sizer->Add(below_check, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	below_sizer->Add(newd wxStaticText(this, wxID_ANY, "Count:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	below_count = newd wxSpinCtrl(this, wxID_ANY, "1", wxDefaultPosition, wxSize(70, -1),
		wxSP_ARROW_KEYS, 1, ALL_FLOORS, 1);
	below_sizer->Add(below_count, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	below_all_check = newd wxCheckBox(this, wxID_ANY, "All");
	below_all_check->SetToolTip("Every floor below, down to the bottom of the map.");
	below_sizer->Add(below_all_check, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(below_sizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

	sizer->Add(newd wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

	// ---- Look ----
	wxSizer* alpha_sizer = newd wxBoxSizer(wxHORIZONTAL);
	alpha_sizer->Add(newd wxStaticText(this, wxID_ANY, "Opacity:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	alpha_slider = newd wxSlider(this, wxID_ANY, 96, 8, 255, wxDefaultPosition, wxSize(180, -1));
	alpha_slider->SetToolTip("How solid the ghost floors are drawn. Ghost Higher Floors (Ctrl+L) uses 38%.");
	alpha_sizer->Add(alpha_slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
	alpha_label = newd wxStaticText(this, wxID_ANY, "100%", wxDefaultPosition, wxSize(40, -1), wxALIGN_RIGHT);
	alpha_sizer->Add(alpha_label, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(alpha_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

	fade_check = newd wxCheckBox(this, wxID_ANY, "Fade with distance (farther floors are fainter)");
	sizer->Add(fade_check, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

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
	enabled_check->SetValue(original.enabled != 0);
	above_check->SetValue(original.above_enabled != 0);
	below_check->SetValue(original.below_enabled != 0);
	const int above = std::clamp(original.above_count, 1, ALL_FLOORS);
	const int below = std::clamp(original.below_count, 1, ALL_FLOORS);
	above_count->SetValue(above);
	below_count->SetValue(below);
	above_all_check->SetValue(above == ALL_FLOORS);
	below_all_check->SetValue(below == ALL_FLOORS);
	alpha_slider->SetValue(std::clamp(original.alpha, 8, 255));
	fade_check->SetValue(original.fade != 0);
	RefreshAlphaLabel();
	UpdateEnabledState();

	enabled_check->Bind(wxEVT_CHECKBOX, &GhostFloorsDialog::OnSettingChanged, this);
	above_check->Bind(wxEVT_CHECKBOX, &GhostFloorsDialog::OnSettingChanged, this);
	below_check->Bind(wxEVT_CHECKBOX, &GhostFloorsDialog::OnSettingChanged, this);
	above_all_check->Bind(wxEVT_CHECKBOX, &GhostFloorsDialog::OnSettingChanged, this);
	below_all_check->Bind(wxEVT_CHECKBOX, &GhostFloorsDialog::OnSettingChanged, this);
	fade_check->Bind(wxEVT_CHECKBOX, &GhostFloorsDialog::OnSettingChanged, this);
	above_count->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { ApplyToSettings(); });
	below_count->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { ApplyToSettings(); });
	alpha_slider->Bind(wxEVT_SLIDER, &GhostFloorsDialog::OnAlphaChanged, this);
	ok_button->Bind(wxEVT_BUTTON, &GhostFloorsDialog::OnClickOK, this);
	cancel_button->Bind(wxEVT_BUTTON, &GhostFloorsDialog::OnClickCancel, this);
	// Closing with the window's X counts as Cancel (undoes the live preview).
	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
		wxCommandEvent dummy;
		OnClickCancel(dummy);
	});

	wxIcon icon;
	icon.CopyFromBitmap(IMAGE_MANAGER.GetBitmap(ICON_GHOST, wxSize(32, 32)));
	SetIcon(icon);
}

void GhostFloorsDialog::UpdateEnabledState() {
	const bool on = enabled_check->GetValue();
	above_check->Enable(on);
	below_check->Enable(on);
	alpha_slider->Enable(on);
	alpha_label->Enable(on);
	fade_check->Enable(on);

	const bool above_on = on && above_check->GetValue();
	above_all_check->Enable(above_on);
	above_count->Enable(above_on && !above_all_check->GetValue());

	const bool below_on = on && below_check->GetValue();
	below_all_check->Enable(below_on);
	below_count->Enable(below_on && !below_all_check->GetValue());
}

void GhostFloorsDialog::RefreshAlphaLabel() {
	const int percent = (alpha_slider->GetValue() * 100 + 127) / 255;
	alpha_label->SetLabel(std::to_string(percent) + "%");
}

void GhostFloorsDialog::ApplyToSettings() {
	const int above = above_all_check->GetValue() ? ALL_FLOORS : above_count->GetValue();
	const int below = below_all_check->GetValue() ? ALL_FLOORS : below_count->GetValue();

	g_settings.setInteger(Config::GHOST_FLOORS_ENABLED, enabled_check->GetValue() ? 1 : 0);
	g_settings.setInteger(Config::GHOST_FLOORS_ABOVE_ENABLED, above_check->GetValue() ? 1 : 0);
	g_settings.setInteger(Config::GHOST_FLOORS_BELOW_ENABLED, below_check->GetValue() ? 1 : 0);
	g_settings.setInteger(Config::GHOST_FLOORS_ABOVE_COUNT, above);
	g_settings.setInteger(Config::GHOST_FLOORS_BELOW_COUNT, below);
	g_settings.setInteger(Config::GHOST_FLOORS_ALPHA, alpha_slider->GetValue());
	g_settings.setInteger(Config::GHOST_FLOORS_FADE, fade_check->GetValue() ? 1 : 0);
	g_gui.RefreshView();
}

void GhostFloorsDialog::OnSettingChanged(wxCommandEvent&) {
	UpdateEnabledState();
	ApplyToSettings();
}

void GhostFloorsDialog::OnAlphaChanged(wxCommandEvent&) {
	RefreshAlphaLabel();
	ApplyToSettings();
}

void GhostFloorsDialog::OnClickOK(wxCommandEvent&) {
	ApplyToSettings();
	g_settings.save();
	EndModal(1);
}

void GhostFloorsDialog::OnClickCancel(wxCommandEvent&) {
	// Undo the live preview.
	g_settings.setInteger(Config::GHOST_FLOORS_ENABLED, original.enabled);
	g_settings.setInteger(Config::GHOST_FLOORS_ABOVE_ENABLED, original.above_enabled);
	g_settings.setInteger(Config::GHOST_FLOORS_BELOW_ENABLED, original.below_enabled);
	g_settings.setInteger(Config::GHOST_FLOORS_ABOVE_COUNT, original.above_count);
	g_settings.setInteger(Config::GHOST_FLOORS_BELOW_COUNT, original.below_count);
	g_settings.setInteger(Config::GHOST_FLOORS_ALPHA, original.alpha);
	g_settings.setInteger(Config::GHOST_FLOORS_FADE, original.fade);
	g_gui.RefreshView();
	EndModal(0);
}
