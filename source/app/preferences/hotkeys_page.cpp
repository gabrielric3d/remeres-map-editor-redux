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
#include "app/preferences/hotkeys_page.h"
#include "app/settings.h"
#include "editor/hotkey_utils.h"
#include "ui/gui.h"
#include "ui/main_frame.h"

HotkeysPage::HotkeysPage(wxWindow* parent) :
	PreferencesPage(parent) {
	auto* sizer = new wxBoxSizer(wxVERTICAL);

	// Search bar
	auto* searchSizer = new wxBoxSizer(wxHORIZONTAL);
	searchSizer->Add(new wxStaticText(this, wxID_ANY, "Search:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	m_searchCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(200), -1));
	m_searchCtrl->SetHint("Filter by menu, action or hotkey...");
	m_searchCtrl->Bind(wxEVT_TEXT, &HotkeysPage::OnSearchChanged, this);
	searchSizer->Add(m_searchCtrl, 1, wxEXPAND);
	sizer->Add(searchSizer, 0, wxEXPAND | wxALL, FromDIP(8));

	// List control
	m_listCtrl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES);
	m_listCtrl->SetBackgroundColour(Theme::Get(Theme::Role::RaisedSurface));
	m_listCtrl->SetTextColour(Theme::Get(Theme::Role::Text));

	m_listCtrl->AppendColumn("Menu", wxLIST_FORMAT_LEFT, FromDIP(100));
	m_listCtrl->AppendColumn("Action", wxLIST_FORMAT_LEFT, FromDIP(180));
	m_listCtrl->AppendColumn("Hotkey", wxLIST_FORMAT_LEFT, FromDIP(150));
	m_listCtrl->AppendColumn("Default", wxLIST_FORMAT_LEFT, FromDIP(150));

	m_listCtrl->Bind(wxEVT_LIST_ITEM_SELECTED, &HotkeysPage::OnListSelected, this);
	sizer->Add(m_listCtrl, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));

	// Hotkey capture area
	auto* captureSizer = new wxBoxSizer(wxHORIZONTAL);

	captureSizer->Add(new wxStaticText(this, wxID_ANY, "Hotkey:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

	m_hotkeyCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(180), -1),
		wxTE_READONLY | wxTE_CENTER);
	m_hotkeyCtrl->SetHint("Click 'Set' then press a key or mouse button...");
	m_hotkeyCtrl->Bind(wxEVT_KEY_DOWN, &HotkeysPage::OnHotkeyKeyDown, this);
	m_hotkeyCtrl->Bind(wxEVT_MIDDLE_DOWN, &HotkeysPage::OnHotkeyMouseDown, this);
	m_hotkeyCtrl->Bind(wxEVT_AUX1_DOWN, &HotkeysPage::OnHotkeyMouseDown, this);
	m_hotkeyCtrl->Bind(wxEVT_AUX2_DOWN, &HotkeysPage::OnHotkeyMouseDown, this);
	m_hotkeyCtrl->Bind(wxEVT_MOUSEWHEEL, &HotkeysPage::OnHotkeyMouseWheel, this);
	captureSizer->Add(m_hotkeyCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

	m_setButton = new wxButton(this, wxID_ANY, "Set Hotkey");
	m_setButton->Bind(wxEVT_BUTTON, &HotkeysPage::OnSetHotkey, this);
	captureSizer->Add(m_setButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));

	m_clearButton = new wxButton(this, wxID_ANY, "Clear");
	m_clearButton->Bind(wxEVT_BUTTON, &HotkeysPage::OnClearHotkey, this);
	captureSizer->Add(m_clearButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));

	m_resetButton = new wxButton(this, wxID_ANY, "Reset to Default");
	m_resetButton->Bind(wxEVT_BUTTON, &HotkeysPage::OnResetSelected, this);
	captureSizer->Add(m_resetButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

	captureSizer->AddStretchSpacer();

	m_resetAllButton = new wxButton(this, wxID_ANY, "Reset All");
	m_resetAllButton->Bind(wxEVT_BUTTON, &HotkeysPage::OnResetAll, this);
	captureSizer->Add(m_resetAllButton, 0, wxALIGN_CENTER_VERTICAL);

	sizer->Add(captureSizer, 0, wxEXPAND | wxALL, FromDIP(8));

	// Status text
	m_statusText = new wxStaticText(this, wxID_ANY, wxEmptyString);
	m_statusText->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	sizer->Add(m_statusText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	// Drawing Modifiers section
	auto* modifierBox = new wxStaticBoxSizer(wxVERTICAL, this, "Drawing Modifiers");
	wxWindow* modBoxWin = modifierBox->GetStaticBox();

	auto* groundRow = new wxBoxSizer(wxHORIZONTAL);
	groundRow->Add(new wxStaticText(modBoxWin, wxID_ANY, "Ground Replace Modifier:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

	wxArrayString modChoices;
	modChoices.Add("Alt");
	modChoices.Add("Ctrl");
	modChoices.Add("Shift");
	m_groundReplaceModifier = new wxChoice(modBoxWin, wxID_ANY, wxDefaultPosition, wxDefaultSize, modChoices);

	std::string currentMod = g_settings.getString(Config::GROUND_REPLACE_MODIFIER);
	if (currentMod == "Ctrl") {
		m_groundReplaceModifier->SetSelection(1);
	} else if (currentMod == "Shift") {
		m_groundReplaceModifier->SetSelection(2);
	} else {
		m_groundReplaceModifier->SetSelection(0);
	}
	groundRow->Add(m_groundReplaceModifier, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	groundRow->Add(new wxStaticText(modBoxWin, wxID_ANY, "(Hold + Left Click to replace specific ground)"), 0, wxALIGN_CENTER_VERTICAL);
	modifierBox->Add(groundRow, 0, wxEXPAND | wxALL, FromDIP(4));

	auto* smartRow = new wxBoxSizer(wxHORIZONTAL);
	smartRow->Add(new wxStaticText(modBoxWin, wxID_ANY, "Smart Brush Picker Modifier:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

	wxArrayString smartChoices;
	smartChoices.Add("Ctrl+Alt");
	smartChoices.Add("Ctrl+Shift");
	smartChoices.Add("Alt+Shift");
	smartChoices.Add("Ctrl");
	smartChoices.Add("Alt");
	smartChoices.Add("Shift");
	m_smartBrushModifier = new wxChoice(modBoxWin, wxID_ANY, wxDefaultPosition, wxDefaultSize, smartChoices);

	std::string currentSmart = g_settings.getString(Config::SMART_BRUSH_MODIFIER);
	int smartSel = 0;
	for (unsigned int i = 0; i < smartChoices.GetCount(); ++i) {
		if (smartChoices[i] == wxString(currentSmart)) {
			smartSel = static_cast<int>(i);
			break;
		}
	}
	m_smartBrushModifier->SetSelection(smartSel);
	smartRow->Add(m_smartBrushModifier, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	smartRow->Add(new wxStaticText(modBoxWin, wxID_ANY, "(Hold + Left Click on a tile to auto-select its brush)"), 0, wxALIGN_CENTER_VERTICAL);
	modifierBox->Add(smartRow, 0, wxEXPAND | wxALL, FromDIP(4));

	auto* smartMouseRow = new wxBoxSizer(wxHORIZONTAL);
	smartMouseRow->Add(new wxStaticText(modBoxWin, wxID_ANY, "Smart Brush Picker Mouse Button:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

	wxArrayString smartMouseChoices;
	smartMouseChoices.Add("None");
	smartMouseChoices.Add("Middle Click");
	smartMouseChoices.Add("Mouse4 (Back)");
	smartMouseChoices.Add("Mouse5 (Forward)");
	m_smartBrushMouseButton = new wxChoice(modBoxWin, wxID_ANY, wxDefaultPosition, wxDefaultSize, smartMouseChoices);

	std::string currentSmartMouse = g_settings.getString(Config::SMART_BRUSH_MOUSE_BUTTON);
	int smartMouseSel = 0;
	for (unsigned int i = 0; i < smartMouseChoices.GetCount(); ++i) {
		if (smartMouseChoices[i] == wxString(currentSmartMouse)) {
			smartMouseSel = static_cast<int>(i);
			break;
		}
	}
	m_smartBrushMouseButton->SetSelection(smartMouseSel);
	smartMouseRow->Add(m_smartBrushMouseButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	smartMouseRow->Add(new wxStaticText(modBoxWin, wxID_ANY, "(Click this mouse button on a tile to auto-select its brush)"), 0, wxALIGN_CENTER_VERTICAL);
	modifierBox->Add(smartMouseRow, 0, wxEXPAND | wxALL, FromDIP(4));

	sizer->Add(modifierBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

	SetSizer(sizer);

	// Load entries from main menu bar
	MainFrame* frame = g_gui.root;
	if (frame) {
		MainMenuBar* mb = frame->GetMainMenuBar();
		if (mb) {
			m_entries = mb->GetMenuHotkeys();
		}
	}

	PopulateList();
	m_setButton->Enable(false);
	m_clearButton->Enable(false);
	m_resetButton->Enable(false);
}

void HotkeysPage::Apply() {
	MainFrame* frame = g_gui.root;
	if (!frame) {
		return;
	}
	MainMenuBar* mb = frame->GetMainMenuBar();
	if (!mb) {
		return;
	}
	mb->ApplyMenuHotkeys(m_entries);

	if (m_groundReplaceModifier) {
		int sel = m_groundReplaceModifier->GetSelection();
		if (sel == 1) {
			g_settings.setString(Config::GROUND_REPLACE_MODIFIER, "Ctrl");
		} else if (sel == 2) {
			g_settings.setString(Config::GROUND_REPLACE_MODIFIER, "Shift");
		} else {
			g_settings.setString(Config::GROUND_REPLACE_MODIFIER, "Alt");
		}
	}

	if (m_smartBrushModifier) {
		int sel = m_smartBrushModifier->GetSelection();
		wxString choice = (sel != wxNOT_FOUND) ? m_smartBrushModifier->GetString(sel) : wxString("Ctrl+Alt");
		g_settings.setString(Config::SMART_BRUSH_MODIFIER, std::string(choice.mb_str()));
	}

	if (m_smartBrushMouseButton) {
		int sel = m_smartBrushMouseButton->GetSelection();
		wxString choice = (sel != wxNOT_FOUND) ? m_smartBrushMouseButton->GetString(sel) : wxString("None");
		g_settings.setString(Config::SMART_BRUSH_MOUSE_BUTTON, std::string(choice.mb_str()));
	}
}

void HotkeysPage::PopulateList() {
	m_listCtrl->DeleteAllItems();

	wxString filter = m_searchCtrl->GetValue().Lower();

	for (size_t i = 0; i < m_entries.size(); ++i) {
		const MenuHotkeyEntry& entry = m_entries[i];

		if (!filter.empty()) {
			wxString menu = wxString(entry.menu).Lower();
			wxString action = wxString(entry.action).Lower();
			wxString hotkey = wxString(entry.currentHotkey).Lower();
			if (menu.Find(filter) == wxNOT_FOUND &&
				action.Find(filter) == wxNOT_FOUND &&
				hotkey.Find(filter) == wxNOT_FOUND) {
				continue;
			}
		}

		long idx = m_listCtrl->InsertItem(m_listCtrl->GetItemCount(), wxString(entry.menu));
		m_listCtrl->SetItem(idx, 1, wxString(entry.action));
		m_listCtrl->SetItem(idx, 2, wxString(entry.currentHotkey));
		m_listCtrl->SetItem(idx, 3, wxString(entry.defaultHotkey));
		m_listCtrl->SetItemData(idx, static_cast<long>(i));

		// Highlight modified hotkeys
		if (entry.currentHotkey != entry.defaultHotkey) {
			m_listCtrl->SetItemTextColour(idx, Theme::Get(Theme::Role::Accent));
		}
	}
}

void HotkeysPage::UpdateSelection() {
	long sel = m_listCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	bool hasSel = (sel != -1);

	m_setButton->Enable(hasSel);
	m_clearButton->Enable(hasSel);
	m_resetButton->Enable(hasSel);

	if (hasSel) {
		size_t entryIdx = static_cast<size_t>(m_listCtrl->GetItemData(sel));
		if (entryIdx < m_entries.size()) {
			m_hotkeyCtrl->SetValue(wxString(m_entries[entryIdx].currentHotkey));
		}
	} else {
		m_hotkeyCtrl->SetValue(wxEmptyString);
	}

	m_capturing = false;
	m_capturedHotkey.clear();
	m_statusText->SetLabel(wxEmptyString);
}

int HotkeysPage::FindHotkeyConflict(const std::string& hotkeyText, MenuBar::ActionID excludeId) const {
	if (hotkeyText.empty()) {
		return -1;
	}

	for (size_t i = 0; i < m_entries.size(); ++i) {
		if (m_entries[i].id == excludeId) {
			continue;
		}
		if (m_entries[i].currentHotkey == hotkeyText) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

void HotkeysPage::OnSearchChanged(wxCommandEvent& WXUNUSED(event)) {
	PopulateList();
}

void HotkeysPage::OnListSelected(wxListEvent& WXUNUSED(event)) {
	UpdateSelection();
}

void HotkeysPage::OnSetHotkey(wxCommandEvent& WXUNUSED(event)) {
	long sel = m_listCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel == -1) {
		return;
	}

	m_capturing = true;
	m_capturedHotkey.clear();
	m_hotkeyCtrl->SetValue("Press a key or click a mouse button...");
	m_hotkeyCtrl->SetFocus();
	m_statusText->SetLabel("Press a key, click a mouse button (Middle/Mouse4/Mouse5), or scroll the wheel. Press Escape to cancel.");
	m_statusText->SetForegroundColour(Theme::Get(Theme::Role::Text));
}

void HotkeysPage::OnClearHotkey(wxCommandEvent& WXUNUSED(event)) {
	long sel = m_listCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel == -1) {
		return;
	}

	size_t entryIdx = static_cast<size_t>(m_listCtrl->GetItemData(sel));
	if (entryIdx >= m_entries.size()) {
		return;
	}

	m_entries[entryIdx].currentHotkey.clear();
	m_hotkeyCtrl->SetValue(wxEmptyString);
	m_listCtrl->SetItem(sel, 2, wxEmptyString);

	if (m_entries[entryIdx].currentHotkey != m_entries[entryIdx].defaultHotkey) {
		m_listCtrl->SetItemTextColour(sel, Theme::Get(Theme::Role::Accent));
	} else {
		m_listCtrl->SetItemTextColour(sel, Theme::Get(Theme::Role::Text));
	}

	m_statusText->SetLabel("Hotkey cleared.");
	m_statusText->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	m_capturing = false;
}

void HotkeysPage::OnResetSelected(wxCommandEvent& WXUNUSED(event)) {
	long sel = m_listCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel == -1) {
		return;
	}

	size_t entryIdx = static_cast<size_t>(m_listCtrl->GetItemData(sel));
	if (entryIdx >= m_entries.size()) {
		return;
	}

	m_entries[entryIdx].currentHotkey = m_entries[entryIdx].defaultHotkey;
	m_hotkeyCtrl->SetValue(wxString(m_entries[entryIdx].currentHotkey));
	m_listCtrl->SetItem(sel, 2, wxString(m_entries[entryIdx].currentHotkey));
	m_listCtrl->SetItemTextColour(sel, Theme::Get(Theme::Role::Text));

	m_statusText->SetLabel("Reset to default.");
	m_statusText->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	m_capturing = false;
}

void HotkeysPage::OnResetAll(wxCommandEvent& WXUNUSED(event)) {
	int answer = wxMessageBox("Reset all hotkeys to their default values?", "Reset All Hotkeys",
		wxYES_NO | wxICON_QUESTION, this);
	if (answer != wxYES) {
		return;
	}

	for (auto& entry : m_entries) {
		entry.currentHotkey = entry.defaultHotkey;
	}

	PopulateList();
	UpdateSelection();

	m_statusText->SetLabel("All hotkeys reset to defaults.");
	m_statusText->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
}

void HotkeysPage::OnHotkeyKeyDown(wxKeyEvent& event) {
	if (!m_capturing) {
		event.Skip();
		return;
	}

	// Escape cancels capture
	if (event.GetKeyCode() == WXK_ESCAPE) {
		m_capturing = false;
		m_capturedHotkey.clear();
		UpdateSelection();
		m_statusText->SetLabel("Capture cancelled.");
		m_statusText->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
		return;
	}

	HotkeyData hotkey;
	if (!EventToHotkey(event, hotkey)) {
		return; // Modifier-only key, keep waiting
	}

	std::string hotkeyText = HotkeyToText(hotkey);
	if (hotkeyText.empty()) {
		return;
	}
	ApplyCapturedHotkey(hotkeyText);
}

void HotkeysPage::OnHotkeyMouseDown(wxMouseEvent& event) {
	if (!m_capturing) {
		event.Skip();
		return;
	}

	HotkeyMouseButton button = HotkeyMouseButton::None;
	wxEventType type = event.GetEventType();
	if (type == wxEVT_MIDDLE_DOWN) {
		button = HotkeyMouseButton::Middle;
	} else if (type == wxEVT_AUX1_DOWN) {
		button = HotkeyMouseButton::Aux1;
	} else if (type == wxEVT_AUX2_DOWN) {
		button = HotkeyMouseButton::Aux2;
	}
	if (button == HotkeyMouseButton::None) {
		event.Skip();
		return;
	}

	HotkeyData hotkey;
	if (!MouseEventToHotkey(event, button, hotkey)) {
		return;
	}
	std::string hotkeyText = HotkeyToText(hotkey);
	if (hotkeyText.empty()) {
		return;
	}
	ApplyCapturedHotkey(hotkeyText);
}

void HotkeysPage::OnHotkeyMouseWheel(wxMouseEvent& event) {
	if (!m_capturing) {
		event.Skip();
		return;
	}
	HotkeyMouseButton button = (event.GetWheelRotation() > 0) ? HotkeyMouseButton::WheelUp : HotkeyMouseButton::WheelDown;
	HotkeyData hotkey;
	if (!MouseEventToHotkey(event, button, hotkey)) {
		return;
	}
	std::string hotkeyText = HotkeyToText(hotkey);
	if (hotkeyText.empty()) {
		return;
	}
	ApplyCapturedHotkey(hotkeyText);
}

void HotkeysPage::ApplyCapturedHotkey(const std::string& hotkeyText) {
	long sel = m_listCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel == -1) {
		m_capturing = false;
		return;
	}

	size_t entryIdx = static_cast<size_t>(m_listCtrl->GetItemData(sel));
	if (entryIdx >= m_entries.size()) {
		m_capturing = false;
		return;
	}

	// Check for conflicts
	int conflictIdx = FindHotkeyConflict(hotkeyText, m_entries[entryIdx].id);
	if (conflictIdx >= 0) {
		m_statusText->SetLabel(wxString::Format("Conflict: '%s' is already assigned to '%s'. Press a different key or Escape to cancel.",
			hotkeyText, m_entries[conflictIdx].action));
		m_statusText->SetForegroundColour(wxColour(200, 80, 80));
		m_hotkeyCtrl->SetValue(wxString(hotkeyText) + " (conflict)");
		return; // Don't apply, let user try again
	}

	// Apply the hotkey
	m_entries[entryIdx].currentHotkey = hotkeyText;
	m_hotkeyCtrl->SetValue(wxString(hotkeyText));
	m_listCtrl->SetItem(sel, 2, wxString(hotkeyText));

	if (m_entries[entryIdx].currentHotkey != m_entries[entryIdx].defaultHotkey) {
		m_listCtrl->SetItemTextColour(sel, Theme::Get(Theme::Role::Accent));
	} else {
		m_listCtrl->SetItemTextColour(sel, Theme::Get(Theme::Role::Text));
	}

	m_capturing = false;
	m_capturedHotkey.clear();
	m_statusText->SetLabel(wxString::Format("Hotkey set to '%s'.", hotkeyText));
	m_statusText->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
}
