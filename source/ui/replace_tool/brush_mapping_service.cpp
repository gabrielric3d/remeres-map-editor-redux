//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/replace_tool/brush_mapping_service.h"

#include "brushes/brush.h"
#include "brushes/brush_enums.h"
#include "brushes/ground/ground_brush.h"
#include "brushes/ground/auto_border.h"
#include "brushes/wall/wall_brush.h"
#include "brushes/carpet/carpet_brush.h"
#include "brushes/carpet/carpet_brush_items.h"
#include "game/item.h"

#include <algorithm>
#include <vector>

Brush* BrushMappingService::FindBrush(const std::string& name) {
	if (name.empty()) {
		return nullptr;
	}
	return g_brushes.getBrush(name);
}

bool BrushMappingService::AreCompatible(const Brush* from, const Brush* to) {
	if (!from || !to) {
		return false;
	}
	// WallDecorationBrush derives from WallBrush, so it is covered by the wall check.
	if (from->is<GroundBrush>() && to->is<GroundBrush>()) {
		return true;
	}
	if (from->is<WallBrush>() && to->is<WallBrush>()) {
		return true;
	}
	if (from->is<CarpetBrush>() && to->is<CarpetBrush>()) {
		return true;
	}
	return false;
}

const char* BrushMappingService::GetFamilyName(const Brush* brush) {
	if (!brush) {
		return "";
	}
	if (brush->is<GroundBrush>()) {
		return "Ground";
	}
	if (brush->is<WallBrush>()) {
		return "Wall";
	}
	if (brush->is<CarpetBrush>()) {
		return "Carpet";
	}
	return "";
}

namespace {

	// Maps a ground-brush item (center or border) onto the destination ground brush.
	BrushMappingService::MapResult MapGroundItem(const Item* item, GroundBrush* fromGround, GroundBrush* toGround) {
		BrushMappingService::MapResult result;

		// 1. Ground center: the item is the brush's own ground tile.
		if (item->getGroundBrush() == fromGround) {
			result.matched = true;
			result.newId = toGround->getRandomGroundItemId();
			result.resolved = (result.newId != 0);
			return result;
		}

		// 2. Border item: find which AutoBorder of the source brush owns this id,
		//    and in which direction, then take the same direction from the
		//    matching border of the destination brush.
		const uint16_t id = item->getID();
		if (id == 0) {
			return result;
		}

		const BorderType hint = item->getBorderAlignment();
		const std::vector<const AutoBorder*> candidates = g_brushes.findAutoBordersByBorderItem(id, hint);
		if (candidates.empty()) {
			return result;
		}

		// GroundBrush::borders is protected, so we can only reach the first outer
		// and the first inner AutoBorder through the public accessors.
		const AutoBorder* fromOuter = fromGround->getFirstOuterAutoBorder();
		const AutoBorder* fromInner = fromGround->getFirstInnerAutoBorder();

		for (const AutoBorder* ab : candidates) {
			if (!ab) {
				continue;
			}
			const bool isOuter = (ab == fromOuter);
			const bool isInner = (ab == fromInner);
			if (!isOuter && !isInner) {
				continue; // this border does not belong to the source brush
			}

			int dir = 0;
			for (int d = 1; d <= 12; ++d) {
				if (ab->containsItemInDirection(id, d)) {
					dir = d;
					break;
				}
			}
			if (dir == 0) {
				continue;
			}

			result.matched = true;

			// Same role on the destination: outer maps to outer, inner to inner,
			// with a cascading fallback so a partially defined brush still works.
			const AutoBorder* destBorder = isOuter ? toGround->getFirstOuterAutoBorder() : toGround->getFirstInnerAutoBorder();
			if (!destBorder) {
				destBorder = toGround->getFirstAutoBorder();
			}
			if (!destBorder) {
				return result; // matched but unresolved: caller keeps the original item
			}

			result.newId = (uint16_t)destBorder->getRandomTileId(dir);
			result.resolved = (result.newId != 0);
			return result;
		}

		return result;
	}

	// Maps a wall item onto the destination wall brush, preserving the segment
	// alignment and, for doors/windows, the DoorType (and locked flag when possible).
	BrushMappingService::MapResult MapWallItem(const Item* item, WallBrush* fromWall, WallBrush* toWall) {
		BrushMappingService::MapResult result;
		const uint16_t id = item->getID();
		if (id == 0) {
			return result;
		}

		for (int align = 0; align < WallBrushItems::WALL_ALIGNMENT_COUNT; ++align) {
			if (fromWall->items.hasWall(id, align)) {
				result.matched = true;
				result.newId = toWall->items.getRandomWallId(align);
				result.resolved = (result.newId != 0);
				return result;
			}

			if (fromWall->items.hasDoor(id, align)) {
				result.matched = true;
				const ::DoorType dt = fromWall->items.getDoorTypeFromID(id);

				const auto& destDoors = toWall->items.getDoorItems(align);
				const WallBrushItems::DoorItem* sameTypeAny = nullptr;
				const WallBrushItems::DoorItem* sameTypeSameLock = nullptr;

				// Preserve the locked flag when the destination offers both variants.
				bool srcLocked = false;
				for (const auto& d : fromWall->items.getDoorItems(align)) {
					if (d.id == id) {
						srcLocked = d.locked;
						break;
					}
				}

				for (const auto& d : destDoors) {
					if (d.type != dt) {
						continue;
					}
					if (!sameTypeAny) {
						sameTypeAny = &d;
					}
					if (d.locked == srcLocked) {
						sameTypeSameLock = &d;
						break;
					}
				}

				const WallBrushItems::DoorItem* pick = sameTypeSameLock ? sameTypeSameLock : sameTypeAny;
				if (pick) {
					result.newId = pick->id;
				} else {
					// The destination brush has no door of this type in this
					// alignment: fall back to a solid wall segment rather than
					// leaving an orphan item from the old brush behind.
					result.newId = toWall->items.getRandomWallId(align);
				}
				result.resolved = (result.newId != 0);
				return result;
			}
		}

		return result;
	}

	// Maps a carpet item onto the destination carpet brush by alignment group.
	BrushMappingService::MapResult MapCarpetItem(const Item* item, CarpetBrush* fromCarpet, CarpetBrush* toCarpet) {
		BrushMappingService::MapResult result;
		const uint16_t id = item->getID();
		if (id == 0) {
			return result;
		}

		const auto& fromGroups = fromCarpet->getItems().getGroups();
		int dir = -1;
		for (size_t g = 0; g < fromGroups.size() && dir < 0; ++g) {
			for (const auto& ci : fromGroups[g].items) {
				if (ci.id == id) {
					dir = (int)g;
					break;
				}
			}
		}
		if (dir < 0) {
			return result;
		}

		result.matched = true;

		const auto& toGroups = toCarpet->getItems().getGroups();
		result.newId = CarpetBrushItems::pickFromGroup(toGroups[dir]);
		if (result.newId == 0) {
			// Cascading fallback so a sparsely defined destination still swaps.
			result.newId = CarpetBrushItems::pickFromGroup(toGroups[CARPET_CENTER]);
		}
		if (result.newId == 0) {
			result.newId = CarpetBrushItems::pickFromGroup(toGroups[0]);
		}
		result.resolved = (result.newId != 0);
		return result;
	}

	// ---- Role naming, shared by the swap dialog ----------------------------
	// BorderType packs ground/carpet directions and wall alignments into the same
	// enum with overlapping values, so each family gets its own lookup.

	const char* GroundBorderRoleName(int dir) {
		switch (dir) {
			case NORTH_HORIZONTAL:
				return "North";
			case EAST_HORIZONTAL:
				return "East";
			case SOUTH_HORIZONTAL:
				return "South";
			case WEST_HORIZONTAL:
				return "West";
			case NORTHWEST_CORNER:
				return "Corner NW";
			case NORTHEAST_CORNER:
				return "Corner NE";
			case SOUTHWEST_CORNER:
				return "Corner SW";
			case SOUTHEAST_CORNER:
				return "Corner SE";
			case NORTHWEST_DIAGONAL:
				return "Diagonal NW";
			case NORTHEAST_DIAGONAL:
				return "Diagonal NE";
			case SOUTHEAST_DIAGONAL:
				return "Diagonal SE";
			case SOUTHWEST_DIAGONAL:
				return "Diagonal SW";
			default:
				return "Border";
		}
	}

	const char* WallRoleName(int align) {
		switch (align) {
			case WALL_POLE:
				return "Pole";
			case WALL_SOUTH_END:
				return "South end";
			case WALL_EAST_END:
				return "East end";
			case WALL_NORTHWEST_DIAGONAL:
				return "Diagonal NW";
			case WALL_WEST_END:
				return "West end";
			case WALL_NORTHEAST_DIAGONAL:
				return "Diagonal NE";
			case WALL_HORIZONTAL:
				return "Horizontal";
			case WALL_SOUTH_T:
				return "T south";
			case WALL_NORTH_END:
				return "North end";
			case WALL_VERTICAL:
				return "Vertical";
			case WALL_SOUTHWEST_DIAGONAL:
				return "Diagonal SW";
			case WALL_EAST_T:
				return "T east";
			case WALL_SOUTHEAST_DIAGONAL:
				return "Diagonal SE";
			case WALL_WEST_T:
				return "T west";
			case WALL_NORTH_T:
				return "T north";
			case WALL_INTERSECTION:
				return "Intersection";
			case WALL_UNTOUCHABLE:
				return "Untouchable";
			default:
				return "Segment";
		}
	}

	const char* DoorTypeName(::DoorType type) {
		switch (type) {
			case WALL_ARCHWAY:
				return "Archway";
			case WALL_DOOR_NORMAL:
				return "Door";
			case WALL_DOOR_LOCKED:
				return "Door (locked)";
			case WALL_DOOR_QUEST:
				return "Door (quest)";
			case WALL_DOOR_MAGIC:
				return "Door (magic)";
			case WALL_DOOR_NORMAL_ALT:
				return "Door (alt)";
			case WALL_WINDOW:
				return "Window";
			case WALL_HATCH_WINDOW:
				return "Hatch window";
			default:
				return "Undefined";
		}
	}

	std::vector<uint16_t> BorderIds(const std::vector<BorderItemChance>& tiles) {
		std::vector<uint16_t> ids;
		ids.reserve(tiles.size());
		for (const auto& entry : tiles) {
			if (entry.id != 0) {
				ids.push_back(entry.id);
			}
		}
		return ids;
	}

} // namespace

std::vector<BrushMappingService::RolePair> BrushMappingService::BuildRolePairs(const Brush* from, const Brush* to) {
	std::vector<RolePair> pairs;
	if (!from) {
		return pairs;
	}
	// A destination is optional, but a mismatched one yields nothing: pairing a
	// wall against a ground would produce rows that mean nothing.
	if (to && !AreCompatible(from, to)) {
		return pairs;
	}

	// Pairs two id lists positionally. When the destination declares fewer
	// variations its last one is reused, so every source item gets a target.
	auto emit = [&pairs](const std::string& role, const std::vector<uint16_t>& src, const std::vector<uint16_t>& dst) {
		for (size_t i = 0; i < src.size(); ++i) {
			RolePair pair;
			pair.role = role;
			pair.fromId = src[i];
			if (!dst.empty()) {
				pair.toId = dst[std::min(i, dst.size() - 1)];
			}
			pairs.push_back(std::move(pair));
		}
	};

	if (const auto* fromGround = from->as<GroundBrush>()) {
		const auto* toGround = to ? to->as<GroundBrush>() : nullptr;

		emit("Ground", fromGround->getGroundItemIds(), toGround ? toGround->getGroundItemIds() : std::vector<uint16_t>());

		// Outer maps to outer and inner to inner, exactly as MapItem does, so the
		// preview cannot disagree with what execution will actually do.
		const AutoBorder* fromOuter = fromGround->getFirstOuterAutoBorder();
		const AutoBorder* fromInner = fromGround->getFirstInnerAutoBorder();
		const AutoBorder* toOuter = toGround ? toGround->getFirstOuterAutoBorder() : nullptr;
		const AutoBorder* toInner = toGround ? toGround->getFirstInnerAutoBorder() : nullptr;
		if (toGround && !toOuter) {
			toOuter = toGround->getFirstAutoBorder();
		}
		if (toGround && !toInner) {
			toInner = toGround->getFirstAutoBorder();
		}

		const struct {
			const char* prefix;
			const AutoBorder* src;
			const AutoBorder* dst;
		} borderSets[] = { { "", fromOuter, toOuter }, { "Inner ", fromInner, toInner } };

		for (const auto& set : borderSets) {
			if (!set.src) {
				continue;
			}
			for (int dir = 1; dir <= 12; ++dir) {
				const std::vector<uint16_t> src = BorderIds(set.src->tiles[dir]);
				if (src.empty()) {
					continue;
				}
				emit(std::string(set.prefix) + GroundBorderRoleName(dir), src, set.dst ? BorderIds(set.dst->tiles[dir]) : std::vector<uint16_t>());
			}
		}
		return pairs;
	}

	if (const auto* fromWall = from->as<WallBrush>()) {
		const auto* toWall = to ? to->as<WallBrush>() : nullptr;

		for (int align = 0; align < WallBrushItems::WALL_ALIGNMENT_COUNT; ++align) {
			std::vector<uint16_t> src;
			for (const auto& wall : fromWall->items.getWallNode(align).items) {
				if (wall.id != 0) {
					src.push_back(wall.id);
				}
			}
			std::vector<uint16_t> dst;
			if (toWall) {
				for (const auto& wall : toWall->items.getWallNode(align).items) {
					if (wall.id != 0) {
						dst.push_back(wall.id);
					}
				}
			}
			emit(WallRoleName(align), src, dst);

			// Doors and windows pair by DoorType rather than by position, so a
			// window never turns into a door just because it came later in the list.
			for (const auto& door : fromWall->items.getDoorItems(align)) {
				if (door.id == 0) {
					continue;
				}
				RolePair pair;
				pair.role = std::string(WallRoleName(align)) + " / " + DoorTypeName(door.type);
				pair.fromId = door.id;

				if (toWall) {
					const WallBrushItems::DoorItem* sameTypeAny = nullptr;
					const WallBrushItems::DoorItem* sameTypeSameLock = nullptr;
					for (const auto& candidate : toWall->items.getDoorItems(align)) {
						if (candidate.type != door.type) {
							continue;
						}
						if (!sameTypeAny) {
							sameTypeAny = &candidate;
						}
						if (candidate.locked == door.locked) {
							sameTypeSameLock = &candidate;
							break;
						}
					}
					const WallBrushItems::DoorItem* pick = sameTypeSameLock ? sameTypeSameLock : sameTypeAny;
					// No door of this type over there: fall back to a solid segment
					// rather than leaving an orphan from the old brush.
					pair.toId = pick ? pick->id : (dst.empty() ? 0 : dst.front());
				}
				pairs.push_back(std::move(pair));
			}
		}
		return pairs;
	}

	if (const auto* fromCarpet = from->as<CarpetBrush>()) {
		const auto* toCarpet = to ? to->as<CarpetBrush>() : nullptr;
		const auto& fromGroups = fromCarpet->getItems().getGroups();

		// Center first, then the twelve directions, then the catch-all group 0.
		std::vector<int> order;
		order.push_back(CARPET_CENTER);
		for (int dir = 1; dir <= 12; ++dir) {
			order.push_back(dir);
		}
		order.push_back(BORDER_NONE);

		for (int group : order) {
			if (group < 0 || group >= (int)fromGroups.size()) {
				continue;
			}
			std::vector<uint16_t> src;
			for (const auto& carpet : fromGroups[group].items) {
				if (carpet.id != 0) {
					src.push_back(carpet.id);
				}
			}
			if (src.empty()) {
				continue;
			}
			std::vector<uint16_t> dst;
			if (toCarpet) {
				for (const auto& carpet : toCarpet->getItems().getGroups()[group].items) {
					if (carpet.id != 0) {
						dst.push_back(carpet.id);
					}
				}
			}
			const char* name = (group == CARPET_CENTER) ? "Center" : (group == BORDER_NONE ? "Default" : GroundBorderRoleName(group));
			emit(name, src, dst);
		}
		return pairs;
	}

	return pairs;
}

const AutoBorder* BrushMappingService::FindBorder(uint32_t borderId) {
	if (borderId == 0) {
		return nullptr;
	}
	const auto& borders = g_brushes.getBorders();
	const auto it = borders.find(borderId);
	return (it != borders.end()) ? it->second.get() : nullptr;
}

bool BrushMappingService::AreCompatible(const Selection& from, const Selection& to) {
	if (from.empty() || to.empty()) {
		return false;
	}
	// A border only pairs with a border: it has no ground center, and a ground
	// brush has no meaningful "border id" to line up against.
	if (from.isBorder() || to.isBorder()) {
		return from.isBorder() && to.isBorder();
	}
	return AreCompatible(FindBrush(from.brushName), FindBrush(to.brushName));
}

uint16_t BrushMappingService::GetPreviewItemId(const Selection& selection) {
	if (selection.isBorder()) {
		const AutoBorder* border = FindBorder(selection.borderId);
		if (!border) {
			return 0;
		}
		// North reads best as a thumbnail; fall back to whatever is defined.
		for (int dir = 1; dir <= 12; ++dir) {
			const uint16_t id = (uint16_t)border->getTileId(dir);
			if (id != 0) {
				return id;
			}
		}
		return 0;
	}
	return GetPreviewItemId(FindBrush(selection.brushName));
}

const char* BrushMappingService::GetFamilyName(const Selection& selection) {
	if (selection.isBorder()) {
		return "Border";
	}
	return GetFamilyName(FindBrush(selection.brushName));
}

std::vector<BrushMappingService::RolePair> BrushMappingService::BuildRolePairs(const Selection& from, const Selection& to) {
	if (!from.isBorder() && !to.isBorder()) {
		return BuildRolePairs(FindBrush(from.brushName), to.empty() ? nullptr : FindBrush(to.brushName));
	}

	std::vector<RolePair> pairs;
	if (!from.isBorder()) {
		return pairs; // mismatched families produce nothing
	}

	const AutoBorder* fromBorder = FindBorder(from.borderId);
	if (!fromBorder) {
		return pairs;
	}
	// A destination is optional; a non-border one is simply ignored.
	const AutoBorder* toBorder = to.isBorder() ? FindBorder(to.borderId) : nullptr;

	for (int dir = 1; dir <= 12; ++dir) {
		const std::vector<uint16_t> src = BorderIds(fromBorder->tiles[dir]);
		if (src.empty()) {
			continue;
		}
		const std::vector<uint16_t> dst = toBorder ? BorderIds(toBorder->tiles[dir]) : std::vector<uint16_t>();
		for (size_t i = 0; i < src.size(); ++i) {
			RolePair pair;
			pair.role = GroundBorderRoleName(dir);
			pair.fromId = src[i];
			if (!dst.empty()) {
				pair.toId = dst[std::min(i, dst.size() - 1)];
			}
			pairs.push_back(std::move(pair));
		}
	}
	return pairs;
}

std::vector<uint16_t> BrushMappingService::GetItemIds(const Selection& selection) {
	// Reuses the role expansion instead of re-walking every brush family: the
	// source column of a pairing already is "every item this selection owns".
	const std::vector<RolePair> pairs = BuildRolePairs(selection, Selection());

	std::vector<uint16_t> ids;
	ids.reserve(pairs.size());
	for (const auto& pair : pairs) {
		if (pair.fromId != 0) {
			ids.push_back(pair.fromId);
		}
	}
	std::sort(ids.begin(), ids.end());
	ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
	return ids;
}

BrushMappingService::MapResult BrushMappingService::MapItem(const Item* item, Brush* fromBrush, Brush* toBrush) {
	MapResult result;
	if (!item || !fromBrush || !toBrush) {
		return result;
	}

	if (auto* fromGround = fromBrush->as<GroundBrush>()) {
		auto* toGround = toBrush->as<GroundBrush>();
		if (!toGround) {
			return result;
		}
		return MapGroundItem(item, fromGround, toGround);
	}

	if (auto* fromWall = fromBrush->as<WallBrush>()) {
		auto* toWall = toBrush->as<WallBrush>();
		if (!toWall) {
			return result;
		}
		return MapWallItem(item, fromWall, toWall);
	}

	if (auto* fromCarpet = fromBrush->as<CarpetBrush>()) {
		auto* toCarpet = toBrush->as<CarpetBrush>();
		if (!toCarpet) {
			return result;
		}
		return MapCarpetItem(item, fromCarpet, toCarpet);
	}

	return result;
}

uint16_t BrushMappingService::GetPreviewItemId(const Brush* brush) {
	if (!brush) {
		return 0;
	}

	if (const auto* gb = brush->as<GroundBrush>()) {
		const uint16_t id = gb->getFirstGroundItemId();
		if (id != 0) {
			return id;
		}
	} else if (const auto* wb = brush->as<WallBrush>()) {
		uint16_t id = wb->items.getRandomWallId(WALL_HORIZONTAL);
		if (id == 0) {
			id = wb->items.getRandomWallId(WALL_VERTICAL);
		}
		if (id == 0) {
			id = wb->items.getRandomWallId(WALL_POLE);
		}
		if (id != 0) {
			return id;
		}
	} else if (const auto* cb = brush->as<CarpetBrush>()) {
		const auto& groups = cb->getItems().getGroups();
		uint16_t id = CarpetBrushItems::pickFromGroup(groups[CARPET_CENTER]);
		if (id == 0) {
			for (const auto& g : groups) {
				id = CarpetBrushItems::pickFromGroup(g);
				if (id != 0) {
					break;
				}
			}
		}
		if (id != 0) {
			return id;
		}
	}

	const int lookId = brush->getLookID();
	return (lookId > 0 && lookId <= 0xFFFF) ? (uint16_t)lookId : 0;
}
