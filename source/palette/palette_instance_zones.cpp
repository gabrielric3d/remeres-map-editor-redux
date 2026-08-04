//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// ============================================================================
// Instance zone palette (BlackTalon)

#include "app/main.h"

#include "ui/gui.h"
#include "brushes/managers/brush_manager.h"
#include "brushes/instance_zone/instance_zone_brush.h"
#include "palette/palette_instance_zones.h"
#include "map/map.h"
#include "game/instance_zones.h"
#include "app/settings.h"
#include "ui/main_frame.h"
#include "ui/main_menubar.h"

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>

// ----------------------------------------------------------------------------
// Small modal dialog to edit a zone's name + instance count.
namespace {
	class EditInstanceZoneDialog : public wxDialog {
	public:
		EditInstanceZoneDialog(wxWindow* parent, InstanceZone* zone) :
			wxDialog(parent, wxID_ANY, "Instance Zone Properties", wxDefaultPosition, wxDefaultSize),
			zone(zone) {
			auto* top = new wxBoxSizer(wxVERTICAL);

			auto* grid = new wxFlexGridSizer(2, 2, 6, 6);
			grid->AddGrowableCol(1, 1);

			grid->Add(new wxStaticText(this, wxID_ANY, "Name:"), 0, wxALIGN_CENTER_VERTICAL);
			name_field = new wxTextCtrl(this, wxID_ANY, wxstr(zone->name), wxDefaultPosition, wxSize(220, -1));
			grid->Add(name_field, 1, wxEXPAND);

			grid->Add(new wxStaticText(this, wxID_ANY, "Instances:"), 0, wxALIGN_CENTER_VERTICAL);
			// Lower bound is 1: a zone with 0 instances would be an area nobody can
			// be in. 1 = painted but behaving exactly like today (world instance).
			count_field = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(220, -1),
				wxSP_ARROW_KEYS, 1, 255, zone->instances);
			grid->Add(count_field, 1, wxEXPAND);

			top->Add(grid, 1, wxEXPAND | wxALL, 10);

			auto* hint = new wxStaticText(this, wxID_ANY,
				"Instances = how many parallel copies of this area run over the SAME\n"
				"coordinates. Ground and walls are shared; only creatures, items and\n"
				"effects are separated. 1 = not instanced.");
			hint->SetForegroundColour(wxColour(128, 128, 128));
			top->Add(hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

			top->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 6);

			SetSizerAndFit(top);
			Bind(wxEVT_BUTTON, &EditInstanceZoneDialog::OnClickOK, this, wxID_OK);
		}

		void OnClickOK(wxCommandEvent& event) {
			zone->name = nstr(name_field->GetValue());
			zone->instances = static_cast<uint16_t>(count_field->GetValue());
			EndModal(wxID_OK);
		}

	private:
		InstanceZone* zone;
		wxTextCtrl* name_field;
		wxSpinCtrl* count_field;
	};

	wxString describeZone(const InstanceZone* zone) {
		wxString label = wxstr(zone->name);
		label << " (ID: " << zone->id << "; Instances: " << zone->instances << ")";
		return label;
	}
} // namespace

// ----------------------------------------------------------------------------

InstanceZonePalettePanel::InstanceZonePalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id),
	map(nullptr) {
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	wxSizer* sidesizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Instance Zones");

	zone_list = newd wxListBox(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_INSTANCEZONE_LISTBOX, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	sidesizer->Add(zone_list, 1, wxEXPAND);

	wxSizer* tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
	add_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_INSTANCEZONE_ADD, "Add", wxDefaultPosition, wxSize(50, -1));
	tmpsizer->Add(add_button, 1, wxEXPAND);
	edit_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_INSTANCEZONE_EDIT, "Edit", wxDefaultPosition, wxSize(50, -1));
	tmpsizer->Add(edit_button, 1, wxEXPAND);
	remove_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_INSTANCEZONE_REMOVE, "Remove", wxDefaultPosition, wxSize(70, -1));
	tmpsizer->Add(remove_button, 1, wxEXPAND);
	recenter_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_INSTANCEZONE_RECENTER, "Recenter", wxDefaultPosition, wxSize(80, -1));
	tmpsizer->Add(recenter_button, 1, wxEXPAND);
	sidesizer->Add(tmpsizer, 0, wxEXPAND);

	// Toggle for the map overlay, mirroring the View menu entry. Same setting, so
	// flipping it here moves the menu check too (see OnToggleShow). Parented to the
	// static box like the buttons above it, so it sits inside the same group.
	show_toggle = newd wxCheckBox(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_INSTANCEZONE_TOGGLE_SHOW, "Show instance zones on map");
	show_toggle->SetToolTip("Tint the painted tiles and draw each zone name on the map. Same as View > Show instance zones.");
	show_toggle->SetValue(g_settings.getBoolean(Config::SHOW_INSTANCE_ZONES));
	sidesizer->Add(show_toggle, 0, wxEXPAND | wxTOP, 4);

	// Preenchimento opaco em vez do tint multiplicativo. Depende do toggle acima:
	// com as zonas escondidas nao ha o que preencher (ver map_display.cpp).
	solid_toggle = newd wxCheckBox(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_INSTANCEZONE_TOGGLE_SOLID, "Paint zones solid (hides terrain)");
	solid_toggle->SetToolTip("Fill each zone with a solid block of its color instead of a translucent tint. The outline becomes obvious, but the selection and brush preview underneath stop showing. Same as View > Instance zones: solid fill.");
	solid_toggle->SetValue(g_settings.getBoolean(Config::INSTANCE_ZONE_SOLID_FILL));
	sidesizer->Add(solid_toggle, 0, wxEXPAND | wxTOP, 2);

	topsizer->Add(sidesizer, 1, wxEXPAND);

	auto* hint = newd wxStaticText(this, wxID_ANY, "Select a zone, then paint the tiles that belong to it. Each zone paints in its own color.");
	hint->SetForegroundColour(wxColour(128, 128, 128));
	topsizer->Add(hint, 0, wxEXPAND | wxALL, 4);

	SetSizerAndFit(topsizer);

	Bind(wxEVT_BUTTON, &InstanceZonePalettePanel::OnClickAdd, this, PALETTE_INSTANCEZONE_ADD);
	Bind(wxEVT_BUTTON, &InstanceZonePalettePanel::OnClickEdit, this, PALETTE_INSTANCEZONE_EDIT);
	Bind(wxEVT_BUTTON, &InstanceZonePalettePanel::OnClickRemove, this, PALETTE_INSTANCEZONE_REMOVE);
	Bind(wxEVT_BUTTON, &InstanceZonePalettePanel::OnClickRecenter, this, PALETTE_INSTANCEZONE_RECENTER);
	Bind(wxEVT_LISTBOX, &InstanceZonePalettePanel::OnClickZone, this, PALETTE_INSTANCEZONE_LISTBOX);
	Bind(wxEVT_LISTBOX_DCLICK, &InstanceZonePalettePanel::OnDoubleClickZone, this, PALETTE_INSTANCEZONE_LISTBOX);
	Bind(wxEVT_CHECKBOX, &InstanceZonePalettePanel::OnToggleShow, this, PALETTE_INSTANCEZONE_TOGGLE_SHOW);
	Bind(wxEVT_CHECKBOX, &InstanceZonePalettePanel::OnToggleSolid, this, PALETTE_INSTANCEZONE_TOGGLE_SOLID);
}

void InstanceZonePalettePanel::OnSwitchIn() {
	PalettePanel::OnSwitchIn();
	// The View menu can change this behind our back, so re-read on every switch in.
	if (show_toggle) {
		show_toggle->SetValue(g_settings.getBoolean(Config::SHOW_INSTANCE_ZONES));
	}
	if (solid_toggle) {
		solid_toggle->SetValue(g_settings.getBoolean(Config::INSTANCE_ZONE_SOLID_FILL));
	}
}

void InstanceZonePalettePanel::SetMap(Map* m) {
	map = m;
	this->Enable(m != nullptr);
	UpdateList();
}

void InstanceZonePalettePanel::SelectFirstBrush() {
	//
}

wxString InstanceZonePalettePanel::GetName() const {
	return "Instance Palette";
}

PaletteType InstanceZonePalettePanel::GetType() const {
	return TILESET_INSTANCE_ZONE;
}

int InstanceZonePalettePanel::GetSelectedBrushSize() const {
	return 0;
}

Brush* InstanceZonePalettePanel::GetSelectedBrush() const {
	g_brush_manager.instance_zone_brush->setInstanceZone(GetSelectedZoneId());
	return g_brush_manager.instance_zone_brush;
}

bool InstanceZonePalettePanel::SelectBrush(const Brush* whatbrush) {
	return whatbrush == g_brush_manager.instance_zone_brush;
}

uint32_t InstanceZonePalettePanel::GetSelectedZoneId() const {
	int sel = zone_list->GetSelection();
	if (sel == wxNOT_FOUND || sel < 0 || static_cast<size_t>(sel) >= row_ids.size()) {
		return 0;
	}
	return row_ids[sel];
}

void InstanceZonePalettePanel::UpdateList(uint32_t select_id) {
	zone_list->Clear();
	row_ids.clear();
	if (!map) {
		return;
	}

	int select_row = wxNOT_FOUND;
	for (InstanceZone* zone : map->instance_zones.getOrdered()) {
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

void InstanceZonePalettePanel::OnUpdate() {
	UpdateList(GetSelectedZoneId());
}

void InstanceZonePalettePanel::OnClickZone(wxCommandEvent& WXUNUSED(event)) {
	// Selecting a zone makes the instance brush paint it. Configure the brush
	// directly and use the no-arg SelectBrush() (house palette pattern): it pulls
	// GetSelectedBrush() from this active palette, which (re)sets the zone id.
	// The 2-arg SelectBrush(brush, type) would select the brush WITHOUT asking
	// the palette, leaving draw_zone_id = 0 -> draw() paints nothing.
	g_brush_manager.instance_zone_brush->setInstanceZone(GetSelectedZoneId());
	g_gui.SelectBrush();
}

void InstanceZonePalettePanel::OnDoubleClickZone(wxCommandEvent& WXUNUSED(event)) {
	wxCommandEvent dummy;
	OnClickEdit(dummy);
}

void InstanceZonePalettePanel::OnClickAdd(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}
	InstanceZone* zone = map->instance_zones.createZone();
	if (!zone) {
		return;
	}
	map->doChange();
	UpdateList(zone->id);
	// Same pattern as OnClickZone: configure the brush, then no-arg SelectBrush().
	g_brush_manager.instance_zone_brush->setInstanceZone(zone->id);
	g_gui.SelectBrush();
}

void InstanceZonePalettePanel::OnClickEdit(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}
	const uint32_t id = GetSelectedZoneId();
	if (id == 0) {
		return;
	}
	InstanceZone* zone = map->instance_zones.getZone(id);
	if (!zone) {
		return;
	}
	EditInstanceZoneDialog dialog(g_gui.root, zone);
	if (dialog.ShowModal() == wxID_OK) {
		map->doChange();
		UpdateList(id);
	}
}

void InstanceZonePalettePanel::OnClickRemove(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}
	const uint32_t id = GetSelectedZoneId();
	if (id == 0) {
		return;
	}
	// O id vive em DOIS lugares: no tile (OTBM attr 25) e aqui no metadata. Remover
	// so o metadata deixava a area carimbada, e como o id e um espaco GLOBAL ela
	// passava a responder como a zona de outro mapa que usasse o mesmo numero --
	// silenciosamente, porque o server le o TILE. Contar antes de perguntar porque
	// a escala e o que decide a resposta: mapa de producao chega a milhoes de tiles
	// marcados, e ali "eu despinto depois na mao" nao e uma opcao real.
	const size_t painted = map->instance_zones.countPaintedTiles(id);

	size_t cleared = 0;
	if (painted == 0) {
		if (wxMessageBox("Remove this instance zone?", "Remove Instance Zone",
				wxYES_NO | wxICON_QUESTION | wxCENTER, this) != wxYES) {
			return;
		}
	} else {
		wxString question;
		question << "Remove this instance zone?\n\n"
			<< static_cast<unsigned long>(painted) << " tile(s) are painted with it.\n\n"
			<< "Yes     -  remove the zone AND clear those tiles (cannot be undone)\n"
			<< "No      -  remove only the zone, leaving the tiles painted\n"
			<< "Cancel  -  keep everything";
		const int answer = wxMessageBox(question, "Remove Instance Zone",
			wxYES_NO | wxCANCEL | wxICON_WARNING | wxCENTER, this);
		if (answer == wxCANCEL) {
			return;
		}
		// wxNO mantem o comportamento antigo de proposito: e o caminho de RENUMERAR
		// (tirar o metadata, repintar os tiles com outro id depois). So deixou de ser
		// o default, que era o que fazia a pintura sobreviver sem ninguem perceber.
		if (answer == wxYES) {
			cleared = map->instance_zones.clearPaintedTiles(id);
		}
	}

	map->instance_zones.removeZone(id);
	map->doChange();
	UpdateList();
	if (cleared > 0) {
		// Redesenha: o tint da zona sai dos tiles no mesmo instante.
		g_gui.RefreshView();
	}
}

void InstanceZonePalettePanel::OnClickRecenter(wxCommandEvent& WXUNUSED(event)) {
	if (!map) {
		return;
	}
	// The incremental bounds only ever grow, so erasing tiles from a zone edge
	// leaves the map label off-center. This walks the map once and rebuilds every
	// zone box from what is actually painted.
	map->instance_zones.recalculateBounds();
	g_gui.RefreshView();
}

void InstanceZonePalettePanel::OnToggleSolid(wxCommandEvent& WXUNUSED(event)) {
	g_settings.setInteger(Config::INSTANCE_ZONE_SOLID_FILL, solid_toggle->GetValue() ? 1 : 0);
	if (g_gui.root) {
		if (MainMenuBar* menu = g_gui.root->GetMainMenuBar()) {
			menu->LoadValues();
		}
	}
	g_gui.RefreshView();
}

void InstanceZonePalettePanel::OnToggleShow(wxCommandEvent& WXUNUSED(event)) {
	g_settings.setInteger(Config::SHOW_INSTANCE_ZONES, show_toggle->GetValue() ? 1 : 0);
	// Keep the View menu check in sync -- it reads the same setting, and leaving
	// the two disagreeing is worse than the extra call.
	if (g_gui.root) {
		if (MainMenuBar* menu = g_gui.root->GetMainMenuBar()) {
			menu->LoadValues();
		}
	}
	g_gui.RefreshView();
}
