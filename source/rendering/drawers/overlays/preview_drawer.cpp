#include "app/main.h"

// glut include removed

#include "rendering/drawers/overlays/preview_drawer.h"
#include "rendering/core/sprite_batch.h"
#include "rendering/core/primitive_renderer.h"
#include "rendering/ui/map_display.h"
#include "rendering/drawers/entities/item_drawer.h"
#include "rendering/drawers/entities/creature_drawer.h"
#include "ui/gui.h"
#include "brushes/brush.h"
#include "editor/copybuffer.h"
#include "editor/editor.h"
#include "map/basemap.h"
#include "map/map_region.h"
#include "ui/map_tab.h"

#include "map/spatial_hash_grid.h"

#include <algorithm>
#include <vector>

PreviewDrawer::PreviewDrawer() {
}

PreviewDrawer::~PreviewDrawer() {
}

void PreviewDrawer::draw(SpriteBatch& sprite_batch, MapCanvas* canvas, const RenderView& view, int map_z, const DrawingOptions& options, Editor& editor, ItemDrawer* item_drawer, SpriteDrawer* sprite_drawer, CreatureDrawer* creature_drawer, uint32_t current_house_id) {
	MapTab* mapTab = dynamic_cast<MapTab*>(canvas->GetMapWindow());
	BaseMap* secondary_map = mapTab ? mapTab->GetSession()->secondary_map : nullptr;

	if (secondary_map != nullptr && !options.ingame) {
		Brush* brush = g_gui.GetCurrentBrush();

		Position normalPos;
		Position to(view.mouse_map_x, view.mouse_map_y, view.floor);

		if (canvas->isPasting()) {
			normalPos = editor.copybuffer.getPosition();
		} else if (brush && brush->is<DoodadBrush>()) {
			normalPos = Position(0x8000, 0x8000, 0x8);
		} else {
			normalPos = to;
		}

		// Compensate for underground/overground (constante para este andar)
		int offset;
		if (map_z <= GROUND_LAYER) {
			offset = (GROUND_LAYER - map_z) * TILE_SIZE;
		} else {
			offset = TILE_SIZE * (view.floor - map_z);
		}

		// Percorre o BUFFER e calcula onde cada tile dele cai na tela, em vez de
		// percorrer a tela perguntando ao buffer o que ha em cada posicao. E a mesma
		// conta, so que invertida: `pos = normalPos + final - to` vira
		// `final = pos - normalPos + to`.
		//
		// O que muda e a escala. O caminho antigo custava um getTile por tile
		// VISIVEL, por andar -- afastado o suficiente sao ~130 mil por andar, quase
		// todos respondendo "aqui nao tem nada", porque um preview de doodad tem
		// meia duzia de tiles.
		//
		// A ORDEM importa, e por isso os tiles sao ordenados antes de desenhar: o
		// batch nao reordena nada, entao quem sai depois cobre quem veio antes, e um
		// sprite que invade o tile vizinho (64x64, offset negativo) trocaria de
		// camada se saisse na ordem em que o buffer guarda os tiles. `x` primario e
		// `y` secundario reproduz exatamente a ordem dos dois lacos antigos.
		//
		// E quando o buffer e MAIOR que a viewport -- colar uma regiao enorme --
		// varrer a tela volta a ser o mais barato dos dois, entao o caminho antigo
		// continua ali embaixo para esse caso.
		const auto drawPreviewTile = [&](Tile* tile, int map_x, int map_y) {
			int draw_x = ((map_x * TILE_SIZE) - view.view_scroll_x) - offset;
			int draw_y = ((map_y * TILE_SIZE) - view.view_scroll_y) - offset;
			// Canto do tile antes de qualquer elevacao: os itens "on top" sao
			// desenhados a partir dele, como Tile::drawTop faz no client.
			const int tile_draw_x = draw_x;
			const int tile_draw_y = draw_y;

			// Draw ground
			uint8_t r = 255, g = 255, b = 255;
			uint8_t base_alpha = canvas->isPasting() ? 128 : 255;

			if (tile->ground) {
				if (tile->isBlocking() && options.show_blocking) {
					g = g / 3 * 2;
					b = b / 3 * 2;
				}
				if (tile->isHouseTile() && options.show_houses) {
					if ((int)tile->getHouseID() == current_house_id) {
						r /= 2;
					} else {
						r /= 2;
						g /= 2;
					}
				} else if (options.show_special_tiles && tile->isPZ()) {
					r /= 2;
					b /= 2;
				}
				if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_PVPZONE) {
					r = r / 3 * 2;
					b = r / 3 * 2;
				}
				if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_NOLOGOUT) {
					b /= 2;
				}
				if (options.show_special_tiles && tile->getMapFlags() & TILESTATE_NOPVP) {
					g /= 2;
				}
				// BlackTalon: mesmo tint ambar do World Boss usado no mapa, para o
				// preview de colagem nao mentir sobre o que vai ser colado.
				//
				// MESMO interruptor e MESMA conta do tile_color_calculator: com
				// gates diferentes, desligar "Show world boss areas" tirava o ambar
				// do mapa e deixava o do preview (e vice-versa com o showspecial);
				// com contas diferentes, as duas ambares apareciam lado a lado.
				if (options.show_worldboss_zones && tile->getMapFlags() & TILESTATE_WORLDBOSS) {
					g = static_cast<uint8_t>((g * 200) >> 8);
					b >>= 2;
				}
				if (tile->ground) {
					BlitItemParams params(tile, tile->ground.get(), options);
					params.ephemeral = true;
					params.red = r;
					params.green = g;
					params.blue = b;
					params.alpha = base_alpha;
					item_drawer->BlitItem(sprite_batch, sprite_drawer, creature_drawer, draw_x, draw_y, params);
				}
			}

			// Draw items on the tile (inclusive: aqui sempre foi `zoom <= 10.0`)
			if (options.drawLooseItemsInclusive()) {
				auto blitPreviewItem = [&](Item* item, int& item_draw_x, int& item_draw_y) {
					BlitItemParams params(tile, item, options);
					params.ephemeral = true;
					params.alpha = base_alpha;
					if (item->isBorder()) {
						params.red = 255;
						params.green = r;
						params.blue = g;
						params.alpha = (base_alpha == 255) ? b : base_alpha;
					}
					item_drawer->BlitItem(sprite_batch, sprite_drawer, creature_drawer, item_draw_x, item_draw_y, params);
				};

				// Mesma ordem do TileRenderer/do client: os itens "on top" (top
				// order 3) ficam guardados antes dos comuns na pilha do tile mas
				// sao desenhados por ultimo (Tile::drawTop), acima dos comuns e
				// da criatura.
				bool has_top_items = false;
				for (const auto& item : tile->items) {
					if (item->isAlwaysOnBottom() && item->getTopOrder() == 3) {
						has_top_items = true;
						continue;
					}
					blitPreviewItem(item.get(), draw_x, draw_y);
				}
				if (tile->creature && options.show_creatures) {
					creature_drawer->BlitCreature(sprite_batch, sprite_drawer, draw_x, draw_y, tile->creature.get());
				}
				if (has_top_items) {
					for (const auto& item : tile->items) {
						if (!item->isAlwaysOnBottom() || item->getTopOrder() != 3) {
							continue;
						}
						int top_draw_x = tile_draw_x;
						int top_draw_y = tile_draw_y;
						blitPreviewItem(item.get(), top_draw_x, top_draw_y);
					}
				}
			}
		};

		const uint64_t viewport_tiles = static_cast<uint64_t>(std::max(0, view.end_x - view.start_x + 1))
			* static_cast<uint64_t>(std::max(0, view.end_y - view.start_y + 1));

		// O que decide e o custo de PERCORRER o buffer, nao quantos tiles ele tem.
		// BaseMap::clear() apenas anula os tiles e deixa a grade montada, e o buffer
		// do autoborder e o MESMO objeto a sessao inteira: depois de pincelar meio
		// mapa ele guarda uma celula para cada regiao ja visitada, com dezenas de
		// tiles vivos e milhares de nos vazios que o iterador ainda tem de percorrer.
		// Medir por tilecount escolheria o caminho do buffer sempre, e ele ficaria
		// mais lento que o codigo que veio substituir.
		//
		// A conta e uma ESTIMATIVA POR BAIXO, nao um teto: MapIterator sonda os 256
		// slots de no de cada celula (que e o que esta contado aqui) e, em cada no
		// vivo, mais 16 slots de andar e 16 de tile. Ela e comparada com um numero de
		// getTile, que custa bem mais que um passo de iterador -- as duas
		// aproximacoes puxam para lados opostos e sobra a mesma ordem de grandeza.
		// O que importa e separar os dois regimes, e para isso ela basta: um preview
		// de doodad fica em 1-4 celulas e um buffer reciclado passa das dezenas.
		const uint64_t buffer_scan_cost = static_cast<uint64_t>(secondary_map->getGrid().cellCount())
			* static_cast<uint64_t>(SpatialHashGrid::NODES_IN_CELL);

		if (buffer_scan_cost <= viewport_tiles) {
			visible_tiles.clear();
			for (TileLocation& location : secondary_map->tiles()) {
				Tile* tile = location.get();
				if (!tile) {
					continue;
				}

				const Position& pos = location.getPosition();
				if (pos.z - normalPos.z + to.z != map_z) {
					continue;
				}

				const int map_x = pos.x - normalPos.x + to.x;
				const int map_y = pos.y - normalPos.y + to.y;
				if (map_x < view.start_x || map_x > view.end_x || map_y < view.start_y || map_y > view.end_y) {
					continue;
				}

				visible_tiles.push_back({ map_x, map_y, tile });
			}

			std::sort(visible_tiles.begin(), visible_tiles.end(), [](const PreviewTile& a, const PreviewTile& b) {
				return a.map_x != b.map_x ? a.map_x < b.map_x : a.map_y < b.map_y;
			});

			for (const PreviewTile& entry : visible_tiles) {
				drawPreviewTile(entry.tile, entry.map_x, entry.map_y);
			}

			// Colar uma regiao enorme uma unica vez nao deve prender dezenas de MB
			// pelo resto da sessao. Abaixo disso a capacidade fica, que e o motivo de
			// o vetor ser membro.
			if (visible_tiles.capacity() > 65536) {
				std::vector<PreviewTile>().swap(visible_tiles);
			}
		} else {
			for (int map_x = view.start_x; map_x <= view.end_x; map_x++) {
				for (int map_y = view.start_y; map_y <= view.end_y; map_y++) {
					const Position pos = normalPos + Position(map_x, map_y, map_z) - to;
					if (pos.z >= MAP_LAYERS || pos.z < 0) {
						continue;
					}
					if (Tile* tile = secondary_map->getTile(pos)) {
						drawPreviewTile(tile, map_x, map_y);
					}
				}
			}
		}
		// Draw highlight on the specific tile under mouse
		// This helps user see where they are pointing in the "chaos"
		Position mousePos(view.mouse_map_x, view.mouse_map_y, view.floor);
		if (mousePos.z == map_z) {
			int draw_x = ((mousePos.x * TILE_SIZE) - view.view_scroll_x) - offset;
			int draw_y = ((mousePos.y * TILE_SIZE) - view.view_scroll_y) - offset;

			if (g_gui.gfx.ensureAtlasManager()) {
				// Draw a semi-transparent white box over the tile
				glm::vec4 highlightColor(1.0f, 1.0f, 1.0f, 0.25f); // 25% white
				sprite_batch.drawRect((float)draw_x, (float)draw_y, (float)TILE_SIZE, (float)TILE_SIZE, highlightColor, *g_gui.gfx.getAtlasManager());
			}
		}
	}
}
