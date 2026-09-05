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

#ifndef RME_RADIAL_WHEEL_H_
#define RME_RADIAL_WHEEL_H_

#include <string>
#include <vector>
#include <functional>

struct NVGcontext;

// A single entry in the radial wheel
struct RadialWheelEntry {
	std::string label;
	std::string icon; // Unicode icon character (rendered as text)
	std::function<void()> action;
	// Set on entries that flip a persistent setting instead of running a one-shot command:
	// the segment renders its on/off state and the wheel stays open after confirming, so
	// several toggles can be flipped in one visit. Click the center (or ESC) to close.
	std::function<bool()> is_toggled;
};

class RadialWheel {
public:
	RadialWheel();
	~RadialWheel();

	// Open the wheel centered on screen
	void Open(int canvas_width, int canvas_height);

	// Close the wheel without executing any action
	void Close();

	// Execute the currently hovered action and close
	void Confirm();

	// Update the mouse position (screen coords) to determine hovered segment
	void UpdateMouse(int mouse_x, int mouse_y);

	// Draw the wheel using NanoVG
	void Draw(NVGcontext* vg, int canvas_width, int canvas_height);

	bool IsOpen() const { return m_open; }
	int GetHoveredIndex() const { return m_hovered_index; }

	// Populate the default entries (called once, uses g_gui commands)
	void SetupDefaultEntries();

private:
	// True when the entry is a toggle and its setting is currently on.
	bool IsEntryToggledOn(int index) const;

	void DrawSegment(NVGcontext* vg, int index, bool hovered) const;
	void DrawLabel(NVGcontext* vg) const;
	void DrawIcon(NVGcontext* vg, float cx, float cy, float size, int index, bool hovered) const;

	// Recomputes the radii for the current canvas: the wheel grows with the window
	// so the labels get room, and shrinks instead of spilling off a small canvas.
	void UpdateLayout(int canvas_width, int canvas_height);

	float GetSegmentAngleStart(int index) const;
	float GetSegmentAngleEnd(int index) const;
	// Room a label has across its segment at the label radius.
	float GetLabelMaxWidth() const;

	bool m_open = false;
	int m_center_x = 0;
	int m_center_y = 0;
	int m_hovered_index = -1;

	std::vector<RadialWheelEntry> m_entries;

	// Visual settings. Sized by UpdateLayout(); the defaults match a mid-size canvas
	// and are what UpdateMouse() uses before the first frame is drawn.
	float m_inner_radius = 96.0f;
	float m_outer_radius = 268.0f;
	float m_dead_zone = 58.0f;
	float m_label_radius = 190.0f;

	static constexpr float MIN_OUTER_RADIUS = 150.0f;
	static constexpr float MAX_OUTER_RADIUS = 340.0f;
	static constexpr float CANVAS_MARGIN = 14.0f;
};

#endif // RME_RADIAL_WHEEL_H_
