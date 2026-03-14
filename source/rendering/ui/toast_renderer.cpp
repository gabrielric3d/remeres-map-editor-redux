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
#include "rendering/ui/toast_renderer.h"
#include "app/settings.h"
#include "ui/gui.h"

#include <nanovg.h>
#include <cmath>
#include <wx/time.h>
#include <algorithm>

ToastRenderer g_toast;

ToastRenderer::ToastRenderer() = default;

void ToastRenderer::Show(const std::string& message, int duration_ms) {
	if (!g_settings.getBoolean(Config::SHOW_TOAST_NOTIFICATIONS)) {
		return;
	}

	ToastMessage toast;
	toast.text = message;
	toast.start_time_ms = wxGetLocalTimeMillis().GetValue();
	toast.duration_ms = duration_ms;
	toast.alpha = 1.0f;

	m_toasts.push_back(std::move(toast));

	// Limit the queue
	while ((int)m_toasts.size() > MAX_TOASTS) {
		m_toasts.pop_front();
	}

	// Trigger a repaint so the toast is visible
	g_gui.RefreshView();
}

void ToastRenderer::Draw(NVGcontext* vg, int canvas_width, int canvas_height) {
	if (!vg || m_toasts.empty()) {
		return;
	}

	int64_t now = wxGetLocalTimeMillis().GetValue();

	// Remove expired toasts
	while (!m_toasts.empty()) {
		auto& front = m_toasts.front();
		int64_t elapsed = now - front.start_time_ms;
		if (elapsed > front.duration_ms) {
			m_toasts.pop_front();
		} else {
			break;
		}
	}

	if (m_toasts.empty()) {
		return;
	}

	nvgSave(vg);

	float y_offset = 60.0f; // Start from top

	for (auto& toast : m_toasts) {
		int64_t elapsed = now - toast.start_time_ms;

		// Fade in (first 150ms)
		if (elapsed < 150) {
			toast.alpha = (float)elapsed / 150.0f;
		}
		// Fade out (last FADE_DURATION_MS)
		else if (elapsed > toast.duration_ms - FADE_DURATION_MS) {
			float remaining = (float)(toast.duration_ms - elapsed);
			toast.alpha = std::max(0.0f, remaining / (float)FADE_DURATION_MS);
		} else {
			toast.alpha = 1.0f;
		}

		// Slide-in effect: slight downward slide during fade-in
		float slide_y = 0.0f;
		if (elapsed < 150) {
			slide_y = -10.0f * (1.0f - toast.alpha);
		}

		uint8_t alpha = (uint8_t)(toast.alpha * 255.0f);
		uint8_t bg_alpha = (uint8_t)(toast.alpha * 200.0f);

		// Measure text
		nvgFontSize(vg, 16.0f);
		nvgFontFace(vg, "sans");

		float bounds[4];
		nvgTextBounds(vg, 0, 0, toast.text.c_str(), nullptr, bounds);
		float text_w = bounds[2] - bounds[0];
		float text_h = bounds[3] - bounds[1];

		float pad_x = 20.0f;
		float pad_y = 10.0f;
		float box_w = text_w + pad_x * 2.0f;
		float box_h = text_h + pad_y * 2.0f;
		float box_x = (canvas_width - box_w) / 2.0f;
		float box_y = y_offset + slide_y;

		// Background rounded rect
		nvgBeginPath(vg);
		nvgRoundedRect(vg, box_x, box_y, box_w, box_h, 8.0f);
		nvgFillColor(vg, nvgRGBA(20, 20, 25, bg_alpha));
		nvgFill(vg);

		// Border
		nvgBeginPath(vg);
		nvgRoundedRect(vg, box_x, box_y, box_w, box_h, 8.0f);
		nvgStrokeColor(vg, nvgRGBA(100, 140, 200, (uint8_t)(toast.alpha * 150.0f)));
		nvgStrokeWidth(vg, 1.5f);
		nvgStroke(vg);

		// Text
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, nvgRGBA(220, 220, 230, alpha));
		nvgText(vg, canvas_width / 2.0f, box_y + box_h / 2.0f, toast.text.c_str(), nullptr);

		y_offset += box_h + 8.0f;
	}

	nvgRestore(vg);
}
