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
uint32_t GroundBrush::global_variant_mask = 0;

namespace {
	// Carpet Fill: edge-piece item id -> carpet_fill ground brushes whose outer
	// borders use it. Populated by the loader, cleared with Brushes::clear().
	std::unordered_map<uint16_t, std::vector<GroundBrush*>> carpet_piece_owners;
	const std::vector<GroundBrush*> no_carpet_piece_owners;
}

const std::vector<GroundBrush*>& GroundBrush::getCarpetPieceOwners(uint16_t itemId) {
	auto it = carpet_piece_owners.find(itemId);
	return it != carpet_piece_owners.end() ? it->second : no_carpet_piece_owners;
}

bool GroundBrush::registerCarpetPieceOwner(uint16_t itemId, GroundBrush* brush) {
	std::vector<GroundBrush*>& owners = carpet_piece_owners[itemId];
	if (std::ranges::find(owners, brush) != owners.end()) {
		return true; // Same brush declaring the border again (e.g. to="all" and to="none").
	}
	owners.push_back(brush);
	return owners.size() == 1;
}

void GroundBrush::clearCarpetPieceOwners() {
	carpet_piece_owners.clear();
}

bool GroundBrush::ownsBorderItem(uint16_t item_id) const {
	if (item_id == 0) {
		return false;
	}
	for (const auto& block : borders) {
		if (block && block->autoborder && block->autoborder->containsItem(item_id)) {
			return true;
		}
	}
	return optional_border && optional_border->containsItem(item_id);
}

bool GroundBrush::paintsAsCarpet() const {
	return carpet_fill && g_settings.getBoolean(Config::CARPET_FILL_BORDERS);
}

bool GroundBrush::ownsCarpetPiece(uint16_t itemId) const {
	for (const auto& bb : borders) {
		if (bb && bb->outer && bb->autoborder && bb->autoborder->containsItem(itemId)) {
			return true;
		}
	}
	return false;
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
	if (paintsAsCarpet()) {
		// Carpet Fill margin tiles carry edge pieces instead of this ground.
		std::erase_if(tile->items, [this](const std::unique_ptr<Item>& item) {
			return item->isBorder() && ownsCarpetPiece(item->getID());
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

	// Carpet Fill needs the borderize pass that follows every stroke, so without
	// automagic the brush simply paints its ground like any other.
	if (paintsAsCarpet() && g_settings.getInteger(Config::USE_AUTOMAGIC)) {
		if (const AutoBorder* border = getActiveOuterAutoBorder()) {
			// The tile joins the carpet region: drop one provisional edge piece
			// as the membership marker over whatever ground is already there.
			// GroundBorderCalculator then decides whether the tile is a filled
			// tile (own ground) or which edge piece it shows.
			if (tile->getGroundBrush() == this) {
				return; // Already a filled tile of this brush
			}
			for (const auto& item : tile->items) {
				if (item->isBorder() && ownsCarpetPiece(item->getID())) {
					return; // Already a member of this brush's region
				}
			}
			for (int direction = 1; direction <= 12; ++direction) {
				const uint32_t pieceId = border->getTileId(direction);
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

int GroundBrush::getActiveBorderVariant() {
	const int variant = g_settings.getInteger(Config::ACTIVE_BORDER_VARIANT);
	// Clamp instead of trusting the config file: variant numbers index a 32-bit mask.
	return (variant < 1 || variant > 32) ? 1 : variant;
}

void GroundBrush::setActiveBorderVariant(int variant) {
	if (variant < 1 || variant > 32) {
		variant = 1;
	}
	g_settings.setInteger(Config::ACTIVE_BORDER_VARIANT, variant);
}

int GroundBrush::cycleActiveBorderVariant(const GroundBrush* context) {
	// Cycle through the variants the brush in hand declares; with no ground brush
	// selected fall back to every variant seen in the loaded materials, so the
	// hotkey still does something sensible outside of a ground brush.
	uint32_t mask = context ? context->variant_mask : 0;
	if (mask == 0) {
		mask = global_variant_mask;
	}
	if (mask == 0) {
		return getActiveBorderVariant();
	}

	const int current = getActiveBorderVariant();
	for (int step = 1; step <= 32; ++step) {
		const int candidate = ((current - 1 + step) % 32) + 1;
		if (mask & (1u << (candidate - 1))) {
			setActiveBorderVariant(candidate);
			return candidate;
		}
	}
	return current;
}

int GroundBrush::getEffectiveVariant() const {
	if (variant_mask == 0) {
		return 0;
	}
	const int active = getActiveBorderVariant();
	if (variant_mask & (1u << (active - 1))) {
		return active;
	}
	// Active variant not declared here: use the lowest one this brush does have.
	for (int variant = 1; variant <= 32; ++variant) {
		if (variant_mask & (1u << (variant - 1))) {
			return variant;
		}
	}
	return 0;
}

const AutoBorder* GroundBrush::getActiveOuterAutoBorder() const {
	for (const auto& b : borders) {
		if (b && b->autoborder && b->outer && isVariantActive(this, b.get())) {
			return b->autoborder;
		}
	}
	return getFirstOuterAutoBorder();
}

bool GroundBrush::isVariantActive(const GroundBrush* owner, const BorderBlock* bb) {
	if (!bb || bb->variant == 0) {
		return true; // Untagged borders are shared by every variant.
	}
	if (!owner) {
		return bb->variant == getActiveBorderVariant();
	}
	return bb->variant == owner->getEffectiveVariant();
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
						if (!isVariantActive(first, bb.get())) {
							continue;
						}
						if (bb->to == secondId || bb->to == 0xFFFFFFFF) {
							return bb.get();
						}
					}
				}
				// Carpet Fill brushes keep their edges on their own tiles: never
				// spill their outer border onto a neighbour.
				for (const auto& bb : second->borders) {
					if (!bb->outer || second->paintsAsCarpet()) {
						continue;
					}
					if (isExcludedBrush(bb.get(), firstId)) {
						continue;
					}
					if (!isVariantActive(second, bb.get())) {
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
					if (!isVariantActive(first, bb.get())) {
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
				}
				if (!isVariantActive(first, bb.get())) {
					continue;
				}
				if (bb->to == 0) {
					return bb.get();
				}
			}
		}
	} else if (second && second->hasOuterZilchBorder() && !second->paintsAsCarpet()) {
		for (const auto& bb : second->borders) {
			if (!bb->outer) {
				continue;
			}
			if (!isVariantActive(second, bb.get())) {
				continue;
			}
			if (bb->to == 0) {
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
						if (!isVariantActive(first, bb.get())) {
							continue;
						}
						if (bb->to == secondId || bb->to == 0xFFFFFFFF) {
							result.push_back(bb.get());
						}
					}
				}
				// Carpet Fill brushes keep their edges on their own tiles: never
				// spill their outer border onto a neighbour.
				for (const auto& bb : second->borders) {
					if (!bb->outer || second->paintsAsCarpet()) {
						continue;
					}
					if (isExcludedBrush(bb.get(), firstId)) {
						continue;
					}
					if (!isVariantActive(second, bb.get())) {
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
					if (!isVariantActive(first, bb.get())) {
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
				if (!isVariantActive(first, bb.get())) {
					continue;
				}
				if (bb->to == 0) {
					result.push_back(bb.get());
				}
			}
		}
	} else if (second && second->hasOuterZilchBorder() && !second->paintsAsCarpet()) {
		for (const auto& bb : second->borders) {
			if (!bb->outer) {
				continue;
			}
			if (!isVariantActive(second, bb.get())) {
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
