//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "game/instance_zones.h"

#include "map/map.h"
#include "map/tile.h"

#include "ext/pugixml.hpp"

#include <algorithm>

void InstanceZones::clear() {
	zones.clear();
}

InstanceZone* InstanceZones::addZone(std::unique_ptr<InstanceZone> zone) {
	if (!zone) {
		return nullptr;
	}
	const uint32_t id = zone->id;
	InstanceZone* raw = zone.get();
	zones[id] = std::move(zone);
	return raw;
}

InstanceZone* InstanceZones::createZone() {
	const uint32_t id = getEmptyID();
	// Default 1 instance = painted but not instanced yet. The mapper raises it in
	// the palette; 1 keeps the area behaving exactly like today until they do.
	auto zone = std::make_unique<InstanceZone>(id, "Instance Zone #" + std::to_string(id), uint16_t(1));
	return addZone(std::move(zone));
}

InstanceZone* InstanceZones::getZone(uint32_t id) {
	auto it = zones.find(id);
	return it != zones.end() ? it->second.get() : nullptr;
}

void InstanceZones::removeZone(uint32_t id) {
	zones.erase(id);
}

uint32_t InstanceZones::getEmptyID() const {
	// Lowest free id >= 1 (ids come from a map, so the container is sorted).
	uint32_t candidate = 1;
	for (const auto& entry : zones) {
		if (entry.first == candidate) {
			++candidate;
		} else if (entry.first > candidate) {
			break;
		}
	}
	return candidate;
}

std::vector<InstanceZone*> InstanceZones::getOrdered() const {
	std::vector<InstanceZone*> ordered;
	ordered.reserve(zones.size());
	for (const auto& entry : zones) {
		ordered.push_back(entry.second.get());
	}
	return ordered;
}

size_t InstanceZones::countPaintedTiles(uint32_t id) const {
	if (id == 0) {
		return 0;
	}
	size_t count = 0;
	for (MapIterator it = map.begin(); it != map.end(); ++it) {
		const Tile* tile = (*it).get();
		if (tile && tile->getInstanceZoneId() == id) {
			++count;
		}
	}
	return count;
}

size_t InstanceZones::clearPaintedTiles(uint32_t id) {
	// Guard: 0 e "nenhuma zona", entao varrer por ele apagaria nada e percorreria
	// o mapa inteiro a toa.
	if (id == 0) {
		return 0;
	}

	size_t cleared = 0;
	for (MapIterator it = map.begin(); it != map.end(); ++it) {
		Tile* tile = (*it).get();
		if (!tile || tile->getInstanceZoneId() != id) {
			continue;
		}
		tile->setInstanceZoneId(0);
		++cleared;
	}

	// Os bounds da zona morrem junto com ela; se a zona continuar existindo (uso
	// futuro: "limpar sem remover"), o box precisa deixar de apontar para a area
	// que acabou de ser apagada -- ele so cresce, entao nao encolhe sozinho.
	if (InstanceZone* zone = getZone(id)) {
		zone->clearBounds();
	}
	return cleared;
}

void InstanceZones::recalculateBounds() {
	for (const auto& entry : zones) {
		entry.second->clearBounds();
	}

	// Full map walk. Only worth doing on demand (palette "Recenter"), never per
	// frame: the incremental path in the OTBM loader and the brush covers normal use.
	for (MapIterator it = map.begin(); it != map.end(); ++it) {
		Tile* tile = (*it).get();
		if (!tile || !tile->isInstanceZoneTile()) {
			continue;
		}
		InstanceZone* zone = getZone(tile->getInstanceZoneId());
		if (!zone) {
			continue;
		}
		const Position pos = tile->getPosition();
		zone->expandBounds(pos.x, pos.y, pos.z);
	}
}

FileName InstanceZones::BuildSidecarPath(const FileName& mapFile) {
	// "<mapbase>-instance.xml" next to the map, matching the -house/-spawn/-sound
	// convention the TFS server already expects (buildRelatedXmlFile).
	FileName sidecar(mapFile);
	if (sidecar.GetFullPath().empty()) {
		return sidecar;
	}
	sidecar.SetName(mapFile.GetName() + "-instance");
	sidecar.SetExt("xml");
	return sidecar;
}

bool InstanceZones::loadFromFile(const FileName& mapFile) {
	clear();
	FileName sidecar = BuildSidecarPath(mapFile);
	if (sidecar.GetFullPath().empty() || !sidecar.FileExists()) {
		return false;
	}

	pugi::xml_document doc;
	if (!doc.load_file(nstr(sidecar.GetFullPath()).c_str())) {
		return false;
	}

	pugi::xml_node root = doc.child("instancezones");
	if (!root) {
		return false;
	}

	for (pugi::xml_node zoneNode = root.child("zone"); zoneNode; zoneNode = zoneNode.next_sibling("zone")) {
		const uint32_t id = zoneNode.attribute("id").as_uint(0);
		if (id == 0) {
			continue;
		}
		// Clamp to at least 1: a zone with 0 instances would mean "area exists but
		// nobody can be in it", which is never what the mapper meant.
		uint32_t rawCount = zoneNode.attribute("instances").as_uint(1);
		if (rawCount < 1) {
			rawCount = 1;
		} else if (rawCount > 0xFFFF) {
			rawCount = 0xFFFF;
		}
		auto zone = std::make_unique<InstanceZone>(
			id,
			zoneNode.attribute("name").as_string(),
			static_cast<uint16_t>(rawCount));
		zones[id] = std::move(zone);
	}
	return true;
}

bool InstanceZones::saveToFile(const FileName& mapFile) const {
	FileName sidecar = BuildSidecarPath(mapFile);
	if (sidecar.GetFullPath().empty()) {
		return false;
	}

	// No zones -> remove a stale sidecar so we don't leave an empty file behind.
	if (zones.empty()) {
		if (sidecar.FileExists()) {
			wxRemoveFile(sidecar.GetFullPath());
		}
		return true;
	}

	pugi::xml_document doc;
	pugi::xml_node decl = doc.append_child(pugi::node_declaration);
	decl.append_attribute("version") = "1.0";

	pugi::xml_node root = doc.append_child("instancezones");
	for (const auto& entry : zones) {
		const InstanceZone* zone = entry.second.get();
		pugi::xml_node zoneNode = root.append_child("zone");
		zoneNode.append_attribute("id") = zone->id;
		zoneNode.append_attribute("name") = zone->name.c_str();
		zoneNode.append_attribute("instances") = zone->instances;
	}

	return doc.save_file(nstr(sidecar.GetFullPath()).c_str(), "\t", pugi::format_default, pugi::encoding_utf8);
}
