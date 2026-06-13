//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "brushes/ground/ground_border_calculator.h"
#include "brushes/ground/ground_brush.h"
#include "brushes/ground/auto_border.h"
#include "map/basemap.h"
#include "map/tile.h"
#include "map/tile_operations.h"
#include "game/item.h"
#include "app/settings.h"
#include "brushes/brush_enums.h"
#include <array>
#include <algorithm>

namespace {
	// Bitmasks shared with the lookup table builder.
	constexpr uint32_t MASK_NW = TILE_NORTHWEST;
	constexpr uint32_t MASK_N = TILE_NORTH;
	constexpr uint32_t MASK_NE = TILE_NORTHEAST;
	constexpr uint32_t MASK_W = TILE_WEST;
	constexpr uint32_t MASK_E = TILE_EAST;
	constexpr uint32_t MASK_SW = TILE_SOUTHWEST;
	constexpr uint32_t MASK_S = TILE_SOUTH;
	constexpr uint32_t MASK_SE = TILE_SOUTHEAST;

	// Carpet-like lookup table for ground borders. Mirrors the rules used by
	// CarpetBrush::carpet_types so ground borders behave like carpets when the
	// CARPET_LIKE_GROUND_BORDERS setting is on: ONE BorderType per tile,
	// favoring clean silhouettes over compositing multiple sprites.
	// See: source/brushes/carpet/carpet_brush_arrays.cpp
	constexpr auto ground_carpet_like_table = []() constexpr {
		std::array<uint32_t, 256> table {};
		for (int i = 0; i < 256; ++i) {
			const bool nw = i & MASK_NW;
			const bool n = i & MASK_N;
			const bool ne = i & MASK_NE;
			const bool w = i & MASK_W;
			const bool e = i & MASK_E;
			const bool sw = i & MASK_SW;
			const bool s = i & MASK_S;
			const bool se = i & MASK_SE;

			// 1. Full enclosed (NSEW): center, unless exactly one diagonal is missing.
			if (n && s && e && w) {
				const int missing_diag = (!nw) + (!ne) + (!sw) + (!se);
				if (missing_diag == 1) {
					if (!nw) {
						table[i] = SOUTHEAST_DIAGONAL;
					} else if (!ne) {
						table[i] = SOUTHWEST_DIAGONAL;
					} else if (!sw) {
						table[i] = NORTHEAST_DIAGONAL;
					} else if (!se) {
						table[i] = NORTHWEST_DIAGONAL;
					}
				} else {
					table[i] = CARPET_CENTER;
				}
				continue;
			}

			// 2. Three-way junctions
			if (n && s && w) {
				if (sw && nw) {
					table[i] = WEST_HORIZONTAL;
				} else if (sw) {
					table[i] = SOUTHWEST_CORNER;
				} else if (nw) {
					table[i] = NORTHWEST_CORNER;
				} else {
					table[i] = WEST_HORIZONTAL;
				}
				continue;
			}
			if (n && s && e) {
				table[i] = EAST_HORIZONTAL;
				continue;
			}
			if (n && w && e) {
				if (sw) {
					table[i] = NORTHWEST_CORNER;
				} else {
					table[i] = NORTH_HORIZONTAL;
				}
				continue;
			}
			if (s && w && e) {
				table[i] = SOUTH_HORIZONTAL;
				continue;
			}

			// 3. Two-way orthogonal
			if (n && w) {
				table[i] = NORTHWEST_CORNER;
				continue;
			}
			if (n && e) {
				table[i] = NORTHEAST_CORNER;
				continue;
			}
			if (s && w) {
				table[i] = SOUTHWEST_CORNER;
				continue;
			}
			if (s && e) {
				table[i] = SOUTHEAST_CORNER;
				continue;
			}

			if (n && s) {
				if (nw && sw) {
					table[i] = WEST_HORIZONTAL;
				} else if (nw) {
					table[i] = NORTHWEST_CORNER;
				} else if (sw) {
					table[i] = SOUTHWEST_CORNER;
				} else if (ne) {
					table[i] = NORTHEAST_CORNER;
				} else if (se) {
					table[i] = SOUTHEAST_CORNER;
				} else {
					table[i] = CARPET_CENTER;
				}
				continue;
			}
			if (w && e) {
				const bool n_side = nw || ne;
				const bool s_side = sw || se;
				if (sw && e && w) {
					table[i] = SOUTHWEST_CORNER;
				} else if (n_side && s_side) {
					table[i] = CARPET_CENTER;
				} else if (n_side) {
					table[i] = NORTH_HORIZONTAL;
				} else if (s_side) {
					table[i] = SOUTH_HORIZONTAL;
				} else {
					table[i] = CARPET_CENTER;
				}
				continue;
			}

			// 4. Single orthogonal
			if (n) {
				if (nw) {
					table[i] = NORTHWEST_CORNER;
				} else if (ne) {
					table[i] = NORTHEAST_CORNER;
				} else if (sw) {
					table[i] = SOUTHWEST_CORNER;
				} else if (se) {
					table[i] = SOUTHEAST_CORNER;
				} else {
					table[i] = CARPET_CENTER;
				}
				continue;
			}
			if (s) {
				if (sw) {
					table[i] = SOUTHWEST_CORNER;
				} else if (se) {
					table[i] = SOUTHEAST_CORNER;
				} else if (nw) {
					table[i] = NORTHWEST_CORNER;
				} else if (ne) {
					table[i] = NORTHEAST_CORNER;
				} else {
					table[i] = SOUTHWEST_CORNER;
				}
				continue;
			}
			if (w) {
				if (nw) {
					table[i] = WEST_HORIZONTAL;
				} else if (sw) {
					table[i] = SOUTHWEST_CORNER;
				} else if (se) {
					table[i] = SOUTH_HORIZONTAL;
				} else {
					table[i] = CARPET_CENTER;
				}
				continue;
			}
			if (e) {
				if (nw) {
					table[i] = NORTHEAST_CORNER;
				} else if (ne) {
					table[i] = NORTHEAST_CORNER;
				} else if (se) {
					table[i] = SOUTH_HORIZONTAL;
				} else {
					table[i] = CARPET_CENTER;
				}
				continue;
			}

			// 5. Pure diagonals
			if (nw && ne) {
				table[i] = NORTH_HORIZONTAL;
			} else if (sw && se) {
				table[i] = SOUTH_HORIZONTAL;
			} else if (nw && sw) {
				table[i] = WEST_HORIZONTAL;
			} else if (ne && se) {
				table[i] = EAST_HORIZONTAL;
			} else if (ne) {
				table[i] = NORTHEAST_CORNER;
			} else if (se) {
				table[i] = SOUTHEAST_CORNER;
			} else if (sw) {
				table[i] = SOUTHWEST_CORNER;
			} else {
				table[i] = CARPET_CENTER;
			}
		}
		return table;
	}();

	struct DiagonalComponent {
		BorderType diagonal;
		BorderType h1;
		BorderType h2;
	};
	constexpr std::array<DiagonalComponent, 4> diagonal_map = { {
		{ NORTHWEST_DIAGONAL, WEST_HORIZONTAL, NORTH_HORIZONTAL },
		{ NORTHEAST_DIAGONAL, EAST_HORIZONTAL, NORTH_HORIZONTAL },
		{ SOUTHWEST_DIAGONAL, SOUTH_HORIZONTAL, WEST_HORIZONTAL },
		{ SOUTHEAST_DIAGONAL, SOUTH_HORIZONTAL, EAST_HORIZONTAL },
	} };

	// The outer border_types table names pieces for the OUTER ring (tiles
	// around an area). On the inner ring (Carpet Fill) the cardinal
	// orientation stays the same, but convex and concave corners swap roles:
	// the top-left tile of a 2x2 block must use "cnw", where the outer table
	// answers NORTHWEST_DIAGONAL. Swap corner<->diagonal of the same name.
	constexpr BorderType swapCornerDiagonal(BorderType piece) {
		switch (piece) {
			case NORTHWEST_CORNER: return NORTHWEST_DIAGONAL;
			case NORTHEAST_CORNER: return NORTHEAST_DIAGONAL;
			case SOUTHWEST_CORNER: return SOUTHWEST_DIAGONAL;
			case SOUTHEAST_CORNER: return SOUTHEAST_DIAGONAL;
			case NORTHWEST_DIAGONAL: return NORTHWEST_CORNER;
			case NORTHEAST_DIAGONAL: return NORTHEAST_CORNER;
			case SOUTHWEST_DIAGONAL: return SOUTHWEST_CORNER;
			case SOUTHEAST_DIAGONAL: return SOUTHEAST_CORNER;
			default: return piece; // horizontals keep their orientation
		}
	}

	// True if this tile is governed by the Carpet Fill pipeline: it carries an
	// edge piece of a carpet_fill brush, or its ground belongs to one. All
	// other tiles keep the traditional auto-border pipeline, so neighbouring
	// traditional grounds (e.g. mountains) keep their borders intact.
	bool isCarpetFillTile(Tile* tile) {
		GroundBrush* ground = tile->getGroundBrush();
		if (ground && ground->isCarpetFill()) {
			return true;
		}
		for (const auto& item : tile->items) {
			if (item->isBorder() && GroundBrush::getCarpetPieceOwner(item->getID()) != nullptr) {
				return true;
			}
		}
		return false;
	}

	// True if the tile counts as part of `brush` for Carpet Fill purposes:
	// either its ground is the brush's filled center, or it carries one of the
	// brush's edge pieces (margin tiles keep their old ground underneath).
	bool belongsToCarpetBrush(Tile* tile, GroundBrush* brush, const AutoBorder* border) {
		if (!tile) {
			return false;
		}
		if (tile->getGroundBrush() == brush) {
			return true;
		}
		for (const auto& item : tile->items) {
			if (item->isBorder() && border->containsItem(item->getID())) {
				return true;
			}
		}
		return false;
	}

	// Carpet Fill mode (global toggle, CARPET_FILL_BORDERS): painting starts as
	// border pieces drawn on the tile itself, over whatever ground was already
	// there; the center ground item is only filled in once the tile is fully
	// surrounded (all 8 neighbours) by the same brush - like a carpet brush.
	// Pieces come from the brush's first outer border, so it works with any
	// existing ground brush without XML changes. While the mode is on this
	// replaces the normal cross-brush border pipeline.
	void calculateCarpetFill(BaseMap* map, Tile* tile) {
		// Resolve which brush is being carpet-filled BEFORE cleaning borders:
		// margin tiles keep their old ground, so the edge piece is the only
		// marker of which brush was painted here.
		GroundBrush* brush = nullptr;
		for (const auto& item : tile->items) {
			if (!item->isBorder()) {
				continue;
			}
			if (GroundBrush* owner = GroundBrush::getCarpetPieceOwner(item->getID())) {
				brush = owner;
				break;
			}
		}
		if (!brush) {
			GroundBrush* ground = tile->getGroundBrush();
			if (ground && ground->isCarpetFill()) {
				brush = ground;
			}
		}

		// Same border cleanup policy as the normal pipeline.
		const bool preserveManual = g_settings.getBoolean(Config::PRESERVE_MANUAL_BORDERS);
		std::erase_if(tile->items, [preserveManual](const std::unique_ptr<Item>& item) {
			if (!item->isBorder()) return false;
			return preserveManual ? item->isAutoPlaced() : true;
		});
		if (preserveManual) {
			TileOperations::cleanAutoBorders(tile);
		} else {
			TileOperations::cleanBorders(tile);
		}

		if (!brush) {
			return;
		}
		const AutoBorder* border = brush->getFirstOuterAutoBorder();
		if (!border) {
			return;
		}

		const Position& position = tile->getPosition();
		static constexpr std::array<std::pair<int32_t, int32_t>, 8> offsets = { { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } } };

		uint32_t tiledata = 0;
		for (size_t i = 0; i < offsets.size(); ++i) {
			Tile* neighbour = map->getTile(position.x + offsets[i].first, position.y + offsets[i].second, position.z);
			if (belongsToCarpetBrush(neighbour, brush, border)) {
				tiledata |= static_cast<uint32_t>(1) << i;
			}
		}

		// Fill in the center ground ONLY when all 8 neighbours belong to the
		// brush (pieces or filled grounds). A relaxed "touches a filled ground"
		// variant was tried and reverted: while painting, the margin between
		// the filled interior and a freshly painted row matches it too, so the
		// area fills almost to the edge and the ring collapses.
		if (tiledata == 0xFF) {
			if (tile->getGroundBrush() != brush) {
				uint16_t groundId = brush->getRandomGroundItemId();
				if (groundId != 0) {
					tile->setGround(Item::Create(groundId));
				}
			}
			return;
		}

		// Isolated tile (first click): start as a closed frame made of the four
		// convex corner pieces (cnw/cne/csw/cse), like a 1-tile ring. The
		// center ground only appears once all 8 neighbours are painted.
		if (tiledata == 0) {
			bool stamped = false;
			for (BorderType direction : { NORTHWEST_CORNER, NORTHEAST_CORNER, SOUTHWEST_CORNER, SOUTHEAST_CORNER }) {
				uint32_t id = border->getRandomTileId(direction);
				if (id != 0) {
					TileOperations::addBorderItem(tile, Item::Create(id));
					stamped = true;
				}
			}
			if (stamped) {
				return;
			}
			// Border set has no corner pieces: fall through to the lookup.
		}

		// Margin tile: keep the old ground underneath and stamp edge pieces.
		// Mirror the open-sides mask 180 degrees (reverse the 8 bits: N<->S,
		// W<->E, NW<->SE, NE<->SW) before the outer-table lookup, then swap
		// corner<->diagonal of the same name after it. Net effect, confirmed
		// piece by piece with the user's tileset: a 2x2 block stamps cse (top
		// left), csw (top right), cne (bottom left), cnw (bottom right) - the
		// fringe hugs the interior of the ring - and straight rows keep the
		// fringe on the inner side ("s" on the top row).
		const uint32_t open = (~tiledata) & 0xFF;
		uint32_t mirrored = 0;
		for (int b = 0; b < 8; ++b) {
			if (open & (1u << b)) {
				mirrored |= 1u << (7 - b);
			}
		}
		const uint32_t composition = GroundBrush::border_types[mirrored];
		for (int i = 0; i < 4; ++i) {
			BorderType rawDirection = static_cast<BorderType>((composition >> (8 * i)) & 0xFF);
			if (rawDirection == BORDER_NONE) {
				break;
			}
			BorderType direction = swapCornerDiagonal(rawDirection);
			uint32_t id = border->getRandomTileId(direction);
			if (id != 0) {
				TileOperations::addBorderItem(tile, Item::Create(id));
				continue;
			}
			// Piece missing from this border set: compose the corner/diagonal
			// from its two horizontal pieces, like the legacy pipeline does.
			auto it = std::ranges::find_if(diagonal_map, [direction](const auto& d) {
				return d.diagonal == direction || d.diagonal == swapCornerDiagonal(direction);
			});
			if (it != diagonal_map.end()) {
				uint32_t h1Id = border->getRandomTileId(it->h1);
				uint32_t h2Id = border->getRandomTileId(it->h2);
				if (h1Id != 0 && h2Id != 0) {
					TileOperations::addBorderItem(tile, Item::Create(h1Id));
					TileOperations::addBorderItem(tile, Item::Create(h2Id));
				}
			}
		}
	}
} // namespace

void GroundBorderCalculator::calculate(BaseMap* map, Tile* tile) {
	static const auto extractGroundBrushFromTile = [](BaseMap* map, int x, int y, int z) -> GroundBrush* {
		Tile* tile = map->getTile(x, y, z);
		if (tile) {
			return tile->getGroundBrush();
		}
		return nullptr;
	};

	ASSERT(tile);

	// Carpet Fill is resolved per tile: only tiles belonging to a carpet_fill
	// brush (edge pieces or filled ground) use the carpet pipeline. Everything
	// else runs the traditional auto-border below, untouched.
	if (g_settings.getBoolean(Config::CARPET_FILL_BORDERS) && isCarpetFillTile(tile)) {
		calculateCarpetFill(map, tile);
		return;
	}

	GroundBrush* borderBrush;
	if (tile->ground) {
		borderBrush = tile->ground->getGroundBrush();
	} else {
		borderBrush = nullptr;
	}

	const Position& position = tile->getPosition();
	int x = position.x;
	int y = position.y;
	int z = position.z;

	// Pair of visited / what border type
	std::pair<bool, GroundBrush*> neighbours[8] = {
		{ false, nullptr }, { false, nullptr }, { false, nullptr }, { false, nullptr }, { false, nullptr }, { false, nullptr }, { false, nullptr }, { false, nullptr }
	};

	static constexpr std::array<std::pair<int32_t, int32_t>, 8> offsets = { { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } } };

	for (size_t i = 0; i < offsets.size(); ++i) {
		const auto& [dx, dy] = offsets[i];

		int nx = x + dx;
		int ny = y + dy;

		neighbours[i] = { false, extractGroundBrushFromTile(map, nx, ny, z) };
	}

	static std::vector<const GroundBrush::BorderBlock*> specificList;
	specificList.clear();

	std::vector<GroundBrush::BorderCluster> borderList;
	for (int32_t i = 0; i < 8; ++i) {
		auto& [visited, other] = neighbours[i];
		if (visited) {
			continue;
		}

		if (borderBrush) {
			if (other) {
				if (other->getID() == borderBrush->getID()) {
					continue;
				}

				if (other->hasOuterBorder() || borderBrush->hasInnerBorder()) {
					bool only_mountain = false;
					if (/*!borderBrush->hasInnerBorder() && */ (other->friendOf(borderBrush) || borderBrush->friendOf(other))) {
						if (!other->hasOptionalBorder()) {
							continue;
						}
						only_mountain = true;
					}

					uint32_t tiledata = 0;
					for (int32_t j = i; j < 8; ++j) {
						auto& [other_visited, other_brush] = neighbours[j];
						if (!other_visited && other_brush && other_brush->getID() == other->getID()) {
							other_visited = true;
							tiledata |= 1 << j;
						}
					}

					if (tiledata != 0) {
						// Add mountain if appropriate!
						if (other->hasOptionalBorder() && tile->hasOptionalBorder()) {
							GroundBrush::BorderCluster borderCluster;
							borderCluster.alignment = tiledata;
							borderCluster.z = 0x7FFFFFFF; // Above all other borders
							borderCluster.layer_order = 0;
							borderCluster.border = other->optional_border;

							borderList.push_back(borderCluster);
							if (other->useSoloOptionalBorder()) {
								only_mountain = true;
							}
						}

						if (!only_mountain) {
							std::vector<const GroundBrush::BorderBlock*> borderBlocks = GroundBrush::getBrushesTo(borderBrush, other);
							for (const GroundBrush::BorderBlock* borderBlock : borderBlocks) {
								if (!borderBlock || !borderBlock->autoborder) {
									continue;
								}

								bool found = false;
								for (GroundBrush::BorderCluster& borderCluster : borderList) {
									if (borderCluster.border == borderBlock->autoborder && borderCluster.to == borderBlock->to) {
										borderCluster.alignment |= tiledata;
										if (borderCluster.z < other->getZ()) {
											borderCluster.z = other->getZ();
										}

										if (!borderBlock->specific_cases.empty()) {
											specificList.push_back(borderBlock);
										}

										found = true;
										break;
									}
								}

								if (!found) {
									GroundBrush::BorderCluster borderCluster;
									borderCluster.alignment = tiledata;
									borderCluster.z = other->getZ();
									borderCluster.layer_order = borderBlock->layer_order;
									borderCluster.border = borderBlock->autoborder;
									borderCluster.to = borderBlock->to;

									borderList.push_back(borderCluster);
									if (!borderBlock->specific_cases.empty()) {
										specificList.push_back(borderBlock);
									}
								}
							}
						}
					}
				}
				// Border against nothing (or undefined tile)
				uint32_t tiledata = 0;
				for (int32_t j = i; j < 8; ++j) {
					auto& [other_visited, other_brush] = neighbours[j];
					if (!other_visited && !other_brush) {
						other_visited = true;
						tiledata |= 1 << j;
					}
				}

				if (tiledata != 0) {
					std::vector<const GroundBrush::BorderBlock*> borderBlocks = GroundBrush::getBrushesTo(borderBrush, nullptr);
					for (const GroundBrush::BorderBlock* borderBlock : borderBlocks) {
						if (!borderBlock) {
							continue;
						}
						if (borderBlock->autoborder) {
							bool found = false;
							for (GroundBrush::BorderCluster& borderCluster : borderList) {
								if (borderCluster.border == borderBlock->autoborder && borderCluster.to == borderBlock->to) {
									borderCluster.alignment |= tiledata;
									borderCluster.z = -1000;
									found = true;
									break;
								}
							}

							if (!found) {
								GroundBrush::BorderCluster borderCluster;
								borderCluster.alignment = tiledata;
								borderCluster.z = -1000;
								borderCluster.layer_order = borderBlock->layer_order;
								borderCluster.border = borderBlock->autoborder;
								borderCluster.to = borderBlock->to;
								borderList.push_back(borderCluster);
							}
						}

						if (!borderBlock->specific_cases.empty()) {
							specificList.push_back(borderBlock);
						}
					}
				}
				continue;
			} else {
				// Border against nothing (or undefined tile)
				uint32_t tiledata = 0;
				for (int32_t j = i; j < 8; ++j) {
					auto& [other_visited, other_brush] = neighbours[j];
					if (!other_visited && !other_brush) {
						other_visited = true;
						tiledata |= 1 << j;
					}
				}

				if (tiledata != 0) {
					std::vector<const GroundBrush::BorderBlock*> borderBlocks = GroundBrush::getBrushesTo(borderBrush, nullptr);
					for (const GroundBrush::BorderBlock* borderBlock : borderBlocks) {
						if (!borderBlock) {
							continue;
						}
						if (borderBlock->autoborder) {
							bool found = false;
							for (GroundBrush::BorderCluster& borderCluster : borderList) {
								if (borderCluster.border == borderBlock->autoborder && borderCluster.to == borderBlock->to) {
									borderCluster.alignment |= tiledata;
									borderCluster.z = -1000;
									found = true;
									break;
								}
							}

							if (!found) {
								GroundBrush::BorderCluster borderCluster;
								borderCluster.alignment = tiledata;
								borderCluster.z = -1000;
								borderCluster.layer_order = borderBlock->layer_order;
								borderCluster.border = borderBlock->autoborder;
								borderCluster.to = borderBlock->to;
								borderList.push_back(borderCluster);
							}
						}

						if (!borderBlock->specific_cases.empty()) {
							specificList.push_back(borderBlock);
						}
					}
				}
				continue;
			}
		} else if (other && other->hasOuterZilchBorder()) {
			uint32_t tiledata = 0;
			for (int32_t j = i; j < 8; ++j) {
				auto& [other_visited, other_brush] = neighbours[j];
				if (!other_visited && other_brush && other_brush->getID() == other->getID()) {
					other_visited = true;
					tiledata |= 1 << j;
				}
			}

			if (tiledata != 0) {
				std::vector<const GroundBrush::BorderBlock*> borderBlocks = GroundBrush::getBrushesTo(nullptr, other);
				for (const GroundBrush::BorderBlock* borderBlock : borderBlocks) {
					if (!borderBlock) {
						continue;
					}
					if (borderBlock->autoborder) {
						bool found = false;
						for (GroundBrush::BorderCluster& borderCluster : borderList) {
							if (borderCluster.border == borderBlock->autoborder && borderCluster.to == borderBlock->to) {
								borderCluster.alignment |= tiledata;
								if (borderCluster.z < other->getZ()) {
									borderCluster.z = other->getZ();
								}
								found = true;
								break;
							}
						}

						if (!found) {
							GroundBrush::BorderCluster borderCluster;
							borderCluster.alignment = tiledata;
							borderCluster.z = other->getZ();
							borderCluster.layer_order = borderBlock->layer_order;
							borderCluster.border = borderBlock->autoborder;
							borderCluster.to = borderBlock->to;
							borderList.push_back(borderCluster);
						}
					}

					if (!borderBlock->specific_cases.empty()) {
						specificList.push_back(borderBlock);
					}
				}

				// Add mountain if appropriate!
				if (other->hasOptionalBorder() && tile->hasOptionalBorder()) {
					GroundBrush::BorderCluster borderCluster;
					borderCluster.alignment = tiledata;
					borderCluster.z = 0x7FFFFFFF; // Above all other borders
					borderCluster.layer_order = 0;
					borderCluster.border = other->optional_border;

					borderList.push_back(borderCluster);
				} else {
					tile->setOptionalBorder(false);
				}
			}
		}
		// Check tile as done
		visited = true;
	}

	// Clean borders before recomputing. If the user opted in to preserving manual
	// borders (the default), only auto-placed borders are wiped; otherwise we fall
	// back to the legacy behavior of clearing everything flagged as a border.
	const bool preserveManual = g_settings.getBoolean(Config::PRESERVE_MANUAL_BORDERS);
	std::erase_if(tile->items, [preserveManual](const std::unique_ptr<Item>& item) {
		if (!item->isBorder()) return false;
		return preserveManual ? item->isAutoPlaced() : true;
	});

	// Sort borders based on z-order, then layer_order (for multi-border layering)
	std::ranges::sort(borderList, [](const GroundBrush::BorderCluster& a, const GroundBrush::BorderCluster& b) {
		if (a.z != b.z) {
			return a.z < b.z;
		}
		return a.layer_order < b.layer_order;
	});

	std::ranges::sort(specificList);
	auto [first, last] = std::ranges::unique(specificList);
	specificList.erase(first, last);

	if (preserveManual) {
		TileOperations::cleanAutoBorders(tile);
	} else {
		TileOperations::cleanBorders(tile);
	}

	// When carpet-like ground borders are enabled, we mirror the carpet brush
	// pipeline: ONE BorderType per cluster (via ground_carpet_like_table) and
	// only one sprite gets stamped on the tile. This produces clean silhouettes
	// like the carpet brush instead of the legacy ground compositing behavior.
	const bool carpetLike = g_settings.getBoolean(Config::CARPET_LIKE_GROUND_BORDERS);

	while (!borderList.empty()) {
		GroundBrush::BorderCluster& borderCluster = borderList.back();
		if (!borderCluster.border) {
			borderList.pop_back();
			continue;
		}

		uint32_t border_alignment = borderCluster.alignment;

		auto tryDecomposeDiagonal = [&](BorderType direction) -> bool {
			auto it = std::ranges::find_if(diagonal_map, [direction](const auto& d) { return d.diagonal == direction; });
			if (it == diagonal_map.end()) {
				return false;
			}
			uint32_t h1Id = borderCluster.border->getRandomTileId(it->h1);
			uint32_t h2Id = borderCluster.border->getRandomTileId(it->h2);
			if (h1Id == 0 || h2Id == 0) {
				return false;
			}
			TileOperations::addBorderItem(tile, Item::Create(h1Id));
			TileOperations::addBorderItem(tile, Item::Create(h2Id));
			return true;
		};

		auto stampSingleSprite = [&](BorderType direction) {
			if (direction == BORDER_NONE || direction == CARPET_CENTER) {
				return; // CARPET_CENTER means "interior, no border" for grounds
			}
			uint32_t tileId = borderCluster.border->getRandomTileId(direction);
			if (tileId != 0) {
				TileOperations::addBorderItem(tile, Item::Create(tileId));
			} else {
				tryDecomposeDiagonal(direction);
			}
		};

		if (carpetLike) {
			// Carpet-like path: pick one BorderType from the carpet-style lookup
			// and stamp at most one sprite for this cluster.
			BorderType direction = static_cast<BorderType>(ground_carpet_like_table[border_alignment]);
			stampSingleSprite(direction);
		} else {
			// Legacy path: up to four BorderTypes per cluster, composed as layers.
			BorderType directions[4] = {
				static_cast<BorderType>((GroundBrush::border_types[border_alignment] & 0x000000FF) >> 0),
				static_cast<BorderType>((GroundBrush::border_types[border_alignment] & 0x0000FF00) >> 8),
				static_cast<BorderType>((GroundBrush::border_types[border_alignment] & 0x00FF0000) >> 16),
				static_cast<BorderType>((GroundBrush::border_types[border_alignment] & 0xFF000000) >> 24)
			};
			for (int32_t i = 0; i < 4; ++i) {
				BorderType direction = directions[i];
				if (direction == BORDER_NONE) {
					break;
				}
				stampSingleSprite(direction);
			}
		}

		borderList.pop_back();
	}

	for (const GroundBrush::BorderBlock* borderBlock : specificList) {
		for (const auto& specificCaseBlockPtr : borderBlock->specific_cases) {
			const GroundBrush::SpecificCaseBlock* specificCaseBlock = specificCaseBlockPtr.get();
			uint32_t matches = 0;
			for (const auto& item : tile->items) {
				if (!item->isBorder()) {
					break;
				}

				if (specificCaseBlock->match_group > 0) {
					if (item->getBorderGroup() == specificCaseBlock->match_group && item->getBorderAlignment() == specificCaseBlock->group_match_alignment) {
						++matches;
						continue;
					}
				}

				for (uint16_t matchId : specificCaseBlock->items_to_match) {
					if (item->getID() == matchId) {
						++matches;
					}
				}
			}

			if (matches >= specificCaseBlock->items_to_match.size()) {
				auto& tileItems = tile->items;
				auto it = tileItems.begin();

				// if delete_all mode, consider the border replaced
				bool replaced = specificCaseBlock->delete_all;

				while (it != tileItems.end()) {
					Item* item = it->get();
					if (!item->isBorder()) {
						++it;
						continue;
					}

					bool inc = true;
					for (uint16_t matchId : specificCaseBlock->items_to_match) {
						if (item->getID() == matchId) {
							if (!replaced && item->getID() == specificCaseBlock->to_replace_id) {
								// replace the matching border, delete everything else
								item->setID(specificCaseBlock->with_id);
								replaced = true;
							} else {
								if (specificCaseBlock->delete_all || !specificCaseBlock->keepBorder) {
									it = tileItems.erase(it);
									inc = false;
									break;
								}
							}
						}
					}

					if (inc) {
						++it;
					}
				}
			}
		}
	}
}
