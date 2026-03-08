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

#include "main.h"

#include "brush_manager_panel.h"
#include "brush.h"
#include "gui.h"
#include "settings.h"
#include "sprites.h"
#include "theme.h"

namespace {

// UI constants
constexpr int kItemHeight = 28;
constexpr int kIconSize = 24;
constexpr int kPadding = 4;
constexpr int kCheckboxWidth = 20;

// Status indicator colors (not part of theme system yet)
const wxColour kActiveColour(0x40, 0xC0, 0x40);   // Green for active
const wxColour kInactiveColour(0xC0, 0x40, 0x40); // Red for inactive

} // namespace

// ============================================================================
// BrushManagerListBox Implementation
// ============================================================================

BEGIN_EVENT_TABLE(BrushManagerListBox, wxVListBox)
	EVT_LEFT_DOWN(BrushManagerListBox::OnMouseDown)
	EVT_LEFT_DCLICK(BrushManagerListBox::OnMouseDoubleClick)
	EVT_KEY_DOWN(BrushManagerListBox::OnKeyDown)
END_EVENT_TABLE()

BrushManagerListBox::BrushManagerListBox(wxWindow* parent, wxWindowID id) :
	wxVListBox(parent, id, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE),
	item_height(kItemHeight),
	icon_size(kIconSize)
{
	const ThemeColors& theme = Theme::Dark();
	SetBackgroundColour(theme.surface);
	SetOwnForegroundColour(theme.text);
}

BrushManagerListBox::~BrushManagerListBox()
{
	// Nothing to clean up - brushes are owned by g_brushes
}

void BrushManagerListBox::SetBrushes(const std::vector<Brush*>& newBrushes)
{
	brushes = newBrushes;
	SetItemCount(brushes.size());
	RefreshList();
}

Brush* BrushManagerListBox::GetBrush(size_t index) const
{
	if (index < brushes.size()) {
		return brushes[index];
	}
	return nullptr;
}

std::vector<Brush*> BrushManagerListBox::GetSelectedBrushes() const
{
	std::vector<Brush*> selected;
	int sel = GetSelection();
	if (sel != wxNOT_FOUND && sel < static_cast<int>(brushes.size())) {
		selected.push_back(brushes[sel]);
	}
	return selected;
}

void BrushManagerListBox::SelectAll()
{
	// wxVListBox with wxLB_SINGLE doesn't support multiple selection
	// This is a placeholder for potential future multi-select support
}

void BrushManagerListBox::DeselectAll()
{
	SetSelection(wxNOT_FOUND);
}

void BrushManagerListBox::RefreshList()
{
	Refresh();
}

void BrushManagerListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const
{
	if (n >= brushes.size()) {
		return;
	}

	Brush* brush = brushes[n];
	if (!brush) {
		return;
	}

	// Draw background using theme colors
	const ThemeColors& theme = Theme::Dark();
	dc.SetPen(*wxTRANSPARENT_PEN);
	if (IsSelected(n)) {
		dc.SetBrush(wxBrush(theme.controlActive));
	} else {
		dc.SetBrush(wxBrush(theme.surface));
	}
	dc.DrawRectangle(rect);

	// Draw activation status indicator (checkbox-like)
	int checkboxX = rect.GetX() + kPadding;
	int checkboxY = rect.GetY() + (rect.GetHeight() - 16) / 2;

	if (brush->isActivated()) {
		dc.SetBrush(wxBrush(kActiveColour));
		dc.SetPen(wxPen(kActiveColour.IsOk() ? kActiveColour : *wxGREEN, 1));
	} else {
		dc.SetBrush(wxBrush(kInactiveColour));
		dc.SetPen(wxPen(kInactiveColour.IsOk() ? kInactiveColour : *wxRED, 1));
	}
	dc.DrawRectangle(checkboxX, checkboxY, 16, 16);

	// Draw checkmark if active
	if (brush->isActivated()) {
		dc.SetPen(wxPen(*wxWHITE, 2));
		dc.DrawLine(checkboxX + 3, checkboxY + 8, checkboxX + 6, checkboxY + 12);
		dc.DrawLine(checkboxX + 6, checkboxY + 12, checkboxX + 13, checkboxY + 4);
	}

	// Draw sprite icon
	int iconX = checkboxX + kCheckboxWidth + kPadding;
	int iconY = rect.GetY() + (rect.GetHeight() - icon_size) / 2;

	int look_id = brush->getLookID();
	if (look_id > 0) {
		Sprite* spr = g_gui.gfx.getSprite(look_id);
		if (spr) {
			spr->DrawTo(&dc, SPRITE_SIZE_32x32, iconX, iconY, icon_size, icon_size);
		}
	}

	// Draw brush name using theme colors
	if (IsSelected(n)) {
		dc.SetTextForeground(*wxWHITE);
	} else {
		dc.SetTextForeground(theme.text);
	}

	int textX = iconX + icon_size + kPadding;
	int textY = rect.GetY() + (rect.GetHeight() - dc.GetCharHeight()) / 2;

	wxString displayName = wxstr(brush->getName());
	if (!brush->isActivated()) {
		displayName += " (disabled)";
	}

	dc.DrawText(displayName, textX, textY);
}

wxCoord BrushManagerListBox::OnMeasureItem(size_t n) const
{
	return item_height;
}

void BrushManagerListBox::OnDrawBackground(wxDC& dc, const wxRect& rect, size_t n) const
{
	// Background is handled in OnDrawItem
}

void BrushManagerListBox::OnMouseDown(wxMouseEvent& event)
{
	int item = HitTest(event.GetPosition());
	if (item != wxNOT_FOUND && item < static_cast<int>(brushes.size())) {
		// Check if click was on the checkbox area
		// Checkbox is at the start of each item row
		int checkboxX = kPadding;
		int checkboxEndX = checkboxX + kCheckboxWidth;

		if (event.GetX() >= checkboxX && event.GetX() < checkboxEndX) {
			// Toggle brush activation
			Brush* brush = brushes[item];
			if (brush) {
				brush->toggleActivatedInXML();
				// Notify parent panel about the toggle for auto-reload handling
				BrushManagerPanel* panel = dynamic_cast<BrushManagerPanel*>(GetParent());
				if (panel) {
					panel->OnBrushActivationToggled();
				} else {
					RefreshList();
				}
			}
			return;
		}
	}

	event.Skip();
}

void BrushManagerListBox::OnMouseDoubleClick(wxMouseEvent& event)
{
	int item = HitTest(event.GetPosition());
	if (item != wxNOT_FOUND && item < static_cast<int>(brushes.size())) {
		Brush* brush = brushes[item];
		if (brush) {
			brush->toggleActivatedInXML();
			// Notify parent panel about the toggle for auto-reload handling
			BrushManagerPanel* panel = dynamic_cast<BrushManagerPanel*>(GetParent());
			if (panel) {
				panel->OnBrushActivationToggled();
			} else {
				RefreshList();
			}
		}
	}
}

void BrushManagerListBox::OnKeyDown(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_SPACE) {
		int sel = GetSelection();
		if (sel != wxNOT_FOUND && sel < static_cast<int>(brushes.size())) {
			Brush* brush = brushes[sel];
			if (brush) {
				brush->toggleActivatedInXML();
				// Notify parent panel about the toggle for auto-reload handling
				BrushManagerPanel* panel = dynamic_cast<BrushManagerPanel*>(GetParent());
				if (panel) {
					panel->OnBrushActivationToggled();
				} else {
					RefreshList();
				}
			}
		}
	} else {
		event.Skip();
	}
}

// ============================================================================
// BrushManagerPanel Implementation
// ============================================================================

BEGIN_EVENT_TABLE(BrushManagerPanel, wxPanel)
	EVT_COMBOBOX(BRUSH_MANAGER_TYPE_FILTER, BrushManagerPanel::OnTypeFilterChanged)
	EVT_COMBOBOX(BRUSH_MANAGER_STATUS_FILTER, BrushManagerPanel::OnStatusFilterChanged)
	EVT_TEXT(BRUSH_MANAGER_SEARCH, BrushManagerPanel::OnSearchChanged)
	EVT_BUTTON(BRUSH_MANAGER_TOGGLE_BTN, BrushManagerPanel::OnToggleSelected)
	EVT_BUTTON(BRUSH_MANAGER_RELOAD_BTN, BrushManagerPanel::OnReloadBrushes)
	EVT_CHECKBOX(BRUSH_MANAGER_AUTO_RELOAD_CHECKBOX, BrushManagerPanel::OnAutoReloadCheckbox)
	// Note: EVT_LISTBOX and EVT_LISTBOX_DCLICK are not used for wxVListBox
	// Selection and double-click are handled directly in BrushManagerListBox via mouse events
END_EVENT_TABLE()

BrushManagerPanel::BrushManagerPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id),
	type_filter(nullptr),
	status_filter(nullptr),
	search_box(nullptr),
	brush_list(nullptr),
	toggle_button(nullptr),
	reload_button(nullptr),
	auto_reload_checkbox(nullptr)
{
	CreateControls();
	RefreshBrushList();
}

BrushManagerPanel::~BrushManagerPanel()
{
	// Controls are managed by wxWidgets
}

void BrushManagerPanel::CreateControls()
{
	wxBoxSizer* mainSizer = newd wxBoxSizer(wxVERTICAL);

	CreateFilterBar();
	CreateBrushList();
	CreateButtonBar();

	// Add filter bar
	wxStaticBoxSizer* filterSizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Filters");

	// Type filter row
	wxBoxSizer* typeRow = newd wxBoxSizer(wxHORIZONTAL);
	typeRow->Add(newd wxStaticText(this, wxID_ANY, "Type:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	typeRow->Add(type_filter, 1, wxEXPAND);
	filterSizer->Add(typeRow, 0, wxEXPAND | wxALL, 3);

	// Status filter row
	wxBoxSizer* statusRow = newd wxBoxSizer(wxHORIZONTAL);
	statusRow->Add(newd wxStaticText(this, wxID_ANY, "Status:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	statusRow->Add(status_filter, 1, wxEXPAND);
	filterSizer->Add(statusRow, 0, wxEXPAND | wxALL, 3);

	// Search row
	wxBoxSizer* searchRow = newd wxBoxSizer(wxHORIZONTAL);
	searchRow->Add(newd wxStaticText(this, wxID_ANY, "Search:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	searchRow->Add(search_box, 1, wxEXPAND);
	filterSizer->Add(searchRow, 0, wxEXPAND | wxALL, 3);

	mainSizer->Add(filterSizer, 0, wxEXPAND | wxALL, 5);

	// Add brush list
	wxStaticBoxSizer* listSizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Brushes");
	listSizer->Add(brush_list, 1, wxEXPAND | wxALL, 3);
	mainSizer->Add(listSizer, 1, wxEXPAND | wxALL, 5);

	// Add button bar
	wxBoxSizer* buttonSizer = newd wxBoxSizer(wxHORIZONTAL);
	buttonSizer->Add(toggle_button, 0, wxALL, 3);
	buttonSizer->Add(reload_button, 0, wxALL, 3);
	buttonSizer->AddStretchSpacer();
	buttonSizer->Add(auto_reload_checkbox, 0, wxALIGN_CENTER_VERTICAL | wxALL, 3);
	mainSizer->Add(buttonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	SetSizer(mainSizer);
}

void BrushManagerPanel::CreateFilterBar()
{
	// Type filter combo box
	wxArrayString typeChoices;
	typeChoices.Add("All Types");
	typeChoices.Add("Raw");
	typeChoices.Add("Doodad");
	typeChoices.Add("Ground");
	typeChoices.Add("Wall");
	typeChoices.Add("Wall Decoration");
	typeChoices.Add("Table");
	typeChoices.Add("Carpet");
	typeChoices.Add("Door");
	typeChoices.Add("Creature");
	typeChoices.Add("Spawn");

	type_filter = newd wxComboBox(this, BRUSH_MANAGER_TYPE_FILTER, "All Types",
		wxDefaultPosition, wxDefaultSize, typeChoices, wxCB_READONLY);
	type_filter->SetSelection(0);

	// Status filter combo box
	wxArrayString statusChoices;
	statusChoices.Add("All");
	statusChoices.Add("Active Only");
	statusChoices.Add("Inactive Only");

	status_filter = newd wxComboBox(this, BRUSH_MANAGER_STATUS_FILTER, "All",
		wxDefaultPosition, wxDefaultSize, statusChoices, wxCB_READONLY);
	status_filter->SetSelection(0);

	// Search box
	search_box = newd wxTextCtrl(this, BRUSH_MANAGER_SEARCH, wxEmptyString,
		wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	search_box->SetHint("Search brushes...");

	// Disable global hotkeys when search box has focus to prevent interference while typing
	search_box->Bind(wxEVT_SET_FOCUS, [](wxFocusEvent& event) {
		g_gui.DisableHotkeys();
		event.Skip();
	});
	search_box->Bind(wxEVT_KILL_FOCUS, [](wxFocusEvent& event) {
		g_gui.EnableHotkeys();
		event.Skip();
	});
}

void BrushManagerPanel::CreateBrushList()
{
	brush_list = newd BrushManagerListBox(this, BRUSH_MANAGER_LISTBOX);
	brush_list->SetMinSize(wxSize(-1, 200));
}

void BrushManagerPanel::CreateButtonBar()
{
	toggle_button = newd wxButton(this, BRUSH_MANAGER_TOGGLE_BTN, "Toggle Selected");
	toggle_button->SetToolTip("Toggle activation state of selected brush");

	reload_button = newd wxButton(this, BRUSH_MANAGER_RELOAD_BTN, "Reload Brushes");
	reload_button->SetToolTip("Reload all brush definitions from XML files");

	auto_reload_checkbox = newd wxCheckBox(this, BRUSH_MANAGER_AUTO_RELOAD_CHECKBOX, "Auto-reload");
	auto_reload_checkbox->SetToolTip("Automatically reload brushes after toggling activation");
	// Initialize checkbox state from saved setting
	auto_reload_checkbox->SetValue(g_settings.getBoolean(Config::BRUSH_MANAGER_AUTO_RELOAD));
}

void BrushManagerPanel::RefreshBrushList()
{
	all_brushes = g_brushes.getAllBrushes();
	ApplyFilters();
}

void BrushManagerPanel::ApplyFilters()
{
	filtered_brushes.clear();

	BrushType typeFilter = GetTypeFilter();
	BrushStatusFilter statusFilter = GetStatusFilter();
	wxString searchText = GetSearchFilter().Lower();

	for (Brush* brush : all_brushes) {
		if (!brush) continue;

		// Apply type filter
		if (typeFilter != BRUSH_TYPE_UNKNOWN) {
			if (brush->getBrushType() != typeFilter) {
				continue;
			}
		}

		// Apply status filter
		if (statusFilter == BRUSH_STATUS_ACTIVE && !brush->isActivated()) {
			continue;
		}
		if (statusFilter == BRUSH_STATUS_INACTIVE && brush->isActivated()) {
			continue;
		}

		// Apply search filter
		if (!searchText.IsEmpty()) {
			wxString brushName = wxstr(brush->getName()).Lower();
			if (brushName.Find(searchText) == wxNOT_FOUND) {
				continue;
			}
		}

		filtered_brushes.push_back(brush);
	}

	brush_list->SetBrushes(filtered_brushes);
}

void BrushManagerPanel::ReloadBrushes()
{
	// Clear brush references before reload to avoid dangling pointers
	filtered_brushes.clear();
	all_brushes.clear();
	brush_list->SetBrushes(filtered_brushes);

	wxString error;
	wxArrayString warnings;

	if (!g_gui.ReloadBrushes(error, warnings)) {
		wxMessageBox("Failed to reload brushes: " + error, "Error", wxOK | wxICON_ERROR, this);
	} else {
		if (!warnings.IsEmpty()) {
			wxString warningMsg = "Brushes reloaded with warnings:\n";
			for (const wxString& warning : warnings) {
				warningMsg += warning + "\n";
			}
			wxMessageBox(warningMsg, "Warning", wxOK | wxICON_WARNING, this);
		}
		RefreshBrushList();
	}
}

bool BrushManagerPanel::IsAutoReloadEnabled() const
{
	return auto_reload_checkbox && auto_reload_checkbox->GetValue();
}

void BrushManagerPanel::SetAutoReloadEnabled(bool enabled)
{
	if (auto_reload_checkbox) {
		auto_reload_checkbox->SetValue(enabled);
	}
}

BrushType BrushManagerPanel::GetTypeFilter() const
{
	if (!type_filter) return BRUSH_TYPE_UNKNOWN;

	int sel = type_filter->GetSelection();
	switch (sel) {
		case 0: return BRUSH_TYPE_UNKNOWN; // All Types
		case 1: return BRUSH_TYPE_RAW;
		case 2: return BRUSH_TYPE_DOODAD;
		case 3: return BRUSH_TYPE_GROUND;
		case 4: return BRUSH_TYPE_WALL;
		case 5: return BRUSH_TYPE_WALL_DECORATION;
		case 6: return BRUSH_TYPE_TABLE;
		case 7: return BRUSH_TYPE_CARPET;
		case 8: return BRUSH_TYPE_DOOR;
		case 9: return BRUSH_TYPE_CREATURE;
		case 10: return BRUSH_TYPE_SPAWN;
		default: return BRUSH_TYPE_UNKNOWN;
	}
}

BrushStatusFilter BrushManagerPanel::GetStatusFilter() const
{
	if (!status_filter) return BRUSH_STATUS_ALL;

	int sel = status_filter->GetSelection();
	switch (sel) {
		case 0: return BRUSH_STATUS_ALL;
		case 1: return BRUSH_STATUS_ACTIVE;
		case 2: return BRUSH_STATUS_INACTIVE;
		default: return BRUSH_STATUS_ALL;
	}
}

wxString BrushManagerPanel::GetSearchFilter() const
{
	if (!search_box) return wxEmptyString;
	return search_box->GetValue();
}

void BrushManagerPanel::ToggleSelectedBrushes()
{
	std::vector<Brush*> selected = brush_list->GetSelectedBrushes();
	for (Brush* brush : selected) {
		if (brush) {
			brush->toggleActivatedInXML();
		}
	}

	if (IsAutoReloadEnabled()) {
		ReloadBrushes();
	} else {
		brush_list->RefreshList();
	}
}

void BrushManagerPanel::SelectAllBrushes()
{
	brush_list->SelectAll();
}

void BrushManagerPanel::DeselectAllBrushes()
{
	brush_list->DeselectAll();
}

void BrushManagerPanel::OnBrushActivationToggled()
{
	// Called when a brush activation state is toggled
	// If auto-reload is enabled, reload brushes; otherwise just refresh the list
	if (IsAutoReloadEnabled()) {
		ReloadBrushes();
	} else {
		brush_list->RefreshList();
	}
}

wxString BrushManagerPanel::GetBrushTypeName(BrushType type)
{
	switch (type) {
		case BRUSH_TYPE_RAW: return "Raw";
		case BRUSH_TYPE_DOODAD: return "Doodad";
		case BRUSH_TYPE_TERRAIN: return "Terrain";
		case BRUSH_TYPE_GROUND: return "Ground";
		case BRUSH_TYPE_WALL: return "Wall";
		case BRUSH_TYPE_WALL_DECORATION: return "Wall Decoration";
		case BRUSH_TYPE_TABLE: return "Table";
		case BRUSH_TYPE_CARPET: return "Carpet";
		case BRUSH_TYPE_DOOR: return "Door";
		case BRUSH_TYPE_OPTIONAL_BORDER: return "Optional Border";
		case BRUSH_TYPE_CREATURE: return "Creature";
		case BRUSH_TYPE_SPAWN: return "Spawn";
		case BRUSH_TYPE_HOUSE: return "House";
		case BRUSH_TYPE_HOUSE_EXIT: return "House Exit";
		case BRUSH_TYPE_WAYPOINT: return "Waypoint";
		case BRUSH_TYPE_CAMERA_PATH: return "Camera Path";
		case BRUSH_TYPE_FLAG: return "Flag";
		case BRUSH_TYPE_ERASER: return "Eraser";
		default: return "Unknown";
	}
}

void BrushManagerPanel::OnTypeFilterChanged(wxCommandEvent& event)
{
	ApplyFilters();
}

void BrushManagerPanel::OnStatusFilterChanged(wxCommandEvent& event)
{
	ApplyFilters();
}

void BrushManagerPanel::OnSearchChanged(wxCommandEvent& event)
{
	ApplyFilters();
}

void BrushManagerPanel::OnToggleSelected(wxCommandEvent& event)
{
	ToggleSelectedBrushes();
}

void BrushManagerPanel::OnReloadBrushes(wxCommandEvent& event)
{
	ReloadBrushes();
}

void BrushManagerPanel::OnAutoReloadCheckbox(wxCommandEvent& event)
{
	// Save the auto-reload setting when checkbox is toggled
	g_settings.setInteger(Config::BRUSH_MANAGER_AUTO_RELOAD, event.IsChecked() ? 1 : 0);
}
