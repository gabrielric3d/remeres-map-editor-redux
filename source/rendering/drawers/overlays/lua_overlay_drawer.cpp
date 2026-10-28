#include "lua_overlay_drawer.h"
#include "rendering/map_drawer.h"
#include "rendering/core/primitive_renderer.h"
#include "rendering/core/text_renderer.h"
#include "rendering/core/graphics.h"
#include "rendering/core/coordinate_mapper.h"
#include "editor/action_queue.h"
#include "editor/editor.h"
#include "editor/selection.h"
#include "map/tile.h"
#include "lua/lua_script_manager.h"
#include "ui/gui.h"
#include "rendering/drawers/entities/creature_drawer.h"
#include "game/outfit.h"
#include "game/creature.h"

#include <functional>

namespace {
	static std::size_t hashSelection(const Selection& selection) {
		std::size_t hash = 1469598103934665603ull;
		for (const Tile* tile : selection.getTiles()) {
			if (!tile) {
				continue;
			}
			const Position pos = tile->getPosition();
			hash ^= std::hash<int>{}(pos.x);
			hash *= 1099511628211ull;
			hash ^= std::hash<int>{}(pos.y);
			hash *= 1099511628211ull;
			hash ^= std::hash<int>{}(pos.z);
			hash *= 1099511628211ull;
		}
		return hash;
	}
}

LuaOverlayDrawer::LuaOverlayDrawer(MapDrawer* mapDrawer) : mapDrawer(mapDrawer) {
}

LuaOverlayDrawer::~LuaOverlayDrawer() {
}

LuaOverlayDrawer::CacheKey LuaOverlayDrawer::makeCacheKey(const RenderView& view) const {
	Editor* editor = g_gui.GetCurrentEditor();
	const Selection* selection = editor ? &editor->selection : nullptr;
	const ActionQueue* actionQueue = editor && editor->actionQueue ? editor->actionQueue.get() : nullptr;

	return CacheKey {
		.start_x = view.start_x,
		.start_y = view.start_y,
		.end_x = view.end_x,
		.end_y = view.end_y,
		.floor = view.floor,
		.zoom = view.zoom,
		.view_scroll_x = view.view_scroll_x,
		.view_scroll_y = view.view_scroll_y,
		.tile_size = view.tile_size,
		.screen_width = view.screensize_x,
		.screen_height = view.screensize_y,
		.overlay_revision = g_luaScripts.getOverlayRevision(),
		.selection_count = selection ? selection->size() : 0,
		.selection_hash = selection ? hashSelection(*selection) : 0,
		.history_index = actionQueue ? actionQueue->getCurrentIndex() : 0,
		.history_size = actionQueue ? actionQueue->getSize() : 0
	};
}

void LuaOverlayDrawer::refreshCache(const RenderView& view) {
	const CacheKey nextKey = makeCacheKey(view);
	if (cacheValid && nextKey == cachedKey) {
		return;
	}

	cachedKey = nextKey;
	cacheValid = false;
	cachedCommands.clear();

	if (!g_luaScripts.isInitialized()) {
		return;
	}

	const auto& shows = g_luaScripts.getMapOverlayShows();
	if (shows.empty()) {
		cacheValid = true;
		return;
	}

	MapViewInfo viewInfo;
	viewInfo.start_x = view.start_x;
	viewInfo.start_y = view.start_y;
	viewInfo.end_x = view.end_x;
	viewInfo.end_y = view.end_y;
	viewInfo.floor = view.floor;
	viewInfo.zoom = view.zoom;
	viewInfo.view_scroll_x = view.view_scroll_x;
	viewInfo.view_scroll_y = view.view_scroll_y;
	viewInfo.tile_size = view.tile_size;
	viewInfo.screen_width = view.screensize_x;
	viewInfo.screen_height = view.screensize_y;

	g_luaScripts.collectMapOverlayCommands(viewInfo, cachedCommands);
	cacheValid = true;
}

void LuaOverlayDrawer::Draw(const RenderView& view, const DrawingOptions& options) {
	auto* primitives = mapDrawer->getPrimitiveRenderer();
	auto* spriteBatch = mapDrawer->getSpriteBatch();
	auto* atlas = g_gui.gfx.getAtlasManager();

	if (!primitives || !spriteBatch || !atlas) return;

	refreshCache(view);

	for (const auto& cmd : cachedCommands) {
		glm::vec4 color(cmd.color.Red() / 255.0f, cmd.color.Green() / 255.0f, cmd.color.Blue() / 255.0f, cmd.color.Alpha() / 255.0f);

		// Este passo desenha na projecao do mapa, glOrtho(0, largura * zoom, ...): pixels LOGICOS,
		// em que um tile mede sempre TILE_SIZE, qualquer que seja o zoom. A posicao de um tile e a
		// de RenderView::getScreenPosition (a mesma dos tiles, do pincel e dos nomes). A conta
		// antiga passava view.start_x -- um indice de TILE -- como scroll em pixels ao
		// CoordinateMapper, dividia pelo zoom e subtraia o scroll de novo: o retangulo caia a
		// dezenas de tiles do lugar, e o erro mudava com a regiao do mapa e com o zoom.
		// screen = true sao pixels de TELA: viram logicos multiplicando pelo zoom.
		const float zoom = view.zoom;
		float screenX = 0, screenY = 0;

		if (cmd.screen_space) {
			screenX = cmd.x * zoom;
			screenY = cmd.y * zoom;
		} else {
			if (cmd.z != view.floor) continue; // Only draw on current floor if world space
			int mapped_x, mapped_y;
			view.getScreenPosition(cmd.x, cmd.y, cmd.z, mapped_x, mapped_y);
			screenX = static_cast<float>(mapped_x);
			screenY = static_cast<float>(mapped_y);
		}

		switch (cmd.type) {
			case MapOverlayCommand::Type::Rect: {
				float w = cmd.screen_space ? cmd.w * zoom : cmd.w * TILE_SIZE;
				float h = cmd.screen_space ? cmd.h * zoom : cmd.h * TILE_SIZE;
				if (cmd.filled) {
					primitives->drawRect(glm::vec4(screenX, screenY, w, h), color);
				} else {
					primitives->drawBox(glm::vec4(screenX, screenY, w, h), color, static_cast<float>(cmd.width));
				}
				break;
			}
			case MapOverlayCommand::Type::Line: {
				float screenX2 = 0, screenY2 = 0;
				if (cmd.screen_space) {
					screenX2 = cmd.x2 * zoom;
					screenY2 = cmd.y2 * zoom;
				} else {
					if (cmd.z2 != view.floor) continue;
					int mapped_x2, mapped_y2;
					view.getScreenPosition(cmd.x2, cmd.y2, cmd.z2, mapped_x2, mapped_y2);
					screenX2 = static_cast<float>(mapped_x2);
					screenY2 = static_cast<float>(mapped_y2);
				}
				primitives->drawLine(glm::vec2(screenX, screenY), glm::vec2(screenX2, screenY2), color);
				break;
			}
			case MapOverlayCommand::Type::Sprite: {
				const AtlasRegion* region = atlas->getRegion(cmd.sprite_id);
				if (region) {
					float sizeX = cmd.screen_space ? cmd.w * zoom : static_cast<float>(TILE_SIZE);
					float sizeY = cmd.screen_space ? cmd.h * zoom : static_cast<float>(TILE_SIZE);
					if (sizeX == 0) sizeX = static_cast<float>(TILE_SIZE);
					if (sizeY == 0) sizeY = static_cast<float>(TILE_SIZE);
					spriteBatch->draw(screenX, screenY, sizeX, sizeY, *region, color.r, color.g, color.b, color.a);
				}
				break;
			}
			case MapOverlayCommand::Type::Creature: {
				auto* creatureDrawer = mapDrawer->getCreatureDrawer();
				auto* spriteDrawer = mapDrawer->getSpriteDrawer();
				if (!creatureDrawer || !spriteDrawer || cmd.screen_space) break;
				// o tile da criatura em pixels logicos (screenX/Y, como o pincel de criatura): o
				// BlitCreature poe o tile de baixo-direita do outfit nele e sobe a noroeste
				const int cx = static_cast<int>(screenX);
				const int cy = static_cast<int>(screenY);
				Outfit outfit;
				outfit.lookType = cmd.look_type;
				outfit.lookHead = cmd.look_head;
				outfit.lookBody = cmd.look_body;
				outfit.lookLegs = cmd.look_legs;
				outfit.lookFeet = cmd.look_feet;
				outfit.lookAddon = cmd.look_addon;
				creatureDrawer->BlitCreature(*spriteBatch, spriteDrawer, cx, cy, outfit, static_cast<Direction>(cmd.direction),
					CreatureDrawOptions { .color = DrawColor(255, 255, 255, cmd.color.Alpha()) });
				break;
			}
			default:
				break;
		}
	}
}

void LuaOverlayDrawer::DrawUI(NVGcontext* vg, const RenderView& view, const DrawingOptions& options) {
	if (!vg) return;

	refreshCache(view);

	for (const auto& cmd : cachedCommands) {
		if (cmd.type != MapOverlayCommand::Type::Text) continue;

		glm::vec4 color(cmd.color.Red() / 255.0f, cmd.color.Green() / 255.0f, cmd.color.Blue() / 255.0f, cmd.color.Alpha() / 255.0f);

		float screenX = 0, screenY = 0;

		if (cmd.screen_space) {
			screenX = cmd.x;
			screenY = cmd.y;
		} else {
			// o NanoVG desenha em pixels de TELA: a posicao logica do tile dividida pelo zoom,
			// como os nomes das criaturas (CreatureNameDrawer)
			if (cmd.z != view.floor) continue;
			int mapped_x, mapped_y;
			view.getScreenPosition(cmd.x, cmd.y, cmd.z, mapped_x, mapped_y);
			screenX = mapped_x / view.zoom;
			screenY = mapped_y / view.zoom;
		}

		TextRenderer::DrawText(vg, static_cast<int>(screenX), static_cast<int>(screenY), cmd.text, color, 14.0f);
	}
}
