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

#ifndef RME_DISPLAY_WINDOW_H_
#define RME_DISPLAY_WINDOW_H_

#include <chrono>

#include "editor/action.h"
#include "map/tile.h"
#include "game/creature.h"
#include "rendering/utilities/frame_pacer.h"

#include "ui/map_popup_menu.h"
#include "ui/map_popup_menu.h"
#include "game/animation_timer.h"
#include "rendering/core/graphics.h"
#include <memory>

struct NVGcontext;
struct DrawingOptions;

class Item;
class Creature;
class MapWindow;
class AnimationTimer;
class AnimationTimer;
class MapDrawer;
class SelectionController;
class DrawingController;
class ScreenshotController;
class MapMenuHandler;
class RadialWheel;
class CarpetPiecePicker;

class MapCanvas : public wxGLCanvas {
	std::unique_ptr<wxGLContext> m_glContext;
	std::unique_ptr<NVGcontext, NVGDeleter> m_nvg;

public:
	MapCanvas(wxWindow* parent, Editor& editor, int* attriblist);
	~MapCanvas() override;
	virtual void Reset();

	// All events
	void OnPaint(wxPaintEvent& event);
	void OnEraseBackground(wxEraseEvent& event) { }

	void OnMouseMove(wxMouseEvent& event);
	void OnMouseLeftRelease(wxMouseEvent& event);
	void OnMouseLeftClick(wxMouseEvent& event);
	void OnMouseLeftDoubleClick(wxMouseEvent& event);
	void OnMouseCenterClick(wxMouseEvent& event);
	void OnMouseCenterRelease(wxMouseEvent& event);
	void OnMouseRightClick(wxMouseEvent& event);
	void OnMouseRightRelease(wxMouseEvent& event);
	void OnMouseAux1Click(wxMouseEvent& event);
	void OnMouseAux2Click(wxMouseEvent& event);

	bool TryHandleSmartBrushMouseClick(wxMouseEvent& event, const char* button);

	void OnKeyDown(wxKeyEvent& event);
	void OnKeyUp(wxKeyEvent& event);
	void OnWheel(wxMouseEvent& event);
	void OnGainMouse(wxMouseEvent& event);
	void OnLoseMouse(wxMouseEvent& event);

	// Mouse events handlers (called by the above)
	void OnMouseActionRelease(wxMouseEvent& event);
	void OnMouseActionClick(wxMouseEvent& event);
	void OnMouseCameraClick(wxMouseEvent& event);
	void OnMouseCameraRelease(wxMouseEvent& event);
	void OnMousePropertiesClick(wxMouseEvent& event);
	void OnMousePropertiesRelease(wxMouseEvent& event);

	virtual void Refresh();

	virtual void ScreenToMap(int screen_x, int screen_y, int* map_x, int* map_y);
	void MouseToMap(int* map_x, int* map_y) {
		ScreenToMap(cursor_x, cursor_y, map_x, map_y);
	}
	virtual void GetScreenCenter(int* map_x, int* map_y);

	virtual void StartPasting();
	virtual void EndPasting();
	virtual void EnterSelectionMode();
	virtual void EnterDrawingMode();

	virtual void UpdatePositionStatus(int x = -1, int y = -1);
	virtual void UpdateZoomStatus();
	void SyncCursorHoverState();

	virtual void ChangeFloor(int new_floor);
	int GetFloor() const {
		return floor;
	}
	double GetZoom() const {
		return zoom;
	}
	virtual void SetZoom(double value);
	virtual void GetViewBox(int* view_scroll_x, int* view_scroll_y, int* screensize_x, int* screensize_y) const;

	Position GetCursorPosition() const;

	// "Erase ground above": while the configured hotkey (Preferences > Hotkeys >
	// Erase Ground Above Key, default C) and the Left mouse button are held in drawing
	// mode, removes the ground (and its auto-borders) on the floor directly above
	// (same x,y, floor-1), so you can see the level you are digging below. Runs
	// alongside the normal brush handling, so the current brush keeps drawing on this
	// floor while the ceiling opens up. Uses the current brush footprint.
	// Checked on demand via wxGetKeyState (no persistent state): the physical key state
	// stays correct across focus changes and isn't swallowed by menu accelerators.
	bool IsEraseAboveActive(const wxMouseEvent& event) const;
	void EraseGroundAboveAt(int map_x, int map_y);

	void TakeScreenshot(wxFileName path, wxString format);

	void ToggleCameraPathPlayback();
	void OnCameraPathTimer(wxTimerEvent& event);
	bool IsCameraPathPlaying() const { return camera_path_playing; }

	enum {
		BLOCK_SIZE = 100
	};

	inline int getFillIndex(int x, int y) const {
		return x + BLOCK_SIZE * y;
	}

	static bool processed[BLOCK_SIZE * BLOCK_SIZE];
	Editor& editor;
	std::unique_ptr<MapDrawer> drawer;
	int keyCode;

	// View related
	int floor;
	double zoom;
	int cursor_x;
	int cursor_y;

	bool dragging;
	bool boundbox_selection;
	bool screendragging;
	// Left button went down while the Cave Warp brush was active: drag/release go to it too.
	bool cave_warp_dragging = false;
	bool isPasting() const;

	std::unique_ptr<ScreenshotController> screenshot_controller;

	int last_cursor_map_x;
	int last_cursor_map_y;
	int last_cursor_map_z;

	int last_click_map_x;
	int last_click_map_y;
	int last_click_map_z;
	int last_click_abs_x;
	int last_click_abs_y;
	int last_click_x;
	int last_click_y;

	int last_mmb_click_x;
	int last_mmb_click_y;

	int view_scroll_x;
	int view_scroll_y;

	uint32_t current_house_id;

	wxStopWatch refresh_watch;
	std::unique_ptr<MapPopupMenu> popup_menu;
	std::unique_ptr<AnimationTimer> animation_timer;

	wxTimer camera_path_timer;
	bool camera_path_playing = false;
	std::string camera_path_name;
	double camera_path_time = 0.0;
	std::chrono::steady_clock::time_point camera_path_last_tick;

	// Zoom suave (ZoomController::ZoomTo / OnAnimationTick): o zoom anda ate
	// zoom_target neste timer, com o ponto zoom_anchor_x/y da tela (pixels logicos)
	// parado. zoom_scroll_rest_* guarda a fracao de rolagem que ainda nao virou
	// pixel inteiro.
	wxTimer zoom_anim_timer;
	bool zoom_anim_active = false;
	double zoom_target = 1.0;
	int zoom_anchor_x = 0;
	int zoom_anchor_y = 0;
	double zoom_scroll_rest_x = 0.0;
	double zoom_scroll_rest_y = 0.0;
	std::chrono::steady_clock::time_point zoom_anim_last_tick;
	void OnZoomAnimationTimer(wxTimerEvent& event);

	FramePacer frame_pacer;

	friend class MapDrawer;
	friend class SelectionDrawer;
	friend class BrushOverlayDrawer;
	friend class DragShadowDrawer;
	friend class PreviewDrawer;
	friend class SelectionController;
	friend class DrawingController;

	std::unique_ptr<SelectionController> selection_controller;
	std::unique_ptr<DrawingController> drawing_controller;
	std::unique_ptr<MapMenuHandler> menu_handler;
	std::unique_ptr<RadialWheel> radial_wheel;
	// Alt + right click with a carpet brush (or one of its pieces) in hand: pick a
	// single border piece (Preferences > Hotkeys > Border Piece Picker Modifier).
	std::unique_ptr<CarpetPiecePicker> piece_picker;

private:
	// Opens the piece picker when the configured modifier is held and a carpet (or one
	// of its pieces) is in hand. True when it opened.
	bool TryOpenPiecePicker(const wxMouseEvent& event);
	// Picks the hovered cell of the open piece picker. A piece is also placed on the
	// tile the picker was opened on. False when no cell was under the cursor.
	bool ConfirmPiecePicker();

	// Tile under the cursor when the piece picker opened: where a picked piece goes.
	Position piece_picker_target;

	// The press that picked or closed the piece picker owns its release: it must not
	// reach the brushes (left) or open the context menu (right).
	bool swallow_left_release = false;
	bool swallow_properties_release = false;

	void EnsureNanoVG();
	void DrawOverlays(NVGcontext* vg, const DrawingOptions& options);
	void PerformGarbageCollection();

	MapWindow* GetMapWindow() const;
	bool renderer_initialized = false;
	long m_last_gc_time = 0;
};

#endif
