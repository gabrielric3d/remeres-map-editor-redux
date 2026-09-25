//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_CORE_TILE_INSTANCE_H_
#define RME_RENDERING_CORE_TILE_INSTANCE_H_

#include <cstdint>

/**
 * TileInstance represents a single sprite instance in a chunk buffer.
 * Decoupled from atlas texture coordinates via sprite_id indirection (SpriteAtlasLUT).
 *
 * 48 bytes per instance (aligned to 16 bytes for GPU std430/std140 alignment).
 */
// flags: o item ja esta selecionado (tint pela metade assado no buffer). O shader
// usa para nao escurecer de novo quando ele cai dentro do retangulo de selecao.
constexpr uint32_t TILE_INSTANCE_SELECTED = 1u << 8;

struct alignas(16) TileInstance {
	float x = 0.0f;          // Byte 0-3: Screen X
	float y = 0.0f;          // Byte 4-7: Screen Y
	float w = 32.0f;         // Byte 8-11: Width
	float h = 32.0f;         // Byte 12-15: Height
	uint32_t sprite_id = 0;  // Byte 16-19: Stable sprite ID (O(1) LUT lookup in GPU SSBO)
	uint32_t flags = 0;      // Byte 20-23: Bit 8: TILE_INSTANCE_SELECTED
	float r = 1.0f;          // Byte 24-27: Red tint [0.0, 1.0]
	float g = 1.0f;          // Byte 28-31: Green tint [0.0, 1.0]
	float b = 1.0f;          // Byte 32-35: Blue tint [0.0, 1.0]
	float a = 1.0f;          // Byte 36-39: Alpha [0.0, 1.0]
	// Byte 40-43: tile dono do sprite, x nos bits 0-15 e y nos 16-31. E o que o
	// retangulo de selecao (arrasto) testa no shader, como o BlitItem testa pos.
	uint32_t tile_xy = 0;
	// Byte 44-47: 0 = sprite estatico; senao indice+1 da sequencia de animacao
	// (ChunkCacheManager), cujo frame corrente o shader busca a cada quadro.
	uint32_t anim_seq = 0;
};

static_assert(sizeof(TileInstance) == 48, "TileInstance must be exactly 48 bytes");

#endif
