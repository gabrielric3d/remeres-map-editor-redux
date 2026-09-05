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

	// The 8 neighbours in TILE_* bit order: bit i of a mask <-> kNeighbourOffsets[i].
	constexpr std::array<std::pair<int32_t, int32_t>, 8> kNeighbourOffsets = { { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } } };

	// Carpet Fill lookup: the piece a MEMBER tile shows, indexed by the mask of
	// its neighbours that belong to the same brush (edge piece or filled
	// ground). Same geometry as CarpetBrush::carpet_types - a piece is named
	// after the direction the region continues in, so the top row of a block
	// shows "s" and its top-left corner "cse" - but without that table's legacy
	// quirks, which were written for full carpet tiles and look broken with
	// thin ground fringes:
	//  - all four sides present: CARPET_CENTER (the brush's own ground), unless
	//    exactly one diagonal is missing -> the concave "d" piece opposite it;
	//  - three sides: the straight piece facing the interior;
	//  - two adjacent sides: the convex corner facing the interior;
	//  - two opposite sides, one side or no neighbour at all: CARPET_CENTER, so
	//    1-wide strips and single clicks are filled tiles, like a lone carpet;
	//  - diagonal-only contact: the corner or straight piece towards it.
	constexpr auto ground_carpet_table = []() constexpr {
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
			const int sides = n + s + e + w;

			uint32_t piece = CARPET_CENTER;
			if (sides == 4) {
				const int missing = (!nw) + (!ne) + (!sw) + (!se);
				if (missing == 1) {
					if (!nw) {
						piece = SOUTHEAST_DIAGONAL;
					} else if (!ne) {
						piece = SOUTHWEST_DIAGONAL;
					} else if (!sw) {
						piece = NORTHEAST_DIAGONAL;
					} else {
						piece = NORTHWEST_DIAGONAL;
					}
				}
			} else if (sides == 3) {
				if (!s) {
					piece = NORTH_HORIZONTAL;
				} else if (!n) {
					piece = SOUTH_HORIZONTAL;
				} else if (!e) {
					piece = WEST_HORIZONTAL;
				} else {
					piece = EAST_HORIZONTAL;
				}
			} else if (sides == 2) {
				if (n && w) {
					piece = NORTHWEST_CORNER;
				} else if (n && e) {
					piece = NORTHEAST_CORNER;
				} else if (s && w) {
					piece = SOUTHWEST_CORNER;
				} else if (s && e) {
					piece = SOUTHEAST_CORNER;
				}
			} else if (sides == 0) {
				const int diagonals = nw + ne + sw + se;
				if (diagonals == 2) {
					if (nw && ne) {
						piece = NORTH_HORIZONTAL;
					} else if (sw && se) {
						piece = SOUTH_HORIZONTAL;
					} else if (nw && sw) {
						piece = WEST_HORIZONTAL;
					} else if (ne && se) {
						piece = EAST_HORIZONTAL;
					}
				} else if (diagonals == 1) {
					if (nw) {
						piece = NORTHWEST_CORNER;
					} else if (ne) {
						piece = NORTHEAST_CORNER;
					} else if (sw) {
						piece = SOUTHWEST_CORNER;
					} else {
						piece = SOUTHEAST_CORNER;
					}
				}
			}
			table[i] = piece;
		}
		return table;
	}();

	static_assert(ground_carpet_table[0] == CARPET_CENTER); // single click: filled tile
	static_assert(ground_carpet_table[0xFF] == CARPET_CENTER);
	static_assert(ground_carpet_table[MASK_N | MASK_S] == CARPET_CENTER); // 1-wide strip
	static_assert(ground_carpet_table[MASK_E | MASK_S | MASK_SE] == SOUTHEAST_CORNER); // top-left of a block
	static_assert(ground_carpet_table[MASK_W | MASK_E | MASK_S | MASK_SW | MASK_SE] == SOUTH_HORIZONTAL); // top row
	static_assert(ground_carpet_table[MASK_N | MASK_S | MASK_E | MASK_NE | MASK_SE] == EAST_HORIZONTAL); // left column
	static_assert(ground_carpet_table[0xFF & ~MASK_NE] == SOUTHWEST_DIAGONAL); // concave corner

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

	// Convex corner -> concave piece of the same name (BORDER_NONE for the rest).
	constexpr BorderType cornerToDiagonal(BorderType piece) {
		switch (piece) {
			case NORTHWEST_CORNER: return NORTHWEST_DIAGONAL;
			case NORTHEAST_CORNER: return NORTHEAST_DIAGONAL;
			case SOUTHWEST_CORNER: return SOUTHWEST_DIAGONAL;
			case SOUTHEAST_CORNER: return SOUTHEAST_DIAGONAL;
			default: return BORDER_NONE;
		}
	}

	// ---- Carpet Fill ---------------------------------------------------------
	// A carpet_fill ground brush (with Edit > Border Options > Carpet Fill
	// Borders as the master switch) paints like a carpet brush: every painted
	// tile is a member of the brush's region. Members surrounded as per
	// ground_carpet_table get the brush's own ground; the others keep whatever
	// ground was there and show ONE edge piece of the brush's outer border,
	// facing the interior. The layer is additive: the traditional pipeline still
	// runs on the same tile for every other brush around it (mountains, water,
	// ...), so a carpet brush never disturbs another ground and vice versa.

	// True if the tile counts as part of `brush`'s region: its ground is the
	// brush's filled ground, or it carries one of the brush's edge pieces.
	bool belongsToCarpetBrush(const Tile* tile, const GroundBrush* brush) {
		if (!tile) {
			return false;
		}
		if (tile->getGroundBrush() == brush) {
			return true;
		}
		for (const auto& item : tile->items) {
			if (item->isBorder() && brush->ownsCarpetPiece(item->getID())) {
				return true;
			}
		}
		return false;
	}

	bool hasMemberNeighbour(BaseMap* map, const Tile* tile, const GroundBrush* brush) {
		const Position& position = tile->getPosition();
		for (const auto& [dx, dy] : kNeighbourOffsets) {
			if (belongsToCarpetBrush(map->getTile(position.x + dx, position.y + dy, position.z), brush)) {
				return true;
			}
		}
		return false;
	}

	// Which carpet brush this tile belongs to, if any. An edge piece on the tile
	// wins over its ground: painting brush B over a filled tile of brush C leaves
	// C's ground underneath B's piece, and the tile is B's from then on.
	GroundBrush* resolveCarpetBrush(BaseMap* map, Tile* tile) {
		GroundBrush* groundBrush = tile->getGroundBrush();
		for (const auto& item : tile->items) {
			if (!item->isBorder()) {
				continue;
			}
			const std::vector<GroundBrush*>& owners = GroundBrush::getCarpetPieceOwners(item->getID());
			GroundBrush* fallback = nullptr;
			for (GroundBrush* owner : owners) {
				if (!owner->paintsAsCarpet()) {
					continue;
				}
				if (owners.size() == 1 || owner == groundBrush || hasMemberNeighbour(map, tile, owner)) {
					return owner;
				}
				if (!fallback) {
					fallback = owner; // Shared border set with nobody around: first declared wins.
				}
			}
			if (fallback) {
				return fallback;
			}
		}
		if (groundBrush && groundBrush->paintsAsCarpet()) {
			return groundBrush;
		}
		return nullptr;
	}

	struct CarpetLayer {
		GroundBrush* brush = nullptr; // Region this tile belongs to (nullptr: none)
		const AutoBorder* border = nullptr; // Outer border the pieces come from
		uint32_t members = 0; // Neighbours belonging to the same region, TILE_* bits
		BorderType piece = BORDER_NONE; // Piece to stamp; BORDER_NONE for filled tiles
	};

	// Resolves the carpet layer of a tile and, when the tile is a filled
	// position of its region, sets the brush's ground on it right away.
	CarpetLayer resolveCarpetLayer(BaseMap* map, Tile* tile) {
		CarpetLayer layer;
		if (!g_settings.getBoolean(Config::CARPET_FILL_BORDERS)) {
			return layer;
		}
		layer.brush = resolveCarpetBrush(map, tile);
		if (!layer.brush) {
			return layer;
		}
		layer.border = layer.brush->getActiveOuterAutoBorder();

		const Position& position = tile->getPosition();
		for (size_t i = 0; i < kNeighbourOffsets.size(); ++i) {
			const auto& [dx, dy] = kNeighbourOffsets[i];
			if (belongsToCarpetBrush(map->getTile(position.x + dx, position.y + dy, position.z), layer.brush)) {
				layer.members |= 1u << i;
			}
		}

		const BorderType piece = static_cast<BorderType>(ground_carpet_table[layer.members]);
		if (piece == CARPET_CENTER) {
			// Filled tile: the brush's own ground and no edge pieces.
			if (tile->getGroundBrush() != layer.brush) {
				const uint16_t groundId = layer.brush->getRandomGroundItemId();
				if (groundId != 0) {
					tile->setGround(Item::Create(groundId));
				}
			}
			return layer;
		}
		// A tile that already carries the brush's ground (a filled tile that lost
		// a neighbour) keeps it: the old ground is gone for good, and a fringe
		// over the brush's own ground would be invisible anyway.
		if (tile->getGroundBrush() != layer.brush) {
			layer.piece = piece;
		}
		return layer;
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

	// Carpet Fill layer, resolved first: it may fill the tile with its brush's
	// ground, which the traditional analysis below must see. Tiles outside any
	// carpet region get an empty layer and run exactly the legacy pipeline.
	const CarpetLayer carpet = resolveCarpetLayer(map, tile);

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

	for (size_t i = 0; i < kNeighbourOffsets.size(); ++i) {
		const auto& [dx, dy] = kNeighbourOffsets[i];

		int nx = x + dx;
		int ny = y + dy;

		neighbours[i] = { false, extractGroundBrushFromTile(map, nx, ny, z) };

		// Inside a carpet region every member counts as the region's brush, so a
		// filled tile never borders against the old grounds still lying under
		// the edge tiles around it.
		if (carpet.brush && borderBrush == carpet.brush && (carpet.members & (1u << i))) {
			neighbours[i].second = carpet.brush;
		}
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

	// Carpet Fill layer: one more cluster, layered by its brush's z-order like
	// any other border (see ground_carpet_table for the piece choice).
	if (carpet.piece != BORDER_NONE && carpet.border) {
		GroundBrush::BorderCluster carpetCluster;
		carpetCluster.alignment = carpet.members;
		carpetCluster.z = carpet.brush->getZ();
		carpetCluster.layer_order = 0;
		carpetCluster.border = carpet.border;
		carpetCluster.carpet_piece = carpet.piece;
		borderList.push_back(carpetCluster);
	}

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

		if (borderCluster.carpet_piece != BORDER_NONE) {
			// Carpet Fill layer: exactly one piece on the member tile itself. A
			// border set without this convex corner falls back to the concave
			// piece of the same name, which still marks the tile as a member.
			BorderType piece = borderCluster.carpet_piece;
			if (borderCluster.border->getRandomTileId(piece) == 0) {
				const BorderType diagonal = cornerToDiagonal(piece);
				if (diagonal != BORDER_NONE) {
					piece = diagonal;
				}
			}
			stampSingleSprite(piece);
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
