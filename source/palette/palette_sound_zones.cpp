//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// ============================================================================
// Sound zone palette (BlackTalon)

#include "app/main.h"

#include "ui/gui.h"
#include "brushes/managers/brush_manager.h"
#include "brushes/sound_zone/sound_zone_brush.h"
#include "palette/palette_sound_zones.h"
#include "map/map.h"
#include "game/sound_zones.h"
#include "app/settings.h"
#include "ui/main_frame.h"
#include "ui/main_menubar.h"

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>

// ----------------------------------------------------------------------------
// Small modal dialog to edit a zone's name + track key.
namespace {
	class EditSoundZoneDialog : public wxDialog {
	public:
		EditSoundZoneDialog(wxWindow* parent, SoundZone* zone) :
			wxDialog(parent, wxID_ANY, "Sound Zone Properties", wxDefaultPosition, wxDefaultSize),
			zone(zone) {
			auto* top = new wxBoxSizer(wxVERTICAL);

			auto* grid = new wxFlexGridSizer(2, 2, 6, 6);
			grid->AddGrowableCol(1, 1);

			grid->Add(new wxStaticText(this, wxID_ANY, "Name:"), 0, wxALIGN_CENTER_VERTICAL);
			name_field = new wxTextCtrl(this, wxID_ANY, wxstr(zone->name), wxDefaultPosition, wxSize(220, -1));
			grid->Add(name_field, 1, wxEXPAND);

			grid->Add(new wxStaticText(this, wxID_ANY, "Sound track:"), 0, wxALIGN_CENTER_VERTICAL);
			track_field = new wxTextCtrl(this, wxID_ANY, wxstr(zone->track), wxDefaultPosition, wxSize(220, -1));
			grid->Add(track_field, 1, wxEXPAND);

			top->Add(grid, 1, wxEXPAND | wxALL, 10);

			auto* hint = new wxStaticText(this, wxID_ANY, "Track = sound key the client plays for this area (e.g. ambient_forest).");
			hint->SetForegroundColour(wxColour(128, 128, 128));
			top->Add(hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

			top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 6);

			SetSizerAndFit(top);
			Bind(wxEVT_BUTTON, &EditSoundZoneDialog::OnClickOK, this, wxID_OK);
		}

		void OnClickOK(wxCommandEvent& event) {
			zone->name = nstr(name_field->GetValue());
			zone->track = nstr(track_field->GetValue());
			EndModal(wxID_OK);
		}

	private:
		SoundZone* zone;
		wxTextCtrl* name_field;
		wxTextCtrl* track_field;
	};

	wxString describeZone(const SoundZone* zone) {
		wxString label = wxstr(zone->name);
		label << " (ID: " << zone->id;
		if (!zone->track.empty()) {
			label << "; Track: " << wxstr(zone->track);
		}
		label << ")";
		return label;
	}
} // namespace

// ----------------------------------------------------------------------------

SoundZonePalettePanel::SoundZonePalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id),
	map(nullptr) {
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	wxSizer* sidesizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Sound Zones");

	zone_list = newd wxListBox(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_SOUNDZONE_LISTBOX, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	sidesizer->Add(zone_list, 1, wxEXPAND);

	wxSizer* tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
	add_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_SOUNDZONE_ADD, "Add", wxDefaultPosition, wxSize(50, -1));
	tmpsizer->Add(add_button, 1, wxEXPAND);
	edit_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_SOUNDZONE_EDIT, "Edit", wxDefaultPosition, wxSize(50, -1));
	tmpsizer->Add(edit_button, 1, wxEXPAND);
	remove_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_SOUNDZONE_REMOVE, "Remove", wxDefaultPosition, wxSize(70, -1));
	tmpsizer->Add(remove_button, 1, wxEXPAND);
	recenter_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_SOUNDZONE_RECENTER, "Recenter", wxDefaultPosition, wxSize(80, -1));
	tmpsizer->Add(recenter_button, 1, wxEXPAND);
	sidesizer->Add(tmpsizer, 0, wxEXPAND);

	// Toggle for the map overlay, mirroring the View menu entry. Same setting, so
	// flipping it here moves the menu check too (see OnToggleShow). Parented to the
	// static box like the buttons above it, so it sits inside the same group.
	show_toggle = newd wxCheckBox(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_SOUNDZONE_TOGGLE_SHOW, "Show sound zones on map");
	show_toggle->SetToolTip("Tint the painted tiles of every sound zone. Same as View > Show sound zones.");
	show_toggle->SetValue(g_settings.getBoolean(Config::SHOW_SOUND_ZONES));
	sidesizer->Add(show_toggle, 0, wxEXPAND | wxTOP, 4);

	topsizer->Add(sidesizer, 1, wxEXPAND);

	auto* hint = newd wxStaticText(this, wxID_ANY, "Select a zone, then paint tiles to assign the ambient sound. Each zone paints in its own color.");
	hint->SetForegroundColour(wxColour(128, 128, 128));
	topsizer->Add(hint, 0, wxEXPAND | wxALL, 4);

	SetSizerAndFit(topsizer);

	Bind(wxEVT_BUTTON, &SoundZonePalettePanel::OnClickAdd, this, PALETTE_SOUNDZONE_ADD);
	Bind(wxEVT_BUTTON, &SoundZonePalettePanel::OnClickEdit, this, PALETTE_SOUNDZONE_EDIT);
	Bind(wxEVT_BUTTON, &SoundZonePalettePanel::OnClickRemove, this, PALETTE_SOUNDZONE_REMOVE);
	Bind(wxEVT_LISTBOX, &SoundZonePalettePanel::OnClickZone, this, PALETTE_SOUNDZONE_LISTBOX);
	Bind(wxEVT_LISTBOX_DCLICK, &SoundZonePalettePanel::OnDoubleClickZone, this, PALETTE_SOUNDZONE_LISTBOX);
	Bind(wxEVT_BUTTON, &SoundZonePalettePanel::OnClickRecenter, this, PALETTE_SOUNDZONE_RECENTER);
	Bind(wxEVT_CHECKBOX, &SoundZonePalettePanel::OnToggleShow, this, PALETTE_SOUNDZONE_TOGGLE_SHOW);
}

void SoundZonePalettePanel::OnSwitchIn() {
	PalettePanel::OnSwitchIn();
	// The View menu can change this behind our back, so re-read on every switch in.
	if (show_toggle) {
		show_toggle->SetValue(g_settings.getBoolean(Config::SHOW_SOUND_ZONES));
	}
}

void SoundZonePalettePanel::SetMap(Map* m) {
	map = m;
	this->Enable(m != nullptr);
	UpdateList();
}

void SoundZonePalettePanel::SelectFirstBrush() {
	//
}

wxString SoundZonePalettePanel::GetName() const {
	return "Sound Palette";
}

PaletteType SoundZonePalettePanel::GetType() const {
	return TILESET_SOUND_ZONE;
}

int SoundZonePalettePanel::GetSelectedBrushSize() const {
	return 0;
}

Brush* SoundZonePalettePanel::GetSelectedBrush() const {
	g_brush_manager.sound_zone_brush->setSoundZone(GetSelectedZoneId());
	return g_brush_manager.sound_zone_brush;
}

bool SoundZonePalettePanel::SelectBrush(const Brush* whatbrush) {
	return whatbrush == g_brush_manager.sound_zone_brush;
}

uint32_t SoundZonePalettePanel::GetSelectedZoneId() const {
	int sel = zone_list->GetSelection();
	if (sel == wxNOT_FOUND || sel < 0 || static_cast<size_t>(sel) >= row_ids.size()) {
		return 0;
	}
	return row_ids[sel];
}

void SoundZonePalettePanel::UpdateList(uint32_t select_id) {
	zone_list->Clear();
	row_ids.clear();
	if (!map) {
		return;
	}

	int select_row = wxNOT_FOUND;
	for (SoundZone* zone : map->sound_zones.getOrdered()) {
		const int row = zone_list->Append(describeZone(zone));
		row_ids.push_back(zone->id);
		if (zone->id == select_id) {
			select_row = row;
		}
	}
	if (select_row != wxNOT_FOUND) {
		zone_list->SetSelection(select_row);
	}
}

void SoundZonePalettePanel::OnUpdate() {
	UpdateList(GetSelectedZoneId());
}

void SoundZonePalettePanel::OnClickZone(wxCommandEvent& WXUNUSED(event)) {
	// Selecting a zone makes the sound brush paint it. Configure the brush
	// directly and use the no-arg SelectBrush() (house palette pattern): it pulls
	// GetSelectedBrush() from this active palette, which (re)sets the zone id.
	// The 2-arg SelectBrush(brush, type) would select the brush WITHOUT asking
	// the palette, leaving draw_zone_id = 0 -> draw() paints nothing.
	g_brush_manager.sound_zone_brush->setSoundZone(GetSelectedZoneId());
	g_gui.SelectBrush();
}

void SoundZonePalettePanel::OnDoubleClickZone(wxCommandEvent& WXUNUSED(event)) {
	wxCommandEvent dummy;
	OnClickEdit(dummy);
}

void SoundZonePalettePanel::OnClickAdd(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}
	SoundZone* zone = map->sound_zones.createZone();
	if (!zone) {
		return;
	}
	map->doChange();
	UpdateList(zone->id);
	// Same pattern as OnClickZone: configure the brush, then no-arg SelectBrush().
	g_brush_manager.sound_zone_brush->setSoundZone(zone->id);
	g_gui.SelectBrush();
}

void SoundZonePalettePanel::OnClickEdit(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}
	const uint32_t id = GetSelectedZoneId();
	if (id == 0) {
		return;
	}
	SoundZone* zone = map->sound_zones.getZone(id);
	if (!zone) {
		return;
	}
	EditSoundZoneDialog dialog(g_gui.root, zone);
	if (dialog.ShowModal() == wxID_OK) {
		map->doChange();
		UpdateList(id);
	}
}

void SoundZonePalettePanel::OnClickRemove(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}
	const uint32_t id = GetSelectedZoneId();
	if (id == 0) {
		return;
	}
	const int answer = wxMessageBox("Remove this sound zone? Tiles painted with it will keep the id until repainted.",
		"Remove Sound Zone", wxYES_NO | wxICON_WARNING | wxCENTER, this);
	if (answer != wxYES) {
		return;
	}
	map->sound_zones.removeZone(id);
	map->doChange();
	UpdateList();
}

void SoundZonePalettePanel::OnToggleShow(wxCommandEvent& WXUNUSED(event)) {
	g_settings.setInteger(Config::SHOW_SOUND_ZONES, show_toggle->GetValue() ? 1 : 0);
	// Keep the View menu check in sync -- it reads the same setting, and leaving
	// the two disagreeing is worse than the extra call.
	if (g_gui.root) {
		if (MainMenuBar* menu = g_gui.root->GetMainMenuBar()) {
			menu->LoadValues();
		}
	}
	g_gui.RefreshView();
}

void SoundZonePalettePanel::OnClickRecenter(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}
	// The incremental bounds only ever grow, so erasing tiles from a zone edge
	// leaves the map label off-center. This walks the map once and rebuilds every
	// zone box from what is actually painted.
	map->sound_zones.recalculateBounds();
	g_gui.RefreshView();
}
