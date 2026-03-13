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

#ifndef RME_HOTKEY_UTILS_H_
#define RME_HOTKEY_UTILS_H_

#include <string>
#include <wx/event.h>

struct HotkeyData {
	int flags = 0;   // wxACCEL_CTRL, wxACCEL_SHIFT, wxACCEL_ALT, wxACCEL_CMD
	int keycode = 0; // WXK_* constants or character codes
};

// Parse a hotkey string like "Ctrl+Shift+A" into HotkeyData.
bool ParseHotkeyText(const std::string& text, HotkeyData& out);

// Convert HotkeyData back to a display string like "Ctrl+Shift+A".
std::string HotkeyToText(const HotkeyData& hotkey);

// Extract HotkeyData from a wxKeyEvent. Returns false for modifier-only keys.
bool EventToHotkey(const wxKeyEvent& event, HotkeyData& out);

#endif
