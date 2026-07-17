//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

// ============================================================================
// Waypoint palette

#include "app/main.h"

#include "ui/gui.h"
#include "brushes/managers/brush_manager.h"
#include "editor/hotkey_manager.h"
#include "palette/palette_waypoints.h"
#include "brushes/waypoint/waypoint_brush.h"
#include "map/map.h"
#include "ui/positionctrl.h"
#include "util/image_manager.h"

#include <algorithm>
#include <wx/textdlg.h>

WaypointPalettePanel::WaypointPalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id),
	map(nullptr) {
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	// Search filter
	filter_text = newd wxTextCtrl(this, PALETTE_WAYPOINT_FILTER, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	filter_text->SetHint("Search waypoints...");
	topsizer->Add(filter_text, 0, wxEXPAND | wxALL, 4);

	// Waypoint list
	wxSizer* sidesizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Waypoints");

	waypoint_list = newd wxListCtrl(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_WAYPOINT_LISTBOX, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_EDIT_LABELS | wxLC_NO_HEADER);
	waypoint_list->InsertColumn(0, "UNNAMED", wxLIST_FORMAT_LEFT, 200);
	sidesizer->Add(waypoint_list, 1, wxEXPAND);

	// Buttons
	wxSizer* tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
	add_waypoint_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_WAYPOINT_ADD_WAYPOINT, "Add", wxDefaultPosition, wxSize(50, -1));
	add_waypoint_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_PLUS, wxSize(16, 16)));
	tmpsizer->Add(add_waypoint_button, 1, wxEXPAND);
	remove_waypoint_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_WAYPOINT_REMOVE_WAYPOINT, "Remove", wxDefaultPosition, wxSize(70, -1));
	remove_waypoint_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_MINUS, wxSize(16, 16)));
	tmpsizer->Add(remove_waypoint_button, 1, wxEXPAND);
	set_position_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_WAYPOINT_SET_POSITION, "Set Position", wxDefaultPosition, wxSize(90, -1));
	tmpsizer->Add(set_position_button, 1, wxEXPAND);
	sidesizer->Add(tmpsizer, 0, wxEXPAND);

	// Reorder buttons (drag & drop also works directly on the list)
	wxSizer* ordersizer = newd wxBoxSizer(wxHORIZONTAL);
	move_up_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_WAYPOINT_MOVE_UP, wxString::FromUTF8("\xE2\x86\x91 Up"), wxDefaultPosition, wxSize(50, -1));
	ordersizer->Add(move_up_button, 1, wxEXPAND);
	move_down_button = newd wxButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_WAYPOINT_MOVE_DOWN, wxString::FromUTF8("\xE2\x86\x93 Down"), wxDefaultPosition, wxSize(50, -1));
	ordersizer->Add(move_down_button, 1, wxEXPAND);
	sidesizer->Add(ordersizer, 0, wxEXPAND);

	topsizer->Add(sidesizer, 1, wxEXPAND);

	SetSizerAndFit(topsizer);

	// Event bindings
	Bind(wxEVT_BUTTON, &WaypointPalettePanel::OnClickAddWaypoint, this, PALETTE_WAYPOINT_ADD_WAYPOINT);
	Bind(wxEVT_BUTTON, &WaypointPalettePanel::OnClickRemoveWaypoint, this, PALETTE_WAYPOINT_REMOVE_WAYPOINT);
	Bind(wxEVT_BUTTON, &WaypointPalettePanel::OnClickSetPosition, this, PALETTE_WAYPOINT_SET_POSITION);
	Bind(wxEVT_BUTTON, &WaypointPalettePanel::OnClickMoveUp, this, PALETTE_WAYPOINT_MOVE_UP);
	Bind(wxEVT_BUTTON, &WaypointPalettePanel::OnClickMoveDown, this, PALETTE_WAYPOINT_MOVE_DOWN);

	Bind(wxEVT_LIST_BEGIN_LABEL_EDIT, &WaypointPalettePanel::OnBeginEditWaypointLabel, this, PALETTE_WAYPOINT_LISTBOX);
	Bind(wxEVT_LIST_END_LABEL_EDIT, &WaypointPalettePanel::OnEditWaypointLabel, this, PALETTE_WAYPOINT_LISTBOX);
	Bind(wxEVT_LIST_ITEM_SELECTED, &WaypointPalettePanel::OnClickWaypoint, this, PALETTE_WAYPOINT_LISTBOX);
	Bind(wxEVT_LIST_BEGIN_DRAG, &WaypointPalettePanel::OnBeginDrag, this, PALETTE_WAYPOINT_LISTBOX);

	// Drag reordering needs raw mouse events on the list itself.
	waypoint_list->Bind(wxEVT_MOTION, &WaypointPalettePanel::OnListMotion, this);
	waypoint_list->Bind(wxEVT_LEFT_UP, &WaypointPalettePanel::OnListLeftUp, this);
	waypoint_list->Bind(wxEVT_MOUSE_CAPTURE_LOST, &WaypointPalettePanel::OnListCaptureLost, this);

	Bind(wxEVT_TEXT, &WaypointPalettePanel::OnFilterTextChange, this, PALETTE_WAYPOINT_FILTER);
	filter_text->Bind(wxEVT_CHAR_HOOK, &WaypointPalettePanel::OnFilterCharHook, this);
}

void WaypointPalettePanel::OnSwitchIn() {
	PalettePanel::OnSwitchIn();
	filter_text->SetFocus();
}

void WaypointPalettePanel::OnSwitchOut() {
	PalettePanel::OnSwitchOut();
}

void WaypointPalettePanel::SetMap(Map* m) {
	map = m;
	this->Enable(m);
}

void WaypointPalettePanel::SelectFirstBrush() {
	// SelectWaypointBrush();
}

Brush* WaypointPalettePanel::GetSelectedBrush() const {
	Waypoint* wp = GetSelectedWaypoint();
	g_brush_manager.waypoint_brush->setWaypoint(wp);
	return g_brush_manager.waypoint_brush;
}

bool WaypointPalettePanel::SelectBrush(const Brush* whatbrush) {
	ASSERT(whatbrush == g_brush_manager.waypoint_brush);
	return false;
}

int WaypointPalettePanel::GetSelectedBrushSize() const {
	return 0;
}

PaletteType WaypointPalettePanel::GetType() const {
	return TILESET_WAYPOINT;
}

wxString WaypointPalettePanel::GetName() const {
	return "Waypoint Palette";
}

Waypoint* WaypointPalettePanel::GetSelectedWaypoint() const {
	if (!map) {
		return nullptr;
	}
	long item = waypoint_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item == -1) {
		return nullptr;
	}
	return map->waypoints.getWaypoint(nstr(waypoint_list->GetItemText(item)));
}

std::string WaypointPalettePanel::WaypointNameAtRow(long row) const {
	if (row < 0 || row >= waypoint_list->GetItemCount()) {
		return "";
	}
	return nstr(waypoint_list->GetItemText(row));
}

void WaypointPalettePanel::OnUpdate() {
	if (wxTextCtrl* tc = waypoint_list->GetEditControl()) {
		Waypoint* wp = map->waypoints.getWaypoint(nstr(tc->GetValue()));
		if (wp && wp->pos == Position()) {
			if (map->getTile(wp->pos)) {
				map->getTileL(wp->pos)->decreaseWaypointCount();
			}
			map->waypoints.removeWaypoint(wp->name);
		}
	}
	UpdateList();
}

void WaypointPalettePanel::UpdateList() {
	waypoint_list->Freeze();
	waypoint_list->DeleteAllItems();

	if (!map) {
		waypoint_list->Enable(false);
		add_waypoint_button->Enable(false);
		remove_waypoint_button->Enable(false);
		set_position_button->Enable(false);
		move_up_button->Enable(false);
		move_down_button->Enable(false);
		waypoint_list->Thaw();
		return;
	}

	waypoint_list->Enable(true);
	add_waypoint_button->Enable(true);
	remove_waypoint_button->Enable(true);
	set_position_button->Enable(true);
	move_up_button->Enable(true);
	move_down_button->Enable(true);

	// Get filter text
	std::string filter_lower = as_lower_str(nstr(filter_text->GetValue()));

	// Flat list following the manual order. Drag rows or use the Up/Down
	// buttons to reorder; new waypoints are always appended at the end.
	long idx = 0;
	for (Waypoint* wp : map->waypoints.getOrdered()) {
		if (!filter_lower.empty()) {
			if (as_lower_str(wp->name).find(filter_lower) == std::string::npos) {
				continue;
			}
		}
		waypoint_list->InsertItem(idx, wxstr(wp->name));
		waypoint_list->SetItemData(idx, 0);
		idx++;
	}

	waypoint_list->Thaw();
}

void WaypointPalettePanel::OnClickWaypoint(wxListEvent& event) {
	if (!map) {
		return;
	}

	std::string wpname = nstr(event.GetText());
	Waypoint* wp = map->waypoints.getWaypoint(wpname);
	if (wp) {
		g_gui.SetScreenCenterPosition(wp->pos);
		g_brush_manager.waypoint_brush->setWaypoint(wp);
	}
}

void WaypointPalettePanel::OnBeginEditWaypointLabel(wxListEvent& WXUNUSED(event)) {
	// We need to disable all hotkeys, so we can type properly
	g_hotkeys.DisableHotkeys();
}

void WaypointPalettePanel::OnEditWaypointLabel(wxListEvent& event) {
	std::string wpname = nstr(event.GetLabel());
	std::string oldwpname = nstr(waypoint_list->GetItemText(event.GetIndex()));
	Waypoint* wp = map->waypoints.getWaypoint(oldwpname);

	if (event.IsEditCancelled()) {
		g_hotkeys.EnableHotkeys();
		return;
	}

	if (wpname == "") {
		map->waypoints.removeWaypoint(oldwpname);
		g_gui.RefreshPalettes();
	} else if (wp) {
		if (wpname == oldwpname) {
			; // do nothing
		} else {
			if (map->waypoints.getWaypoint(wpname)) {
				// Already exists a waypoint with this name!
				g_gui.SetStatusText("There already is a waypoint with this name.");
				event.Veto();
				if (oldwpname == "") {
					map->waypoints.removeWaypoint(oldwpname);
					g_gui.RefreshPalettes();
				}
			} else {
				auto nwp_ptr = std::make_unique<Waypoint>(*wp);
				nwp_ptr->name = wpname;
				Waypoint* nwp = nwp_ptr.get();

				Waypoint* rwp = map->waypoints.getWaypoint(oldwpname);
				if (rwp) {
					if (map->getTile(rwp->pos)) {
						map->getTileL(rwp->pos)->decreaseWaypointCount();
					}
					map->waypoints.removeWaypoint(rwp->name);
				}

				map->waypoints.addWaypoint(std::move(nwp_ptr));
				g_brush_manager.waypoint_brush->setWaypoint(nwp);

				// Refresh other palettes
				refresh_timer.Start(300, true);
			}
		}
	}

	if (event.IsAllowed()) {
		g_hotkeys.EnableHotkeys();
	} else {
		g_hotkeys.EnableHotkeys();
	}
}

void WaypointPalettePanel::OnClickAddWaypoint(wxCommandEvent& event) {
	if (!map) {
		return;
	}

	// Suggest a unique default name so the user can just confirm.
	std::string default_name;
	int suffix = 1;
	do {
		default_name = "Waypoint " + std::to_string(suffix++);
	} while (map->waypoints.getWaypoint(default_name));

	// Default position: center of the map on the ground floor.
	Position default_pos(map->getWidth() / 2, map->getHeight() / 2, GROUND_LAYER);

	// Dialog to configure the name and position before the waypoint is created.
	wxDialog dlg(this, wxID_ANY, "Add Waypoint", wxDefaultPosition, wxDefaultSize);
	wxBoxSizer* main_sizer = newd wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer* grid = newd wxFlexGridSizer(2, 5, 5);
	grid->AddGrowableCol(1);

	grid->Add(newd wxStaticText(&dlg, wxID_ANY, "Name:"), 0, wxALIGN_CENTER_VERTICAL);
	wxTextCtrl* name_field = newd wxTextCtrl(&dlg, wxID_ANY, wxstr(default_name), wxDefaultPosition, wxSize(180, -1));
	grid->Add(name_field, 1, wxEXPAND);

	grid->Add(newd wxStaticText(&dlg, wxID_ANY, "X:"), 0, wxALIGN_CENTER_VERTICAL);
	wxSpinCtrl* spin_x = newd wxSpinCtrl(&dlg, wxID_ANY, std::to_string(default_pos.x), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535, default_pos.x);
	grid->Add(spin_x, 1, wxEXPAND);

	grid->Add(newd wxStaticText(&dlg, wxID_ANY, "Y:"), 0, wxALIGN_CENTER_VERTICAL);
	wxSpinCtrl* spin_y = newd wxSpinCtrl(&dlg, wxID_ANY, std::to_string(default_pos.y), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535, default_pos.y);
	grid->Add(spin_y, 1, wxEXPAND);

	grid->Add(newd wxStaticText(&dlg, wxID_ANY, "Z:"), 0, wxALIGN_CENTER_VERTICAL);
	wxSpinCtrl* spin_z = newd wxSpinCtrl(&dlg, wxID_ANY, std::to_string(default_pos.z), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 15, default_pos.z);
	grid->Add(spin_z, 1, wxEXPAND);

	main_sizer->Add(grid, 0, wxEXPAND | wxALL, 10);

	// Allow pasting a full "x, y, z" position (Ctrl+V) into any of the fields.
	EnablePositionPaste(spin_x, spin_y, spin_z);

	// OK / Cancel buttons
	wxStdDialogButtonSizer* btn_sizer = newd wxStdDialogButtonSizer();
	btn_sizer->AddButton(newd wxButton(&dlg, wxID_OK, "OK"));
	btn_sizer->AddButton(newd wxButton(&dlg, wxID_CANCEL, "Cancel"));
	btn_sizer->Realize();
	main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, 10);

	dlg.SetSizerAndFit(main_sizer);
	dlg.CenterOnParent();

	name_field->SetFocus();
	name_field->SelectAll();

	// Re-prompt until we get a valid, unique name (or the user cancels).
	std::string name;
	while (true) {
		if (dlg.ShowModal() != wxID_OK) {
			return;
		}

		name = nstr(name_field->GetValue());
		// Trim surrounding whitespace.
		name.erase(0, name.find_first_not_of(" \t"));
		if (const size_t last = name.find_last_not_of(" \t"); last != std::string::npos) {
			name.erase(last + 1);
		} else {
			name.clear();
		}

		if (name.empty()) {
			wxMessageBox("Waypoint name cannot be empty.", "Add Waypoint", wxOK | wxICON_WARNING, &dlg);
			name_field->SetFocus();
			continue;
		}
		if (map->waypoints.getWaypoint(name)) {
			wxMessageBox("There already is a waypoint with this name.", "Add Waypoint", wxOK | wxICON_WARNING, &dlg);
			name_field->SetFocus();
			name_field->SelectAll();
			continue;
		}
		break;
	}

	Position new_pos(spin_x->GetValue(), spin_y->GetValue(), spin_z->GetValue());

	auto wp_ptr = std::make_unique<Waypoint>(name, new_pos);
	// New waypoints are always appended at the end of the manual order.
	wp_ptr->order = map->waypoints.getNextOrder();
	Waypoint* wp = wp_ptr.get();
	map->waypoints.addWaypoint(std::move(wp_ptr));
	map->doChange();

	// Register the waypoint on its destination tile.
	if (new_pos != Position()) {
		Tile* new_tile = map->getTile(new_pos);
		if (!new_tile) {
			new_tile = map->createTile(new_pos.x, new_pos.y, new_pos.z);
		}
		new_tile->getLocation()->increaseWaypointCount();
	}

	g_brush_manager.waypoint_brush->setWaypoint(wp);

	// Refresh and select the new waypoint in the list.
	UpdateList();
	const long count = waypoint_list->GetItemCount();
	for (long i = 0; i < count; ++i) {
		if (waypoint_list->GetItemData(i) == 0 && nstr(waypoint_list->GetItemText(i)) == name) {
			waypoint_list->SetItemState(i, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			waypoint_list->EnsureVisible(i);
			break;
		}
	}
	g_gui.RefreshPalettes();

	g_gui.SetScreenCenterPosition(new_pos);
	g_gui.SetStatusText(wxString::Format("Waypoint '%s' created at (%d, %d, %d)", wxstr(name), new_pos.x, new_pos.y, new_pos.z));
}

void WaypointPalettePanel::OnClickRemoveWaypoint(wxCommandEvent& event) {
	if (!map) {
		return;
	}

	long item = waypoint_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item != -1) {
		Waypoint* wp = map->waypoints.getWaypoint(nstr(waypoint_list->GetItemText(item)));
		if (wp) {
			if (map->getTile(wp->pos)) {
				map->getTileL(wp->pos)->decreaseWaypointCount();
			}
			map->waypoints.removeWaypoint(wp->name);
		}
		waypoint_list->DeleteItem(item);
		// Keep order values compact after the removal.
		map->waypoints.normalizeOrder();
		map->doChange();
		refresh_timer.Start(300, true);
	}
}

void WaypointPalettePanel::OnClickSetPosition(wxCommandEvent& event) {
	if (!map) {
		return;
	}

	Waypoint* wp = GetSelectedWaypoint();
	if (!wp) {
		g_gui.SetStatusText("No waypoint selected.");
		return;
	}

	// Create a dialog to set position
	wxDialog dlg(this, wxID_ANY, wxString::Format("Set Position - %s", wxstr(wp->name)), wxDefaultPosition, wxDefaultSize);
	wxBoxSizer* main_sizer = newd wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer* grid = newd wxFlexGridSizer(2, 5, 5);
	grid->AddGrowableCol(1);

	grid->Add(newd wxStaticText(&dlg, wxID_ANY, "X:"), 0, wxALIGN_CENTER_VERTICAL);
	wxSpinCtrl* spin_x = newd wxSpinCtrl(&dlg, wxID_ANY, std::to_string(wp->pos.x), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535, wp->pos.x);
	grid->Add(spin_x, 1, wxEXPAND);

	grid->Add(newd wxStaticText(&dlg, wxID_ANY, "Y:"), 0, wxALIGN_CENTER_VERTICAL);
	wxSpinCtrl* spin_y = newd wxSpinCtrl(&dlg, wxID_ANY, std::to_string(wp->pos.y), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535, wp->pos.y);
	grid->Add(spin_y, 1, wxEXPAND);

	grid->Add(newd wxStaticText(&dlg, wxID_ANY, "Z:"), 0, wxALIGN_CENTER_VERTICAL);
	wxSpinCtrl* spin_z = newd wxSpinCtrl(&dlg, wxID_ANY, std::to_string(wp->pos.z), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 15, wp->pos.z);
	grid->Add(spin_z, 1, wxEXPAND);

	main_sizer->Add(grid, 0, wxEXPAND | wxALL, 10);

	// Allow pasting a full "x, y, z" position (Ctrl+V) into any of the fields.
	EnablePositionPaste(spin_x, spin_y, spin_z);

	// Current position info
	wxString current_pos = wxString::Format("Current: (%d, %d, %d)", wp->pos.x, wp->pos.y, wp->pos.z);
	main_sizer->Add(newd wxStaticText(&dlg, wxID_ANY, current_pos), 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

	// OK / Cancel buttons
	wxStdDialogButtonSizer* btn_sizer = newd wxStdDialogButtonSizer();
	btn_sizer->AddButton(newd wxButton(&dlg, wxID_OK, "OK"));
	btn_sizer->AddButton(newd wxButton(&dlg, wxID_CANCEL, "Cancel"));
	btn_sizer->Realize();
	main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, 10);

	dlg.SetSizerAndFit(main_sizer);
	dlg.CenterOnParent();

	if (dlg.ShowModal() == wxID_OK) {
		Position new_pos(spin_x->GetValue(), spin_y->GetValue(), spin_z->GetValue());

		// Decrease waypoint count on old tile
		if (wp->pos != Position()) {
			Tile* old_tile = map->getTile(wp->pos);
			if (old_tile) {
				old_tile->getLocation()->decreaseWaypointCount();
			}
		}

		// Update position
		wp->pos = new_pos;

		// Increase waypoint count on new tile
		if (new_pos != Position()) {
			Tile* new_tile = map->getTile(new_pos);
			if (!new_tile) {
				new_tile = map->createTile(new_pos.x, new_pos.y, new_pos.z);
			}
			new_tile->getLocation()->increaseWaypointCount();
		}

		map->doChange();

		// Navigate to the new position
		g_gui.SetScreenCenterPosition(new_pos);
		g_gui.SetStatusText(wxString::Format("Waypoint '%s' moved to (%d, %d, %d)", wxstr(wp->name), new_pos.x, new_pos.y, new_pos.z));
	}
}

void WaypointPalettePanel::FinishReorder(const std::string& select_name) {
	UpdateList();
	const long count = waypoint_list->GetItemCount();
	for (long i = 0; i < count; ++i) {
		if (as_lower_str(nstr(waypoint_list->GetItemText(i))) == as_lower_str(select_name)) {
			waypoint_list->SetItemState(i, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			waypoint_list->EnsureVisible(i);
			break;
		}
	}
}

void WaypointPalettePanel::MoveSelectedWaypoint(int delta) {
	if (!map || delta == 0) {
		return;
	}

	Waypoint* wp = GetSelectedWaypoint();
	if (!wp) {
		g_gui.SetStatusText("No waypoint selected.");
		return;
	}

	// Build the currently visible order (respecting the filter) so moving is
	// relative to what the user actually sees.
	std::string filter_lower = as_lower_str(nstr(filter_text->GetValue()));
	std::vector<Waypoint*> visible;
	for (Waypoint* w : map->waypoints.getOrdered()) {
		if (!filter_lower.empty() && as_lower_str(w->name).find(filter_lower) == std::string::npos) {
			continue;
		}
		visible.push_back(w);
	}

	auto it = std::find(visible.begin(), visible.end(), wp);
	if (it == visible.end()) {
		return;
	}
	const long idx = static_cast<long>(std::distance(visible.begin(), it));
	const long target = idx + delta;
	if (target < 0 || target >= static_cast<long>(visible.size())) {
		return; // Already at the edge.
	}

	// Swap the manual order of the two visible neighbours, then compact.
	std::swap(wp->order, visible[target]->order);
	map->waypoints.normalizeOrder();
	map->doChange();

	FinishReorder(wp->name);
}

void WaypointPalettePanel::OnClickMoveUp(wxCommandEvent& WXUNUSED(event)) {
	MoveSelectedWaypoint(-1);
}

void WaypointPalettePanel::OnClickMoveDown(wxCommandEvent& WXUNUSED(event)) {
	MoveSelectedWaypoint(1);
}

void WaypointPalettePanel::OnBeginDrag(wxListEvent& event) {
	if (!map) {
		return;
	}
	dragged_name = WaypointNameAtRow(event.GetIndex());
	if (dragged_name.empty()) {
		return;
	}
	dragging = true;
	if (!waypoint_list->HasCapture()) {
		waypoint_list->CaptureMouse();
	}
}

void WaypointPalettePanel::OnListCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event)) {
	// Capture can be yanked away (e.g. Alt-Tab). Abort any in-progress drag.
	dragging = false;
	dragged_name.clear();
}

void WaypointPalettePanel::OnListMotion(wxMouseEvent& event) {
	// Let the list handle normal hover/selection; drag drop is finalized on
	// left-up. Just keep default processing here.
	event.Skip();
}

void WaypointPalettePanel::OnListLeftUp(wxMouseEvent& event) {
	if (!dragging) {
		event.Skip();
		return;
	}
	dragging = false;
	if (waypoint_list->HasCapture()) {
		waypoint_list->ReleaseMouse();
	}

	if (!map || dragged_name.empty()) {
		event.Skip();
		return;
	}

	Waypoint* dragged = map->waypoints.getWaypoint(dragged_name);
	if (!dragged) {
		event.Skip();
		return;
	}

	// Figure out which row we dropped on.
	int flags = 0;
	long drop_row = waypoint_list->HitTest(event.GetPosition(), flags);
	std::string target_name = WaypointNameAtRow(drop_row);

	// Dropping onto itself is a no-op.
	if (as_lower_str(target_name) == as_lower_str(dragged_name)) {
		event.Skip();
		return;
	}

	// Rebuild the master order with the dragged waypoint moved to just before
	// the target row (or to the end when dropped past the last item).
	std::vector<Waypoint*> all = map->waypoints.getOrdered();
	all.erase(std::remove(all.begin(), all.end(), dragged), all.end());

	std::vector<Waypoint*>::iterator insert_at = all.end();
	if (!target_name.empty()) {
		Waypoint* target = map->waypoints.getWaypoint(target_name);
		auto tit = std::find(all.begin(), all.end(), target);
		if (tit != all.end()) {
			insert_at = tit;
		}
	}
	all.insert(insert_at, dragged);

	// Reassign compact order values following the new arrangement.
	for (size_t i = 0; i < all.size(); ++i) {
		all[i]->order = static_cast<int>(i);
	}
	map->doChange();

	FinishReorder(dragged_name);
	event.Skip();
}

void WaypointPalettePanel::OnFilterTextChange(wxCommandEvent& WXUNUSED(event)) {
	UpdateList();
}

void WaypointPalettePanel::OnFilterCharHook(wxKeyEvent& event) {
	int keycode = event.GetKeyCode();

	// For special keys, let them propagate normally
	if (keycode == WXK_ESCAPE || keycode == WXK_RETURN || keycode == WXK_NUMPAD_ENTER ||
		keycode == WXK_TAB || keycode == WXK_UP || keycode == WXK_DOWN ||
		keycode == WXK_LEFT || keycode == WXK_RIGHT || keycode == WXK_HOME ||
		keycode == WXK_END || keycode == WXK_DELETE || keycode == WXK_BACK ||
		keycode == WXK_PAGEUP || keycode == WXK_PAGEDOWN) {
		event.Skip();
		return;
	}

	// For Ctrl+key shortcuts, let them propagate
	if (event.ControlDown()) {
		event.Skip();
		return;
	}

	// For printable characters, consume the event to prevent the PaletteWindow's
	// wxChoicebook from changing tabs, then insert the character manually
	if (keycode >= WXK_SPACE && keycode <= 255) {
		wxChar ch = event.GetUnicodeKey();
		if (ch != WXK_NONE) {
			filter_text->WriteText(wxString(ch));
		}
		return;
	}

	event.Skip();
}
