#ifndef RME_RENDERING_DRAWING_OPTIONS_H_
#define RME_RENDERING_DRAWING_OPTIONS_H_

#include <cstdint>
#include <wx/wx.h>
#include <string>
#include <optional>
#include "map/position.h"

struct DrawingOptions {
	DrawingOptions();

	void SetIngame();
	void SetDefault();
	void Update();
	bool isDrawLight() const noexcept;

	// "Hide loose items when zoomed out" ainda deixa desenhar os itens neste zoom?
	//
	// Sao DUAS variantes porque o codigo antigo tinha dois criterios: o TileRenderer
	// comparava com `<` e o FloorDrawer e o PreviewDrawer com `<=`. A diferenca so
	// aparece em zoom exatamente igual ao limiar, mas aparece -- e o zoom pode cair
	// nesse valor cravado por script Lua ou por keyframe de camera. Cada chamador
	// fica com a variante que sempre teve.
	[[nodiscard]] bool drawLooseItems() const noexcept {
		return !hide_items_when_zoomed || zoom < hide_items_zoom;
	}

	[[nodiscard]] bool drawLooseItemsInclusive() const noexcept {
		return !hide_items_when_zoomed || zoom <= hide_items_zoom;
	}

	bool transparent_floors;
	// "Ghost Floors" (radial wheel). Counts already resolved from the settings:
	// 0 = that direction is off, otherwise how many floors to draw translucent.
	bool ghost_floors_enabled;
	int ghost_floors_above;
	int ghost_floors_below;
	int ghost_floors_alpha; // 0..255
	bool ghost_floors_fade; // farther floors get fainter
	bool transparent_items;
	bool transparent_grounds;
	bool show_ingame_box;
	bool show_lights;
	bool show_light_str;
	bool show_tech_items;
	bool show_invalid_tiles;
	bool show_invalid_zones;
	bool show_waypoints;
	bool ingame;
	bool dragging;
	bool boundbox_selection;
	bool lasso_selection;

	std::optional<MapBounds> transient_selection_bounds;

	int show_grid;
	bool show_cursor_highlight;
	bool show_all_floors;
	bool show_creatures;
	bool show_creature_names;
	bool show_spawns;
	bool show_houses;
	bool show_sound_zones; // BlackTalon: tint ambient sound zones by color
	bool show_instance_zones; // BlackTalon: tint instance zones by color
	bool solid_instance_zones; // BlackTalon: opaque fill instead of tint (needs the above)
	bool show_worldboss_zones; // BlackTalon: tint + rotulo "World Boss" nas arenas com a flag 0x40
	bool show_shade;
	bool show_special_tiles;
	bool show_items;

	bool highlight_items;
	bool highlight_locked_doors;
	bool show_blocking;
	bool show_tooltips;

	bool show_as_minimap;
	bool show_only_colors;
	bool show_only_modified;
	bool show_only_grounds;
	bool show_preview;
	bool show_hooks;
	bool show_pickupables;
	bool show_moveables;
	bool hide_items_when_zoomed;
	// Zoom a partir do qual o acima esconde os itens, ja convertido da porcentagem
	// da preferencia: 10% -> 10.0. Comparar com `zoom` direto, sem dividir nada.
	float hide_items_zoom;
	bool show_towns;
	bool always_show_zones;
	bool extended_house_shader;

	bool show_camera_paths;

	bool show_shadow_occlusion;
	bool show_custom_item_lights;
	bool show_forced_light_zones;
	bool show_zone_boundaries;

	bool show_wall_borders;
	bool show_mountain_overlay;
	bool show_stair_direction;

	bool experimental_fog;

	uint32_t current_house_id;
	wxColor global_light_color;
	float light_intensity;
	float ambient_light_level;
	float highlight_pulse;

	bool anti_aliasing;

	// Copia do zoom de RenderView, atualizada uma vez por frame em
	// MapDrawer::SetupVars. Existe porque os drawers de item/tile so recebem
	// `options`, e sem isso nao teriam como aplicar LOD.
	float zoom;

	std::string screen_shader_name;
};

#endif
