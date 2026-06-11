//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ROTATION_UTILITY_H
#define RME_ROTATION_UTILITY_H

#include "map/position.h"
#include "brushes/brush_enums.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

class Item;
class Tile;
class AutoBorder;
class WallBrush;

// Shared helper for rotating map content in 90-degree steps.
// Holds per-operation caches (border lookups, wall brush catalogs), so create
// one instance per rotate operation and reuse it for every tile/item involved.
class RotationUtility {
public:
	// quarterTurns: 1 = 90 CW, 2 = 180, 3 = 90 CCW. Normalized to 0..3 (negatives wrap).
	explicit RotationUtility(int quarterTurns);

	int turns() const {
		return turns_;
	}
	bool isIdentity() const {
		return turns_ == 0;
	}

	// Rotates pos within the bounding box anchored at minPos (width x height), Z unchanged.
	Position rotatePosition(const Position& pos, const Position& minPos, int width, int height) const;

	// Remaps the item ID/orientation (border -> AutoBorder group; wall/door -> WallBrush catalog; fallback doRotate).
	void rotateItem(Item* item);

	// rotateItem on the ground and all items of the tile.
	void rotateTileItems(Tile* tile);

private:
	BorderType rotateBorderType(BorderType type) const;
	BorderType rotateWallAlignment(BorderType type) const;
	const AutoBorder* getBorderForItem(uint16_t itemId, BorderType currentAlignment, BorderType rotatedAlignment);
	void ensureWallCatalogs();

	int turns_;

	// Cache: itemId -> AutoBorder*
	std::unordered_map<uint16_t, const AutoBorder*> border_for_item_id;

	// Wall brush catalog for remapping wall IDs across alignments
	struct WallBrushCatalog {
		std::vector<uint16_t> byAlignment[17];
		std::unordered_map<uint32_t, std::vector<uint16_t>> doorsByKey;
	};

	bool wall_catalog_built = false;
	std::unordered_map<const WallBrush*, WallBrushCatalog> wall_catalog_by_brush;
};

#endif
