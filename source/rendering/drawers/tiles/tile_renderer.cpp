#include "app/main.h"
#include "rendering/drawers/tiles/tile_renderer.h"
#include "rendering/core/sprite_batch.h"
#include "rendering/core/primitive_renderer.h"
#include "ui/gui.h"

#include "editor/editor.h"
#include "map/tile.h"
#include "game/item.h"
#include "brushes/waypoint/waypoint_brush.h"
#include "game/complexitem.h"

#include "rendering/core/drawing_options.h"
#include "rendering/core/render_view.h"
#include "rendering/drawers/tiles/tile_color_calculator.h"
#include "app/definitions.h"
#include "game/sprites.h"

#include "rendering/drawers/entities/item_drawer.h"
#include "rendering/drawers/entities/sprite_drawer.h"
#include "rendering/drawers/entities/creature_drawer.h"
#include "rendering/drawers/entities/creature_name_drawer.h"
#include "rendering/drawers/tiles/floor_drawer.h"
#include "rendering/drawers/overlays/marker_drawer.h"
#include "rendering/ui/tooltip_drawer.h"
#include "rendering/core/sprite_preloader.h"
#include "rendering/utilities/pattern_calculator.h"
#include "rendering/core/render_timer.h"
#include "rendering/core/render_order.h"

TileRenderer::TileRenderer(ItemDrawer* id, SpriteDrawer* sd, CreatureDrawer* cd, CreatureNameDrawer* cnd, FloorDrawer* fd, MarkerDrawer* md, TooltipDrawer* td, Editor* ed) :
	item_drawer(id), sprite_drawer(sd), creature_drawer(cd), floor_drawer(fd), marker_drawer(md), tooltip_drawer(td), creature_name_drawer(cnd), editor(ed) {
}

static DrawColor invalidTileOverlayColor(InvalidOTBMItemMarkerColor markerColor, bool selected) {
	uint8_t red = 255;
	uint8_t green = 0;
	uint8_t blue = 0;

	if (markerColor == InvalidOTBMItemMarkerColor::Orange) {
		green = 165;
	}

	if (selected) {
		red = static_cast<uint8_t>(red / 2);
		green = static_cast<uint8_t>(green / 2);
		blue = static_cast<uint8_t>(blue / 2);
	}

	return DrawColor(red, green, blue, 171);
}

void TileRenderer::DrawTile(SpriteBatch& sprite_batch, TileLocation* location, const RenderView& view, const DrawingOptions& options, uint32_t current_house_id, int in_draw_x, int in_draw_y, TileRenderPass pass) {
	if (!location) {
		return;
	}

	// Which slices of the tile this invocation renders. Side effects that must
	// happen exactly once per tile (overlays, markers) are tied to the contents
	// slice. Lights and tooltips no longer ride along here: LightGatherer and
	// TooltipCollector walk the map on their own, because the chunk cache makes
	// this function skip the tiles it already baked.
	const bool draw_ground = (pass == TileRenderPass::All || pass == TileRenderPass::Ground);
	const bool draw_borders = (pass == TileRenderPass::All || pass == TileRenderPass::Borders);
	const bool draw_contents = (pass == TileRenderPass::All || pass == TileRenderPass::Contents);
	Tile* tile = location->get();

	if (!tile) {
		return;
	}

	if (options.show_only_modified && !tile->isModified()) {
		return;
	}

	int map_x = location->getX();
	int map_y = location->getY();
	int map_z = location->getZ();

	int draw_x, draw_y;
	if (in_draw_x != -1 && in_draw_y != -1) {
		draw_x = in_draw_x;
		draw_y = in_draw_y;
	} else {
		// Early viewport culling - skip tiles that are completely off-screen
		if (!view.IsTileVisible(map_x, map_y, map_z, draw_x, draw_y)) {
			return;
		}
	}

	// A posicao do tile nunca muda aqui dentro: cada elemento sai em
	// (tile - elevacao), e a elevacao vem pronta do classificador de ordem.
	const int tile_draw_x = draw_x;
	const int tile_draw_y = draw_y;

	const auto& position = location->getPosition();

	ItemDefinitionView ground_it;
	if (tile->ground) {
		ground_it = tile->ground->getDefinition();
	}

	const bool hidden_invalid_ground = tile->ground && tile->ground->isInvalidOTBMItem() && !options.show_invalid_tiles;
	const bool unresolved_invalid_ground = tile->ground && tile->ground->isInvalidOTBMItem() && !ground_it;
	const bool ground_drawable = tile->ground && ground_it && !hidden_invalid_ground;

	// O unico consumidor do ponteiro e o MarkerDrawer la no fim; com a condicao
	// falsa a busca no mapa de waypoints (hash por posicao) e trabalho jogado
	// fora. Os tooltips de waypoint agora saem do TooltipCollector.
	const bool need_waypoint = view.zoom < 10.0 && !options.ingame && options.show_waypoints;

	Waypoint* waypoint = nullptr;
	if (draw_contents && need_waypoint && location->getWaypointCount() > 0) {
		waypoint = editor->map.waypoints.getWaypoint(location);
	}

	bool as_minimap = options.show_as_minimap;
	bool only_colors = as_minimap || options.show_only_colors;

	uint8_t r = 255, g = 255, b = 255;

	// begin filters for ground tile. O hasTileColorModifiers() pula a chamada quando
	// nenhuma opcao de tint esta ligada -- Calculate devolveria 255/255/255 intacto.
	if (!as_minimap && options.hasTileColorModifiers()) {
		TileColorCalculator::Calculate(tile, options, current_house_id, location->getSpawnCount(), r, g, b);
	}

	InvalidOTBMItemMarkerColor invalid_tile_marker_color = InvalidOTBMItemMarkerColor::None;
	bool has_selected_invalid_item = false;

	if (options.show_invalid_tiles && tile->ground && tile->ground->isInvalidOTBMItem()) {
		invalid_tile_marker_color = tile->ground->invalidOTBMMarkerColor();
		has_selected_invalid_item = tile->ground->isSelected();
	}

	if (only_colors) {
		if (!draw_ground) {
			// Squares are emitted by the ground pass.
		} else if (as_minimap) {
			TileColorCalculator::GetMinimapColor(tile, r, g, b);
			sprite_drawer->glBlitSquare(sprite_batch, tile_draw_x, tile_draw_y, DrawColor(r, g, b, 255));
		} else if (r != 255 || g != 255 || b != 255) {
			sprite_drawer->glBlitSquare(sprite_batch, tile_draw_x, tile_draw_y, DrawColor(r, g, b, 128));
		}
	} else if (!ground_drawable && !unresolved_invalid_ground && draw_ground && options.always_show_zones && (r != 255 || g != 255 || b != 255)) {
		// Groundless tile that still carries a map flag (PZ / NoPVP / NoLogout / PVPZone).
		// Draw a solid colored square so these "ghost zones" are clearly visible for
		// manual cleanup. Using glBlitSquare (same path as "Show Only Colors") keeps it
		// visible over the void and avoids depending on the SPRITE_ZONE asset.
		// (Missing-definition ground placeholders are represented by the tile-level
		// invalid overlay.)
		sprite_drawer->glBlitSquare(sprite_batch, tile_draw_x, tile_draw_y, DrawColor(r, g, b, 128));
	}

	// Cache isHouseTile â€” used multiple times below
	const bool is_house_tile = tile->isHouseTile();

	// end filters for ground tile

	// Draw helper border for selected house tiles
	// Only draw on the current floor (grid). Sai por cima do chao do tile e por
	// baixo dos itens -- inclusive de um chao que subiu para a passada de conteudo.
	bool house_box_pending = draw_contents && options.show_houses && is_house_tile && static_cast<int>(tile->getHouseID()) == current_house_id && map_z == view.floor;
	auto flushHouseBox = [&]() {
		if (!house_box_pending) {
			return;
		}
		house_box_pending = false;

		uint8_t hr, hg, hb;
		TileColorCalculator::GetHouseColor(tile->getHouseID(), hr, hg, hb);

		float intensity = 0.5f + (0.5f * options.highlight_pulse);
		// Optimization: Use integer math for border color to avoid vec4 construction and casting
		int ba = static_cast<int>(intensity * 255.0f);
		// hr, hg, hb are already uint8_t
		sprite_drawer->glDrawBox(sprite_batch, tile_draw_x, tile_draw_y, 32, 32, DrawColor(hr, hg, hb, ba));
	};

	if (only_colors) {
		flushHouseBox();
		return;
	}

	// Hoist house color calculation out of item loop
	uint8_t house_r = 255, house_g = 255, house_b = 255;
	const bool calculate_house_color = options.extended_house_shader && options.show_houses && is_house_tile;
	const bool should_pulse = calculate_house_color && (static_cast<int>(tile->getHouseID()) == current_house_id) && (options.highlight_pulse > 0.0f);
	float boost = 0.0f;

	if (calculate_house_color) {
		TileColorCalculator::GetHouseColor(tile->getHouseID(), house_r, house_g, house_b);
		if (should_pulse) {
			boost = options.highlight_pulse * 0.6f;
		}
	}

	auto blitGround = [&](const RenderOrder::TileElement& element) {
		// BlitItem recebe a posicao por referencia e a empurra pela elevacao do
		// sprite; aqui ela e descartada, porque o classificador ja a acumulou.
		int ground_x = tile_draw_x - element.elevation;
		int ground_y = tile_draw_y - element.elevation;
		BlitItemParams params(position, element.item, options);
		params.tile = tile;
		params.item_definition = element.definition;
		params.red = r;
		params.green = g;
		params.blue = b;
		if (GameSprite* ground_sprite = element.sprite) {
			SpritePatterns patterns = PatternCalculator::Calculate(ground_sprite, element.definition, element.item, tile, position);

			// Inline preload check â€” skip function call when sprite is simple and loaded (95%+ case)
			if (!ground_sprite->isSimpleAndLoaded()) {
				rme::collectTileSprites(ground_sprite, patterns.x, patterns.y, patterns.z, patterns.frame);
			}

			params.sprite = ground_sprite;
			params.patterns = &patterns;
			item_drawer->BlitItem(sprite_batch, sprite_drawer, creature_drawer, ground_x, ground_y, params);
		} else if (!unresolved_invalid_ground) {
			item_drawer->BlitItem(sprite_batch, sprite_drawer, creature_drawer, ground_x, ground_y, params);
		}
	};

	auto blitTileItem = [&](const RenderOrder::TileElement& element) {
		GameSprite* sprite = element.sprite;
		if (!sprite) {
			// Missing-definition placeholders are represented by the tile-level invalid overlay.
			return;
		}
		Item* item = element.item;

		SpritePatterns patterns = PatternCalculator::Calculate(sprite, element.definition, item, tile, position);

		// Inline preload check - skip function call when sprite is simple and loaded
		if (!sprite->isSimpleAndLoaded()) {
			rme::collectTileSprites(sprite, patterns.x, patterns.y, patterns.z, patterns.frame);
		}

		BlitItemParams params(position, item, options);
		params.tile = tile;
		params.item_definition = element.definition;
		params.sprite = sprite;
		params.patterns = &patterns;

		// item sprite
		if (item->isBorder()) {
			params.red = r;
			params.green = g;
			params.blue = b;
		} else {
			uint8_t ir = 255, ig = 255, ib = 255;

			if (calculate_house_color) {
				// Apply house color tint
				ir = static_cast<uint8_t>(ir * house_r / 255);
				ig = static_cast<uint8_t>(ig * house_g / 255);
				ib = static_cast<uint8_t>(ib * house_b / 255);

				if (should_pulse) {
					// Pulse effect matching the tile pulse
					ir = static_cast<uint8_t>(std::min(255, static_cast<int>(ir + (255 - ir) * boost)));
					ig = static_cast<uint8_t>(std::min(255, static_cast<int>(ig + (255 - ig) * boost)));
					ib = static_cast<uint8_t>(std::min(255, static_cast<int>(ib + (255 - ib) * boost)));
				}
			}

			params.red = ir;
			params.green = ig;
			params.blue = ib;
		}

		int item_x = tile_draw_x - element.elevation;
		int item_y = tile_draw_y - element.elevation;
		item_drawer->BlitItem(sprite_batch, sprite_drawer, creature_drawer, item_x, item_y, params);
	};

	auto inThisPass = [&](RenderOrder::Layer layer) {
		switch (layer) {
			case RenderOrder::Layer::Ground:
				return draw_ground;
			case RenderOrder::Layer::Borders:
				return draw_borders;
			case RenderOrder::Layer::Contents:
			default:
				return draw_contents;
		}
	};

	// A altura em que os marcadores saem: a da pilha depois dos itens comuns,
	// como o draw_x que o laco antigo deixava.
	int marker_elevation = 0;

	// Camada e elevacao de cada elemento vem do perfil de ordem do cliente
	// (RenderOrder::visitTileElements) -- o mesmo classificador que o chunk
	// cache usa no bake. Esta passada so desenha o que e da camada dela, na
	// ordem em que o cliente empilha: chao, bordas, paredes, itens comuns,
	// criatura e, por ultimo e sem elevacao, os itens "on top".
	RenderOrder::visitTileElements(tile, options, [&](const RenderOrder::TileElement& element) {
		if (!inThisPass(element.layer)) {
			return;
		}
		switch (element.kind) {
			case RenderOrder::ElementKind::Ground:
				blitGround(element);
				break;
			case RenderOrder::ElementKind::Item:
				flushHouseBox();
				blitTileItem(element);
				break;
			case RenderOrder::ElementKind::Creature:
				flushHouseBox();
				marker_elevation = element.elevation;
				// monster/npc on tile
				if (tile->creature && options.show_creatures) {
					creature_drawer->BlitCreature(sprite_batch, sprite_drawer, tile_draw_x - element.elevation, tile_draw_y - element.elevation, tile->creature.get(), CreatureDrawOptions { .map_pos = position, .transient_selection_bounds = options.transient_selection_bounds });
					// O nome so aparece no andar da camera (ver CreatureNameDrawer::draw):
					// filtrar aqui evita montar labels que seriam descartadas na hora de
					// desenhar. Nada muda na tela.
					if (creature_name_drawer && options.show_creature_names && map_z == view.floor) {
						creature_name_drawer->addLabel(position, tile->creature->getName(), tile->creature.get());
					}
				}
				break;
		}
	});
	flushHouseBox();

	if (!draw_contents) {
		return;
	}

	// O marcador de item invalido olha os mesmos itens que a passada de conteudo mostraria.
	if (options.show_invalid_tiles && options.drawLooseItems()) {
		for (const auto& item : tile->items) {
			if (options.show_only_grounds && !item->isBorder() && !item->isOptionalBorder()) {
				continue;
			}
			if (item->isInvalidOTBMItem()) {
				if (invalid_tile_marker_color != InvalidOTBMItemMarkerColor::Red) {
					invalid_tile_marker_color = item->invalidOTBMMarkerColor();
				}
				has_selected_invalid_item = has_selected_invalid_item || item->isSelected();
			}
		}
	}

	if (options.show_invalid_zones && !as_minimap && tile->hasInvalidZones()) {
		sprite_drawer->glBlitSquare(sprite_batch, tile_draw_x, tile_draw_y, DrawColor(255, 0, 255, 171));
	}

	if (options.show_invalid_tiles && !as_minimap && invalid_tile_marker_color != InvalidOTBMItemMarkerColor::None) {
		const DrawColor overlay = invalidTileOverlayColor(invalid_tile_marker_color, has_selected_invalid_item);
		sprite_drawer->glBlitSquare(sprite_batch, tile_draw_x, tile_draw_y, overlay);
	}

	if (view.zoom < 10.0) {
		// markers (waypoint, house exit, town temple, spawn)
		marker_drawer->draw(sprite_batch, sprite_drawer, tile_draw_x - marker_elevation, tile_draw_y - marker_elevation, tile, waypoint, current_house_id, *editor, options);
	}
}
