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
#include <memory>
#include <vector>
#include <set>
#include <string>

#include <wx/filename.h>

#include "action.h"
#include "hotkey_utils.h"
#include "tile.h"
#include "creature.h"

class Item;
class Creature;
class BaseMap;
class Brush;
class MapWindow;
class MapPopupMenu;
class AnimationTimer;
class MapDrawer;
class AnimatedGifWriter;

class MapCanvas : public wxGLCanvas
{
public:
	MapCanvas(MapWindow* parent, Editor& editor, int* attriblist);
	virtual ~MapCanvas();
	void Reset();

	// All events
	void OnPaint(wxPaintEvent& event);
	void OnEraseBackground(wxEraseEvent& event) {}

	void OnMouseMove(wxMouseEvent& event);
	void OnMouseLeftRelease(wxMouseEvent& event);
	void OnMouseLeftClick(wxMouseEvent& event);
	void OnMouseLeftDoubleClick(wxMouseEvent& event);
	void OnMouseMiddleDoubleClick(wxMouseEvent& event);
	void OnMouseAux1DoubleClick(wxMouseEvent& event);
	void OnMouseAux2DoubleClick(wxMouseEvent& event);
	void OnMouseCenterClick(wxMouseEvent& event);
	void OnMouseCenterRelease(wxMouseEvent& event);
	void OnMouseRightClick(wxMouseEvent& event);
	void OnMouseRightRelease(wxMouseEvent& event);
	void OnMouseRightDoubleClick(wxMouseEvent& event);
	void OnMouseAux1Click(wxMouseEvent& event);
	void OnMouseAux1Release(wxMouseEvent& event);
	void OnMouseAux2Click(wxMouseEvent& event);
	void OnMouseAux2Release(wxMouseEvent& event);
	void OnMouseAuxEvent(wxMouseEvent& event);
	void OnCameraPathTimer(wxTimerEvent& event);
	bool HandleMouseKeyboardHotkey(wxKeyEvent& event, bool keyDown);
	void DispatchKeyboardMouseAction(MouseActionID action, bool keyDown, const wxKeyEvent& source);

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

	//
	void OnCut(wxCommandEvent& event);
	void OnCopy(wxCommandEvent& event);
	void OnCopyPosition(wxCommandEvent& event);
	void OnCopyServerId(wxCommandEvent& event);
	void OnCopyClientId(wxCommandEvent& event);
	void OnCopyName(wxCommandEvent& event);
	void OnBrowseTile(wxCommandEvent& event);
	void OnPaste(wxCommandEvent& event);
	void OnDelete(wxCommandEvent& event);
	void OnDeleteAll(wxCommandEvent& event);
	void OnApplyReplaceBox1(wxCommandEvent& event);
	void OnApplyReplaceBox2(wxCommandEvent& event);
	// ----
	void OnGotoDestination(wxCommandEvent& event);
	void OnCopyDestination(wxCommandEvent& event);
	void OnRotateItem(wxCommandEvent& event);
	void OnRotateSelectionCW(wxCommandEvent& event);
	void OnRotateSelectionCCW(wxCommandEvent& event);
	void OnRotateSelection180(wxCommandEvent& event);
	void OnSwitchDoor(wxCommandEvent& event);
	// ----
	void OnSelectRAWBrush(wxCommandEvent& event);
	void OnSelectGroundBrush(wxCommandEvent& event);
	void OnSelectDoodadBrush(wxCommandEvent& event);
	void OnSelectDoorBrush(wxCommandEvent& event);
	void OnSelectWallBrush(wxCommandEvent& event);
	void OnSelectCarpetBrush(wxCommandEvent& event);
	void OnSelectTableBrush(wxCommandEvent& event);
	void OnSelectCreatureBrush(wxCommandEvent& event);
	void OnSelectSpawnBrush(wxCommandEvent& event);
	void OnSelectHouseBrush(wxCommandEvent& event);
	// ---
	void OnToggleCarpetActivated(wxCommandEvent& event);
	void OnToggleDoodadActivated(wxCommandEvent& event);
	// ---
	void OnProperties(wxCommandEvent& event);
	void OnAddAreaDecorationRule(wxCommandEvent& event);

	void Refresh();

	void ScreenToMap(int screen_x, int screen_y, int* map_x, int* map_y);
	void MouseToMap(int* map_x, int* map_y) { ScreenToMap(cursor_x, cursor_y, map_x, map_y); }
	void GetScreenCenter(int* map_x, int* map_y);

	void StartPasting();
	void EndPasting();
	void EnterSelectionMode();
	void EnterDrawingMode();

	void UpdatePositionStatus(int x = -1, int y = -1);
	void UpdateZoomStatus();
	void UpdateAutoborderPreview(int mouse_map_x, int mouse_map_y, const wxMouseEvent& event);
	void ClearAutoborderPreview();

	void ChangeFloor(int new_floor);
	int GetFloor() const noexcept { return floor; }
	double GetZoom() const noexcept { return zoom; }
	void SetZoom(double value);
	void ZoomBy(double delta, const wxPoint& anchor);
	void SetPreviewMode(bool preview);
	bool IsPreviewMode() const noexcept { return preview_mode; }
	void GetViewBox(int* view_scroll_x, int* view_scroll_y, int* screensize_x, int* screensize_y) const;

	MapWindow* GetMapWindow() const;
	Position GetCursorPosition() const;

	void ShowPositionIndicator(const Position& position);
	void TakeScreenshot(wxFileName path, wxString format);
	void TakeRegionScreenshot(wxFileName path, wxString format, const Position& fromPos, const Position& toPos);
	bool StartGifRecording(const wxFileName& path, int fps);
	void StopGifRecording(bool keepFile, bool notify = true);
	bool IsGifRecording() const noexcept { return gif_recording; }
	void ToggleCameraPathPlayback();
	bool IsCameraPathPlaying() const noexcept { return camera_path_playing; }

	// Floor fading
	bool IsFloorFading() const;
	int GetFloorFadeAlpha() const;
	float GetFloorFadeProgress() const;
	int GetFloorFadeOldFloor() const { return floor_fade_old_floor; }

	struct FadeLayer {
		int floor_z;
		wxStopWatch timer;
		FadeLayer(int z) : floor_z(z) { timer.Start(); }
	};
	const std::vector<FadeLayer>& GetFadeLayers() const { return floor_fade_layers; }

protected:
	void getTilesToDraw(int mouse_map_x, int mouse_map_y, int floor, PositionVector* tilestodraw, PositionVector* tilestoborder, bool fill = false);
	bool floodFill(Map *map, const Position& center, int x, int y, GroundBrush* brush, PositionVector* positions);
	void captureGifFrame();

private:
	enum {
		BLOCK_SIZE = 64
	};

	inline int getFillIndex(int x, int y) const noexcept { return ((y % BLOCK_SIZE) * BLOCK_SIZE) + (x % BLOCK_SIZE); }

	static bool processed[BLOCK_SIZE*BLOCK_SIZE];

	void ClearLassoSelection();
	void StartLassoSelection(int screen_x, int screen_y, int map_x, int map_y);
	void AddLassoPoint(int screen_x, int screen_y, int map_x, int map_y);
	void ApplyLassoSelection(Selection& selection, bool creaturesOnly, bool removeSelection);
	bool IsLassoSelectionEnabled() const noexcept;
	bool HasLassoSelection() const noexcept;

	bool CaptureScreenshot(wxImage& outImage, int* out_view_start_x = nullptr, int* out_view_start_y = nullptr);
	bool SaveScreenshotImage(const wxImage& image, wxFileName path, const wxString& format, const wxString& prefix);

	Editor& editor;
	MapDrawer *drawer;
	int keyCode;

	// View related
	int floor;
	double zoom;
	bool preview_mode;
	int cursor_x;
	int cursor_y;
	bool cursor_in_window;

	bool dragging;
	bool boundbox_selection;
	bool boundbox_select_creatures;
	bool boundbox_deselect;
	bool screendragging;
	bool suppress_right_release;
	bool isPasting() const;
	bool drawing;
	bool dragging_draw;
	bool replace_dragging;
	bool alt_ground_mode;
	GroundBrush* alt_ground_reference;
	BaseMap* autoborder_preview_map;
	bool autoborder_preview_active;
	int last_preview_map_x;
	int last_preview_map_y;
	int last_preview_map_z;
	Brush* last_preview_brush;
	int last_preview_brush_size;
	int last_preview_brush_shape;
	bool last_preview_alt;
	std::vector<wxPoint> lasso_screen_points;
	std::vector<wxPoint> lasso_map_points;

	uint8_t* screenshot_buffer;
	std::unique_ptr<AnimatedGifWriter> gif_writer;
	std::vector<uint8_t> gif_frame_buffer;
	bool gif_recording;
	int gif_fps;
	int gif_width;
	int gif_height;
	uint64_t gif_frames_written;
	wxFileName gif_output_path;
	std::chrono::steady_clock::time_point gif_next_frame_time;
	std::chrono::steady_clock::duration gif_frame_interval;
	wxTimer camera_path_timer;
	bool camera_path_playing;
	std::string camera_path_name;
	double camera_path_time;
	std::chrono::steady_clock::time_point camera_path_last_tick;

	// Floor fading
	wxStopWatch floor_fade_timer;
	bool floor_fading;
	int floor_fade_old_floor;
	std::vector<FadeLayer> floor_fade_layers;

	int drag_start_x;
	int drag_start_y;
	int drag_start_z;

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
	MapPopupMenu* popup_menu;
	AnimationTimer* animation_timer;
	std::set<MouseActionID> active_keyboard_mouse_actions;

	// Performance optimization: garbage collection counter
	int gc_frame_counter;

	void HandlePropertiesDoubleClick(wxMouseEvent& event);

	friend class MapDrawer;

	DECLARE_EVENT_TABLE()
};

// Right-click popup menu
class MapPopupMenu : public wxMenu {
public:
	MapPopupMenu(Editor& editor);
	virtual ~MapPopupMenu();

	void Update();

protected:
	Editor& editor;
};

class AnimationTimer : public wxTimer
{
public:
	AnimationTimer(MapCanvas *canvas);

	void Notify();
	void Start();
	void Stop();
	void RequestRefresh() { needs_refresh = true; } // Mark that a refresh is needed

private:
	MapCanvas *map_canvas;
	bool started;
	bool needs_refresh; // Track if refresh is actually needed
	int idle_frames;    // Count frames without activity
};

#endif
