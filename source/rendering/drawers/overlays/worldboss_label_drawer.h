//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// BlackTalon: escreve "World Boss" no meio de cada arena marcada com a flag
// TILESTATE_WORLDBOSS (0x40, a mesma OTBM_TILEFLAG_WORLDBOSS que o servidor le).
//
// POR QUE ESTE ARQUIVO EXISTE (e por que nao da para reusar o caminho das zonas):
// zona de instancia e zona de som tem IDENTIDADE -- um id gravado no tile mais um
// sidecar com nome e bounds. World Boss e UM BIT. Nao ha id, nao ha nome, nao ha
// regiao declarada em lugar nenhum: duas arenas em cantos opostos do mapa sao
// indistinguiveis, os dois tiles apenas dizem "sim".
//
// Entao a regiao precisa ser DESCOBERTA na hora: agrupamos os tiles marcados em
// componentes conexos (flood fill) e cada componente vira um rotulo. Depois disso
// o desenho e o molde ja existente -- montamos ZoneLabel e entregamos ao
// ZoneLabelDrawer, que ja sabe posicionar, dimensionar, contornar e prender o
// rotulo dentro da janela.
//
// As travas de custo (portao de zoom, teto por blob, orcamento de tiles, teto de
// rotulos) estao todas em constantes no comeco do .cpp, cada uma com o motivo.

#ifndef RME_WORLDBOSS_LABEL_DRAWER_H_
#define RME_WORLDBOSS_LABEL_DRAWER_H_

#include "rendering/core/render_view.h"
#include "rendering/drawers/overlays/zone_label_drawer.h"

#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

class Editor;
struct NVGcontext;

class WorldBossLabelDrawer {
public:
	WorldBossLabelDrawer() = default;
	~WorldBossLabelDrawer() = default;

	// Agrupa os tiles marcados que tocam a janela visivel e manda os rotulos para
	// label_drawer. Ele vem de FORA de proposito: e o mesmo objeto que desenha os
	// rotulos de sound/instance zone, entao o visual nunca diverge entre eles.
	void draw(NVGcontext* vg, const RenderView& view, Editor& editor, int floor, ZoneLabelDrawer& label_drawer);

private:
	// Buffers reaproveitados entre frames. O agrupamento roda UMA vez por frame (nao
	// uma vez por blob), e alocar/destruir tres containers por frame so para joga-los
	// fora seria desperdicio puro. O conteudo nao sobrevive ao frame: draw() limpa
	// tudo na entrada.
	std::unordered_set<uint64_t> visited;
	std::vector<std::pair<int, int>> pending;
	std::vector<ZoneLabel> labels;
};

#endif
