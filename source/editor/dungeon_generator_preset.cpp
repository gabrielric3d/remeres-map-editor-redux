//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "editor/dungeon_generator.h"
#include "util/file_system.h"

#include <wx/dir.h>
#include <wx/filename.h>

namespace DungeonGen {

//=============================================================================
// WallConfig XML helpers
//=============================================================================

static void saveWallItems(pugi::xml_node& parent, const char* dirName, const std::vector<WallItem>& items) {
	if (items.empty()) return;

	// Single item: keep compact format as attribute on parent for backwards compat
	if (items.size() == 1) {
		parent.append_attribute(dirName) = items.front().id;
		return;
	}

	// Multiple items: write child nodes
	for (const auto& item : items) {
		pugi::xml_node itemNode = parent.append_child(dirName);
		itemNode.append_attribute("id") = item.id;
		itemNode.append_attribute("chance") = item.chance;
	}
}

static std::vector<WallItem> loadWallItems(const pugi::xml_node& parent, const char* dirName) {
	std::vector<WallItem> items;

	// Check for child nodes first (new format with variations)
	for (pugi::xml_node child = parent.child(dirName); child; child = child.next_sibling(dirName)) {
		uint16_t id = child.attribute("id").as_uint(0);
		int chance = child.attribute("chance").as_int(100);
		if (id > 0) {
			items.push_back({id, chance});
		}
	}

	// Fallback: single attribute (legacy format)
	if (items.empty()) {
		uint16_t id = parent.attribute(dirName).as_uint(0);
		if (id > 0) {
			items.push_back({id, 100});
		}
	}

	return items;
}

static void saveWallConfig(pugi::xml_node& parent, const char* name, const WallConfig& cfg) {
	if (!cfg.isValid()) return;

	pugi::xml_node node = parent.append_child(name);
	saveWallItems(node, "north", cfg.north);
	saveWallItems(node, "south", cfg.south);
	saveWallItems(node, "east", cfg.east);
	saveWallItems(node, "west", cfg.west);
	saveWallItems(node, "nw", cfg.nw);
	saveWallItems(node, "ne", cfg.ne);
	saveWallItems(node, "sw", cfg.sw);
	saveWallItems(node, "se", cfg.se);
	saveWallItems(node, "pillar", cfg.pillar);
}

static WallConfig loadWallConfig(const pugi::xml_node& node) {
	WallConfig cfg;
	cfg.north = loadWallItems(node, "north");
	cfg.south = loadWallItems(node, "south");
	cfg.east = loadWallItems(node, "east");
	cfg.west = loadWallItems(node, "west");
	cfg.nw = loadWallItems(node, "nw");
	cfg.ne = loadWallItems(node, "ne");
	cfg.sw = loadWallItems(node, "sw");
	cfg.se = loadWallItems(node, "se");
	cfg.pillar = loadWallItems(node, "pillar");
	return cfg;
}

//=============================================================================
// FloorConfig XML helpers
//=============================================================================

static void saveFloorConfig(pugi::xml_node& parent, const char* name, const FloorConfig& cfg) {
	if (!cfg.isValid()) return;

	pugi::xml_node node = parent.append_child(name);
	for (const auto& item : cfg.items) {
		pugi::xml_node itemNode = node.append_child("floor");
		itemNode.append_attribute("id") = item.id;
		itemNode.append_attribute("chance") = item.chance;
	}
}

static FloorConfig loadFloorConfig(const pugi::xml_node& node) {
	FloorConfig cfg;
	for (pugi::xml_node child = node.child("floor"); child; child = child.next_sibling("floor")) {
		uint16_t id = child.attribute("id").as_uint(0);
		int chance = child.attribute("chance").as_int(100);
		if (id > 0) {
			cfg.items.push_back({id, chance});
		}
	}
	return cfg;
}

//=============================================================================
// BorderConfig XML helpers
//=============================================================================

static void saveBorderConfig(pugi::xml_node& parent, const char* name, const BorderConfig& cfg) {
	pugi::xml_node node = parent.append_child(name);
	node.append_attribute("north") = cfg.north;
	node.append_attribute("south") = cfg.south;
	node.append_attribute("east") = cfg.east;
	node.append_attribute("west") = cfg.west;
	node.append_attribute("nw") = cfg.nw;
	node.append_attribute("ne") = cfg.ne;
	node.append_attribute("sw") = cfg.sw;
	node.append_attribute("se") = cfg.se;
	node.append_attribute("inner_nw") = cfg.inner_nw;
	node.append_attribute("inner_ne") = cfg.inner_ne;
	node.append_attribute("inner_sw") = cfg.inner_sw;
	node.append_attribute("inner_se") = cfg.inner_se;
}

static BorderConfig loadBorderConfig(const pugi::xml_node& node) {
	BorderConfig cfg;
	cfg.north = node.attribute("north").as_uint(0);
	cfg.south = node.attribute("south").as_uint(0);
	cfg.east = node.attribute("east").as_uint(0);
	cfg.west = node.attribute("west").as_uint(0);
	cfg.nw = node.attribute("nw").as_uint(0);
	cfg.ne = node.attribute("ne").as_uint(0);
	cfg.sw = node.attribute("sw").as_uint(0);
	cfg.se = node.attribute("se").as_uint(0);
	cfg.inner_nw = node.attribute("inner_nw").as_uint(0);
	cfg.inner_ne = node.attribute("inner_ne").as_uint(0);
	cfg.inner_sw = node.attribute("inner_sw").as_uint(0);
	cfg.inner_se = node.attribute("inner_se").as_uint(0);
	return cfg;
}

//=============================================================================
// DungeonPreset Serialization
//=============================================================================

bool DungeonPreset::saveToFile(const std::string& filepath) const {
	pugi::xml_document doc;

	pugi::xml_node root = doc.append_child("dungeon_preset");
	root.append_attribute("name") = name.c_str();
	root.append_attribute("version") = "1.0";

	// Terrain
	pugi::xml_node terrainNode = root.append_child("terrain");
	terrainNode.append_attribute("ground") = groundId;
	terrainNode.append_attribute("patch") = patchId;
	terrainNode.append_attribute("fill") = fillId;
	terrainNode.append_attribute("brush") = brushId;

	// Floor variations
	saveFloorConfig(root, "room_floors", roomFloors);
	saveFloorConfig(root, "corridor_floors", corridorFloors);

	// Walls
	saveWallConfig(root, "walls", walls);
	saveWallConfig(root, "corridor_walls", corridorWalls);

	// Borders
	saveBorderConfig(root, "borders", borders);
	saveBorderConfig(root, "brush_borders", brushBorders);

	// Details
	pugi::xml_node detailsNode = root.append_child("details");
	for (const auto& group : details) {
		pugi::xml_node groupNode = detailsNode.append_child("group");
		groupNode.append_attribute("chance") = group.chance;
		groupNode.append_attribute("placement") = DetailGroup::placementToString(group.placement).c_str();

		for (uint16_t itemId : group.itemIds) {
			pugi::xml_node itemNode = groupNode.append_child("item");
			itemNode.append_attribute("id") = itemId;
		}
	}

	// Hangables
	if (hangables.isValid()) {
		pugi::xml_node hangNode = root.append_child("hangables");
		hangNode.append_attribute("chance") = hangables.chance;
		hangNode.append_attribute("enable_vertical") = hangables.enableVertical;

		for (uint16_t id : hangables.horizontalIds) {
			pugi::xml_node itemNode = hangNode.append_child("horizontal");
			itemNode.append_attribute("id") = id;
		}
		for (uint16_t id : hangables.verticalIds) {
			pugi::xml_node itemNode = hangNode.append_child("vertical");
			itemNode.append_attribute("id") = id;
		}
	}

	return doc.save_file(filepath.c_str());
}

bool DungeonPreset::loadFromFile(const std::string& filepath) {
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filepath.c_str());
	if (!result) return false;

	pugi::xml_node root = doc.child("dungeon_preset");
	if (!root) return false;

	name = root.attribute("name").as_string("");

	// Terrain
	pugi::xml_node terrainNode = root.child("terrain");
	if (terrainNode) {
		groundId = terrainNode.attribute("ground").as_uint(0);
		patchId = terrainNode.attribute("patch").as_uint(0);
		fillId = terrainNode.attribute("fill").as_uint(0);
		brushId = terrainNode.attribute("brush").as_uint(0);
	}

	// Floor variations
	pugi::xml_node roomFloorsNode = root.child("room_floors");
	if (roomFloorsNode) {
		roomFloors = loadFloorConfig(roomFloorsNode);
	}

	pugi::xml_node corridorFloorsNode = root.child("corridor_floors");
	if (corridorFloorsNode) {
		corridorFloors = loadFloorConfig(corridorFloorsNode);
	}

	// Walls
	pugi::xml_node wallsNode = root.child("walls");
	if (wallsNode) {
		walls = loadWallConfig(wallsNode);
	}

	pugi::xml_node corridorWallsNode = root.child("corridor_walls");
	if (corridorWallsNode) {
		corridorWalls = loadWallConfig(corridorWallsNode);
	}

	// Borders
	pugi::xml_node bordersNode = root.child("borders");
	if (bordersNode) {
		borders = loadBorderConfig(bordersNode);
	}

	pugi::xml_node brushBordersNode = root.child("brush_borders");
	if (brushBordersNode) {
		brushBorders = loadBorderConfig(brushBordersNode);
	}

	// Details
	details.clear();
	pugi::xml_node detailsNode = root.child("details");
	if (detailsNode) {
		for (pugi::xml_node groupNode = detailsNode.child("group"); groupNode;
		     groupNode = groupNode.next_sibling("group")) {
			DetailGroup group;
			group.chance = groupNode.attribute("chance").as_float(0.0f);
			group.placement = DetailGroup::placementFromString(
				groupNode.attribute("placement").as_string("anywhere"));

			for (pugi::xml_node itemNode = groupNode.child("item"); itemNode;
			     itemNode = itemNode.next_sibling("item")) {
				uint16_t id = itemNode.attribute("id").as_uint(0);
				if (id > 0) {
					group.itemIds.push_back(id);
				}
			}

			if (!group.itemIds.empty() || group.chance > 0.0f) {
				details.push_back(std::move(group));
			}
		}
	}

	// Hangables
	hangables = HangableConfig{};
	pugi::xml_node hangNode = root.child("hangables");
	if (hangNode) {
		hangables.chance = hangNode.attribute("chance").as_float(0.05f);
		hangables.enableVertical = hangNode.attribute("enable_vertical").as_bool(false);

		for (pugi::xml_node itemNode = hangNode.child("horizontal"); itemNode;
		     itemNode = itemNode.next_sibling("horizontal")) {
			uint16_t id = itemNode.attribute("id").as_uint(0);
			if (id > 0) {
				hangables.horizontalIds.push_back(id);
			}
		}
		for (pugi::xml_node itemNode = hangNode.child("vertical"); itemNode;
		     itemNode = itemNode.next_sibling("vertical")) {
			uint16_t id = itemNode.attribute("id").as_uint(0);
			if (id > 0) {
				hangables.verticalIds.push_back(id);
			}
		}
	}

	return true;
}

//=============================================================================
// PresetManager
//=============================================================================

PresetManager& PresetManager::getInstance() {
	static PresetManager instance;
	return instance;
}

std::string PresetManager::getPresetsDirectory() const {
	wxString dataDir = FileSystem::GetDataDirectory();
	wxString presetsBaseDir = dataDir + "/presets";
	wxString presetsDir = presetsBaseDir + "/dungeon";

	if (!wxDirExists(presetsBaseDir)) {
		wxMkdir(presetsBaseDir);
	}
	if (!wxDirExists(presetsDir)) {
		wxMkdir(presetsDir);
	}

	return presetsDir.ToStdString();
}

bool PresetManager::loadPresets() {
	m_presets.clear();

	std::string dir = getPresetsDirectory();
	wxDir wxdir(dir);

	if (!wxdir.IsOpened()) {
		return false;
	}

	wxString filename;
	bool cont = wxdir.GetFirst(&filename, "*.xml", wxDIR_FILES);

	while (cont) {
		std::string filepath = dir + "/" + filename.ToStdString();
		DungeonPreset preset;
		if (preset.loadFromFile(filepath)) {
			m_presets[preset.name] = preset;
		}
		cont = wxdir.GetNext(&filename);
	}

	m_loaded = true;
	return true;
}

bool PresetManager::savePresets() {
	std::string dir = getPresetsDirectory();
	bool allOk = true;

	for (const auto& [name, preset] : m_presets) {
		std::string filename = name;
		// Sanitize filename
		std::replace(filename.begin(), filename.end(), ' ', '_');
		std::replace(filename.begin(), filename.end(), '/', '_');
		std::replace(filename.begin(), filename.end(), '\\', '_');

		std::string filepath = dir + "/" + filename + ".xml";
		if (!preset.saveToFile(filepath)) {
			allOk = false;
		}
	}

	return allOk;
}

std::vector<std::string> PresetManager::getPresetNames() const {
	std::vector<std::string> names;
	names.reserve(m_presets.size());
	for (const auto& [name, preset] : m_presets) {
		names.push_back(name);
	}
	std::sort(names.begin(), names.end());
	return names;
}

const DungeonPreset* PresetManager::getPreset(const std::string& name) const {
	auto it = m_presets.find(name);
	return it != m_presets.end() ? &it->second : nullptr;
}

DungeonPreset* PresetManager::getPresetMutable(const std::string& name) {
	auto it = m_presets.find(name);
	return it != m_presets.end() ? &it->second : nullptr;
}

bool PresetManager::addPreset(const DungeonPreset& preset) {
	m_presets[preset.name] = preset;
	return savePresets();
}

bool PresetManager::removePreset(const std::string& name) {
	auto it = m_presets.find(name);
	if (it == m_presets.end()) return false;

	// Delete file
	std::string dir = getPresetsDirectory();
	std::string filename = name;
	std::replace(filename.begin(), filename.end(), ' ', '_');
	std::string filepath = dir + "/" + filename + ".xml";
	wxRemoveFile(filepath);

	m_presets.erase(it);
	return true;
}

bool PresetManager::renamePreset(const std::string& oldName, const std::string& newName) {
	auto it = m_presets.find(oldName);
	if (it == m_presets.end()) return false;

	DungeonPreset preset = it->second;
	preset.name = newName;
	m_presets.erase(it);

	// Delete old file
	std::string dir = getPresetsDirectory();
	std::string oldFilename = oldName;
	std::replace(oldFilename.begin(), oldFilename.end(), ' ', '_');
	wxRemoveFile(dir + "/" + oldFilename + ".xml");

	m_presets[newName] = preset;
	return savePresets();
}

} // namespace DungeonGen
