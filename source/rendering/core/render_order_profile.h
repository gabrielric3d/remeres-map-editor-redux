//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_CORE_RENDER_ORDER_PROFILE_H_
#define RME_RENDERING_CORE_RENDER_ORDER_PROFILE_H_

#include <cstdint>

// Ordem de desenho do mapa, por cliente.
//
// Os dois jogos que o editor atende desenham o mesmo mapa em ordens diferentes,
// e o editor tem de mostrar o que o jogador ve. Os dois perfis sao leituras do
// codigo de cada cliente:
//
// Black Talon (OTClientV8, 10.98) -- mapview.cpp / tile.cpp do fork:
//   - tiles do andar em ordem de LINHA (y, depois x);
//   - passada A: chao + bordas de todos os tiles; passada B: o resto, tile a
//     tile. Um chao SEM FullGround e redesenhado na passada B, na vez do tile --
//     entao so um chao maior que o tile (ou deslocado) e sem FullGround sobe
//     para o conteudo e cobre os vizinhos ja desenhados.
//
// Battle Royale (mehah/otclient-redemption) -- DrawPool com camadas por andar:
//   - tiles em DIAGONAL (x+y crescente; na diagonal, de sudoeste para nordeste);
//   - FIRST: todo ground 1x1; SECOND: toda borda 1x1 e a "decoracao rasteira"
//     (custom do fork); THIRD: todo o resto, inclusive ground e borda maiores
//     que um tile, em ordem de tile.
//
// Nos dois a elevacao acumula do chao ate os itens comuns (teto de 24 px), a
// criatura sai na altura acumulada e os itens "on top" saem por ultimo, sem
// elevacao. O classificador fica em rendering/core/render_order.h.
enum class RenderOrderProfile : uint8_t {
	BlackTalon = 0,
	BattleRoyale = 1,
};

#endif
