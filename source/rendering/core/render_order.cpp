//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/core/render_order.h"

#include "app/client_version.h"
#include "ui/gui.h"

#include <spdlog/spdlog.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace RenderOrder {

	namespace {
		// Indexado por client id. Vazio = ninguem projeta sombra (arquivo ausente,
		// ou um cliente que nao e o do BR).
		std::vector<bool> g_shadow_casters;
		const ClientVersion* g_shadow_casters_version = nullptr;
		bool g_shadow_casters_synced = false;

		// Le SHADOW_CASTER_IDS = { ... } do shadow_casters.lua gerado pelo projeto
		// do BR (scripts/make_shadow_casters.py). So os ids interessam: o codigo de
		// forma/altura ao lado nunca e zero, entao estar na lista ja e ser caster.
		std::vector<bool> loadShadowCasters(const wxString& path) {
			std::vector<bool> casters;
			std::ifstream file(std::filesystem::path(path.ToStdWstring()), std::ios::binary);
			if (!file) {
				return casters;
			}
			std::stringstream buffer;
			buffer << file.rdbuf();
			const std::string text = buffer.str();

			const size_t name = text.find("SHADOW_CASTER_IDS");
			if (name == std::string::npos) {
				return casters;
			}
			const size_t open = text.find('{', name);
			const size_t close = open == std::string::npos ? std::string::npos : text.find('}', open);
			if (close == std::string::npos) {
				return casters;
			}

			size_t count = 0;
			size_t i = open + 1;
			while (i < close) {
				if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
					++i;
					continue;
				}
				uint32_t value = 0;
				while (i < close && std::isdigit(static_cast<unsigned char>(text[i]))) {
					value = value * 10 + static_cast<uint32_t>(text[i] - '0');
					++i;
				}
				if (value > 0xFFFF) {
					continue;
				}
				if (casters.size() <= value) {
					casters.resize(static_cast<size_t>(value) + 1, false);
				}
				casters[value] = true;
				++count;
			}
			spdlog::info("[RenderOrder] {} shadow casters loaded from {}", count, path.ToStdString());
			return casters;
		}

		ChunkTileOrder buildChunkTileOrder(RenderOrderProfile profile) {
			ChunkTileOrder order {};
			size_t n = 0;
			if (profile == RenderOrderProfile::BattleRoyale) {
				for (int diagonal = 0; diagonal <= 30; ++diagonal) {
					for (int tx = 0; tx < 16; ++tx) {
						const int ty = diagonal - tx;
						if (ty < 0 || ty >= 16) {
							continue;
						}
						order[n++] = { static_cast<uint8_t>(tx), static_cast<uint8_t>(ty) };
					}
				}
			} else {
				for (int ty = 0; ty < 16; ++ty) {
					for (int tx = 0; tx < 16; ++tx) {
						order[n++] = { static_cast<uint8_t>(tx), static_cast<uint8_t>(ty) };
					}
				}
			}
			return order;
		}
	}

	RenderOrderProfile resolveProfile(int setting) {
		if (setting == 1) {
			return RenderOrderProfile::BlackTalon;
		}
		if (setting == 2) {
			return RenderOrderProfile::BattleRoyale;
		}
		const ClientVersion* version = g_gui.gfx.client_version;
		return (version && version->isProtobuf()) ? RenderOrderProfile::BattleRoyale : RenderOrderProfile::BlackTalon;
	}

	const char* profileName(RenderOrderProfile profile) {
		return profile == RenderOrderProfile::BattleRoyale ? "Battle Royale" : "Black Talon";
	}

	void syncShadowCasters() {
		const ClientVersion* version = g_gui.gfx.client_version;
		if (g_shadow_casters_synced && version == g_shadow_casters_version) {
			return;
		}
		g_shadow_casters_synced = true;
		g_shadow_casters_version = version;
		g_shadow_casters.clear();
		if (!version) {
			return;
		}
		const wxString base = version->getDataPath().GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
		g_shadow_casters = loadShadowCasters(base + "shadow_casters.lua");
	}

	bool isShadowCaster(ClientItemId client_id) noexcept {
		return client_id < g_shadow_casters.size() && g_shadow_casters[client_id];
	}

	const ChunkTileOrder& chunkTileOrder(RenderOrderProfile profile) {
		static const ChunkTileOrder black_talon = buildChunkTileOrder(RenderOrderProfile::BlackTalon);
		static const ChunkTileOrder battle_royale = buildChunkTileOrder(RenderOrderProfile::BattleRoyale);
		return profile == RenderOrderProfile::BattleRoyale ? battle_royale : black_talon;
	}

	bool isFlatDecoration(const ItemDefinitionView& definition, const GameSprite* spr) noexcept {
		if (!definition || !spr) {
			return false;
		}
		// NotMoveable e nenhuma de: Ground, GroundBorder, OnBottom, OnTop (ja
		// garantido por quem chama: item comum), Container, Splash, NotWalkable,
		// Pickupable, Hangable, HookSouth/East, Displacement, Elevation, LyingCorpse.
		if (definition.hasFlag(ItemFlag::Moveable) || definition.isGroundTile() || definition.isContainer() || definition.isSplash()) {
			return false;
		}
		if (definition.hasFlag(ItemFlag::Unpassable) || definition.hasFlag(ItemFlag::Pickupable) || definition.hasFlag(ItemFlag::IsHangable)) {
			return false;
		}
		if (definition.hasFlag(ItemFlag::HookSouth) || definition.hasFlag(ItemFlag::HookEast) || definition.hasFlag(ItemFlag::LyingObject)) {
			return false;
		}
		if (definition.hasFlag(ItemFlag::HasElevation) || spr->draw_height > 0 || spr->drawoffset_x != 0 || spr->drawoffset_y != 0) {
			return false;
		}
		// m_size.area() == 1 e m_shadowCaster == 0.
		return isSingleCell(spr) && !isShadowCaster(definition.clientId());
	}

} // namespace RenderOrder
