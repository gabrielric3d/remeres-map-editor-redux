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
#include "editor/hotkey_utils.h"

#include <wx/accel.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace {

std::string Trim(const std::string& text) {
	if (text.empty()) {
		return text;
	}
	size_t start = 0;
	while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
		++start;
	}
	size_t end = text.size();
	while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
		--end;
	}
	return text.substr(start, end - start);
}

std::vector<std::string> Tokenize(const std::string& text) {
	std::vector<std::string> tokens;
	std::string current;
	for (char ch : text) {
		if (ch == '+') {
			if (!current.empty()) {
				tokens.push_back(Trim(current));
				current.clear();
			} else {
				tokens.emplace_back("+");
			}
		} else {
			current.push_back(ch);
		}
	}
	if (!current.empty()) {
		tokens.push_back(Trim(current));
	}
	return tokens;
}

bool StartsWithFKey(const std::string& token, int& outKey) {
	if (token.size() < 2 || (token[0] != 'F' && token[0] != 'f')) {
		return false;
	}
	int number = 0;
	for (size_t i = 1; i < token.size(); ++i) {
		if (!std::isdigit(static_cast<unsigned char>(token[i]))) {
			return false;
		}
		number = number * 10 + (token[i] - '0');
	}
	if (number < 1 || number > 24) {
		return false;
	}
	outKey = WXK_F1 + (number - 1);
	return true;
}

int KeyFromToken(const std::string& token) {
	if (token.empty()) {
		return 0;
	}

	if (token.size() == 1) {
		char ch = token[0];
		if (std::islower(static_cast<unsigned char>(ch))) {
			ch = std::toupper(static_cast<unsigned char>(ch));
		}
		return ch;
	}

	std::string upper = token;
	std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) { return std::toupper(ch); });

	if (upper == "+" || upper == "PLUS") return '+';
	if (upper == "MINUS") return '-';
	if (upper == "EQUALS" || upper == "EQUAL") return '=';
	if (upper == "SPACE") return WXK_SPACE;
	if (upper == "TAB") return WXK_TAB;
	if (upper == "ENTER" || upper == "RETURN") return WXK_RETURN;
	if (upper == "ESC" || upper == "ESCAPE") return WXK_ESCAPE;
	if (upper == "BACKSPACE") return WXK_BACK;
	if (upper == "DELETE" || upper == "DEL") return WXK_DELETE;
	if (upper == "INSERT" || upper == "INS") return WXK_INSERT;
	if (upper == "HOME") return WXK_HOME;
	if (upper == "END") return WXK_END;
	if (upper == "PGUP" || upper == "PAGEUP") return WXK_PAGEUP;
	if (upper == "PGDN" || upper == "PAGEDOWN") return WXK_PAGEDOWN;
	if (upper == "UP") return WXK_UP;
	if (upper == "DOWN") return WXK_DOWN;
	if (upper == "LEFT") return WXK_LEFT;
	if (upper == "RIGHT") return WXK_RIGHT;
	if (upper == "PERIOD") return '.';
	if (upper == "COMMA") return ',';
	if (upper == "SLASH") return '/';
	if (upper == "BACKSLASH") return '\\';

	int fKey = 0;
	if (StartsWithFKey(upper, fKey)) {
		return fKey;
	}

	return 0;
}

std::string KeyToString(int keycode) {
	if (keycode >= 'A' && keycode <= 'Z') {
		return std::string(1, static_cast<char>(keycode));
	}
	if (keycode >= '0' && keycode <= '9') {
		return std::string(1, static_cast<char>(keycode));
	}

	switch (keycode) {
		case '+': return "+";
		case '-': return "-";
		case '=': return "=";
		case '.': return "Period";
		case ',': return "Comma";
		case '/': return "Slash";
		case '\\': return "Backslash";
		case WXK_SPACE: return "Space";
		case WXK_TAB: return "Tab";
		case WXK_RETURN: return "Enter";
		case WXK_ESCAPE: return "Esc";
		case WXK_BACK: return "Backspace";
		case WXK_DELETE: return "Delete";
		case WXK_INSERT: return "Insert";
		case WXK_HOME: return "Home";
		case WXK_END: return "End";
		case WXK_PAGEUP: return "PgUp";
		case WXK_PAGEDOWN: return "PgDn";
		case WXK_UP: return "Up";
		case WXK_DOWN: return "Down";
		case WXK_LEFT: return "Left";
		case WXK_RIGHT: return "Right";
		default:
			break;
	}

	if (keycode >= WXK_F1 && keycode <= WXK_F24) {
		return "F" + std::to_string((keycode - WXK_F1) + 1);
	}

	return "";
}

} // anonymous namespace

bool ParseHotkeyText(const std::string& text, HotkeyData& out) {
	std::vector<std::string> tokens = Tokenize(text);
	if (tokens.empty()) {
		return false;
	}

	int flags = 0;
	int keycode = 0;

	for (const std::string& token : tokens) {
		if (token.empty()) {
			continue;
		}

		std::string upper = token;
		std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) { return std::toupper(ch); });

		if (upper == "CTRL" || upper == "CONTROL") {
			flags |= wxACCEL_CTRL;
			continue;
		}
		if (upper == "SHIFT") {
			flags |= wxACCEL_SHIFT;
			continue;
		}
		if (upper == "ALT") {
			flags |= wxACCEL_ALT;
			continue;
		}
		if (upper == "CMD" || upper == "COMMAND" || upper == "META") {
			flags |= wxACCEL_CMD;
			continue;
		}

		int key = KeyFromToken(token);
		if (key == 0) {
			return false;
		}
		keycode = key;
	}

	if (keycode == 0) {
		return false;
	}

	out.flags = flags;
	out.keycode = keycode;
	return true;
}

std::string HotkeyToText(const HotkeyData& hotkey) {
	if (hotkey.keycode == 0) {
		return "";
	}

	std::vector<std::string> parts;
	if (hotkey.flags & wxACCEL_CTRL) {
		parts.emplace_back("Ctrl");
	}
	if (hotkey.flags & wxACCEL_ALT) {
		parts.emplace_back("Alt");
	}
	if (hotkey.flags & wxACCEL_SHIFT) {
		parts.emplace_back("Shift");
	}
#ifdef __APPLE__
	if (hotkey.flags & wxACCEL_CMD) {
		parts.emplace_back("Cmd");
	}
#endif

	std::string key = KeyToString(hotkey.keycode);
	if (key.empty()) {
		return "";
	}
	parts.push_back(key);

	std::string result;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (i > 0) {
			result += '+';
		}
		result += parts[i];
	}
	return result;
}

bool EventToHotkey(const wxKeyEvent& event, HotkeyData& out) {
	int keycode = event.GetKeyCode();

	// Filter out pure modifier keys
	if (keycode == WXK_SHIFT || keycode == WXK_CONTROL || keycode == WXK_ALT || keycode == WXK_RAW_CONTROL) {
		return false;
	}
#ifdef WXK_WINDOWS_LEFT
	if (keycode == WXK_WINDOWS_LEFT) return false;
#endif
#ifdef WXK_WINDOWS_RIGHT
	if (keycode == WXK_WINDOWS_RIGHT) return false;
#endif
#ifdef WXK_COMMAND
	if (keycode == WXK_COMMAND) return false;
#endif

	int flags = 0;
	if (event.ControlDown()) flags |= wxACCEL_CTRL;
	if (event.AltDown()) flags |= wxACCEL_ALT;
	if (event.ShiftDown()) flags |= wxACCEL_SHIFT;
#ifdef __APPLE__
	if (event.CmdDown()) flags |= wxACCEL_CMD;
#endif

	out.flags = flags;
	out.keycode = keycode;
	return true;
}
