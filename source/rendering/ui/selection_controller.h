//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_UI_SELECTION_CONTROLLER_H_
#define RME_RENDERING_UI_SELECTION_CONTROLLER_H_

#include <wx/event.h>
#include <vector>
#include <wx/gdicmn.h>
#include "map/position.h"

class MapCanvas;
class Editor;
class Tile;
class SelectionThread;

class SelectionController {
public:
	SelectionController(MapCanvas* canvas, Editor& editor);
	~SelectionController();

	void HandleClick(const Position& mouse_map_pos, bool shift_down, bool ctrl_down, bool alt_down);
	void HandleDrag(const Position& mouse_map_pos, bool shift_down, bool ctrl_down, bool alt_down);
	void HandleRelease(const Position& mouse_map_pos, bool shift_down, bool ctrl_down, bool alt_down);
	void HandlePropertiesClick(const Position& mouse_map_pos, bool shift_down, bool ctrl_down, bool alt_down);
	void HandlePropertiesRelease(const Position& mouse_map_pos, bool shift_down, bool ctrl_down, bool alt_down);
	void HandleDoubleClick(const Position& mouse_map_pos);

	bool IsDragging() const {
		return dragging;
	}
	bool IsBoundboxSelection() const {
		return boundbox_selection;
	}
	bool IsLassoSelection() const {
		return lasso_active;
	}

	void StartDragging(const Position& start_pos) {
		dragging = true;
		drag_start_pos = start_pos;
	}

	Position GetDragStartPosition() const {
		return drag_start_pos;
	}

	void Reset() {
		dragging = false;
		boundbox_selection = false;
		ClearLassoSelection();
	}

	// Lasso selection
	bool IsLassoEnabled() const;
	bool HasLassoSelection() const;
	const std::vector<wxPoint>& GetLassoScreenPoints() const { return lasso_screen_points; }
	const std::vector<wxPoint>& GetLassoMapPoints() const { return lasso_map_points; }

private:
	void ExecuteBoundboxSelection(const Position& start_pos, const Position& end_pos, int floor);
	void ExecuteLassoSelection(int floor);

	// Lasso helpers
	void ClearLassoSelection();
	void StartLassoSelection(int screen_x, int screen_y, int map_x, int map_y);
	void AddLassoPoint(int screen_x, int screen_y, int map_x, int map_y);

	MapCanvas* canvas;
	Editor& editor;

	bool dragging;
	bool boundbox_selection;
	bool lasso_active;

	Position drag_start_pos;

	// Lasso data
	std::vector<wxPoint> lasso_screen_points;
	std::vector<wxPoint> lasso_map_points;

	static constexpr int LASSO_MIN_DISTANCE_SQ = 36; // Minimum squared pixel distance between points
};

#endif
