//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// BlackTalon: modo "pintado por cima" das instance zones.
//
// O tint normal de zona e MULTIPLICATIVO (tile_color_calculator.cpp:73): o terreno
// continua aparecendo por baixo. Isso e o certo para editar DENTRO de uma zona e
// exatamente o errado para conferir o CONTORNO de uma zona grande -- chao claro
// quase nao recebe o tint, e o limite da area fica dificil de ler.
//
// Com este modo ligado, cada tile pintado recebe um quad OPACO na cor da zona: a
// area vira um bloco solido e a borda fica obvia em qualquer zoom.
//
// O preco: em opacidade total o preenchimento tambem esconde o destaque de SELECAO
// e o preview do brush, que sao desenhados na passada de sprites, antes desta. Por
// isso ele vem desligado e mora num toggle proprio -- e modo de CONFERIR, nao de
// editar. SOLID_ALPHA no .cpp e a unica constante a mexer se voce quiser que as
// camadas de baixo aparecam fantasmando.
//
// So INSTANCE zones. Fazer o mesmo para sound zones exigiria uma regra de
// precedencia para o tile que carrega as duas: dois preenchimentos opacos no mesmo
// tile significa que o ultimo a desenhar ganha, calado.

#ifndef RME_SOLID_ZONE_FILL_DRAWER_H_
#define RME_SOLID_ZONE_FILL_DRAWER_H_

#include "rendering/core/render_view.h"

class Editor;
struct NVGcontext;

class SolidZoneFillDrawer {
public:
	SolidZoneFillDrawer() = default;
	~SolidZoneFillDrawer() = default;

	void draw(NVGcontext* vg, const RenderView& view, Editor& editor);
};

#endif
