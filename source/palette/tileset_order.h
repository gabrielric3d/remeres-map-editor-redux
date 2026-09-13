#ifndef RME_PALETTE_TILESET_ORDER_H_
#define RME_PALETTE_TILESET_ORDER_H_

#include <map>
#include <string>
#include <vector>

#include "brushes/brush_enums.h"

class Brush;

/**
 * @class TilesetOrderStore
 * @brief Persists a user defined brush order for tileset categories.
 *
 * The order is kept in "tileset_order.xml" inside the data directory of the
 * loaded client version. tilesets.xml is never touched, so deleting the file
 * restores the shipped order.
 */
class TilesetOrderStore {
public:
	// Stable identifier for a brush: "raw:<serverid>" or "brush:<name>".
	static std::string MakeBrushKey(const Brush* brush);

	// Returns the stored order, or nullptr when the category has none.
	const std::vector<std::string>* GetOrder(const std::string& tilesetName, TilesetCategoryType type);
	bool HasOrder(const std::string& tilesetName, TilesetCategoryType type);

	// Stores (and writes out) the order of the given brushes.
	void SetOrder(const std::string& tilesetName, TilesetCategoryType type, const std::vector<Brush*>& brushes);
	// Drops the custom order for one category.
	void ClearOrder(const std::string& tilesetName, TilesetCategoryType type);

protected:
	static std::string MakeStoreKey(const std::string& tilesetName, TilesetCategoryType type);
	// Loads the file of the currently loaded version (reloads when it changes).
	void EnsureLoaded();
	std::string GetStorePath() const;
	void Save();

	std::map<std::string, std::vector<std::string>> orders;
	std::string loaded_path;
	bool loaded = false;
};

extern TilesetOrderStore g_tileset_order;

#endif
