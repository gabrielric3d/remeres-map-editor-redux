//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// BlackTalon: instance zones. A zone is a painted set of tiles (each tile stores
// the zone id via OTBM_ATTR_INSTANCE_ZONE) plus metadata {id, name, instances}.
// The tile->id link lives in the OTBM; this metadata lives in a sidecar
// "<map>-instance.xml" so the TFS server can read zoneId -> instance count and
// know how many parallel copies of that area exist over the SAME coordinates
// (the 4th axis `i` of Position, see server/src/thing.h).
//
// Modeled 1:1 on SoundZones -- same sidecar convention, same id allocation, same
// palette shape. If you touch one, check whether the other needs the same fix.
//
// NOTE on what this does NOT do: the zone marks WHICH area is instanced and HOW
// MANY instances it has. It does not duplicate geometry -- ground and walls are
// shared by every instance, because the engine's Tile has no instance of its own.
// Only what is CREATED over the tiles (creatures, items, effects) is separated.

#ifndef RME_INSTANCE_ZONES_H_
#define RME_INSTANCE_ZONES_H_

#include "app/main.h" // FileName (= wxFileName) alias
#include "game/zone_bounds.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class InstanceZone : public ZoneBoundsHolder {
public:
	InstanceZone() = default;
	InstanceZone(uint32_t id, std::string name, uint16_t instances) :
		id(id), name(std::move(name)), instances(instances) { }

	uint32_t id = 0;
	std::string name;        // editor-only label
	uint16_t instances = 1;  // how many parallel instances this area runs (1 = not instanced)
};

using InstanceZoneMap = std::map<uint32_t, std::unique_ptr<InstanceZone>>;

class Map;

class InstanceZones {
	Map& map;

public:
	explicit InstanceZones(Map& map) : map(map) { }
	~InstanceZones() = default;

	void clear();

	// Adds/replaces a zone (owns it). Returns the raw pointer.
	InstanceZone* addZone(std::unique_ptr<InstanceZone> zone);
	// Creates a new zone with the next free id and a default name; returns it.
	InstanceZone* createZone();
	InstanceZone* getZone(uint32_t id);
	void removeZone(uint32_t id);
	uint32_t getEmptyID() const;

	// Zones sorted by id (for the palette list).
	std::vector<InstanceZone*> getOrdered() const;

	// Quantos tiles do mapa estao pintados com esta zona. O(map) -- e para o
	// dialogo do Remove poder dizer a escala antes de o mapper decidir.
	size_t countPaintedTiles(uint32_t id) const;
	// Apaga o carimbo desta zona de todo tile que a tenha, e devolve quantos.
	//
	// POR QUE ISTO EXISTE: o id vive em DOIS lugares -- no tile (OTBM attr 25) e
	// no sidecar (metadata). removeZone() so tira o segundo, entao a area seguia
	// carimbada e o server continuava lendo aquele id; como o id e um espaco
	// global, ela passava a responder como a zona de OUTRO mapa que usasse o
	// mesmo numero. Despintar a mao nao e alternativa: mapa real chega a milhoes
	// de tiles marcados.
	//
	// NAO passa pelo sistema de undo: guardar o estado de milhoes de tiles custaria
	// mais que a operacao. Quem chama avisa o usuario.
	size_t clearPaintedTiles(uint32_t id);

	// Walks the map and rebuilds every zone's bounds from scratch. Only needed
	// after erasing tiles (the incremental path never shrinks a box) -- the palette
	// exposes it as "Recenter". O(map), so never call it per frame.
	void recalculateBounds();

	// Sidecar "<map>-instance.xml" persistence (server reads this file).
	bool loadFromFile(const FileName& mapFile);
	bool saveToFile(const FileName& mapFile) const;
	static FileName BuildSidecarPath(const FileName& mapFile);

	size_t size() const { return zones.size(); }
	bool empty() const { return zones.empty(); }

	InstanceZoneMap zones;

	InstanceZoneMap::iterator begin() { return zones.begin(); }
	InstanceZoneMap::const_iterator begin() const { return zones.begin(); }
	InstanceZoneMap::iterator end() { return zones.end(); }
	InstanceZoneMap::const_iterator end() const { return zones.end(); }
};

#endif
