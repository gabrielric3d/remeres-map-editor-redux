//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "game/sound_zones.h"

#include "map/map.h"
#include "map/tile.h"

#include "ext/pugixml.hpp"

#include <algorithm>

void SoundZones::clear() {
	zones.clear();
}

SoundZone* SoundZones::addZone(std::unique_ptr<SoundZone> zone) {
	if (!zone) {
		return nullptr;
	}
	const uint32_t id = zone->id;
	SoundZone* raw = zone.get();
	zones[id] = std::move(zone);
	return raw;
}

SoundZone* SoundZones::createZone() {
	const uint32_t id = getEmptyID();
	auto zone = std::make_unique<SoundZone>(id, "Ambient Zone #" + std::to_string(id), std::string());
	return addZone(std::move(zone));
}

SoundZone* SoundZones::getZone(uint32_t id) {
	auto it = zones.find(id);
	return it != zones.end() ? it->second.get() : nullptr;
}

void SoundZones::removeZone(uint32_t id) {
	zones.erase(id);
}

uint32_t SoundZones::getEmptyID() const {
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

std::vector<SoundZone*> SoundZones::getOrdered() const {
	std::vector<SoundZone*> ordered;
	ordered.reserve(zones.size());
	for (const auto& entry : zones) {
		ordered.push_back(entry.second.get());
	}
	return ordered;
}

void SoundZones::recalculateBounds() {
	for (const auto& entry : zones) {
		entry.second->clearBounds();
	}

	// Full map walk. Only worth doing on demand (palette "Recenter") and once at
	// load: the label drawer tops the boxes up incrementally during normal use.
	for (MapIterator it = map.begin(); it != map.end(); ++it) {
		Tile* tile = (*it).get();
		if (!tile || !tile->isSoundZoneTile()) {
			continue;
		}
		SoundZone* zone = getZone(tile->getSoundZoneId());
		if (!zone) {
			continue;
		}
		const Position pos = tile->getPosition();
		zone->expandBounds(pos.x, pos.y, pos.z);
	}
}

FileName SoundZones::BuildSidecarPath(const FileName& mapFile) {
	// "<mapbase>-sound.xml" next to the map, matching the -house/-spawn convention
	// the TFS server already expects (buildRelatedXmlFile(fileName, "-sound.xml")).
	FileName sidecar(mapFile);
	if (sidecar.GetFullPath().empty()) {
		return sidecar;
	}
	sidecar.SetName(mapFile.GetName() + "-sound");
	sidecar.SetExt("xml");
	return sidecar;
}

bool SoundZones::loadFromFile(const FileName& mapFile) {
	clear();
	FileName sidecar = BuildSidecarPath(mapFile);
	if (sidecar.GetFullPath().empty() || !sidecar.FileExists()) {
		return false;
	}

	pugi::xml_document doc;
	if (!doc.load_file(nstr(sidecar.GetFullPath()).c_str())) {
		return false;
	}

	pugi::xml_node root = doc.child("sounds");
	if (!root) {
		return false;
	}

	for (pugi::xml_node zoneNode = root.child("zone"); zoneNode; zoneNode = zoneNode.next_sibling("zone")) {
		const uint32_t id = zoneNode.attribute("id").as_uint(0);
		if (id == 0) {
			continue;
		}
		auto zone = std::make_unique<SoundZone>(
			id,
			zoneNode.attribute("name").as_string(),
			zoneNode.attribute("track").as_string());
		zones[id] = std::move(zone);
	}
	return true;
}

bool SoundZones::saveToFile(const FileName& mapFile) const {
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

	pugi::xml_node root = doc.append_child("sounds");
	for (const auto& entry : zones) {
		const SoundZone* zone = entry.second.get();
		pugi::xml_node zoneNode = root.append_child("zone");
		zoneNode.append_attribute("id") = zone->id;
		zoneNode.append_attribute("name") = zone->name.c_str();
		zoneNode.append_attribute("track") = zone->track.c_str();
	}

	return doc.save_file(nstr(sidecar.GetFullPath()).c_str(), "\t", pugi::format_default, pugi::encoding_utf8);
}
