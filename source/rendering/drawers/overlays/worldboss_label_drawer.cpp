//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/drawers/overlays/worldboss_label_drawer.h"

#include "app/definitions.h" // TILE_SIZE
#include "editor/editor.h"
#include "map/tile.h"

#include <algorithm>
#include <string>

namespace {
	// Mesmo portao do MapDrawer::DrawPaintedZoneLabels: a janela visivel MEDIDA EM
	// TILES cresce com o zoom, e em zoom out a varredura chegava a ~130 mil tiles por
	// frame. Acima deste zoom o rotulo simplesmente nao aparece -- e aqui, ao
	// contrario das instance zones, nao ha bounds guardados para cair de volta,
	// porque a flag nao tem identidade nem sidecar. Visto de tao longe o rotulo seria
	// ilegivel de qualquer jeito.
	constexpr float SCAN_MAX_ZOOM = 2.0f;

	// Folga em tiles ao redor da janela ao procurar SEMENTES. Serve para pegar a
	// arena que esta encostando na borda da tela.
	constexpr int SCAN_PAD = 2;

	// Teto de tiles por blob. As sementes saem da janela visivel, mas o preenchimento
	// em si NAO e recortado nela: ele segue a arena para fora da tela ate acabar.
	// Isso e de proposito -- recortar na viewport faria o centro (e portanto o rotulo)
	// escorregar enquanto o mapper arrasta o mapa, que era o defeito mais visivel da
	// primeira versao. O teto e o que impede uma pintura errada gigante de virar um
	// passeio pelo mapa inteiro: 64x64 tiles ja e uma arena enorme.
	constexpr size_t MAX_BLOB_TILES = 4096;

	// Teto de tiles processados no frame INTEIRO, somando todos os blobs. Segunda
	// trava, para o caso de alguem ter pintado a flag em muitos blobs de uma vez.
	constexpr size_t TILE_BUDGET = 32768;

	// Teto de rotulos por frame. Todos escrevem o mesmo texto: passando disso vira
	// ruido em cima do mapa, nao informacao.
	constexpr size_t MAX_LABELS = 24;

	// Ambar, o mesmo tom para onde o tint puxa o tile em tile_color_calculator.cpp
	// (g a ~78%, b a 25%). Tint e rotulo com a mesma cor fazem os dois lerem como uma
	// coisa so, e a cor nao briga com o azul do PZ nem com o laranja saturado do PVP.
	constexpr uint8_t LABEL_R = 255;
	constexpr uint8_t LABEL_G = 196;
	constexpr uint8_t LABEL_B = 64;

	inline uint64_t packKey(int x, int y) {
		return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y);
	}
} // namespace

void WorldBossLabelDrawer::draw(NVGcontext* vg, const RenderView& view, Editor& editor, int floor, ZoneLabelDrawer& label_drawer) {
	if (!vg || view.zoom > SCAN_MAX_ZOOM) {
		return;
	}

	const int map_w = editor.map.getWidth();
	const int map_h = editor.map.getHeight();
	if (map_w <= 0 || map_h <= 0) {
		return;
	}

	// Janela visivel em tiles, com folga. Mesma conta do DrawPaintedZoneLabels, so
	// que ja presa aos limites do mapa: o flood fill le vizinhos e nao quero testar
	// borda duas vezes.
	const int min_tile_x = std::max(0, view.view_scroll_x / TILE_SIZE - SCAN_PAD);
	const int min_tile_y = std::max(0, view.view_scroll_y / TILE_SIZE - SCAN_PAD);
	const int max_tile_x = std::min(map_w - 1, static_cast<int>((view.view_scroll_x + view.screensize_x * view.zoom) / TILE_SIZE) + SCAN_PAD);
	const int max_tile_y = std::min(map_h - 1, static_cast<int>((view.view_scroll_y + view.screensize_y * view.zoom) / TILE_SIZE) + SCAN_PAD);
	if (min_tile_x > max_tile_x || min_tile_y > max_tile_y) {
		return;
	}

	visited.clear();
	labels.clear();

	size_t processed = 0;

	auto isMarked = [&](int x, int y) -> bool {
		if (x < 0 || y < 0 || x >= map_w || y >= map_h) {
			return false;
		}
		const Tile* tile = editor.map.getTile(x, y, floor);
		return tile && (tile->getMapFlags() & TILESTATE_WORLDBOSS) != 0;
	};

	// Uma passada so pela janela procurando sementes; cada blob e percorrido uma vez
	// e nunca reaparece, porque `visited` e comum a todos eles.
	for (int seed_y = min_tile_y; seed_y <= max_tile_y && labels.size() < MAX_LABELS && processed < TILE_BUDGET; ++seed_y) {
		for (int seed_x = min_tile_x; seed_x <= max_tile_x && labels.size() < MAX_LABELS && processed < TILE_BUDGET; ++seed_x) {
			if (visited.count(packKey(seed_x, seed_y)) != 0 || !isMarked(seed_x, seed_y)) {
				continue;
			}

			pending.clear();
			pending.emplace_back(seed_x, seed_y);
			visited.insert(packKey(seed_x, seed_y));

			int min_x = seed_x, max_x = seed_x;
			int min_y = seed_y, max_y = seed_y;
			size_t count = 0;

			while (!pending.empty()) {
				const std::pair<int, int> cur = pending.back();
				pending.pop_back();

				++count;
				++processed;
				min_x = std::min(min_x, cur.first);
				max_x = std::max(max_x, cur.first);
				min_y = std::min(min_y, cur.second);
				max_y = std::max(max_y, cur.second);

				if (count >= MAX_BLOB_TILES || processed >= TILE_BUDGET) {
					// Estourou a trava: paramos de crescer e o rotulo fica no centro do que
					// ja foi achado. Prefiro um rotulo levemente descentralizado a um frame
					// que engasga.
					break;
				}

				// 8 vizinhos, nao 4. A flag e pintada a mao com brush por cima de chao
				// irregular, e um degrau na diagonal e comum: com 4 vizinhos UMA arena se
				// quebraria em varios blobs e o mapa ganharia tres "World Boss" empilhados.
				// O risco oposto -- duas arenas que se encostam so pela quina virarem um
				// blob so -- e raro e inofensivo: encostadas assim elas sao a mesma luta, e
				// a flag e por TILE de qualquer forma, o agrupamento aqui existe so para
				// decidir onde escrever o texto.
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						if (dx == 0 && dy == 0) {
							continue;
						}
						const int nx = cur.first + dx;
						const int ny = cur.second + dy;
						const uint64_t key = packKey(nx, ny);
						if (visited.count(key) != 0 || !isMarked(nx, ny)) {
							continue;
						}
						visited.insert(key);
						pending.emplace_back(nx, ny);
					}
				}
			}

			ZoneLabel label;
			label.text = "World Boss";
			// Contagem de tiles no lugar do nome que a flag nao tem: e o unico dado real
			// que existe sobre a area, e o que denuncia o tile solto pintado por engano
			// ("1 tile") ao lado da arena de verdade.
			label.subtext = (count == 1) ? std::string("1 tile") : (std::to_string(count) + " tiles");
			label.min_x = min_x;
			label.min_y = min_y;
			label.max_x = max_x;
			label.max_y = max_y;
			label.r = LABEL_R;
			label.g = LABEL_G;
			label.b = LABEL_B;
			label.icon = ZoneLabelIcon::Boss;
			labels.push_back(std::move(label));
		}
	}

	if (labels.empty()) {
		return;
	}

	label_drawer.draw(vg, view, floor, labels);
}
