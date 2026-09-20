//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/ui/tooltip_collector.h"

#include "app/definitions.h"
#include "editor/editor.h"
#include "game/complexitem.h"
#include "game/item.h"
#include "map/basemap.h"
#include "game/waypoints.h"
#include "map/map.h"
#include "map/map_region.h"
#include "map/tile.h"
#include "rendering/core/drawing_options.h"
#include "rendering/core/render_view.h"
#include "rendering/ui/tooltip_drawer.h"

namespace {
	// Populates tooltip data from an item (in-place).
	bool FillItemTooltipData(TooltipData& data, Item* item, const ItemDefinitionView& it, const Position& pos, bool isHouseTile, float zoom) {
		if (!item) {
			return false;
		}

		const uint16_t id = item->getID();
		if (id < 100) {
			return false;
		}

		uint16_t unique = 0;
		uint16_t action = 0;
		std::string_view text;
		std::string_view description;
		uint8_t doorId = 0;
		Position destination;
		bool hasContent = false;

		bool is_complex = item->isComplex();
		// Early exit for simple items
		// isTooltipable is cached (isContainer || isDoor || isTeleport)
		if (!is_complex && !it.isTooltipable()) {
			return false;
		}

		bool is_container = it.isContainer();
		bool is_door = isHouseTile && item->isDoor();
		bool is_teleport = item->isTeleport();

		if (is_complex) {
			unique = item->getUniqueID();
			action = item->getActionID();
			text = item->getText();
			description = item->getDescription();
		}

		// Check if it's a door
		if (is_door) {
			if (const Door* door = item->asDoor()) {
				if (door->isRealDoor()) {
					doorId = door->getDoorID();
				}
			}
		}

		// Check if it's a teleport
		if (is_teleport) {
			Teleport* tp = static_cast<Teleport*>(item);
			if (tp->hasDestination()) {
				destination = tp->getDestination();
			}
		}

		// Check if container has content
		if (is_container) {
			if (const Container* container = item->asContainer()) {
				hasContent = container->getItemCount() > 0;
			}
		}

		// Only create tooltip if there's something to show
		if (unique == 0 && action == 0 && doorId == 0 && text.empty() && description.empty() && destination.x == 0 && !hasContent) {
			return false;
		}

		// Get item name from database
		std::string_view itemName = it.name();
		if (itemName.empty()) {
			itemName = "Item";
		}

		data.pos = pos;
		data.itemId = id;
		data.itemName = itemName; // Assign string_view to string_view (no copy)

		data.actionId = action;
		data.uniqueId = unique;
		data.doorId = doorId;
		data.text = text;
		data.description = description;
		data.destination = destination;

		// Populate container items
		if (it.isContainer() && zoom <= 1.5f) {
			if (const Container* container = item->asContainer()) {
				// Set capacity for rendering empty slots
				data.containerCapacity = static_cast<uint8_t>(container->getVolume());

				const auto& items = container->getVector();
				data.containerItems.clear();
				// Reserve only what we need (capped at 32)
				data.containerItems.reserve(std::min(items.size(), size_t(32)));
				for (const auto& subItem : items) {
					if (subItem) {
						ContainerItem ci;
						ci.id = subItem->getID();
						ci.subtype = subItem->getSubtype();
						ci.count = subItem->getCount();
						// Sanity check for count
						if (ci.count == 0) {
							ci.count = 1;
						}

						data.containerItems.push_back(ci);

						// Limit preview items to avoid massive tooltips
						if (data.containerItems.size() >= 32) {
							break;
						}
					}
				}
			}
		}

		data.updateCategory();

		return true;
	}
}

void TooltipCollector::Collect(
	const RenderView& view,
	const DrawingOptions& options,
	TooltipDrawer& out_tooltip_drawer,
	Editor& editor
) {
	if (!options.show_tooltips) {
		return;
	}

	// Tooltips only ever showed on the camera floor, so this walks that one
	// floor instead of testing map_z == view.floor once per tile of every
	// floor, the way the tile renderer had to.
	const int map_z = view.floor;

	const int nd_start_x = view.start_x & ~3;
	const int nd_start_y = view.start_y & ~3;
	const int nd_end_x = (view.end_x & ~3) + 4;
	const int nd_end_y = (view.end_y & ~3) + 4;

	editor.map.visitLeaves(nd_start_x, nd_start_y, nd_end_x, nd_end_y, [&](MapNode* nd, int nd_map_x, int nd_map_y) {
		Floor* floor = nd->getFloor(map_z);
		if (!floor) {
			return;
		}

		for (int local = 0; local < 16; ++local) {
			TileLocation& location = floor->locs[local];
			Tile* tile = location.get();
			if (!tile) {
				continue;
			}
			if (options.show_only_modified && !tile->isModified()) {
				continue;
			}

			const Position& position = location.getPosition();
			const bool is_house_tile = tile->isHouseTile();

			// Waypoint tooltip (one per waypoint)
			if (location.getWaypointCount() > 0) {
				if (Waypoint* waypoint = editor.map.waypoints.getWaypoint(&location)) {
					out_tooltip_drawer.addWaypointTooltip(position, waypoint->name);
				}
			}

			// Ground tooltip (one per item)
			if (tile->ground) {
				const ItemDefinitionView ground_it = tile->ground->getDefinition();
				if (ground_it) {
					TooltipData& ground_data = out_tooltip_drawer.requestTooltipData();
					if (FillItemTooltipData(ground_data, tile->ground.get(), ground_it, position, is_house_tile, view.zoom)) {
						if (ground_data.hasVisibleFields()) {
							out_tooltip_drawer.commitTooltip();
						}
					}
				}
			}

			// Item tooltips (one per item)
			for (const auto& item : tile->items) {
				const ItemDefinitionView it = item->getDefinition();
				if (item->isInvalidOTBMItem() && (!options.show_invalid_tiles || !it)) {
					continue;
				}
				TooltipData& item_data = out_tooltip_drawer.requestTooltipData();
				if (FillItemTooltipData(item_data, item.get(), it, position, is_house_tile, view.zoom)) {
					if (item_data.hasVisibleFields()) {
						out_tooltip_drawer.commitTooltip();
					}
				}
			}
		}
	});
}
