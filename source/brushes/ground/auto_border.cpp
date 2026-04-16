//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "brushes/ground/auto_border.h"
#include "brushes/brush_enums.h"
#include "brushes/ground/ground_brush.h"
#include "item_definitions/core/item_definition_store.h"
#include "ext/pugixml.hpp"
#include <wx/string.h>
#include <utility>

AutoBorder::AutoBorder(int _id) :
	id(_id), group(0), ground(false) {
}

int AutoBorder::edgeNameToID(std::string_view edgename) {
	static constexpr std::pair<std::string_view, int> edges[] = {
		{ "n", NORTH_HORIZONTAL },
		{ "w", WEST_HORIZONTAL },
		{ "s", SOUTH_HORIZONTAL },
		{ "e", EAST_HORIZONTAL },
		{ "cnw", NORTHWEST_CORNER },
		{ "cne", NORTHEAST_CORNER },
		{ "csw", SOUTHWEST_CORNER },
		{ "cse", SOUTHEAST_CORNER },
		{ "dnw", NORTHWEST_DIAGONAL },
		{ "dne", NORTHEAST_DIAGONAL },
		{ "dsw", SOUTHWEST_DIAGONAL },
		{ "dse", SOUTHEAST_DIAGONAL }
	};

	for (const auto& [name, id] : edges) {
		if (name == edgename) {
			return id;
		}
	}
	return BORDER_NONE;
}

uint32_t AutoBorder::getTileId(int direction) const {
	if (direction < 0 || direction >= 13 || tiles[direction].empty()) {
		return 0;
	}
	return tiles[direction][0].id;
}

uint32_t AutoBorder::getRandomTileId(int direction) const {
	if (direction < 0 || direction >= 13 || tiles[direction].empty()) {
		return 0;
	}

	const auto& items = tiles[direction];
	if (items.size() == 1) {
		return items[0].id;
	}

	int totalChance = 0;
	for (const auto& item : items) {
		totalChance += item.chance;
	}

	if (totalChance <= 0) {
		return items[0].id;
	}

	int roll = std::rand() % totalChance;
	int cumulative = 0;
	for (const auto& item : items) {
		cumulative += item.chance;
		if (roll < cumulative) {
			return item.id;
		}
	}

	return items.back().id;
}

bool AutoBorder::containsItem(uint16_t itemId) const {
	for (int i = 1; i <= 12; ++i) {
		if (containsItemInDirection(itemId, i)) {
			return true;
		}
	}
	return false;
}

bool AutoBorder::containsItemInDirection(uint16_t itemId, int direction) const {
	if (direction < 0 || direction >= 13) {
		return false;
	}
	for (const auto& item : tiles[direction]) {
		if (item.id == itemId) {
			return true;
		}
	}
	return false;
}

bool AutoBorder::load(pugi::xml_node node, std::vector<std::string>& warnings, GroundBrush* owner, uint16_t border_base_ground_id) {
	ASSERT(ground ? border_base_ground_id != 0 : true);

	pugi::xml_attribute attribute;

	bool optionalBorder = false;
	if ((attribute = node.attribute("type"))) {
		if (std::string_view(attribute.as_string()) == "optional") {
			optionalBorder = true;
		}
	}

	if ((attribute = node.attribute("group"))) {
		group = attribute.as_ushort();
	}

	auto setupItemFlags = [&](uint16_t itemid) {
		if (!g_item_definitions.exists(itemid)) {
			warnings.push_back("Invalid item ID " + std::to_string(itemid) + " for border " + std::to_string(id));
			return false;
		}

		if (ground) {
			g_item_definitions.setGroup(itemid, ITEM_GROUP_NONE);
			g_item_definitions.setAttribute(itemid, ItemAttributeKey::BorderBaseGroundId, border_base_ground_id);
			g_item_definitions.mutableEditorData(itemid).brush = owner;
		}

		g_item_definitions.setFlag(itemid, ItemFlag::AlwaysOnBottom, true);
		g_item_definitions.setFlag(itemid, ItemFlag::IsBorder, true);
		if (optionalBorder) {
			g_item_definitions.setFlag(itemid, ItemFlag::IsOptionalBorder, true);
		}
		if (group && g_item_definitions.get(itemid).attribute(ItemAttributeKey::BorderGroup) == 0) {
			g_item_definitions.setAttribute(itemid, ItemAttributeKey::BorderGroup, group);
		}
		return true;
	};

	for (pugi::xml_node childNode = node.first_child(); childNode; childNode = childNode.next_sibling()) {
		if (!(attribute = childNode.attribute("edge"))) {
			continue;
		}

		const std::string_view orientation = attribute.as_string();
		int32_t edge_id = edgeNameToID(orientation);
		if (edge_id == BORDER_NONE) {
			continue;
		}

		// New format: <borderitem edge="n"> <item id="123" chance="80" /> <item id="456" chance="20" /> </borderitem>
		pugi::xml_node firstItemChild = childNode.child("item");
		if (firstItemChild) {
			for (pugi::xml_node itemNode = firstItemChild; itemNode; itemNode = itemNode.next_sibling("item")) {
				pugi::xml_attribute idAttr = itemNode.attribute("id");
				if (!idAttr) {
					continue;
				}

				uint16_t itemid = idAttr.as_ushort();
				if (!setupItemFlags(itemid)) {
					continue;
				}

				BorderItemChance bic;
				bic.id = itemid;
				bic.chance = itemNode.attribute("chance") ? itemNode.attribute("chance").as_int() : 100;

				tiles[edge_id].push_back(bic);

				if (g_item_definitions.get(itemid).attribute(ItemAttributeKey::BorderAlignment) == BORDER_NONE) {
					g_item_definitions.setAttribute(itemid, ItemAttributeKey::BorderAlignment, edge_id);
				}
			}
		} else {
			// Legacy format: <borderitem edge="n" item="123" />
			if (!(attribute = childNode.attribute("item"))) {
				continue;
			}

			uint16_t itemid = attribute.as_ushort();
			if (!setupItemFlags(itemid)) {
				continue;
			}

			BorderItemChance bic;
			bic.id = itemid;
			bic.chance = 100;

			tiles[edge_id].clear();
			tiles[edge_id].push_back(bic);

			if (g_item_definitions.get(itemid).attribute(ItemAttributeKey::BorderAlignment) == BORDER_NONE) {
				g_item_definitions.setAttribute(itemid, ItemAttributeKey::BorderAlignment, edge_id);
			}
		}
	}
	return true;
}
