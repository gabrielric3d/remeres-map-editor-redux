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

#include "app/main.h"
#include "rendering/ui/carpet_piece_service.h"

#include "brushes/brush.h"
#include "brushes/carpet/carpet_brush.h"
#include "brushes/raw/raw_brush.h"
#include "game/item.h"
#include "item_definitions/core/item_definition_store.h"
#include "map/basemap.h"
#include "map/tile.h"
#include "rendering/core/game_sprite.h"
#include "rendering/core/normal_image.h"
#include "ui/dialogs/carpet_editor_panel.h" // CARPET_ALIGN_ORDER, carpetAlignLabel
#include "ui/gui.h"
#include "util/common.h"

#include <algorithm>

namespace CarpetPieces {

	namespace {

		CarpetBrush* OwnerCarpet(uint16_t itemId) {
			const auto definition = g_item_definitions.get(itemId);
			if (!definition) {
				return nullptr;
			}
			Brush* owner = definition.editorData().brush;
			return owner ? owner->as<CarpetBrush>() : nullptr;
		}

		constexpr int EDGE_BINS = 8;
		constexpr uint8_t SOLID_ALPHA = 100;

		// Where the drawing of a one-tile sprite crosses each of its four edges.
		struct SpriteEdges {
			bool valid = false;
			std::array<uint8_t, 4> side {};
		};

		SpriteEdges ReadSpriteEdges(uint16_t itemId) {
			SpriteEdges edges;
			const auto definition = g_item_definitions.get(itemId);
			if (!definition) {
				return edges;
			}
			GameSprite* sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(definition.clientId()));
			// Border pieces are one tile; anything bigger is left out of the test.
			if (!sprite || sprite->width != 1 || sprite->height != 1 || sprite->spriteList.empty() || !sprite->spriteList[0]) {
				return edges;
			}
			NormalImage* image = sprite->spriteList[0];
			const ImageDimensions dimensions = image->getDimensions();
			const int width = dimensions.width;
			const int height = dimensions.height;
			if (width <= 0 || height <= 0) {
				return edges;
			}
			std::unique_ptr<uint8_t[]> rgba = image->getRGBAData();
			if (!rgba) {
				return edges;
			}

			// A couple of pixel lines deep, so a stroke that stops one pixel short of the
			// edge still counts as crossing it.
			const int depth = std::max(2, std::min(width, height) / 16);
			const auto solid = [&](int x, int y) {
				return rgba[(static_cast<size_t>(y) * width + x) * 4 + 3] >= SOLID_ALPHA;
			};
			for (int side = 0; side < 4; ++side) {
				const bool horizontal = side == EDGE_NORTH || side == EDGE_SOUTH;
				const int length = horizontal ? width : height;
				uint8_t bins = 0;
				for (int i = 0; i < length; ++i) {
					bool hit = false;
					for (int d = 0; d < depth && !hit; ++d) {
						switch (side) {
							case EDGE_NORTH:
								hit = solid(i, d);
								break;
							case EDGE_SOUTH:
								hit = solid(i, height - 1 - d);
								break;
							case EDGE_WEST:
								hit = solid(d, i);
								break;
							default:
								hit = solid(width - 1 - d, i);
								break;
						}
					}
					if (hit) {
						bins |= static_cast<uint8_t>(1u << (i * EDGE_BINS / length));
					}
				}
				edges.side[side] = bins;
			}
			edges.valid = true;
			return edges;
		}

		uint8_t Dilate(uint8_t bins) {
			return static_cast<uint8_t>(bins | (bins << 1) | (bins >> 1));
		}

		// Both sides of a shared edge: the drawing crosses it at the same places, with one
		// bin of slack for strokes that wander a few pixels. A stroke that reaches the edge
		// on one side only means the drawing would break off there.
		bool EdgesMeet(uint8_t a, uint8_t b) {
			return (a & static_cast<uint8_t>(~Dilate(b))) == 0 && (b & static_cast<uint8_t>(~Dilate(a))) == 0;
		}

	} // namespace

	CarpetBrush* ResolveSource(Brush* brush) {
		if (!brush) {
			return nullptr;
		}
		if (CarpetBrush* carpet = brush->as<CarpetBrush>()) {
			return carpet;
		}
		if (RAWBrush* raw = brush->as<RAWBrush>()) {
			return OwnerCarpet(raw->getItemID());
		}
		return nullptr;
	}

	std::vector<Piece> ListPieces(const CarpetBrush& carpet) {
		std::vector<Piece> pieces;
		const auto& groups = carpet.getItems().getGroups();
		for (BorderType align : CARPET_ALIGN_ORDER) {
			const auto& items = groups[align].items;
			const int count = static_cast<int>(items.size());
			for (int i = 0; i < count; ++i) {
				pieces.push_back({ align, items[i].id, i, count });
			}
		}
		return pieces;
	}

	std::optional<Piece> FirstPiece(const CarpetBrush& carpet, BorderType align) {
		const auto& groups = carpet.getItems().getGroups();
		if (align <= BORDER_NONE || static_cast<size_t>(align) >= groups.size()) {
			return std::nullopt;
		}
		const auto& items = groups[align].items;
		if (items.empty()) {
			return std::nullopt;
		}
		return Piece { align, items.front().id, 0, static_cast<int>(items.size()) };
	}

	std::optional<Piece> CurrentPiece(const CarpetBrush& carpet) {
		Brush* brush = g_gui.GetCurrentBrush();
		RAWBrush* raw = brush ? brush->as<RAWBrush>() : nullptr;
		if (!raw) {
			return std::nullopt;
		}
		const uint16_t itemId = raw->getItemID();
		for (const Piece& piece : ListPieces(carpet)) {
			if (piece.itemId == itemId) {
				return piece;
			}
		}
		return std::nullopt;
	}

	bool SelectPiece(const CarpetBrush& carpet, const Piece& piece) {
		const auto definition = g_item_definitions.get(piece.itemId);
		if (!definition) {
			return false;
		}
		RAWBrush* raw = definition.editorData().raw_brush;
		if (!raw) {
			return false;
		}
		// SelectBrushInternal leaves the palette alone: the Terrain tab keeps showing the
		// tileset and the carpet the piece came from.
		g_gui.SelectBrushInternal(raw);
		g_gui.SetStatusText(Describe(carpet, piece));
		return true;
	}

	void SelectSource(CarpetBrush* carpet) {
		if (carpet) {
			g_gui.SelectBrush(carpet, TILESET_TERRAIN);
		}
	}

	wxString Describe(const CarpetBrush& carpet, const Piece& piece) {
		wxString text = wxString::Format("%s: %u - %s (%s)", wxstr(carpet.getName()), static_cast<unsigned int>(piece.itemId), carpetAlignLabel(piece.align), carpetAlignToString(piece.align));
		if (piece.alternateCount > 1) {
			text += wxString::Format(" [%d/%d]", piece.alternate + 1, piece.alternateCount);
		}
		return text;
	}

	wxString DescribeSource(const CarpetBrush& carpet) {
		return wxstr(carpet.getName()) + " - auto brush";
	}

	NeighbourEdges ReadNeighbourEdges(const CarpetBrush& carpet, BaseMap& map, const Position& position) {
		static constexpr int DX[4] = { 0, 1, 0, -1 }; // N, E, S, W
		static constexpr int DY[4] = { -1, 0, 1, 0 };

		NeighbourEdges result;
		for (int side = 0; side < 4; ++side) {
			const int x = position.x + DX[side];
			const int y = position.y + DY[side];
			if (x < 0 || y < 0) {
				continue;
			}
			const Tile* tile = map.getTile(x, y, position.z);
			if (!tile) {
				continue;
			}
			// The neighbour's edge that touches the tile is on its opposite side. Stacked
			// pieces of the same carpet draw together, so their profiles add up.
			const int facing_side = (side + 2) % 4;
			uint8_t facing = 0;
			bool found = false;
			for (const auto& item : tile->items) {
				if (!item || OwnerCarpet(item->getID()) != &carpet) {
					continue;
				}
				const SpriteEdges edges = ReadSpriteEdges(item->getID());
				if (!edges.valid) {
					continue;
				}
				facing |= edges.side[facing_side];
				found = true;
			}
			if (found) {
				result.facing[side] = facing;
			}
		}
		return result;
	}

	bool FitsNeighbours(uint16_t itemId, const NeighbourEdges& neighbours) {
		if (!neighbours.any()) {
			return true;
		}
		const SpriteEdges edges = ReadSpriteEdges(itemId);
		if (!edges.valid) {
			return true;
		}
		for (int side = 0; side < 4; ++side) {
			const auto& facing = neighbours.facing[side];
			if (facing && !EdgesMeet(edges.side[side], *facing)) {
				return false;
			}
		}
		return true;
	}

} // namespace CarpetPieces
