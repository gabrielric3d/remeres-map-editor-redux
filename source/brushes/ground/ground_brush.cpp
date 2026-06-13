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

#include <algorithm>
#include <unordered_map>
#include "brushes/ground/ground_brush.h"
#include "brushes/ground/auto_border.h"
#include "brushes/ground/ground_brush_loader.h"
#include "brushes/ground/ground_border_calculator.h"
#include "brushes/ground/terrain_placement.h"
#include "map/basemap.h"
#include "map/tile_operations.h"
#include "game/item.h"
#include "app/settings.h"

uint32_t GroundBrush::border_types[256];

namespace {
	// Carpet Fill: edge-piece item id -> owning ground brush. Populated by the
	// loader from each brush's outer borders; first registration wins when a
	// border set is shared between brushes. Cleared with Brushes::clear().
	std::unordered_map<uint16_t, GroundBrush*> carpet_piece_owners;
}

GroundBrush* GroundBrush::getCarpetPieceOwner(uint16_t itemId) {
	auto it = carpet_piece_owners.find(itemId);
	return it != carpet_piece_owners.end() ? it->second : nullptr;
}

void GroundBrush::registerCarpetPieceOwner(uint16_t itemId, GroundBrush* brush) {
	carpet_piece_owners.emplace(itemId, brush);
}

void GroundBrush::clearCarpetPieceOwners() {
	carpet_piece_owners.clear();
}

GroundBrush::GroundBrush() :
	z_order(0),
	has_zilch_outer_border(false),
	has_zilch_inner_border(false),
	has_outer_border(false),
	has_inner_border(false),
	optional_border(nullptr),
	use_only_optional(false),
	randomize(true),
	carpet_fill(false),
	total_chance(0) {
	////
}

GroundBrush::~GroundBrush() {
}

bool GroundBrush::load(pugi::xml_node node, std::vector<std::string>& warnings) {
	return GroundBrushLoader::load(*this, node, warnings);
}

void GroundBrush::undraw(BaseMap* map, Tile* tile) {
	ASSERT(tile);
	if (carpet_fill && g_settings.getBoolean(Config::CARPET_FILL_BORDERS)) {
		// Carpet Fill margin tiles carry edge pieces instead of this ground.
		std::erase_if(tile->items, [this](const std::unique_ptr<Item>& item) {
			return item->isBorder() && getCarpetPieceOwner(item->getID()) == this;
		});
	}
	if (tile->hasGround() && tile->ground->getGroundBrush() == this) {
		tile->ground = nullptr;
	}
}

void GroundBrush::draw(BaseMap* map, Tile* tile, void* parameter) {
	ASSERT(tile);
	if (border_items.empty()) {
		return;
	}

	if (parameter != nullptr) {
		std::pair<bool, GroundBrush*>& param = *reinterpret_cast<std::pair<bool, GroundBrush*>*>(parameter);
		GroundBrush* other = tile->getGroundBrush();
		if (param.first) { // Volatile? :)
			if (other != nullptr) {
				return;
			}
		} else if (other != param.second) {
			return;
		}
	}

	if (carpet_fill && g_settings.getBoolean(Config::CARPET_FILL_BORDERS)) {
		const AutoBorder* border = getFirstOuterAutoBorder();
		if (border) {
			// Carpet Fill: keep the old ground visible underneath. Drop one
			// provisional edge piece as the membership marker; borderize picks
			// the real piece(s) right after, and fills the center ground only
			// once the tile is fully surrounded by this brush.
			if (tile->getGroundBrush() == this) {
				return; // Already the filled center of this brush
			}
			for (const auto& item : tile->items) {
				if (item->isBorder() && border->containsItem(item->getID())) {
					return; // Already a margin piece of this brush
				}
			}
			for (int direction = 1; direction <= 12; ++direction) {
				uint32_t pieceId = border->getTileId(direction);
				if (pieceId != 0) {
					TileOperations::addBorderItem(tile, Item::Create(static_cast<uint16_t>(pieceId)));
					return;
				}
			}
			// Border set has no usable pieces: fall back to normal placement.
		}
	}

	TerrainPlacement::placeBrushItem(*tile, getRandomGroundItemId());
}

uint16_t GroundBrush::getRandomGroundItemId() const {
	if (border_items.empty()) {
		return 0;
	}
	int chance = random(1, total_chance);
	for (const auto& item_block : border_items) {
		if (chance < item_block.chance) {
			return item_block.id;
		}
	}
	return border_items.front().id;
}

bool GroundBrush::isExcludedBrush(const BorderBlock* bb, uint32_t brushId) {
	for (uint32_t excludedId : bb->not_to) {
		if (excludedId == brushId) {
			return true;
		}
	}
	return false;
}

const GroundBrush::BorderBlock* GroundBrush::getBrushTo(GroundBrush* first, GroundBrush* second) {
	if (first) {
		if (second) {
			uint32_t secondId = second->getID();
			uint32_t firstId = first->getID();
			if (first->getZ() < second->getZ() && second->hasOuterBorder()) {
				if (first->hasInnerBorder()) {
					for (const auto& bb : first->borders) {
						if (bb->outer) {
							continue;
						}
						if (isExcludedBrush(bb.get(), secondId)) {
							continue;
						}
						if (bb->to == secondId || bb->to == 0xFFFFFFFF) {
							return bb.get();
						}
					}
				}
				for (const auto& bb : second->borders) {
					if (!bb->outer) {
						continue;
					}
					if (isExcludedBrush(bb.get(), firstId)) {
						continue;
					}
					if (bb->to == firstId) {
						return bb.get();
					} else if (bb->to == 0xFFFFFFFF) {
						return bb.get();
					}
				}
			} else if (first->hasInnerBorder()) {
				for (const auto& bb : first->borders) {
					if (bb->outer) {
						continue;
					}
					if (isExcludedBrush(bb.get(), secondId)) {
						continue;
					}
					if (bb->to == secondId) {
						return bb.get();
					} else if (bb->to == 0xFFFFFFFF) {
						return bb.get();
					}
				}
			}
		} else if (first->hasInnerZilchBorder()) {
			for (const auto& bb : first->borders) {
				if (bb->outer) {
					continue;
				} else if (bb->to == 0) {
					return bb.get();
				}
			}
		}
	} else if (second && second->hasOuterZilchBorder()) {
		for (const auto& bb : second->borders) {
			if (!bb->outer) {
				continue;
			} else if (bb->to == 0) {
				return bb.get();
			}
		}
	}
	return nullptr;
}

std::vector<const GroundBrush::BorderBlock*> GroundBrush::getBrushesTo(GroundBrush* first, GroundBrush* second) {
	std::vector<const BorderBlock*> result;

	if (first) {
		if (second) {
			uint32_t secondId = second->getID();
			uint32_t firstId = first->getID();
			if (first->getZ() < second->getZ() && second->hasOuterBorder()) {
				if (first->hasInnerBorder()) {
					for (const auto& bb : first->borders) {
						if (bb->outer) {
							continue;
						}
						if (isExcludedBrush(bb.get(), secondId)) {
							continue;
						}
						if (bb->to == secondId || bb->to == 0xFFFFFFFF) {
							result.push_back(bb.get());
						}
					}
				}
				for (const auto& bb : second->borders) {
					if (!bb->outer) {
						continue;
					}
					if (isExcludedBrush(bb.get(), firstId)) {
						continue;
					}
					if (bb->to == firstId || bb->to == 0xFFFFFFFF) {
						result.push_back(bb.get());
					}
				}
			} else if (first->hasInnerBorder()) {
				for (const auto& bb : first->borders) {
					if (bb->outer) {
						continue;
					}
					if (isExcludedBrush(bb.get(), secondId)) {
						continue;
					}
					if (bb->to == secondId || bb->to == 0xFFFFFFFF) {
						result.push_back(bb.get());
					}
				}
			}
		} else if (first->hasInnerZilchBorder()) {
			for (const auto& bb : first->borders) {
				if (bb->outer) {
					continue;
				}
				if (bb->to == 0) {
					result.push_back(bb.get());
				}
			}
		}
	} else if (second && second->hasOuterZilchBorder()) {
		for (const auto& bb : second->borders) {
			if (!bb->outer) {
				continue;
			}
			if (bb->to == 0) {
				result.push_back(bb.get());
			}
		}
	}

	return result;
}

inline GroundBrush* extractGroundBrushFromTile(BaseMap* map, uint32_t x, uint32_t y, uint32_t z) {
	Tile* t = map->getTile(x, y, z);
	return t ? t->getGroundBrush() : nullptr;
}

void GroundBrush::doBorders(BaseMap* map, Tile* tile) {
	GroundBorderCalculator::calculate(map, tile);
}
void GroundBrush::getRelatedItems(std::vector<uint16_t>& items) {
	for (const auto& item_block : border_items) {
		if (item_block.id != 0) {
			items.push_back(item_block.id);
		}
	}

	for (const auto& bb : borders) {
		if (bb->autoborder) {
			for (const auto& direction_items : bb->autoborder->tiles) {
				for (const auto& bic : direction_items) {
					if (bic.id != 0) {
						items.push_back(bic.id);
					}
				}
			}
		}
		for (const auto& sc : bb->specific_cases) {
			if (sc->to_replace_id != 0) {
				items.push_back(sc->to_replace_id);
			}
			if (sc->with_id != 0) {
				items.push_back(sc->with_id);
			}
		}
	}
}
