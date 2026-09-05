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
#include "ui/menubar/view_settings_handler.h"
#include "ui/map_window.h"
#include "ui/map_tab.h"
#include "ui/dialogs/erase_floors_dialog.h"
#include "ui/dialogs/ghost_floors_dialog.h"
#include "rendering/ui/toast_renderer.h"
#include "rendering/ui/border_variant_hud.h"
#include "app/settings.h"
#include <nanovg.h>
#include <algorithm>
#include <cmath>
#include <string>

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

// Flips a boolean setting and toasts the new state, so the change is visible even after
// the wheel closes — these toggles keep erasing floors until they are turned back off.
void ToggleSetting(Config::Key key, const std::string& name, Config::Key count_key) {
	const bool new_state = !g_settings.getBoolean(key);
	g_settings.setInteger(key, new_state ? 1 : 0);
	g_settings.save();

	std::string message = name + (new_state ? ": ON" : ": OFF");
	if (new_state) {
		message += " (" + std::to_string(g_settings.getInteger(count_key)) + " floor(s), Ctrl + brush)";
	}
	g_toast.Show(message);
}

// Ghost Floors is a view toggle, so the map must repaint right away. The toast spells
// out what is being ghosted (the dialog decides the directions and counts).
void ToggleGhostFloors() {
	const bool new_state = !g_settings.getBoolean(Config::GHOST_FLOORS_ENABLED);
	g_settings.setInteger(Config::GHOST_FLOORS_ENABLED, new_state ? 1 : 0);
	g_settings.save();

	std::string message = new_state ? "Ghost floors: ON" : "Ghost floors: OFF";
	if (new_state) {
		auto describe = [](Config::Key enabled_key, Config::Key count_key) -> std::string {
			if (!g_settings.getBoolean(enabled_key)) {
				return "";
			}
			const int count = g_settings.getInteger(count_key);
			return count >= MAP_MAX_LAYER ? "all" : std::to_string(count);
		};
		const std::string above = describe(Config::GHOST_FLOORS_ABOVE_ENABLED, Config::GHOST_FLOORS_ABOVE_COUNT);
		const std::string below = describe(Config::GHOST_FLOORS_BELOW_ENABLED, Config::GHOST_FLOORS_BELOW_COUNT);
		if (above.empty() && below.empty()) {
			message += " (nothing selected, see Ghost Floors...)";
		} else {
			message += " (";
			if (!above.empty()) {
				message += above + " above";
			}
			if (!below.empty()) {
				message += (above.empty() ? "" : ", ") + below + " below";
			}
			message += ")";
		}
	}
	g_toast.Show(message);
	g_gui.RefreshView();
}

// Magic wand is a menu check item; the shared toggle keeps the menu check, the Tool
// Options button and the toast in sync (the wheel stays open, it is a toggle entry).
void ToggleMagicWand() {
	ViewSettingsHandler::SetMagicWandEnabled(!g_settings.getBoolean(Config::SELECTION_MAGIC_WAND));
}

} // namespace

RadialWheel::RadialWheel() {
	SetupDefaultEntries();
}

RadialWheel::~RadialWheel() = default;

void RadialWheel::SetupDefaultEntries() {
	// NOTE: DrawIcon() picks each entry's vector icon by its index in this list.
	// Inserting or reordering entries here means renumbering the cases there too.
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

	m_entries.push_back({"Shader Settings", "H", []() {
		FireMenuEvent(MenuBar::OPEN_GRAPHICS_PREFERENCES);
	}});

	// Erase-extra-floors toggles: with these on, erasing with Ctrl + brush also wipes the
	// same footprint on the floors above/below (count and "whole tile" live in the dialog).
	m_entries.push_back({ "Erase Above", "U",
		[]() {
			ToggleSetting(Config::ERASE_FLOORS_ABOVE_ENABLED, "Erase floors above", Config::ERASE_FLOORS_ABOVE_COUNT);
		},
		[]() {
			return g_settings.getBoolean(Config::ERASE_FLOORS_ABOVE_ENABLED);
		} });

	m_entries.push_back({ "Erase Below", "N",
		[]() {
			ToggleSetting(Config::ERASE_FLOORS_BELOW_ENABLED, "Erase floors below", Config::ERASE_FLOORS_BELOW_COUNT);
		},
		[]() {
			return g_settings.getBoolean(Config::ERASE_FLOORS_BELOW_ENABLED);
		} });

	// Cycles the border shape the ground brushes paint with. Not a plain on/off
	// toggle, so it closes the wheel like the other commands; the canvas badge and
	// the toast report which variant is now active.
	m_entries.push_back({"Border Variant", "B", []() {
		BorderVariantHUD::CycleAndNotify();
	}});

	m_entries.push_back({"Erase Floors...", "E", []() {
		EraseFloorsDialog dialog(g_gui.root);
		dialog.ShowModal();
	}});

	// Ghost Floors: Ghost Higher Floors (Ctrl+L) generalised to N floors above and
	// below, drawn translucent over the current one. Directions, counts and opacity
	// live in the dialog and can be tuned while the toggle is on.
	m_entries.push_back({ "Ghost Floors", "F",
		[]() {
			ToggleGhostFloors();
		},
		[]() {
			return g_settings.getBoolean(Config::GHOST_FLOORS_ENABLED);
		} });

	m_entries.push_back({"Ghost Floors...", "O", []() {
		GhostFloorsDialog dialog(g_gui.root);
		dialog.ShowModal();
	}});

	// Index 14 in DrawIcon(). Appended last so the icons above keep their numbers.
	m_entries.push_back({ "Magic Wand", "W",
		[]() {
			ToggleMagicWand();
		},
		[]() {
			return g_settings.getBoolean(Config::SELECTION_MAGIC_WAND);
		} });
}

void RadialWheel::Open(int canvas_width, int canvas_height) {
	m_open = true;
	m_center_x = canvas_width / 2;
	m_center_y = canvas_height / 2;
	m_hovered_index = -1;
	UpdateLayout(canvas_width, canvas_height);
}

void RadialWheel::Close() {
	m_open = false;
	m_hovered_index = -1;
}

void RadialWheel::Confirm() {
	if (m_hovered_index >= 0 && m_hovered_index < (int)m_entries.size()) {
		const RadialWheelEntry& entry = m_entries[m_hovered_index];
		auto action = entry.action;
		// Toggles keep the wheel open so several of them can be flipped in one visit
		// (erase above + erase below, for instance); the center/ESC closes it.
		const bool keep_open = (entry.is_toggled != nullptr);
		if (!keep_open) {
			Close();
		}
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

	if (dist < m_dead_zone) {
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

void RadialWheel::UpdateLayout(int canvas_width, int canvas_height) {
	const float available = std::min(canvas_width, canvas_height) * 0.5f - CANVAS_MARGIN;
	m_outer_radius = std::max(MIN_OUTER_RADIUS, std::min(MAX_OUTER_RADIUS, available));
	// Hub big enough for the hovered action name, but never eating the label ring.
	m_inner_radius = std::max(72.0f, std::min(118.0f, m_outer_radius * 0.36f));
	// Slightly past the middle of the ring: labels wrap outwards, where there is more room.
	m_label_radius = m_inner_radius + (m_outer_radius - m_inner_radius) * 0.54f;
	m_dead_zone = m_inner_radius * 0.62f;
}

float RadialWheel::GetLabelMaxWidth() const {
	const int count = (int)m_entries.size();
	if (count <= 0) {
		return 0.0f;
	}
	const float segment_size = 2.0f * (float)M_PI / count;
	// Chord of the segment at the label radius, minus a little breathing room.
	return std::max(72.0f, 2.0f * m_label_radius * std::sin(segment_size * 0.5f) - 16.0f);
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

	// Follow a canvas that was resized while the wheel is open, so the hit test
	// (which reads these same members) never drifts from what is drawn.
	m_center_x = canvas_width / 2;
	m_center_y = canvas_height / 2;
	UpdateLayout(canvas_width, canvas_height);

	nvgSave(vg);

	float cx = (float)m_center_x;
	float cy = (float)m_center_y;
	int count = (int)m_entries.size();

	// Dim background, darkest right behind the wheel so the map stops competing
	// with the labels near the center.
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, (float)canvas_width, (float)canvas_height);
	nvgFillColor(vg, nvgRGBA(0, 0, 0, 90));
	nvgFill(vg);

	nvgBeginPath(vg);
	nvgCircle(vg, cx, cy, m_outer_radius + 4.0f);
	nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, m_inner_radius * 0.15f, m_outer_radius + 4.0f,
		nvgRGBA(0, 0, 0, 170), nvgRGBA(0, 0, 0, 0)));
	nvgFill(vg);

	// Rim around the whole wheel. Drawn before the slices so it cannot cut across
	// the hovered one, which grows past this radius.
	nvgBeginPath(vg);
	nvgCircle(vg, cx, cy, m_outer_radius + 1.0f);
	nvgStrokeColor(vg, nvgRGBA(190, 190, 200, 90));
	nvgStrokeWidth(vg, 1.0f);
	nvgStroke(vg);

	// Draw segments (each one outlined; the outline is what separates them).
	// The hovered slice goes last so neighbours never paint over its highlight.
	for (int i = 0; i < count; i++) {
		if (i != m_hovered_index) {
			DrawSegment(vg, i, false);
		}
	}
	if (m_hovered_index >= 0 && m_hovered_index < count) {
		DrawSegment(vg, m_hovered_index, true);
	}

	// Draw icons and labels inside each segment. Labels wrap inside the width the
	// segment actually gives them, so long names ("Shader Settings") break into two
	// lines instead of bleeding into the neighbouring slice.
	const float label_max_w = GetLabelMaxWidth();
	for (int i = 0; i < count; i++) {
		float mid_angle = (GetSegmentAngleStart(i) + GetSegmentAngleEnd(i)) / 2.0f;
		float lx = cx + std::cos(mid_angle) * m_label_radius;
		float ly = cy + std::sin(mid_angle) * m_label_radius;

		const bool hovered = (i == m_hovered_index);
		const bool toggle = (m_entries[i].is_toggled != nullptr);
		const float icon_size = hovered ? 18.0f : 15.0f;
		const char* label = m_entries[i].label.c_str();

		nvgFontFace(vg, "sans");
		nvgFontSize(vg, hovered ? 15.0f : 13.0f);
		nvgTextLineHeight(vg, 1.15f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

		// nvgTextBox takes the LEFT edge of the wrap box, not its center.
		const float text_x = lx - label_max_w * 0.5f;
		float text_bounds[4];
		nvgTextBoxBounds(vg, text_x, 0.0f, label_max_w, label, nullptr, text_bounds);
		const float text_h = text_bounds[3] - text_bounds[1];

		// Stack icon + label (+ ON/OFF badge) as one block centred on the label point.
		const float gap = 6.0f;
		const float badge_h = toggle ? 13.0f : 0.0f;
		const float block_h = icon_size + gap + text_h + badge_h;
		const float block_top = ly - block_h * 0.5f;

		DrawIcon(vg, lx, block_top + icon_size * 0.5f, icon_size, i, hovered);

		const float text_y = block_top + icon_size + gap;
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 200));
		nvgTextBox(vg, text_x + 1.0f, text_y + 1.0f, label_max_w, label, nullptr);
		nvgFillColor(vg, hovered ? nvgRGBA(255, 255, 255, 255) : nvgRGBA(205, 205, 212, 225));
		nvgTextBox(vg, text_x, text_y, label_max_w, label, nullptr);

		// On/off badge under the label of toggle entries
		if (toggle) {
			const bool active = IsEntryToggledOn(i);
			const float badge_y = text_y + text_h + 1.0f;
			nvgFontSize(vg, 10.0f);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgFillColor(vg, nvgRGBA(0, 0, 0, 200));
			nvgText(vg, lx + 1.0f, badge_y + 1.0f, active ? "ON" : "OFF", nullptr);
			nvgFillColor(vg, active ? nvgRGBA(140, 255, 190, 255) : nvgRGBA(170, 170, 175, 200));
			nvgText(vg, lx, badge_y, active ? "ON" : "OFF", nullptr);
		}
	}

	// Draw hovered label tooltip at center
	DrawLabel(vg);

	nvgRestore(vg);
}

bool RadialWheel::IsEntryToggledOn(int index) const {
	if (index < 0 || index >= (int)m_entries.size()) {
		return false;
	}
	const auto& is_toggled = m_entries[index].is_toggled;
	return is_toggled && is_toggled();
}

void RadialWheel::DrawSegment(NVGcontext* vg, int index, bool hovered) const {
	float cx = (float)m_center_x;
	float cy = (float)m_center_y;
	const float angle_start = GetSegmentAngleStart(index);
	const float angle_end = GetSegmentAngleEnd(index);
	const float outer = m_outer_radius + (hovered ? 6.0f : 0.0f);

	// Slices sit flush against each other: what separates them is the outline drawn
	// around each one, not a gap.
	nvgBeginPath(vg);
	nvgArc(vg, cx, cy, m_inner_radius, angle_start, angle_end, NVG_CW);
	nvgArc(vg, cx, cy, outer, angle_end, angle_start, NVG_CCW);
	nvgClosePath(vg);

	// Toggles that are currently on get a green tint so the state is readable at a glance.
	const bool active = IsEntryToggledOn(index);
	if (hovered) {
		nvgFillColor(vg, active ? nvgRGBA(62, 172, 112, 235) : nvgRGBA(58, 118, 198, 225));
	} else {
		nvgFillColor(vg, active ? nvgRGBA(38, 90, 62, 215) : nvgRGBA(30, 30, 36, 214));
	}
	nvgFill(vg);

	// Same path, stroked: one continuous outline around the whole slice.
	nvgStrokeColor(vg, hovered ? nvgRGBA(240, 240, 250, 235) : nvgRGBA(155, 155, 168, 130));
	nvgStrokeWidth(vg, hovered ? 2.0f : 1.0f);
	nvgStroke(vg);
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
			case 8:
			case 9: {
				// Erase Above / Erase Below - two stacked floor plates: the one you draw on
				// is solid, the target one is outlined and crossed out, with an arrow
				// pointing at it.
				const bool up = (index == 8);
				const float plate_w = half * 0.85f;
				const float plate_h = half * 0.32f;
				const float gap = half * 0.52f;
				// Solid plate = the floor you are drawing on
				const float solid_y = up ? y + gap : y - gap;
				nvgBeginPath(vg);
				nvgMoveTo(vg, x, solid_y - plate_h);
				nvgLineTo(vg, x + plate_w, solid_y);
				nvgLineTo(vg, x, solid_y + plate_h);
				nvgLineTo(vg, x - plate_w, solid_y);
				nvgClosePath(vg);
				nvgFill(vg);
				// Outlined plate = the floor being wiped
				const float target_y = up ? y - gap : y + gap;
				nvgBeginPath(vg);
				nvgMoveTo(vg, x, target_y - plate_h);
				nvgLineTo(vg, x + plate_w, target_y);
				nvgLineTo(vg, x, target_y + plate_h);
				nvgLineTo(vg, x - plate_w, target_y);
				nvgClosePath(vg);
				nvgStroke(vg);
				// Cross over the wiped plate
				nvgBeginPath(vg);
				nvgMoveTo(vg, x - plate_w * 0.5f, target_y - plate_h * 0.7f);
				nvgLineTo(vg, x + plate_w * 0.5f, target_y + plate_h * 0.7f);
				nvgMoveTo(vg, x + plate_w * 0.5f, target_y - plate_h * 0.7f);
				nvgLineTo(vg, x - plate_w * 0.5f, target_y + plate_h * 0.7f);
				nvgStroke(vg);
				// Direction arrowhead
				const float tip_y = up ? y - half : y + half;
				const float base_y = up ? tip_y + half * 0.35f : tip_y - half * 0.35f;
				nvgBeginPath(vg);
				nvgMoveTo(vg, x, tip_y);
				nvgLineTo(vg, x - half * 0.28f, base_y);
				nvgLineTo(vg, x + half * 0.28f, base_y);
				nvgClosePath(vg);
				nvgFill(vg);
				break;
			}
			case 10: {
				// Border Variant - two overlapping tile frames, one per border shape
				nvgBeginPath(vg);
				nvgRect(vg, x - half, y - half, half * 1.35f, half * 1.35f);
				nvgStroke(vg);

				nvgBeginPath(vg);
				nvgRect(vg, x - half * 0.35f, y - half * 0.35f, half * 1.35f, half * 1.35f);
				nvgStroke(vg);
				break;
			}
			case 12: {
				// Ghost Floors - three stacked plates: the middle one (current floor) is
				// solid, the ones above and below are outlined, i.e. see-through.
				const float plate_w = half * 0.85f;
				const float plate_h = half * 0.3f;
				const float gap = half * 0.6f;
				for (int row = -1; row <= 1; ++row) {
					const float py = y + gap * (float)row;
					nvgBeginPath(vg);
					nvgMoveTo(vg, x, py - plate_h);
					nvgLineTo(vg, x + plate_w, py);
					nvgLineTo(vg, x, py + plate_h);
					nvgLineTo(vg, x - plate_w, py);
					nvgClosePath(vg);
					if (row == 0) {
						nvgFill(vg);
					} else {
						nvgStroke(vg);
					}
				}
				break;
			}
			case 13: {
				// Ghost Floors settings - a little ghost outline
				nvgBeginPath(vg);
				nvgMoveTo(vg, x - half * 0.6f, y + half * 0.8f);
				nvgLineTo(vg, x - half * 0.6f, y - half * 0.2f);
				nvgArc(vg, x, y - half * 0.2f, half * 0.6f, (float)M_PI, 2.0f * (float)M_PI, NVG_CW);
				nvgLineTo(vg, x + half * 0.6f, y + half * 0.8f);
				// Wavy hem
				nvgLineTo(vg, x + half * 0.3f, y + half * 0.5f);
				nvgLineTo(vg, x, y + half * 0.8f);
				nvgLineTo(vg, x - half * 0.3f, y + half * 0.5f);
				nvgClosePath(vg);
				nvgStroke(vg);
				// Eyes
				nvgBeginPath(vg);
				nvgCircle(vg, x - half * 0.22f, y - half * 0.2f, half * 0.12f);
				nvgCircle(vg, x + half * 0.22f, y - half * 0.2f, half * 0.12f);
				nvgFill(vg);
				break;
			}
			case 11: {
				// Erase Floors settings - sliders
				for (int row = -1; row <= 1; ++row) {
					const float ry = y + row * half * 0.55f;
					nvgBeginPath(vg);
					nvgMoveTo(vg, x - half * 0.9f, ry);
					nvgLineTo(vg, x + half * 0.9f, ry);
					nvgStroke(vg);
					// Knob, staggered per row
					const float knob_x = x + half * 0.45f * (float)row;
					nvgBeginPath(vg);
					nvgCircle(vg, knob_x, ry, half * 0.22f);
					nvgFill(vg);
				}
				break;
			}
			case 14: {
				// Magic Wand - a wand held diagonally with a sparkle at its tip
				nvgBeginPath(vg);
				nvgMoveTo(vg, x - half * 0.75f, y + half * 0.75f);
				nvgLineTo(vg, x + half * 0.2f, y - half * 0.2f);
				nvgStroke(vg);
				// Four-point star
				const float sx = x + half * 0.42f;
				const float sy = y - half * 0.42f;
				const float r_out = half * 0.42f;
				const float r_in = half * 0.14f;
				nvgBeginPath(vg);
				for (int i = 0; i < 8; ++i) {
					const float r = (i % 2 == 0) ? r_out : r_in;
					const float a = (float)i * (float)M_PI / 4.0f - (float)M_PI / 2.0f;
					const float px = sx + std::cos(a) * r;
					const float py = sy + std::sin(a) * r;
					if (i == 0) {
						nvgMoveTo(vg, px, py);
					} else {
						nvgLineTo(vg, px, py);
					}
				}
				nvgClosePath(vg);
				nvgFill(vg);
				// Two tiny sparks
				nvgBeginPath(vg);
				nvgCircle(vg, x - half * 0.15f, y - half * 0.7f, half * 0.08f);
				nvgCircle(vg, x + half * 0.8f, y + half * 0.15f, half * 0.08f);
				nvgFill(vg);
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

	// The hub is open (no disc), so every line here carries its own drop shadow to
	// stay readable against whatever the map is showing through it.
	auto shadowedText = [&](float x, float y, float size, NVGcolor col, const char* text) {
		nvgFontSize(vg, size);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 220));
		nvgText(vg, x + 1.0f, y + 1.0f, text, nullptr);
		nvgFillColor(vg, col);
		nvgText(vg, x, y, text, nullptr);
	};

	if (m_hovered_index < 0 || m_hovered_index >= (int)m_entries.size()) {
		// No hover - show hint in center
		shadowedText(cx, cy, 14.0f, nvgRGBA(175, 175, 185, 200), "Select");
		shadowedText(cx, cy + 16.0f, 10.0f, nvgRGBA(140, 140, 150, 180), "click here to close");
		return;
	}

	// Show selected action name in the hub
	const RadialWheelEntry& entry = m_entries[m_hovered_index];
	shadowedText(cx, cy, 16.0f, nvgRGBA(255, 255, 255, 245), entry.label.c_str());

	// Toggles say what the click will do and leave the wheel open afterwards
	if (entry.is_toggled) {
		const bool active = IsEntryToggledOn(m_hovered_index);
		shadowedText(cx, cy + 19.0f, 11.0f,
			active ? nvgRGBA(255, 170, 170, 230) : nvgRGBA(140, 255, 190, 230),
			active ? "click to turn OFF" : "click to turn ON");
	}
}
