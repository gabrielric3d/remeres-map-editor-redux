#include "app/main.h"
#include "ui/gui.h"
#include "rendering/core/drawing_options.h"
#include "rendering/postprocess/post_process_manager.h"

DrawingOptions::DrawingOptions() {
	SetDefault();
}

void DrawingOptions::SetDefault() {
	transparent_floors = false;
	transparent_items = false;
	show_ingame_box = false;
	show_lights = false;
	show_light_str = true;
	show_tech_items = true;
	show_invalid_tiles = true;
	show_invalid_zones = true;
	show_waypoints = true;
	ingame = false;
	dragging = false;
	boundbox_selection = false;
	lasso_selection = false;

	show_grid = 0;
	show_cursor_highlight = true;
	show_all_floors = true;
	show_creatures = true;
	show_creature_names = true;
	show_spawns = true;
	show_houses = true;
	show_shade = true;
	show_special_tiles = true;
	show_items = true;

	highlight_items = false;
	highlight_locked_doors = true;
	show_blocking = false;
	show_tooltips = false;
	show_as_minimap = false;
	show_only_colors = false;
	show_only_modified = false;
	show_only_grounds = false;
	show_preview = false;
	show_hooks = false;
	show_pickupables = false;
	show_moveables = false;
	show_camera_paths = true;
	show_shadow_occlusion = false;
	show_custom_item_lights = true;
	show_forced_light_zones = false;
	show_zone_boundaries = true;
	show_wall_borders = false;
	show_mountain_overlay = false;
	show_stair_direction = false;
	hide_items_when_zoomed = true;
	current_house_id = 0;
	light_intensity = 1.0f;
	ambient_light_level = 0.5f;
	global_light_color = wxColor(128, 128, 128);
	highlight_pulse = 0.0f;
	anti_aliasing = false;
	screen_shader_name = ShaderNames::NONE;
}

void DrawingOptions::SetIngame() {
	transparent_floors = false;
	transparent_items = false;
	show_ingame_box = false;
	show_lights = false;
	show_light_str = false;
	show_tech_items = false;
	show_invalid_tiles = false;
	show_invalid_zones = false;
	show_waypoints = false;
	ingame = true;
	dragging = false;
	boundbox_selection = false;
	lasso_selection = false;

	show_grid = 0;
	show_cursor_highlight = false;
	show_all_floors = true;
	show_creatures = true;
	show_creature_names = true;
	show_spawns = false;
	show_houses = false;
	show_shade = false;
	show_special_tiles = false;
	show_items = true;

	highlight_items = false;
	highlight_locked_doors = false;
	show_blocking = false;
	show_tooltips = false;
	show_as_minimap = false;
	show_only_colors = false;
	show_only_modified = false;
	show_only_grounds = false;
	show_preview = false;
	show_hooks = false;
	show_pickupables = false;
	show_moveables = false;
	show_camera_paths = false;
	show_shadow_occlusion = false;
	show_custom_item_lights = false;
	show_forced_light_zones = false;
	show_zone_boundaries = false;
	show_wall_borders = false;
	show_mountain_overlay = false;
	show_stair_direction = false;
	hide_items_when_zoomed = false;
	current_house_id = 0;
}

#include "app/settings.h"

void DrawingOptions::Update() {
	transparent_floors = g_settings.getBoolean(Config::TRANSPARENT_FLOORS);
	transparent_items = g_settings.getBoolean(Config::TRANSPARENT_ITEMS);
	show_ingame_box = g_settings.getBoolean(Config::SHOW_INGAME_BOX);
	show_lights = g_settings.getBoolean(Config::SHOW_LIGHTS);
	show_light_str = g_settings.getBoolean(Config::SHOW_LIGHT_STR);
	show_tech_items = g_settings.getBoolean(Config::SHOW_TECHNICAL_ITEMS);
	show_invalid_tiles = g_settings.getBoolean(Config::SHOW_INVALID_TILES);
	show_invalid_zones = g_settings.getBoolean(Config::SHOW_INVALID_ZONES);
	show_waypoints = g_settings.getBoolean(Config::SHOW_WAYPOINTS);
	show_grid = g_settings.getInteger(Config::SHOW_GRID);
	show_cursor_highlight = g_settings.getBoolean(Config::SHOW_CURSOR_HIGHLIGHT);
	ingame = !g_settings.getBoolean(Config::SHOW_EXTRA);
	show_all_floors = g_settings.getBoolean(Config::SHOW_ALL_FLOORS);
	show_creatures = g_settings.getBoolean(Config::SHOW_CREATURES);
	show_creature_names = g_settings.getBoolean(Config::SHOW_CREATURE_NAMES);
	show_spawns = g_settings.getBoolean(Config::SHOW_SPAWNS);
	show_houses = g_settings.getBoolean(Config::SHOW_HOUSES);
	show_shade = g_settings.getBoolean(Config::SHOW_SHADE);
	show_special_tiles = g_settings.getBoolean(Config::SHOW_SPECIAL_TILES);
	show_items = g_settings.getBoolean(Config::SHOW_ITEMS);
	highlight_items = g_settings.getBoolean(Config::HIGHLIGHT_ITEMS);
	highlight_locked_doors = g_settings.getBoolean(Config::HIGHLIGHT_LOCKED_DOORS);
	show_blocking = g_settings.getBoolean(Config::SHOW_BLOCKING);
	show_tooltips = g_settings.getBoolean(Config::SHOW_TOOLTIPS);
	show_as_minimap = g_settings.getBoolean(Config::SHOW_AS_MINIMAP);
	show_only_colors = g_settings.getBoolean(Config::SHOW_ONLY_TILEFLAGS);
	show_only_modified = g_settings.getBoolean(Config::SHOW_ONLY_MODIFIED_TILES);
	show_only_grounds = g_settings.getBoolean(Config::SHOW_ONLY_GROUNDS);
	show_preview = g_settings.getBoolean(Config::SHOW_PREVIEW);
	show_hooks = g_settings.getBoolean(Config::SHOW_WALL_HOOKS);
	show_pickupables = g_settings.getBoolean(Config::SHOW_PICKUPABLES);
	show_moveables = g_settings.getBoolean(Config::SHOW_MOVEABLES);
	hide_items_when_zoomed = g_settings.getBoolean(Config::HIDE_ITEMS_WHEN_ZOOMED);
	show_towns = g_settings.getBoolean(Config::SHOW_TOWNS);
	always_show_zones = g_settings.getBoolean(Config::ALWAYS_SHOW_ZONES);
	extended_house_shader = g_settings.getBoolean(Config::EXT_HOUSE_SHADER);
	light_intensity = g_gui.GetLightIntensity();
	ambient_light_level = g_gui.GetAmbientLightLevel();

	show_camera_paths = g_settings.getBoolean(Config::SHOW_CAMERA_PATHS);
	show_shadow_occlusion = g_settings.getBoolean(Config::SHOW_SHADOW_OCCLUSION);
	show_custom_item_lights = g_settings.getBoolean(Config::SHOW_CUSTOM_ITEM_LIGHTS);
	show_forced_light_zones = g_settings.getBoolean(Config::SHOW_FORCED_LIGHT_ZONES);
	show_zone_boundaries = g_settings.getBoolean(Config::SHOW_ZONE_BOUNDARIES);
	show_wall_borders = g_settings.getBoolean(Config::SHOW_WALL_BORDERS);
	show_mountain_overlay = g_settings.getBoolean(Config::SHOW_MOUNTAIN_OVERLAY);
	show_stair_direction = g_settings.getBoolean(Config::SHOW_STAIR_DIRECTION);

	experimental_fog = g_settings.getBoolean(Config::EXPERIMENTAL_FOG);
	anti_aliasing = g_settings.getBoolean(Config::ANTI_ALIASING);
	screen_shader_name = g_settings.getString(Config::SCREEN_SHADER);
}

bool DrawingOptions::isDrawLight() const noexcept {
	return show_lights;
}
