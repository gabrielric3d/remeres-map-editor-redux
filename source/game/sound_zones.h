//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// BlackTalon: ambient sound zones. A zone is a painted set of tiles (each tile
// stores the zone id via OTBM_ATTR_SOUND_ZONE) plus metadata {id, name, track}.
// The tile->id link lives in the OTBM; this metadata lives in a sidecar
// "<map>-sound.xml" so the TFS server can read zoneId -> track and play the
// zone's ambient music when the player steps onto a painted tile.

#ifndef RME_SOUND_ZONES_H_
#define RME_SOUND_ZONES_H_

#include "app/main.h" // FileName (= wxFileName) alias
#include "game/zone_bounds.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class SoundZone : public ZoneBoundsHolder {
public:
	SoundZone() = default;
	SoundZone(uint32_t id, std::string name, std::string track) :
		id(id), name(std::move(name)), track(std::move(track)) { }

	uint32_t id = 0;
	std::string name;   // editor-only label
	std::string track;  // sound key the client plays for this zone (e.g. "ambient_forest")
};

using SoundZoneMap = std::map<uint32_t, std::unique_ptr<SoundZone>>;

class Map;

class SoundZones {
	Map& map;

public:
	explicit SoundZones(Map& map) : map(map) { }
	~SoundZones() = default;

	void clear();

	// Adds/replaces a zone (owns it). Returns the raw pointer.
	SoundZone* addZone(std::unique_ptr<SoundZone> zone);
	// Creates a new zone with the next free id and a default name; returns it.
	SoundZone* createZone();
	SoundZone* getZone(uint32_t id);
	void removeZone(uint32_t id);
	uint32_t getEmptyID() const;

	// Zones sorted by id (for the palette list).
	std::vector<SoundZone*> getOrdered() const;

	// Walks the map and rebuilds every zone's bounds from scratch. Only needed
	// after erasing tiles (the incremental path never shrinks a box) -- the palette
	// exposes it as "Recenter". O(map), so never call it per frame.
	void recalculateBounds();

	// Sidecar "<map>-sound.xml" persistence (server reads this file).
	bool loadFromFile(const FileName& mapFile);
	bool saveToFile(const FileName& mapFile) const;
	static FileName BuildSidecarPath(const FileName& mapFile);

	size_t size() const { return zones.size(); }
	bool empty() const { return zones.empty(); }

	SoundZoneMap zones;

	SoundZoneMap::iterator begin() { return zones.begin(); }
	SoundZoneMap::const_iterator begin() const { return zones.begin(); }
	SoundZoneMap::iterator end() { return zones.end(); }
	SoundZoneMap::const_iterator end() const { return zones.end(); }
};

#endif
