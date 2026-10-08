//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_CORE_RENDER_ORDER_H_
#define RME_RENDERING_CORE_RENDER_ORDER_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include "item_definitions/core/item_definition_store.h"
#include "game/item.h"
#include "map/tile.h"
#include "rendering/core/drawing_options.h"
#include "rendering/core/game_sprite.h"
#include "rendering/core/render_order_profile.h"

// Classificador da ordem de desenho de um tile para o perfil ativo. Os perfis
// estao descritos em render_order_profile.h.
namespace RenderOrder {

	// Teto da elevacao acumulada num tile, em pixels de um tile de 32. O
	// OTClientV8 usa MAX_ELEVATION = 24; o cliente do BR, com a arte em 2x, 48.
	constexpr int MAX_ELEVATION = 24;

	// A camada do andar em que um elemento do tile e desenhado. Cada uma e uma
	// passada inteira sobre o andar (TileRenderPass::Ground/Borders/Contents).
	enum class Layer : uint8_t {
		Ground,
		Borders,
		Contents,
	};

	enum class ElementKind : uint8_t {
		Ground,
		Item,
		// Marcador da vez da criatura: vem depois dos itens comuns e antes dos
		// "on top", com a elevacao acumulada. Sai sempre -- quem desenha decide
		// se o tile tem criatura e se ela aparece.
		Creature,
	};

	struct TileElement {
		ElementKind kind = ElementKind::Item;
		Layer layer = Layer::Contents;
		// Deslocamento para noroeste, em pixels, ja com o teto aplicado.
		int elevation = 0;
		Item* item = nullptr; // o ground ou o item; nulo para a criatura
		GameSprite* sprite = nullptr; // pode ser nulo (definicao sem sprite)
		ItemDefinitionView definition;
	};

	// Configuracao 0 = automatico (protobuf/appearances -> Battle Royale, .dat
	// classico -> Black Talon), 1 = Black Talon, 2 = Battle Royale.
	[[nodiscard]] RenderOrderProfile resolveProfile(int setting);
	[[nodiscard]] const char* profileName(RenderOrderProfile profile);

	// Recarrega a lista de "shadow casters" do BR quando o cliente carregado
	// muda. Barata quando nada mudou (compara um ponteiro); chamada uma vez por
	// frame em DrawingOptions::Update.
	void syncShadowCasters();
	// Item que projeta sombra do sol no cliente do BR (data/<versao>/shadow_casters.lua).
	// Esses nunca descem para a camada das bordas.
	[[nodiscard]] bool isShadowCaster(ClientItemId client_id) noexcept;

	// Chave da ordem global dos tiles de um andar: desenhar em ordem crescente
	// reproduz o cliente. Chaves iguais so para o mesmo tile.
	[[nodiscard]] inline uint64_t tileKey(RenderOrderProfile profile, int x, int y) noexcept {
		const uint64_t ux = static_cast<uint32_t>(x);
		if (profile == RenderOrderProfile::BattleRoyale) {
			// Diagonal x+y; dentro dela x crescente = y decrescente (sudoeste -> nordeste).
			return (static_cast<uint64_t>(static_cast<uint32_t>(x + y)) << 32) | ux;
		}
		return (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32) | ux;
	}

	// Ordem de visita dos 16x16 tiles de um chunk do cache (tx, ty), coerente com tileKey().
	using ChunkTileOrder = std::array<std::pair<uint8_t, uint8_t>, 16 * 16>;
	[[nodiscard]] const ChunkTileOrder& chunkTileOrder(RenderOrderProfile profile);

	// O sprite cabe numa celula de 32x32, sem crescer para os vizinhos? E o
	// "isSingleGround/isSingleGroundBorder" do BR: tamanho 1x1. Nas folhas
	// 12+/13 um recorte de 64px chega com width == height == 1 e overhang > 0.
	[[nodiscard]] inline bool isSingleCell(const GameSprite* spr) noexcept {
		return !spr || (spr->width == 1 && spr->height == 1 && spr->overhang_x == 0 && spr->overhang_y == 0);
	}

	// O desenho do sprite sai da propria celula (tamanho, recorte maior ou
	// deslocamento em qualquer direcao)?
	[[nodiscard]] inline bool exceedsCell(const GameSprite* spr) noexcept {
		return spr && (!isSingleCell(spr) || spr->drawoffset_x != 0 || spr->drawoffset_y != 0);
	}

	// Onde o ground do tile e desenhado.
	[[nodiscard]] inline Layer groundLayer(RenderOrderProfile profile, const ItemDefinitionView& definition, const GameSprite* spr) noexcept {
		if (profile == RenderOrderProfile::BattleRoyale) {
			return isSingleCell(spr) ? Layer::Ground : Layer::Contents;
		}
		// Black Talon: o redesenho da passada B so aparece quando o chao sai da celula.
		return (!definition.hasFlag(ItemFlag::FullTile) && exceedsCell(spr)) ? Layer::Contents : Layer::Ground;
	}

	// "Decoracao rasteira" do cliente do BR (ThingType::isFlatDecoration): item
	// comum, fixo, de uma celula, sem nenhuma propriedade que o faca interagir
	// com a pilha -- pedra, flor, tapete. Desce para a camada das bordas.
	[[nodiscard]] bool isFlatDecoration(const ItemDefinitionView& definition, const GameSprite* spr) noexcept;

	// Visita os elementos de um tile na ordem em que o cliente os empilha: ground,
	// bordas, bottom (paredes), itens comuns, a vez da criatura, itens "on top".
	// Cada um vem com a camada e a elevacao do perfil ativo (options.render_order).
	// Os filtros de desenho (show_only_grounds, itens invalidos, LOD de zoom) ja
	// vem aplicados: o que e pulado aqui tambem nao soma elevacao.
	//
	// Quem desenha uma camada so filtra por element.layer; a ordem dentro dela ja
	// e a do cliente. CPU (TileRenderer) e cache (ChunkCacheManager) usam esta
	// mesma funcao, entao nao divergem.
	template <typename Fn>
	void visitTileElements(const Tile* tile, const DrawingOptions& options, Fn&& fn) {
		const RenderOrderProfile profile = options.render_order;
		const bool battle_royale = profile == RenderOrderProfile::BattleRoyale;
		int elevation = 0;
		auto bump = [&elevation](const GameSprite* spr) {
			if (spr && spr->draw_height > 0) {
				elevation = std::min(elevation + static_cast<int>(spr->draw_height), MAX_ELEVATION);
			}
		};

		bool ground_in_contents = false;
		if (Item* ground = tile->ground.get()) {
			const ItemDefinitionView definition = ground->getDefinition();
			const bool hidden_invalid = ground->isInvalidOTBMItem() && !options.show_invalid_tiles;
			if (definition && !hidden_invalid) {
				TileElement element;
				element.kind = ElementKind::Ground;
				element.item = ground;
				element.sprite = ground->getSprite();
				element.definition = definition;
				element.layer = groundLayer(profile, definition, element.sprite);
				element.elevation = elevation;
				ground_in_contents = element.layer == Layer::Contents;
				fn(element);
				bump(element.sprite);
			}
		}

		// O LOD de zoom esconde tudo que nao e o chao, inclusive a criatura.
		if (!options.drawLooseItems()) {
			return;
		}

		auto visibleItem = [&options](Item* item, ItemDefinitionView& definition) {
			if (options.show_only_grounds && !item->isBorder() && !item->isOptionalBorder()) {
				return false;
			}
			definition = item->getDefinition();
			return !(item->isInvalidOTBMItem() && (!options.show_invalid_tiles || !definition));
		};
		auto emitItem = [&](Item* item, const ItemDefinitionView& definition, Layer layer, int item_elevation) {
			TileElement element;
			element.kind = ElementKind::Item;
			element.item = item;
			element.sprite = item->getSprite();
			element.definition = definition;
			element.layer = layer;
			element.elevation = item_elevation;
			fn(element);
			return element.sprite;
		};

		// Bordas (top order 1). No BR cada uma vai para a camada das bordas se
		// for 1x1 -- mesmo que o ground do tile tenha subido para o conteudo --, e
		// para o conteudo se for maior. No BT seguem o ground.
		bool has_bottom = false;
		for (const auto& owned : tile->items) {
			Item* item = owned.get();
			if (!item->isAlwaysOnBottom()) {
				continue;
			}
			const int top_order = item->getTopOrder();
			if (top_order == 2) {
				has_bottom = true;
			}
			if (top_order != 1) {
				continue;
			}
			ItemDefinitionView definition;
			if (!visibleItem(item, definition)) {
				continue;
			}
			Layer layer;
			if (battle_royale) {
				layer = isSingleCell(item->getSprite()) ? Layer::Borders : Layer::Contents;
			} else {
				layer = ground_in_contents ? Layer::Contents : Layer::Borders;
			}
			bump(emitItem(item, definition, layer, elevation));
		}

		// Bottom (paredes): tudo que e always-on-bottom e nao e borda nem "on top".
		for (const auto& owned : tile->items) {
			Item* item = owned.get();
			if (!item->isAlwaysOnBottom()) {
				continue;
			}
			const int top_order = item->getTopOrder();
			if (top_order == 1 || top_order == 3) {
				continue;
			}
			ItemDefinitionView definition;
			if (!visibleItem(item, definition)) {
				continue;
			}
			bump(emitItem(item, definition, Layer::Contents, elevation));
		}

		// Itens comuns, do mais antigo (embaixo) para o mais novo. No BR a
		// decoracao rasteira desce para as bordas, desde que o tile nao tenha
		// bottom e nada abaixo dela tenha elevado a pilha.
		for (const auto& owned : tile->items) {
			Item* item = owned.get();
			if (item->isAlwaysOnBottom()) {
				continue;
			}
			ItemDefinitionView definition;
			if (!visibleItem(item, definition)) {
				continue;
			}
			Layer layer = Layer::Contents;
			if (battle_royale && !has_bottom && elevation == 0 && isFlatDecoration(definition, item->getSprite())) {
				layer = Layer::Borders;
			}
			bump(emitItem(item, definition, layer, elevation));
		}

		{
			TileElement element;
			element.kind = ElementKind::Creature;
			element.layer = Layer::Contents;
			element.elevation = elevation;
			fn(element);
		}

		// "On top" fecha o tile, sem elevacao (Tile::drawTop desenha em `dest`).
		for (const auto& owned : tile->items) {
			Item* item = owned.get();
			if (!item->isAlwaysOnBottom() || item->getTopOrder() != 3) {
				continue;
			}
			ItemDefinitionView definition;
			if (!visibleItem(item, definition)) {
				continue;
			}
			emitItem(item, definition, Layer::Contents, 0);
		}
	}

} // namespace RenderOrder

#endif
