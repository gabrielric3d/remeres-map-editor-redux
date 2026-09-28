//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "game/br_loot_catalog.h"

#include "app/managers/version_manager.h"

#include <wx/file.h>
#include <spdlog/spdlog.h>

#include <algorithm>

BRLootCatalog g_br_loot_catalog;

namespace {
	constexpr const char* CATALOG_FILE = "battleroyale_loot_catalog.json";
	constexpr std::chrono::milliseconds CHECK_EVERY { 1000 };

	int readInt(const json::json& object, const char* key, int fallback) {
		const auto it = object.find(key);
		if (it == object.end()) {
			return fallback;
		}
		if (it->is_number_integer()) {
			return static_cast<int>(it->get<int64_t>());
		}
		if (it->is_number_float()) {
			return static_cast<int>(it->get<double>());
		}
		return fallback;
	}

	std::string readString(const json::json& object, const char* key) {
		const auto it = object.find(key);
		if (it == object.end() || !it->is_string()) {
			return std::string();
		}
		return it->get<std::string>();
	}
} // namespace

wxString BRLootCatalog::getPath() const {
	ClientVersion* client = g_version.getLoadedVersion();
	if (!client) {
		return wxString();
	}
	wxFileName file(client->getDataPath().GetFullPath(), CATALOG_FILE);
	return file.GetFullPath();
}

void BRLootCatalog::refresh(bool force) {
	const auto now = std::chrono::steady_clock::now();
	if (!force && checked_once && now - last_check < CHECK_EVERY) {
		return;
	}
	checked_once = true;
	last_check = now;

	const wxString path = getPath();
	if (path.empty() || !wxFileName::FileExists(path)) {
		if (loaded) {
			loaded = false;
			tiers.clear();
			entries.clear();
			++version;
		}
		return;
	}

	const time_t mtime = wxFileName(path).GetModificationTime().GetTicks();
	if (loaded && path == loaded_path && mtime == loaded_mtime) {
		return;
	}
	if (load(path)) {
		loaded_path = path;
		loaded_mtime = mtime;
	}
}

bool BRLootCatalog::load(const wxString& path) {
	std::string text;
	{
		wxFile file;
		if (!file.Open(path, wxFile::read)) {
			return false;
		}
		const wxFileOffset length = file.Length();
		if (length <= 0) {
			return false;
		}
		text.assign(static_cast<size_t>(length), '\0');
		const auto got = file.Read(text.data(), static_cast<size_t>(length));
		if (got != static_cast<decltype(got)>(length)) {
			return false;
		}
	}

	const json::json doc = json::json::parse(text, nullptr, false);
	if (doc.is_discarded() || !doc.is_object()) {
		spdlog::error("BR loot catalog: {} is not valid JSON", nstr(path));
		return false;
	}

	std::vector<Tier> new_tiers;
	if (const auto list = doc.find("tiers"); list != doc.end() && list->is_array()) {
		for (const json::json& entry : *list) {
			if (!entry.is_object()) {
				continue;
			}
			Tier tier;
			tier.tier = readInt(entry, "tier", 0);
			tier.label = readString(entry, "label");
			if (tier.tier > 0) {
				new_tiers.push_back(std::move(tier));
			}
		}
	}
	std::sort(new_tiers.begin(), new_tiers.end(), [](const Tier& a, const Tier& b) {
		return a.tier < b.tier;
	});

	std::vector<Entry> new_entries;
	if (const auto list = doc.find("items"); list != doc.end() && list->is_array()) {
		for (const json::json& entry : *list) {
			if (!entry.is_object()) {
				continue;
			}
			Entry item;
			item.name = readString(entry, "name");
			const int id = readInt(entry, "id", 0);
			item.server_id = static_cast<uint16_t>(std::clamp(id, 0, 0xFFFF));
			if (!item.name.empty()) {
				new_entries.push_back(std::move(item));
			}
		}
	}
	std::sort(new_entries.begin(), new_entries.end(), [](const Entry& a, const Entry& b) {
		return a.name < b.name;
	});

	tiers = std::move(new_tiers);
	entries = std::move(new_entries);
	loaded = true;
	++version;
	spdlog::info("BR loot catalog: {} tier(s), {} item(s) from {}", tiers.size(), entries.size(), nstr(path));
	return true;
}

std::string BRLootCatalog::tierLabel(int tier) const {
	for (const Tier& entry : tiers) {
		if (entry.tier == tier) {
			return entry.label;
		}
	}
	return std::string();
}

uint16_t BRLootCatalog::serverIdOf(const std::string& name) const {
	// The list is sorted by name, and it is short (the loot table).
	const auto it = std::lower_bound(entries.begin(), entries.end(), name, [](const Entry& entry, const std::string& key) {
		return entry.name < key;
	});
	if (it != entries.end() && it->name == name) {
		return it->server_id;
	}
	return 0;
}
