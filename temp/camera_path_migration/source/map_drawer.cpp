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

#ifdef __APPLE__
	#include <GLUT/glut.h>
#else
	#include <GL/glut.h>
#endif

#include "editor.h"
#include "ground_brush.h"
#include "gui.h"
#include "sprites.h"
#include "map_drawer.h"
#include "map_display.h"
#include "copybuffer.h"
#include "live_socket.h"
#include "graphics.h"
#include "complexitem.h"
#include "camera_path.h"
#include "npc_path.h"
#include "structure_manager_window.h"

#include "doodad_brush.h"
#include "creature_brush.h"
#include "creatures.h"
#include "house_exit_brush.h"
#include "house_brush.h"
#include "spawn_brush.h"
#include "wall_brush.h"
#include "carpet_brush.h"
#include "raw_brush.h"
#include "table_brush.h"
#include "waypoint_brush.h"
#include "light_drawer.h"
#include "creature_walk_animator.h"
#include <cmath>

DrawingOptions::DrawingOptions()
{
	SetDefault();
}

void DrawingOptions::SetDefault()
{
	transparent_floors = false;
	transparent_items = false;
	transparent_ground_items = false;
	show_ingame_box = false;
	show_lights = false;
	show_tech_items = false;
	ingame = false;
	dragging = false;
	light_hour = 12;

	show_grid = 0;
	show_all_floors = true;
	show_creatures = true;
	show_creature_idle_animation = false;
	show_spawns = true;
	show_spawn_creatureslist = true;
	show_spawn_overlays = true;
	show_houses = true;
	show_shade = true;
	show_special_tiles = true;
	show_items = true;

	highlight_items = false;
	show_blocking = false;
	show_tooltips = false;
	show_as_minimap = false;
	show_only_colors = false;
	show_only_modified = false;
	show_preview = false;
	show_hooks = false;
	show_pickupables = false;
	show_moveables = false;
	show_only_grounds = false;
	show_camera_paths = false;
	show_npc_paths = false;
	show_creature_wander_radius = false;
	animate_creature_walk = false;
	show_selected_tile_indicator = false;
	hide_items_when_zoomed = true;
	full_detail_zoom_out = false;
	custom_client_box = false;
	client_box_width = rme::ClientMapWidth;
	client_box_height = rme::ClientMapHeight;
	client_box_offset_x = 0;
	client_box_offset_y = 2;
}

void DrawingOptions::SetIngame()
{
	transparent_floors = false;
	transparent_items = false;
	transparent_ground_items = false;
	show_ingame_box = false;
	show_lights = false;
	show_tech_items = true;
	ingame = true;
	dragging = false;
	light_hour = 12;

	show_grid = 0;
	show_all_floors = true;
	show_creatures = true;
	show_creature_idle_animation = false;
	show_spawns = false;
	show_spawn_creatureslist = false;
	show_spawn_overlays = false;
	show_houses = false;
	show_shade = false;
	show_special_tiles = false;
	show_items = true;

	highlight_items = false;
	show_blocking = false;
	show_tooltips = false;
	show_as_minimap = false;
	show_only_colors = false;
	show_only_modified = false;
	show_preview = false;
	show_hooks = false;
	show_pickupables = false;
	show_moveables = false;
	show_only_grounds = false;
	show_camera_paths = false;
	show_npc_paths = false;
	show_creature_wander_radius = false;
	animate_creature_walk = false;
	show_selected_tile_indicator = false;
	hide_items_when_zoomed = false;
	full_detail_zoom_out = false;
	custom_client_box = false;
	client_box_width = rme::ClientMapWidth;
	client_box_height = rme::ClientMapHeight;
	client_box_offset_x = 0;
	client_box_offset_y = 2;
}

void DrawingOptions::LoadFromSettings()
{
	// Load all settings at once to avoid repeated lookups per frame
	transparent_floors = g_settings.getBoolean(Config::TRANSPARENT_FLOORS);
	transparent_items = g_settings.getBoolean(Config::TRANSPARENT_ITEMS);
	transparent_ground_items = g_settings.getBoolean(Config::TRANSPARENT_GROUND_ITEMS);
	show_ingame_box = g_settings.getBoolean(Config::SHOW_INGAME_BOX);
	show_lights = g_settings.getBoolean(Config::SHOW_LIGHTS);
	show_tech_items = g_settings.getBoolean(Config::SHOW_TECHNICAL_ITEMS);
	show_grid = g_settings.getInteger(Config::SHOW_GRID);
	ingame = !g_settings.getBoolean(Config::SHOW_EXTRA);
	show_all_floors = g_settings.getBoolean(Config::SHOW_ALL_FLOORS);
	show_creatures = g_settings.getBoolean(Config::SHOW_CREATURES);
	show_creature_idle_animation = g_settings.getBoolean(Config::SHOW_CREATURE_IDLE_ANIMATION);
	show_spawns = g_settings.getBoolean(Config::SHOW_SPAWNS);
	show_spawn_creatureslist = g_settings.getBoolean(Config::SHOW_SPAWN_CREATURESLIST);
	show_spawn_overlays = g_settings.getBoolean(Config::SHOW_SPAWN_OVERLAYS);
	show_houses = g_settings.getBoolean(Config::SHOW_HOUSES);
	show_shade = g_settings.getBoolean(Config::SHOW_SHADE);
	show_special_tiles = g_settings.getBoolean(Config::SHOW_SPECIAL_TILES);
	show_items = g_settings.getBoolean(Config::SHOW_ITEMS);
	highlight_items = g_settings.getBoolean(Config::HIGHLIGHT_ITEMS);
	show_blocking = g_settings.getBoolean(Config::SHOW_BLOCKING);
	show_tooltips = g_settings.getBoolean(Config::SHOW_TOOLTIPS);
	show_as_minimap = g_settings.getBoolean(Config::SHOW_AS_MINIMAP);
	show_only_colors = g_settings.getBoolean(Config::SHOW_ONLY_TILEFLAGS);
	show_only_modified = g_settings.getBoolean(Config::SHOW_ONLY_MODIFIED_TILES);
	show_preview = g_settings.getBoolean(Config::SHOW_PREVIEW);
	show_hooks = g_settings.getBoolean(Config::SHOW_WALL_HOOKS);
	show_pickupables = g_settings.getBoolean(Config::SHOW_PICKUPABLES);
	show_moveables = g_settings.getBoolean(Config::SHOW_MOVEABLES);
	show_wall_borders = g_settings.getBoolean(Config::SHOW_WALL_BORDERS);
	show_mountain_overlay = g_settings.getBoolean(Config::SHOW_MOUNTAIN_OVERLAY);
	show_stair_direction = g_settings.getBoolean(Config::SHOW_STAIR_DIRECTION);
	show_camera_paths = g_settings.getBoolean(Config::SHOW_CAMERA_PATHS);
	show_npc_paths = g_settings.getBoolean(Config::SHOW_NPC_PATHS);
	show_creature_wander_radius = g_settings.getBoolean(Config::SHOW_CREATURE_WANDER_RADIUS);
	animate_creature_walk = g_settings.getBoolean(Config::ANIMATE_CREATURE_WALK);
	show_only_grounds = g_settings.getBoolean(Config::SHOW_ONLY_GROUNDS);
	show_chunk_boundaries = g_settings.getBoolean(Config::SHOW_CHUNK_BOUNDARIES);
	show_selected_tile_indicator = g_settings.getBoolean(Config::SELECTED_TILE_INDICATOR);
	hide_items_when_zoomed = g_settings.getBoolean(Config::HIDE_ITEMS_WHEN_ZOOMED);
	full_detail_zoom_out = g_settings.getBoolean(Config::FULL_DETAIL_ZOOM_OUT);
	custom_client_box = g_settings.getBoolean(Config::CUSTOM_CLIENT_BOX);
	client_box_width = g_settings.getInteger(Config::CLIENT_BOX_WIDTH);
	client_box_height = g_settings.getInteger(Config::CLIENT_BOX_HEIGHT);
	client_box_offset_x = g_settings.getInteger(Config::CLIENT_BOX_OFFSET_X);
	client_box_offset_y = g_settings.getInteger(Config::CLIENT_BOX_OFFSET_Y);
	light_hour = g_settings.getInteger(Config::LIGHT_HOUR);
}

bool DrawingOptions::isOnlyColors() const noexcept
{
	return show_as_minimap || show_only_colors;
}

bool DrawingOptions::isTileIndicators() const noexcept
{
	if(isOnlyColors())
		return false;
	return show_pickupables || show_moveables || show_houses || show_spawns;
}

bool DrawingOptions::isTooltips() const noexcept
{
	return show_tooltips && !isOnlyColors();
}

bool DrawingOptions::isDrawLight() const noexcept
{
	return show_lights;
}

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr uint8_t kDayBrightness = 240;
constexpr uint8_t kNightBrightness = 35;

double CatmullRomValue(double p0, double p1, double p2, double p3, double t)
{
	const double t2 = t * t;
	const double t3 = t2 * t;
	return 0.5 * ((2.0 * p1) +
		(-p0 + p2) * t +
		(2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
		(-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

uint8_t CalculateAmbientBrightness(int hour)
{
	if(hour < 0) {
		hour = 0;
	}
	hour %= 24;
	double radians = (static_cast<double>(hour) / 24.0) * 2.0 * kPi - kPi;
	double normalized = (std::cos(radians) + 1.0) * 0.5;
	double value = static_cast<double>(kNightBrightness) + normalized * (kDayBrightness - kNightBrightness);
	if(value < 0.0) {
		return 0;
	}
	if(value > 255.0) {
		return 255;
	}
	return static_cast<uint8_t>(value);
}
}

MapDrawer::MapDrawer(MapCanvas* canvas) : canvas(canvas), editor(canvas->editor),
	frame_count(0), current_fps(0.0), tiles_rendered(0)
{
	light_drawer = std::make_shared<LightDrawer>();
	selection_indicator_timer.Start();
	fps_timer.Start();
}

MapDrawer::~MapDrawer()
{
	Release();
}

void MapDrawer::SetupVars()
{
	canvas->MouseToMap(&mouse_map_x, &mouse_map_y);
	canvas->GetViewBox(&view_scroll_x, &view_scroll_y, &screensize_x, &screensize_y);

	dragging = canvas->dragging;
	dragging_draw = canvas->dragging_draw;

	zoom = static_cast<float>(canvas->GetZoom());
	tile_size = int(rme::TileSize / zoom); // after zoom
	floor = canvas->GetFloor();

	if(options.show_all_floors) {
		if(floor < 8)
			start_z = rme::MapGroundLayer;
		else
			start_z = std::min(rme::MapMaxLayer, floor + 2);
	}
	else
		start_z = floor;

	end_z = floor;
	superend_z = (floor > rme::MapGroundLayer ? 8 : 0);

	start_x = view_scroll_x / rme::TileSize;
	start_y = view_scroll_y / rme::TileSize;

	if(floor > rme::MapGroundLayer) {
		start_x -= 2;
		start_y -= 2;
	}

	end_x = start_x + screensize_x / tile_size + 2;
	end_y = start_y + screensize_y / tile_size + 2;
}

void MapDrawer::SetupGL()
{
	glViewport(0, 0, screensize_x, screensize_y);

	// Enable 2D mode
	int vPort[4];

	glGetIntegerv(GL_VIEWPORT, vPort);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, vPort[2]*zoom, vPort[3]*zoom, 0, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glTranslatef(0.375f, 0.375f, 0.0f);
}

void MapDrawer::Release()
{
	for(auto it = tooltips.begin(); it != tooltips.end(); ++it) {
		delete *it;
	}
	tooltips.clear();

	if(light_drawer) {
		light_drawer->clear();
	}

	// Disable 2D mode
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

int MapDrawer::GetLODLevel() const
{
	if(options.full_detail_zoom_out) {
		return 0;
	}
	// LOD Level 0: Full detail (zoom <= 2.0)
	// LOD Level 1: Medium detail (2.0 < zoom <= 6.0)
	// LOD Level 2: Low detail (6.0 < zoom <= 10.0)
	// LOD Level 3: Minimal detail (zoom > 10.0)
	
	if(zoom <= 2.0f) {
		return 0;
	} else if(zoom <= 6.0f) {
		return 1;
	} else if(zoom <= 10.0f) {
		return 2;
	} else {
		return 3;
	}
}

void MapDrawer::Draw()
{
	int lod_level = GetLODLevel();

	// Update creature walk animation state
	if(options.animate_creature_walk) {
		if(!g_creature_walk_animator.isEnabled()) {
			g_creature_walk_animator.setEnabled(true);
		}
		g_creature_walk_animator.update(wxGetLocalTimeMillis().GetLo());
	} else {
		if(g_creature_walk_animator.isEnabled()) {
			g_creature_walk_animator.setEnabled(false);
		}
	}

	DrawBackground();
	DrawMap();
	DrawFloorFadeOverlay();
	DrawSpawnOverlays();
	DrawWanderRadiusOverlays();
	if(options.show_mountain_overlay)
		DrawMountainOverlay();
	if(options.show_wall_borders)
		DrawWallBorderLines();
	DrawDraggingShadow();
	DrawHigherFloors();
	if(options.dragging)
		DrawSelectionBox();
	DrawLiveCursors();
	DrawBrush();
	DrawCursorTile();

	// Skip grid in high LOD levels
	if(options.show_grid && (zoom <= 10.f || options.full_detail_zoom_out) && lod_level < 3)
		DrawGrid();
	if(options.show_chunk_boundaries && (zoom <= 10.f || options.full_detail_zoom_out) && lod_level < 3)
		DrawChunkBoundaries();
	if(options.show_camera_paths)
		DrawCameraPaths();
	if(options.show_npc_paths)
		DrawNPCPaths();

	// Skip lights in medium/high LOD levels
	if(options.isDrawLight() && lod_level < 2)
		DrawLights();

	if(options.show_ingame_box)
		DrawIngameBox();

	// Skip tooltips in high LOD levels
	if(options.isTooltips() && lod_level < 3)
		DrawTooltips();

	// Draw stair directions on top of everything for visibility
	if(options.show_stair_direction)
		DrawStairDirections();

	DrawStatsOverlay();

	// Request continuous refresh for creature walk animations
	if(options.animate_creature_walk) {
		canvas->animation_timer->RequestRefresh();
	}
}

void MapDrawer::DrawBackground()
{
	// Black Background
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glLoadIdentity();

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);

	//glAlphaFunc(GL_GEQUAL, 0.9f);
	//glEnable(GL_ALPHA_TEST);
}

inline int getFloorAdjustment(int floor)
{
	if(floor > rme::MapGroundLayer) // Underground
		return 0; // No adjustment
	else
		return rme::TileSize * (rme::MapGroundLayer - floor);
}

void MapDrawer::DrawShade(int map_z)
{
	if(map_z == end_z && start_z != end_z) {
		bool only_colors = options.isOnlyColors();
		if(!only_colors)
			glDisable(GL_TEXTURE_2D);

		float x = screensize_x * zoom;
		float y = screensize_y * zoom;
		glColor4ub(0, 0, 0, 128);
		glBegin(GL_QUADS);
			glVertex2f(0, y);
			glVertex2f(x, y);
			glVertex2f(x,0);
			glVertex2f(0,0);
		glEnd();

		if(!only_colors)
			glEnable(GL_TEXTURE_2D);
	}
}

void MapDrawer::DrawMap()
{
	tiles_rendered = 0; // Reset tile counter
	bool live_client = editor.IsLiveClient();
	spawn_overlays.clear();
	wander_overlays.clear();

	Brush* brush = g_gui.GetCurrentBrush();

	// The current house we're drawing
	current_house_id = 0;
	if(brush) {
		if(brush->isHouse())
			current_house_id = brush->asHouse()->getHouseID();
		else if(brush->isHouseExit())
			current_house_id = brush->asHouseExit()->getHouseID();
	}

	bool only_colors = options.isOnlyColors();
	bool tile_indicators = options.isTileIndicators();

	for(int map_z = start_z; map_z >= superend_z; map_z--) {
		if(options.show_shade) {
			DrawShade(map_z);
		}

		if(map_z >= end_z) {
			if(!only_colors)
				glEnable(GL_TEXTURE_2D);

			int nd_start_x = start_x & ~3;
			int nd_start_y = start_y & ~3;
			int nd_end_x = (end_x & ~3) + 4;
			int nd_end_y = (end_y & ~3) + 4;

			for(int nd_map_x = nd_start_x; nd_map_x <= nd_end_x; nd_map_x += 4) {
				for(int nd_map_y = nd_start_y; nd_map_y <= nd_end_y; nd_map_y += 4) {
					QTreeNode* nd = editor.getMap().getLeaf(nd_map_x, nd_map_y);
					if(!nd) {
						if(!live_client)
							continue;
						nd = editor.getMap().createLeaf(nd_map_x, nd_map_y);
						nd->setVisible(false, false);
					}

					if(!live_client || nd->isVisible(map_z > rme::MapGroundLayer)) {
						for(int map_x = 0; map_x < 4; ++map_x) {
							for(int map_y = 0; map_y < 4; ++map_y) {
								TileLocation* location = nd->getTile(map_x, map_y, map_z);
								DrawTile(location);
								if(location) tiles_rendered++;
								AddLight(location);
							}
						}
						if(tile_indicators) {
							for(int map_x = 0; map_x < 4; ++map_x) {
								for(int map_y = 0; map_y < 4; ++map_y) {
									DrawTileIndicators(nd->getTile(map_x, map_y, map_z));
								}
							}
						}
					} else {
						if(!nd->isRequested(map_z > rme::MapGroundLayer)) {
							// Request the node
							editor.QueryNode(nd_map_x, nd_map_y, map_z > rme::MapGroundLayer);
							nd->setRequested(map_z > rme::MapGroundLayer, true);
						}
						int cy = (nd_map_y) * rme::TileSize - view_scroll_y - getFloorAdjustment(floor);
						int cx = (nd_map_x) * rme::TileSize - view_scroll_x - getFloorAdjustment(floor);

						glColor4ub(255, 0, 255, 128);
						glBegin(GL_QUADS);
							glVertex2f(cx, cy + rme::TileSize * 4);
							glVertex2f(cx + rme::TileSize * 4, cy + rme::TileSize * 4);
							glVertex2f(cx + rme::TileSize * 4, cy);
							glVertex2f(cx,     cy);
						glEnd();
					}
				}
			}

			if(!only_colors)
				glDisable(GL_TEXTURE_2D);

			DrawPositionIndicator(map_z);
		}

		// Draws the doodad preview or the paste preview (or import preview)
		DrawSecondaryMap(map_z);
		DrawFixedSavePreview(map_z);

		--start_x;
		--start_y;
		++end_x;
		++end_y;
	}

	if(!only_colors)
		glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawSecondaryMap(int map_z)
{
	if(canvas && canvas->IsPreviewMode()) {
		return;
	}

	if(options.ingame)
		return;

	BaseMap* secondary_map = g_gui.secondary_map;
	bool autoborder_preview = false;
	if(!secondary_map &&
		g_settings.getBoolean(Config::SHOW_AUTOBORDER_PREVIEW) &&
		canvas->autoborder_preview_active) {
		secondary_map = canvas->autoborder_preview_map;
		autoborder_preview = true;
	}
	if(!secondary_map) return;

	Position normal_pos;
	Position to_pos(mouse_map_x, mouse_map_y, floor);

	if(canvas->isPasting()) {
		normal_pos = editor.copybuffer.getPosition();
	} else if(autoborder_preview) {
		normal_pos = to_pos;
	} else {
		Brush* brush = g_gui.GetCurrentBrush();
		if(brush && brush->isDoodad()) {
			normal_pos = Position(0x8000, 0x8000, 0x8);
		}
	}

	glEnable(GL_TEXTURE_2D);

	for(int map_x = start_x; map_x <= end_x; map_x++) {
		for(int map_y = start_y; map_y <= end_y; map_y++) {
			Position final_pos(map_x, map_y, map_z);
			Position pos = normal_pos + final_pos - to_pos;
			if(pos.z < 0 || pos.z >= rme::MapLayers) {
				continue;
			}

			Tile* tile = secondary_map->getTile(pos);
			if(!tile) continue;

			int draw_x, draw_y;
			getDrawPosition(final_pos, draw_x, draw_y);

			// Draw ground
			uint8_t r = 160, g = 160, b = 160;
			if(tile->ground) {
				if(options.show_blocking && tile->isBlocking()) {
					g = g/3*2;
					b = b/3*2;
				}
				if(options.show_houses && tile->isHouseTile()) {
					if(tile->getHouseID() == current_house_id) {
						r /= 2;
					} else {
						r /= 2;
						g /= 2;
					}
				} else if(options.show_special_tiles && tile->isPZ()) {
					r /= 2;
					b /= 2;
				}
				if(options.show_special_tiles && tile->getMapFlags() & TILESTATE_PVPZONE) {
					r = r/3*2;
					b = r/3*2;
				}
				if(options.show_special_tiles && tile->getMapFlags() & TILESTATE_NOLOGOUT) {
					b /= 2;
				}
				if(options.show_special_tiles && tile->getMapFlags() & TILESTATE_NOPVP) {
					g /= 2;
				}
				if(options.show_special_tiles && tile->getMapFlags() & TILESTATE_WORLDBOSS) {
                    r /= 2;
                    g /= 2;
                    b = 160;
                }
				BlitItem(draw_x, draw_y, tile, tile->ground, true, r, g, b, 160);
			}

			bool hidden = options.hide_items_when_zoomed && zoom > 10.f && !options.full_detail_zoom_out;

			// Draw items
			if(!hidden && !tile->items.empty()) {
				for(const Item* item : tile->items) {
					if(item->isBorder()) {
						BlitItem(draw_x, draw_y, tile, item, true, 160, r, g, b);
					} else {
						BlitItem(draw_x, draw_y, tile, item, true, 160, 160, 160, 160);
					}
				}
			}

			// Draw creature
			if(!hidden && options.show_creatures && tile->creature) {
				BlitCreature(draw_x, draw_y, tile->creature);
			}
		}
	}

	glDisable(GL_TEXTURE_2D);
}

void MapDrawer::DrawIngameBox()
{
	const bool use_custom_box = options.custom_client_box;
	const int map_width = std::max(1, use_custom_box ? options.client_box_width : rme::ClientMapWidth);
	const int map_height = std::max(1, use_custom_box ? options.client_box_height : rme::ClientMapHeight);
	const int offset_x = use_custom_box ? options.client_box_offset_x : 0;
	const int offset_y = use_custom_box ? options.client_box_offset_y : 2;

	int center_x = start_x + int(screensize_x * zoom / 64);
	int center_y = start_y + int(screensize_y * zoom / 64);

	int box_start_map_x = center_x + offset_x;
	int box_start_map_y = center_y + offset_y;
	int box_end_map_x = box_start_map_x + map_width;
	int box_end_map_y = box_start_map_y + map_height;

	int box_start_x = box_start_map_x * rme::TileSize - view_scroll_x;
	int box_start_y = box_start_map_y * rme::TileSize - view_scroll_y;
	int box_end_x = box_end_map_x * rme::TileSize - view_scroll_x;
	int box_end_y = box_end_map_y * rme::TileSize - view_scroll_y;

	static wxColor side_color(0, 0, 0, 150);

	glDisable(GL_TEXTURE_2D);

	// left side
	if(box_start_map_x >= start_x) {
		drawFilledRect(0, 0, box_start_x, screensize_y * zoom, side_color);
	}

	// right side
	if(box_end_map_x < end_x) {
		drawFilledRect(box_end_x, 0, screensize_x * zoom, screensize_y * zoom, side_color);
	}

	// top side
	if(box_start_map_y >= start_y) {
		drawFilledRect(box_start_x, 0, box_end_x-box_start_x, box_start_y, side_color);
	}

	// bottom side
	if(box_end_map_y < end_y) {
		drawFilledRect(box_start_x, box_end_y, box_end_x-box_start_x, screensize_y * zoom, side_color);
	}

	// hidden tiles
	drawRect(box_start_x, box_start_y, box_end_x-box_start_x, box_end_y-box_start_y, *wxRED);

	// visible tiles
	const int visible_start_x = box_start_x + rme::TileSize;
	const int visible_start_y = box_start_y + rme::TileSize;
	const int visible_end_x = box_end_x - 2 * rme::TileSize;
	const int visible_end_y = box_end_y - 2 * rme::TileSize;

	if(visible_end_x > visible_start_x && visible_end_y > visible_start_y) {
		drawRect(visible_start_x, visible_start_y, visible_end_x-visible_start_x, visible_end_y-visible_start_y, *wxGREEN);

		// player position
		const int player_offset_x = std::max(0, (map_width / 2) - 2);
		const int player_offset_y = std::max(0, (map_height / 2) - 2);
		int player_start_x = visible_start_x + player_offset_x * rme::TileSize;
		int player_start_y = visible_start_y + player_offset_y * rme::TileSize;
		int player_end_x = player_start_x + rme::TileSize;
		int player_end_y = player_start_y + rme::TileSize;
		drawRect(player_start_x, player_start_y, player_end_x-player_start_x, player_end_y-player_start_y, *wxGREEN);
	}

	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawGrid()
{
	glDisable(GL_TEXTURE_2D);
	glColor4ub(255, 255, 255, 128);
	glBegin(GL_LINES); 

	for(int y = start_y; y < end_y; ++y) {
		int py = y * rme::TileSize - view_scroll_y;
		glVertex2f(start_x * rme::TileSize - view_scroll_x, py);
		glVertex2f(end_x * rme::TileSize - view_scroll_x, py);
	}

	for(int x = start_x; x < end_x; ++x) {
		int px = x * rme::TileSize - view_scroll_x;
		glVertex2f(px, start_y * rme::TileSize - view_scroll_y);
		glVertex2f(px, end_y * rme::TileSize - view_scroll_y);
	}

	glEnd();
	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawChunkBoundaries()
{
	const int CHUNK_TILES = 256;

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_LINE_STIPPLE);
	glLineStipple(2, 0xAAAA);
	glLineWidth(2.0f);
	glColor4ub(0, 200, 255, 180);
	glBegin(GL_LINES);

	// Align to chunk boundaries
	int cx_start = (start_x / CHUNK_TILES) * CHUNK_TILES;
	int cy_start = (start_y / CHUNK_TILES) * CHUNK_TILES;

	int sx_min = start_x * rme::TileSize - view_scroll_x;
	int sx_max = end_x * rme::TileSize - view_scroll_x;
	int sy_min = start_y * rme::TileSize - view_scroll_y;
	int sy_max = end_y * rme::TileSize - view_scroll_y;

	// Vertical lines at chunk boundaries
	for(int cx = cx_start; cx <= end_x; cx += CHUNK_TILES) {
		int sx = cx * rme::TileSize - view_scroll_x;
		glVertex2f(sx, sy_min);
		glVertex2f(sx, sy_max);
	}

	// Horizontal lines at chunk boundaries
	for(int cy = cy_start; cy <= end_y; cy += CHUNK_TILES) {
		int sy = cy * rme::TileSize - view_scroll_y;
		glVertex2f(sx_min, sy);
		glVertex2f(sx_max, sy);
	}

	glEnd();
	glLineWidth(1.0f);
	glDisable(GL_LINE_STIPPLE);
	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawCameraPaths()
{
	if(options.ingame) {
		return;
	}

	const CameraPaths& cameraPaths = editor.getMap().camera_paths;
	const std::vector<CameraPath>& paths = cameraPaths.getPaths();
	if(paths.empty()) {
		return;
	}

	const std::string& activePathName = cameraPaths.getActivePathName();
	const int activeKeyframe = cameraPaths.getActiveKeyframe();

	auto toScreen = [&](double mapX, double mapY, int mapZ, float& sx, float& sy) {
		int offset = 0;
		if(mapZ <= rme::MapGroundLayer) {
			offset = (rme::MapGroundLayer - mapZ) * rme::TileSize;
		} else {
			offset = rme::TileSize * (floor - mapZ);
		}
		sx = static_cast<float>((mapX * rme::TileSize) - view_scroll_x - offset);
		sy = static_cast<float>((mapY * rme::TileSize) - view_scroll_y - offset);
	};

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_LINE_SMOOTH);

	for(const CameraPath& path : paths) {
		if(path.keyframes.size() < 2) {
			continue;
		}

		const bool isActive = path.name == activePathName;
		const uint8_t alpha = isActive ? 220 : 120;
		glColor4ub(path.color.r, path.color.g, path.color.b, alpha);
		glLineWidth(isActive ? 2.0f : 1.0f);

		const size_t count = path.keyframes.size();
		const size_t segments = path.loop ? count : (count - 1);

		for(size_t seg = 0; seg < segments; ++seg) {
			const size_t next = path.loop ? (seg + 1) % count : (seg + 1);
			const CameraKeyframe& a = path.keyframes[seg];
			const CameraKeyframe& b = path.keyframes[next];
			if(a.pos.z != floor || b.pos.z != floor) {
				continue;
			}

			const size_t i0 = (seg == 0 && !path.loop) ? seg : (seg + count - 1) % count;
			const size_t i1 = seg;
			const size_t i2 = next;
			const size_t i3 = (seg + 2 >= count && !path.loop) ? (count - 1) : (seg + 2) % count;

			const CameraKeyframe& p0 = path.keyframes[i0];
			const CameraKeyframe& p1 = path.keyframes[i1];
			const CameraKeyframe& p2 = path.keyframes[i2];
			const CameraKeyframe& p3 = path.keyframes[i3];

			const int steps = 12;
			glBegin(GL_LINE_STRIP);
			for(int s = 0; s <= steps; ++s) {
				double t = static_cast<double>(s) / static_cast<double>(steps);
				double x = CatmullRomValue(p0.pos.x, p1.pos.x, p2.pos.x, p3.pos.x, t);
				double y = CatmullRomValue(p0.pos.y, p1.pos.y, p2.pos.y, p3.pos.y, t);
				float sx, sy;
				toScreen(x, y, p1.pos.z, sx, sy);
				glVertex2f(sx + rme::TileSize * 0.5f, sy + rme::TileSize * 0.5f);
			}
			glEnd();
		}
	}

	glLineWidth(1.0f);

	for(const CameraPath& path : paths) {
		const bool isActive = path.name == activePathName;
		const uint8_t alpha = isActive ? 230 : 160;
		const wxColor fill(path.color.r, path.color.g, path.color.b, alpha);
		const wxColor outline(0, 0, 0, 200);
		const wxColor activeOutline(255, 255, 255, 230);

		for(size_t i = 0; i < path.keyframes.size(); ++i) {
			const CameraKeyframe& key = path.keyframes[i];
			if(key.pos.z != floor) {
				continue;
			}

			float sx, sy;
			toScreen(key.pos.x, key.pos.y, key.pos.z, sx, sy);
			const float centerX = sx + rme::TileSize * 0.5f;
			const float centerY = sy + rme::TileSize * 0.5f;
			const int size = isActive && static_cast<int>(i) == activeKeyframe ? 10 : 8;
			const int half = size / 2;

			drawFilledRect(static_cast<int>(centerX) - half, static_cast<int>(centerY) - half, size, size, fill);
			drawRect(static_cast<int>(centerX) - half, static_cast<int>(centerY) - half, size, size,
				(isActive && static_cast<int>(i) == activeKeyframe) ? activeOutline : outline, 1);

			const std::string label = i2s(static_cast<int>(i + 1));
			int textWidth = 0;
			for(char c : label) {
				textWidth += glutBitmapWidth(GLUT_BITMAP_8_BY_13, c);
			}
			glColor4ub(0, 0, 0, 220);
			glRasterPos2f(centerX - (textWidth * 0.5f), centerY - 6.0f);
			for(char c : label) {
				glutBitmapCharacter(GLUT_BITMAP_8_BY_13, c);
			}
		}
	}

	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawNPCPaths()
{
	if(options.ingame) {
		return;
	}

	const NPCPaths& npcPaths = editor.getMap().npc_paths;
	const std::vector<NPCPath>& paths = npcPaths.getPaths();
	if(paths.empty()) {
		return;
	}

	const std::string& activePathName = npcPaths.getActivePathName();
	const int activeWaypointIdx = npcPaths.getActiveWaypoint();

	// Lambda to convert map coordinates to screen coordinates
	auto toScreen = [&](double mapX, double mapY, int mapZ, float& sx, float& sy) {
		int offset = 0;
		if(mapZ <= rme::MapGroundLayer) {
			offset = (rme::MapGroundLayer - mapZ) * rme::TileSize;
		} else {
			offset = rme::TileSize * (floor - mapZ);
		}
		sx = static_cast<float>((mapX * rme::TileSize) - view_scroll_x - offset);
		sy = static_cast<float>((mapY * rme::TileSize) - view_scroll_y - offset);
	};

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_LINE_SMOOTH);

	// Draw path lines (use linear interpolation since NPC paths are typically straight)
	for(const NPCPath& path : paths) {
		if(path.waypoints.size() < 2) {
			continue;
		}

		const bool isActive = path.name == activePathName;
		const uint8_t alpha = isActive ? 220 : 120;
		// Use a different color from camera paths (default is cyan-ish 80, 200, 255)
		// NPC paths use the path's color with slightly different rendering
		glColor4ub(path.color.r, path.color.g, path.color.b, alpha);
		glLineWidth(isActive ? 2.5f : 1.5f);

		const size_t count = path.waypoints.size();
		const size_t segments = path.loop ? count : (count - 1);

		for(size_t seg = 0; seg < segments; ++seg) {
			const size_t next = path.loop ? (seg + 1) % count : (seg + 1);
			const NPCWaypoint& a = path.waypoints[seg];
			const NPCWaypoint& b = path.waypoints[next];
			if(a.pos.z != floor || b.pos.z != floor) {
				continue;
			}

			float sx1, sy1, sx2, sy2;
			toScreen(a.pos.x, a.pos.y, a.pos.z, sx1, sy1);
			toScreen(b.pos.x, b.pos.y, b.pos.z, sx2, sy2);

			// Draw line between waypoints
			glBegin(GL_LINES);
			glVertex2f(sx1 + rme::TileSize * 0.5f, sy1 + rme::TileSize * 0.5f);
			glVertex2f(sx2 + rme::TileSize * 0.5f, sy2 + rme::TileSize * 0.5f);
			glEnd();

			// Draw direction arrow in the middle of the line
			float midX = (sx1 + sx2) * 0.5f + rme::TileSize * 0.5f;
			float midY = (sy1 + sy2) * 0.5f + rme::TileSize * 0.5f;
			float dx = sx2 - sx1;
			float dy = sy2 - sy1;
			float len = std::sqrt(dx * dx + dy * dy);
			if(len > 10.0f) {
				dx /= len;
				dy /= len;
				const float arrowSize = 6.0f;
				// Draw arrowhead
				glBegin(GL_TRIANGLES);
				glVertex2f(midX + dx * arrowSize, midY + dy * arrowSize);
				glVertex2f(midX - dy * arrowSize * 0.5f - dx * arrowSize * 0.5f, midY + dx * arrowSize * 0.5f - dy * arrowSize * 0.5f);
				glVertex2f(midX + dy * arrowSize * 0.5f - dx * arrowSize * 0.5f, midY - dx * arrowSize * 0.5f - dy * arrowSize * 0.5f);
				glEnd();
			}
		}
	}

	glLineWidth(1.0f);

	// Draw waypoint markers
	for(const NPCPath& path : paths) {
		const bool isActive = path.name == activePathName;
		const uint8_t alpha = isActive ? 230 : 160;
		const wxColor fill(path.color.r, path.color.g, path.color.b, alpha);
		const wxColor outline(0, 0, 0, 200);
		const wxColor activeOutline(255, 255, 0, 230);  // Yellow for active waypoint

		for(size_t i = 0; i < path.waypoints.size(); ++i) {
			const NPCWaypoint& waypoint = path.waypoints[i];
			if(waypoint.pos.z != floor) {
				continue;
			}

			float sx, sy;
			toScreen(waypoint.pos.x, waypoint.pos.y, waypoint.pos.z, sx, sy);
			const float centerX = sx + rme::TileSize * 0.5f;
			const float centerY = sy + rme::TileSize * 0.5f;
			const bool isActiveWaypoint = isActive && static_cast<int>(i) == activeWaypointIdx;
			const int size = isActiveWaypoint ? 12 : 9;
			const int half = size / 2;

			// Draw circle-ish marker (diamond shape for NPCs to distinguish from camera path squares)
			glColor4ub(fill.Red(), fill.Green(), fill.Blue(), alpha);
			glBegin(GL_QUADS);
			glVertex2f(centerX, centerY - half);      // top
			glVertex2f(centerX + half, centerY);      // right
			glVertex2f(centerX, centerY + half);      // bottom
			glVertex2f(centerX - half, centerY);      // left
			glEnd();

			// Draw outline
			const wxColor& outlineColor = isActiveWaypoint ? activeOutline : outline;
			glColor4ub(outlineColor.Red(), outlineColor.Green(), outlineColor.Blue(), outlineColor.Alpha());
			glLineWidth(isActiveWaypoint ? 2.0f : 1.0f);
			glBegin(GL_LINE_LOOP);
			glVertex2f(centerX, centerY - half);
			glVertex2f(centerX + half, centerY);
			glVertex2f(centerX, centerY + half);
			glVertex2f(centerX - half, centerY);
			glEnd();
			glLineWidth(1.0f);

			// Draw waypoint number
			const std::string label = i2s(static_cast<int>(i + 1));
			int textWidth = 0;
			for(char c : label) {
				textWidth += glutBitmapWidth(GLUT_BITMAP_8_BY_13, c);
			}
			glColor4ub(255, 255, 255, 220);
			glRasterPos2f(centerX - (textWidth * 0.5f), centerY - 8.0f);
			for(char c : label) {
				glutBitmapCharacter(GLUT_BITMAP_8_BY_13, c);
			}

			// Draw action icons for waypoints with actions
			float iconOffsetY = half + 4.0f;
			for(const NPCAction& action : waypoint.actions) {
				switch(action.type) {
					case NPCActionType::Speak: {
						// Draw speech bubble icon (small rounded rectangle)
						glColor4ub(255, 255, 255, 200);
						const float bubbleSize = 5.0f;
						drawFilledRect(
							static_cast<int>(centerX - bubbleSize),
							static_cast<int>(centerY + iconOffsetY),
							static_cast<int>(bubbleSize * 2),
							static_cast<int>(bubbleSize * 1.5f),
							wxColor(255, 255, 255, 200));
						// Bubble tail
						glBegin(GL_TRIANGLES);
						glVertex2f(centerX - 2.0f, centerY + iconOffsetY + bubbleSize * 1.5f);
						glVertex2f(centerX, centerY + iconOffsetY + bubbleSize * 2.0f);
						glVertex2f(centerX + 2.0f, centerY + iconOffsetY + bubbleSize * 1.5f);
						glEnd();
						iconOffsetY += bubbleSize * 2.5f;
						break;
					}
					case NPCActionType::Wait: {
						// Draw clock/timer icon (small circle)
						glColor4ub(255, 200, 100, 200);
						const float clockSize = 4.0f;
						glBegin(GL_TRIANGLE_FAN);
						glVertex2f(centerX, centerY + iconOffsetY + clockSize);
						for(int angle = 0; angle <= 360; angle += 30) {
							float rad = static_cast<float>(angle) * 3.14159f / 180.0f;
							glVertex2f(centerX + std::cos(rad) * clockSize, centerY + iconOffsetY + clockSize + std::sin(rad) * clockSize);
						}
						glEnd();
						iconOffsetY += clockSize * 2.5f;
						break;
					}
					case NPCActionType::FaceDirection: {
						// Draw direction arrow icon
						glColor4ub(100, 200, 255, 200);
						const float arrowSize = 4.0f;
						float dirX = 0, dirY = 0;
						switch(action.direction) {
							case 0: dirY = -1; break;  // North
							case 1: dirX = 1; break;   // East
							case 2: dirY = 1; break;   // South
							case 3: dirX = -1; break;  // West
						}
						glBegin(GL_TRIANGLES);
						glVertex2f(centerX + dirX * arrowSize, centerY + iconOffsetY + arrowSize + dirY * arrowSize);
						glVertex2f(centerX - dirY * arrowSize * 0.5f - dirX * arrowSize * 0.3f, centerY + iconOffsetY + arrowSize + dirX * arrowSize * 0.5f - dirY * arrowSize * 0.3f);
						glVertex2f(centerX + dirY * arrowSize * 0.5f - dirX * arrowSize * 0.3f, centerY + iconOffsetY + arrowSize - dirX * arrowSize * 0.5f - dirY * arrowSize * 0.3f);
						glEnd();
						iconOffsetY += arrowSize * 2.5f;
						break;
					}
					case NPCActionType::Emote: {
						// Draw star/emote icon
						glColor4ub(255, 255, 100, 200);
						const float starSize = 4.0f;
						glBegin(GL_TRIANGLES);
						// Simple star shape
						glVertex2f(centerX, centerY + iconOffsetY);
						glVertex2f(centerX - starSize * 0.4f, centerY + iconOffsetY + starSize);
						glVertex2f(centerX + starSize * 0.4f, centerY + iconOffsetY + starSize);
						glVertex2f(centerX, centerY + iconOffsetY + starSize * 1.5f);
						glVertex2f(centerX - starSize * 0.4f, centerY + iconOffsetY + starSize * 0.5f);
						glVertex2f(centerX + starSize * 0.4f, centerY + iconOffsetY + starSize * 0.5f);
						glEnd();
						iconOffsetY += starSize * 2.0f;
						break;
					}
					case NPCActionType::None:
					default:
						break;
				}
			}
		}
	}

	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawLights()
{
	if(!options.isDrawLight() || !light_drawer) {
		return;
	}

	const int width = end_x - start_x;
	const int height = end_y - start_y;
	if(width <= 0 || height <= 0) {
		return;
	}

	const uint8_t brightness = CalculateAmbientBrightness(options.light_hour);
	light_drawer->setGlobalLightColor(brightness);
	light_drawer->draw(start_x, start_y, width, height, view_scroll_x, view_scroll_y);
}

void MapDrawer::DrawDraggingShadow()
{
	if(!dragging || options.ingame || editor.getSelection().isBusy())
		return;

	glEnable(GL_TEXTURE_2D);

	for(Tile* tile : editor.getSelection()) {
		int move_z = canvas->drag_start_z - floor;
		int move_x = canvas->drag_start_x - mouse_map_x;
		int move_y = canvas->drag_start_y - mouse_map_y;

		if(move_x == 0 && move_y == 0 && move_z == 0)
			continue;

		const Position& position = tile->getPosition();
		int pos_z = position.z - move_z;
		if(pos_z < 0 || pos_z >= rme::MapLayers) {
			continue;
		}

		int pos_x = position.x - move_x;
		int pos_y = position.y - move_y;

		// On screen and dragging?
		if(pos_x+2 > start_x && pos_x < end_x && pos_y+2 > start_y && pos_y < end_y) {
			Position pos(pos_x, pos_y, pos_z);
			int draw_x, draw_y;
			getDrawPosition(pos, draw_x, draw_y);

			ItemVector items = tile->getSelectedItems();
			Tile* dest_tile = editor.getMap().getTile(pos);

			for(Item* item : items) {
				if(dest_tile)
					BlitItem(draw_x, draw_y, dest_tile, item, true, 160,160,160,160);
				else
					BlitItem(draw_x, draw_y, pos, item, true, 160,160,160,160);
			}

			if(options.show_creatures && tile->creature && tile->creature->isSelected())
				BlitCreature(draw_x, draw_y, tile->creature);
			if(tile->spawn && tile->spawn->isSelected())
				DrawIndicator(draw_x, draw_y, EDITOR_SPRITE_SPAWNS, 160, 160, 160, 160);
		}
	}

	glDisable(GL_TEXTURE_2D);
}

void MapDrawer::DrawHigherFloors()
{
	if(!options.transparent_floors || floor == 0 || floor == 8)
		return;

	glEnable(GL_TEXTURE_2D);

	int map_z = floor - 1;
	for(int map_x = start_x; map_x <= end_x; map_x++) {
		for(int map_y = start_y; map_y <= end_y; map_y++) {
			Tile* tile = editor.getMap().getTile(map_x, map_y, map_z);
			if(!tile) continue;

			int draw_x, draw_y;
			getDrawPosition(tile->getPosition(), draw_x, draw_y);

			if(tile->ground) {
				if(tile->isPZ()) {
					BlitItem(draw_x, draw_y, tile, tile->ground, false, 128,255,128, 96);
				} else {
					BlitItem(draw_x, draw_y, tile, tile->ground, false, 255,255,255, 96);
				}
			}

			bool hidden = options.hide_items_when_zoomed && zoom > 10.f && !options.full_detail_zoom_out;
			if(!hidden && !tile->items.empty()) {
				for(const Item* item : tile->items)
					BlitItem(draw_x, draw_y, tile, item, false, 255,255,255, 96);
			}
		}
	}

	glDisable(GL_TEXTURE_2D);
}

void MapDrawer::DrawFloorTilesWithAlpha(int target_floor, int alpha)
{
	int current_floor = floor;
	int cur_adjustment = getFloorAdjustment(current_floor);
	int target_adjustment = getFloorAdjustment(target_floor);
	int offset = target_adjustment - cur_adjustment;

	glEnable(GL_TEXTURE_2D);

	for(int map_x = start_x; map_x <= end_x; map_x++) {
		for(int map_y = start_y; map_y <= end_y; map_y++) {
			Tile* tile = editor.getMap().getTile(map_x, map_y, target_floor);
			if(!tile) continue;

			int draw_x = map_x * rme::TileSize - view_scroll_x - cur_adjustment + offset;
			int draw_y = map_y * rme::TileSize - view_scroll_y - cur_adjustment + offset;

			if(tile->ground) {
				BlitItem(draw_x, draw_y, tile, tile->ground, false, 255, 255, 255, alpha);
			}

			bool hidden = options.hide_items_when_zoomed && zoom > 10.f && !options.full_detail_zoom_out;
			if(!hidden && !tile->items.empty()) {
				for(const Item* item : tile->items)
					BlitItem(draw_x, draw_y, tile, item, false, 255, 255, 255, alpha);
			}

			if(options.show_creatures && tile->creature) {
				BlitCreature(draw_x, draw_y, tile->creature, 255, 255, 255, alpha);
			}
		}
	}

	glDisable(GL_TEXTURE_2D);
}

void MapDrawer::DrawFloorFadeOverlay()
{
	int mode = g_settings.getInteger(Config::FLOOR_FADING_MODE);
	long duration = g_settings.getInteger(Config::FLOOR_FADING_DURATION);
	int easing_type = g_settings.getInteger(Config::FLOOR_FADING_EASING);
	int max_opacity_pct = g_settings.getInteger(Config::FLOOR_FADING_OPACITY);
	float max_alpha = 255.0f * max_opacity_pct / 100.0f;

	if(mode == 1) {
		// Continuous mode: render all accumulated fade layers
		auto& layers = canvas->floor_fade_layers;

		// Remove expired layers
		for(auto it = layers.begin(); it != layers.end(); ) {
			if(it->timer.Time() >= duration)
				it = layers.erase(it);
			else
				++it;
		}

		// Render each layer with its own alpha
		for(const auto& layer : layers) {
			long elapsed = layer.timer.Time();
			if(elapsed >= duration) continue;

			float t = static_cast<float>(elapsed) / duration;
			float eased;
			switch(easing_type) {
				case 1: eased = 1.0f - (1.0f - t) * (1.0f - t); break;
				case 2: eased = t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f; break;
				default: eased = t; break;
			}

			int alpha = static_cast<int>(max_alpha * (1.0f - eased));
			if(alpha > 0)
				DrawFloorTilesWithAlpha(layer.floor_z, alpha);
		}

		// Also handle the main fade for the most recent floor change
		if(!canvas->IsFloorFading()) {
			canvas->floor_fading = false;
			return;
		}
		// The most recent change is already in layers, so we're done
		return;
	}

	// Non-continuous modes use the single fade timer
	if(!canvas->IsFloorFading()) {
		canvas->floor_fading = false;
		return;
	}

	int alpha = canvas->GetFloorFadeAlpha();
	if(alpha <= 0) {
		canvas->floor_fading = false;
		return;
	}

	if(mode == 0) {
		// Crossfade: render old floor with decreasing alpha
		DrawFloorTilesWithAlpha(canvas->GetFloorFadeOldFloor(), alpha);
	}
	else if(mode == 2) {
		// Fade to Black: draw black quad with alpha that goes up then down
		float progress = canvas->GetFloorFadeProgress();
		float black_alpha;
		if(progress < 0.5f) {
			// First half: fade to black (0 -> max)
			black_alpha = max_alpha * (progress * 2.0f);
		} else {
			// Second half: fade from black (max -> 0)
			black_alpha = max_alpha * ((1.0f - progress) * 2.0f);
		}

		int a = static_cast<int>(black_alpha);
		if(a > 0) {
			glDisable(GL_TEXTURE_2D);
			glColor4ub(0, 0, 0, static_cast<uint8_t>(a));
			glBegin(GL_QUADS);
				glVertex2f(0, 0);
				glVertex2f(screensize_x * zoom, 0);
				glVertex2f(screensize_x * zoom, screensize_y * zoom);
				glVertex2f(0, screensize_y * zoom);
			glEnd();
			glEnable(GL_TEXTURE_2D);
		}
	}
	else if(mode == 3) {
		// Fade Out: draw a covering quad that fades away (new floor appears gradually)
		// The new floor is already rendered; we cover it with black that fades out
		glDisable(GL_TEXTURE_2D);
		glColor4ub(0, 0, 0, static_cast<uint8_t>(alpha));
		glBegin(GL_QUADS);
			glVertex2f(0, 0);
			glVertex2f(screensize_x * zoom, 0);
			glVertex2f(screensize_x * zoom, screensize_y * zoom);
			glVertex2f(0, screensize_y * zoom);
		glEnd();
		glEnable(GL_TEXTURE_2D);
	}
}

void MapDrawer::DrawSelectionBox()
{
	if (options.ingame) {
		return;
	}

	if(g_settings.getBoolean(Config::SELECTION_LASSO) && !canvas->lasso_screen_points.empty()) {
		const std::vector<wxPoint>& points = canvas->lasso_screen_points;
		if(points.size() < 2) {
			return;
		}

		glDisable(GL_TEXTURE_2D);
		glEnable(GL_LINE_STIPPLE);
		glLineStipple(2, 0xAAAA);
		glLineWidth(1.0f);
		glColor4f(1.0, 1.0, 1.0, 1.0);
		glBegin(GL_LINE_STRIP);
		for(const wxPoint& point : points) {
			glVertex2f(point.x * zoom, point.y * zoom);
		}
		glVertex2f(points.front().x * zoom, points.front().y * zoom);
		glEnd();
		glDisable(GL_LINE_STIPPLE);

		const float dot_size = 3.0f;
		glColor4f(1.0, 1.0, 1.0, 1.0);
		glBegin(GL_QUADS);
		for(const wxPoint& point : points) {
			float x = point.x * zoom;
			float y = point.y * zoom;
			glVertex2f(x - dot_size, y - dot_size);
			glVertex2f(x + dot_size, y - dot_size);
			glVertex2f(x + dot_size, y + dot_size);
			glVertex2f(x - dot_size, y + dot_size);
		}
		glEnd();
		glEnable(GL_TEXTURE_2D);
		return;
	}

	// Draw bounding box

	int last_click_rx = canvas->last_click_abs_x - view_scroll_x;
	int last_click_ry = canvas->last_click_abs_y - view_scroll_y;
	double cursor_rx = canvas->cursor_x * zoom;
	double cursor_ry = canvas->cursor_y * zoom;

	static double lines[4][4];

	lines[0][0] = last_click_rx;
	lines[0][1] = last_click_ry;
	lines[0][2] = cursor_rx;
	lines[0][3] = last_click_ry;

	lines[1][0] = cursor_rx;
	lines[1][1] = last_click_ry;
	lines[1][2] = cursor_rx;
	lines[1][3] = cursor_ry;

	lines[2][0] = cursor_rx;
	lines[2][1] = cursor_ry;
	lines[2][2] = last_click_rx;
	lines[2][3] = cursor_ry;

	lines[3][0] = last_click_rx;
	lines[3][1] = cursor_ry;
	lines[3][2] = last_click_rx;
	lines[3][3] = last_click_ry;

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_LINE_STIPPLE);
	glLineStipple(2, 0xAAAA);
	glLineWidth(1.0);
	glColor4f(1.0,1.0,1.0,1.0);
	glBegin(GL_LINES);
	for(int i = 0; i < 4; i++) {
		glVertex2f(lines[i][0], lines[i][1]);
		glVertex2f(lines[i][2], lines[i][3]);
	}
	glEnd();
	glDisable(GL_LINE_STIPPLE);
	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawLiveCursors()
{
	if(options.ingame || !editor.IsLive())
		return;

	LiveSocket& live = editor.GetLive();
	for(LiveCursor& cursor : live.getCursorList()) {
		if(cursor.pos.z <= rme::MapGroundLayer && floor > rme::MapGroundLayer) {
			continue;
		}

		if(cursor.pos.z > rme::MapGroundLayer && floor <= 8) {
			continue;
		}

		if(cursor.pos.z < floor) {
			cursor.color = wxColor(
				cursor.color.Red(),
				cursor.color.Green(),
				cursor.color.Blue(),
				std::max<uint8_t>(cursor.color.Alpha() / 2, 64)
			);
		}

		int offset;
		if(cursor.pos.z <= rme::MapGroundLayer)
			offset = (rme::MapGroundLayer - cursor.pos.z) * rme::TileSize;
		else
			offset = rme::TileSize * (floor - cursor.pos.z);

		float draw_x = ((cursor.pos.x * rme::TileSize) - view_scroll_x) - offset;
		float draw_y = ((cursor.pos.y * rme::TileSize) - view_scroll_y) - offset;

		glColor(cursor.color);
		glBegin(GL_QUADS);
			glVertex2f(draw_x, draw_y);
			glVertex2f(draw_x + rme::TileSize, draw_y);
			glVertex2f(draw_x + rme::TileSize, draw_y + rme::TileSize);
			glVertex2f(draw_x, draw_y + rme::TileSize);
		glEnd();
	}
}

void MapDrawer::DrawBrush()
{
	if(options.ingame || !g_gui.IsDrawingMode() || !g_gui.GetCurrentBrush()) {
		return;
	}

	Brush* brush = g_gui.GetCurrentBrush();

	BrushColor brushColor = COLOR_BLANK;
	if(brush->isTerrain() || brush->isTable() || brush->isCarpet())
		brushColor = COLOR_BRUSH;
	else if(brush->isHouse())
		brushColor = COLOR_HOUSE_BRUSH;
	else if(brush->isFlag())
		brushColor = COLOR_FLAG_BRUSH;
	else if(brush->isSpawn())
		brushColor = COLOR_SPAWN_BRUSH;
	else if(brush->isEraser())
		brushColor = COLOR_ERASER;

	int adjustment = getFloorAdjustment(floor);


	if(dragging_draw) {
		ASSERT(brush->canDrag());

		if(brush->isWall()) {
			int last_click_start_map_x = std::min(canvas->last_click_map_x, mouse_map_x);
			int last_click_start_map_y = std::min(canvas->last_click_map_y, mouse_map_y);
			int last_click_end_map_x = std::max(canvas->last_click_map_x, mouse_map_x)+1;
			int last_click_end_map_y = std::max(canvas->last_click_map_y, mouse_map_y)+1;

			int last_click_start_sx = last_click_start_map_x * rme::TileSize - view_scroll_x - adjustment;
			int last_click_start_sy = last_click_start_map_y * rme::TileSize - view_scroll_y - adjustment;
			int last_click_end_sx = last_click_end_map_x * rme::TileSize - view_scroll_x - adjustment;
			int last_click_end_sy = last_click_end_map_y * rme::TileSize - view_scroll_y - adjustment;

			int delta_x = last_click_end_sx - last_click_start_sx;
			int delta_y = last_click_end_sy - last_click_start_sy;

			glColor(brushColor);
			glBegin(GL_QUADS);
				{
					glVertex2f(last_click_start_sx, last_click_start_sy + rme::TileSize);
					glVertex2f(last_click_end_sx, last_click_start_sy + rme::TileSize);
					glVertex2f(last_click_end_sx, last_click_start_sy);
					glVertex2f(last_click_start_sx, last_click_start_sy);
				}

				if(delta_y > rme::TileSize) {
					glVertex2f(last_click_start_sx, last_click_end_sy - rme::TileSize);
					glVertex2f(last_click_start_sx + rme::TileSize, last_click_end_sy - rme::TileSize);
					glVertex2f(last_click_start_sx + rme::TileSize, last_click_start_sy + rme::TileSize);
					glVertex2f(last_click_start_sx, last_click_start_sy + rme::TileSize);
				}

				if(delta_x > rme::TileSize && delta_y > rme::TileSize) {
					glVertex2f(last_click_end_sx - rme::TileSize, last_click_start_sy + rme::TileSize);
					glVertex2f(last_click_end_sx, last_click_start_sy + rme::TileSize);
					glVertex2f(last_click_end_sx, last_click_end_sy - rme::TileSize);
					glVertex2f(last_click_end_sx - rme::TileSize, last_click_end_sy - rme::TileSize);
				}

				if(delta_y > rme::TileSize) {
					glVertex2f(last_click_start_sx, last_click_end_sy - rme::TileSize);
					glVertex2f(last_click_end_sx, last_click_end_sy - rme::TileSize);
					glVertex2f(last_click_end_sx, last_click_end_sy);
					glVertex2f(last_click_start_sx, last_click_end_sy);
				}
			glEnd();
		} else {
			if(brush->isRaw())
				glEnable(GL_TEXTURE_2D);

			if(g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE || brush->isSpawn() /* Spawn brush is always square */) {
				int last_click_start_map_x = std::min(canvas->last_click_map_x, mouse_map_x);
				int last_click_start_map_y = std::min(canvas->last_click_map_y, mouse_map_y);
				int last_click_end_map_x = std::max(canvas->last_click_map_x, mouse_map_x) + 1;
				int last_click_end_map_y = std::max(canvas->last_click_map_y, mouse_map_y) + 1;

				if(brush->isRaw() || brush->isOptionalBorder()) {
					int start_x, end_x;
					int start_y, end_y;

					if(mouse_map_x < canvas->last_click_map_x) {
						start_x = mouse_map_x;
						end_x = canvas->last_click_map_x;
					} else {
						start_x = canvas->last_click_map_x;
						end_x = mouse_map_x;
					}
					if(mouse_map_y < canvas->last_click_map_y) {
						start_y = mouse_map_y;
						end_y = canvas->last_click_map_y;
					} else {
						start_y = canvas->last_click_map_y;
						end_y = mouse_map_y;
					}

					RAWBrush* raw_brush = nullptr;
					if(brush->isRaw())
						raw_brush = brush->asRaw();

					for(int y = start_y; y <= end_y; y++) {
						int cy = y * rme::TileSize - view_scroll_y - adjustment;
						for(int x = start_x; x <= end_x; x++) {
							int cx = x * rme::TileSize - view_scroll_x - adjustment;
							if(brush->isOptionalBorder())
								glColorCheck(brush, Position(x, y, floor));
							else
								BlitSpriteType(cx, cy, raw_brush->getItemType()->sprite, 160, 160, 160, 160);
						}
					}
				} else if(brush->isSpawn()) {
					int radius = g_gui.GetBrushSize();
					int start_map_x = mouse_map_x - radius;
					int start_map_y = mouse_map_y - radius;
					int end_map_x   = mouse_map_x + radius + 1;
					int end_map_y   = mouse_map_y + radius + 1;
					DrawSpawnAreaOverlay(start_map_x, start_map_y, end_map_x, end_map_y, radius, true);
				} else {
					int last_click_start_sx = last_click_start_map_x * rme::TileSize - view_scroll_x - adjustment;
					int last_click_start_sy = last_click_start_map_y * rme::TileSize - view_scroll_y - adjustment;
					int last_click_end_sx = last_click_end_map_x * rme::TileSize - view_scroll_x - adjustment;
					int last_click_end_sy = last_click_end_map_y * rme::TileSize - view_scroll_y - adjustment;

					glColor(brushColor);
					glBegin(GL_QUADS);
						glVertex2f(last_click_start_sx, last_click_start_sy);
						glVertex2f(last_click_end_sx, last_click_start_sy);
						glVertex2f(last_click_end_sx, last_click_end_sy);
						glVertex2f(last_click_start_sx, last_click_end_sy);
					glEnd();
				}
			} else if(g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE) {
				// Calculate drawing offsets
				int start_x, end_x;
				int start_y, end_y;
				int width = std::max(
					std::abs(std::max(mouse_map_y, canvas->last_click_map_y) - std::min(mouse_map_y, canvas->last_click_map_y)),
					std::abs(std::max(mouse_map_x, canvas->last_click_map_x) - std::min(mouse_map_x, canvas->last_click_map_x))
					);

				if(mouse_map_x < canvas->last_click_map_x) {
					start_x = canvas->last_click_map_x - width;
					end_x = canvas->last_click_map_x;
				} else {
					start_x = canvas->last_click_map_x;
					end_x = canvas->last_click_map_x + width;
				}

				if(mouse_map_y < canvas->last_click_map_y) {
					start_y = canvas->last_click_map_y - width;
					end_y = canvas->last_click_map_y;
				} else {
					start_y = canvas->last_click_map_y;
					end_y = canvas->last_click_map_y + width;
				}

				int center_x = start_x + (end_x - start_x) / 2;
				int center_y = start_y + (end_y - start_y) / 2;
				float radii = width / 2.0f + 0.005f;

				RAWBrush* raw_brush = nullptr;
				if(brush->isRaw())
					raw_brush = brush->asRaw();

				for(int y = start_y-1; y <= end_y+1; y++) {
					int cy = y * rme::TileSize - view_scroll_y - adjustment;
					float dy = center_y - y;
					for(int x = start_x-1; x <= end_x+1; x++) {
						int cx = x * rme::TileSize - view_scroll_x - adjustment;

						float dx = center_x - x;
						//printf("%f;%f\n", dx, dy);
						float distance = sqrt(dx*dx + dy*dy);
						if(distance < radii) {
							if(brush->isRaw()) {
								BlitSpriteType(cx, cy, raw_brush->getItemType()->sprite, 160, 160, 160, 160);
							} else {
								glColor(brushColor);
								glBegin(GL_QUADS);
									glVertex2f(cx, cy + rme::TileSize);
									glVertex2f(cx + rme::TileSize, cy + rme::TileSize);
									glVertex2f(cx + rme::TileSize, cy);
									glVertex2f(cx,   cy);
								glEnd();
							}
						}
					}
				}
			}

			if(brush->isRaw())
				glDisable(GL_TEXTURE_2D);
		}
	} else {
		if(brush->isWall()) {
			int start_map_x = mouse_map_x - g_gui.GetBrushSize();
			int start_map_y = mouse_map_y - g_gui.GetBrushSize();
			int end_map_x   = mouse_map_x + g_gui.GetBrushSize() + 1;
			int end_map_y   = mouse_map_y + g_gui.GetBrushSize() + 1;

			int start_sx = start_map_x * rme::TileSize - view_scroll_x - adjustment;
			int start_sy = start_map_y * rme::TileSize - view_scroll_y - adjustment;
			int end_sx = end_map_x * rme::TileSize - view_scroll_x - adjustment;
			int end_sy = end_map_y * rme::TileSize - view_scroll_y - adjustment;

			int delta_x = end_sx - start_sx;
			int delta_y = end_sy - start_sy;

			glColor(brushColor);
			glBegin(GL_QUADS);
				{
					glVertex2f(start_sx, start_sy + rme::TileSize);
					glVertex2f(end_sx, start_sy + rme::TileSize);
					glVertex2f(end_sx, start_sy);
					glVertex2f(start_sx, start_sy);
				}

				if(delta_y > rme::TileSize) {
					glVertex2f(start_sx, end_sy - rme::TileSize);
					glVertex2f(start_sx + rme::TileSize, end_sy - rme::TileSize);
					glVertex2f(start_sx + rme::TileSize, start_sy + rme::TileSize);
					glVertex2f(start_sx, start_sy + rme::TileSize);
				}

				if(delta_x > rme::TileSize && delta_y > rme::TileSize) {
					glVertex2f(end_sx - rme::TileSize, start_sy + rme::TileSize);
					glVertex2f(end_sx, start_sy + rme::TileSize);
					glVertex2f(end_sx, end_sy - rme::TileSize);
					glVertex2f(end_sx - rme::TileSize, end_sy - rme::TileSize);
				}

				if(delta_y > rme::TileSize) {
					glVertex2f(start_sx, end_sy - rme::TileSize);
					glVertex2f(end_sx, end_sy - rme::TileSize);
					glVertex2f(end_sx, end_sy);
					glVertex2f(start_sx, end_sy);
				}
			glEnd();
		} else if(brush->isDoor()) {
			int cx = (mouse_map_x) * rme::TileSize - view_scroll_x - adjustment;
			int cy = (mouse_map_y) * rme::TileSize - view_scroll_y - adjustment;

			glColorCheck(brush, Position(mouse_map_x, mouse_map_y, floor));
			glBegin(GL_QUADS);
				glVertex2f(cx, cy + rme::TileSize);
				glVertex2f(cx + rme::TileSize, cy + rme::TileSize);
				glVertex2f(cx + rme::TileSize, cy);
				glVertex2f(cx, cy);
			glEnd();
		} else if(brush->isCreature()) {
			glEnable(GL_TEXTURE_2D);
			int cy = (mouse_map_y) * rme::TileSize - view_scroll_y - adjustment;
			int cx = (mouse_map_x) * rme::TileSize - view_scroll_x - adjustment;
			CreatureBrush* creature_brush = brush->asCreature();
			if(creature_brush->canDraw(&editor.getMap(), Position(mouse_map_x, mouse_map_y, floor)))
				BlitCreature(cx, cy, creature_brush->getType()->outfit, SOUTH, 255, 255, 255, 160);
			else
				BlitCreature(cx, cy, creature_brush->getType()->outfit, SOUTH, 255, 64, 64, 160);
			glDisable(GL_TEXTURE_2D);
		} else if(brush->isSpawn()) {
			int radius = g_gui.GetBrushSize();
			int start_map_x = mouse_map_x - radius;
			int start_map_y = mouse_map_y - radius;
			int end_map_x   = mouse_map_x + radius + 1;
			int end_map_y   = mouse_map_y + radius + 1;
			DrawSpawnAreaOverlay(start_map_x, start_map_y, end_map_x, end_map_y, radius, true);
		} else if(!brush->isDoodad()) {
			RAWBrush* raw_brush = nullptr;
			if(brush->isRaw()) { // Textured brush
				glEnable(GL_TEXTURE_2D);
				raw_brush = brush->asRaw();
			} else {
				glDisable(GL_TEXTURE_2D);
			}

			for(int y = -g_gui.GetBrushSize()-1; y <= g_gui.GetBrushSize()+1; y++) {
				int cy = (mouse_map_y + y) * rme::TileSize - view_scroll_y - adjustment;
				for(int x = -g_gui.GetBrushSize()-1; x <= g_gui.GetBrushSize()+1; x++) {
					int cx = (mouse_map_x + x) * rme::TileSize - view_scroll_x - adjustment;
					if(g_gui.GetBrushShape() == BRUSHSHAPE_SQUARE) {
						if(x >= -g_gui.GetBrushSize() && x <= g_gui.GetBrushSize() && y >= -g_gui.GetBrushSize() && y <= g_gui.GetBrushSize()) {
							if(brush->isRaw()) {
								BlitSpriteType(cx, cy, raw_brush->getItemType()->sprite, 160, 160, 160, 160);
							} else {
								if(brush->isWaypoint()) {
									uint8_t r, g, b;
									getColor(brush, Position(mouse_map_x + x, mouse_map_y + y, floor), r, g, b);
									DrawBrushIndicator(cx, cy, brush, r, g, b);
								} else {
									if(brush->isHouseExit() || brush->isOptionalBorder())
										glColorCheck(brush, Position(mouse_map_x + x, mouse_map_y + y, floor));
									else
										glColor(brushColor);

									glBegin(GL_QUADS);
										glVertex2f(cx, cy + rme::TileSize);
										glVertex2f(cx + rme::TileSize, cy + rme::TileSize);
										glVertex2f(cx + rme::TileSize, cy);
										glVertex2f(cx, cy);
									glEnd();
								}
							}
						}
					} else if(g_gui.GetBrushShape() == BRUSHSHAPE_CIRCLE) {
						double distance = sqrt(double(x*x) + double(y*y));
						if(distance < g_gui.GetBrushSize()+0.005) {
							if(brush->isRaw()) {
								BlitSpriteType(cx, cy, raw_brush->getItemType()->sprite, 160, 160, 160, 160);
							} else {
								if(brush->isWaypoint()) {
									uint8_t r, g, b;
									getColor(brush, Position(mouse_map_x + x, mouse_map_y + y, floor), r, g, b);
									DrawBrushIndicator(cx, cy, brush, r, g, b);
								} else {
									if(brush->isHouseExit() || brush->isOptionalBorder())
										glColorCheck(brush, Position(mouse_map_x + x, mouse_map_y + y, floor));
									else
										glColor(brushColor);

									glBegin(GL_QUADS);
										glVertex2f(cx, cy + rme::TileSize);
										glVertex2f(cx + rme::TileSize, cy + rme::TileSize);
										glVertex2f(cx + rme::TileSize, cy);
										glVertex2f(cx, cy);
									glEnd();
								}
							}
						}
					}
				}
			}

			if(brush->isRaw()) { // Textured brush
				glDisable(GL_TEXTURE_2D);
			} else {
				glEnable(GL_TEXTURE_2D);
			}
		}
	}
}

void MapDrawer::DrawFixedSavePreview(int map_z)
{
	if(canvas && canvas->IsPreviewMode()) {
		return;
	}
	if(options.ingame) {
		return;
	}

	int width = 0;
	int height = 0;
	int zFrom = 0;
	int zTo = 0;
	if(!StructureManagerDialog::GetFixedSavePreview(width, height, zFrom, zTo)) {
		return;
	}

	if(!canvas || !canvas->cursor_in_window) {
		return;
	}

	const int minZ = std::min(zFrom, zTo);
	const int maxZ = std::max(zFrom, zTo);
	if(map_z < minZ || map_z > maxZ) {
		return;
	}

	const int minX = mouse_map_x - (width / 2);
	const int minY = mouse_map_y - (height / 2);
	const int rectWidth = width * rme::TileSize;
	const int rectHeight = height * rme::TileSize;

	Position topLeft(minX, minY, map_z);
	int drawX = 0;
	int drawY = 0;
	getDrawPosition(topLeft, drawX, drawY);

	glDisable(GL_TEXTURE_2D);
	drawFilledRect(drawX, drawY, rectWidth, rectHeight, wxColour(255, 165, 0, 40));
	drawRect(drawX, drawY, rectWidth, rectHeight, wxColour(255, 165, 0, 200), 2);
	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawCursorTile()
{
	if(canvas->screenshot_buffer) {
		return;
	}

	if(!canvas->cursor_in_window) {
		return;
	}

	const bool show_cursor_border = g_settings.getBoolean(Config::SHOW_CURSOR_HIGHLIGHT);
	const bool show_indicator = HasActiveSelectionIndicator();
	if(!show_cursor_border && !show_indicator) {
		return;
	}

	Position cursor_position(mouse_map_x, mouse_map_y, floor);
	int x, y;
	getDrawPosition(cursor_position, x, y);

	if(show_indicator) {
		DrawSelectedTileIndicator(x, y);
	}

	if(show_cursor_border) {
		glDisable(GL_TEXTURE_2D);
		drawRect(x, y, rme::TileSize, rme::TileSize, *wxWHITE, 2);
		glEnable(GL_TEXTURE_2D);
	}
}

void MapDrawer::BlitItem(int& draw_x, int& draw_y, const Tile* tile, const Item* item, bool ephemeral, int red, int green, int blue, int alpha)
{
	const ItemType& type = g_items.getItemType(item->getID());
	if(type.id == 0) {
		glDisable(GL_TEXTURE_2D);
		glBlitSquare(draw_x, draw_y, *wxRED);
		glEnable(GL_TEXTURE_2D);
		return;
	}

	if(!options.ingame && !ephemeral && item->isSelected()) {
		red /= 2;
		blue /= 2;
		green /= 2;
	}

	// Ugly hacks. :)
	if(type.id == 459 && !options.ingame && options.show_tech_items) {
		glDisable(GL_TEXTURE_2D);
		glBlitSquare(draw_x, draw_y, red, green, 0, alpha / 3 * 2);
		glEnable(GL_TEXTURE_2D);
		return;
	} else if(type.id == 460 && !options.ingame && options.show_tech_items) {
		glDisable(GL_TEXTURE_2D);
		glBlitSquare(draw_x, draw_y, red, 0, 0, alpha / 3 * 2);
		glEnable(GL_TEXTURE_2D);
		return;
	} else if(type.id == 1548 && !options.ingame && options.show_tech_items) {
		glDisable(GL_TEXTURE_2D);
		glBlitSquare(draw_x, draw_y, 0, green, blue, 80);
		glEnable(GL_TEXTURE_2D);
		return;
	}

	if(type.isMetaItem())
		return;
	if(!ephemeral && type.pickupable && !options.show_items)
		return;

	GameSprite* sprite = type.sprite;
	if(!sprite)
		return;

	// Check if this is a mountain ground and mountain overlay is enabled
	// If so, render only the first tile (cx=0, cy=0) at the current position
	bool mountain_overlay_mode = false;
	if(options.show_mountain_overlay && type.isGroundTile() && tile->ground == item) {
		GroundBrush* gb = item->getGroundBrush();
		if(gb && gb->getZ() >= 9000) {
			mountain_overlay_mode = true;
		}
	}

	int screenx = draw_x - sprite->getDrawOffset().x;
	int screeny = draw_y - sprite->getDrawOffset().y;

	const Position& pos = tile->getPosition();

	// Set the newd drawing height accordingly
	draw_x -= sprite->getDrawHeight();
	draw_y -= sprite->getDrawHeight();

	int subtype = -1;

	int pattern_x = 0;
	int pattern_y = 0;
	int pattern_z = pos.z % sprite->pattern_z;

	if(type.isSplash() || type.isFluidContainer()) {
		subtype = item->getSubtype();
	} else if(type.isHangable) {
		if(tile->hasProperty(HOOK_SOUTH)) {
			pattern_x = 1;
		} else if(tile->hasProperty(HOOK_EAST)) {
			pattern_x = 2;
		}
	} else if(type.stackable && sprite->pattern_x == 4 && sprite->pattern_y == 2) {
		int count = item->getSubtype();
		if(count <= 0) {
			pattern_x = 0;
			pattern_y = 0;
		} else if(count < 5) {
			pattern_x = count - 1;
			pattern_y = 0;
		} else if(count < 10) {
			pattern_x = 0;
			pattern_y = 1;
		} else if(count < 25) {
			pattern_x = 1;
			pattern_y = 1;
		} else if(count < 50) {
			pattern_x = 2;
			pattern_y = 1;
		} else {
			pattern_x = 3;
			pattern_y = 1;
		}
	} else {
		pattern_x = pos.x % sprite->pattern_x;
		pattern_y = pos.y % sprite->pattern_y;
	}

	if(!ephemeral && !type.isSplash()) {
		bool apply_transparency = false;

		// Ghost ground items - applies to ground tiles of size 1x1
		if(options.transparent_ground_items && type.isGroundTile() && sprite->width == 1 && sprite->height == 1) {
			apply_transparency = true;
		}
		// Ghost loose items - applies to non-ground items (or large ground items)
		else if(options.transparent_items &&
				(!type.isGroundTile() || sprite->width > 1 || sprite->height > 1) &&
				(!type.isBorder || sprite->width > 1 || sprite->height > 1)) {
			apply_transparency = true;
		}

		if(apply_transparency) {
			alpha /= 2;
		}
	}

	int frame = item->getFrame();

	// Mountain overlay mode: render the full sprite with low opacity
	// and draw a yellow X pattern indicator at the tile position
	if(mountain_overlay_mode) {
		// Draw the full sprite at normal position with low opacity
		for(int cx = 0; cx != sprite->width; cx++) {
			for(int cy = 0; cy != sprite->height; cy++) {
				for(int cf = 0; cf != sprite->layers; cf++) {
					int texnum = sprite->getHardwareID(cx, cy, cf,
						subtype,
						pattern_x,
						pattern_y,
						pattern_z,
						frame
					);
					glBlitTexture(screenx - cx * rme::TileSize, screeny - cy * rme::TileSize, texnum, red, green, blue, 80);
				}
			}
		}

		// Draw orange X pattern indicator at the actual tile position (draw_x, draw_y)
		// The indicator should be at the tile where the mountain ground is placed
		int indicator_x = draw_x + rme::TileSize; // Offset to match visual position
		int indicator_y = draw_y + rme::TileSize;
		glDisable(GL_TEXTURE_2D);
		glColor4ub(255, 165, 0, 200); // Orange color
		glLineWidth(1.0f);
		glBegin(GL_LINES);
		// Diagonal from top-left to bottom-right
		glVertex2f(indicator_x + 2, indicator_y + 2);
		glVertex2f(indicator_x + rme::TileSize - 2, indicator_y + rme::TileSize - 2);
		// Diagonal from top-right to bottom-left
		glVertex2f(indicator_x + rme::TileSize - 2, indicator_y + 2);
		glVertex2f(indicator_x + 2, indicator_y + rme::TileSize - 2);
		glEnd();
		// Draw border
		glBegin(GL_LINE_LOOP);
		glVertex2f(indicator_x, indicator_y);
		glVertex2f(indicator_x + rme::TileSize, indicator_y);
		glVertex2f(indicator_x + rme::TileSize, indicator_y + rme::TileSize);
		glVertex2f(indicator_x, indicator_y + rme::TileSize);
		glEnd();
		glEnable(GL_TEXTURE_2D);
	} else {
		for(int cx = 0; cx != sprite->width; cx++) {
			for(int cy = 0; cy != sprite->height; cy++) {
				for(int cf = 0; cf != sprite->layers; cf++) {
					int texnum = sprite->getHardwareID(cx,cy,cf,
						subtype,
						pattern_x,
						pattern_y,
						pattern_z,
						frame
					);
					glBlitTexture(screenx - cx * rme::TileSize, screeny - cy * rme::TileSize, texnum, red, green, blue, alpha);
				}
			}
		}
	}

	if(options.show_hooks && (type.hookSouth || type.hookEast))
		DrawHookIndicator(draw_x, draw_y, type);
}

void MapDrawer::BlitItem(int& draw_x, int& draw_y, const Position& pos, const Item* item, bool ephemeral, int red, int green, int blue, int alpha)
{
	const ItemType& type = g_items.getItemType(item->getID());
	if(type.id == 0)
		return;

	if(!options.ingame && !ephemeral && item->isSelected()) {
		red /= 2;
		blue /= 2;
		green /= 2;
	}

	if(type.id == 459 && !options.ingame) { // Ugly hack yes?
		glDisable(GL_TEXTURE_2D);
		glBlitSquare(draw_x, draw_y, red, green, 0, alpha/3*2);
		glEnable(GL_TEXTURE_2D);
		return;
	} else if(type.id == 460 && !options.ingame) { // Ugly hack yes?
		glDisable(GL_TEXTURE_2D);
		glBlitSquare(draw_x, draw_y, red, 0, 0, alpha/3*2);
		glEnable(GL_TEXTURE_2D);
		return;
	}

	if(type.isMetaItem())
		return;
	if(!ephemeral && type.pickupable && options.show_items)
		return;

	GameSprite* sprite = type.sprite;
	if(!sprite)
		return;

	// Check if this is a mountain ground and mountain overlay is enabled
	// If so, render only the first tile (cx=0, cy=0) at the current position
	bool mountain_overlay_mode = false;
	if(options.show_mountain_overlay && type.isGroundTile()) {
		GroundBrush* gb = item->getGroundBrush();
		if(gb && gb->getZ() >= 9000) {
			mountain_overlay_mode = true;
		}
	}

	int screenx = draw_x - sprite->getDrawOffset().x;
	int screeny = draw_y - sprite->getDrawOffset().y;

	// Set the newd drawing height accordingly
	draw_x -= sprite->getDrawHeight();
	draw_y -= sprite->getDrawHeight();

	int subtype = -1;

	int pattern_x = 0;
	int pattern_y = 0;
	int pattern_z = pos.z % sprite->pattern_z;

	if(type.isSplash() || type.isFluidContainer()) {
		subtype = item->getSubtype();
	} else if(type.stackable && sprite->pattern_x == 4 && sprite->pattern_y == 2) {
		int count = item->getSubtype();
		if(count <= 0) {
			pattern_x = 0;
			pattern_y = 0;
		} else if(count < 5) {
			pattern_x = count - 1;
			pattern_y = 0;
		} else if(count < 10) {
			pattern_x = 0;
			pattern_y = 1;
		} else if(count < 25) {
			pattern_x = 1;
			pattern_y = 1;
		} else if(count < 50) {
			pattern_x = 2;
			pattern_y = 1;
		} else {
			pattern_x = 3;
			pattern_y = 1;
		}
	} else {
		pattern_x = pos.x % sprite->pattern_x;
		pattern_y = pos.y % sprite->pattern_y;
	}

	if(!ephemeral && !type.isSplash()) {
		bool apply_transparency = false;

		// Ghost ground items - applies to ground tiles of size 1x1
		if(options.transparent_ground_items && type.isGroundTile() && sprite->width == 1 && sprite->height == 1) {
			apply_transparency = true;
		}
		// Ghost loose items - applies to non-ground items (or large ground items)
		else if(options.transparent_items &&
				(!type.isGroundTile() || sprite->width > 1 || sprite->height > 1) &&
				(!type.isBorder || sprite->width > 1 || sprite->height > 1)) {
			apply_transparency = true;
		}

		if(apply_transparency) {
			alpha /= 2;
		}
	}

	int frame = item->getFrame();

	// Mountain overlay mode: render the full sprite with low opacity
	// and draw an orange X pattern indicator
	if(mountain_overlay_mode) {
		// Draw the full sprite at normal position with low opacity
		for(int cx = 0; cx != sprite->width; ++cx) {
			for(int cy = 0; cy != sprite->height; ++cy) {
				for(int cf = 0; cf != sprite->layers; ++cf) {
					int texnum = sprite->getHardwareID(cx, cy, cf,
						subtype,
						pattern_x,
						pattern_y,
						pattern_z,
						frame
					);
					glBlitTexture(screenx - cx * rme::TileSize, screeny - cy * rme::TileSize, texnum, red, green, blue, 80);
				}
			}
		}

		// Draw orange X pattern indicator at the actual tile position
		int indicator_x = draw_x + rme::TileSize;
		int indicator_y = draw_y + rme::TileSize;
		glDisable(GL_TEXTURE_2D);
		glColor4ub(255, 165, 0, 200); // Orange color
		glLineWidth(1.0f);
		glBegin(GL_LINES);
		// Diagonal from top-left to bottom-right
		glVertex2f(indicator_x + 2, indicator_y + 2);
		glVertex2f(indicator_x + rme::TileSize - 2, indicator_y + rme::TileSize - 2);
		// Diagonal from top-right to bottom-left
		glVertex2f(indicator_x + rme::TileSize - 2, indicator_y + 2);
		glVertex2f(indicator_x + 2, indicator_y + rme::TileSize - 2);
		glEnd();
		// Draw border
		glBegin(GL_LINE_LOOP);
		glVertex2f(indicator_x, indicator_y);
		glVertex2f(indicator_x + rme::TileSize, indicator_y);
		glVertex2f(indicator_x + rme::TileSize, indicator_y + rme::TileSize);
		glVertex2f(indicator_x, indicator_y + rme::TileSize);
		glEnd();
		glEnable(GL_TEXTURE_2D);
	} else {
		for(int cx = 0; cx != sprite->width; ++cx) {
			for(int cy = 0; cy != sprite->height; ++cy) {
				for(int cf = 0; cf != sprite->layers; ++cf) {
					int texnum = sprite->getHardwareID(cx,cy,cf,
						subtype,
						pattern_x,
						pattern_y,
						pattern_z,
						frame
					);
					glBlitTexture(screenx - cx * rme::TileSize, screeny - cy * rme::TileSize, texnum, red, green, blue, alpha);
				}
			}
		}
	}

	if(options.show_hooks && (type.hookSouth || type.hookEast) && zoom <= 3.0)
		DrawHookIndicator(draw_x, draw_y, type);
}

void MapDrawer::BlitSpriteType(int screenx, int screeny, uint32_t spriteid, int red, int green, int blue, int alpha)
{
	const ItemType& type = g_items.getItemType(spriteid);
	if(type.id == 0)
		return;

	GameSprite* sprite = type.sprite;
	if(!sprite)
		return;

	screenx -= sprite->getDrawOffset().x;
	screeny -= sprite->getDrawOffset().y;

	int frame = 0;
	for(int cx = 0; cx != sprite->width; ++cx) {
		for(int cy = 0; cy != sprite->height; ++cy) {
			for(int cf = 0; cf != sprite->layers; ++cf) {
				int texnum = sprite->getHardwareID(cx,cy,cf,-1,0,0,0, frame);
				glBlitTexture(screenx - cx * rme::TileSize, screeny - cy * rme::TileSize, texnum, red, green, blue, alpha);
			}
		}
	}
}

void MapDrawer::BlitSpriteType(int screenx, int screeny, GameSprite* sprite, int red, int green, int blue, int alpha)
{
	if(!sprite) return;

	screenx -= sprite->getDrawOffset().x;
	screeny -= sprite->getDrawOffset().y;

	int frame = 0;
	for(int cx = 0; cx != sprite->width; ++cx) {
		for(int cy = 0; cy != sprite->height; ++cy) {
			for(int cf = 0; cf != sprite->layers; ++cf) {
				int texnum = sprite->getHardwareID(cx,cy,cf,-1,0,0,0, frame);
				glBlitTexture(screenx - cx * rme::TileSize, screeny - cy * rme::TileSize, texnum, red, green, blue, alpha);
			}
		}
	}
}

void MapDrawer::BlitCreature(int screenx, int screeny, const Outfit& outfit, Direction dir, int red, int green, int blue, int alpha)
{
	if(outfit.lookItem != 0) {
		const ItemType& type = g_items.getItemType(outfit.lookItem);
		BlitSpriteType(screenx, screeny, type.sprite, red, green, blue, alpha);
	} else {
		GameSprite* sprite = g_gui.gfx.getCreatureSprite(outfit.lookType);
		if(!sprite || outfit.lookType == 0) {
			return;
		}

		const bool animate_idle = options.show_creature_idle_animation && zoom <= 2.0f &&
			(sprite->has_idle_frame_group || sprite->animate_always);

		// mount and addon drawing thanks to otc code
		int pattern_z = 0;
		if(outfit.lookMount != 0) {
			if(GameSprite* mountSpr = g_gui.gfx.getCreatureSprite(outfit.lookMount)) {
				int mount_frame = 0;
				if(animate_idle && mountSpr->animator &&
					(mountSpr->has_idle_frame_group || mountSpr->animate_always)) {
					mount_frame = mountSpr->animator->getFrame();
				}
				for(int cx = 0; cx != mountSpr->width; ++cx) {
					for(int cy = 0; cy != mountSpr->height; ++cy) {
						int texnum = mountSpr->getHardwareID(cx, cy, 0, 0, (int)dir, 0, 0, mount_frame);
						glBlitTexture(screenx - cx * rme::TileSize, screeny - cy * rme::TileSize, texnum, red, green, blue, alpha);
					}
				}
				pattern_z = std::min<int>(1, sprite->pattern_z - 1);
			}
		}

		int frame = 0;
		if(animate_idle && sprite->animator) {
			frame = sprite->animator->getFrame();
		}

		// pattern_y => creature addon
		for(int pattern_y = 0; pattern_y < sprite->pattern_y; pattern_y++) {

			// continue if we dont have this addon
			if(pattern_y > 0 && !(outfit.lookAddon & (1 << (pattern_y - 1))))
				continue;

			for(int cx = 0; cx != sprite->width; ++cx) {
				for(int cy = 0; cy != sprite->height; ++cy) {
					int texnum = sprite->getHardwareID(cx, cy, (int)dir, pattern_y, pattern_z, outfit, frame);
					glBlitTexture(screenx - cx * rme::TileSize, screeny - cy * rme::TileSize, texnum, red, green, blue, alpha);
				}
			}
		}
	}
}

void MapDrawer::BlitCreature(int screenx, int screeny, const Creature* creature, int red, int green, int blue, int alpha)
{
	if(!options.ingame && creature->isSelected()) {
		red /= 2;
		green /= 2;
		blue /= 2;
	}
	BlitCreature(screenx, screeny, creature->getLookType(), creature->getDirection(), red, green, blue, alpha);
}

void MapDrawer::WriteTooltip(const Item* item, std::ostringstream& stream)
{
	if(!item) return;

	const uint16_t id = item->getID();
	if(id < 100)
		return;

	const uint16_t unique = item->getUniqueID();
	const uint16_t action = item->getActionID();
	const std::string& text = item->getText();
	const Teleport* teleport = dynamic_cast<const Teleport*>(item);
	const bool has_destination = teleport && teleport->hasDestination();

	if(unique == 0 && action == 0 && text.empty() && !has_destination)
		return;

	if(stream.tellp() > 0)
		stream << "\n";

	stream << "id: " << id << "\n";

	if(action > 0)
		stream << "aid: " << action << "\n";
	if(unique > 0)
		stream << "uid: " << unique << "\n";
	if(!text.empty())
		stream << "text: " << text << "\n";
	if(has_destination) {
		const Position& destination = teleport->getDestination();
		stream << "dest: " << destination.x << ", " << destination.y << ", " << destination.z << "\n";
	}
}

void MapDrawer::WriteTooltip(const Waypoint* waypoint, std::ostringstream& stream)
{
	if(stream.tellp() > 0)
		stream << "\n";
	stream << "wp: " << waypoint->name << "\n";
}

void MapDrawer::DrawTile(TileLocation* location)
{
	if(!location) return;

	Tile* tile = location->get();
	if(!tile) return;

	if(options.show_only_modified && !tile->isModified())
		return;

	const Position& position = location->getPosition();
	int lod_level = GetLODLevel();
	
	// LOD Level 3 (zoom > 10): Ultra simplified - just minimap colors
	if(lod_level >= 3) {
		int draw_x, draw_y;
		getDrawPosition(position, draw_x, draw_y);
		if(tile->hasGround()) {
			glDisable(GL_TEXTURE_2D);
			wxColor color = colorFromEightBit(tile->getMiniMapColor());
			glBlitSquare(draw_x, draw_y, color);
			glEnable(GL_TEXTURE_2D);
		}
		return;
	}
	
	bool show_tooltips = options.isTooltips() && lod_level < 3;

	if(show_tooltips && location->getWaypointCount() > 0) {
		Waypoint* waypoint = canvas->editor.getMap().waypoints.getWaypoint(position);
		if(waypoint)
			WriteTooltip(waypoint, tooltip);
	}

	bool only_colors = options.isOnlyColors();

	int draw_x, draw_y;
	getDrawPosition(position, draw_x, draw_y);
	int base_draw_x = draw_x;
	int base_draw_y = draw_y;

	uint8_t r = 255,g = 255,b = 255;
	if(only_colors || tile->hasGround()) {

		if(!options.show_as_minimap) {
			bool showspecial = options.show_only_colors || options.show_special_tiles;

			if(options.show_blocking && tile->isBlocking() && tile->size() > 0) {
				g = g / 3 * 2;
				b = b / 3 * 2;
			}

			int item_count = tile->items.size();
			if(options.highlight_items && item_count > 0 && !tile->items.back()->isBorder()) {
				static const float factor[5] = { 0.75f, 0.6f, 0.48f, 0.40f, 0.33f };
				int idx = (item_count < 5 ? item_count : 5) - 1;
				g = int(g * factor[idx]);
				r = int(r * factor[idx]);
			}

			if(options.show_houses && tile->isHouseTile()) {
				if((int)tile->getHouseID() == current_house_id) {
					r /= 2;
				} else {
					r /= 2;
					g /= 2;
				}
			}
			else if(showspecial && tile->isPZ()) {
				r /= 2;
				b /= 2;
			}

			if(showspecial && tile->getMapFlags() & TILESTATE_PVPZONE) {
				g = r / 4;
				b = b / 3 * 2;
			}

			if(showspecial && tile->getMapFlags() & TILESTATE_NOLOGOUT) {
				b /= 2;
			}

			if(showspecial && tile->getMapFlags() & TILESTATE_NOPVP) {
				g /= 2;
			}
			if(showspecial && tile->getMapFlags() & TILESTATE_WORLDBOSS) {
				r /= 2;
				g /= 2;
				b = 160;
			}
		}

		if(only_colors) {
			glDisable(GL_TEXTURE_2D);
			if(options.show_as_minimap) {
				wxColor color = colorFromEightBit(tile->getMiniMapColor());
				glBlitSquare(draw_x, draw_y, color);
			} else if(r != 255 || g != 255 || b != 255) {
				glBlitSquare(draw_x, draw_y, r, g, b, 128);
			}
			glEnable(GL_TEXTURE_2D);
		} else {
		// LOD Level 0: Enable animations
			if(lod_level == 0 && options.show_preview && zoom <= 2.0)
				tile->ground->animate();

			BlitItem(draw_x, draw_y, tile, tile->ground, false, r, g, b);
		}

		if(show_tooltips && position.z == floor)
			WriteTooltip(tile->ground, tooltip);
	}
	base_draw_x = draw_x;
	base_draw_y = draw_y;

	bool hidden = only_colors || (options.hide_items_when_zoomed && zoom > 10.f && !options.full_detail_zoom_out);
	
	// LOD Level 2+ (zoom > 6): Skip items entirely
	if(lod_level >= 2) {
		hidden = true;
	}

	if(!hidden && !tile->items.empty()) {
		for(Item* item : tile->items) {
			// Skip non-border items when show_only_grounds is enabled
			if(options.show_only_grounds && !item->isBorder())
				continue;

			if(show_tooltips && position.z == floor)
				WriteTooltip(item, tooltip);

		// LOD Level 0: Enable animations
			if(lod_level == 0 && options.show_preview && zoom <= 2.0)
				item->animate();

			if(item->isBorder()) {
				const int before_x = base_draw_x;
				const int before_y = base_draw_y;
				BlitItem(base_draw_x, base_draw_y, tile, item, false, r, g, b);
				draw_x -= (before_x - base_draw_x);
				draw_y -= (before_y - base_draw_y);
			} else {
				BlitItem(draw_x, draw_y, tile, item);
			}
		}
	}

	// LOD Level 2+ (zoom > 6): Skip creatures
	if(!hidden && options.show_creatures && tile->creature && lod_level < 2) {
		if(options.animate_creature_walk && tile->creature->hasWanderBehavior()) {
			// Register creature for walk animation and get visual offset
			g_creature_walk_animator.ensureRegistered(position.x, position.y, position.z, tile->creature);
			auto walk = g_creature_walk_animator.getOffset(position.x, position.y, position.z, tile->creature);

			int creature_draw_x = draw_x + walk.pixel_x;
			int creature_draw_y = draw_y + walk.pixel_y;

			// Determine direction: use walk direction when actively walking, otherwise creature default
			Direction dir = walk.is_walking ? walk.direction : tile->creature->getDirection();

			// Apply selection tint (same logic as BlitCreature(const Creature*))
			int cr = 255, cg = 255, cb = 255;
			if(!options.ingame && tile->creature->isSelected()) {
				cr /= 2;
				cg /= 2;
				cb /= 2;
			}

			BlitCreature(creature_draw_x, creature_draw_y, tile->creature->getLookType(), dir, cr, cg, cb, 255);
		} else {
			BlitCreature(draw_x, draw_y, tile->creature);
		}
		if(show_tooltips && options.show_spawn_creatureslist && position.z == floor) {
			MakeTooltip(draw_x, draw_y, tile->creature->getName(), 0, 0, 0, 255, 255, 255);
		}
	}

	if(show_tooltips) {
		if(location->getWaypointCount() > 0)
			MakeTooltip(draw_x, draw_y, tooltip.str(), 0, 255, 0);
		else
			MakeTooltip(draw_x, draw_y, tooltip.str());
		tooltip.str("");
	}
}

void MapDrawer::DrawBrushIndicator(int x, int y, Brush* brush, uint8_t r, uint8_t g, uint8_t b)
{
	x += (rme::TileSize / 2);
	y += (rme::TileSize / 2);

	// 7----0----1
	// |         |
	// 6--5  3--2
	//     \/
	//     4
	static int vertexes[9][2] = {
		{-15, -20},  // 0
		{ 15, -20},  // 1
		{ 15, -5},   // 2
		{ 5,  -5},   // 3
		{ 0,   0},   // 4
		{-5,  -5},   // 5
		{-15, -5},   // 6
		{-15, -20},  // 7
		{-15, -20},  // 0
	};

	// circle
	glBegin(GL_TRIANGLE_FAN);
	glColor4ub(0x00, 0x00, 0x00, 0x50);
	glVertex2i(x, y);
	for(int i = 0; i <= 30; i++) {
		float angle = i * 2.0f * rme::PI / 30;
		glVertex2f(cos(angle) * (rme::TileSize / 2) + x, sin(angle) * (rme::TileSize / 2) + y);
	}
	glEnd();

	// background
	glColor4ub(r, g, b, 0xB4);
	glBegin(GL_POLYGON);
	for(int i = 0; i < 8; ++i)
		glVertex2i(vertexes[i][0] + x, vertexes[i][1] + y);
	glEnd();

	// borders
	glColor4ub(0x00, 0x00, 0x00, 0xB4);
	glLineWidth(1.0);
	glBegin(GL_LINES);
	for(int i = 0; i < 8; ++i) {
		glVertex2i(vertexes[i][0] + x, vertexes[i][1] + y);
		glVertex2i(vertexes[i + 1][0] + x, vertexes[i + 1][1] + y);
	}
	glEnd();
}

void MapDrawer::DrawHookIndicator(int x, int y, const ItemType& type)
{
	glDisable(GL_TEXTURE_2D);
	glColor4ub(uint8_t(0), uint8_t(0), uint8_t(255), uint8_t(200));
	glBegin(GL_QUADS);
	if(type.hookSouth) {
		x -= 10;
		y += 10;
		glVertex2f(x, y);
		glVertex2f(x + 10, y);
		glVertex2f(x + 20, y + 10);
		glVertex2f(x + 10, y + 10);
	} else if(type.hookEast) {
		x += 10;
		y -= 10;
		glVertex2f(x, y);
		glVertex2f(x + 10, y + 10);
		glVertex2f(x + 10, y + 20);
		glVertex2f(x, y + 10);
	}
	glEnd();
	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawTileIndicators(TileLocation* location)
{
	if(!location)
		return;

	Tile* tile = location->get();
	if(!tile)
		return;

	int x, y;
	getDrawPosition(location->getPosition(), x, y);

	if((zoom < 10.0 || options.full_detail_zoom_out) && (options.show_pickupables || options.show_moveables)) {
		uint8_t red = 0xFF, green = 0xFF, blue = 0xFF;
		if(tile->isHouseTile()) {
			green = 0x00;
			blue = 0x00;
		}
		for(const Item* item : tile->items) {
			const ItemType& type = g_items.getItemType(item->getID());
			if((type.pickupable && options.show_pickupables) || (type.moveable && options.show_moveables)) {
				if(type.pickupable && options.show_pickupables && type.moveable && options.show_moveables)
					DrawIndicator(x, y, EDITOR_SPRITE_PICKUPABLE_MOVEABLE_ITEM, red, green, blue);
				else if(type.pickupable && options.show_pickupables)
					DrawIndicator(x, y, EDITOR_SPRITE_PICKUPABLE_ITEM, red, green, blue);
				else if(type.moveable && options.show_moveables)
					DrawIndicator(x, y, EDITOR_SPRITE_MOVEABLE_ITEM, red, green, blue);
			}
		}
	}

	if(options.show_houses && tile->isHouseExit()) {
		if(tile->hasHouseExit(current_house_id)) {
			DrawIndicator(x, y, EDITOR_SPRITE_HOUSE_EXIT);
		} else {
			DrawIndicator(x, y, EDITOR_SPRITE_HOUSE_EXIT, 64, 64, 255, 128);
		}
	}

	if(options.show_spawns && tile->spawn) {
		const Position& spawnPosition = tile->getPosition();
		if(spawnPosition.z != floor) {
			return;
		}
		int32_t radius = tile->spawn->getSize();
		int start_map_x = spawnPosition.x - radius;
		int start_map_y = spawnPosition.y - radius;
		int end_map_x   = spawnPosition.x + radius + 1;
		int end_map_y   = spawnPosition.y + radius + 1;
		if(options.show_spawn_overlays) {
			spawn_overlays.push_back(std::make_tuple(start_map_x, start_map_y, end_map_x, end_map_y, radius, tile->spawn->isSelected()));
		} else {
			DrawSpawnAreaFill(start_map_x, start_map_y, end_map_x, end_map_y);
		}
		
		if(options.show_spawn_creatureslist) {
			std::unordered_map<std::string, int32_t> creatureCount;

			for (int32_t dy = -radius; dy <= radius; ++dy) {
				for (int32_t dx = -radius; dx <= radius; ++dx) {
					Tile* creature_tile = editor.getMap().getTile(spawnPosition + Position(dx, dy, 0));
					if (creature_tile) {
						Creature* creature = creature_tile->creature;
						if (creature) {
							++creatureCount[creature->getName()];
						}
					}
				}
			}

			std::string monsterText = "";
			for (const auto& creature : creatureCount) {
				monsterText += creature.first + " " + std::to_string(creature.second) + "x\n";
			}

			MakeTooltip(x, y, monsterText, 0, 0, 0, 255, 255, 0);
		}
	}

	if(options.show_creature_wander_radius && tile->creature) {
		CreatureType* cType = g_creatures[tile->creature->getName()];
		if(cType && cType->hasWanderBehavior()) {
			const Position& creaturePos = tile->getPosition();
			if(creaturePos.z == floor) {
				int radius = cType->wander_radius;
				int start_x = creaturePos.x - radius;
				int start_y = creaturePos.y - radius;
				int end_x = creaturePos.x + radius + 1;
				int end_y = creaturePos.y + radius + 1;
				wander_overlays.push_back(std::make_tuple(start_x, start_y, end_x, end_y, radius, tile->creature->isSelected()));
			}
		}
	}
}

void MapDrawer::DrawSpawnAreaOverlay(int start_map_x, int start_map_y, int end_map_x, int end_map_y, int radius, bool full_visibility)
{
	int adjustment = getFloorAdjustment(floor);
	int start_sx = start_map_x * rme::TileSize - view_scroll_x - adjustment;
	int start_sy = start_map_y * rme::TileSize - view_scroll_y - adjustment;
	int end_sx = end_map_x * rme::TileSize - view_scroll_x - adjustment;
	int end_sy = end_map_y * rme::TileSize - view_scroll_y - adjustment;

	const wxColor outline(220, 0, 220, 220);
	const wxColor box_fill(220, 0, 220, 220);
	const wxColor box_text(255, 255, 255, 255);
	const uint8_t faint_alpha = 60;

	glDisable(GL_TEXTURE_2D);
	glLineWidth(3.0f);

	auto is_walkable = [&](int x, int y) -> bool {
		Tile* tile = editor.getMap().getTile(x, y, floor);
		return tile && tile->hasGround() && !tile->isBlocking();
	};

	glBegin(GL_LINES);
	// Top edge
	for(int x = start_map_x; x < end_map_x; ++x) {
		bool walkable = is_walkable(x, start_map_y);
		uint8_t alpha = full_visibility || walkable ? outline.Alpha() : faint_alpha;
		glColor4ub(outline.Red(), outline.Green(), outline.Blue(), alpha);
		int sx = x * rme::TileSize - view_scroll_x - adjustment;
		int sy = start_map_y * rme::TileSize - view_scroll_y - adjustment;
		glVertex2f(sx, sy);
		glVertex2f(sx + rme::TileSize, sy);
	}
	// Bottom edge
	for(int x = start_map_x; x < end_map_x; ++x) {
		int y = end_map_y - 1;
		bool walkable = is_walkable(x, y);
		uint8_t alpha = full_visibility || walkable ? outline.Alpha() : faint_alpha;
		glColor4ub(outline.Red(), outline.Green(), outline.Blue(), alpha);
		int sx = x * rme::TileSize - view_scroll_x - adjustment;
		int sy = y * rme::TileSize - view_scroll_y - adjustment + rme::TileSize;
		glVertex2f(sx, sy);
		glVertex2f(sx + rme::TileSize, sy);
	}
	// Left edge
	for(int y = start_map_y; y < end_map_y; ++y) {
		bool walkable = is_walkable(start_map_x, y);
		uint8_t alpha = full_visibility || walkable ? outline.Alpha() : faint_alpha;
		glColor4ub(outline.Red(), outline.Green(), outline.Blue(), alpha);
		int sx = start_map_x * rme::TileSize - view_scroll_x - adjustment;
		int sy = y * rme::TileSize - view_scroll_y - adjustment;
		glVertex2f(sx, sy);
		glVertex2f(sx, sy + rme::TileSize);
	}
	// Right edge
	for(int y = start_map_y; y < end_map_y; ++y) {
		int x = end_map_x - 1;
		bool walkable = is_walkable(x, y);
		uint8_t alpha = full_visibility || walkable ? outline.Alpha() : faint_alpha;
		glColor4ub(outline.Red(), outline.Green(), outline.Blue(), alpha);
		int sx = x * rme::TileSize - view_scroll_x - adjustment + rme::TileSize;
		int sy = y * rme::TileSize - view_scroll_y - adjustment;
		glVertex2f(sx, sy);
		glVertex2f(sx, sy + rme::TileSize);
	}
	glEnd();

	int center_box_size = rme::TileSize;
	int center_box_x = start_sx + ((end_sx - start_sx) - center_box_size) / 2;
	int center_box_y = start_sy + ((end_sy - start_sy) - center_box_size) / 2;
	drawFilledRect(center_box_x, center_box_y, center_box_size, center_box_size, box_fill);
	drawRect(center_box_x, center_box_y, center_box_size, center_box_size, *wxWHITE, 1);

	const char* center_text = "S";
	int center_text_width = 0;
	for(const char* c = center_text; *c != '\0'; ++c) {
		center_text_width += glutBitmapWidth(GLUT_BITMAP_8_BY_13, *c);
	}
	float center_text_x = center_box_x + (center_box_size - center_text_width * zoom) / 2.0f;
	float center_text_y = center_box_y + (center_box_size + 8 * zoom) / 2.0f;
	glColor4ub(0, 0, 0, 255);
	const float outline_offsets[4][2] = {
		{-1.0f, 0.0f},
		{1.0f, 0.0f},
		{0.0f, -1.0f},
		{0.0f, 1.0f},
	};
	for(const auto& offset : outline_offsets) {
		glRasterPos2f(center_text_x + offset[0], center_text_y + offset[1]);
		for(const char* c = center_text; *c != '\0'; ++c) {
			glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
		}
	}
	glColor4ub(box_text.Red(), box_text.Green(), box_text.Blue(), box_text.Alpha());
	glRasterPos2f(center_text_x, center_text_y);
	for(const char* c = center_text; *c != '\0'; ++c) {
		glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
	}

	int box_size = rme::TileSize;
	int box_x = end_sx - box_size;
	int box_y = end_sy - box_size;

	drawFilledRect(box_x, box_y, box_size, box_size, box_fill);
	drawRect(box_x, box_y, box_size, box_size, *wxWHITE, 1);

	std::string size_text = std::to_string(radius);
	int text_width = 0;
	for(char c : size_text) {
		text_width += glutBitmapWidth(GLUT_BITMAP_8_BY_13, c);
	}

	float text_x = box_x + (box_size - text_width * zoom) / 2.0f;
	float text_y = box_y + (box_size + 8 * zoom) / 2.0f;
	glColor4ub(box_text.Red(), box_text.Green(), box_text.Blue(), box_text.Alpha());
	glRasterPos2f(text_x, text_y);
	for(char c : size_text) {
		glutBitmapCharacter(GLUT_BITMAP_8_BY_13, c);
	}

	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawSpawnAreaFill(int start_map_x, int start_map_y, int end_map_x, int end_map_y)
{
	glDisable(GL_TEXTURE_2D);
	const wxColor fill(200, 0, 0, 80);
	const wxColor fill_blocked(200, 0, 0, 40);

	for(int y = start_map_y; y < end_map_y; ++y) {
		for(int x = start_map_x; x < end_map_x; ++x) {
			Tile* tile = editor.getMap().getTile(x, y, floor);
			const bool walkable = !(tile && tile->isBlocking());
			int sx, sy;
			getDrawPosition(Position(x, y, floor), sx, sy);
			drawFilledRect(sx, sy, rme::TileSize, rme::TileSize, walkable ? fill : fill_blocked);
		}
	}
	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawSpawnOverlays()
{
	if(spawn_overlays.empty())
		return;

	for(const auto& overlay : spawn_overlays) {
		DrawSpawnAreaOverlay(
			std::get<0>(overlay),
			std::get<1>(overlay),
			std::get<2>(overlay),
			std::get<3>(overlay),
			std::get<4>(overlay),
			std::get<5>(overlay)
		);
	}
}

void MapDrawer::DrawWanderRadiusOverlays()
{
	if(wander_overlays.empty())
		return;

	for(const auto& overlay : wander_overlays) {
		DrawWanderRadiusOverlay(
			std::get<0>(overlay),
			std::get<1>(overlay),
			std::get<2>(overlay),
			std::get<3>(overlay),
			std::get<4>(overlay),
			std::get<5>(overlay)
		);
	}
}

void MapDrawer::DrawWanderRadiusOverlay(int start_map_x, int start_map_y, int end_map_x, int end_map_y, int radius, bool selected)
{
	int adjustment = getFloorAdjustment(floor);
	int start_sx = start_map_x * rme::TileSize - view_scroll_x - adjustment;
	int start_sy = start_map_y * rme::TileSize - view_scroll_y - adjustment;
	int end_sx = end_map_x * rme::TileSize - view_scroll_x - adjustment;
	int end_sy = end_map_y * rme::TileSize - view_scroll_y - adjustment;

	// Cyan/teal color to distinguish from spawn overlays (magenta)
	const wxColor outline(0, 180, 220, selected ? 255 : 180);
	const wxColor box_fill(0, 180, 220, selected ? 220 : 180);
	const wxColor box_text(255, 255, 255, 255);

	glDisable(GL_TEXTURE_2D);
	glLineWidth(selected ? 3.0f : 2.0f);

	// Draw border outline
	glBegin(GL_LINES);
	// Top edge
	for(int x = start_map_x; x < end_map_x; ++x) {
		glColor4ub(outline.Red(), outline.Green(), outline.Blue(), outline.Alpha());
		int sx = x * rme::TileSize - view_scroll_x - adjustment;
		int sy = start_map_y * rme::TileSize - view_scroll_y - adjustment;
		glVertex2f(sx, sy);
		glVertex2f(sx + rme::TileSize, sy);
	}
	// Bottom edge
	for(int x = start_map_x; x < end_map_x; ++x) {
		int y = end_map_y - 1;
		glColor4ub(outline.Red(), outline.Green(), outline.Blue(), outline.Alpha());
		int sx = x * rme::TileSize - view_scroll_x - adjustment;
		int sy = y * rme::TileSize - view_scroll_y - adjustment + rme::TileSize;
		glVertex2f(sx, sy);
		glVertex2f(sx + rme::TileSize, sy);
	}
	// Left edge
	for(int y = start_map_y; y < end_map_y; ++y) {
		glColor4ub(outline.Red(), outline.Green(), outline.Blue(), outline.Alpha());
		int sx = start_map_x * rme::TileSize - view_scroll_x - adjustment;
		int sy = y * rme::TileSize - view_scroll_y - adjustment;
		glVertex2f(sx, sy);
		glVertex2f(sx, sy + rme::TileSize);
	}
	// Right edge
	for(int y = start_map_y; y < end_map_y; ++y) {
		int x = end_map_x - 1;
		glColor4ub(outline.Red(), outline.Green(), outline.Blue(), outline.Alpha());
		int sx = x * rme::TileSize - view_scroll_x - adjustment + rme::TileSize;
		int sy = y * rme::TileSize - view_scroll_y - adjustment;
		glVertex2f(sx, sy);
		glVertex2f(sx, sy + rme::TileSize);
	}
	glEnd();

	// Draw center box with "W" label
	int center_box_size = rme::TileSize;
	int center_box_x = start_sx + ((end_sx - start_sx) - center_box_size) / 2;
	int center_box_y = start_sy + ((end_sy - start_sy) - center_box_size) / 2;
	drawFilledRect(center_box_x, center_box_y, center_box_size, center_box_size, box_fill);
	drawRect(center_box_x, center_box_y, center_box_size, center_box_size, *wxWHITE, 1);

	const char* center_text = "W";
	int center_text_width = 0;
	for(const char* c = center_text; *c != '\0'; ++c) {
		center_text_width += glutBitmapWidth(GLUT_BITMAP_8_BY_13, *c);
	}
	float center_text_x = center_box_x + (center_box_size - center_text_width * zoom) / 2.0f;
	float center_text_y = center_box_y + (center_box_size + 8 * zoom) / 2.0f;
	// Draw text outline in black
	glColor4ub(0, 0, 0, 255);
	const float outline_offsets[4][2] = {
		{-1.0f, 0.0f},
		{1.0f, 0.0f},
		{0.0f, -1.0f},
		{0.0f, 1.0f},
	};
	for(const auto& offset : outline_offsets) {
		glRasterPos2f(center_text_x + offset[0], center_text_y + offset[1]);
		for(const char* c = center_text; *c != '\0'; ++c) {
			glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
		}
	}
	glColor4ub(box_text.Red(), box_text.Green(), box_text.Blue(), box_text.Alpha());
	glRasterPos2f(center_text_x, center_text_y);
	for(const char* c = center_text; *c != '\0'; ++c) {
		glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
	}

	// Draw radius number box in bottom-right corner
	int box_size = rme::TileSize;
	int box_x = end_sx - box_size;
	int box_y = end_sy - box_size;

	drawFilledRect(box_x, box_y, box_size, box_size, box_fill);
	drawRect(box_x, box_y, box_size, box_size, *wxWHITE, 1);

	std::string size_text = std::to_string(radius);
	int text_width = 0;
	for(char c : size_text) {
		text_width += glutBitmapWidth(GLUT_BITMAP_8_BY_13, c);
	}

	float text_x = box_x + (box_size - text_width * zoom) / 2.0f;
	float text_y = box_y + (box_size + 8 * zoom) / 2.0f;
	glColor4ub(box_text.Red(), box_text.Green(), box_text.Blue(), box_text.Alpha());
	glRasterPos2f(text_x, text_y);
	for(char c : size_text) {
		glutBitmapCharacter(GLUT_BITMAP_8_BY_13, c);
	}

	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawWallBorderLines()
{
	int adjustment = getFloorAdjustment(floor);
	const wxColor border_color(220, 220, 0, 200); // Yellow
	const int mountain_z_order_threshold = 9000; // Mountains have z-order >= 9000
	const int half_tile = rme::TileSize / 2; // 16 pixels - center of tile

	// Helper to check if tile is a mountain (z-order >= 9000)
	auto is_mountain_tile = [&](Tile* t) -> bool {
		if(!t || !t->ground) return false;
		GroundBrush* gb = t->ground->getGroundBrush();
		return gb && gb->getZ() >= mountain_z_order_threshold;
	};

	// Helper to check if tile has a wall
	auto is_wall_tile = [&](Tile* t) -> bool {
		return t && t->hasWall();
	};

	// Helper to detect wall orientation by analyzing pixel density along axes
	// Returns: 0 = unknown, 1 = horizontal, 2 = vertical, 3 = corner (both)
	auto detect_wall_orientation = [&](GameSprite* sprite) -> int {
		if(!sprite || sprite->spriteList.empty()) return 0;
		auto* img = sprite->spriteList[0];
		if(!img) return 0;
		uint8_t* rgba = img->getRGBAData();
		if(!rgba) return 0;

		// Count pixels in each row and column
		int row_counts[32] = {0};
		int col_counts[32] = {0};
		int total_pixels = 0;

		for(int py = 0; py < rme::SpritePixels; ++py) {
			for(int px = 0; px < rme::SpritePixels; ++px) {
				int pixel_index = (py * rme::SpritePixels + px) * 4;
				if(rgba[pixel_index + 3] > 0) {
					row_counts[py]++;
					col_counts[px]++;
					total_pixels++;
				}
			}
		}

		delete[] rgba;

		if(total_pixels == 0) return 0;

		// Find max row density and max column density
		int max_row = 0, max_col = 0;
		int rows_with_pixels = 0, cols_with_pixels = 0;

		for(int i = 0; i < 32; ++i) {
			if(row_counts[i] > max_row) max_row = row_counts[i];
			if(col_counts[i] > max_col) max_col = col_counts[i];
			if(row_counts[i] > 0) rows_with_pixels++;
			if(col_counts[i] > 0) cols_with_pixels++;
		}

		// Calculate "spread" - how many rows/cols have significant pixels
		// A horizontal wall has pixels spread across many columns but few rows
		// A vertical wall has pixels spread across many rows but few columns

		float row_spread = (float)rows_with_pixels / 32.0f;
		float col_spread = (float)cols_with_pixels / 32.0f;

		// If spread is similar in both directions, it's a corner
		float spread_ratio = row_spread / col_spread;

		if(spread_ratio > 1.4f) return 2;      // More rows than cols = Vertical
		if(spread_ratio < 0.7f) return 1;      // More cols than rows = Horizontal
		return 3;                               // Similar spread = Corner
	};

	glDisable(GL_TEXTURE_2D);
	glLineWidth(3.0f);
	glEnable(GL_LINE_STIPPLE);
	glLineStipple(1, 0xFFFE); // Dashed pattern: long dash with small gap
	glBegin(GL_LINES);

	for(int y = start_y; y <= end_y; ++y) {
		for(int x = start_x; x <= end_x; ++x) {
			Tile* tile = editor.getMap().getTile(x, y, floor);
			if(!tile) continue;

			bool is_mountain = is_mountain_tile(tile);
			bool is_wall = is_wall_tile(tile);

			if(!is_mountain && !is_wall) continue;

			int sx = x * rme::TileSize - view_scroll_x - adjustment;
			int sy = y * rme::TileSize - view_scroll_y - adjustment;

			glColor4ub(border_color.Red(), border_color.Green(), border_color.Blue(), border_color.Alpha());

			if(is_mountain) {
				// MOUNTAINS: Draw rectangle border around the group
				// Check neighbors (only mountains count as neighbors for mountains)
				bool neighbor_north = is_mountain_tile(editor.getMap().getTile(x, y - 1, floor));
				bool neighbor_south = is_mountain_tile(editor.getMap().getTile(x, y + 1, floor));
				bool neighbor_west = is_mountain_tile(editor.getMap().getTile(x - 1, y, floor));
				bool neighbor_east = is_mountain_tile(editor.getMap().getTile(x + 1, y, floor));

				// Border rectangle - no offset, aligned with tile position
				float border_left = sx;
				float border_right = sx + rme::TileSize;
				float border_top = sy;
				float border_bottom = sy + rme::TileSize;

				// Draw outer edges only (edges not shared with neighbors)
				if(!neighbor_north) {
					glVertex2f(border_left, border_top);
					glVertex2f(border_right, border_top);
				}
				if(!neighbor_south) {
					glVertex2f(border_left, border_bottom);
					glVertex2f(border_right, border_bottom);
				}
				if(!neighbor_west) {
					glVertex2f(border_left, border_top);
					glVertex2f(border_left, border_bottom);
				}
				if(!neighbor_east) {
					glVertex2f(border_right, border_top);
					glVertex2f(border_right, border_bottom);
				}
			} else if(is_wall) {
				// WALLS: Draw center line based on neighboring walls
				// Check wall neighbors in cardinal directions
				bool wall_north = is_wall_tile(editor.getMap().getTile(x, y - 1, floor));
				bool wall_south = is_wall_tile(editor.getMap().getTile(x, y + 1, floor));
				bool wall_west = is_wall_tile(editor.getMap().getTile(x - 1, y, floor));
				bool wall_east = is_wall_tile(editor.getMap().getTile(x + 1, y, floor));

				float center_x = sx + half_tile;
				float center_y = sy + half_tile;

				// Draw partial lines based on which neighbors exist
				// Horizontal segments
				if(wall_west) {
					// Line from left edge to center
					glVertex2f(sx, center_y);
					glVertex2f(center_x, center_y);
				}
				if(wall_east) {
					// Line from center to right edge
					glVertex2f(center_x, center_y);
					glVertex2f(sx + rme::TileSize, center_y);
				}
				// Vertical segments
				if(wall_north) {
					// Line from top edge to center
					glVertex2f(center_x, sy);
					glVertex2f(center_x, center_y);
				}
				if(wall_south) {
					// Line from center to bottom edge
					glVertex2f(center_x, center_y);
					glVertex2f(center_x, sy + rme::TileSize);
				}

				// If isolated wall (no neighbors), draw full horizontal line
				if(!wall_north && !wall_south && !wall_west && !wall_east) {
					glVertex2f(sx, center_y);
					glVertex2f(sx + rme::TileSize, center_y);
				}
			}

		}
	}

	glEnd();
	glDisable(GL_LINE_STIPPLE);
	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawMountainOverlay()
{
	int adjustment = getFloorAdjustment(floor);
	const int mountain_z_order_threshold = 9000; // Mountains have z-order >= 9000

	// Helper to check if tile has mountain ground
	auto is_mountain_tile = [&](Tile* t) -> bool {
		if(!t || !t->ground) return false;
		GroundBrush* gb = t->ground->getGroundBrush();
		return gb && gb->getZ() >= mountain_z_order_threshold;
	};

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	for(int y = start_y; y <= end_y; ++y) {
		for(int x = start_x; x <= end_x; ++x) {
			Tile* tile = editor.getMap().getTile(x, y, floor);
			if(!tile || !is_mountain_tile(tile)) continue;

			// Mountain ground at (x,y) renders visually at (x+1, y+1) due to isometric offset
			int draw_x = x + 1;
			int draw_y = y + 1;

			int sx = draw_x * rme::TileSize - view_scroll_x - adjustment;
			int sy = draw_y * rme::TileSize - view_scroll_y - adjustment;

			// Draw semi-transparent overlay (dark with alpha)
			glColor4ub(0, 0, 0, 128);
			glBegin(GL_QUADS);
			glVertex2f(sx, sy);
			glVertex2f(sx + rme::TileSize, sy);
			glVertex2f(sx + rme::TileSize, sy + rme::TileSize);
			glVertex2f(sx, sy + rme::TileSize);
			glEnd();
		}
	}

	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawStairDirections()
{
	int adjustment = getFloorAdjustment(floor);
	const int half_tile = rme::TileSize / 2;

	// VERY LARGE arrow dimensions
	const float body_length = 22.0f;  // Length of the arrow body
	const float body_width = 14.0f;   // Width of the arrow body
	const float head_length = 16.0f;  // Length of the arrow head
	const float head_width = 26.0f;   // Width of the arrow head (wider than body)

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Helper lambda to draw arrow shape (body + head) like the reference image
	auto drawArrow = [&](float cx, float cy, int direction, bool is_outline) {
		// direction: 0=north, 1=south, 2=east, 3=west, 4=down
		float outline_offset = is_outline ? 3.0f : 0.0f;
		float bl = body_length + outline_offset;
		float bw = (body_width + outline_offset) / 2.0f;
		float hl = head_length + outline_offset;
		float hw = (head_width + outline_offset) / 2.0f;

		// Calculate total arrow length
		float total_len = bl + hl;
		float start_offset = total_len / 2.0f;

		switch(direction) {
			case 0: // North (up)
				// Arrow body (rectangle)
				glBegin(GL_QUADS);
				glVertex2f(cx - bw, cy + start_offset);
				glVertex2f(cx + bw, cy + start_offset);
				glVertex2f(cx + bw, cy + start_offset - bl);
				glVertex2f(cx - bw, cy + start_offset - bl);
				glEnd();
				// Arrow head (triangle)
				glBegin(GL_TRIANGLES);
				glVertex2f(cx, cy - start_offset);
				glVertex2f(cx - hw, cy + start_offset - bl);
				glVertex2f(cx + hw, cy + start_offset - bl);
				glEnd();
				break;

			case 1: // South (down)
			case 4: // Down (same visual)
				glBegin(GL_QUADS);
				glVertex2f(cx - bw, cy - start_offset);
				glVertex2f(cx + bw, cy - start_offset);
				glVertex2f(cx + bw, cy - start_offset + bl);
				glVertex2f(cx - bw, cy - start_offset + bl);
				glEnd();
				glBegin(GL_TRIANGLES);
				glVertex2f(cx, cy + start_offset);
				glVertex2f(cx - hw, cy - start_offset + bl);
				glVertex2f(cx + hw, cy - start_offset + bl);
				glEnd();
				break;

			case 2: // East (right)
				glBegin(GL_QUADS);
				glVertex2f(cx - start_offset, cy - bw);
				glVertex2f(cx - start_offset, cy + bw);
				glVertex2f(cx - start_offset + bl, cy + bw);
				glVertex2f(cx - start_offset + bl, cy - bw);
				glEnd();
				glBegin(GL_TRIANGLES);
				glVertex2f(cx + start_offset, cy);
				glVertex2f(cx - start_offset + bl, cy - hw);
				glVertex2f(cx - start_offset + bl, cy + hw);
				glEnd();
				break;

			case 3: // West (left)
				glBegin(GL_QUADS);
				glVertex2f(cx + start_offset, cy - bw);
				glVertex2f(cx + start_offset, cy + bw);
				glVertex2f(cx + start_offset - bl, cy + bw);
				glVertex2f(cx + start_offset - bl, cy - bw);
				glEnd();
				glBegin(GL_TRIANGLES);
				glVertex2f(cx - start_offset, cy);
				glVertex2f(cx + start_offset - bl, cy - hw);
				glVertex2f(cx + start_offset - bl, cy + hw);
				glEnd();
				break;

			default:
				return;
		}
	};

	// Collect all stairs to draw - ONLY CURRENT FLOOR
	struct StairInfo {
		float cx, cy;
		int direction;
	};
	std::vector<StairInfo> stairs;

	for(int y = start_y; y <= end_y; ++y) {
		for(int x = start_x; x <= end_x; ++x) {
			Tile* tile = editor.getMap().getTile(x, y, floor);
			if(!tile) continue;

			for(Item* item : tile->items) {
				if(!item) continue;
				const ItemType& type = g_items.getItemType(item->getID());
				if(!type.isFloorChange()) continue;

				int sx = x * rme::TileSize - view_scroll_x - adjustment;
				int sy = y * rme::TileSize - view_scroll_y - adjustment;
				float cx = sx + half_tile;
				float cy = sy + half_tile;

				int direction = -1;

				if(type.floorChangeNorth) direction = 0;
				else if(type.floorChangeSouth) direction = 1;
				else if(type.floorChangeEast) direction = 2;
				else if(type.floorChangeWest) direction = 3;
				else if(type.floorChangeDown) direction = 4;

				if(direction >= 0) {
					stairs.push_back({cx, cy, direction});
				}

				break; // Only one arrow per tile
			}
		}
	}

	// Draw black outlines first (behind)
	glColor4ub(0, 0, 0, 255);
	for(const auto& stair : stairs) {
		drawArrow(stair.cx, stair.cy, stair.direction, true);
	}

	// Draw red fill on top (like the reference image)
	glColor4ub(220, 50, 50, 255);
	for(const auto& stair : stairs) {
		drawArrow(stair.cx, stair.cy, stair.direction, false);
	}

	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::DrawIndicator(int x, int y, int indicator, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	GameSprite* sprite = g_gui.gfx.getEditorSprite(indicator);
	if(sprite == nullptr)
		return;

	int textureId = sprite->getHardwareID(0,0,0,-1,0,0,0,0);
	glBlitTexture(x, y, textureId, r, g, b, a, true);
}

void MapDrawer::DrawPositionIndicator(int z)
{
	if(z != pos_indicator.z
		|| pos_indicator.x < start_x
		|| pos_indicator.x > end_x
		|| pos_indicator.y < start_y
		|| pos_indicator.y > end_y) {
		return;
	}

	const long time = GetPositionIndicatorTime();
	if(time == 0) {
		return;
	}

	int x, y;
	getDrawPosition(pos_indicator, x, y);

	int size = static_cast<int>(rme::TileSize * (0.3f + std::abs(500 - time % 1000) / 1000.f));
	int offset = (rme::TileSize - size) / 2;

	glDisable(GL_TEXTURE_2D);
	drawRect(x + offset + 2, y + offset + 2, size - 4, size - 4, *wxWHITE, 2);
	drawRect(x + offset + 1, y + offset + 1, size - 2, size - 2, *wxBLACK, 2);
	glEnable(GL_TEXTURE_2D);
}

bool MapDrawer::HasActiveSelectionIndicator() const
{
	if(options.ingame || !options.show_selected_tile_indicator) {
		return false;
	}
	if(!canvas->cursor_in_window) {
		return false;
	}
	if(canvas->isPasting()) {
		return true;
	}
	if(g_gui.IsDrawingMode() && g_gui.GetCurrentBrush() != nullptr) {
		return true;
	}
	return false;
}

void MapDrawer::DrawSelectedTileIndicator(int x, int y)
{
	glDisable(GL_TEXTURE_2D);
	const uint8_t alpha = GetSelectionIndicatorAlpha();
	wxColour overlay(255, 215, 0, alpha);
	drawFilledRect(x, y, rme::TileSize, rme::TileSize, overlay);
	drawRect(x, y, rme::TileSize, rme::TileSize, *wxWHITE, 1);
	glEnable(GL_TEXTURE_2D);
}

uint8_t MapDrawer::GetSelectionIndicatorAlpha() const
{
	const long elapsed = selection_indicator_timer.Time() % 1000;
	const float normalized = static_cast<float>(elapsed) / 1000.f;
	float wave = normalized < 0.5f ? normalized * 2.0f : (1.0f - normalized) * 2.0f;
	int alpha = int(80 + wave * 120.0f);
	if(alpha < 0)
		alpha = 0;
	else if(alpha > 255)
		alpha = 255;
	return static_cast<uint8_t>(alpha);
}

void MapDrawer::DrawTooltips()
{
	if(!options.show_tooltips || tooltips.empty())
		return;

	glDisable(GL_TEXTURE_2D);

	for(MapTooltip* tooltip : tooltips) {
		const char* text = tooltip->text.c_str();
		float line_width = 0.0f;
		float width = 2.0f;
		float height = 14.0f;
		int char_count = 0;
		int line_char_count = 0;

		for(const char* c = text; *c != '\0'; c++) {
			if(*c == '\n' || (line_char_count >= MapTooltip::MAX_CHARS_PER_LINE && *c == ' ')) {
				height += 14.0f;
				line_width = 0.0f;
				line_char_count = 0;
			} else {
				line_width += glutBitmapWidth(GLUT_BITMAP_HELVETICA_12, *c);
			}
			width = std::max<float>(width, line_width);
			char_count++;
			line_char_count++;

			if(tooltip->ellipsis && char_count > (MapTooltip::MAX_CHARS + 3))
				break;
		}

		float scale = zoom < 1.0f ? zoom : 1.0f;

		width = (width + 8.0f) * scale;
		height = (height + 4.0f) * scale;

		float x = tooltip->x + (rme::TileSize / 2.0f);
		float y = tooltip->y + ((rme::TileSize / 2.0f) * scale);
		float center = width / 2.0f;
		float space = (7.0f * scale);
		float startx = x - center;
		float endx = x + center;
		float starty = y - (height + space);
		float endy = y - space;

		// 7----0----1
		// |         |
		// 6--5  3--2
		//     \/
		//     4
		float vertexes[9][2] = {
			{x,         starty}, // 0
			{endx,      starty}, // 1
			{endx,      endy},   // 2
			{x + space, endy},   // 3
			{x,         y},      // 4
			{x - space, endy},   // 5
			{startx,    endy},   // 6
			{startx,    starty}, // 7
			{x,         starty}, // 0
		};

		// background
		glColor4ub(tooltip->r, tooltip->g, tooltip->b, 255);
		glBegin(GL_POLYGON);
		for(int i = 0; i < 8; ++i)
			glVertex2f(vertexes[i][0], vertexes[i][1]);
		glEnd();

		// borders
		glColor4ub(0, 0, 0, 255);
		glLineWidth(1.0);
		glBegin(GL_LINES);
		for(int i = 0; i < 8; ++i) {
			glVertex2f(vertexes[i][0], vertexes[i][1]);
			glVertex2f(vertexes[i + 1][0], vertexes[i + 1][1]);
		}
		glEnd();

		// text
		if(zoom <= 1.0) {
			startx += (3.0f * scale);
			starty += (14.0f * scale);
			glColor4ub(tooltip->tr, tooltip->tg, tooltip->tb, 255);
			glRasterPos2f(startx, starty);
			char_count = 0;
			line_char_count = 0;
			for(const char* c = text; *c != '\0'; c++) {
				if(*c == '\n' || (line_char_count >= MapTooltip::MAX_CHARS_PER_LINE && *c == ' ')) {
					starty += (14.0f * scale);
					glRasterPos2f(startx, starty);
					line_char_count = 0;
				}
				char_count++;
				line_char_count++;

				if(tooltip->ellipsis && char_count >= MapTooltip::MAX_CHARS) {
					glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, '.');
					if(char_count >= (MapTooltip::MAX_CHARS + 2))
						break;
				} else if(!iscntrl(*c)) {
					glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
				}
			}
		}
	}

	glEnable(GL_TEXTURE_2D);
}

void MapDrawer::MakeTooltip(int screenx, int screeny, const std::string& text, uint8_t r, uint8_t g, uint8_t b, uint8_t tr, uint8_t tg, uint8_t tb)
{
	if(text.empty())
		return;

	MapTooltip *tooltip = new MapTooltip(screenx, screeny, text, r, g, b, tr, tg, tb);
	tooltip->checkLineEnding();
	tooltips.push_back(tooltip);
}

void MapDrawer::AddLight(TileLocation* location)
{
	if(!options.isDrawLight() || !location) {
		return;
	}

	auto tile = location->get();
	if(!tile) {
		return;
	}

	auto& position = location->getPosition();

	if(tile->ground) {
		if (tile->ground->hasLight()) {
			light_drawer->addLight(position.x, position.y, tile->ground->getLight());
		}
	}

	bool hidden = options.hide_items_when_zoomed && zoom > 10.f && !options.full_detail_zoom_out;
	if(!hidden && !tile->items.empty()) {
		for(auto item : tile->items) {
			if(item->hasLight()) {
				light_drawer->addLight(position.x, position.y, item->getLight());
			}
		}
	}
}

void MapDrawer::getColor(Brush* brush, const Position& position, uint8_t &r, uint8_t &g, uint8_t &b)
{
	if(brush->canDraw(&editor.getMap(), position)) {
		if(brush->isWaypoint()) {
			r = 0x00; g = 0xff, b = 0x00;
		} else {
			r = 0x00; g = 0x00, b = 0xff;
		}
	} else {
		r = 0xff; g = 0x00, b = 0x00;
	}
}

void MapDrawer::TakeScreenshot(uint8_t* screenshot_buffer)
{
	glFinish(); // Wait for the operation to finish

	glPixelStorei(GL_PACK_ALIGNMENT, 1); // 1 byte alignment

	for(int i = 0; i < screensize_y; ++i)
		glReadPixels(0, screensize_y - i, screensize_x, 1, GL_RGB, GL_UNSIGNED_BYTE, (GLubyte*)(screenshot_buffer) + 3*screensize_x*i);
}

void MapDrawer::ShowPositionIndicator(const Position& position)
{
	pos_indicator = position;
	pos_indicator_timer.Start();
}

void MapDrawer::glBlitTexture(int x, int y, int textureId, int red, int green, int blue, int alpha, bool adjustZoom)
{
	if(textureId <= 0)
		return;

	glBindTexture(GL_TEXTURE_2D, textureId);
	glColor4ub(uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha));
	glBegin(GL_QUADS);

	if(adjustZoom) {
		float size = rme::TileSize;
		if(zoom < 1.0f) {
			float offset = 10 / (10 * zoom);
			size = std::max<float>(16, rme::TileSize * zoom);
			x += offset;
			y += offset;
		} else if(zoom > 1.f) {
			float offset = (10 * zoom);
			size = rme::TileSize + offset;
			x -= offset;
			y -= offset;
		}
		glTexCoord2f(0.f, 0.f); glVertex2f(x, y);
		glTexCoord2f(1.f, 0.f); glVertex2f(x + size, y);
		glTexCoord2f(1.f, 1.f); glVertex2f(x + size, y + size);
		glTexCoord2f(0.f, 1.f); glVertex2f(x, y + size);
	} else {
		glTexCoord2f(0.f, 0.f); glVertex2f(x, y);
		glTexCoord2f(1.f, 0.f); glVertex2f(x + rme::TileSize, y);
		glTexCoord2f(1.f, 1.f); glVertex2f(x + rme::TileSize, y + rme::TileSize);
		glTexCoord2f(0.f, 1.f); glVertex2f(x, y + rme::TileSize);
	}

	glEnd();
}

void MapDrawer::glBlitSquare(int x, int y, int red, int green, int blue, int alpha)
{
	glColor4ub(uint8_t(red), uint8_t(green), uint8_t(blue), uint8_t(alpha));
	glBegin(GL_QUADS);
		glVertex2f(x, y);
		glVertex2f(x + rme::TileSize, y);
		glVertex2f(x + rme::TileSize, y + rme::TileSize);
		glVertex2f(x, y + rme::TileSize);
	glEnd();
}

void MapDrawer::glBlitSquare(int x, int y, const wxColor& color)
{
	glColor4ub(color.Red(), color.Green(), color.Blue(), color.Alpha());
	glBegin(GL_QUADS);
		glVertex2f(x, y);
		glVertex2f(x + rme::TileSize, y);
		glVertex2f(x + rme::TileSize, y + rme::TileSize);
		glVertex2f(x, y + rme::TileSize);
	glEnd();
}

void MapDrawer::glColor(const wxColor& color)
{
	glColor4ub(color.Red(), color.Green(), color.Blue(), color.Alpha());
}

void MapDrawer::glColor(MapDrawer::BrushColor color)
{
	switch(color) {
		case COLOR_BRUSH:
			glColor4ub(
				g_settings.getInteger(Config::CURSOR_RED),
				g_settings.getInteger(Config::CURSOR_GREEN),
				g_settings.getInteger(Config::CURSOR_BLUE),
				g_settings.getInteger(Config::CURSOR_ALPHA)
			);
			break;

		case COLOR_FLAG_BRUSH:
		case COLOR_HOUSE_BRUSH:
			glColor4ub(
				g_settings.getInteger(Config::CURSOR_ALT_RED),
				g_settings.getInteger(Config::CURSOR_ALT_GREEN),
				g_settings.getInteger(Config::CURSOR_ALT_BLUE),
				g_settings.getInteger(Config::CURSOR_ALT_ALPHA)
			);
			break;

		case COLOR_SPAWN_BRUSH:
			glColor4ub(166, 0, 0, 128);
			break;

		case COLOR_ERASER:
			glColor4ub(166, 0, 0, 128);
			break;

		case COLOR_VALID:
			glColor4ub(0, 166, 0, 128);
			break;

		case COLOR_INVALID:
			glColor4ub(166, 0, 0, 128);
			break;

		default:
			glColor4ub(255, 255, 255, 128);
			break;
	}
}

void MapDrawer::glColorCheck(Brush* brush, const Position& pos)
{
	if(brush->canDraw(&editor.getMap(), pos))
		glColor(COLOR_VALID);
	else
		glColor(COLOR_INVALID);
}

void MapDrawer::drawRect(int x, int y, int w, int h, const wxColor& color, int width)
{
	glLineWidth(width);
	glColor4ub(color.Red(), color.Green(), color.Blue(), color.Alpha());
	glBegin(GL_LINE_STRIP);
		glVertex2f(x, y);
		glVertex2f(x + w, y);
		glVertex2f(x + w, y + h);
		glVertex2f(x, y + h);
		glVertex2f(x, y);
	glEnd();
}

void MapDrawer::drawFilledRect(int x, int y, int w, int h, const wxColor& color)
{
	glColor4ub(color.Red(), color.Green(), color.Blue(), color.Alpha());
	glBegin(GL_QUADS);
		glVertex2f(x, y);
		glVertex2f(x + w, y);
		glVertex2f(x + w, y + h);
		glVertex2f(x, y + h);
	glEnd();
}

void MapDrawer::getDrawPosition(const Position& position, int& x, int& y)
{
	int offset;
	if(position.z <= rme::MapGroundLayer)
		offset = (rme::MapGroundLayer - position.z) * rme::TileSize;
	else
		offset = rme::TileSize * (floor - position.z);

	x = ((position.x * rme::TileSize) - view_scroll_x) - offset;
	y = ((position.y * rme::TileSize) - view_scroll_y) - offset;
}

void MapDrawer::DrawStatsOverlay()
{
	if(canvas && canvas->IsPreviewMode()) {
		return;
	}

	// Calculate FPS
	frame_count++;
	long elapsed = fps_timer.Time();
	if(elapsed >= 1000) {
		current_fps = frame_count * 1000.0 / elapsed;
		frame_count = 0;
		fps_timer.Start();
	}

	// Don't draw stats overlay during screenshot capture (ingame mode)
	if(options.ingame) {
		return;
	}

	// Format text
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "%.0f FPS  %d tiles", current_fps, tiles_rendered);

	// Calculate position (top-right corner) - scale by zoom to stay fixed on screen
	int text_len = strlen(buffer);
	float box_width = (text_len * 8 + 16) * zoom;
	float box_height = 24 * zoom;
	float margin = 10 * zoom;
	float box_x = screensize_x * zoom - box_width - margin;
	float box_y = margin;

	// Draw background box
	glDisable(GL_TEXTURE_2D);
	glColor4ub(30, 30, 40, 200);
	glBegin(GL_QUADS);
		glVertex2f(box_x, box_y);
		glVertex2f(box_x + box_width, box_y);
		glVertex2f(box_x + box_width, box_y + box_height);
		glVertex2f(box_x, box_y + box_height);
	glEnd();

	// Draw text
	glColor4ub(200, 200, 220, 255);
	glRasterPos2f(box_x + 8 * zoom, box_y + 16 * zoom);
	for(const char* c = buffer; *c != '\0'; c++) {
		glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *c);
	}

	glEnable(GL_TEXTURE_2D);
}
