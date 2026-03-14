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
	void DrawSegment(NVGcontext* vg, int index, bool hovered) const;
	void DrawCenterCircle(NVGcontext* vg) const;
	void DrawLabel(NVGcontext* vg) const;

	float GetSegmentAngleStart(int index) const;
	float GetSegmentAngleEnd(int index) const;

	bool m_open = false;
	int m_center_x = 0;
	int m_center_y = 0;
	int m_hovered_index = -1;

	std::vector<RadialWheelEntry> m_entries;

	// Visual settings
	static constexpr float INNER_RADIUS = 80.0f;
	static constexpr float OUTER_RADIUS = 240.0f;
	static constexpr float DEAD_ZONE = 50.0f;
	static constexpr float LABEL_RADIUS = 160.0f; // Where labels are drawn
};

#endif // RME_RADIAL_WHEEL_H_
