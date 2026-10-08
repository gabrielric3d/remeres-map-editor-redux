//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#ifndef RME_MAP_LAYER_DRAWER_H
#define RME_MAP_LAYER_DRAWER_H

#include <cstdint>
#include <iosfwd>
#include <vector>

class Editor;
class TileRenderer;
class GridDrawer;
class ChunkCacheManager;
struct RenderView;
struct DrawingOptions;
struct LightBuffer;
struct RenderFrameContext;
class SpriteBatch;
class PrimitiveRenderer;
class TileLocation;

class MapLayerDrawer {
public:
	MapLayerDrawer(TileRenderer* tile_renderer, GridDrawer* grid_drawer, Editor* editor);
	~MapLayerDrawer();

	// chunk_cache and ctx are optional: with both set and the floor eligible,
	// the ground and border passes come from the GPU chunk cache instead of
	// being walked tile by tile. The contents pass always runs on the CPU.
	void Draw(SpriteBatch& sprite_batch, int map_z, bool live_client, const RenderView& view, const DrawingOptions& options, LightBuffer& light_buffer, ChunkCacheManager* chunk_cache = nullptr, const RenderFrameContext* ctx = nullptr);

private:
	// Um tile visivel do andar, na ordem de desenho do perfil do cliente
	// (RenderOrder::tileKey). Reusados entre frames para nao alocar.
	struct OrderedTile {
		TileLocation* location = nullptr;
		int draw_x = 0;
		int draw_y = 0;
		int map_x = 0;
		int map_y = 0;
	};
	// Tile deferido pelo chunk cache, com a chave da ordem global.
	struct DeferredTile {
		uint64_t key = 0;
		int map_x = 0;
		int map_y = 0;
	};

	// Ordena ordered_tiles_ pela chave do perfil: dois counting sorts estaveis
	// (x, depois a chave primaria -- y ou x+y), O(n) no numero de tiles.
	void sortOrderedTiles(bool battle_royale);

	TileRenderer* tile_renderer;
	GridDrawer* grid_drawer;
	Editor* editor;

	std::vector<OrderedTile> ordered_tiles_;
	std::vector<OrderedTile> ordered_scratch_;
	std::vector<uint32_t> sort_counts_;
	std::vector<DeferredTile> deferred_tiles_;
};

#endif
