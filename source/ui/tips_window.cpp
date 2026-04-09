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

#include "app/main.h"

#include "ui/tips_window.h"
#include "util/image_manager.h"

TipsWindow::TipsWindow(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Tips & Shortcuts", wxDefaultPosition, wxSize(620, 550), wxRESIZE_BORDER | wxCAPTION | wxCLOSE_BOX | wxMAXIMIZE_BOX) {
	auto* topsizer = newd wxBoxSizer(wxVERTICAL);

	// Search bar
	search_ctrl = newd wxSearchCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	search_ctrl->SetDescriptiveText("Search shortcuts...");
	topsizer->Add(search_ctrl, 0, wxEXPAND | wxALL, 10);

	// List control
	list_ctrl = newd wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	list_ctrl->AppendColumn("Category", wxLIST_FORMAT_LEFT, 140);
	list_ctrl->AppendColumn("Shortcut", wxLIST_FORMAT_LEFT, 180);
	list_ctrl->AppendColumn("Description", wxLIST_FORMAT_LEFT, 280);
	topsizer->Add(list_ctrl, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

	// Close button
	auto* btn_sizer = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* close_btn = newd wxButton(this, wxID_OK, "Close");
	close_btn->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_CHECK));
	btn_sizer->Add(close_btn, 0, wxALIGN_CENTER);
	topsizer->Add(btn_sizer, 0, wxALIGN_CENTER | wxALL, 10);

	SetSizerAndFit(topsizer);
	SetMinSize(wxSize(500, 400));
	SetSize(wxSize(620, 550));
	Centre(wxBOTH);

	// Build data and populate
	BuildShortcutList();
	PopulateList();

	// Events
	search_ctrl->Bind(wxEVT_TEXT, &TipsWindow::OnSearchText, this);
	Bind(wxEVT_BUTTON, &TipsWindow::OnClose, this, wxID_OK);

	wxAcceleratorEntry entries[1];
	entries[0].Set(wxACCEL_NORMAL, WXK_ESCAPE, wxID_CANCEL);
	wxAcceleratorTable accel(1, entries);
	SetAcceleratorTable(accel);
	Bind(wxEVT_MENU, &TipsWindow::OnClose, this, wxID_CANCEL);

	SetIcons(IMAGE_MANAGER.GetIconBundle(ICON_LIGHTBULB_SOLID));

	search_ctrl->SetFocus();
}

TipsWindow::~TipsWindow() {
	////
}

void TipsWindow::BuildShortcutList() {
	// File
	all_shortcuts.push_back({"File", "Ctrl+N", "New map"});
	all_shortcuts.push_back({"File", "Ctrl+O", "Open map"});
	all_shortcuts.push_back({"File", "Ctrl+S", "Save map"});
	all_shortcuts.push_back({"File", "Ctrl+Alt+S", "Save As"});
	all_shortcuts.push_back({"File", "Ctrl+Q", "Close current map"});
	all_shortcuts.push_back({"File", "F5", "Reload all data files"});
	all_shortcuts.push_back({"File", "F6", "Reload brushes"});

	// Edit
	all_shortcuts.push_back({"Edit", "Ctrl+Z", "Undo"});
	all_shortcuts.push_back({"Edit", "Ctrl+Shift+Z", "Redo"});
	all_shortcuts.push_back({"Edit", "Ctrl+X", "Cut"});
	all_shortcuts.push_back({"Edit", "Ctrl+C", "Copy"});
	all_shortcuts.push_back({"Edit", "Ctrl+V", "Paste"});
	all_shortcuts.push_back({"Edit", "Ctrl+Shift+F", "Replace Items"});
	all_shortcuts.push_back({"Edit", "A", "Toggle Border Automagic"});
	all_shortcuts.push_back({"Edit", "Ctrl+B", "Borderize Selection"});
	all_shortcuts.push_back({"Edit", "Ctrl+Shift+R", "Remove Items by ID"});
	all_shortcuts.push_back({"Edit", "Delete", "Destroy current selection"});

	// Navigation
	all_shortcuts.push_back({"Navigation", "Arrow Keys", "Scroll view (3 tiles)"});
	all_shortcuts.push_back({"Navigation", "Ctrl+Arrow Keys", "Scroll view (10 tiles)"});
	all_shortcuts.push_back({"Navigation", "Numpad +  /  Page Up", "Go up one floor"});
	all_shortcuts.push_back({"Navigation", "Numpad -  /  Page Down", "Go down one floor"});
	all_shortcuts.push_back({"Navigation", "P", "Go to previous position"});
	all_shortcuts.push_back({"Navigation", "Ctrl+G", "Go to position"});
	all_shortcuts.push_back({"Navigation", "J", "Jump to brush"});
	all_shortcuts.push_back({"Navigation", "Ctrl+J", "Jump to item brush (RAW)"});

	// Zoom
	all_shortcuts.push_back({"Zoom", "Ctrl++", "Zoom In"});
	all_shortcuts.push_back({"Zoom", "Ctrl+-", "Zoom Out"});
	all_shortcuts.push_back({"Zoom", "Ctrl+0", "Zoom Normal (100%)"});
	all_shortcuts.push_back({"Zoom", "Numpad *", "Zoom In (canvas)"});
	all_shortcuts.push_back({"Zoom", "Numpad /", "Zoom Out (canvas)"});

	// Drawing
	all_shortcuts.push_back({"Drawing", "Space", "Switch Drawing / Selection mode"});
	all_shortcuts.push_back({"Drawing", "Ctrl+Space", "Fill doodad preview buffer"});
	all_shortcuts.push_back({"Drawing", "Z", "Previous brush variation"});
	all_shortcuts.push_back({"Drawing", "X", "Next brush variation"});
	all_shortcuts.push_back({"Drawing", "Q", "Select previous brush"});
	all_shortcuts.push_back({"Drawing", "[  or  +", "Increase brush size"});
	all_shortcuts.push_back({"Drawing", "]  or  -", "Decrease brush size"});

	// Palette
	all_shortcuts.push_back({"Palette", "Shift+Up", "Select previous brush in palette"});
	all_shortcuts.push_back({"Palette", "Shift+Down", "Select next brush in palette"});
	all_shortcuts.push_back({"Palette", "Tab", "Cycle to next tab"});
	all_shortcuts.push_back({"Palette", "Shift+Tab", "Cycle to previous tab"});
	all_shortcuts.push_back({"Palette", "T", "Terrain palette"});
	all_shortcuts.push_back({"Palette", "D", "Doodad palette"});
	all_shortcuts.push_back({"Palette", "I", "Item palette"});
	all_shortcuts.push_back({"Palette", "N", "Collection palette"});
	all_shortcuts.push_back({"Palette", "H", "House palette"});
	all_shortcuts.push_back({"Palette", "C", "Creature palette"});
	all_shortcuts.push_back({"Palette", "W", "Waypoint palette"});
	all_shortcuts.push_back({"Palette", "R", "RAW palette"});

	// View
	all_shortcuts.push_back({"View", "Ctrl+W", "Show all floors"});
	all_shortcuts.push_back({"View", "Shift+E", "Show as minimap"});
	all_shortcuts.push_back({"View", "Ctrl+E", "Only show colors"});
	all_shortcuts.push_back({"View", "Ctrl+M", "Only show modified"});
	all_shortcuts.push_back({"View", "Shift+B", "Show only grounds"});
	all_shortcuts.push_back({"View", "Y", "Show tooltips"});
	all_shortcuts.push_back({"View", "Shift+G", "Show grid"});
	all_shortcuts.push_back({"View", "Shift+I", "Show client box"});
	all_shortcuts.push_back({"View", "G", "Ghost loose items"});
	all_shortcuts.push_back({"View", "Ctrl+L", "Ghost higher floors"});
	all_shortcuts.push_back({"View", "Q", "Show shade"});

	// Show
	all_shortcuts.push_back({"Show", "L", "Show animation"});
	all_shortcuts.push_back({"Show", "Shift+L", "Show light"});
	all_shortcuts.push_back({"Show", "Shift+K", "Show light strength"});
	all_shortcuts.push_back({"Show", "Shift+T", "Show technical items"});
	all_shortcuts.push_back({"Show", "F", "Show creatures"});
	all_shortcuts.push_back({"Show", "Shift+F", "Show creature names"});
	all_shortcuts.push_back({"Show", "S", "Show spawns"});
	all_shortcuts.push_back({"Show", "E", "Show special tiles"});
	all_shortcuts.push_back({"Show", "Ctrl+H", "Show houses"});
	all_shortcuts.push_back({"Show", "O", "Show pathing"});
	all_shortcuts.push_back({"Show", "Shift+W", "Show waypoints"});
	all_shortcuts.push_back({"Show", "V", "Highlight items"});
	all_shortcuts.push_back({"Show", "U", "Highlight locked doors"});
	all_shortcuts.push_back({"Show", "K", "Show wall hooks"});
	all_shortcuts.push_back({"Show", "Shift+C", "Show camera paths"});

	// Hotkeys
	all_shortcuts.push_back({"Hotkeys", "0-9", "Load saved hotkey (position or brush)"});
	all_shortcuts.push_back({"Hotkeys", "Ctrl+0 to Ctrl+9", "Save hotkey at current view/brush"});

	// Windows
	all_shortcuts.push_back({"Window", "M", "Minimap window"});
	all_shortcuts.push_back({"Window", "Shift+Q", "Radial Wheel"});
	all_shortcuts.push_back({"Window", "Ctrl+Shift+N", "New View"});
	all_shortcuts.push_back({"Window", "F11", "Toggle Fullscreen"});
	all_shortcuts.push_back({"Window", "F10", "Take Screenshot"});

	// Search
	all_shortcuts.push_back({"Search", "Ctrl+F", "Find item"});

	// Map
	all_shortcuts.push_back({"Map", "Ctrl+T", "Edit towns"});
	all_shortcuts.push_back({"Map", "Ctrl+P", "Map properties"});
	all_shortcuts.push_back({"Map", "F8", "Map statistics"});

	// Scripts
	all_shortcuts.push_back({"Scripts", "Ctrl+Shift+F5", "Reload scripts"});

	// About
	all_shortcuts.push_back({"About", "F1", "About"});
	all_shortcuts.push_back({"About", "F2", "Extensions"});
	all_shortcuts.push_back({"About", "F3", "Goto Website"});
}

void TipsWindow::PopulateList(const wxString& filter) {
	list_ctrl->DeleteAllItems();

	wxString lower_filter = filter.Lower();
	long idx = 0;

	for (const auto& entry : all_shortcuts) {
		if (!lower_filter.IsEmpty()) {
			wxString cat(entry.category);
			wxString key(entry.shortcut);
			wxString desc(entry.description);
			if (cat.Lower().Find(lower_filter) == wxNOT_FOUND
				&& key.Lower().Find(lower_filter) == wxNOT_FOUND
				&& desc.Lower().Find(lower_filter) == wxNOT_FOUND) {
				continue;
			}
		}

		long item = list_ctrl->InsertItem(idx, wxString(entry.category));
		list_ctrl->SetItem(item, 1, wxString(entry.shortcut));
		list_ctrl->SetItem(item, 2, wxString(entry.description));
		idx++;
	}
}

void TipsWindow::OnSearchText(wxCommandEvent& event) {
	PopulateList(search_ctrl->GetValue());
}

void TipsWindow::OnClose(wxCommandEvent& WXUNUSED(event)) {
	EndModal(0);
}
