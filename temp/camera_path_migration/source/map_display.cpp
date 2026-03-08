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

#include "main.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <time.h>
#include <unordered_set>
#include <wx/accel.h>
#include <wx/wfstream.h>
#include <wx/filefn.h>

#include "gui.h"
#include "hotkey_utils.h"
#include "editor.h"
#include "brush.h"
#include "sprites.h"
#include "map.h"
#include "tile.h"
#include "old_properties_window.h"
#include "properties_window.h"
#include "palette_window.h"
#include "map_display.h"
#include "map_drawer.h"
#include "application.h"
#include "live_server.h"
#include "structure_manager_window.h"

#include "browse_tile_window.h"
#include "map_window.h"
#include "theme.h"

#include "doodad_brush.h"
#include "house_exit_brush.h"
#include "house_brush.h"
#include "wall_brush.h"
#include "spawn_brush.h"
#include "creature_brush.h"
#include "ground_brush.h"
#include "waypoint_brush.h"
#include "raw_brush.h"
#include "carpet_brush.h"
#include "table_brush.h"
#include "creature_walk_animator.h"
#include "gif_recorder.h"
#include "camera_path.h"
#include "area_decoration_rule_from_selection_dialog.h"
#include "area_decoration_dialog.h"

namespace
{
MouseActionID ActionForButton(MouseButtonBinding button)
{
	if(GetMouseBinding(MouseActionID::PrimaryAction) == button)
		return MouseActionID::PrimaryAction;
	if(GetMouseBinding(MouseActionID::Camera) == button)
		return MouseActionID::Camera;
	if(GetMouseBinding(MouseActionID::Properties) == button)
		return MouseActionID::Properties;
	return MouseActionID::Count;
}

int GetModifierMask(const wxMouseEvent& event)
{
	int mask = 0;
	if(event.ControlDown())
		mask |= wxACCEL_CTRL;
	if(event.AltDown())
		mask |= wxACCEL_ALT;
	if(event.ShiftDown())
		mask |= wxACCEL_SHIFT;
	return mask;
}

void DispatchMousePress(MouseButtonBinding button, MapCanvas& canvas, wxMouseEvent& event)
{
	switch(ActionForButton(button)) {
		case MouseActionID::PrimaryAction:
			canvas.OnMouseActionClick(event);
			break;
		case MouseActionID::Camera:
			canvas.OnMouseCameraClick(event);
			break;
		case MouseActionID::Properties:
			canvas.OnMousePropertiesClick(event);
			break;
		default:
			break;
	}
}

void DispatchMouseRelease(MouseButtonBinding button, MapCanvas& canvas, wxMouseEvent& event)
{
	switch(ActionForButton(button)) {
		case MouseActionID::PrimaryAction:
			canvas.OnMouseActionRelease(event);
			break;
		case MouseActionID::Camera:
			canvas.OnMouseCameraRelease(event);
			break;
		case MouseActionID::Properties:
			canvas.OnMousePropertiesRelease(event);
			break;
		default:
			break;
	}
}
}

namespace
{
	void AddTileOrCreaturesToSelection(Selection& selection, Tile* tile, bool creaturesOnly, Map& map);
	void RemoveTileOrCreaturesFromSelection(Selection& selection, Tile* tile, bool creaturesOnly, Map& map);
	bool IsPointInPolygon(const std::vector<wxPoint>& polygon, double x, double y);
}


BEGIN_EVENT_TABLE(MapCanvas, wxGLCanvas)
	EVT_KEY_DOWN(MapCanvas::OnKeyDown)
	EVT_KEY_UP(MapCanvas::OnKeyUp)

	// Mouse events
	EVT_MOTION(MapCanvas::OnMouseMove)
	EVT_LEFT_UP(MapCanvas::OnMouseLeftRelease)
	EVT_LEFT_DOWN(MapCanvas::OnMouseLeftClick)
	EVT_LEFT_DCLICK(MapCanvas::OnMouseLeftDoubleClick)
	EVT_MIDDLE_DCLICK(MapCanvas::OnMouseMiddleDoubleClick)
	EVT_MIDDLE_DOWN(MapCanvas::OnMouseCenterClick)
	EVT_MIDDLE_UP(MapCanvas::OnMouseCenterRelease)
	EVT_RIGHT_DCLICK(MapCanvas::OnMouseRightDoubleClick)
	EVT_RIGHT_DOWN(MapCanvas::OnMouseRightClick)
	EVT_RIGHT_UP(MapCanvas::OnMouseRightRelease)
	EVT_MOUSE_EVENTS(MapCanvas::OnMouseAuxEvent)
	EVT_MOUSEWHEEL(MapCanvas::OnWheel)
	EVT_ENTER_WINDOW(MapCanvas::OnGainMouse)
	EVT_LEAVE_WINDOW(MapCanvas::OnLoseMouse)
	EVT_TIMER(CAMERA_PATH_TIMER, MapCanvas::OnCameraPathTimer)

	//Drawing events
	EVT_PAINT(MapCanvas::OnPaint)
	EVT_ERASE_BACKGROUND(MapCanvas::OnEraseBackground)

	// Menu events
	EVT_MENU(MAP_POPUP_MENU_CUT, MapCanvas::OnCut)
	EVT_MENU(MAP_POPUP_MENU_COPY, MapCanvas::OnCopy)
	EVT_MENU(MAP_POPUP_MENU_COPY_POSITION, MapCanvas::OnCopyPosition)
	EVT_MENU(MAP_POPUP_MENU_PASTE, MapCanvas::OnPaste)
	EVT_MENU(MAP_POPUP_MENU_DELETE, MapCanvas::OnDelete)
	//----
	EVT_MENU(MAP_POPUP_MENU_COPY_SERVER_ID, MapCanvas::OnCopyServerId)
	EVT_MENU(MAP_POPUP_MENU_COPY_CLIENT_ID, MapCanvas::OnCopyClientId)
	EVT_MENU(MAP_POPUP_MENU_COPY_NAME, MapCanvas::OnCopyName)
	EVT_MENU(MAP_POPUP_MENU_REMOVE_ITEMS, MapCanvas::OnDeleteAll)
	EVT_MENU(MAP_POPUP_MENU_APPLY_REPLACE_BOX1, MapCanvas::OnApplyReplaceBox1)
	EVT_MENU(MAP_POPUP_MENU_APPLY_REPLACE_BOX2, MapCanvas::OnApplyReplaceBox2)
	// ----
	EVT_MENU(MAP_POPUP_MENU_ROTATE, MapCanvas::OnRotateItem)
	EVT_MENU(MAP_POPUP_MENU_ROTATE_SELECTION_CW, MapCanvas::OnRotateSelectionCW)
	EVT_MENU(MAP_POPUP_MENU_ROTATE_SELECTION_CCW, MapCanvas::OnRotateSelectionCCW)
	EVT_MENU(MAP_POPUP_MENU_ROTATE_SELECTION_180, MapCanvas::OnRotateSelection180)
	EVT_MENU(MAP_POPUP_MENU_GOTO, MapCanvas::OnGotoDestination)
	EVT_MENU(MAP_POPUP_MENU_COPY_DESTINATION, MapCanvas::OnCopyDestination)
	EVT_MENU(MAP_POPUP_MENU_SWITCH_DOOR, MapCanvas::OnSwitchDoor)
	// ----
	EVT_MENU(MAP_POPUP_MENU_SELECT_RAW_BRUSH, MapCanvas::OnSelectRAWBrush)
	EVT_MENU(MAP_POPUP_MENU_SELECT_GROUND_BRUSH, MapCanvas::OnSelectGroundBrush)
	EVT_MENU(MAP_POPUP_MENU_SELECT_DOODAD_BRUSH, MapCanvas::OnSelectDoodadBrush)
	EVT_MENU(MAP_POPUP_MENU_SELECT_DOOR_BRUSH, MapCanvas::OnSelectDoorBrush)
	EVT_MENU(MAP_POPUP_MENU_SELECT_WALL_BRUSH, MapCanvas::OnSelectWallBrush)
	EVT_MENU(MAP_POPUP_MENU_SELECT_CARPET_BRUSH, MapCanvas::OnSelectCarpetBrush)
	EVT_MENU(MAP_POPUP_MENU_SELECT_TABLE_BRUSH, MapCanvas::OnSelectTableBrush)
	EVT_MENU(MAP_POPUP_MENU_SELECT_CREATURE_BRUSH, MapCanvas::OnSelectCreatureBrush)
	EVT_MENU(MAP_POPUP_MENU_SELECT_SPAWN_BRUSH, MapCanvas::OnSelectSpawnBrush)
	EVT_MENU(MAP_POPUP_MENU_SELECT_HOUSE_BRUSH, MapCanvas::OnSelectHouseBrush)
	// ----
	EVT_MENU(MAP_POPUP_MENU_TOGGLE_CARPET_ACTIVATED, MapCanvas::OnToggleCarpetActivated)
	EVT_MENU(MAP_POPUP_MENU_TOGGLE_DOODAD_ACTIVATED, MapCanvas::OnToggleDoodadActivated)
	// ----
	EVT_MENU(MAP_POPUP_MENU_PROPERTIES, MapCanvas::OnProperties)
	// ----
	EVT_MENU(MAP_POPUP_MENU_BROWSE_TILE, MapCanvas::OnBrowseTile)
	// ----
	EVT_MENU(MAP_POPUP_MENU_ADD_AREA_DECORATION_RULE, MapCanvas::OnAddAreaDecorationRule)
END_EVENT_TABLE()

bool MapCanvas::processed[] = {0};

MapCanvas::MapCanvas(MapWindow* parent, Editor& editor, int* attriblist) :
	wxGLCanvas(parent, wxID_ANY, nullptr, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS),
	editor(editor),
	floor(rme::MapGroundLayer),
	zoom(1.0),
	preview_mode(false),
	cursor_x(-1),
	cursor_y(-1),
	cursor_in_window(false),
	dragging(false),
	boundbox_selection(false),
	boundbox_select_creatures(false),
	boundbox_deselect(false),
	screendragging(false),
	suppress_right_release(false),
	drawing(false),
	dragging_draw(false),
	replace_dragging(false),
	alt_ground_mode(false),
	autoborder_preview_map(newd BaseMap()),
	autoborder_preview_active(false),
	last_preview_map_x(-1),
	last_preview_map_y(-1),
	last_preview_map_z(-1),
	last_preview_brush(nullptr),
	last_preview_brush_size(-1),
	last_preview_brush_shape(-1),
	last_preview_alt(false),

	screenshot_buffer(nullptr),
	gif_writer(nullptr),
	gif_recording(false),
	gif_fps(0),
	gif_width(0),
	gif_height(0),
	gif_frames_written(0),
	gif_frame_interval(std::chrono::steady_clock::duration::zero()),
	camera_path_timer(this, CAMERA_PATH_TIMER),
	camera_path_playing(false),
	camera_path_time(0.0),
	camera_path_last_tick(),

	floor_fading(false),
	floor_fade_old_floor(rme::MapGroundLayer),

	drag_start_x(-1),
	drag_start_y(-1),
	drag_start_z(-1),

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
	gc_frame_counter(0)
{
	popup_menu = newd MapPopupMenu(editor);
	animation_timer = newd AnimationTimer(this);
	drawer = new MapDrawer(this);
	keyCode = WXK_NONE;
}

void MapCanvas::ClearLassoSelection()
{
	lasso_screen_points.clear();
	lasso_map_points.clear();
}

void MapCanvas::StartLassoSelection(int screen_x, int screen_y, int map_x, int map_y)
{
	ClearLassoSelection();
	AddLassoPoint(screen_x, screen_y, map_x, map_y);
}

void MapCanvas::AddLassoPoint(int screen_x, int screen_y, int map_x, int map_y)
{
	const int min_dist_sq = 36;
	if(!lasso_screen_points.empty()) {
		const wxPoint& last = lasso_screen_points.back();
		int dx = screen_x - last.x;
		int dy = screen_y - last.y;
		if(dx * dx + dy * dy < min_dist_sq) {
			return;
		}
	}

	lasso_screen_points.push_back(wxPoint(screen_x, screen_y));
	lasso_map_points.push_back(wxPoint(map_x, map_y));
}

void MapCanvas::ApplyLassoSelection(Selection& selection, bool creaturesOnly, bool removeSelection)
{
	if(lasso_map_points.size() < 3) {
		return;
	}

	int min_x = lasso_map_points.front().x;
	int max_x = lasso_map_points.front().x;
	int min_y = lasso_map_points.front().y;
	int max_y = lasso_map_points.front().y;
	for(const wxPoint& point : lasso_map_points) {
		min_x = std::min(min_x, point.x);
		max_x = std::max(max_x, point.x);
		min_y = std::min(min_y, point.y);
		max_y = std::max(max_y, point.y);
	}

	int start_z = floor;
	int end_z = floor;
	switch(g_settings.getInteger(Config::SELECTION_TYPE)) {
		case SELECT_ALL_FLOORS:
			start_z = rme::MapMaxLayer;
			end_z = floor;
			break;
		case SELECT_VISIBLE_FLOORS:
			start_z = (floor < 8) ? rme::MapGroundLayer : std::min(rme::MapMaxLayer, floor + 2);
			end_z = floor;
			break;
		case SELECT_CURRENT_FLOOR:
		default:
			start_z = end_z = floor;
			break;
	}

	const bool compensated = g_settings.getInteger(Config::COMPENSATED_SELECT) &&
		floor < rme::MapGroundLayer;

	Map& map = editor.getMap();
	for(int z = start_z; z >= end_z; --z) {
		int offset = 0;
		if(compensated) {
			if(z > rme::MapGroundLayer) {
				offset = floor - rme::MapGroundLayer;
			} else {
				offset = floor - z;
			}
		}

		const int start_x = min_x + offset;
		const int end_x = max_x + offset;
		const int start_y = min_y + offset;
		const int end_y = max_y + offset;

		for(int x = start_x; x <= end_x; ++x) {
			for(int y = start_y; y <= end_y; ++y) {
				double px = x + 0.5;
				double py = y + 0.5;
				if(IsPointInPolygon(lasso_map_points, px - offset, py - offset)) {
					Tile* tile = map.getTile(x, y, z);
					if(removeSelection) {
						RemoveTileOrCreaturesFromSelection(selection, tile, creaturesOnly, map);
					} else {
						AddTileOrCreaturesToSelection(selection, tile, creaturesOnly, map);
					}
				}
			}
		}
	}
}

bool MapCanvas::IsLassoSelectionEnabled() const noexcept
{
	return g_settings.getBoolean(Config::SELECTION_LASSO);
}

bool MapCanvas::HasLassoSelection() const noexcept
{
	return !lasso_map_points.empty();
}

namespace OnMapRemoveItems
{
	struct RemoveItemCondition
	{
		RemoveItemCondition(uint16_t itemId) :
			itemId(itemId) { }

		uint16_t itemId;

		bool operator()(Map& map, Item* item, int64_t removed, int64_t done) {
			if(done % 0x8000 == 0)
				g_gui.SetLoadDone((uint32_t)(100 * done / map.getTileCount()));
			return item->getID() == itemId && !item->isComplex();
		}
	};
}

namespace
{
	std::vector<Tile*> GetSpawnTilesForTile(Map& map, Tile* tile)
	{
		std::vector<Tile*> spawnTiles;
		if(!tile) {
			return spawnTiles;
		}

		const TileLocation* location = tile->getLocation();
		if(!location || location->getSpawnCount() == 0) {
			return spawnTiles;
		}

		// Use unordered_set for O(1) duplicate checking instead of O(n) std::find
		std::unordered_set<Tile*> spawnTilesSet;
		spawnTiles.reserve(location->getSpawnCount());

		auto pushSpawnTile = [&spawnTiles, &spawnTilesSet](Tile* candidate) {
			if(!candidate || !candidate->spawn) {
				return;
			}
			if(spawnTilesSet.insert(candidate).second) {
				spawnTiles.push_back(candidate);
			}
		};

		pushSpawnTile(tile);
		const Position& position = tile->getPosition();
		int start_x = position.x - 1;
		int end_x = position.x + 1;
		int start_y = position.y - 1;
		int end_y = position.y + 1;

		const size_t targetCount = location->getSpawnCount();
		while(spawnTiles.size() < targetCount) {
			for(int x = start_x; x <= end_x && spawnTiles.size() < targetCount; ++x) {
				pushSpawnTile(map.getTile(x, start_y, position.z));
				pushSpawnTile(map.getTile(x, end_y, position.z));
			}

			for(int y = start_y + 1; y < end_y && spawnTiles.size() < targetCount; ++y) {
				pushSpawnTile(map.getTile(start_x, y, position.z));
				pushSpawnTile(map.getTile(end_x, y, position.z));
			}

			--start_x;
			--start_y;
			++end_x;
			++end_y;
		}

		return spawnTiles;
	}

	void AddCreatureWithSpawn(Selection& selection, Tile* tile, Map& map)
	{
		if(!tile || !tile->creature) {
			return;
		}
		selection.add(tile, tile->creature);
	}

	void RemoveCreatureWithSpawn(Selection& selection, Tile* tile, Map& map)
	{
		if(!tile || !tile->creature) {
			return;
		}
		if(tile->creature->isSelected()) {
			selection.remove(tile, tile->creature);
		}
	}

	void AddTileOrCreaturesToSelection(Selection& selection, Tile* tile, bool creaturesOnly, Map& map)
	{
		if(!tile) {
			return;
		}

		if(creaturesOnly) {
			if(tile->spawn && (!tile->creature || !g_settings.getInteger(Config::SHOW_CREATURES))) {
				selection.add(tile, tile->spawn);
			}
			if(tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
				AddCreatureWithSpawn(selection, tile, map);
			}
			return;
		}

		selection.add(tile);
	}

	void RemoveTileOrCreaturesFromSelection(Selection& selection, Tile* tile, bool creaturesOnly, Map& map)
	{
		if(!tile) {
			return;
		}

		if(creaturesOnly) {
			if(tile->spawn && tile->spawn->isSelected() && !tile->creature) {
				selection.remove(tile, tile->spawn);
			}
			if(tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
				RemoveCreatureWithSpawn(selection, tile, map);
			}
			return;
		}

		if(tile->isSelected()) {
			selection.remove(tile);
		}
	}

	bool IsPointInPolygon(const std::vector<wxPoint>& polygon, double x, double y)
	{
		if(polygon.size() < 3) {
			return false;
		}

		bool inside = false;
		size_t count = polygon.size();
		for(size_t i = 0, j = count - 1; i < count; j = i++) {
			double xi = polygon[i].x;
			double yi = polygon[i].y;
			double xj = polygon[j].x;
			double yj = polygon[j].y;

			bool intersect = ((yi > y) != (yj > y)) &&
				(x < (xj - xi) * (y - yi) / (yj - yi) + xi);
			if(intersect) {
				inside = !inside;
			}
		}
		return inside;
	}

	bool ToggleTileObjectSelection(Selection& selection, Tile* tile, Map& map)
	{
		if(!tile) {
			return false;
		}

		if(tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
			if(tile->creature->isSelected()) {
				RemoveCreatureWithSpawn(selection, tile, map);
			} else {
				AddCreatureWithSpawn(selection, tile, map);
			}
			return true;
		}

		if(tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
			if(tile->spawn->isSelected()) {
				selection.remove(tile, tile->spawn);
			} else {
				selection.add(tile, tile->spawn);
			}
			return true;
		}

		Item* item = tile->getTopItem();
		if(item) {
			if(item->isSelected()) {
				selection.remove(tile, item);
			} else {
				selection.add(tile, item);
			}
			return true;
		}

		return false;
	}
}


MapCanvas::~MapCanvas()
{
	delete popup_menu;
	delete animation_timer;
	delete drawer;
	delete autoborder_preview_map;
	camera_path_timer.Stop();
	StopGifRecording(false, false);
	free(screenshot_buffer);
}

void MapCanvas::Refresh()
{
	if(refresh_watch.Time() > g_settings.getInteger(Config::HARD_REFRESH_RATE)) {
		refresh_watch.Start();
		wxGLCanvas::Update();
	}
	wxGLCanvas::Refresh();
}

void MapCanvas::SetZoom(double value)
{
	if(value < 0.125)
		value = 0.125;

	if(value > 25.00)
		value = 25.0;

	if(zoom != value) {
		int center_x, center_y;
		GetScreenCenter(&center_x, &center_y);

		zoom = value;
		GetMapWindow()->SetScreenCenterPosition(Position(center_x, center_y, floor));

		UpdatePositionStatus();
		UpdateZoomStatus();
		Refresh();
	}
}

void MapCanvas::ZoomBy(double delta, const wxPoint& anchor)
{
	if(delta == 0.0) {
		return;
	}

	double oldzoom = zoom;
	zoom += delta;

	if(zoom < 0.125) {
		zoom = 0.125;
	}
	if(zoom > 25.00) {
		zoom = 25.0;
	}

	if(std::abs(zoom - oldzoom) < 0.0001) {
		return;
	}

	if(!preview_mode) {
		UpdateZoomStatus();
	}

	int screensize_x = 0;
	int screensize_y = 0;
	MapWindow* window = GetMapWindow();
	if(window) {
		window->GetViewSize(&screensize_x, &screensize_y);
	}
	if(screensize_x > 0 && screensize_y > 0) {
		const double anchor_x = std::max(anchor.x, 1) / double(screensize_x);
		const double anchor_y = std::max(anchor.y, 1) / double(screensize_y);
		const double diff = zoom - oldzoom;
		int scroll_x = int(screensize_x * diff * anchor_x) * GetContentScaleFactor();
		int scroll_y = int(screensize_y * diff * anchor_y) * GetContentScaleFactor();
		window->ScrollRelative(-scroll_x, -scroll_y);
	}

	Refresh();
}

void MapCanvas::SetPreviewMode(bool preview)
{
	preview_mode = preview;
}

void MapCanvas::GetViewBox(int* view_scroll_x, int* view_scroll_y, int* screensize_x, int* screensize_y) const
{
	MapWindow* window = GetMapWindow();
	window->GetViewSize(screensize_x, screensize_y);
	window->GetViewStart(view_scroll_x, view_scroll_y);
}

void MapCanvas::OnPaint(wxPaintEvent& event)
{
	SetCurrent(*g_gui.GetGLContext(this));

	if(g_gui.IsRenderingEnabled()) {
		DrawingOptions& options = drawer->getOptions();
		if(screenshot_buffer) {
			options.SetIngame();
			options.show_lights = true;
			options.light_hour = g_settings.getInteger(Config::LIGHT_HOUR);
		} else {
			// Load all settings at once (optimized - single batch load)
			options.LoadFromSettings();
		}

		options.dragging = boundbox_selection;

		// Always keep animation timer running to update FPS display in real-time
		animation_timer->Start();

		drawer->SetupVars();
		drawer->SetupGL();
		drawer->Draw();

		if(screenshot_buffer)
			drawer->TakeScreenshot(screenshot_buffer);

		if(gif_recording)
			captureGifFrame();

		drawer->Release();
	}

	// Clean unused textures (only every 60 frames to reduce overhead)
	if(++gc_frame_counter >= 60) {
		gc_frame_counter = 0;
		g_gui.gfx.garbageCollection();
	}

	// Swap buffer
	SwapBuffers();

	// Send newd node requests
	editor.SendNodeRequests();
}

void MapCanvas::ShowPositionIndicator(const Position& position)
{
	if(drawer) {
		drawer->ShowPositionIndicator(position);
		if(!animation_timer->IsRunning()) {
			Update();
		}
	}
}

void MapCanvas::TakeScreenshot(wxFileName path, wxString format)
{
	wxImage screenshot;
	if(!CaptureScreenshot(screenshot)) {
		return;
	}

	SaveScreenshotImage(screenshot, path, format, "screenshot");
	Refresh();
}

bool MapCanvas::CaptureScreenshot(wxImage& outImage, int* out_view_start_x, int* out_view_start_y)
{
	int screensize_x, screensize_y;
	GetViewBox(&view_scroll_x, &view_scroll_y, &screensize_x, &screensize_y);

	if(out_view_start_x) {
		*out_view_start_x = view_scroll_x;
	}
	if(out_view_start_y) {
		*out_view_start_y = view_scroll_y;
	}

	delete[] screenshot_buffer;
	screenshot_buffer = newd uint8_t[3 * screensize_x * screensize_y];

	Refresh();
	wxGLCanvas::Update();

	if(screenshot_buffer == nullptr) {
		g_gui.PopupDialog("Capture failed", "Image capture failed. Old Video Driver?", wxOK);
		return false;
	}

	int width, height;
	GetMapWindow()->GetViewSize(&width, &height);
	wxImage screenshot(width, height, screenshot_buffer);
	screenshot_buffer = nullptr;

	outImage = screenshot;
	return true;
}

bool MapCanvas::SaveScreenshotImage(const wxImage& image, wxFileName path, const wxString& format, const wxString& prefix)
{
	time_t t = time(nullptr);
	struct tm* current_time = localtime(&t);
	if(!current_time) {
		g_gui.PopupDialog("Capture failed", "Could not get current time for screenshot naming.", wxOK);
		return false;
	}

	wxString date;
	date << prefix << "_" << (1900 + current_time->tm_year);
	if(current_time->tm_mon < 9)
		date << "-" << "0" << current_time->tm_mon+1;
	else
		date << "-" << current_time->tm_mon+1;
	date << "-" << current_time->tm_mday;
	date << "-" << current_time->tm_hour;
	date << "-" << current_time->tm_min;
	date << "-" << current_time->tm_sec;

	int type = 0;
	path.SetName(date);
	if(format == "bmp") {
		path.SetExt(format);
		type = wxBITMAP_TYPE_BMP;
	} else if(format == "png") {
		path.SetExt(format);
		type = wxBITMAP_TYPE_PNG;
	} else if(format == "jpg" || format == "jpeg") {
		path.SetExt(format);
		type = wxBITMAP_TYPE_JPEG;
	} else if(format == "tga") {
		path.SetExt(format);
		type = wxBITMAP_TYPE_TGA;
	} else {
		g_gui.SetStatusText("Unknown screenshot format '" + format + "', switching to default (png)");
		path.SetExt("png");
		type = wxBITMAP_TYPE_PNG;
	}

	path.Mkdir(0755, wxPATH_MKDIR_FULL);
	wxFileOutputStream of(path.GetFullPath());
	if(!of.IsOk()) {
		g_gui.PopupDialog("File error", "Couldn't open file " + path.GetFullPath() + " for writing.", wxOK);
		return false;
	}

	if(image.SaveFile(of, static_cast<wxBitmapType>(type))) {
		g_gui.SetStatusText("Took screenshot and saved as " + path.GetFullName());
		return true;
	}

	g_gui.PopupDialog("File error", "Couldn't save image file correctly.", wxOK);
	return false;
}

void MapCanvas::TakeRegionScreenshot(wxFileName path, wxString format, const Position& fromPos, const Position& toPos)
{
	if(!fromPos.isValid() || !toPos.isValid())
		return;

	Position minPos(std::min(fromPos.x, toPos.x), std::min(fromPos.y, toPos.y), std::min(fromPos.z, toPos.z));
	Position maxPos(std::max(fromPos.x, toPos.x), std::max(fromPos.y, toPos.y), std::max(fromPos.z, toPos.z));

	if(minPos.z != maxPos.z) {
		g_gui.PopupDialog("Area screenshot", "Please select tiles on the same floor before taking an area screenshot.", wxOK | wxICON_INFORMATION);
		return;
	}

	MapWindow* window = GetMapWindow();
	if(!window)
		return;

	int view_px_w = 0;
	int view_px_h = 0;
	window->GetViewSize(&view_px_w, &view_px_h);
	const double current_zoom = g_gui.GetCurrentZoom();
	const int view_world_w = std::max(1, static_cast<int>(std::round(view_px_w * current_zoom)));
	const int view_world_h = std::max(1, static_cast<int>(std::round(view_px_h * current_zoom)));

	const int floor_offset = (minPos.z < rme::MapGroundLayer) ? (rme::MapGroundLayer - minPos.z) * rme::TileSize : 0;
	const int min_world_x = minPos.x * rme::TileSize - floor_offset;
	const int min_world_y = minPos.y * rme::TileSize - floor_offset;

	const int total_world_w = std::max(1, (maxPos.x - minPos.x + 1) * rme::TileSize);
	const int total_world_h = std::max(1, (maxPos.y - minPos.y + 1) * rme::TileSize);

	const int cols = std::max(1, (total_world_w + view_world_w - 1) / view_world_w);
	const int rows = std::max(1, (total_world_h + view_world_h - 1) / view_world_h);

	const Position original_center = window->GetScreenCenterPosition();
	struct CapturedChunk {
		wxImage image;
		int origin_x;
		int origin_y;
	};
	std::vector<CapturedChunk> captured_chunks;
	captured_chunks.reserve(rows * cols);
	int min_origin_x = std::numeric_limits<int>::max();
	int min_origin_y = std::numeric_limits<int>::max();
	int max_origin_x = std::numeric_limits<int>::min();
	int max_origin_y = std::numeric_limits<int>::min();

	for(int row = 0; row < rows; ++row) {
		for(int col = 0; col < cols; ++col) {
			const int chunk_world_x = min_world_x + col * view_world_w;
			const int chunk_world_y = min_world_y + row * view_world_h;

			const int center_tile_x = std::clamp((chunk_world_x + floor_offset + view_world_w / 2) / rme::TileSize, minPos.x, maxPos.x);
			const int center_tile_y = std::clamp((chunk_world_y + floor_offset + view_world_h / 2) / rme::TileSize, minPos.y, maxPos.y);

			window->SetScreenCenterPosition(Position(center_tile_x, center_tile_y, minPos.z), false);

			wxImage chunk;
			int view_origin_x = 0;
			int view_origin_y = 0;
			if(!CaptureScreenshot(chunk, &view_origin_x, &view_origin_y)) {
				window->SetScreenCenterPosition(original_center, false);
				Refresh();
				return;
			}

			captured_chunks.push_back(CapturedChunk{chunk, view_origin_x, view_origin_y});
			min_origin_x = std::min(min_origin_x, view_origin_x);
			min_origin_y = std::min(min_origin_y, view_origin_y);
			max_origin_x = std::max(max_origin_x, view_origin_x);
			max_origin_y = std::max(max_origin_y, view_origin_y);
		}
	}

	window->SetScreenCenterPosition(original_center, false);
	Refresh();

	if(captured_chunks.empty())
		return;

	const int world_span_x = (max_origin_x - min_origin_x) + view_world_w;
	const int world_span_y = (max_origin_y - min_origin_y) + view_world_h;
	const int mosaic_px_w = std::max(1, static_cast<int>(std::ceil(world_span_x / current_zoom)));
	const int mosaic_px_h = std::max(1, static_cast<int>(std::ceil(world_span_y / current_zoom)));
	wxImage mosaic(mosaic_px_w, mosaic_px_h);
	if(unsigned char* data = mosaic.GetData()) {
		std::fill(data, data + (mosaic_px_w * mosaic_px_h * 3), 0);
	}

	for(const CapturedChunk& chunk : captured_chunks) {
		const int dest_x = static_cast<int>(std::round((chunk.origin_x - min_origin_x) / current_zoom));
		const int dest_y = static_cast<int>(std::round((chunk.origin_y - min_origin_y) / current_zoom));
		mosaic.Paste(chunk.image, dest_x, dest_y);
	}

	const int output_px_w = std::max(1, static_cast<int>(std::ceil(total_world_w / current_zoom)));
	const int output_px_h = std::max(1, static_cast<int>(std::ceil(total_world_h / current_zoom)));
	const int left_crop = std::max(0, static_cast<int>(std::round((min_world_x - min_origin_x) / current_zoom)));
	const int top_crop = std::max(0, static_cast<int>(std::round((min_world_y - min_origin_y) / current_zoom)));
	const int crop_w = std::min(output_px_w, mosaic_px_w - left_crop);
	const int crop_h = std::min(output_px_h, mosaic_px_h - top_crop);

	wxImage final_image = mosaic.GetSubImage(wxRect(left_crop, top_crop, crop_w, crop_h));
	SaveScreenshotImage(final_image, path, format, "screenshot_region");
}

bool MapCanvas::StartGifRecording(const wxFileName& path, int fps)
{
	if(fps <= 0 || gif_recording)
		return false;

	int screensize_x = 0;
	int screensize_y = 0;
	GetMapWindow()->GetViewSize(&screensize_x, &screensize_y);

	if(screensize_x <= 0 || screensize_y <= 0)
		return false;

	const int delayCs = std::max(1, (100 + fps / 2) / fps);
	auto writer = std::make_unique<AnimatedGifWriter>();
	if(!writer->Open(path, static_cast<uint16_t>(screensize_x), static_cast<uint16_t>(screensize_y), static_cast<uint16_t>(delayCs), 0)) {
		return false;
	}

	gif_writer = std::move(writer);
	gif_frame_buffer.assign(static_cast<size_t>(screensize_x) * screensize_y * 3, 0);

	gif_recording = true;
	gif_fps = fps;
	gif_width = screensize_x;
	gif_height = screensize_y;
	gif_frames_written = 0;
	gif_output_path = path;
	const auto seconds_per_frame = std::chrono::duration<double>(1.0 / std::max(1, fps));
	gif_frame_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(seconds_per_frame);
	gif_next_frame_time = std::chrono::steady_clock::now();

	wxString msg;
	msg << "Recording GIF to " << gif_output_path.GetFullName() << "...";
	g_gui.SetStatusText(msg);
	return true;
}

void MapCanvas::StopGifRecording(bool keepFile, bool notify)
{
	if(!gif_recording)
		return;

	if(gif_writer) {
		gif_writer->Close();
		gif_writer.reset();
	}

	const bool deleteFile = (!keepFile || gif_frames_written == 0);
	if(deleteFile && !gif_output_path.GetFullPath().empty()) {
		wxRemoveFile(gif_output_path.GetFullPath());
	}

	if(notify) {
		if(deleteFile) {
			g_gui.SetStatusText("GIF recording canceled.");
		} else {
			wxString msg;
			msg << "Saved GIF to " << gif_output_path.GetFullName();
			g_gui.SetStatusText(msg);
		}
	}

	gif_frame_buffer.clear();
	gif_recording = false;
	gif_fps = 0;
	gif_width = 0;
	gif_height = 0;
	gif_frames_written = 0;
	gif_output_path.Clear();
}

void MapCanvas::ToggleCameraPathPlayback()
{
	if(camera_path_playing) {
		camera_path_timer.Stop();
		camera_path_playing = false;
		camera_path_time = 0.0;
		camera_path_name.clear();
		g_gui.SetStatusText("Camera path playback stopped.");
		return;
	}

	CameraPaths& cameraPaths = editor.getMap().camera_paths;
	CameraPath* path = cameraPaths.getActivePath();
	if(!path || path->keyframes.size() < 2) {
		g_gui.SetStatusText("No camera path with at least 2 keyframes selected.");
		return;
	}

	camera_path_name = path->name;
	camera_path_time = 0.0;
	camera_path_last_tick = std::chrono::steady_clock::now();
	camera_path_playing = true;
	camera_path_timer.Start(16);
	g_gui.SetStatusText("Camera path playback started.");
}

void MapCanvas::OnCameraPathTimer(wxTimerEvent& WXUNUSED(event))
{
	if(!camera_path_playing) {
		return;
	}

	CameraPaths& cameraPaths = editor.getMap().camera_paths;
	CameraPath* path = cameraPaths.getPath(camera_path_name);
	if(!path || path->keyframes.size() < 2) {
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
	if(window) {
		SetZoom(sample.zoom);
		window->SetScreenCenterPosition(sample.x, sample.y, sample_z, false);
	}
	Refresh();

	if(finished && !path->loop) {
		ToggleCameraPathPlayback();
	}
}

void MapCanvas::captureGifFrame()
{
	if(!gif_recording || !gif_writer)
		return;

	int current_width = 0;
	int current_height = 0;
	GetMapWindow()->GetViewSize(&current_width, &current_height);
	if(current_width != gif_width || current_height != gif_height) {
		g_gui.SetStatusText("GIF recording stopped because the window size changed.");
		StopGifRecording(false, false);
		return;
	}

	auto now = std::chrono::steady_clock::now();
	if(now < gif_next_frame_time)
		return;

	gif_next_frame_time += gif_frame_interval;
	if(gif_next_frame_time < now) {
		gif_next_frame_time = now + gif_frame_interval;
	}

	if(gif_frame_buffer.size() < static_cast<size_t>(gif_width) * gif_height * 3) {
		gif_frame_buffer.assign(static_cast<size_t>(gif_width) * gif_height * 3, 0);
	}

	drawer->TakeScreenshot(gif_frame_buffer.data());
	if(!gif_writer->WriteFrame(gif_frame_buffer.data())) {
		g_gui.SetStatusText("Failed to record GIF frame.");
		StopGifRecording(false, false);
		return;
	}

	++gif_frames_written;
}

void MapCanvas::ScreenToMap(int screen_x, int screen_y, int* map_x, int* map_y)
{
	int start_x, start_y;
	GetMapWindow()->GetViewStart(&start_x, &start_y);

	screen_x *= GetContentScaleFactor();
	screen_y *= GetContentScaleFactor();

	if(screen_x < 0) {
		*map_x = (start_x + screen_x) / rme::TileSize;
	} else {
		*map_x = int(start_x + (screen_x * zoom)) / rme::TileSize;
	}

	if(screen_y < 0) {
		*map_y = (start_y + screen_y) / rme::TileSize;
	} else {
		*map_y = int(start_y + (screen_y * zoom)) / rme::TileSize;
	}

	if(floor <= rme::MapGroundLayer) {
		*map_x += rme::MapGroundLayer - floor;
		*map_y += rme::MapGroundLayer - floor;
	}/* else {
		*map_x += rme::MapMaxLayer - floor;
		*map_y += rme::MapMaxLayer - floor;
	}*/
}

MapWindow* MapCanvas::GetMapWindow() const
{
	wxWindow* window = GetParent();
	if(window)
		return static_cast<MapWindow*>(window);
	return nullptr;
}

void MapCanvas::GetScreenCenter(int* map_x, int* map_y)
{
	int width, height;
	GetMapWindow()->GetViewSize(&width, &height);
	return ScreenToMap(width/2, height/2, map_x, map_y);
}

Position MapCanvas::GetCursorPosition() const
{
	return Position(last_cursor_map_x, last_cursor_map_y, floor);
}

void MapCanvas::UpdatePositionStatus(int x, int y)
{
	if(preview_mode) {
		return;
	}

	if(x == -1) x = cursor_x;
	if(y == -1) y = cursor_y;

	int map_x, map_y;
	ScreenToMap(x, y, &map_x, &map_y);

	wxString ss;
	ss << "x: " << map_x << " y:" << map_y << " z:" << floor;
	g_gui.root->SetStatusText(ss,2);

	ss = "";
	Tile* tile = editor.getMap().getTile(map_x, map_y, floor);
	if(tile) {
		if(tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
			ss << "Spawn radius: " << tile->spawn->getSize();
		} else if(tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
			ss << (tile->creature->isNpc()? "NPC" : "Monster");
			ss << " \"" << wxstr(tile->creature->getName()) << "\" spawntime: " << tile->creature->getSpawnTime();
		} else if(Item* item = tile->getTopItem()) {
			ss << "Item \"" << wxstr(item->getName()) << "\"";
			ss << " id:" << item->getID();
			ss << " cid:" << item->getClientID();
			if(item->getUniqueID()) ss << " uid:" << item->getUniqueID();
			if(item->getActionID()) ss << " aid:" << item->getActionID();
			if(item->hasWeight()) {
				wxString s;
				s.Printf("%.2f", item->getWeight());
				ss << " weight: " << s;
			}
		} else {
			ss << "Nothing";
		}
	} else {
		ss << "Nothing";
	}

	if(editor.IsLive()) {
		editor.GetLive().updateCursor(Position(map_x, map_y, floor));
	}

	g_gui.root->SetStatusText(ss, 1);
}

void MapCanvas::UpdateZoomStatus()
{
	if(preview_mode) {
		return;
	}

	int percentage = (int)((1.0 / zoom) * 100);
	wxString ss;
	ss << "zoom: " << percentage << "%";
	wxStatusBar* statusbar = g_gui.root ? g_gui.root->GetStatusBar() : nullptr;
	if(statusbar) {
		statusbar->SetStatusText(ss, 3);
		statusbar->SetForegroundColour(wxColour(230, 230, 230));
		statusbar->Refresh();
	} else {
		g_gui.root->SetStatusText(ss, 3);
	}
}

void MapCanvas::ClearAutoborderPreview()
{
	if(!autoborder_preview_map || !autoborder_preview_active) {
		return;
	}
	autoborder_preview_map->clear();
	autoborder_preview_active = false;
}

void MapCanvas::UpdateAutoborderPreview(int mouse_map_x, int mouse_map_y, const wxMouseEvent& event)
{
	if(!autoborder_preview_map) {
		return;
	}

	if(!g_settings.getBoolean(Config::SHOW_AUTOBORDER_PREVIEW) ||
		!g_settings.getBoolean(Config::USE_AUTOMAGIC)) {
		ClearAutoborderPreview();
		return;
	}

	Brush* brush = g_gui.GetCurrentBrush();
	if(!brush || !brush->isGround() || !brush->needBorders()) {
		ClearAutoborderPreview();
		return;
	}

	// Avoid misleading preview while erasing.
	if(event.ControlDown()) {
		ClearAutoborderPreview();
		return;
	}

	PositionVector tilestodraw;
	PositionVector tilestoborder;
	getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, &tilestoborder, false);

	if(tilestodraw.empty() && tilestoborder.empty()) {
		ClearAutoborderPreview();
		return;
	}

	autoborder_preview_map->clear();

	GroundBrush* replace_brush = nullptr;
	if(event.AltDown()) {
		Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
		if(tile) {
			replace_brush = tile->getGroundBrush();
		}
	}

	// Copy impacted tiles (including neighbor ring for accurate borders).
	PositionVector copy_positions = tilestodraw;
	copy_positions.insert(copy_positions.end(), tilestoborder.begin(), tilestoborder.end());
	for(const Position& pos : tilestoborder) {
		for(int dy = -1; dy <= 1; ++dy) {
			for(int dx = -1; dx <= 1; ++dx) {
				copy_positions.push_back(Position(pos.x + dx, pos.y + dy, pos.z));
			}
		}
	}
	std::sort(copy_positions.begin(), copy_positions.end());
	copy_positions.erase(std::unique(copy_positions.begin(), copy_positions.end()), copy_positions.end());

	for(const Position& pos : copy_positions) {
		Tile* src = editor.getMap().getTile(pos);
		if(!src) {
			continue;
		}
		Tile* copy = src->deepCopy(*autoborder_preview_map);
		autoborder_preview_map->setTile(pos, copy, true);
	}

	// Apply the ground brush in the preview map.
	for(const Position& pos : tilestodraw) {
		Tile* tile = autoborder_preview_map->getTile(pos);
		if(!tile) {
			tile = autoborder_preview_map->allocator(autoborder_preview_map->createTileL(pos));
			autoborder_preview_map->setTile(pos, tile, true);
		}

		tile->cleanBorders();

		if(event.AltDown()) {
			GroundBrush::DrawParams params;
			if(replace_brush) {
				params.replaceCondition = GroundBrush::DrawParams::ReplaceCondition::MatchBrush;
				params.matchBrush = replace_brush;
			} else {
				params.replaceCondition = GroundBrush::DrawParams::ReplaceCondition::RequireEmptyTile;
			}
			brush->draw(autoborder_preview_map, tile, &params);
		} else {
			brush->draw(autoborder_preview_map, tile, nullptr);
		}
		tile->update();
	}

	// Apply autoborder logic on the preview map.
	std::sort(tilestoborder.begin(), tilestoborder.end());
	tilestoborder.erase(std::unique(tilestoborder.begin(), tilestoborder.end()), tilestoborder.end());
	for(const Position& pos : tilestoborder) {
		Tile* tile = autoborder_preview_map->getTile(pos);
		if(tile) {
			tile->borderize(autoborder_preview_map);
			continue;
		}

		TileLocation* location = autoborder_preview_map->createTileL(pos);
		Tile* new_tile = autoborder_preview_map->allocator(location);
		new_tile->borderize(autoborder_preview_map);
		if(new_tile->size() > 0) {
			autoborder_preview_map->setTile(pos, new_tile, true);
		} else {
			delete new_tile;
			autoborder_preview_map->setTile(pos, nullptr, true);
		}
	}

	autoborder_preview_active = (autoborder_preview_map->size() > 0);
	if(!autoborder_preview_active) {
		autoborder_preview_map->clear();
	}
}

void MapCanvas::OnMouseMove(wxMouseEvent& event)
{
	// Signal that user is active, so animation timer should refresh at full rate
	animation_timer->RequestRefresh();

	bool refresh_requested = false;
	if(screendragging) {
		GetMapWindow()->ScrollRelative(int(g_settings.getFloat(Config::SCROLL_SPEED) * zoom*(event.GetX() - cursor_x)), int(g_settings.getFloat(Config::SCROLL_SPEED) * zoom*(event.GetY() - cursor_y)));
		Refresh();
		refresh_requested = true;
	}

	cursor_in_window = true;
	cursor_x = event.GetX();
	cursor_y = event.GetY();

	int mouse_map_x, mouse_map_y;
	MouseToMap(&mouse_map_x,&mouse_map_y);

	bool map_update = false;
	if(last_cursor_map_x != mouse_map_x || last_cursor_map_y != mouse_map_y || last_cursor_map_z != floor) {
		map_update = true;
	}
	last_cursor_map_x = mouse_map_x;
	last_cursor_map_y = mouse_map_y;
	last_cursor_map_z = floor;

	if(map_update) {
		UpdatePositionStatus(cursor_x, cursor_y);
		UpdateZoomStatus();
	}

	if(g_gui.IsSelectionMode()) {
		ClearAutoborderPreview();
		if(map_update && isPasting()) {
			Refresh();
			refresh_requested = true;
		} else if(map_update && dragging) {
			wxString ss;

			int move_x = drag_start_x - mouse_map_x;
			int move_y = drag_start_y - mouse_map_y;
			int move_z = drag_start_z - floor;
			ss << "Dragging " << -move_x << "," << -move_y << "," << -move_z;
			g_gui.SetStatusText(ss);

			Refresh();
			refresh_requested = true;
		} else if(boundbox_selection) {
			if(map_update) {
				wxString ss;

				int move_x = std::abs(last_click_map_x - mouse_map_x);
				int move_y = std::abs(last_click_map_y - mouse_map_y);
				ss << "Selection " << move_x+1 << ":" << move_y+1;
				g_gui.SetStatusText(ss);
			}

			if(IsLassoSelectionEnabled() && (event.LeftIsDown() || event.RightIsDown())) {
				AddLassoPoint(event.GetX(), event.GetY(), mouse_map_x, mouse_map_y);
			}

			Refresh();
			refresh_requested = true;
		}
	} else { // Drawing mode
		Brush* brush = g_gui.GetCurrentBrush();
		if(alt_ground_mode && !event.AltDown()) {
			alt_ground_mode = false;
		}

		if(map_update && drawing && brush) {
			g_gui.AddRecentBrush(brush);
			if(brush->isDoodad()) {
				if(event.ControlDown()) {
					PositionVector tilestodraw;
					getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, nullptr);
					editor.undraw(tilestodraw, event.ShiftDown() || event.AltDown());
				} else {
					editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown() || event.AltDown());
				}
			} else if(brush->isDoor()) {
				if(!brush->canDraw(&editor.getMap(), Position(mouse_map_x, mouse_map_y, floor))) {
					// We don't have to waste an action in this case...
				} else {
					PositionVector tilestodraw;
					PositionVector tilestoborder;

					tilestodraw.push_back(Position(mouse_map_x, mouse_map_y, floor));

					tilestoborder.push_back(Position(mouse_map_x    , mouse_map_y - 1, floor));
					tilestoborder.push_back(Position(mouse_map_x - 1, mouse_map_y    , floor));
					tilestoborder.push_back(Position(mouse_map_x    , mouse_map_y + 1, floor));
					tilestoborder.push_back(Position(mouse_map_x + 1, mouse_map_y    , floor));

					if(event.ControlDown()) {
						editor.undraw(tilestodraw, tilestoborder, event.AltDown());
					} else {
						editor.draw(tilestodraw, tilestoborder, event.AltDown());
					}
				}
			} else if(brush->needBorders()) {
				PositionVector tilestodraw, tilestoborder;

				getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, &tilestoborder);

				if(event.ControlDown()) {
					editor.undraw(tilestodraw, tilestoborder, event.AltDown());
				} else {
					editor.draw(tilestodraw, tilestoborder, event.AltDown());
				}
			} else if(brush->oneSizeFitsAll()) {
				drawing = true;
				PositionVector tilestodraw;
				tilestodraw.push_back(Position(mouse_map_x,mouse_map_y, floor));

				if(event.ControlDown()) {
					editor.undraw(tilestodraw, event.AltDown());
				} else {
					editor.draw(tilestodraw, event.AltDown());
				}
			} else { // No borders
				PositionVector tilestodraw;

				for(int y = -g_gui.GetBrushSize(); y <= g_gui.GetBrushSize(); y++) {
					for(int x = -g_gui.GetBrushSize(); x <= g_gui.GetBrushSize(); x++) {
						if(g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE) {
							tilestodraw.push_back(Position(mouse_map_x+x,mouse_map_y+y, floor));
						} else if(g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE) {
							double distance = sqrt(double(x*x) + double(y*y));
							if(distance < g_gui.GetBrushSize()+0.005) {
								tilestodraw.push_back(Position(mouse_map_x+x,mouse_map_y+y, floor));
							}
						}
					}
				}
				if(event.ControlDown()) {
					editor.undraw(tilestodraw, event.AltDown());
				} else {
					editor.draw(tilestodraw, event.AltDown());
				}
			}

			// Create newd doodad layout (does nothing if a non-doodad brush is selected)
			g_gui.FillDoodadPreviewBuffer();

			g_gui.RefreshView();
			refresh_requested = true;
		} else if(dragging_draw) {
			g_gui.RefreshView();
			refresh_requested = true;
		} else if(map_update && brush) {
			Refresh();
			refresh_requested = true;
		}

		const bool allow_preview = brush && brush->isGround() && !drawing && !dragging_draw &&
			!event.ControlDown() &&
			g_settings.getBoolean(Config::SHOW_AUTOBORDER_PREVIEW) &&
			g_settings.getBoolean(Config::USE_AUTOMAGIC);
		if(allow_preview) {
			const bool preview_state_changed =
				!autoborder_preview_active ||
				brush != last_preview_brush ||
				g_gui.GetBrushSize() != last_preview_brush_size ||
				static_cast<int>(g_gui.GetBrushShape()) != last_preview_brush_shape ||
				floor != last_preview_map_z ||
				event.AltDown() != last_preview_alt;
			if(map_update || preview_state_changed) {
				UpdateAutoborderPreview(mouse_map_x, mouse_map_y, event);
				last_preview_map_x = mouse_map_x;
				last_preview_map_y = mouse_map_y;
				last_preview_map_z = floor;
				last_preview_brush = brush;
				last_preview_brush_size = g_gui.GetBrushSize();
				last_preview_brush_shape = static_cast<int>(g_gui.GetBrushShape());
				last_preview_alt = event.AltDown();
				if(!map_update) {
					Refresh();
					refresh_requested = true;
				}
			}
		} else {
			const bool had_preview = autoborder_preview_active;
			ClearAutoborderPreview();
			if(had_preview && !map_update) {
				Refresh();
				refresh_requested = true;
			}
		}
	}

	if(map_update && !refresh_requested) {
		Refresh();
	}
}

void MapCanvas::OnMouseLeftRelease(wxMouseEvent& event)
{
	DispatchMouseRelease(MouseButtonBinding::Left, *this, event);
}

void MapCanvas::OnMouseLeftClick(wxMouseEvent& event)
{
	DispatchMousePress(MouseButtonBinding::Left, *this, event);
}

void MapCanvas::OnMouseLeftDoubleClick(wxMouseEvent& event)
{
	HandlePropertiesDoubleClick(event);
}

void MapCanvas::OnMouseMiddleDoubleClick(wxMouseEvent& event)
{
	if(GetMouseBinding(MouseActionID::Properties) != MouseButtonBinding::Middle)
		return;
	HandlePropertiesDoubleClick(event);
}

void MapCanvas::OnMouseAux1DoubleClick(wxMouseEvent& event)
{
	if(GetMouseBinding(MouseActionID::Properties) != MouseButtonBinding::Button4)
		return;
	HandlePropertiesDoubleClick(event);
}

void MapCanvas::OnMouseAux2DoubleClick(wxMouseEvent& event)
{
	if(GetMouseBinding(MouseActionID::Properties) != MouseButtonBinding::Button5)
		return;
	HandlePropertiesDoubleClick(event);
}

void MapCanvas::OnMouseCenterClick(wxMouseEvent& event)
{
	DispatchMousePress(MouseButtonBinding::Middle, *this, event);
}

void MapCanvas::OnMouseCenterRelease(wxMouseEvent& event)
{
	DispatchMouseRelease(MouseButtonBinding::Middle, *this, event);
}

void MapCanvas::OnMouseRightClick(wxMouseEvent& event)
{
	if(isPasting()) {
		EndPasting();
		suppress_right_release = true;
		g_gui.SetStatusText("Paste canceled.");
		g_gui.RefreshView();
		return;
	}
	suppress_right_release = false;
	DispatchMousePress(MouseButtonBinding::Right, *this, event);
}

void MapCanvas::OnMouseRightRelease(wxMouseEvent& event)
{
	if(suppress_right_release) {
		suppress_right_release = false;
		return;
	}
	DispatchMouseRelease(MouseButtonBinding::Right, *this, event);
}

void MapCanvas::OnMouseRightDoubleClick(wxMouseEvent& event)
{
	if(GetMouseBinding(MouseActionID::Properties) != MouseButtonBinding::Right)
		return;
	HandlePropertiesDoubleClick(event);
}

void MapCanvas::OnMouseAux1Click(wxMouseEvent& event)
{
	DispatchMousePress(MouseButtonBinding::Button4, *this, event);
}

void MapCanvas::OnMouseAux1Release(wxMouseEvent& event)
{
	DispatchMouseRelease(MouseButtonBinding::Button4, *this, event);
}

void MapCanvas::OnMouseAux2Click(wxMouseEvent& event)
{
	DispatchMousePress(MouseButtonBinding::Button5, *this, event);
}

void MapCanvas::OnMouseAux2Release(wxMouseEvent& event)
{
	DispatchMouseRelease(MouseButtonBinding::Button5, *this, event);
}

void MapCanvas::OnMouseAuxEvent(wxMouseEvent& event)
{
	const auto eventType = event.GetEventType();
#ifdef wxEVT_AUX1_DOWN
	if(eventType == wxEVT_AUX1_DOWN) {
		OnMouseAux1Click(event);
		return;
	}
#endif
#ifdef wxEVT_AUX1_UP
	if(eventType == wxEVT_AUX1_UP) {
		OnMouseAux1Release(event);
		return;
	}
#endif
#ifdef wxEVT_AUX2_DOWN
	if(eventType == wxEVT_AUX2_DOWN) {
		OnMouseAux2Click(event);
		return;
	}
#endif
#ifdef wxEVT_AUX2_UP
	if(eventType == wxEVT_AUX2_UP) {
		OnMouseAux2Release(event);
		return;
	}
#endif
#ifdef wxEVT_AUX1_DCLICK
	if(eventType == wxEVT_AUX1_DCLICK) {
		OnMouseAux1DoubleClick(event);
		return;
	}
#endif
#ifdef wxEVT_AUX2_DCLICK
	if(eventType == wxEVT_AUX2_DCLICK) {
		OnMouseAux2DoubleClick(event);
		return;
	}
#endif
	event.Skip();
}

bool MapCanvas::HandleMouseKeyboardHotkey(wxKeyEvent& event, bool keyDown)
{
	HotkeyData hotkeyData;
	if(!EventToHotkey(event, hotkeyData))
		return false;

	MouseActionID action;
	if(!MatchMouseKeyboardHotkey(hotkeyData, action))
		return false;

	if(keyDown) {
		if(active_keyboard_mouse_actions.find(action) != active_keyboard_mouse_actions.end())
			return true;
		active_keyboard_mouse_actions.insert(action);
	} else {
		auto it = active_keyboard_mouse_actions.find(action);
		if(it == active_keyboard_mouse_actions.end())
			return true;
		active_keyboard_mouse_actions.erase(it);
	}

	DispatchKeyboardMouseAction(action, keyDown, event);
	return true;
}

void MapCanvas::DispatchKeyboardMouseAction(MouseActionID action, bool keyDown, const wxKeyEvent& source)
{
	wxMouseEvent mouseEvent(keyDown ? wxEVT_LEFT_DOWN : wxEVT_LEFT_UP);
	switch(action) {
		case MouseActionID::PrimaryAction:
			mouseEvent.SetEventType(keyDown ? wxEVT_LEFT_DOWN : wxEVT_LEFT_UP);
			break;
		case MouseActionID::Camera:
			mouseEvent.SetEventType(keyDown ? wxEVT_MIDDLE_DOWN : wxEVT_MIDDLE_UP);
			break;
		case MouseActionID::Properties:
			mouseEvent.SetEventType(keyDown ? wxEVT_RIGHT_DOWN : wxEVT_RIGHT_UP);
			break;
		default:
			return;
	}

	mouseEvent.SetPosition(wxPoint(cursor_x, cursor_y));
	mouseEvent.SetControlDown(source.ControlDown());
	mouseEvent.SetAltDown(source.AltDown());
	mouseEvent.SetShiftDown(source.ShiftDown());
#ifdef __APPLE__
	mouseEvent.SetMetaDown(source.MetaDown());
#endif

	if(keyDown) {
		switch(action) {
			case MouseActionID::PrimaryAction:
				OnMouseActionClick(mouseEvent);
				break;
			case MouseActionID::Camera:
				OnMouseCameraClick(mouseEvent);
				break;
			case MouseActionID::Properties:
				OnMousePropertiesClick(mouseEvent);
				break;
			default:
				break;
		}
	} else {
		switch(action) {
			case MouseActionID::PrimaryAction:
				OnMouseActionRelease(mouseEvent);
				break;
			case MouseActionID::Camera:
				OnMouseCameraRelease(mouseEvent);
				break;
			case MouseActionID::Properties:
				OnMousePropertiesRelease(mouseEvent);
				break;
			default:
				break;
		}
	}
}
void MapCanvas::HandlePropertiesDoubleClick(wxMouseEvent& event)
{
	if(!g_settings.getInteger(Config::DOUBLECLICK_PROPERTIES)) {
		return;
	}

	Map& map = editor.getMap();
	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);
	const Tile* tile = map.getTile(mouse_map_x, mouse_map_y, floor);

	if(tile && tile->size() > 0) {
		Tile* new_tile = tile->deepCopy(map);
		wxDialog* dialog = nullptr;
		if(new_tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
			dialog = newd OldPropertiesWindow(g_gui.root, &map, new_tile, new_tile->spawn);
		} else if(new_tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
			dialog = newd OldPropertiesWindow(g_gui.root, &map, new_tile, new_tile->creature);
		} else if(Item* item = new_tile->getTopItem()) {
			if(map.getVersion().otbm >= MAP_OTBM_4) {
				dialog = newd PropertiesWindow(g_gui.root, &map, new_tile, item);
			} else {
				dialog = newd OldPropertiesWindow(g_gui.root, &map, new_tile, item);
			}
		} else {
			delete new_tile;
			return;
		}

		int ret = dialog->ShowModal();
		if(ret != 0) {
			Action* action = editor.createAction(ACTION_CHANGE_PROPERTIES);
			action->addChange(newd Change(new_tile));
			editor.addAction(action);
		} else {
			// Cancel!
			delete new_tile;
		}
		dialog->Destroy();
	}
}

void MapCanvas::OnMouseActionClick(wxMouseEvent& event)
{
	SetFocus();

	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);
	if(g_gui.HandleRectanglePickClick(Position(mouse_map_x, mouse_map_y, floor))) {
		return;
	}
	boundbox_select_creatures = false;

	const int raw_palette_modifier = g_settings.getInteger(Config::RAW_PALETTE_SELECT_MODIFIER);
	const bool raw_pick_modifier = (raw_palette_modifier != 0) && (GetModifierMask(event) == raw_palette_modifier);
	const bool allow_raw_pick = raw_pick_modifier && (
		g_gui.IsSelectionMode() ||
		(g_gui.IsDrawingMode() && raw_palette_modifier != wxACCEL_ALT)
	);
	if(allow_raw_pick) {
		Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
		if(tile && tile->size() > 0) {
			Item* item = tile->getTopItem();
			if(item && item->getRAWBrush())
				g_gui.SelectBrush(item->getRAWBrush(), TILESET_RAW);
		}
	} else if(g_gui.IsSelectionMode()) {
		Selection& selection = editor.getSelection();
		if(isPasting()) {
			const bool keepPasting = g_gui.IsKeepPasting();
			// Set paste to false (no rendering etc.)
			if(!keepPasting) {
				EndPasting();
			}

			// Paste to the map
			editor.copybuffer.paste(editor, Position(mouse_map_x, mouse_map_y, floor));

			if(!keepPasting) {
				// Start dragging
				dragging = true;
				drag_start_x = mouse_map_x;
				drag_start_y = mouse_map_y;
				drag_start_z = floor;
			}
		} else do {
			boundbox_selection = false;
			boundbox_deselect = false;
			if(IsLassoSelectionEnabled()) {
				ClearLassoSelection();
			}
		if(event.ShiftDown()) {
			boundbox_selection = true;
			boundbox_deselect = event.ControlDown() && event.ShiftDown() && event.AltDown();
			boundbox_select_creatures = event.AltDown() && !boundbox_deselect;
			if(IsLassoSelectionEnabled()) {
				StartLassoSelection(event.GetX(), event.GetY(), mouse_map_x, mouse_map_y);
			} else {
				ClearLassoSelection();
			}

			if(!event.ControlDown()) {
				selection.start(Selection::NONE, ACTION_UNSELECT); // Start selection session
				selection.clear(); // Clear out selection
					selection.finish(); // End selection session
					selection.updateSelectionCount();
				}
			} else if(event.ControlDown()) {
				Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
				if(tile) {
					selection.start(); // Start selection session
					const bool changed = ToggleTileObjectSelection(selection, tile, editor.getMap());
					selection.finish(); // Finish selection session
					if(changed) {
						selection.updateSelectionCount();
					}
				}
			} else {
				Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
				if(!tile) {
					selection.start(Selection::NONE, ACTION_UNSELECT); // Start selection session
					selection.clear(); // Clear out selection
					selection.finish(); // End selection session
					selection.updateSelectionCount();
				} else if(tile->isSelected()) {
					dragging = true;
					drag_start_x = mouse_map_x;
					drag_start_y = mouse_map_y;
					drag_start_z = floor;
				} else {
					selection.start(); // Start a selection session
					selection.clear();
					selection.commit();
					if(tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS) && !tile->creature) {
						selection.add(tile, tile->spawn);
						dragging = true;
						drag_start_x = mouse_map_x;
						drag_start_y = mouse_map_y;
						drag_start_z = floor;
					} else if(tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
						AddCreatureWithSpawn(selection, tile, editor.getMap());
						dragging = true;
						drag_start_x = mouse_map_x;
						drag_start_y = mouse_map_y;
						drag_start_z = floor;
					} else if(tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
						selection.add(tile, tile->spawn);
						dragging = true;
						drag_start_x = mouse_map_x;
						drag_start_y = mouse_map_y;
						drag_start_z = floor;
					} else {
						Item* item = tile->getTopItem();
						if(item) {
							selection.add(tile, item);
							dragging = true;
							drag_start_x = mouse_map_x;
							drag_start_y = mouse_map_y;
							drag_start_z = floor;
						}
					}
					selection.finish(); // Finish the selection session
					selection.updateSelectionCount();
				}
			}
		} while(false);
	} else if(g_gui.GetCurrentBrush()) { // Drawing mode
		Brush* brush = g_gui.GetCurrentBrush();
		g_gui.AddRecentBrush(brush);


		alt_ground_mode = brush->isGround() && event.AltDown();
		if(alt_ground_mode) {
			alt_ground_reference = nullptr;
			Tile* clicked_tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
			if(clicked_tile) {
				alt_ground_reference = clicked_tile->getGroundBrush();
			}
			editor.replace_brush = alt_ground_reference;
		}

		bool handledSingleTile = false;


		if(!handledSingleTile) {
			if(event.ShiftDown() && brush->canDrag()) {
				dragging_draw = true;
			} else {
				if(g_gui.GetBrushSize() == 0 && !brush->oneSizeFitsAll()) {
					drawing = true;
				} else {
					drawing = g_gui.GetCurrentBrush()->canSmear();
				}
				if(brush->isWall()) {
				if(event.AltDown() && g_gui.GetBrushSize() == 0) {
					// z0mg, just clicked a tile, shift variaton.
					if(event.ControlDown()) {
						editor.undraw(Position(mouse_map_x, mouse_map_y, floor), event.AltDown());
					} else {
						editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.AltDown());
					}
				} else {
					PositionVector tilestodraw;
					PositionVector tilestoborder;

					int start_map_x = mouse_map_x - g_gui.GetBrushSize();
					int start_map_y = mouse_map_y - g_gui.GetBrushSize();
					int end_map_x   = mouse_map_x + g_gui.GetBrushSize();
					int end_map_y   = mouse_map_y + g_gui.GetBrushSize();

					for(int y = start_map_y -1; y <= end_map_y + 1; ++y) {
						for(int x = start_map_x - 1; x <= end_map_x + 1; ++x) {
							if((x <= start_map_x+1 || x >= end_map_x-1) || (y <= start_map_y+1 || y >= end_map_y-1)) {
								tilestoborder.push_back(Position(x,y,floor));
							}
							if(((x == start_map_x || x == end_map_x) || (y == start_map_y || y == end_map_y)) &&
								((x >= start_map_x && x <= end_map_x) && (y >= start_map_y && y <= end_map_y))) {
								tilestodraw.push_back(Position(x,y,floor));
							}
						}
					}
					if(event.ControlDown()) {
						editor.undraw(tilestodraw, tilestoborder, event.AltDown());
					} else {
						editor.draw(tilestodraw, tilestoborder, event.AltDown());
					}
				}
			} else if(brush->isDoor()) {
				PositionVector tilestodraw;
				PositionVector tilestoborder;

				tilestodraw.push_back(Position(mouse_map_x, mouse_map_y, floor));

				tilestoborder.push_back(Position(mouse_map_x    , mouse_map_y - 1, floor));
				tilestoborder.push_back(Position(mouse_map_x - 1, mouse_map_y    , floor));
				tilestoborder.push_back(Position(mouse_map_x    , mouse_map_y + 1, floor));
				tilestoborder.push_back(Position(mouse_map_x + 1, mouse_map_y    , floor));

				if(event.ControlDown()) {
					editor.undraw(tilestodraw, tilestoborder, event.AltDown());
				} else {
					editor.draw(tilestodraw, tilestoborder, event.AltDown());
				}
			} else if(brush->isDoodad() || brush->isSpawn() || brush->isCreature()) {
				if(event.ControlDown()) {
					if(brush->isDoodad()) {
						PositionVector tilestodraw;
						getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, nullptr);
						editor.undraw(tilestodraw, event.AltDown());
					} else {
						editor.undraw(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown() || event.AltDown());
					}
				} else {
					bool will_show_spawn = false;
					if(brush->isSpawn() || brush->isCreature()) {
						if(!g_settings.getBoolean(Config::SHOW_SPAWNS)) {
							Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
							if(!tile || !tile->spawn) {
								will_show_spawn = true;
							}
						}
					}

					editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.ShiftDown() || event.AltDown());

					if(will_show_spawn) {
						Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
						if(tile && tile->spawn) {
							g_settings.setInteger(Config::SHOW_SPAWNS, true);
							g_gui.UpdateMenubar();
						}
					}
				}
			} else {
				if(brush->needBorders()) {
					PositionVector tilestodraw;
					PositionVector tilestoborder;

					bool fill = keyCode == WXK_CONTROL_D && event.ControlDown() && brush->isGround();
					getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, &tilestoborder, fill);

					if(!fill && event.ControlDown()) {
						editor.undraw(tilestodraw, tilestoborder, event.AltDown());
					} else {
						editor.draw(tilestodraw, tilestoborder, event.AltDown());
					}
				} else if(brush->oneSizeFitsAll()) {
					if(brush->isHouseExit() || brush->isWaypoint() || brush->isCameraPath()) {
						if(brush->isCameraPath() && event.ControlDown()) {
							editor.undraw(Position(mouse_map_x, mouse_map_y, floor), event.AltDown());
						} else {
							editor.draw(Position(mouse_map_x, mouse_map_y, floor), event.AltDown());
						}
					} else {
						PositionVector tilestodraw;
						tilestodraw.push_back(Position(mouse_map_x,mouse_map_y, floor));
						if(event.ControlDown()) {
							editor.undraw(tilestodraw, event.AltDown());
						} else {
							editor.draw(tilestodraw, event.AltDown());
						}
					}
				} else {
					PositionVector tilestodraw;

					getTilesToDraw(mouse_map_x, mouse_map_y, floor, &tilestodraw, nullptr);

					if(event.ControlDown()) {
						editor.undraw(tilestodraw, event.AltDown());
					} else {
						editor.draw(tilestodraw, event.AltDown());
					}
				}
			}
		}
		// Change the doodad layout brush
		g_gui.FillDoodadPreviewBuffer();
		}
	}
	last_click_x = int(event.GetX()*zoom);
	last_click_y = int(event.GetY()*zoom);

	int start_x, start_y;
	GetMapWindow()->GetViewStart(&start_x, &start_y);
	last_click_abs_x = last_click_x + start_x;
	last_click_abs_y = last_click_y + start_y;

	last_click_map_x = mouse_map_x;
	last_click_map_y = mouse_map_y;
	last_click_map_z = floor;
	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void MapCanvas::OnMouseActionRelease(wxMouseEvent& event)
{
	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);

	int move_x = last_click_map_x - mouse_map_x;
	int move_y = last_click_map_y - mouse_map_y;
	int move_z = last_click_map_z - floor;

	alt_ground_mode = false;

	if(g_gui.IsSelectionMode()) {
		if(dragging && (move_x != 0 || move_y != 0 || move_z != 0)) {
			editor.moveSelection(Position(move_x, move_y, move_z));
		} else {
			Selection& selection = editor.getSelection();
			if(boundbox_selection) {
				bool deselect_mode = boundbox_deselect;
				if(IsLassoSelectionEnabled()) {
					AddLassoPoint(event.GetX(), event.GetY(), mouse_map_x, mouse_map_y);
				}
				if(IsLassoSelectionEnabled() && lasso_map_points.size() >= 3) {
					selection.start(deselect_mode ? Selection::NONE : Selection::NONE,
						deselect_mode ? ACTION_UNSELECT : ACTION_SELECT); // Start a selection session
					ApplyLassoSelection(selection, boundbox_select_creatures, deselect_mode);
					selection.finish(); // Finish the selection session
					selection.updateSelectionCount();
				} else if(mouse_map_x == last_click_map_x && mouse_map_y == last_click_map_y && event.ControlDown()) {
					// Mouse hasn't moved, do control+shift thingy!
					Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
					if(tile) {
						selection.start(); // Start a selection session
						bool changed = false;
						if(deselect_mode) {
							RemoveTileOrCreaturesFromSelection(selection, tile, boundbox_select_creatures, editor.getMap());
							changed = true;
						} else {
							changed = ToggleTileObjectSelection(selection, tile, editor.getMap());
						}
						if(!changed) {
							if(tile->isSelected()) {
								selection.remove(tile);
							} else {
								selection.add(tile);
							}
							changed = true;
						}
						selection.finish(); // Finish the selection session
						if(changed) {
							selection.updateSelectionCount();
						}
					}
				} else {
					// The cursor has moved, do some boundboxing!
					if(last_click_map_x > mouse_map_x) {
						int tmp = mouse_map_x;
						mouse_map_x = last_click_map_x;
						last_click_map_x = tmp;
					}
					if(last_click_map_y > mouse_map_y) {
						int tmp = mouse_map_y;
						mouse_map_y = last_click_map_y;
						last_click_map_y = tmp;
					}

					int numtiles = 0;
					int threadcount = std::max(g_settings.getInteger(Config::WORKER_THREADS), 1);

					int start_x = 0, start_y = 0, start_z = 0;
					int end_x = 0, end_y = 0, end_z = 0;

					switch(g_settings.getInteger(Config::SELECTION_TYPE)) {
						case SELECT_CURRENT_FLOOR: {
							start_z = end_z = floor;
							start_x = last_click_map_x;
							start_y = last_click_map_y;
							end_x = mouse_map_x;
							end_y = mouse_map_y;
							break;
						}
						case SELECT_ALL_FLOORS: {
							start_x = last_click_map_x;
							start_y = last_click_map_y;
							start_z = rme::MapMaxLayer;
							end_x = mouse_map_x;
							end_y = mouse_map_y;
							end_z = floor;

							if(g_settings.getInteger(Config::COMPENSATED_SELECT)) {
								start_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
								start_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);

								end_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
								end_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
							}

							numtiles = (start_z - end_z) * (end_x - start_x) * (end_y - start_y);
							break;
						}
						case SELECT_VISIBLE_FLOORS: {
							start_x = last_click_map_x;
							start_y = last_click_map_y;
							if(floor < 8) {
								start_z = rme::MapGroundLayer;
							} else {
								start_z = std::min(rme::MapMaxLayer, floor + 2);
							}
							end_x = mouse_map_x;
							end_y = mouse_map_y;
							end_z = floor;

							if(g_settings.getInteger(Config::COMPENSATED_SELECT)) {
								start_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
								start_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);

								end_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
								end_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
							}
							break;
						}
					}

					if(numtiles < 500) {
						// No point in threading for such a small set.
						threadcount = 1;
					}
					// Subdivide the selection area
					// We know it's a square, just split it into several areas
					int width = end_x - start_x;
					if(width < threadcount) {
						threadcount = std::min(1, width);
					}
					// Let's divide!
					int remainder = width;
					int cleared = 0;
					std::vector<SelectionThread*> threads;
					if(width == 0) {
						threads.push_back(newd SelectionThread(editor, Position(start_x, start_y, start_z), Position(start_x, end_y, end_z), boundbox_select_creatures));
					} else {
						for(int i = 0; i < threadcount; ++i) {
							int chunksize = width / threadcount;
							// The last threads takes all the remainder
							if(i == threadcount - 1) {
								chunksize = remainder;
							}
							threads.push_back(newd SelectionThread(editor, Position(start_x + cleared, start_y, start_z), Position(start_x + cleared + chunksize, end_y, end_z), boundbox_select_creatures));
							cleared += chunksize;
							remainder -= chunksize;
						}
					}
					ASSERT(cleared == width);
					ASSERT(remainder == 0);

					if(deselect_mode) {
						selection.start(Selection::NONE, ACTION_UNSELECT); // Start a selection session
						switch(g_settings.getInteger(Config::SELECTION_TYPE)) {
							case SELECT_CURRENT_FLOOR: {
								for(int x = last_click_map_x; x <= mouse_map_x; x++) {
									for(int y = last_click_map_y; y <= mouse_map_y; y++) {
										Tile* tile = editor.getMap().getTile(x, y, floor);
										RemoveTileOrCreaturesFromSelection(selection, tile, boundbox_select_creatures, editor.getMap());
									}
								}
								break;
							}
							case SELECT_ALL_FLOORS: {
								int start_x = last_click_map_x;
								int start_y = last_click_map_y;
								int end_x = mouse_map_x;
								int end_y = mouse_map_y;
								int start_z = rme::MapMaxLayer;
								int end_z = floor;

								if(g_settings.getInteger(Config::COMPENSATED_SELECT)) {
									start_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
									start_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
									end_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
									end_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
								}

								for(int z = start_z; z >= end_z; z--) {
									for(int x = start_x; x <= end_x; x++) {
										for(int y = start_y; y <= end_y; y++) {
											Tile* tile = editor.getMap().getTile(x, y, z);
											RemoveTileOrCreaturesFromSelection(selection, tile, boundbox_select_creatures, editor.getMap());
										}
									}
									if(z <= rme::MapGroundLayer && g_settings.getInteger(Config::COMPENSATED_SELECT)) {
										start_x++; start_y++;
										end_x++; end_y++;
									}
								}
								break;
							}
							case SELECT_VISIBLE_FLOORS: {
								int start_x = last_click_map_x;
								int start_y = last_click_map_y;
								int end_x = mouse_map_x;
								int end_y = mouse_map_y;
								int start_z = (floor < 8) ? rme::MapGroundLayer : std::min(rme::MapMaxLayer, floor + 2);
								int end_z = floor;

								if(g_settings.getInteger(Config::COMPENSATED_SELECT)) {
									start_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
									start_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
									end_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
									end_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
								}

								for(int z = start_z; z >= end_z; z--) {
									for(int x = start_x; x <= end_x; x++) {
										for(int y = start_y; y <= end_y; y++) {
											Tile* tile = editor.getMap().getTile(x, y, z);
											RemoveTileOrCreaturesFromSelection(selection, tile, boundbox_select_creatures, editor.getMap());
										}
									}
									if(z <= rme::MapGroundLayer && g_settings.getInteger(Config::COMPENSATED_SELECT)) {
										start_x++; start_y++;
										end_x++; end_y++;
									}
								}
								break;
							}
						}
						selection.finish(); // Finish the selection session
						selection.updateSelectionCount();
					} else {
						selection.start(); // Start a selection session
						for(SelectionThread* thread : threads) {
							thread->Execute();
						}
						for(SelectionThread* thread : threads) {
							selection.join(thread);
						}
						selection.finish(); // Finish the selection session
						selection.updateSelectionCount();
					}
				}
			} else if(event.ControlDown()) {
				////
			} else {
				// User hasn't moved anything, meaning selection/deselection
				Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
				if(tile) {
					if(tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
						if(!tile->spawn->isSelected()) {
							selection.start(); // Start a selection session
							selection.add(tile, tile->spawn);
							selection.finish(); // Finish the selection session
							selection.updateSelectionCount();
						}
					} else if(tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
						if(!tile->creature->isSelected()) {
							selection.start(); // Start a selection session
							AddCreatureWithSpawn(selection, tile, editor.getMap());
							selection.finish(); // Finish the selection session
							selection.updateSelectionCount();
						}
					} else {
						Item* item = tile->getTopItem();
						if(item && !item->isSelected()) {
							selection.start(); // Start a selection session
							selection.add(tile, item);
							selection.finish(); // Finish the selection session
							selection.updateSelectionCount();
						}
					}
				}
			}
		}
		editor.resetActionsTimer();
		editor.updateActions();
		dragging = false;
		boundbox_selection = false;
		boundbox_select_creatures = false;
		boundbox_deselect = false;
		ClearLassoSelection();
	} else if(g_gui.GetCurrentBrush()){ // Drawing mode
		Brush* brush = g_gui.GetCurrentBrush();
		if(!replace_dragging) {
			if(dragging_draw) {
				if(brush->isSpawn()) {
					int start_map_x = std::min(last_click_map_x, mouse_map_x);
					int start_map_y = std::min(last_click_map_y, mouse_map_y);
					int end_map_x   = std::max(last_click_map_x, mouse_map_x);
					int end_map_y   = std::max(last_click_map_y, mouse_map_y);

					int map_x = start_map_x + (end_map_x - start_map_x)/2;
					int map_y = start_map_y + (end_map_y - start_map_y)/2;

					int width = std::min(g_settings.getInteger(Config::MAX_SPAWN_RADIUS), ((end_map_x - start_map_x)/2 + (end_map_y - start_map_y)/2)/2);
					int old = g_gui.GetBrushSize();
					g_gui.SetBrushSize(width);
					editor.draw(Position(map_x, map_y, floor), event.AltDown());
					g_gui.SetBrushSize(old);
				} else {
					PositionVector tilestodraw;
					PositionVector tilestoborder;
					if(brush->isWall()) {
						int start_map_x = std::min(last_click_map_x, mouse_map_x);
						int start_map_y = std::min(last_click_map_y, mouse_map_y);
						int end_map_x   = std::max(last_click_map_x, mouse_map_x);
						int end_map_y   = std::max(last_click_map_y, mouse_map_y);

						for(int y = start_map_y-1; y <= end_map_y+1; y ++) {
							for(int x = start_map_x-1; x <= end_map_x+1; x++) {
								if((x <= start_map_x+1 || x >= end_map_x-1) || (y <= start_map_y+1 || y >= end_map_y-1)) {
									tilestoborder.push_back(Position(x,y,floor));
								}
								if(((x == start_map_x || x == end_map_x) || (y == start_map_y || y == end_map_y)) &&
									((x >= start_map_x && x <= end_map_x) && (y >= start_map_y && y <= end_map_y))) {
									tilestodraw.push_back(Position(x,y,floor));
								}
							}
						}
					} else {
						if(g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE) {
							if(last_click_map_x > mouse_map_x) {
								int tmp = mouse_map_x; mouse_map_x = last_click_map_x; last_click_map_x = tmp;
							}
							if(last_click_map_y > mouse_map_y) {
								int tmp = mouse_map_y; mouse_map_y = last_click_map_y; last_click_map_y = tmp;
							}

							for(int x = last_click_map_x-1; x <= mouse_map_x+1; x++) {
								for(int y = last_click_map_y-1; y <= mouse_map_y+1; y ++) {
									if((x <= last_click_map_x || x >= mouse_map_x) || (y <= last_click_map_y || y >= mouse_map_y)) {
										tilestoborder.push_back(Position(x,y,floor));
									}
									if((x >= last_click_map_x && x <= mouse_map_x) && (y >= last_click_map_y && y <= mouse_map_y)) {
										tilestodraw.push_back(Position(x,y,floor));
									}
								}
							}
						} else {
							int start_x, end_x;
							int start_y, end_y;
							int width = std::max(
								std::abs(
								std::max(mouse_map_y, last_click_map_y) -
								std::min(mouse_map_y, last_click_map_y)
								),
								std::abs(
								std::max(mouse_map_x, last_click_map_x) -
								std::min(mouse_map_x, last_click_map_x)
								)
								);
							if(mouse_map_x < last_click_map_x) {
								start_x = last_click_map_x - width;
								end_x = last_click_map_x;
							} else {
								start_x = last_click_map_x;
								end_x = last_click_map_x + width;
							}
							if(mouse_map_y < last_click_map_y) {
								start_y = last_click_map_y - width;
								end_y = last_click_map_y;
							} else {
								start_y = last_click_map_y;
								end_y = last_click_map_y + width;
							}

							int center_x = start_x + (end_x - start_x) / 2;
							int center_y = start_y + (end_y - start_y) / 2;
							float radii = width / 2.0f + 0.005f;

							for(int y = start_y-1; y <= end_y+1; y++) {
								float dy = center_y - y;
								for(int x = start_x-1; x <= end_x+1; x++) {
									float dx = center_x - x;
									//printf("%f;%f\n", dx, dy);
									float distance = sqrt(dx*dx + dy*dy);
									if(distance < radii) {
										tilestodraw.push_back(Position(x,y,floor));
									}
									if(std::abs(distance - radii) < 1.5) {
										tilestoborder.push_back(Position(x,y,floor));
									}
								}
							}
						}
					}
					if(event.ControlDown()) {
						editor.undraw(tilestodraw, tilestoborder, event.AltDown());
					} else {
						editor.draw(tilestodraw, tilestoborder, event.AltDown());
					}
				}
		}
		}
		editor.resetActionsTimer();
		editor.updateActions();
		drawing = false;
		dragging_draw = false;
		replace_dragging = false;
		editor.replace_brush = nullptr;
	}
	g_gui.RefreshView();
	g_gui.UpdateMinimap();
}

void MapCanvas::OnMouseCameraClick(wxMouseEvent& event)
{
	SetFocus();

	last_mmb_click_x = event.GetX();
	last_mmb_click_y = event.GetY();

	if(event.ControlDown()) {
		int screensize_x, screensize_y;
		MapWindow* window = GetMapWindow();
		window->GetViewSize(&screensize_x, &screensize_y);
		window->ScrollRelative(
			int(-screensize_x * (1.0 - zoom) * (std::max(cursor_x, 1) / double(screensize_x))),
			int(-screensize_y * (1.0 - zoom) * (std::max(cursor_y, 1) / double(screensize_y)))
		);
		zoom = 1.0;
		Refresh();
	} else {
		screendragging = true;
	}
}

void MapCanvas::OnMouseCameraRelease(wxMouseEvent& event)
{
	SetFocus();
	screendragging = false;
	if(event.ControlDown()) {
		// ...
		// Haven't moved much, it's a click!
	} else if(last_mmb_click_x > event.GetX() - 3 && last_mmb_click_x < event.GetX() + 3 &&
				last_mmb_click_y > event.GetY() - 3 && last_mmb_click_y < event.GetY() + 3) {
		int screensize_x, screensize_y;
		MapWindow* window = GetMapWindow();
		window->GetViewSize(&screensize_x, &screensize_y);
		window->ScrollRelative(
			int(zoom * (2*cursor_x - screensize_x)),
			int(zoom * (2*cursor_y - screensize_y))
		);
		Refresh();
	}
}

void MapCanvas::OnMousePropertiesClick(wxMouseEvent& event)
{
	SetFocus();

	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);
	Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);

	if(g_gui.IsDrawingMode()) {
		g_gui.SetSelectionMode();
	}

	EndPasting();

	Selection& selection = editor.getSelection();

	boundbox_selection = false;
	boundbox_select_creatures = false;
	boundbox_deselect = false;
	if(IsLassoSelectionEnabled()) {
		ClearLassoSelection();
	}
	if(event.ShiftDown()) {
		boundbox_selection = true;
		boundbox_deselect = event.ControlDown() && event.ShiftDown() && event.AltDown();
		boundbox_select_creatures = event.AltDown() && !boundbox_deselect;
		if(IsLassoSelectionEnabled()) {
			StartLassoSelection(event.GetX(), event.GetY(), mouse_map_x, mouse_map_y);
		} else {
			ClearLassoSelection();
		}

		if(!event.ControlDown()) {
			selection.start(); // Start selection session
			selection.clear(); // Clear out selection
			selection.finish(); // End selection session
			selection.updateSelectionCount();
		}
	} else if(!tile) {
		selection.start(); // Start selection session
		selection.clear(); // Clear out selection
		selection.finish(); // End selection session
		selection.updateSelectionCount();
	} else if(tile->isSelected()) {
		// Do nothing!
	} else {
		selection.start(); // Start a selection session
		selection.clear();
		selection.commit();
		if(tile->creature && g_settings.getInteger(Config::SHOW_CREATURES)) {
			AddCreatureWithSpawn(selection, tile, editor.getMap());
		} else if(tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS)) {
			selection.add(tile, tile->spawn);
		} else {
			Item* item = tile->getTopItem();
			if(item) {
				selection.add(tile, item);
			}
		}
		selection.finish(); // Finish the selection session
		selection.updateSelectionCount();
	}

	last_click_x = int(event.GetX()*zoom);
	last_click_y = int(event.GetY()*zoom);

	int start_x, start_y;
	GetMapWindow()->GetViewStart(&start_x, &start_y);
	last_click_abs_x = last_click_x + start_x;
	last_click_abs_y = last_click_y + start_y;

	last_click_map_x = mouse_map_x;
	last_click_map_y = mouse_map_y;
	g_gui.RefreshView();
}

void MapCanvas::OnMousePropertiesRelease(wxMouseEvent& event)
{
	int mouse_map_x, mouse_map_y;
	ScreenToMap(event.GetX(), event.GetY(), &mouse_map_x, &mouse_map_y);

	if(g_gui.IsDrawingMode()) {
		g_gui.SetSelectionMode();
	}

	if(boundbox_selection) {
		Selection& selection = editor.getSelection();
		bool deselect_mode = boundbox_deselect;
		if(IsLassoSelectionEnabled()) {
			AddLassoPoint(event.GetX(), event.GetY(), mouse_map_x, mouse_map_y);
		}
		if(IsLassoSelectionEnabled() && lasso_map_points.size() >= 3) {
			selection.start(Selection::NONE, deselect_mode ? ACTION_UNSELECT : ACTION_SELECT); // Start a selection session
			ApplyLassoSelection(selection, boundbox_select_creatures, deselect_mode);
			selection.finish(); // Finish the selection session
			selection.updateSelectionCount();
		} else if(mouse_map_x == last_click_map_x && mouse_map_y == last_click_map_y && event.ControlDown()) {
			// Mouse hasn't move, do control+shift thingy!
			Tile* tile = editor.getMap().getTile(mouse_map_x, mouse_map_y, floor);
			if(tile) {
				selection.start(); // Start a selection session
				bool changed = false;
				if(deselect_mode) {
					RemoveTileOrCreaturesFromSelection(selection, tile, boundbox_select_creatures, editor.getMap());
					changed = true;
				} else {
					changed = ToggleTileObjectSelection(selection, tile, editor.getMap());
				}
				if(!changed) {
					if(tile->isSelected()) {
						selection.remove(tile);
					} else {
						selection.add(tile);
					}
					changed = true;
				}
				selection.finish(); // Finish the selection session
				if(changed) {
					selection.updateSelectionCount();
				}
			}
		} else {
			// The cursor has moved, do some boundboxing!
			if(last_click_map_x > mouse_map_x) {
				int tmp = mouse_map_x; mouse_map_x = last_click_map_x; last_click_map_x = tmp;
			}
			if(last_click_map_y > mouse_map_y) {
				int tmp = mouse_map_y; mouse_map_y = last_click_map_y; last_click_map_y = tmp;
			}

			if(deselect_mode) {
				selection.start(Selection::NONE, ACTION_UNSELECT); // Start a selection session
			} else {
				selection.start(); // Start a selection session
			}
			switch(g_settings.getInteger(Config::SELECTION_TYPE)) {
				case SELECT_CURRENT_FLOOR: {
					for(int x = last_click_map_x; x <= mouse_map_x; x++) {
						for(int y = last_click_map_y; y <= mouse_map_y; y ++) {
							Tile* tile = editor.getMap().getTile(x, y, floor);
							if(deselect_mode) {
								RemoveTileOrCreaturesFromSelection(selection, tile, boundbox_select_creatures, editor.getMap());
							} else {
								AddTileOrCreaturesToSelection(selection, tile, boundbox_select_creatures, editor.getMap());
							}
						}
					}
					break;
				}
				case SELECT_ALL_FLOORS: {
					int start_x, start_y, start_z;
					int end_x, end_y, end_z;

					start_x = last_click_map_x;
					start_y = last_click_map_y;
					start_z = rme::MapMaxLayer;
					end_x = mouse_map_x;
					end_y = mouse_map_y;
					end_z = floor;

					if(g_settings.getInteger(Config::COMPENSATED_SELECT)) {
						start_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
						start_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);

						end_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
						end_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
					}

					for(int z = start_z; z >= end_z; z--) {
						for(int x = start_x; x <= end_x; x++) {
							for(int y = start_y; y <= end_y; y++) {
								Tile* tile = editor.getMap().getTile(x, y, z);
								if(deselect_mode) {
									RemoveTileOrCreaturesFromSelection(selection, tile, boundbox_select_creatures, editor.getMap());
								} else {
									AddTileOrCreaturesToSelection(selection, tile, boundbox_select_creatures, editor.getMap());
								}
							}
						}
						if(z <= rme::MapGroundLayer && g_settings.getInteger(Config::COMPENSATED_SELECT)) {
							start_x++; start_y++;
							end_x++; end_y++;
						}
					}
					break;
				}
				case SELECT_VISIBLE_FLOORS: {
					int start_x, start_y, start_z;
					int end_x, end_y, end_z;

					start_x = last_click_map_x;
					start_y = last_click_map_y;
					if(floor < 8) {
						start_z = rme::MapGroundLayer;
					} else {
						start_z = std::min(rme::MapMaxLayer, floor + 2);
					}
					end_x = mouse_map_x;
					end_y = mouse_map_y;
					end_z = floor;

					if(g_settings.getInteger(Config::COMPENSATED_SELECT)) {
						start_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
						start_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);

						end_x -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
						end_y -= (floor < rme::MapGroundLayer ? rme::MapGroundLayer - floor : 0);
					}

					for(int z = start_z; z >= end_z; z--) {
						for(int x = start_x; x <= end_x; x++) {
							for(int y = start_y; y <= end_y; y++) {
								Tile* tile = editor.getMap().getTile(x, y, z);
								if(deselect_mode) {
									RemoveTileOrCreaturesFromSelection(selection, tile, boundbox_select_creatures, editor.getMap());
								} else {
									AddTileOrCreaturesToSelection(selection, tile, boundbox_select_creatures, editor.getMap());
								}
							}
						}
						if(z <= rme::MapGroundLayer && g_settings.getInteger(Config::COMPENSATED_SELECT)) {
							start_x++; start_y++;
							end_x++; end_y++;
						}
					}
					break;
				}
			}
			selection.finish(); // Finish the selection session
			selection.updateSelectionCount();
		}
	} else if(event.ControlDown()) {
		// Nothing
	}

	boundbox_select_creatures = false;

	popup_menu->Update();
	PopupMenu(popup_menu);

	editor.resetActionsTimer();
	dragging = false;
	boundbox_selection = false;

	ClearLassoSelection();

	last_cursor_map_x = mouse_map_x;
	last_cursor_map_y = mouse_map_y;
	last_cursor_map_z = floor;

	g_gui.RefreshView();
}

void MapCanvas::OnWheel(wxMouseEvent& event)
{
	// Signal that user is active
	animation_timer->RequestRefresh();

	if(event.ControlDown()) {
		static double diff = 0.0;
		diff += event.GetWheelRotation();
		if(diff <= 1.0 || diff >= 1.0) {
			if(diff < 0.0) {
				g_gui.ChangeFloor(floor - 1);
			} else {
				g_gui.ChangeFloor(floor + 1);
			}
			diff = 0.0;
		}
		UpdatePositionStatus();
	} else if(event.AltDown()) {
		static double diff = 0.0;
		diff += event.GetWheelRotation();
		if(diff <= 1.0 || diff >= 1.0) {
			if(diff < 0.0) {
				g_gui.IncreaseBrushSize();
			} else {
				g_gui.DecreaseBrushSize();
			}
			diff = 0.0;
		}
	} else {
		double diff = -event.GetWheelRotation() * g_settings.getFloat(Config::ZOOM_SPEED) / 640.0;
		double oldzoom = zoom;
		zoom += diff;

		if(zoom < 0.125) {
			diff = 0.125 - oldzoom;
			zoom = 0.125;
		}
		if(zoom > 25.00) {
			diff = 25.00 - oldzoom;
			zoom = 25.0;
		}

		UpdateZoomStatus();

		int screensize_x, screensize_y;
		MapWindow* window = GetMapWindow();
		window->GetViewSize(&screensize_x, &screensize_y);

		// This took a day to figure out!
		int scroll_x = int(screensize_x * diff * (std::max(cursor_x, 1) / double(screensize_x))) * GetContentScaleFactor();
		int scroll_y = int(screensize_y * diff * (std::max(cursor_y, 1) / double(screensize_y))) * GetContentScaleFactor();

		window->ScrollRelative(-scroll_x, -scroll_y);
	}

	Refresh();
}

void MapCanvas::OnLoseMouse(wxMouseEvent& event)
{
	cursor_in_window = false;
	alt_ground_mode = false;
	ClearAutoborderPreview();
	Refresh();
}

void MapCanvas::OnGainMouse(wxMouseEvent& event)
{
	if(!event.LeftIsDown()) {
		dragging = false;
		boundbox_selection = false;
		drawing = false;
	}
	if(!event.MiddleIsDown()) {
		screendragging = false;
	}

	Refresh();
}

void MapCanvas::OnKeyDown(wxKeyEvent& event)
{
	// Signal that user is active
	animation_timer->RequestRefresh();

	if(event.GetKeyCode() == WXK_ESCAPE && g_gui.IsRectanglePickActive()) {
		g_gui.CancelRectanglePick();
		return;
	}

	if(StructureManagerDialog::HandleGlobalHotkey(event))
		return;

	if(HandleMouseKeyboardHotkey(event, true))
		return;

	MapWindow* window = GetMapWindow();

	//char keycode = event.GetKeyCode();
	// std::cout << "Keycode " << keycode << std::endl;
	switch(event.GetKeyCode()) {
		case WXK_NUMPAD_ADD:
		case WXK_PAGEUP: {
			g_gui.ChangeFloor(floor - 1);
			break;
		}
		case WXK_NUMPAD_SUBTRACT:
		case WXK_PAGEDOWN: {
			g_gui.ChangeFloor(floor + 1);
			break;
		}
		case WXK_NUMPAD_MULTIPLY: {
			double diff = -0.3;

			double oldzoom = zoom;
			zoom += diff;

			if(zoom < 0.125) {
				diff = 0.125 - oldzoom; zoom = 0.125;
			}

			int screensize_x, screensize_y;
			window->GetViewSize(&screensize_x, &screensize_y);

			// This took a day to figure out!
			int scroll_x = int(screensize_x * diff * (std::max(cursor_x, 1) / double(screensize_x)));
			int scroll_y = int(screensize_y * diff * (std::max(cursor_y, 1) / double(screensize_y)));

			window->ScrollRelative(-scroll_x, -scroll_y);

			UpdatePositionStatus();
			UpdateZoomStatus();
			Refresh();
			break;
		}
		case WXK_NUMPAD_DIVIDE: {
			double diff = 0.3;
			double oldzoom = zoom;
			zoom += diff;

			if(zoom > 25.00) {
				diff = 25.00 - oldzoom; zoom = 25.0;
			}

			int screensize_x, screensize_y;
			window->GetViewSize(&screensize_x, &screensize_y);

			// This took a day to figure out!
			int scroll_x = int(screensize_x * diff * (std::max(cursor_x, 1) / double(screensize_x)));
			int scroll_y = int(screensize_y * diff * (std::max(cursor_y, 1) / double(screensize_y)));
			window->ScrollRelative(-scroll_x, -scroll_y);

			UpdatePositionStatus();
			UpdateZoomStatus();
			Refresh();
			break;
		}
		// This will work like crap with non-us layouts, well, sucks for them until there is another solution.
		case '[':
		case '+': {
			g_gui.IncreaseBrushSize();
			Refresh();
			break;
		}
		case ']':
		case '-': {
			g_gui.DecreaseBrushSize();
			Refresh();
			break;
		}
		case WXK_NUMPAD_UP:
		case WXK_UP: {
			int start_x, start_y;
			window->GetViewStart(&start_x, &start_y);

			int tiles = 3;
			if(event.ControlDown())
				tiles = 10;
			else if(zoom == 1.0)
				tiles = 1;

			window->Scroll(start_x, int(start_y - rme::TileSize * tiles * zoom));
			UpdatePositionStatus();
			Refresh();
			break;
		}
		case WXK_NUMPAD_DOWN:
		case WXK_DOWN: {
			int start_x, start_y;
			window->GetViewStart(&start_x, &start_y);

			int tiles = 3;
			if(event.ControlDown())
				tiles = 10;
			else if(zoom == 1.0)
				tiles = 1;

			window->Scroll(start_x, int(start_y + rme::TileSize * tiles * zoom));
			UpdatePositionStatus();
			Refresh();
			break;
		}
		case WXK_NUMPAD_LEFT:
		case WXK_LEFT: {
			int start_x, start_y;
			window->GetViewStart(&start_x, &start_y);

			int tiles = 3;
			if(event.ControlDown())
				tiles = 10;
			else if(zoom == 1.0)
				tiles = 1;

			window->Scroll(int(start_x - rme::TileSize * tiles * zoom), start_y);
			UpdatePositionStatus();
			Refresh();
			break;
		}
		case WXK_NUMPAD_RIGHT:
		case WXK_RIGHT: {
			int start_x, start_y;
			window->GetViewStart(&start_x, &start_y);

			int tiles = 3;
			if(event.ControlDown())
				tiles = 10;
			else if(zoom == 1.0)
				tiles = 1;

			window->Scroll(int(start_x + rme::TileSize * tiles * zoom), start_y);
			UpdatePositionStatus();
			Refresh();
			break;
		}
		case WXK_SPACE: { // Utility keys
			if(event.ControlDown()) {
				g_gui.FillDoodadPreviewBuffer();
				g_gui.RefreshView();
			} else {
				g_gui.SwitchMode();
			}
			break;
		}
		case WXK_TAB: { // Tab switch
			if(event.ShiftDown()) {
				g_gui.CycleTab(false);
			} else {
				g_gui.CycleTab(true);
			}
			break;
		}
		case WXK_DELETE: { // Delete
			editor.destroySelection();
			g_gui.RefreshView();
			break;
		}
		case 'z':
		case 'Z': { // Rotate counterclockwise (actually shift variaton, but whatever... :P)
			int nv = g_gui.GetBrushVariation();
			--nv;
			if(nv < 0) {
				nv = std::max(0, (g_gui.GetCurrentBrush()? g_gui.GetCurrentBrush()->getMaxVariation() - 1 : 0));
			}
			g_gui.SetBrushVariation(nv);
			g_gui.RefreshView();
			break;
		}
		case 'x':
		case 'X': { // Rotate clockwise (actually shift variaton, but whatever... :P)
			int nv = g_gui.GetBrushVariation();
			++nv;
			if(nv >= (g_gui.GetCurrentBrush()? g_gui.GetCurrentBrush()->getMaxVariation() : 0)) {
				nv = 0;
			}
			g_gui.SetBrushVariation(nv);
			g_gui.RefreshView();
			break;
		}
		case 'q':
		case 'Q': { // Select previous brush
			g_gui.SelectPreviousBrush();
			break;
		}
		// Hotkeys
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9': {
			int index = event.GetKeyCode() - '0';
			if(event.ControlDown()) {
				Hotkey hk;
				if(g_gui.IsSelectionMode()) {
					int view_start_x, view_start_y;
					window->GetViewStart(&view_start_x, &view_start_y);
					int view_start_map_x = view_start_x / rme::TileSize, view_start_map_y = view_start_y / rme::TileSize;

					int view_screensize_x, view_screensize_y;
					window->GetViewSize(&view_screensize_x, &view_screensize_y);

					int map_x = int(view_start_map_x + (view_screensize_x * zoom) / rme::TileSize / 2);
					int map_y = int(view_start_map_y + (view_screensize_y * zoom) / rme::TileSize / 2);

					hk = Hotkey(Position(map_x, map_y, floor));
				} else if(g_gui.GetCurrentBrush()) {
					// Drawing mode
					hk = Hotkey(g_gui.GetCurrentBrush());
				} else {
					break;
				}
				g_gui.SetHotkey(index, hk);
			} else {
				// Click hotkey
				Hotkey hk = g_gui.GetHotkey(index);
				if(hk.IsPosition()) {
					g_gui.SetSelectionMode();

					int map_x = hk.GetPosition().x;
					int map_y = hk.GetPosition().y;
					int map_z = hk.GetPosition().z;

					window->Scroll(rme::TileSize * map_x, rme::TileSize * map_y, true);
					floor = map_z;

					g_gui.SetStatusText("Used hotkey " + i2ws(index));
					g_gui.RefreshView();
				} else if(hk.IsBrush()) {
					g_gui.SetDrawingMode();

					std::string name = hk.GetBrushname();
					Brush* brush = g_brushes.getBrush(name);
					if(brush == nullptr) {
						g_gui.SetStatusText("Brush \"" + wxstr(name) + "\" not found");
						return;
					}

					if(!g_gui.SelectBrush(brush)) {
						g_gui.SetStatusText("Brush \"" + wxstr(name) + "\" is not in any palette");
						return;
					}

					g_gui.SetStatusText("Used hotkey " + i2ws(index));
					g_gui.RefreshView();
				} else {
					g_gui.SetStatusText("Unassigned hotkey " + i2ws(index));
				}
			}
			break;
		}
		case 'd':
		case 'D': {
			keyCode = WXK_CONTROL_D;
			break;
		}
		default:{
			event.Skip();
			break;
		}
	}
}

void MapCanvas::OnKeyUp(wxKeyEvent& event)
{
	if(HandleMouseKeyboardHotkey(event, false)) {
		keyCode = WXK_NONE;
		return;
	}

	keyCode = WXK_NONE;
}

void MapCanvas::OnCopy(wxCommandEvent& WXUNUSED(event))
{
	if(g_gui.IsSelectionMode())
	   editor.copybuffer.copy(editor, GetFloor());
}

void MapCanvas::OnCut(wxCommandEvent& WXUNUSED(event))
{
	if(g_gui.IsSelectionMode())
		editor.copybuffer.cut(editor, GetFloor());
	g_gui.RefreshView();
}

void MapCanvas::OnPaste(wxCommandEvent& WXUNUSED(event))
{
	g_gui.DoPaste();
	g_gui.RefreshView();
}

void MapCanvas::OnDelete(wxCommandEvent& WXUNUSED(event))
{
	editor.destroySelection();
	g_gui.RefreshView();
}

void MapCanvas::OnDeleteAll(wxCommandEvent& WXUNUSED(event))
{
	ASSERT(editor.getSelection().size() == 1);

	Tile* tile = editor.getSelection().getSelectedTile();
	ItemVector selected_items = tile->getSelectedItems();
	ASSERT(selected_items.size() == 1);

	const Item* item = selected_items.front();
	uint32_t itemId = item->getID();

	Selection& selection = editor.getSelection();
	selection.start(Selection::NONE, ACTION_UNSELECT);
	selection.clear();
	selection.finish();
	selection.updateSelectionCount();

	OnMapRemoveItems::RemoveItemCondition condition(itemId);
	g_gui.CreateLoadBar("Searching map for items to remove...");

	int64_t count = RemoveItemOnMap(g_gui.GetCurrentMap(), condition, false);

	g_gui.DestroyLoadBar();

	wxString msg;
	msg << count << " items deleted.";

	g_gui.PopupDialog("Search completed", msg, wxOK);
	g_gui.GetCurrentMap().doChange();
	g_gui.RefreshView();
}

void MapCanvas::OnApplyReplaceBox1(wxCommandEvent& WXUNUSED(event))
{
	ASSERT(editor.getSelection().size() == 1);

	Tile* tile = editor.getSelection().getSelectedTile();
	ItemVector selected_items = tile->getSelectedItems();
	ASSERT(selected_items.size() == 1);

	const Item* item = selected_items.front();
	uint16_t itemId = item->getID();

	MapWindow* window = GetMapWindow();
	if(window) {
		window->ApplyItemToReplaceBoxOriginal(itemId);
	}
}

void MapCanvas::OnApplyReplaceBox2(wxCommandEvent& WXUNUSED(event))
{
	ASSERT(editor.getSelection().size() == 1);

	Tile* tile = editor.getSelection().getSelectedTile();
	ItemVector selected_items = tile->getSelectedItems();
	ASSERT(selected_items.size() == 1);

	const Item* item = selected_items.front();
	uint16_t itemId = item->getID();

	MapWindow* window = GetMapWindow();
	if(window) {
		window->ApplyItemToReplaceBoxReplacement(itemId);
	}
}

void MapCanvas::OnCopyPosition(wxCommandEvent& WXUNUSED(event))
{
	if (editor.hasSelection()) {
		const Position minPos = editor.getSelection().minPosition();
		const Position maxPos = editor.getSelection().maxPosition();
		if (minPos != maxPos) {
			posToClipboard(minPos.x, minPos.y, minPos.z, maxPos.x, maxPos.y, maxPos.z);
			return;
		}
	}

	MapTab* tab = g_gui.GetCurrentMapTab();
	if (tab) {
		MapCanvas* canvas = tab->GetCanvas();
		int x, y;
		int z = canvas->GetFloor();
		canvas->MouseToMap(&x, &y);
		posToClipboard(x, y, z, g_settings.getInteger(Config::COPY_POSITION_FORMAT));
	}
}

void MapCanvas::OnCopyServerId(wxCommandEvent& WXUNUSED(event))
{
	ASSERT(editor.getSelection().size() == 1);

	if(wxTheClipboard->Open()) {
		Tile* tile = editor.getSelection().getSelectedTile();
		ItemVector selected_items = tile->getSelectedItems();
		ASSERT(selected_items.size() == 1);

		const Item* item = selected_items.front();

		wxTextDataObject* obj = new wxTextDataObject();
		obj->SetText(i2ws(item->getID()));
		wxTheClipboard->SetData(obj);

		wxTheClipboard->Close();
	}
}

void MapCanvas::OnCopyClientId(wxCommandEvent& WXUNUSED(event))
{
	ASSERT(editor.getSelection().size() == 1);

	if(wxTheClipboard->Open()) {
		Tile* tile = editor.getSelection().getSelectedTile();
		ItemVector selected_items = tile->getSelectedItems();
		ASSERT(selected_items.size() == 1);

		const Item* item = selected_items.front();

		wxTextDataObject* obj = new wxTextDataObject();
		obj->SetText(i2ws(item->getClientID()));
		wxTheClipboard->SetData(obj);

		wxTheClipboard->Close();
	}
}

void MapCanvas::OnCopyName(wxCommandEvent& WXUNUSED(event))
{
	ASSERT(editor.getSelection().size() == 1);

	if(wxTheClipboard->Open()) {
		Tile* tile = editor.getSelection().getSelectedTile();
		ItemVector selected_items = tile->getSelectedItems();
		ASSERT(selected_items.size() == 1);

		const Item* item = selected_items.front();

		wxTextDataObject* obj = new wxTextDataObject();
		obj->SetText(wxstr(item->getName()));
		wxTheClipboard->SetData(obj);

		wxTheClipboard->Close();
	}
}

void MapCanvas::OnBrowseTile(wxCommandEvent& WXUNUSED(event))
{
	if(editor.getSelection().size() != 1)
		return;

	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile) return;
	ASSERT(tile->isSelected());
	Tile* new_tile = tile->deepCopy(editor.getMap());

	wxDialog* w = new BrowseTileWindow(g_gui.root, new_tile, wxPoint(cursor_x, cursor_y));

	int ret = w->ShowModal();
	if(ret != 0) {
		Action* action = editor.createAction(ACTION_DELETE_TILES);
		action->addChange(newd Change(new_tile));
		editor.addAction(action);
		editor.updateActions();
	} else {
		// Cancel
		delete new_tile;
	}

	w->Destroy();
}

void MapCanvas::OnRotateItem(wxCommandEvent& WXUNUSED(event))
{
	if(!editor.hasSelection()) {
		return;
	}

	Selection& selection = editor.getSelection();
	if(selection.size() != 1) {
		return;
	}

	Tile* tile = selection.getSelectedTile();
	ItemVector items = tile->getSelectedItems();
	if(items.empty()) {
		return;
	}

	Item* item = items.front();
	if(!item || !item->isRoteable()) {
		return;
	}

	Action* action = editor.createAction(ACTION_ROTATE_ITEM);
	Tile* new_tile = tile->deepCopy(editor.getMap());
	Item* new_item = new_tile->getSelectedItems().front();
	new_item->doRotate();
	action->addChange(new Change(new_tile));

 	editor.addAction(action);
	editor.updateActions();
	g_gui.RefreshView();
}

void MapCanvas::OnRotateSelectionCW(wxCommandEvent& WXUNUSED(event))
{
	editor.rotateSelection(1);
	g_gui.RefreshView();
}

void MapCanvas::OnRotateSelectionCCW(wxCommandEvent& WXUNUSED(event))
{
	editor.rotateSelection(3);
	g_gui.RefreshView();
}

void MapCanvas::OnRotateSelection180(wxCommandEvent& WXUNUSED(event))
{
	editor.rotateSelection(2);
	g_gui.RefreshView();
}

void MapCanvas::OnGotoDestination(wxCommandEvent& WXUNUSED(event))
{
	Tile* tile = editor.getSelection().getSelectedTile();
	ItemVector selected_items = tile->getSelectedItems();
	ASSERT(selected_items.size() > 0);
	Teleport* teleport = dynamic_cast<Teleport*>(selected_items.front());
	if(teleport) {
		Position pos = teleport->getDestination();
		g_gui.SetScreenCenterPosition(pos);
	}
}

void MapCanvas::OnCopyDestination(wxCommandEvent& WXUNUSED(event))
{
	Tile* tile = editor.getSelection().getSelectedTile();
	ItemVector selected_items = tile->getSelectedItems();
	ASSERT(selected_items.size() > 0);

	Teleport* teleport = dynamic_cast<Teleport*>(selected_items.front());
	if(teleport) {
		const Position& destination = teleport->getDestination();
		int format = g_settings.getInteger(Config::COPY_POSITION_FORMAT);
		posToClipboard(destination.x, destination.y, destination.z, format);
	}
}

void MapCanvas::OnSwitchDoor(wxCommandEvent& WXUNUSED(event))
{
	Tile* tile = editor.getSelection().getSelectedTile();

	Action* action = editor.createAction(ACTION_SWITCHDOOR);

	Tile* new_tile = tile->deepCopy(editor.getMap());

	ItemVector selected_items = new_tile->getSelectedItems();
	ASSERT(selected_items.size() > 0);

	DoorBrush::switchDoor(selected_items.front());

	action->addChange(newd Change(new_tile));

	editor.addAction(action);
	editor.updateActions();
	g_gui.RefreshView();
}

void MapCanvas::OnSelectRAWBrush(wxCommandEvent& WXUNUSED(event))
{
	if(editor.getSelection().size() != 1) return;
	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile) return;
	Item* item = tile->getTopSelectedItem();

	if(item && item->getRAWBrush())
		g_gui.SelectBrush(item->getRAWBrush(), TILESET_RAW);
}

void MapCanvas::OnSelectGroundBrush(wxCommandEvent& WXUNUSED(event))
{
	if(editor.getSelection().size() != 1) return;
	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile) return;
	GroundBrush* bb = tile->getGroundBrush();

	if(bb)
		g_gui.SelectBrush(bb, TILESET_TERRAIN);
}

void MapCanvas::OnSelectDoodadBrush(wxCommandEvent& WXUNUSED(event))
{
	if(editor.getSelection().size() != 1) return;
	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile) return;
	Item* item = tile->getTopSelectedItem();

	if(item)
		g_gui.SelectBrush(item->getDoodadBrush(), TILESET_DOODAD);
}

void MapCanvas::OnSelectDoorBrush(wxCommandEvent& WXUNUSED(event))
{
	if(editor.getSelection().size() != 1) return;
	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile) return;
	Item* item = tile->getTopSelectedItem();

	if(item)
		g_gui.SelectBrush(item->getDoorBrush(), TILESET_TERRAIN);
}

void MapCanvas::OnSelectWallBrush(wxCommandEvent& WXUNUSED(event))
{
	if(editor.getSelection().size() != 1) return;
	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile) return;
	Item* wall = tile->getWall();
	WallBrush* wb = wall->getWallBrush();

	if(wb)
		g_gui.SelectBrush(wb, TILESET_TERRAIN);
}

void MapCanvas::OnSelectCarpetBrush(wxCommandEvent& WXUNUSED(event))
{
	if(editor.getSelection().size() != 1) return;
	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile) return;
	Item* wall = tile->getCarpet();
	CarpetBrush* cb = wall->getCarpetBrush();

	if(cb)
		g_gui.SelectBrush(cb);
}

void MapCanvas::OnSelectTableBrush(wxCommandEvent& WXUNUSED(event))
{
	if(editor.getSelection().size() != 1) return;
	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile) return;
	Item* wall = tile->getTable();
	TableBrush* tb = wall->getTableBrush();

	if(tb)
		g_gui.SelectBrush(tb);
}

void MapCanvas::OnSelectHouseBrush(wxCommandEvent& WXUNUSED(event))
{
	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile)
		return;

	if(tile->isHouseTile()) {
		House* house = editor.getMap().houses.getHouse(tile->getHouseID());
		if(house) {
			g_gui.house_brush->setHouse(house);
			g_gui.SelectBrush(g_gui.house_brush, TILESET_HOUSE);
		}
	}
}

void MapCanvas::OnSelectCreatureBrush(wxCommandEvent& WXUNUSED(event))
{
	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile)
		return;

	if(tile->creature)
		g_gui.SelectBrush(tile->creature->getBrush(), TILESET_CREATURE);
}

void MapCanvas::OnSelectSpawnBrush(wxCommandEvent& WXUNUSED(event))
{
	g_gui.SelectBrush(g_gui.spawn_brush, TILESET_CREATURE);
}

void MapCanvas::OnToggleCarpetActivated(wxCommandEvent& WXUNUSED(event))
{
	if(editor.getSelection().size() != 1) return;
	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile) return;

	Item* carpet = tile->getCarpet();
	if(!carpet) return;

	CarpetBrush* cb = carpet->getCarpetBrush();
	if(!cb || !cb->hasSourceFile()) return;

	if(cb->toggleActivatedInXML()) {
		wxMessageBox(
			wxString::Format("Brush '%s' has been deactivated in:\n%s\n\nReload brushes to apply changes.",
				wxString(cb->getName()), wxString(cb->getSourceFile())),
			"Brush Deactivated",
			wxOK | wxICON_INFORMATION
		);
	} else {
		wxMessageBox(
			wxString::Format("Failed to update brush '%s' in XML file.", wxString(cb->getName())),
			"Error",
			wxOK | wxICON_ERROR
		);
	}
}

void MapCanvas::OnToggleDoodadActivated(wxCommandEvent& WXUNUSED(event))
{
	if(editor.getSelection().size() != 1) return;
	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile) return;

	Item* topSelectedItem = tile->getTopSelectedItem();
	if(!topSelectedItem) return;

	Brush* brush = topSelectedItem->getDoodadBrush();
	if(!brush || !brush->hasSourceFile()) return;
	DoodadBrush* db = brush->asDoodad();
	if(!db) return;

	if(db->toggleActivatedInXML()) {
		wxMessageBox(
			wxString::Format("Brush '%s' has been deactivated in:\n%s\n\nReload brushes to apply changes.",
				wxString(db->getName()), wxString(db->getSourceFile())),
			"Brush Deactivated",
			wxOK | wxICON_INFORMATION
		);
	} else {
		wxMessageBox(
			wxString::Format("Failed to update brush '%s' in XML file.", wxString(db->getName())),
			"Error",
			wxOK | wxICON_ERROR
		);
	}
}

void MapCanvas::OnProperties(wxCommandEvent& WXUNUSED(event))
{
	if(editor.getSelection().size() != 1)
		return;

	Tile* tile = editor.getSelection().getSelectedTile();
	if(!tile) return;
	ASSERT(tile->isSelected());
	Tile* new_tile = tile->deepCopy(editor.getMap());

	wxDialog* w = nullptr;

	if(new_tile->spawn && g_settings.getInteger(Config::SHOW_SPAWNS))
		w = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), new_tile, new_tile->spawn);
	else if(new_tile->creature && g_settings.getInteger(Config::SHOW_CREATURES))
		w = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), new_tile, new_tile->creature);
	else {
		ItemVector selected_items = new_tile->getSelectedItems();

		Item* item = nullptr;
		int count = 0;
		for(ItemVector::iterator it = selected_items.begin(); it != selected_items.end(); ++it) {
			++count;
			if((*it)->isSelected()) {
				item = *it;
			}
		}

		if(item) {
			if(editor.getMap().getVersion().otbm >= MAP_OTBM_4)
				w = newd PropertiesWindow(g_gui.root, &editor.getMap(), new_tile, item);
			else
				w = newd OldPropertiesWindow(g_gui.root, &editor.getMap(), new_tile, item);
		}
		else
			return;
	}

	int ret = w->ShowModal();
	if(ret != 0) {
		Action* action = editor.createAction(ACTION_CHANGE_PROPERTIES);
		action->addChange(newd Change(new_tile));
		editor.addAction(action);
	} else {
		// Cancel!
		delete new_tile;
	}
	w->Destroy();
}

void MapCanvas::OnAddAreaDecorationRule(wxCommandEvent& WXUNUSED(event))
{
	if(!editor.hasSelection()) return;

	AreaDecorationRuleFromSelectionDialog dialog(this, editor);
	if(dialog.ShowModal() == wxID_OK && dialog.WasAccepted()) {
		// Show the Area Decoration dialog and add the generated rule
		g_gui.ShowAreaDecorationDialog();
		if(g_gui.area_decoration_dialog) {
			g_gui.area_decoration_dialog->AddRuleFromExternal(dialog.GetGeneratedRule());
		}
	}
}

void MapCanvas::ChangeFloor(int new_floor)
{
	ASSERT(new_floor >= 0 || new_floor <= rme::MapMaxLayer);
	int old_floor = floor;
	floor = new_floor;
	if(old_floor != new_floor) {
		if(g_settings.getBoolean(Config::FLOOR_FADING)) {
			int mode = g_settings.getInteger(Config::FLOOR_FADING_MODE);
			if(mode == 1) {
				// Continuous mode: accumulate layers
				floor_fade_layers.emplace_back(old_floor);
			}
			floor_fade_old_floor = old_floor;
			floor_fading = true;
			floor_fade_timer.Start();
		}
		if(!preview_mode) {
			UpdatePositionStatus();
			g_gui.root->UpdateFloorMenu();
			g_gui.UpdateMinimap(true);
		}
		ClearAutoborderPreview();
	}
	Refresh();
}

namespace {
	float applyEasing(float t, int easing_type)
	{
		switch(easing_type) {
			case 0: return t; // Linear
			case 1: return 1.0f - (1.0f - t) * (1.0f - t); // Ease-Out
			case 2: return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f; // Ease-In-Out
			default: return t;
		}
	}
}

bool MapCanvas::IsFloorFading() const
{
	if(!floor_fading)
		return false;
	long duration = g_settings.getInteger(Config::FLOOR_FADING_DURATION);
	return floor_fade_timer.Time() < duration;
}

int MapCanvas::GetFloorFadeAlpha() const
{
	if(!floor_fading)
		return 0;

	long duration = g_settings.getInteger(Config::FLOOR_FADING_DURATION);
	long elapsed = floor_fade_timer.Time();
	if(elapsed >= duration)
		return 0;

	int easing = g_settings.getInteger(Config::FLOOR_FADING_EASING);
	int max_opacity = g_settings.getInteger(Config::FLOOR_FADING_OPACITY);
	float max_alpha = 255.0f * max_opacity / 100.0f;

	float t = static_cast<float>(elapsed) / duration;
	float eased = applyEasing(t, easing);

	return static_cast<int>(max_alpha * (1.0f - eased));
}

float MapCanvas::GetFloorFadeProgress() const
{
	if(!floor_fading)
		return 1.0f;

	long duration = g_settings.getInteger(Config::FLOOR_FADING_DURATION);
	long elapsed = floor_fade_timer.Time();
	if(elapsed >= duration)
		return 1.0f;

	int easing = g_settings.getInteger(Config::FLOOR_FADING_EASING);
	float t = static_cast<float>(elapsed) / duration;
	return applyEasing(t, easing);
}

void MapCanvas::EnterDrawingMode()
{
	dragging = false;
	boundbox_selection = false;
	ClearLassoSelection();
	EndPasting();
	Refresh();
}

void MapCanvas::EnterSelectionMode()
{
	drawing = false;
	dragging_draw = false;
	replace_dragging = false;
	editor.replace_brush = nullptr;
	ClearAutoborderPreview();
	Refresh();
}

bool MapCanvas::isPasting() const
{
	if(preview_mode) {
		return false;
	}
	return g_gui.IsPasting();
}

void MapCanvas::StartPasting()
{
	if(preview_mode) {
		return;
	}
	g_gui.StartPasting();
}

void MapCanvas::EndPasting()
{
	if(preview_mode) {
		return;
	}
	g_gui.EndPasting();
}

void MapCanvas::Reset()
{
	StopGifRecording(false, false);
	if(camera_path_playing) {
		camera_path_timer.Stop();
		camera_path_playing = false;
		camera_path_time = 0.0;
		camera_path_name.clear();
	}

	cursor_x = 0;
	cursor_y = 0;

	zoom = 1.0;
	floor = rme::MapGroundLayer;

	dragging = false;
	boundbox_selection = false;
	screendragging = false;
	drawing = false;
	dragging_draw = false;
	suppress_right_release = false;
	boundbox_deselect = false;
	ClearLassoSelection();
	ClearAutoborderPreview();

	replace_dragging = false;
	editor.replace_brush = nullptr;

	drag_start_x = -1;
	drag_start_y = -1;
	drag_start_z = -1;

	last_click_map_x = -1;
	last_click_map_y = -1;
	last_click_map_z = -1;

	last_mmb_click_x = -1;
	last_mmb_click_y = -1;

	g_creature_walk_animator.clear();

	editor.getSelection().clear();
	editor.clearActions();
}

MapPopupMenu::MapPopupMenu(Editor& editor) : wxMenu(""), editor(editor)
{
	////
}

MapPopupMenu::~MapPopupMenu()
{
	////
}

void MapPopupMenu::Update()
{
	// Clear the menu of all items
	while(GetMenuItemCount() != 0) {
		wxMenuItem* m_item = FindItemByPosition(0);
		// If you add a submenu, this won't delete it.
		Delete(m_item);
	}

	bool anything_selected = editor.hasSelection();

	wxMenuItem* cutItem = Append( MAP_POPUP_MENU_CUT, "&Cut\tCTRL+X", "Cut out all selected items");
	cutItem->Enable(anything_selected);

	wxMenuItem* copyItem = Append( MAP_POPUP_MENU_COPY, "&Copy\tCTRL+C", "Copy all selected items");
	copyItem->Enable(anything_selected);

	wxMenuItem* copyPositionItem = Append( MAP_POPUP_MENU_COPY_POSITION, "&Copy Position", "Copy the position as a lua table");
	copyPositionItem->Enable(true);

	wxMenuItem* pasteItem = Append( MAP_POPUP_MENU_PASTE, "&Paste\tCTRL+V", "Paste items in the copybuffer here");
	pasteItem->Enable(editor.copybuffer.canPaste());

	wxMenuItem* deleteItem = Append( MAP_POPUP_MENU_DELETE, "&Delete\tDEL", "Removes all seleceted items");
	deleteItem->Enable(anything_selected);

	if(anything_selected) {
		if(editor.getSelection().size() == 1) {
			Tile* tile = editor.getSelection().getSelectedTile();
			ItemVector selected_items = tile->getSelectedItems();

			bool hasWall = false;
			bool hasCarpet = false;
			bool hasTable = false;
			Item* topItem = nullptr;
			Item* topSelectedItem = (selected_items.size() == 1 ? selected_items.back() : nullptr);
			Creature* topCreature = tile->creature;
			Spawn* topSpawn = tile->spawn;

			for(auto *item : tile->items) {
				if(item->isWall()) {
					Brush* wb = item->getWallBrush();
					if(wb && wb->visibleInPalette()) hasWall = true;
				}
				if(item->isTable()) {
					Brush* tb = item->getTableBrush();
					if(tb && tb->visibleInPalette()) hasTable = true;
				}
				if(item->isCarpet()) {
					Brush* cb = item->getCarpetBrush();
					if(cb && cb->visibleInPalette()) hasCarpet = true;
				}
				if(item->isSelected()) {
					topItem = item;
				}
			}
			if(!topItem) {
				topItem = tile->ground;
			}

 			AppendSeparator();

			if(topSelectedItem) {
				Append(MAP_POPUP_MENU_COPY_SERVER_ID, "Copy Item Server Id", "Copy the server id of this item");
				Append(MAP_POPUP_MENU_COPY_CLIENT_ID, "Copy Item Client Id", "Copy the client id of this item");
				Append(MAP_POPUP_MENU_COPY_NAME, "Copy Item Name", "Copy the name of this item");
				Append(MAP_POPUP_MENU_REMOVE_ITEMS, "Remove this item", "Remove all this item in the map");
				AppendSeparator();
				Append(MAP_POPUP_MENU_APPLY_REPLACE_BOX1, "Apply to Replace Box 1", "Apply this item to Replace Box 1");
				Append(MAP_POPUP_MENU_APPLY_REPLACE_BOX2, "Apply to Replace Box 2", "Apply this item to Replace Box 2 (With)");
				AppendSeparator();
			}

			if(topSelectedItem || topCreature || topItem) {
				Teleport* teleport = dynamic_cast<Teleport*>(topSelectedItem);
				if(topSelectedItem && (topSelectedItem->isBrushDoor() || topSelectedItem->isRoteable() || teleport)) {

					if(topSelectedItem->isRoteable())
						Append(MAP_POPUP_MENU_ROTATE, "&Rotate item", "Rotate this item");

					if(teleport) {
						bool enabled = teleport->hasDestination();
						wxMenuItem* goto_menu = Append(MAP_POPUP_MENU_GOTO, "&Go To Destination", "Go to the destination of this teleport");
						goto_menu->Enable(enabled);
						wxMenuItem* dest_menu = Append(MAP_POPUP_MENU_COPY_DESTINATION, "Copy &Destination", "Copy the destination of this teleport");
						dest_menu->Enable(enabled);
						AppendSeparator();
					}

					if(topSelectedItem->isDoor())
					{
						if(topSelectedItem->isOpen()) {
							Append(MAP_POPUP_MENU_SWITCH_DOOR, "&Close door", "Close this door");
						} else {
							Append(MAP_POPUP_MENU_SWITCH_DOOR, "&Open door", "Open this door");
						}
						AppendSeparator();
					}
				}

				if(topCreature)
					Append( MAP_POPUP_MENU_SELECT_CREATURE_BRUSH, "Select Creature", "Uses the current creature as a creature brush");

				if(topSpawn)
					Append( MAP_POPUP_MENU_SELECT_SPAWN_BRUSH, "Select Spawn", "Select the spawn brush");

				Append( MAP_POPUP_MENU_SELECT_RAW_BRUSH, "Select RAW", "Uses the top item as a RAW brush");

				if(hasWall)
					Append( MAP_POPUP_MENU_SELECT_WALL_BRUSH, "Select Wallbrush", "Uses the current item as a wallbrush");

				if(hasCarpet) {
					Append( MAP_POPUP_MENU_SELECT_CARPET_BRUSH, "Select Carpetbrush", "Uses the current item as a carpetbrush");
					// Check if carpet brush has source file for toggle option
					Item* carpet = tile->getCarpet();
					if(carpet) {
						Brush* carpetBrush = carpet->getCarpetBrush();
						if(carpetBrush && carpetBrush->hasSourceFile()) {
							Append( MAP_POPUP_MENU_TOGGLE_CARPET_ACTIVATED, "Deactivate Carpetbrush", "Deactivate this carpet brush in XML file");
						}
					}
				}

				if(hasTable)
					Append( MAP_POPUP_MENU_SELECT_TABLE_BRUSH, "Select Tablebrush", "Uses the current item as a tablebrush");

				if(topSelectedItem && topSelectedItem->getDoodadBrush() && topSelectedItem->getDoodadBrush()->visibleInPalette()) {
					Append( MAP_POPUP_MENU_SELECT_DOODAD_BRUSH, "Select Doodadbrush", "Use this doodad brush");
					// Check if doodad brush has source file for toggle option
					Brush* doodadBrush = topSelectedItem->getDoodadBrush();
					if(doodadBrush && doodadBrush->hasSourceFile()) {
						Append( MAP_POPUP_MENU_TOGGLE_DOODAD_ACTIVATED, "Deactivate Doodadbrush", "Deactivate this doodad brush in XML file");
					}
				}

				if(topSelectedItem && topSelectedItem->isBrushDoor() && topSelectedItem->getDoorBrush())
					Append( MAP_POPUP_MENU_SELECT_DOOR_BRUSH, "Select Doorbrush", "Use this door brush");

				if(tile->hasGround() && tile->getGroundBrush() && tile->getGroundBrush()->visibleInPalette())
					Append( MAP_POPUP_MENU_SELECT_GROUND_BRUSH, "Select Groundbrush", "Uses the current item as a groundbrush");

				if(tile->isHouseTile())
					Append(MAP_POPUP_MENU_SELECT_HOUSE_BRUSH, "Select House", "Draw with the house on this tile.");

				AppendSeparator();
				Append( MAP_POPUP_MENU_PROPERTIES, "&Properties", "Properties for the current object");
			} else {

				if(topCreature)
					Append( MAP_POPUP_MENU_SELECT_CREATURE_BRUSH, "Select Creature", "Uses the current creature as a creature brush");

				if(topSpawn)
					Append( MAP_POPUP_MENU_SELECT_SPAWN_BRUSH, "Select Spawn", "Select the spawn brush");

				Append( MAP_POPUP_MENU_SELECT_RAW_BRUSH, "Select RAW", "Uses the top item as a RAW brush");
				if(hasWall) {
					Append( MAP_POPUP_MENU_SELECT_WALL_BRUSH, "Select Wallbrush", "Uses the current item as a wallbrush");
				}
				if(tile->hasGround() && tile->getGroundBrush() && tile->getGroundBrush()->visibleInPalette()) {
					Append( MAP_POPUP_MENU_SELECT_GROUND_BRUSH, "Select Groundbrush", "Uses the current tile as a groundbrush");
				}

				if(tile->isHouseTile()) {
					Append(MAP_POPUP_MENU_SELECT_HOUSE_BRUSH, "Select House", "Draw with the house on this tile.");
				}

				if(tile->hasGround() || topCreature || topSpawn) {
					AppendSeparator();
					Append( MAP_POPUP_MENU_PROPERTIES, "&Properties", "Properties for the current object");
				}
			}

			AppendSeparator();

			wxMenuItem* browseTile = Append(MAP_POPUP_MENU_BROWSE_TILE, "Browse Field", "Navigate from tile items");
			browseTile->Enable(anything_selected);

			AppendSeparator();
			Append(MAP_POPUP_MENU_ADD_AREA_DECORATION_RULE,
			       "Add area decoration rule",
			       "Create an area decoration rule from the selected tiles");
		} else {
			AppendSeparator();
			wxMenu* rotate_menu = new wxMenu();
			rotate_menu->Append(MAP_POPUP_MENU_ROTATE_SELECTION_CW, "Rotate selection clockwise", "Rotate the selection 90 degrees clockwise");
			rotate_menu->Append(MAP_POPUP_MENU_ROTATE_SELECTION_CCW, "Rotate selection counterclockwise", "Rotate the selection 90 degrees counterclockwise");
			rotate_menu->Append(MAP_POPUP_MENU_ROTATE_SELECTION_180, "Rotate selection 180", "Rotate the selection 180 degrees");
			AppendSubMenu(rotate_menu, "Rotate selection");

			AppendSeparator();
			Append(MAP_POPUP_MENU_ADD_AREA_DECORATION_RULE,
			       "Add area decoration rule",
			       "Create an area decoration rule from the selected tiles");
		}
	}

	Theme::ApplyMenu(this);
}

void MapCanvas::getTilesToDraw(int mouse_map_x, int mouse_map_y, int floor, PositionVector* tilestodraw, PositionVector* tilestoborder, bool fill /*= false*/)
{
	if(fill) {
		Brush* brush = g_gui.GetCurrentBrush();
		if(!brush || !brush->isGround()) {
			return;
		}

		GroundBrush* newBrush = brush->asGround();
		Position position(mouse_map_x, mouse_map_y, floor);

		Tile* tile = editor.getMap().getTile(position);
		GroundBrush* oldBrush = nullptr;
		if(tile) {
			oldBrush = tile->getGroundBrush();
		}

		if(oldBrush && oldBrush->getID() == newBrush->getID()) {
			return;
		}

		if((tile && tile->ground && !oldBrush) || (!tile && oldBrush)) {
			return;
		}

		if(tile && oldBrush) {
			GroundBrush* groundBrush = tile->getGroundBrush();
			if(!groundBrush || groundBrush->getID() != oldBrush->getID()) {
				return;
			}
		}

		std::fill(std::begin(processed), std::end(processed), false);
		floodFill(&editor.getMap(), position, BLOCK_SIZE/2, BLOCK_SIZE/2, oldBrush, tilestodraw);

	} else {
		for(int y = -g_gui.GetBrushSize() - 1; y <= g_gui.GetBrushSize() + 1; y++) {
			for(int x = -g_gui.GetBrushSize() - 1; x <= g_gui.GetBrushSize() + 1; x++) {
				if(g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE) {
					if(x >= -g_gui.GetBrushSize() && x <= g_gui.GetBrushSize() && y >= -g_gui.GetBrushSize() && y <= g_gui.GetBrushSize()) {
						if(tilestodraw)
							tilestodraw->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
					}
					if(std::abs(x) - g_gui.GetBrushSize() < 2 && std::abs(y) - g_gui.GetBrushSize() < 2) {
						if(tilestoborder)
							tilestoborder->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
					}
				} else if(g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE) {
					double distance = sqrt(double(x*x) + double(y*y));
					if(distance < g_gui.GetBrushSize() + 0.005) {
						if(tilestodraw)
							tilestodraw->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
					}
					if(std::abs(distance - g_gui.GetBrushSize()) < 1.5) {
						if(tilestoborder)
							tilestoborder->push_back(Position(mouse_map_x + x, mouse_map_y + y, floor));
					}
				}
			}
		}
	}
}

bool MapCanvas::floodFill(Map *map, const Position& center, int x, int y, GroundBrush* brush, PositionVector* positions)
{
	if(x < 0 || y < 0 || x > BLOCK_SIZE || y > BLOCK_SIZE) {
		return false;
	}

	processed[getFillIndex(x, y)] = true;

	int px = (center.x + x) - (BLOCK_SIZE/2);
	int py = (center.y + y) - (BLOCK_SIZE/2);
	if(px <= 0 || py <= 0 || px >= map->getWidth() || py >= map->getHeight()) {
		return false;
	}

	Tile* tile = map->getTile(px, py, center.z);
	if((tile && tile->ground && !brush) || (!tile && brush)) {
		return false;
	}

	if(tile && brush) {
		GroundBrush* groundBrush = tile->getGroundBrush();
		if(!groundBrush || groundBrush->getID() != brush->getID()) {
			return false;
		}
	}

	positions->push_back(Position(px, py, center.z));

	bool deny = false;
	if(!processed[getFillIndex(x-1, y)]) {
		deny = floodFill(map, center, x-1, y, brush, positions);
	}

	if(!deny && !processed[getFillIndex(x, y-1)]) {
		deny = floodFill(map, center, x, y-1, brush, positions);
	}

	if(!deny && !processed[getFillIndex(x+1, y)]) {
		deny = floodFill(map, center, x+1, y, brush, positions);
	}

	if(!deny && !processed[getFillIndex(x, y+1)]) {
		deny = floodFill(map, center, x, y+1, brush, positions);
	}

	return deny;
}

// ============================================================================
// AnimationTimer

AnimationTimer::AnimationTimer(MapCanvas *canvas) : wxTimer(),
	map_canvas(canvas),
	started(false),
	needs_refresh(true),
	idle_frames(0)
{
	////
};

void AnimationTimer::Notify()
{
	// Always refresh but track idle frames for potential future optimization
	// We keep refreshing to maintain FPS display updates
	if(needs_refresh) {
		idle_frames = 0;
		needs_refresh = false;
	} else {
		++idle_frames;
	}

	// Refresh at lower rate when idle (every 4th frame = ~15fps) to save CPU
	// This still provides smooth FPS display while reducing CPU usage when idle
	if(idle_frames < 60 || (idle_frames % 4) == 0) {
		map_canvas->Refresh();
	}
};

void AnimationTimer::Start()
{
	if(!started) {
		started = true;
		needs_refresh = true;
		idle_frames = 0;
		wxTimer::Start(16); // ~60 FPS refresh rate for real-time FPS display
	}
};

void AnimationTimer::Stop()
{
	if(started) {
		started = false;
		wxTimer::Stop();
	}
};
