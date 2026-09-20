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

	// Cache de chunks na GPU para as passadas de ground e border
	// (Preferences > Graphics). Desligado, o editor volta a percorrer os tres
	// passes tile a tile na CPU, como antes.
	bool use_chunk_cache;

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

	// Alguma das opcoes que TileColorCalculator::Calculate consulta esta ligada?
	//
	// Serve para pular a chamada por tile: com todas desligadas -- o caso comum --
	// Calculate percorre uma duzia de ifs e devolve o mesmo 255/255/255 que entrou.
	// O TileRenderer chama isso tres vezes por tile (uma por passada de andar), entao
	// o desvio se paga.
	//
	// ATENCAO: opcao nova em TileColorCalculator::Calculate tem de entrar aqui tambem,
	// senao o tint dela simplesmente nao aparece.
	[[nodiscard]] bool hasTileColorModifiers() const noexcept {
		return show_blocking || highlight_items || show_houses
			|| show_sound_zones || show_instance_zones || show_worldboss_zones
			|| show_special_tiles || show_only_colors;
	}

	// Fingerprint of every option the chunk cache bakes into its vertex buffers
	// (ground/border sprite choice, tint and alpha). ChunkCacheManager compares
	// it once per frame and re-bakes everything when it changes, so no option
	// can silently go stale in the cache -- unlike a MarkDirty() call that a
	// future setting might forget to make.
	//
	// ATTENTION: an option that changes how a ground or a border is drawn has
	// to be listed here, or toggling it will not repaint the cached chunks.
	[[nodiscard]] uint64_t chunkBakeSignature() const noexcept {
		uint64_t sig = 0;
		auto bit = [&sig](bool value) noexcept {
			sig = (sig << 1) | (value ? 1u : 0u);
		};
		bit(transparent_items);
		bit(transparent_grounds);
		bit(show_items);
		bit(show_only_grounds);
		bit(show_tech_items);
		bit(show_invalid_tiles);
		bit(ingame);
		bit(show_houses);
		bit(extended_house_shader);
		bit(show_blocking);
		bit(highlight_items);
		bit(show_spawns);
		bit(show_sound_zones);
		bit(show_instance_zones);
		bit(show_worldboss_zones);
		bit(show_special_tiles);
		bit(show_only_colors);
		bit(always_show_zones);
		bit(show_mountain_overlay);
		// Options that turn an item into an indicator/overlay case, which the
		// bake refuses and hands back to the CPU renderer.
		bit(highlight_locked_doors);
		bit(show_hooks);
		bit(show_pickupables);
		bit(show_moveables);
		bit(show_light_str);
		bit(show_invalid_zones);
		bit(show_creatures);
		// The LOD gate, not the raw zoom: only crossing the threshold changes
		// what gets baked, so panning and zooming do not thrash the cache.
		bit(drawLooseItems());
		return (sig << 32) ^ static_cast<uint64_t>(current_house_id);
	}
};

#endif
