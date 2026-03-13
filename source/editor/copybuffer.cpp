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

#include "editor/copybuffer.h"
#include "editor/editor.h"
#include "editor/operations/copy_operations.h"
#include "ui/gui.h"
#include "game/creature.h"
#include "map/tile_operations.h"
#include "brushes/brush.h"
#include "brushes/ground/auto_border.h"
#include "brushes/wall/wall_brush.h"
#include "item_definitions/core/item_definition_store.h"

CopyBuffer::CopyBuffer() :
	tiles(std::make_unique<BaseMap>()) {
	;
}

size_t CopyBuffer::GetTileCount() {
	return tiles ? (size_t)tiles->size() : 0;
}

BaseMap& CopyBuffer::getBufferMap() {
	ASSERT(tiles);
	return *tiles;
}

CopyBuffer::~CopyBuffer() {
	clear();
}

Position CopyBuffer::getPosition() const {
	ASSERT(tiles);
	return copyPos;
}

void CopyBuffer::clear() {
	tiles.reset();
}

void CopyBuffer::copy(Editor& editor, int floor) {
	CopyOperations::copy(editor, *this, floor);
}

void CopyBuffer::cut(Editor& editor, int floor) {
	CopyOperations::cut(editor, *this, floor);
}

void CopyBuffer::paste(Editor& editor, const Position& toPosition) {
	CopyOperations::paste(editor, *this, toPosition);
}

bool CopyBuffer::canPaste() const {
	return tiles && tiles->size() != 0;
}

void CopyBuffer::rotate(int quarterTurns) {
	if (!tiles || tiles->size() == 0) {
		return;
	}

	int turns = quarterTurns % 4;
	if (turns < 0) {
		turns += 4;
	}
	if (turns == 0) {
		return;
	}

	// Find bounding box
	bool hasPos = false;
	Position minPos;
	Position maxPos;

	for (MapIterator it = tiles->begin(); it != tiles->end(); ++it) {
		Tile* tile = (*it).get();
		if (!tile) {
			continue;
		}

		const Position pos = tile->getPosition();
		if (!hasPos) {
			minPos = pos;
			maxPos = pos;
			hasPos = true;
		} else {
			minPos.x = std::min(minPos.x, pos.x);
			minPos.y = std::min(minPos.y, pos.y);
			maxPos.x = std::max(maxPos.x, pos.x);
			maxPos.y = std::max(maxPos.y, pos.y);
		}
	}

	if (!hasPos) {
		return;
	}

	const int width = maxPos.x - minPos.x + 1;
	const int height = maxPos.y - minPos.y + 1;

	// Border alignment rotation (CW once)
	const auto rotate_border_type_cw_once = [](BorderType type) -> BorderType {
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
	};

	const auto rotate_border_type = [&](BorderType type, int cwTurns) -> BorderType {
		int t = cwTurns % 4;
		if (t < 0) {
			t += 4;
		}
		BorderType out = type;
		for (int i = 0; i < t; ++i) {
			out = rotate_border_type_cw_once(out);
		}
		return out;
	};

	// Cache: itemId -> AutoBorder*
	std::unordered_map<uint16_t, const AutoBorder*> border_for_item_id;
	border_for_item_id.reserve(128);

	const auto get_border_for_item = [&](uint16_t itemId, BorderType alignmentHint) -> const AutoBorder* {
		auto it = border_for_item_id.find(itemId);
		if (it != border_for_item_id.end()) {
			return it->second;
		}

		const AutoBorder* border = g_brushes.findAutoBorderByBorderItem(itemId, alignmentHint);
		border_for_item_id.emplace(itemId, border);
		return border;
	};

	// Wall alignment rotation (CW once)
	const auto rotate_wall_alignment_cw_once = [](BorderType type) -> BorderType {
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
	};

	const auto rotate_wall_alignment = [&](BorderType type, int cwTurns) -> BorderType {
		int t = cwTurns % 4;
		if (t < 0) {
			t += 4;
		}
		BorderType out = type;
		for (int i = 0; i < t; ++i) {
			out = rotate_wall_alignment_cw_once(out);
		}
		return out;
	};

	// Wall brush catalog for remapping wall IDs across alignments
	struct WallBrushCatalog {
		std::vector<uint16_t> byAlignment[17];
		std::unordered_map<uint32_t, std::vector<uint16_t>> doorsByKey;
	};

	bool wall_catalog_built = false;
	std::unordered_map<const WallBrush*, WallBrushCatalog> wall_catalog_by_brush;

	const auto build_door_key = [](BorderType alignment, ::DoorType doorType, bool open) -> uint32_t {
		return (static_cast<uint32_t>(static_cast<uint8_t>(alignment)) & 0xFFu) |
			((static_cast<uint32_t>(static_cast<uint8_t>(doorType)) & 0xFFu) << 8) |
			((open ? 1u : 0u) << 16);
	};

	const auto ensure_wall_catalogs = [&]() {
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
	};

	auto rotate_position = [&](const Position& pos) -> Position {
		const int rx = pos.x - minPos.x;
		const int ry = pos.y - minPos.y;
		switch (turns) {
			case 1: // 90 CW
				return Position(minPos.x + (height - 1 - ry), minPos.y + rx, pos.z);
			case 2: // 180
				return Position(minPos.x + (width - 1 - rx), minPos.y + (height - 1 - ry), pos.z);
			case 3: // 90 CCW
				return Position(minPos.x + ry, minPos.y + (width - 1 - rx), pos.z);
			default:
				return pos;
		}
	};

	auto rotate_item = [&](Item* item) {
		if (!item) {
			return;
		}

		// Rotate borders via border group/alignment remap
		if (item->isBorder() || item->isOptionalBorder()) {
			const BorderType current = item->getBorderAlignment();
			const BorderType rotated = rotate_border_type(current, turns);

			if (rotated != BORDER_NONE) {
				const AutoBorder* border = get_border_for_item(item->getID(), current);
				if (border && border->tiles[rotated] != 0) {
					const uint16_t newId = static_cast<uint16_t>(border->tiles[rotated]);
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
				ensure_wall_catalogs();

				const BorderType current = item->getWallAlignment();
				const BorderType rotated = rotate_wall_alignment(current, turns);
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
		for (int i = 0; i < turns; ++i) {
			item->doRotate();
		}
	};

	auto rotate_tile_items = [&](Tile* tile) {
		if (!tile) {
			return;
		}
		rotate_item(tile->ground.get());
		for (auto& item : tile->items) {
			rotate_item(item.get());
		}
	};

	// Deep copy all tiles, rotate items, compute new positions
	struct PendingTile {
		std::unique_ptr<Tile> tile;
		Position pos;
	};

	std::vector<PendingTile> rotatedTiles;
	rotatedTiles.reserve(static_cast<size_t>(tiles->size()));

	Position newMinPos = Position(0xFFFF, 0xFFFF, copyPos.z);

	for (MapIterator it = tiles->begin(); it != tiles->end(); ++it) {
		Tile* oldTile = (*it).get();
		if (!oldTile) {
			continue;
		}

		auto rotatedTile = TileOperations::deepCopy(oldTile, *tiles);
		const Position newPos = rotate_position(oldTile->getPosition());

		rotate_tile_items(rotatedTile.get());

		newMinPos.x = std::min(newMinPos.x, newPos.x);
		newMinPos.y = std::min(newMinPos.y, newPos.y);

		rotatedTiles.push_back(PendingTile { std::move(rotatedTile), newPos });
	}

	// Clear old tiles and insert rotated ones
	tiles->clear(true);

	for (PendingTile& entry : rotatedTiles) {
		if (!entry.tile) {
			continue;
		}
		TileLocation* location = tiles->createTileL(entry.pos);
		entry.tile->setLocation(location);
		tiles->setTile(entry.pos, std::move(entry.tile));
	}

	// Ensure walls are consistent with their new neighbors
	for (MapIterator it = tiles->begin(); it != tiles->end(); ++it) {
		Tile* tile = (*it).get();
		if (tile && tile->hasWall()) {
			TileOperations::wallize(tile, tiles.get());
		}
	}

	copyPos.x = newMinPos.x;
	copyPos.y = newMinPos.y;
}
