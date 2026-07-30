//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/drawers/overlays/solid_zone_fill_drawer.h"

#include "app/definitions.h" // TILE_SIZE
#include "editor/editor.h"
#include "map/tile.h"
#include "rendering/drawers/tiles/tile_color_calculator.h"

#include <nanovg.h>

namespace {
	// 255 = cobre tudo, que e o pedido: "totalmente pintada". Baixar para ~225 deixa
	// selecao e preview do brush fantasmarem por baixo, ao custo de o bloco nao ler
	// mais como solido. E a unica constante a mexer aqui.
	constexpr uint8_t SOLID_ALPHA = 255;
}

void SolidZoneFillDrawer::draw(NVGcontext* vg, const RenderView& view, Editor& editor) {
	if (!vg || editor.map.instance_zones.empty()) {
		return;
	}

	const float zoom = (view.zoom > 0.0f) ? view.zoom : 1.0f;
	const float tile_size = static_cast<float>(TILE_SIZE) / zoom;
	const int floor = view.floor;

	nvgSave(vg);

	// A cor muda por TILE (cada zona tem a sua), entao nao da para agrupar tudo num
	// nvgFillColor. O custo fica no numero de tiles da janela visivel -- o mesmo que
	// os outros overlays por tile ja pagam (ver pathing_overlay_drawer).
	for (int y = view.start_y; y <= view.end_y; ++y) {
		for (int x = view.start_x; x <= view.end_x; ++x) {
			Tile* tile = editor.map.getTile(x, y, floor);
			if (!tile || !tile->isInstanceZoneTile()) {
				continue;
			}

			int ux, uy;
			if (!view.IsTileVisible(x, y, floor, ux, uy)) {
				continue;
			}

			uint8_t r = 255, g = 255, b = 255;
			TileColorCalculator::GetInstanceZoneColor(tile->getInstanceZoneId(), r, g, b);

			// IsTileVisible devolve coordenada de MAPA, nao de janela: dividir pelo
			// zoom e obrigatorio, senao o preenchimento descola do terreno em zoom out.
			nvgBeginPath(vg);
			nvgRect(vg, ux / zoom, uy / zoom, tile_size, tile_size);
			nvgFillColor(vg, nvgRGBA(r, g, b, SOLID_ALPHA));
			nvgFill(vg);
		}
	}

	nvgRestore(vg);
}
