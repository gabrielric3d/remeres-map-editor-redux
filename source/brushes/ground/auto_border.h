//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_AUTO_BORDER_H
#define RME_AUTO_BORDER_H

#include "app/main.h"
#include <array>
#include <cstdlib>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

class GroundBrush;
namespace pugi {
	class xml_node;
}

/**
 * @brief Represents a single border item with a chance weight.
 */
struct BorderItemChance {
	uint16_t id = 0;
	int chance = 100;
};

/**
 * @brief Handles auto-bordering logic for brushes.
 *
 * The AutoBorder class defines a set of rules and tiles used to automatically
 * create borders around ground tiles. It maps specific edge configurations
 * (north, south, corners, etc.) to item IDs.
 */
class AutoBorder {
public:
	/**
	 * @brief Constructs a new AutoBorder object with a given ID.
	 *
	 * @param id The unique identifier for this border configuration.
	 */
	explicit AutoBorder(int id);

	/**
	 * @brief Destroys the AutoBorder object.
	 */
	~AutoBorder() = default;

	/**
	 * @brief Converts a string representation of an edge to its integer ID.
	 *
	 * @param edgename The name of the edge (e.g., "n", "cnw").
	 * @return int The corresponding integer ID for the edge, or BORDER_NONE if invalid.
	 */
	static int edgeNameToID(std::string_view edgename);

	/**
	 * @brief Loads the auto-border configuration from an XML node.
	 *
	 * @param node The XML node containing the border configuration.
	 * @param warnings A list to append any warnings encountered during loading.
	 * @param owner The GroundBrush that owns this border (optional).
	 * @param border_base_ground_id The base ground item used by terrain tools for this border (optional).
	 * @return true if loading was successful, false otherwise.
	 */
	bool load(pugi::xml_node node, std::vector<std::string>& warnings, GroundBrush* owner = nullptr, uint16_t border_base_ground_id = 0);

	/**
	 * @brief Returns the primary (first) item ID for a given direction, or 0 if none.
	 */
	uint32_t getTileId(int direction) const;

	/**
	 * @brief Returns a random item ID for a given direction based on chance weights.
	 * Falls back to the first item if only one exists.
	 */
	uint32_t getRandomTileId(int direction) const;

	/**
	 * @brief Checks if a given item ID exists in any direction of this border.
	 */
	bool containsItem(uint16_t itemId) const;

	/**
	 * @brief Checks if a given item ID exists in a specific direction.
	 */
	bool containsItemInDirection(uint16_t itemId, int direction) const;

	/**
	 * @brief Items for each border direction, supporting multiple items with chance weights.
	 * Indices correspond to the direction mapping (e.g., NORTH_HORIZONTAL).
	 */
	std::array<std::vector<BorderItemChance>, 13> tiles;

	/**
	 * @brief The unique ID of this auto-border.
	 */
	uint32_t id;

	/**
	 * @brief Group ID for this border, used for matching specific cases.
	 */
	uint16_t group;

	/**
	 * @brief Flag indicating if this is a ground border.
	 */
	bool ground;
};

#endif // RME_AUTO_BORDER_H
