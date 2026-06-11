//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"

#include "editor/operations/rotation_utility.h"
#include "game/item.h"
#include "map/tile.h"
#include "brushes/brush.h"
#include "brushes/ground/auto_border.h"
#include "brushes/wall/wall_brush.h"
#include "item_definitions/core/item_definition_store.h"

#include <algorithm>

namespace {

// Border alignment rotation (CW once)
BorderType rotate_border_type_cw_once(BorderType type) {
	switch (type) {
		case NORTH_HORIZONTAL: return EAST_HORIZONTAL;
		case EAST_HORIZONTAL: return SOUTH_HORIZONTAL;
		case SOUTH_HORIZONTAL: return WEST_HORIZONTAL;
		case WEST_HORIZONTAL: return NORTH_HORIZONTAL;

		case NORTHWEST_CORNER: return NORTHEAST_CORNER;
		case NORTHEAST_CORNER: return SOUTHEAST_CORNER;
		case SOUTHEAST_CORNER: return SOUTHWEST_CORNER;
		case SOUTHWEST_CORNER: return NORTHWEST_CORNER;

		case NORTHWEST_DIAGONAL: return NORTHEAST_DIAGONAL;
		case NORTHEAST_DIAGONAL: return SOUTHEAST_DIAGONAL;
		case SOUTHEAST_DIAGONAL: return SOUTHWEST_DIAGONAL;
		case SOUTHWEST_DIAGONAL: return NORTHWEST_DIAGONAL;

		default:
			return type;
	}
}

// Wall alignment rotation (CW once)
BorderType rotate_wall_alignment_cw_once(BorderType type) {
	switch (type) {
		case WALL_POLE: return WALL_POLE;

		case WALL_VERTICAL: return WALL_HORIZONTAL;
		case WALL_HORIZONTAL: return WALL_VERTICAL;

		case WALL_NORTH_END: return WALL_EAST_END;
		case WALL_EAST_END: return WALL_SOUTH_END;
		case WALL_SOUTH_END: return WALL_WEST_END;
		case WALL_WEST_END: return WALL_NORTH_END;

		case WALL_NORTH_T: return WALL_EAST_T;
		case WALL_EAST_T: return WALL_SOUTH_T;
		case WALL_SOUTH_T: return WALL_WEST_T;
		case WALL_WEST_T: return WALL_NORTH_T;

		case WALL_NORTHWEST_DIAGONAL: return WALL_NORTHEAST_DIAGONAL;
		case WALL_NORTHEAST_DIAGONAL: return WALL_SOUTHEAST_DIAGONAL;
		case WALL_SOUTHEAST_DIAGONAL: return WALL_SOUTHWEST_DIAGONAL;
		case WALL_SOUTHWEST_DIAGONAL: return WALL_NORTHWEST_DIAGONAL;

		case WALL_INTERSECTION: return WALL_INTERSECTION;
		case WALL_UNTOUCHABLE: return WALL_UNTOUCHABLE;

		default:
			return type;
	}
}

uint32_t build_door_key(BorderType alignment, ::DoorType doorType, bool open) {
	return (static_cast<uint32_t>(static_cast<uint8_t>(alignment)) & 0xFFu) |
		((static_cast<uint32_t>(static_cast<uint8_t>(doorType)) & 0xFFu) << 8) |
		((open ? 1u : 0u) << 16);
}

} // namespace

RotationUtility::RotationUtility(int quarterTurns) {
	int turns = quarterTurns % 4;
	if (turns < 0) {
		turns += 4;
	}
	turns_ = turns;

	border_for_item_id.reserve(128);
}

BorderType RotationUtility::rotateBorderType(BorderType type) const {
	BorderType out = type;
	for (int i = 0; i < turns_; ++i) {
		out = rotate_border_type_cw_once(out);
	}
	return out;
}

BorderType RotationUtility::rotateWallAlignment(BorderType type) const {
	BorderType out = type;
	for (int i = 0; i < turns_; ++i) {
		out = rotate_wall_alignment_cw_once(out);
	}
	return out;
}

const AutoBorder* RotationUtility::getBorderForItem(uint16_t itemId, BorderType currentAlignment, BorderType rotatedAlignment) {
	auto it = border_for_item_id.find(itemId);
	if (it != border_for_item_id.end()) {
		return it->second;
	}

	// An item id can belong to several border groups (some of them partial).
	// Prefer the first group that can express the rotated edge, so a partial
	// group never shadows a complete one and the choice stays deterministic.
	const AutoBorder* chosen = nullptr;
	const auto candidates = g_brushes.findAutoBordersByBorderItem(itemId, currentAlignment);
	for (const AutoBorder* candidate : candidates) {
		if (candidate->getTileId(rotatedAlignment) != 0) {
			chosen = candidate;
			break;
		}
	}
	if (!chosen && !candidates.empty()) {
		chosen = candidates.front();
	}

	border_for_item_id.emplace(itemId, chosen);
	return chosen;
}

void RotationUtility::ensureWallCatalogs() {
	if (wall_catalog_built) {
		return;
	}

	for (ServerItemId id : g_item_definitions.allIds()) {
		if (!g_item_definitions.exists(id)) {
			continue;
		}
		const auto def = g_item_definitions.get(id);
		if (!def.hasFlag(ItemFlag::IsWall)) {
			continue;
		}

		const auto& editorData = def.editorData();
		if (!editorData.brush || !editorData.brush->is<WallBrush>()) {
			continue;
		}

		WallBrush* brush = editorData.brush->as<WallBrush>();
		if (!brush) {
			continue;
		}

		const int alignmentIndex = static_cast<int>(def.attribute(ItemAttributeKey::BorderAlignment));
		if (alignmentIndex < 0 || alignmentIndex >= 17) {
			continue;
		}

		WallBrushCatalog& catalog = wall_catalog_by_brush[brush];
		if (def.hasFlag(ItemFlag::IsBrushDoor)) {
			const ::DoorType doorType = brush->getDoorTypeFromID(id);
			const BorderType borderAlign = static_cast<BorderType>(alignmentIndex);
			const bool isOpen = def.hasFlag(ItemFlag::IsOpen);
			const uint32_t key = build_door_key(borderAlign, doorType, isOpen);
			catalog.doorsByKey[key].push_back(id);
		} else {
			catalog.byAlignment[alignmentIndex].push_back(id);
		}
	}

	for (auto& [brush, catalog] : wall_catalog_by_brush) {
		for (int i = 0; i < 17; ++i) {
			std::sort(catalog.byAlignment[i].begin(), catalog.byAlignment[i].end());
		}
		for (auto& [key, ids] : catalog.doorsByKey) {
			std::sort(ids.begin(), ids.end());
		}
	}

	wall_catalog_built = true;
}

Position RotationUtility::rotatePosition(const Position& pos, const Position& minPos, int width, int height) const {
	const int rx = pos.x - minPos.x;
	const int ry = pos.y - minPos.y;
	switch (turns_) {
		case 1: // 90 CW
			return Position(minPos.x + (height - 1 - ry), minPos.y + rx, pos.z);
		case 2: // 180
			return Position(minPos.x + (width - 1 - rx), minPos.y + (height - 1 - ry), pos.z);
		case 3: // 90 CCW
			return Position(minPos.x + ry, minPos.y + (width - 1 - rx), pos.z);
		default:
			return pos;
	}
}

void RotationUtility::rotateItem(Item* item) {
	if (!item) {
		return;
	}

	// Rotate borders via border group/alignment remap
	if (item->isBorder() || item->isOptionalBorder()) {
		const BorderType current = item->getBorderAlignment();
		const BorderType rotated = rotateBorderType(current);

		if (rotated != BORDER_NONE) {
			const AutoBorder* border = getBorderForItem(item->getID(), current, rotated);
			if (border && border->getTileId(rotated) != 0) {
				const uint16_t newId = static_cast<uint16_t>(border->getTileId(rotated));
				if (newId != item->getID()) {
					item->setID(newId);
				}
				return;
			}
		}
	}

	// Rotate wall items by remapping their wall alignment within the same wall brush
	if (item->isWall()) {
		WallBrush* brush = item->getWallBrush();
		if (brush) {
			ensureWallCatalogs();

			const BorderType current = item->getWallAlignment();
			const BorderType rotated = rotateWallAlignment(current);
			if (rotated != current) {
				auto catalogIt = wall_catalog_by_brush.find(brush);
				if (catalogIt != wall_catalog_by_brush.end()) {
					WallBrushCatalog& catalog = catalogIt->second;
					const int currentIndex = static_cast<int>(current);
					const int rotatedIndex = static_cast<int>(rotated);

					if (item->isBrushDoor()) {
						const ::DoorType doorType = brush->getDoorTypeFromID(item->getID());
						const bool open = item->isOpen();

						const uint32_t oldKey = build_door_key(current, doorType, open);
						const uint32_t newKey = build_door_key(rotated, doorType, open);
						auto oldIt = catalog.doorsByKey.find(oldKey);
						auto newIt = catalog.doorsByKey.find(newKey);

						if (newIt != catalog.doorsByKey.end() && !newIt->second.empty()) {
							size_t idx = 0;
							if (oldIt != catalog.doorsByKey.end()) {
								const auto& oldIds = oldIt->second;
								auto findIt = std::find(oldIds.begin(), oldIds.end(), item->getID());
								if (findIt != oldIds.end()) {
									idx = static_cast<size_t>(findIt - oldIds.begin());
								}
							}
							const auto& newIds = newIt->second;
							const uint16_t newId = newIds[idx % newIds.size()];
							if (newId != 0 && newId != item->getID()) {
								item->setID(newId);
							}
							return;
						}
					} else if (currentIndex >= 0 && currentIndex < 17 && rotatedIndex >= 0 && rotatedIndex < 17) {
						const auto& oldIds = catalog.byAlignment[currentIndex];
						const auto& newIds = catalog.byAlignment[rotatedIndex];
						if (!newIds.empty()) {
							size_t idx = 0;
							auto findIt = std::find(oldIds.begin(), oldIds.end(), item->getID());
							if (findIt != oldIds.end()) {
								idx = static_cast<size_t>(findIt - oldIds.begin());
							}
							const uint16_t newId = newIds[idx % newIds.size()];
							if (newId != 0 && newId != item->getID()) {
								item->setID(newId);
							}
							return;
						}
					}
				}
			}
		}
	}

	// Fallback: use rotateTo chain
	for (int i = 0; i < turns_; ++i) {
		item->doRotate();
	}
}

void RotationUtility::rotateTileItems(Tile* tile) {
	if (!tile) {
		return;
	}
	rotateItem(tile->ground.get());
	for (auto& item : tile->items) {
		rotateItem(item.get());
	}
}
