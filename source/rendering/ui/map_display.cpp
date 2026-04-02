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

#include <format>
#include <sstream>
#include <time.h>
#include <thread>
#include <chrono>
#include <wx/wfstream.h>
#include <spdlog/spdlog.h>

#include "ui/gui.h"
#include "editor/editor.h"
#include "editor/action_queue.h"
#include "brushes/brush.h"
#include "game/sprites.h"
#include "map/map.h"
#include "map/tile.h"
#include "game/item.h"
#include "ui/properties/old_properties_window.h"
#include "ui/properties/properties_window.h"
#include "ui/tileset_window.h"
#include "palette/palette_window.h"
#include "rendering/ui/screenshot_controller.h"
#include "rendering/utilities/tile_describer.h"
#include "rendering/core/coordinate_mapper.h"
#include "rendering/ui/map_display.h"
#include "rendering/ui/map_status_updater.h"
#include "rendering/map_drawer.h"
#include "rendering/core/text_renderer.h"
#include <glad/glad.h>
#include <nanovg.h>
#include <nanovg_gl.h>
#include "app/application.h"
#include "live/live_server.h"
#include "live/live_client.h"
#include "ui/browse_tile_window.h"
#include "ui/dialog_helper.h"
#include "game/animation_timer.h"
#include "ui/map_popup_menu.h"
#include "brushes/brush_utility.h"
#include "rendering/ui/clipboard_handler.h"
#include "rendering/ui/keyboard_handler.h"
#include "rendering/ui/brush_selector.h"
#include "rendering/ui/popup_action_handler.h"
#include "rendering/ui/zoom_controller.h"
#include "rendering/ui/navigation_controller.h"
#include "rendering/ui/selection_controller.h"
#include "rendering/ui/drawing_controller.h"
#include "rendering/ui/map_menu_handler.h"
#include "rendering/ui/radial_wheel.h"
#include "rendering/ui/toast_renderer.h"
#include "rendering/drawers/overlays/lua_overlay_drawer.h"

#include "brushes/doodad/doodad_brush.h"
#include "brushes/house/house_exit_brush.h"
#include "brushes/house/house_brush.h"
#include "brushes/wall/wall_brush.h"
#include "brushes/spawn/spawn_brush.h"
#include "brushes/creature/creature_brush.h"
#include "brushes/ground/ground_brush.h"
#include "brushes/waypoint/waypoint_brush.h"
#include "brushes/raw/raw_brush.h"
#include "brushes/carpet/carpet_brush.h"
#include "brushes/table/table_brush.h"
#include "brushes/camera/camera_path_brush.h"
#include "game/camera_paths.h"
#include "palette/palette_camera_paths.h"

bool MapCanvas::processed[] = { 0 };

// Helper to create attributes
static wxGLAttributes& GetCoreProfileAttributes() {
	static wxGLAttributes vAttrs = []() {
		wxGLAttributes a;
		a.PlatformDefaults().Defaults().RGBA().DoubleBuffer().Depth(24).Stencil(8).EndList();
		return a;
	}();
	return vAttrs;
}

MapCanvas::MapCanvas(wxWindow* parent, Editor& editor, int* attriblist) :
	wxGLCanvas(parent, GetCoreProfileAttributes(), wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS),
	editor(editor),
	floor(GROUND_LAYER),
	zoom(1.0),
	renderer_initialized(false),
	cursor_x(-1),
	cursor_y(-1),
	dragging(false),
	boundbox_selection(false),
	screendragging(false),

	last_cursor_map_x(-1),
	last_cursor_map_y(-1),
	last_cursor_map_z(-1),

	last_click_map_x(-1),
	last_click_map_y(-1),
	last_click_map_z(-1),
	last_click_abs_x(-1),
	last_click_abs_y(-1),
	last_click_x(-1),
	last_click_y(-1),
	last_mmb_click_x(-1),
	last_mmb_click_y(-1),
	m_last_gc_time(0) {
	// Context creation must happen on the main/UI thread
	m_glContext = std::make_unique<wxGLContext>(this, g_gui.GetGLContext(this));
	if (!m_glContext->IsOK()) {
		spdlog::error("MapCanvas: Failed to create wxGLContext");
		m_glContext.reset();
	}

	popup_menu = std::make_unique<MapPopupMenu>(editor);
	animation_timer = std::make_unique<AnimationTimer>(this);
	drawer = std::make_unique<MapDrawer>(this);
	selection_controller = std::make_unique<SelectionController>(this, editor);
	drawing_controller = std::make_unique<DrawingController>(this, editor);
	screenshot_controller = std::make_unique<ScreenshotController>(this);
	radial_wheel = std::make_unique<RadialWheel>();
	menu_handler = std::make_unique<MapMenuHandler>(this, editor);
	menu_handler->BindEvents();
	keyCode = WXK_NONE;

	Bind(wxEVT_KEY_DOWN, &MapCanvas::OnKeyDown, this);
	Bind(wxEVT_KEY_UP, &MapCanvas::OnKeyUp, this);

	Bind(wxEVT_MOTION, &MapCanvas::OnMouseMove, this);
	Bind(wxEVT_LEFT_UP, &MapCanvas::OnMouseLeftRelease, this);
	Bind(wxEVT_LEFT_DOWN, &MapCanvas::OnMouseLeftClick, this);
	Bind(wxEVT_LEFT_DCLICK, &MapCanvas::OnMouseLeftDoubleClick, this);
	Bind(wxEVT_MIDDLE_DOWN, &MapCanvas::OnMouseCenterClick, this);
	Bind(wxEVT_MIDDLE_UP, &MapCanvas::OnMouseCenterRelease, this);
	Bind(wxEVT_RIGHT_DOWN, &MapCanvas::OnMouseRightClick, this);
	Bind(wxEVT_RIGHT_UP, &MapCanvas::OnMouseRightRelease, this);
	Bind(wxEVT_MOUSEWHEEL, &MapCanvas::OnWheel, this);
	Bind(wxEVT_ENTER_WINDOW, &MapCanvas::OnGainMouse, this);
	Bind(wxEVT_LEAVE_WINDOW, &MapCanvas::OnLoseMouse, this);

	Bind(wxEVT_PAINT, &MapCanvas::OnPaint, this);
	Bind(wxEVT_ERASE_BACKGROUND, &MapCanvas::OnEraseBackground, this);

	camera_path_timer.SetOwner(this);
	Bind(wxEVT_TIMER, &MapCanvas::OnCameraPathTimer, this, camera_path_timer.GetId());
}

MapCanvas::~MapCanvas() {
	camera_path_timer.Stop();

	bool context_ok = false;
	if (m_glContext) {
		context_ok = g_gl_context.EnsureContextCurrent(*m_glContext, this);
	} else if (auto context = g_gui.GetGLContext(this)) {
		context_ok = g_gl_context.EnsureContextCurrent(*context, this);
	}

	if (!context_ok) {
		spdlog::warn("MapCanvas: Destroying canvas without a current OpenGL context. Cleanup might fail or assert.");
	}

	drawer.reset();
	m_nvg.reset();

	g_gl_context.UnregisterCanvas(this);
}

void MapCanvas::Refresh() {
	if (refresh_watch.Time() > g_settings.getInteger(Config::HARD_REFRESH_RATE)) {
		refresh_watch.Start();
		wxGLCanvas::Update();
	}
	wxGLCanvas::Refresh();
}

void MapCanvas::SetZoom(double value) {
	ZoomController::SetZoom(this, value);
}

void MapCanvas::GetViewBox(int* view_scroll_x, int* view_scroll_y, int* screensize_x, int* screensize_y) const {
	if (auto mw = GetMapWindow()) {
		mw->GetViewSize(screensize_x, screensize_y);
		mw->GetViewStart(view_scroll_x, view_scroll_y);
	} else {
		if (screensize_x) *screensize_x = GetSize().x;
		if (screensize_y) *screensize_y = GetSize().y;
		if (view_scroll_x) *view_scroll_x = 0;
		if (view_scroll_y) *view_scroll_y = 0;
	}
}

MapWindow* MapCanvas::GetMapWindow() const {
	return wxDynamicCast(GetParent(), MapWindow);
}

void MapCanvas::EnsureNanoVG() {
	if (!m_nvg) {
		if (!gladLoadGL()) {
			spdlog::error("MapCanvas: Failed to initialize GLAD");
		}
		m_nvg.reset(nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES));
		if (m_nvg) {
			TextRenderer::LoadFont(m_nvg.get());
		} else {
			spdlog::error("MapCanvas: Failed to initialize NanoVG");
		}
	}
}

void MapCanvas::DrawOverlays(NVGcontext* vg, const DrawingOptions& options) {
	if (!vg) {
		return;
	}

	// Sanitize state before handover to NanoVG
	glUseProgram(0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	glClear(GL_STENCIL_BUFFER_BIT);
	TextRenderer::BeginFrame(vg, GetSize().x, GetSize().y, GetContentScaleFactor());

	if (options.show_creatures && options.show_creature_names) {
		drawer->DrawCreatureNames(vg);
	}
	if (options.show_tooltips) {
		drawer->DrawTooltips(vg);
	}
	if (options.show_hooks) {
		drawer->DrawHookIndicators(vg);
	}
	if (options.show_tech_items) {
		drawer->DrawLightIndicators(vg);
	}
	if (options.highlight_locked_doors) {
		drawer->DrawDoorIndicators(vg);
	}
	if (options.show_mountain_overlay) {
		drawer->DrawMountainOverlay(vg);
	}
	if (options.show_wall_borders) {
		drawer->DrawWallBorders(vg);
	}
	if (options.show_stair_direction) {
		drawer->DrawStairDirections(vg);
	}
	if (options.show_spawns) {
		drawer->DrawSpawnOverlays(vg);
	}
	if (drawer->getLuaOverlayDrawer()) {
		drawer->getLuaOverlayDrawer()->DrawUI(vg, drawer->getView(), options);
	}

	// Draw toast notifications
	if (g_toast.HasActiveToasts()) {
		g_toast.Draw(vg, GetSize().x, GetSize().y);
		// Keep refreshing while toasts are animating
		if (g_toast.HasActiveToasts()) {
			Refresh();
		}
	}

	// Draw radial wheel overlay (always on top)
	if (radial_wheel && radial_wheel->IsOpen()) {
		radial_wheel->Draw(vg, GetSize().x, GetSize().y);
	}

	TextRenderer::EndFrame(vg);

	// Sanitize state after NanoVG to avoid polluting the next frame or other tabs
	glUseProgram(0);
	glBindVertexArray(0);
}

void MapCanvas::PerformGarbageCollection() {
	// Clean unused textures once every second
	// Only run GC if this is the active tab to prevent multiple tabs from fighting over resources
	long current_time = wxGetLocalTime();
	if (current_time - m_last_gc_time >= 1 && g_gui.GetCurrentMapTab() == GetParent()) {
		g_gui.gfx.garbageCollection();
		m_last_gc_time = current_time;
	}
}

void MapCanvas::OnPaint(wxPaintEvent& event) {
	wxPaintDC dc(this); // validates the paint event
	if (m_glContext) {
		g_gl_context.EnsureContextCurrent(*m_glContext, this);
		g_gl_context.SetFallbackCanvas(this);
	}

	EnsureNanoVG();

	if (g_gui.IsRenderingEnabled()) {
		// Advance graphics clock and drain the preloader queue before rendering
		g_gui.gfx.updateTime();

		DrawingOptions& options = drawer->getOptions();
		if (screenshot_controller->IsCapturing()) {
			options.SetIngame();
		} else {
			options.Update();
		}

		options.dragging = selection_controller->IsDragging();
		options.boundbox_selection = selection_controller->IsBoundboxSelection();
		options.lasso_selection = selection_controller->IsLassoSelection();

		if (options.show_preview) {
			animation_timer->Start();
			g_gui.gfx.resumeAnimation();
		} else {
			animation_timer->Stop();
			g_gui.gfx.pauseAnimation();
		}

		// BatchRenderer calls removed - MapDrawer handles its own renderers

		drawer->SetupVars();
		drawer->SetupGL();
		drawer->Draw();

		if (screenshot_controller->IsCapturing()) {
			drawer->TakeScreenshot(screenshot_controller->GetBuffer());
		}

		drawer->Release();

		// Draw UI (Tooltips, Overlays & HUD) using NanoVG
		DrawOverlays(m_nvg.get(), options);

		drawer->ClearFrameOverlays();
	}

	PerformGarbageCollection();

	SwapBuffers();

	// FPS tracking and limiting
	frame_pacer.UpdateAndLimit(g_settings.getInteger(Config::FRAME_RATE_LIMIT), g_settings.getBoolean(Config::SHOW_FPS_COUNTER));

	// Send newd node requests
	if (editor.live_manager.GetClient()) {
		editor.live_manager.GetClient()->sendNodeRequests();
	}
}

void MapCanvas::TakeScreenshot(wxFileName path, wxString format) {
	screenshot_controller->TakeScreenshot(path, format);
}

void MapCanvas::ScreenToMap(int screen_x, int screen_y, int* map_x, int* map_y) {
	int start_x = 0, start_y = 0;
	if (auto mw = GetMapWindow()) {
		mw->GetViewStart(&start_x, &start_y);
	}

	CoordinateMapper::ScreenToMap(screen_x, screen_y, start_x, start_y, zoom, floor, GetContentScaleFactor(), map_x, map_y);
}
#if 0

*map_y = int(start_y + (screen_y * zoom)) / TILE_SIZE;
}

if (floor <= GROUND_LAYER) {
	*map_x += GROUND_LAYER - floor;
	*map_y += GROUND_LAYER - floor;
} /* else {
	 *map_x += MAP_MAX_LAYER - floor;
	 *map_y += MAP_MAX_LAYER - floor;
 }*/
}

#endif
void MapCanvas::GetScreenCenter(int* map_x, int* map_y) {
	int width = GetSize().x, height = GetSize().y;
	if (auto mw = GetMapWindow()) {
		mw->GetViewSize(&width, &height);
	}
	ScreenToMap(width / 2, height / 2, map_x, map_y);
}

Position MapCanvas::GetCursorPosition() const {
	return Position(last_cursor_map_x, last_cursor_map_y, floor);
}

void MapCanvas::UpdatePositionStatus(int x, int y) {
	if (x == -1) {
		x = cursor_x;
	}
	if (y == -1) {
		y = cursor_y;
	}

	int map_x, map_y;
	ScreenToMap(x, y, &map_x, &map_y);

	MapStatusUpdater::Update(editor, map_x, map_y, floor);
}

void MapCanvas::UpdateZoomStatus() {
	ZoomController::UpdateStatus(this);
}

void MapCanvas::OnMouseMove(wxMouseEvent& event) {
	// Update radial wheel hover
	if (radial_wheel && radial_wheel->IsOpen()) {
		radial_wheel->UpdateMouse(event.GetX(), event.GetY());
		Refresh();
		return;
	}

	NavigationController::HandleMouseDrag(this, event);

	cursor_x = event.GetX();
	cursor_y = event.GetY();

	int mouse_map_x, mouse_map_y;
	MouseToMap(&mouse_map_x, &mouse_map_y);

	bool map_update = false;
	if (last_cursor_map_x != mouse_map_x || last_cursor_map_y != mouse_map_y || last_cursor_map_z != floor) {
		map_update = true;
	}
	last_cursor_map_x = mouse_map_x;
	last_cursor_map_y = mouse_map_y;
	last_cursor_map_z = floor;

	if (map_update) {
		g_gui.UpdateAutoborderPreview(Position(mouse_map_x, mouse_map_y, floor));
		UpdatePositionStatus(cursor_x, cursor_y);
		UpdateZoomStatus();
		Refresh();
	}

	if (g_gui.IsSelectionMode()) {
		selection_controller->HandleDrag(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown(), event.ControlDown(), event.AltDown());
	} else { // Drawing mode
		drawing_controller->HandleDrag(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown(), event.ControlDown(), event.AltDown());
	}
}

void MapCanvas::OnMouseLeftRelease(wxMouseEvent& event) {
	OnMouseActionRelease(event);
}

void MapCanvas::OnMouseLeftClick(wxMouseEvent& event) {
	OnMouseActionClick(event);
}

void MapCanvas::OnMouseLeftDoubleClick(wxMouseEvent& event) {
	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);
	if (selection_controller) {
		selection_controller->HandleDoubleClick(Position(mouse_map_x, mouse_map_y, floor));
	}
}

void MapCanvas::OnMouseCenterClick(wxMouseEvent& event) {
	if (g_settings.getInteger(Config::SWITCH_MOUSEBUTTONS)) {
		OnMousePropertiesClick(event);
	} else {
		OnMouseCameraClick(event);
	}
}

void MapCanvas::OnMouseCenterRelease(wxMouseEvent& event) {
	if (g_settings.getInteger(Config::SWITCH_MOUSEBUTTONS)) {
		OnMousePropertiesRelease(event);
	} else {
		OnMouseCameraRelease(event);
	}
}

void MapCanvas::OnMouseRightClick(wxMouseEvent& event) {
	if (g_settings.getInteger(Config::SWITCH_MOUSEBUTTONS)) {
		OnMouseCameraClick(event);
	} else {
		OnMousePropertiesClick(event);
	}
}

void MapCanvas::OnMouseRightRelease(wxMouseEvent& event) {
	if (g_settings.getInteger(Config::SWITCH_MOUSEBUTTONS)) {
		OnMouseCameraRelease(event);
	} else {
		OnMousePropertiesRelease(event);
	}
}

void MapCanvas::OnMouseActionClick(wxMouseEvent& event) {
	SetFocus();

	// If radial wheel is open, confirm selection on click
	if (radial_wheel && radial_wheel->IsOpen()) {
		radial_wheel->Confirm();
		Refresh();
		return;
	}

	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);

	// Rectangle pick mode for area decoration dialog
	if (g_gui.IsRectanglePicking()) {
		g_gui.OnRectanglePickClick(Position(mouse_map_x, mouse_map_y, floor));
		g_gui.RefreshView();
		return;
	}

	// Alt+Click: add camera path keyframe at click position (when camera path palette is active)
	if (event.AltDown() && !event.ControlDown() && g_gui.GetCurrentBrush() && g_gui.GetCurrentBrush()->is<CameraPathBrush>()) {
		PaletteWindow* palette = g_gui.GetPalette();
		CameraPathPalettePanel* camPalette = palette ? palette->GetCameraPathPalette() : nullptr;
		if (camPalette) {
			Position clickPos(mouse_map_x, mouse_map_y, floor);
			CameraPaths temp = editor.map.camera_paths;
			CameraPath* path = temp.getActivePath();
			if (!path) {
				path = temp.addPath("Path");
			}

			CameraKeyframe key;
			key.pos = clickPos;
			key.duration = camPalette->GetKeyframeDuration();
			key.speed = camPalette->GetKeyframeSpeed();
			key.zoom = camPalette->GetKeyframeZoom();
			key.easing = static_cast<CameraEasing>(camPalette->GetKeyframeEasing());

			int insertIndex = static_cast<int>(path->keyframes.size());
			int activeIndex = temp.getActiveKeyframe();
			if (activeIndex >= 0 && activeIndex < static_cast<int>(path->keyframes.size())) {
				insertIndex = activeIndex + 1;
			}
			path->keyframes.insert(path->keyframes.begin() + insertIndex, key);
			temp.setActiveKeyframe(insertIndex);

			Editor* ed = g_gui.GetCurrentEditor();
			if (ed) {
				ed->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_DRAW);
			}
		}
	} else if (event.ControlDown() && event.AltDown()) {
		Tile* tile = editor.map.getTile(mouse_map_x, mouse_map_y, floor);
		BrushSelector::SelectSmartBrush(editor, tile);
	} else if (g_gui.IsSelectionMode()) {
		if (selection_controller) {
			selection_controller->HandleClick(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown(), event.ControlDown(), event.AltDown());
		}
	} else if (g_gui.GetCurrentBrush()) { // Drawing mode
		if (drawing_controller) {
			drawing_controller->HandleClick(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown(), event.ControlDown(), event.AltDown());
		}
	}
	last_click_x = int(event.GetX() * zoom);
	last_click_y = int(event.GetY() * zoom);

	int start_x, start_y;
	if (auto mw = GetMapWindow()) {
		mw->GetViewStart(&start_x, &start_y);
	} else {
		start_x = 0; start_y = 0;
	}
	last_click_abs_x = last_click_x + start_x;
	last_click_abs_y = last_click_y + start_y;

	last_click_map_x = mouse_map_x;
	last_click_map_y = mouse_map_y;
	last_click_map_z = floor;
	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void MapCanvas::OnMouseActionRelease(wxMouseEvent& event) {
	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);

	int move_x = last_click_map_x - mouse_map_x;
	int move_y = last_click_map_y - mouse_map_y;
	int move_z = last_click_map_z - floor;

	if (g_gui.IsSelectionMode()) {
		if (selection_controller) {
			selection_controller->HandleRelease(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown(), event.ControlDown(), event.AltDown());
		}
	} else if (g_gui.GetCurrentBrush()) { // Drawing mode
		if (drawing_controller) {
			drawing_controller->HandleRelease(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown(), event.ControlDown(), event.AltDown());
		}
	}
	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void MapCanvas::OnMouseCameraClick(wxMouseEvent& event) {
	SetFocus();

	last_mmb_click_x = event.GetX();
	last_mmb_click_y = event.GetY();
	if (event.ControlDown()) {
		ZoomController::ApplyRelativeZoom(this, 1.0 - zoom);
	} else {
		NavigationController::HandleCameraClick(this, event);
	}
}

void MapCanvas::OnMouseCameraRelease(wxMouseEvent& event) {
	NavigationController::HandleCameraRelease(this, event);
}

void MapCanvas::OnMousePropertiesClick(wxMouseEvent& event) {
	SetFocus();

	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);
	Tile* tile = editor.map.getTile(mouse_map_x, mouse_map_y, floor);

	if (g_gui.IsDrawingMode()) {
		g_gui.SetSelectionMode();
	}

	last_click_x = int(event.GetX() * zoom);
	last_click_y = int(event.GetY() * zoom);
	selection_controller->HandlePropertiesClick(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown(), event.ControlDown(), event.AltDown());

	int start_x = 0, start_y = 0;
	if (auto mw = GetMapWindow()) {
		mw->GetViewStart(&start_x, &start_y);
	}
	last_click_abs_x = last_click_x + start_x;
	last_click_abs_y = last_click_y + start_y;

	last_click_map_x = mouse_map_x;
	last_click_map_y = mouse_map_y;
	g_gui.RefreshView();
}

void MapCanvas::OnMousePropertiesRelease(wxMouseEvent& event) {
	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);

	if (g_gui.IsDrawingMode()) {
		g_gui.SetSelectionMode();
	}

	selection_controller->HandlePropertiesRelease(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown(), event.ControlDown(), event.AltDown());

	popup_menu->Update();
	PopupMenu(popup_menu.get());

	editor.actionQueue->resetTimer();
	dragging = false;
	boundbox_selection = false;

	last_cursor_map_x = mouse_map_x;
	last_cursor_map_y = mouse_map_y;
	last_cursor_map_z = floor;

	g_gui.RefreshView();
}

void MapCanvas::OnWheel(wxMouseEvent& event) {
	if (event.ControlDown()) {
		NavigationController::HandleWheel(this, event);
	} else if (event.AltDown()) {
		drawing_controller->HandleWheel(event.GetWheelRotation(), event.AltDown(), event.ControlDown());
	} else {
		ZoomController::OnWheel(this, event);
	}

	Refresh();
}

void MapCanvas::OnLoseMouse(wxMouseEvent& event) {
	Refresh();
}

void MapCanvas::OnGainMouse(wxMouseEvent& event) {
	if (!event.LeftIsDown()) {
		dragging = false;
		boundbox_selection = false;
		drawing_controller->Reset();
	}
	if (!event.MiddleIsDown()) {
		screendragging = false;
	}

	Refresh();
}

void MapCanvas::OnKeyDown(wxKeyEvent& event) {
	KeyboardHandler::OnKeyDown(this, event);
}

void MapCanvas::OnKeyUp(wxKeyEvent& event) {
	KeyboardHandler::OnKeyUp(this, event);
}

void MapCanvas::ChangeFloor(int new_floor) {
	NavigationController::ChangeFloor(this, new_floor);
}

void MapCanvas::EnterDrawingMode() {
	dragging = false;
	boundbox_selection = false;
	EndPasting();
	Refresh();
}

void MapCanvas::EnterSelectionMode() {
	drawing_controller->Reset();
	editor.replace_brush = nullptr;
	Refresh();
}

bool MapCanvas::isPasting() const {
	return g_gui.IsPasting();
}

void MapCanvas::StartPasting() {
	g_gui.StartPasting();
}

void MapCanvas::EndPasting() {
	g_gui.EndPasting();
}

void MapCanvas::Reset() {
	if (camera_path_playing) {
		camera_path_timer.Stop();
		camera_path_playing = false;
		camera_path_time = 0.0;
		camera_path_name.clear();
	}

	cursor_x = 0;
	cursor_y = 0;

	zoom = 1.0;
	floor = GROUND_LAYER;

	dragging = false;
	boundbox_selection = false;
	screendragging = false;
	drawing_controller->Reset();

	editor.replace_brush = nullptr;

	last_click_map_x = -1;
	last_click_map_y = -1;
	last_click_map_z = -1;

	last_mmb_click_x = -1;
	last_mmb_click_y = -1;

	editor.selection.clear();
	editor.actionQueue->clear();
}

void MapCanvas::ToggleCameraPathPlayback() {
	if (camera_path_playing) {
		camera_path_timer.Stop();
		camera_path_playing = false;
		camera_path_time = 0.0;
		camera_path_name.clear();
		g_gui.SetStatusText("Camera path playback stopped.");
		return;
	}

	CameraPaths& cameraPaths = editor.map.camera_paths;
	CameraPath* path = cameraPaths.getActivePath();
	if (!path || path->keyframes.size() < 2) {
		g_gui.SetStatusText("No camera path with at least 2 keyframes selected.");
		return;
	}

	camera_path_name = path->name;
	camera_path_time = 0.0;
	camera_path_last_tick = std::chrono::steady_clock::now();
	camera_path_playing = true;
	camera_path_timer.Start(16); // ~60fps
	g_gui.SetStatusText("Camera path playback started.");
}

void MapCanvas::OnCameraPathTimer(wxTimerEvent& WXUNUSED(event)) {
	if (!camera_path_playing) {
		return;
	}

	CameraPaths& cameraPaths = editor.map.camera_paths;
	CameraPath* path = cameraPaths.getPath(camera_path_name);
	if (!path || path->keyframes.size() < 2) {
		ToggleCameraPathPlayback();
		return;
	}

	auto now = std::chrono::steady_clock::now();
	std::chrono::duration<double> delta = now - camera_path_last_tick;
	camera_path_last_tick = now;
	camera_path_time += delta.count();

	bool finished = false;
	CameraPathSample sample = SampleCameraPathByTime(*path, camera_path_time, path->loop, &finished);

	const int sample_z = static_cast<int>(std::round(sample.z));
	MapWindow* window = GetMapWindow();
	if (window) {
		SetZoom(sample.zoom);
		window->SetScreenCenterPosition(sample.x, sample.y, sample_z);
	}
	Refresh();

	if (finished && !path->loop) {
		ToggleCameraPathPlayback();
	}
}
