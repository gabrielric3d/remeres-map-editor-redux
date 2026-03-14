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
#include "rendering/ui/radial_wheel.h"
#include "ui/gui.h"
#include "ui/gui_ids.h"
#include "ui/main_frame.h"
#include "ui/main_menubar.h"
#include "ui/map_window.h"
#include "ui/map_tab.h"
#include <nanovg.h>
#include <cmath>

#ifndef M_PI
	#define M_PI 3.14159265358979323846
#endif

namespace {

void FireMenuEvent(int menu_id) {
	if (!g_gui.root) {
		return;
	}
	wxCommandEvent evt(wxEVT_MENU, MAIN_FRAME_MENU + menu_id);
	g_gui.root->GetEventHandler()->ProcessEvent(evt);
}

} // namespace

RadialWheel::RadialWheel() {
	SetupDefaultEntries();
}

RadialWheel::~RadialWheel() = default;

void RadialWheel::SetupDefaultEntries() {
	m_entries.clear();

	m_entries.push_back({"Selection Mode", "S", []() {
		g_gui.SetSelectionMode();
	}});

	m_entries.push_back({"Drawing Mode", "D", []() {
		g_gui.SetDrawingMode();
	}});

	m_entries.push_back({"Find Item", "?", []() {
		FireMenuEvent(MenuBar::FIND_ITEM);
	}});

	m_entries.push_back({"Replace Items", "R", []() {
		FireMenuEvent(MenuBar::REPLACE_ITEMS);
	}});

	m_entries.push_back({"Go to Position", "G", []() {
		FireMenuEvent(MenuBar::GOTO_POSITION);
	}});

	m_entries.push_back({"Jump to Brush", "J", []() {
		FireMenuEvent(MenuBar::JUMP_TO_BRUSH);
	}});

	m_entries.push_back({"Lasso Tool", "L", []() {
		FireMenuEvent(MenuBar::SELECT_MODE_LASSO);
	}});

	m_entries.push_back({"Borderize", "B", []() {
		FireMenuEvent(MenuBar::BORDERIZE_SELECTION);
	}});
}

void RadialWheel::Open(int canvas_width, int canvas_height) {
	m_open = true;
	m_center_x = canvas_width / 2;
	m_center_y = canvas_height / 2;
	m_hovered_index = -1;
}

void RadialWheel::Close() {
	m_open = false;
	m_hovered_index = -1;
}

void RadialWheel::Confirm() {
	if (m_hovered_index >= 0 && m_hovered_index < (int)m_entries.size()) {
		auto action = m_entries[m_hovered_index].action;
		Close();
		if (action) {
			action();
		}
	} else {
		Close();
	}
}

void RadialWheel::UpdateMouse(int mouse_x, int mouse_y) {
	if (!m_open) {
		return;
	}

	float dx = (float)(mouse_x - m_center_x);
	float dy = (float)(mouse_y - m_center_y);
	float dist = std::sqrt(dx * dx + dy * dy);

	if (dist < DEAD_ZONE) {
		m_hovered_index = -1;
		return;
	}

	float angle = std::atan2(dy, dx);
	if (angle < 0) {
		angle += 2.0f * (float)M_PI;
	}

	int count = (int)m_entries.size();
	if (count == 0) {
		m_hovered_index = -1;
		return;
	}

	float segment_size = 2.0f * (float)M_PI / count;
	float offset_angle = angle + (float)M_PI / 2.0f + segment_size / 2.0f;
	if (offset_angle < 0) {
		offset_angle += 2.0f * (float)M_PI;
	}
	if (offset_angle >= 2.0f * (float)M_PI) {
		offset_angle -= 2.0f * (float)M_PI;
	}

	m_hovered_index = (int)(offset_angle / segment_size);
	if (m_hovered_index >= count) {
		m_hovered_index = 0;
	}
}

float RadialWheel::GetSegmentAngleStart(int index) const {
	int count = (int)m_entries.size();
	float segment_size = 2.0f * (float)M_PI / count;
	return -(float)M_PI / 2.0f + segment_size * index - segment_size / 2.0f;
}

float RadialWheel::GetSegmentAngleEnd(int index) const {
	int count = (int)m_entries.size();
	float segment_size = 2.0f * (float)M_PI / count;
	return -(float)M_PI / 2.0f + segment_size * (index + 1) - segment_size / 2.0f;
}

void RadialWheel::Draw(NVGcontext* vg, int canvas_width, int canvas_height) {
	if (!m_open || !vg || m_entries.empty()) {
		return;
	}

	nvgSave(vg);

	// Dim background
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, (float)canvas_width, (float)canvas_height);
	nvgFillColor(vg, nvgRGBA(0, 0, 0, 100));
	nvgFill(vg);

	float cx = (float)m_center_x;
	float cy = (float)m_center_y;
	int count = (int)m_entries.size();

	// Draw segments
	for (int i = 0; i < count; i++) {
		DrawSegment(vg, i, i == m_hovered_index);
	}

	// Draw divider lines between segments
	for (int i = 0; i < count; i++) {
		float angle = GetSegmentAngleStart(i);
		float x1 = cx + std::cos(angle) * INNER_RADIUS;
		float y1 = cy + std::sin(angle) * INNER_RADIUS;
		float x2 = cx + std::cos(angle) * OUTER_RADIUS;
		float y2 = cy + std::sin(angle) * OUTER_RADIUS;

		nvgBeginPath(vg);
		nvgMoveTo(vg, x1, y1);
		nvgLineTo(vg, x2, y2);
		nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 150));
		nvgStrokeWidth(vg, 2.0f);
		nvgStroke(vg);
	}

	// Draw center circle
	DrawCenterCircle(vg);

	// Draw icons and labels inside each segment
	for (int i = 0; i < count; i++) {
		float mid_angle = (GetSegmentAngleStart(i) + GetSegmentAngleEnd(i)) / 2.0f;
		float lx = cx + std::cos(mid_angle) * LABEL_RADIUS;
		float ly = cy + std::sin(mid_angle) * LABEL_RADIUS;

		bool hovered = (i == m_hovered_index);
		float icon_size = hovered ? 16.0f : 13.0f;

		// Draw icon above text
		DrawIcon(vg, lx, ly - 12.0f, icon_size, i, hovered);

		// Draw the label text below icon
		nvgFontSize(vg, hovered ? 15.0f : 12.0f);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		// Text shadow
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 200));
		nvgText(vg, lx + 1.0f, ly + 10.0f + 1.0f, m_entries[i].label.c_str(), nullptr);

		// Text
		if (hovered) {
			nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
		} else {
			nvgFillColor(vg, nvgRGBA(200, 200, 205, 220));
		}
		nvgText(vg, lx, ly + 10.0f, m_entries[i].label.c_str(), nullptr);
	}

	// Draw hovered label tooltip at center
	DrawLabel(vg);

	nvgRestore(vg);
}

void RadialWheel::DrawSegment(NVGcontext* vg, int index, bool hovered) const {
	float cx = (float)m_center_x;
	float cy = (float)m_center_y;
	float angle_start = GetSegmentAngleStart(index);
	float angle_end = GetSegmentAngleEnd(index);

	nvgBeginPath(vg);
	nvgArc(vg, cx, cy, INNER_RADIUS, angle_start, angle_end, NVG_CW);
	nvgArc(vg, cx, cy, OUTER_RADIUS, angle_end, angle_start, NVG_CCW);
	nvgClosePath(vg);

	if (hovered) {
		nvgFillColor(vg, nvgRGBA(60, 120, 200, 210));
	} else {
		nvgFillColor(vg, nvgRGBA(35, 35, 40, 200));
	}
	nvgFill(vg);

	// Outer arc border
	nvgBeginPath(vg);
	nvgArc(vg, cx, cy, OUTER_RADIUS, angle_start, angle_end, NVG_CW);
	nvgStrokeColor(vg, nvgRGBA(160, 160, 170, 120));
	nvgStrokeWidth(vg, 1.5f);
	nvgStroke(vg);

	// Inner arc border
	nvgBeginPath(vg);
	nvgArc(vg, cx, cy, INNER_RADIUS, angle_start, angle_end, NVG_CW);
	nvgStrokeColor(vg, nvgRGBA(160, 160, 170, 120));
	nvgStrokeWidth(vg, 1.5f);
	nvgStroke(vg);
}

void RadialWheel::DrawCenterCircle(NVGcontext* vg) const {
	float cx = (float)m_center_x;
	float cy = (float)m_center_y;
	float r = INNER_RADIUS - 3.0f;

	// Dark center
	nvgBeginPath(vg);
	nvgCircle(vg, cx, cy, r);
	nvgFillColor(vg, nvgRGBA(25, 25, 30, 230));
	nvgFill(vg);

	// Border
	nvgBeginPath(vg);
	nvgCircle(vg, cx, cy, r);
	nvgStrokeColor(vg, nvgRGBA(130, 130, 140, 200));
	nvgStrokeWidth(vg, 2.0f);
	nvgStroke(vg);

	// Center dot
	nvgBeginPath(vg);
	nvgCircle(vg, cx, cy, 4.0f);
	nvgFillColor(vg, nvgRGBA(180, 180, 190, 200));
	nvgFill(vg);
}

void RadialWheel::DrawIcon(NVGcontext* vg, float cx, float cy, float size, int index, bool hovered) const {
	float half = size / 2.0f;
	NVGcolor color = hovered ? nvgRGBA(255, 255, 255, 255) : nvgRGBA(180, 180, 190, 220);
	NVGcolor shadow = nvgRGBA(0, 0, 0, 150);
	float stroke_w = hovered ? 2.0f : 1.5f;

	// Draw shadow offset
	auto drawShape = [&](float ox, float oy, NVGcolor col) {
		nvgStrokeColor(vg, col);
		nvgFillColor(vg, col);
		nvgStrokeWidth(vg, stroke_w);
		float x = cx + ox;
		float y = cy + oy;

		switch (index) {
			case 0: {
				// Selection Mode - cursor/pointer arrow
				nvgBeginPath(vg);
				nvgMoveTo(vg, x - half * 0.4f, y - half);
				nvgLineTo(vg, x - half * 0.4f, y + half * 0.6f);
				nvgLineTo(vg, x, y + half * 0.2f);
				nvgLineTo(vg, x + half * 0.4f, y + half);
				nvgLineTo(vg, x + half * 0.6f, y + half * 0.6f);
				nvgLineTo(vg, x + half * 0.15f, y);
				nvgLineTo(vg, x + half * 0.7f, y - half * 0.3f);
				nvgClosePath(vg);
				nvgFill(vg);
				break;
			}
			case 1: {
				// Drawing Mode - pencil
				nvgBeginPath(vg);
				// Pencil body (diagonal)
				nvgMoveTo(vg, x + half * 0.7f, y - half * 0.9f);
				nvgLineTo(vg, x + half * 0.9f, y - half * 0.7f);
				nvgLineTo(vg, x - half * 0.5f, y + half * 0.7f);
				nvgLineTo(vg, x - half * 0.9f, y + half * 0.9f);
				nvgLineTo(vg, x - half * 0.7f, y + half * 0.5f);
				nvgClosePath(vg);
				nvgFill(vg);
				break;
			}
			case 2: {
				// Find Item - magnifying glass
				float r = half * 0.5f;
				nvgBeginPath(vg);
				nvgCircle(vg, x - half * 0.15f, y - half * 0.15f, r);
				nvgStroke(vg);
				// Handle
				nvgBeginPath(vg);
				nvgMoveTo(vg, x + half * 0.2f, y + half * 0.2f);
				nvgLineTo(vg, x + half * 0.8f, y + half * 0.8f);
				nvgStrokeWidth(vg, stroke_w + 1.0f);
				nvgStroke(vg);
				nvgStrokeWidth(vg, stroke_w);
				break;
			}
			case 3: {
				// Replace Items - two arrows cycling
				float r = half * 0.6f;
				// Top arc
				nvgBeginPath(vg);
				nvgArc(vg, x, y, r, -(float)M_PI * 0.8f, -(float)M_PI * 0.1f, NVG_CW);
				nvgStroke(vg);
				// Top arrowhead
				float ax = x + std::cos(-(float)M_PI * 0.1f) * r;
				float ay = y + std::sin(-(float)M_PI * 0.1f) * r;
				nvgBeginPath(vg);
				nvgMoveTo(vg, ax, ay);
				nvgLineTo(vg, ax - half * 0.3f, ay - half * 0.2f);
				nvgLineTo(vg, ax + half * 0.1f, ay - half * 0.3f);
				nvgClosePath(vg);
				nvgFill(vg);
				// Bottom arc
				nvgBeginPath(vg);
				nvgArc(vg, x, y, r, (float)M_PI * 0.2f, (float)M_PI * 0.9f, NVG_CW);
				nvgStroke(vg);
				// Bottom arrowhead
				float bx = x + std::cos((float)M_PI * 0.9f) * r;
				float by = y + std::sin((float)M_PI * 0.9f) * r;
				nvgBeginPath(vg);
				nvgMoveTo(vg, bx, by);
				nvgLineTo(vg, bx + half * 0.3f, by + half * 0.2f);
				nvgLineTo(vg, bx - half * 0.1f, by + half * 0.3f);
				nvgClosePath(vg);
				nvgFill(vg);
				break;
			}
			case 4: {
				// Go to Position - crosshair
				nvgBeginPath(vg);
				nvgCircle(vg, x, y, half * 0.6f);
				nvgStroke(vg);
				// Cross lines
				nvgBeginPath(vg);
				nvgMoveTo(vg, x, y - half);
				nvgLineTo(vg, x, y + half);
				nvgStroke(vg);
				nvgBeginPath(vg);
				nvgMoveTo(vg, x - half, y);
				nvgLineTo(vg, x + half, y);
				nvgStroke(vg);
				break;
			}
			case 5: {
				// Jump to Brush - paintbrush
				nvgBeginPath(vg);
				// Brush head
				nvgRoundedRect(vg, x - half * 0.4f, y - half * 0.9f, half * 0.8f, half * 0.7f, half * 0.15f);
				nvgFill(vg);
				// Handle
				nvgBeginPath(vg);
				nvgRect(vg, x - half * 0.2f, y - half * 0.2f, half * 0.4f, half * 1.1f);
				nvgFill(vg);
				break;
			}
			case 6: {
				// Lasso Tool - lasso/loop shape
				nvgBeginPath(vg);
				nvgEllipse(vg, x, y - half * 0.15f, half * 0.65f, half * 0.5f);
				nvgStroke(vg);
				// Rope tail hanging down
				nvgBeginPath(vg);
				nvgMoveTo(vg, x + half * 0.6f, y + half * 0.1f);
				nvgBezierTo(vg, x + half * 0.8f, y + half * 0.6f, x + half * 0.2f, y + half * 0.7f, x + half * 0.1f, y + half * 0.9f);
				nvgStroke(vg);
				break;
			}
			case 7: {
				// Borderize - grid/border pattern
				float s = half * 0.7f;
				// Outer square
				nvgBeginPath(vg);
				nvgRect(vg, x - s, y - s, s * 2.0f, s * 2.0f);
				nvgStroke(vg);
				// Inner dashed cross pattern (border lines)
				float q = s * 0.5f;
				nvgBeginPath(vg);
				nvgMoveTo(vg, x - s, y - q);
				nvgLineTo(vg, x + s, y - q);
				nvgStroke(vg);
				nvgBeginPath(vg);
				nvgMoveTo(vg, x - s, y + q);
				nvgLineTo(vg, x + s, y + q);
				nvgStroke(vg);
				nvgBeginPath(vg);
				nvgMoveTo(vg, x - q, y - s);
				nvgLineTo(vg, x - q, y + s);
				nvgStroke(vg);
				nvgBeginPath(vg);
				nvgMoveTo(vg, x + q, y - s);
				nvgLineTo(vg, x + q, y + s);
				nvgStroke(vg);
				break;
			}
			default:
				break;
		}
	};

	// Shadow pass
	drawShape(1.0f, 1.0f, shadow);
	// Main pass
	drawShape(0.0f, 0.0f, color);
}

void RadialWheel::DrawLabel(NVGcontext* vg) const {
	float cx = (float)m_center_x;
	float cy = (float)m_center_y;

	if (m_hovered_index < 0 || m_hovered_index >= (int)m_entries.size()) {
		// No hover - show hint in center
		nvgFontSize(vg, 14.0f);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, nvgRGBA(160, 160, 170, 180));
		nvgText(vg, cx, cy, "Select", nullptr);
		return;
	}

	// Show selected action name in center circle
	const std::string& label = m_entries[m_hovered_index].label;

	nvgFontSize(vg, 15.0f);
	nvgFontFace(vg, "sans");
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	nvgFillColor(vg, nvgRGBA(255, 255, 255, 240));
	nvgText(vg, cx, cy, label.c_str(), nullptr);
}
