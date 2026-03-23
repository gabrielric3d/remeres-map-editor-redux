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
#include "util/image_manager.h"

#include <algorithm>
#include <map>
#include <set>
#include <wx/textdlg.h>

namespace {

// Extract category from waypoint name.
// Names with format "Category (Detail)" or "Category Name" are grouped.
// Common multi-word prefixes are detected by looking for shared prefixes among all names.
std::string ExtractCategory(const std::string& name, const std::set<std::string>& known_categories) {
	for (const auto& cat : known_categories) {
		if (name.length() > cat.length() && name.substr(0, cat.length()) == cat) {
			char next = name[cat.length()];
			if (next == ' ' || next == '(') {
				return cat;
			}
		}
	}
	return "";
}

// Build category set from all waypoint names by finding common prefixes
std::set<std::string> BuildCategories(const Waypoints& waypoints) {
	std::map<std::string, int> prefix_counts;

	// Collect all names
	std::vector<std::string> names;
	for (const auto& [key, wp] : waypoints) {
		names.push_back(wp->name);
	}

	// For each pair of names, find shared word-boundary prefixes
	for (size_t i = 0; i < names.size(); ++i) {
		for (size_t j = i + 1; j < names.size(); ++j) {
			const std::string& a = names[i];
			const std::string& b = names[j];

			// Find common prefix length
			size_t common = 0;
			while (common < a.length() && common < b.length() && a[common] == b[common]) {
				++common;
			}

			// Truncate to last word boundary (space)
			size_t last_space = std::string::npos;
			for (size_t k = 0; k < common; ++k) {
				if (a[k] == ' ') {
					last_space = k;
				}
			}

			if (last_space != std::string::npos && last_space >= 2) {
				std::string prefix = a.substr(0, last_space);
				// Only count if both names are longer than the prefix (i.e., it's truly a category)
				if (a.length() > last_space + 1 && b.length() > last_space + 1) {
					prefix_counts[prefix]++;
				}
			}
		}
	}

	std::set<std::string> categories;
	for (const auto& [prefix, count] : prefix_counts) {
		if (count >= 1) {
			// Check that at least 2 names actually have this prefix
			int match_count = 0;
			for (const auto& name : names) {
				if (name.length() > prefix.length() && name.substr(0, prefix.length()) == prefix) {
					char next = name[prefix.length()];
					if (next == ' ' || next == '(') {
						match_count++;
					}
				}
			}
			if (match_count >= 2) {
				categories.insert(prefix);
			}
		}
	}

	// Remove categories that are subsets of longer categories with same members
	std::set<std::string> to_remove;
	for (const auto& short_cat : categories) {
		for (const auto& long_cat : categories) {
			if (long_cat.length() > short_cat.length() &&
				long_cat.substr(0, short_cat.length()) == short_cat) {
				// Check if all members of short_cat are also members of long_cat
				bool all_match = true;
				for (const auto& name : names) {
					if (name.length() > short_cat.length() &&
						name.substr(0, short_cat.length()) == short_cat &&
						(name[short_cat.length()] == ' ' || name[short_cat.length()] == '(')) {
						if (!(name.length() > long_cat.length() &&
							  name.substr(0, long_cat.length()) == long_cat)) {
							all_match = false;
							break;
						}
					}
				}
				if (all_match) {
					to_remove.insert(short_cat);
				}
			}
		}
	}
	for (const auto& r : to_remove) {
		categories.erase(r);
	}

	return categories;
}

} // namespace

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

	topsizer->Add(sidesizer, 1, wxEXPAND);

	SetSizerAndFit(topsizer);

	// Event bindings
	Bind(wxEVT_BUTTON, &WaypointPalettePanel::OnClickAddWaypoint, this, PALETTE_WAYPOINT_ADD_WAYPOINT);
	Bind(wxEVT_BUTTON, &WaypointPalettePanel::OnClickRemoveWaypoint, this, PALETTE_WAYPOINT_REMOVE_WAYPOINT);
	Bind(wxEVT_BUTTON, &WaypointPalettePanel::OnClickSetPosition, this, PALETTE_WAYPOINT_SET_POSITION);

	Bind(wxEVT_LIST_BEGIN_LABEL_EDIT, &WaypointPalettePanel::OnBeginEditWaypointLabel, this, PALETTE_WAYPOINT_LISTBOX);
	Bind(wxEVT_LIST_END_LABEL_EDIT, &WaypointPalettePanel::OnEditWaypointLabel, this, PALETTE_WAYPOINT_LISTBOX);
	Bind(wxEVT_LIST_ITEM_SELECTED, &WaypointPalettePanel::OnClickWaypoint, this, PALETTE_WAYPOINT_LISTBOX);

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
	// Skip category headers (bold items have data = 1)
	if (waypoint_list->GetItemData(item) == 1) {
		return nullptr;
	}
	return map->waypoints.getWaypoint(nstr(waypoint_list->GetItemText(item)));
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
		waypoint_list->Thaw();
		return;
	}

	waypoint_list->Enable(true);
	add_waypoint_button->Enable(true);
	remove_waypoint_button->Enable(true);
	set_position_button->Enable(true);

	Waypoints& waypoints = map->waypoints;

	// Get filter text
	std::string filter = nstr(filter_text->GetValue());
	std::string filter_lower = as_lower_str(filter);

	// Build categories
	std::set<std::string> categories = BuildCategories(waypoints);

	// Organize waypoints into categories
	std::map<std::string, std::vector<Waypoint*>> categorized;
	std::vector<Waypoint*> uncategorized;

	for (const auto& [name, wp] : waypoints) {
		// Apply filter
		if (!filter_lower.empty()) {
			std::string name_lower = as_lower_str(wp->name);
			if (name_lower.find(filter_lower) == std::string::npos) {
				continue;
			}
		}

		std::string cat = ExtractCategory(wp->name, categories);
		if (cat.empty()) {
			uncategorized.push_back(wp.get());
		} else {
			categorized[cat].push_back(wp.get());
		}
	}

	// Sort uncategorized alphabetically
	std::sort(uncategorized.begin(), uncategorized.end(), [](const Waypoint* a, const Waypoint* b) {
		return a->name < b->name;
	});

	long idx = 0;

	// Add categorized waypoints
	for (const auto& [cat, wps] : categorized) {
		// Category header
		long header_idx = waypoint_list->InsertItem(idx, wxString::Format("--- %s ---", wxstr(cat)));
		waypoint_list->SetItemData(header_idx, 1); // Mark as category header
		wxFont font = waypoint_list->GetFont();
		font.SetWeight(wxFONTWEIGHT_BOLD);
		wxListItem item;
		item.SetId(header_idx);
		item.SetFont(font);
		item.SetTextColour(wxColour(180, 180, 100));
		waypoint_list->SetItem(item);
		idx++;

		// Sort waypoints within category
		auto sorted_wps = wps;
		std::sort(sorted_wps.begin(), sorted_wps.end(), [](const Waypoint* a, const Waypoint* b) {
			return a->name < b->name;
		});

		for (const auto& wp : sorted_wps) {
			waypoint_list->InsertItem(idx, wxstr(wp->name));
			waypoint_list->SetItemData(idx, 0);
			idx++;
		}
	}

	// Add uncategorized waypoints
	if (!uncategorized.empty() && !categorized.empty()) {
		long header_idx = waypoint_list->InsertItem(idx, "--- Other ---");
		waypoint_list->SetItemData(header_idx, 1);
		wxFont font = waypoint_list->GetFont();
		font.SetWeight(wxFONTWEIGHT_BOLD);
		wxListItem item;
		item.SetId(header_idx);
		item.SetFont(font);
		item.SetTextColour(wxColour(180, 180, 100));
		waypoint_list->SetItem(item);
		idx++;
	}

	for (const auto& wp : uncategorized) {
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

	// Skip category headers
	if (waypoint_list->GetItemData(event.GetIndex()) == 1) {
		waypoint_list->SetItemState(event.GetIndex(), 0, wxLIST_STATE_SELECTED);
		return;
	}

	std::string wpname = nstr(event.GetText());
	Waypoint* wp = map->waypoints.getWaypoint(wpname);
	if (wp) {
		g_gui.SetScreenCenterPosition(wp->pos);
		g_brush_manager.waypoint_brush->setWaypoint(wp);
	}
}

void WaypointPalettePanel::OnBeginEditWaypointLabel(wxListEvent& event) {
	// Don't allow editing category headers
	if (waypoint_list->GetItemData(event.GetIndex()) == 1) {
		event.Veto();
		return;
	}
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
	if (map) {
		map->waypoints.addWaypoint(std::make_unique<Waypoint>());
		long i = waypoint_list->InsertItem(0, "");
		waypoint_list->SetItemData(i, 0);
		waypoint_list->EditLabel(i);
	}
}

void WaypointPalettePanel::OnClickRemoveWaypoint(wxCommandEvent& event) {
	if (!map) {
		return;
	}

	long item = waypoint_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item != -1 && waypoint_list->GetItemData(item) != 1) {
		Waypoint* wp = map->waypoints.getWaypoint(nstr(waypoint_list->GetItemText(item)));
		if (wp) {
			if (map->getTile(wp->pos)) {
				map->getTileL(wp->pos)->decreaseWaypointCount();
			}
			map->waypoints.removeWaypoint(wp->name);
		}
		waypoint_list->DeleteItem(item);
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

		// Navigate to the new position
		g_gui.SetScreenCenterPosition(new_pos);
		g_gui.SetStatusText(wxString::Format("Waypoint '%s' moved to (%d, %d, %d)", wxstr(wp->name), new_pos.x, new_pos.y, new_pos.z));
	}
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
