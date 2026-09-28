//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// Battle Royale: loot zones. A zone is a painted set of tiles (each tile keeps the
// zone id in Tile::brLootZoneId) plus a TIER, which decides what the server rolls
// there. The items placed by hand on single tiles live here too.
//
// Everything goes to a sidecar next to the map, "<map>-brloot.json", and NOT to the
// OTBM: TFS 1.6 refuses a map with an unknown tile attribute (iomap.cpp), so an OTBM
// attribute would cost a server change. The sidecar is also the very file the battle
// royale tooling reads (make_loot_zones.py), in the same canonical text it writes --
// saving without changes gives the same bytes, and git shows no false diff.
//
// Kept apart from the instance zones on purpose: those belong to BlackTalon, and a
// map of one game must not grow the zones of the other.
//
// Undo: the tile id goes through the normal tile actions. The zone table and the
// placed items are swapped whole (BRLootZonesState, CHANGE_BR_LOOT in
// editor/action.cpp), the way the camera paths are.

#ifndef RME_BR_LOOT_ZONES_H_
#define RME_BR_LOOT_ZONES_H_

#include "app/main.h" // FileName (= wxFileName) alias
#include "game/zone_bounds.h"
#include "map/position.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct BRLootZone : public ZoneBoundsHolder {
	uint32_t id = 0;
	int tier = 1;
};

// An item placed by hand on one tile. It is the loot catalog NAME, not an item id:
// the catalog (BRLoot.entries on the server) is the only list the vision cut hides,
// so an item outside it would be seen through walls.
struct BRLootItem {
	Position pos;
	std::string name;
	int min = 1;
	int max = 1;
	int chance = 100;

	bool operator==(const BRLootItem& other) const {
		return pos == other.pos && name == other.name && min == other.min && max == other.max && chance == other.chance;
	}
};

// What one undo step swaps.
struct BRLootZonesState {
	std::map<uint32_t, BRLootZone> zones;
	std::vector<BRLootItem> items;
};

class Map;

class BRLootZones {
public:
	explicit BRLootZones(Map& owner) :
		map(owner) { }

	void clear();
	bool empty() const {
		return state.zones.empty() && state.items.empty();
	}

	const BRLootZone* getZone(uint32_t id) const;
	// 0 = not a zone of this table (a tile pasted from another map, for instance).
	int tierOf(uint32_t id) const;
	std::vector<const BRLootZone*> getOrdered() const;
	const std::vector<BRLootItem>& getItems() const {
		return state.items;
	}
	const BRLootItem* itemAt(const Position& pos) const;

	// Ids are never reused: rules.json keys the per-zone overrides by id, and a new
	// zone silently inheriting a deleted zone's rules would be a nasty surprise. So
	// the counter lives OUTSIDE the undoable state -- undoing "New" burns the id.
	uint32_t allocateId();

	BRLootZonesState snapshot() const {
		return state;
	}
	void swapState(BRLootZonesState& other);

	// Changes whenever a zone can have changed tier. The chunk cache bakes the tile
	// tint, and this is how it learns that it has to bake again.
	uint32_t getGeneration() const {
		return generation;
	}

	// Grows the painted box of a zone (bounds are not undoable content: they only
	// place the label). Unknown ids are ignored.
	void feedBounds(uint32_t id, int x, int y, int z);
	// Rebuilds the painted box of one zone (0 = every zone) from the tiles. O(map).
	void recalculateBounds(uint32_t id = 0);
	size_t countPaintedTiles(uint32_t id) const;

	// Sidecar "<map>-brloot.json". Load paints the ids onto the tiles of the map, so
	// it must run after the OTBM is loaded.
	bool loadFromFile(const FileName& mapFile);
	// Writes only when something changed (and bumps the revision when the CONTENT
	// changed). A map without zones and without a sidecar gets no file.
	bool saveToFile(const FileName& mapFile);
	static FileName BuildSidecarPath(const FileName& mapFile);

	// The last problem found while loading or saving, for the status bar.
	const std::string& getLastReport() const {
		return last_report;
	}

private:
	using TilesByZone = std::map<uint32_t, std::vector<Position>>;

	TilesByZone collectTiles() const;
	std::string canonicalText(uint32_t with_revision, const TilesByZone& tiles) const;

	Map& map;
	BRLootZonesState state;
	uint32_t next_id = 1;
	uint32_t revision = 0;
	uint32_t generation = 1;
	// What saving the state as it was loaded would write, and the bytes that were on
	// disk. Comparing with both is what tells "content changed" (bump the revision)
	// from "only the formatting is off" (rewrite, same revision).
	std::string loaded_canonical;
	std::string loaded_disk;
	// The sidecar exists but could not be read: saving must not write over it.
	bool load_failed = false;
	std::string last_report;
};

#endif
