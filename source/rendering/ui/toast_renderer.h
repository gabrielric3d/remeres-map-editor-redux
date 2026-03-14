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

#ifndef RME_TOAST_RENDERER_H_
#define RME_TOAST_RENDERER_H_

#include <string>
#include <cstdint>
#include <deque>

struct NVGcontext;

struct ToastMessage {
	std::string text;
	int64_t start_time_ms; // from wxGetLocalTimeMillis()
	int duration_ms;
	float alpha = 1.0f; // computed each frame
};

class ToastRenderer {
public:
	ToastRenderer();

	// Show a toast message for the given duration (default 2 seconds)
	void Show(const std::string& message, int duration_ms = 2000);

	// Draw active toasts using NanoVG. Call each frame.
	void Draw(NVGcontext* vg, int canvas_width, int canvas_height);

	// Check if there are active toasts
	bool HasActiveToasts() const { return !m_toasts.empty(); }

private:
	std::deque<ToastMessage> m_toasts;
	static constexpr int MAX_TOASTS = 3;
	static constexpr int FADE_DURATION_MS = 300;
};

// Global toast renderer instance
extern ToastRenderer g_toast;

#endif // RME_TOAST_RENDERER_H_
