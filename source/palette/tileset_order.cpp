#include "app/main.h"

#include "palette/tileset_order.h"

#include "app/managers/version_manager.h"
#include "brushes/brush.h"
#include "brushes/raw/raw_brush.h"
#include "ext/pugixml.hpp"

#include <spdlog/spdlog.h>

TilesetOrderStore g_tileset_order;

namespace {
	constexpr const char* ORDER_FILE_NAME = "tileset_order.xml";
}

std::string TilesetOrderStore::MakeBrushKey(const Brush* brush) {
	if (!brush) {
		return std::string();
	}
	// RAW brushes share their name with the item, so key them by server id.
	if (brush->is<RAWBrush>()) {
		const RAWBrush* raw = brush->as<RAWBrush>();
		if (raw) {
			return "raw:" + std::to_string(raw->getItemID());
		}
	}
	return "brush:" + brush->getName();
}

std::string TilesetOrderStore::MakeStoreKey(const std::string& tilesetName, TilesetCategoryType type) {
	return tilesetName + "\x1F" + std::to_string(static_cast<int>(type));
}

std::string TilesetOrderStore::GetStorePath() const {
	ClientVersion* version = g_version.getLoadedVersion();
	if (!version) {
		return std::string();
	}
	wxFileName file(version->getDataPath().GetFullPath(), ORDER_FILE_NAME);
	return file.GetFullPath().ToStdString();
}

void TilesetOrderStore::EnsureLoaded() {
	const std::string path = GetStorePath();
	if (path.empty()) {
		// No version loaded, keep whatever is cached but don't claim it's valid.
		return;
	}
	if (loaded && loaded_path == path) {
		return;
	}

	orders.clear();
	loaded = true;
	loaded_path = path;

	pugi::xml_document doc;
	const pugi::xml_parse_result result = doc.load_file(path.c_str());
	if (!result) {
		// Missing file simply means "no custom order yet".
		return;
	}

	pugi::xml_node root = doc.child("tileset_order");
	if (!root) {
		return;
	}

	for (pugi::xml_node tilesetNode = root.child("tileset"); tilesetNode; tilesetNode = tilesetNode.next_sibling("tileset")) {
		const std::string name = tilesetNode.attribute("name").as_string();
		if (name.empty()) {
			continue;
		}
		const int category = tilesetNode.attribute("category").as_int(-1);
		if (category < 0 || category > static_cast<int>(TILESET_UNKNOWN)) {
			continue;
		}

		std::vector<std::string> keys;
		for (pugi::xml_node brushNode = tilesetNode.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
			const std::string key = brushNode.attribute("key").as_string();
			if (!key.empty()) {
				keys.push_back(key);
			}
		}

		if (!keys.empty()) {
			orders[MakeStoreKey(name, static_cast<TilesetCategoryType>(category))] = std::move(keys);
		}
	}
}

void TilesetOrderStore::Save() {
	const std::string path = GetStorePath();
	if (path.empty()) {
		return;
	}

	pugi::xml_document doc;
	pugi::xml_node decl = doc.append_child(pugi::node_declaration);
	decl.append_attribute("version") = "1.0";
	decl.append_attribute("encoding") = "UTF-8";

	pugi::xml_node root = doc.append_child("tileset_order");

	for (const auto& entry : orders) {
		if (entry.second.empty()) {
			continue;
		}
		const size_t separator = entry.first.find('\x1F');
		if (separator == std::string::npos) {
			continue;
		}

		pugi::xml_node tilesetNode = root.append_child("tileset");
		tilesetNode.append_attribute("name") = entry.first.substr(0, separator).c_str();
		tilesetNode.append_attribute("category") = entry.first.substr(separator + 1).c_str();

		for (const std::string& key : entry.second) {
			tilesetNode.append_child("brush").append_attribute("key") = key.c_str();
		}
	}

	if (!doc.save_file(path.c_str(), "\t", pugi::format_default, pugi::encoding_utf8)) {
		spdlog::warn("Could not write the tileset order to '{}'", path);
	}
}

const std::vector<std::string>* TilesetOrderStore::GetOrder(const std::string& tilesetName, TilesetCategoryType type) {
	EnsureLoaded();
	const auto it = orders.find(MakeStoreKey(tilesetName, type));
	if (it == orders.end() || it->second.empty()) {
		return nullptr;
	}
	return &it->second;
}

bool TilesetOrderStore::HasOrder(const std::string& tilesetName, TilesetCategoryType type) {
	return GetOrder(tilesetName, type) != nullptr;
}

void TilesetOrderStore::SetOrder(const std::string& tilesetName, TilesetCategoryType type, const std::vector<Brush*>& brushes) {
	EnsureLoaded();

	std::vector<std::string> keys;
	keys.reserve(brushes.size());
	for (const Brush* brush : brushes) {
		std::string key = MakeBrushKey(brush);
		if (!key.empty()) {
			keys.push_back(std::move(key));
		}
	}

	orders[MakeStoreKey(tilesetName, type)] = std::move(keys);
	Save();
}

void TilesetOrderStore::ClearOrder(const std::string& tilesetName, TilesetCategoryType type) {
	EnsureLoaded();
	const auto it = orders.find(MakeStoreKey(tilesetName, type));
	if (it == orders.end()) {
		return;
	}
	orders.erase(it);
	Save();
}
