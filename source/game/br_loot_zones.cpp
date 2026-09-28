//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "game/br_loot_zones.h"

#include "map/map.h"
#include "map/tile.h"

#include <wx/file.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <format>
#include <utility>

namespace {
	// A row wider than this is garbage, not a room: it would also make the load
	// walk millions of positions.
	constexpr int MAX_ROW_WIDTH = 4096;

	bool readFileBytes(const wxString& path, std::string& out) {
		wxFile file;
		if (!file.Open(path, wxFile::read)) {
			return false;
		}
		const wxFileOffset length = file.Length();
		if (length < 0) {
			return false;
		}
		out.assign(static_cast<size_t>(length), '\0');
		if (length > 0) {
			const auto got = file.Read(out.data(), static_cast<size_t>(length));
			if (got != static_cast<decltype(got)>(length)) {
				return false;
			}
		}
		return true;
	}

	// Temp file + rename: a failure halfway never leaves a truncated sidecar behind,
	// and a truncated sidecar would be read back as "the map lost its zones".
	bool writeFileBytes(const wxString& path, const std::string& data) {
		const wxString temp = path + ".tmp";
		{
			wxFile file;
			if (!file.Create(temp, true)) {
				return false;
			}
			if (!data.empty() && file.Write(data.data(), data.size()) != data.size()) {
				file.Close();
				wxRemoveFile(temp);
				return false;
			}
		}
		if (!wxRenameFile(temp, path, true)) {
			wxRemoveFile(temp);
			return false;
		}
		return true;
	}

	void appendEscapedCodepoint(std::string& out, uint32_t cp) {
		char buffer[8];
		std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(cp));
		out += buffer;
	}

	// The same bytes as Python's json.dumps (ensure_ascii): make_loot_zones.py writes
	// this file too, and the two writers have to agree to the byte.
	std::string jsonQuote(const std::string& text) {
		std::string out = "\"";
		size_t i = 0;
		while (i < text.size()) {
			const unsigned char c = static_cast<unsigned char>(text[i]);
			if (c == '"') {
				out += "\\\"";
				++i;
				continue;
			}
			if (c == '\\') {
				out += "\\\\";
				++i;
				continue;
			}
			if (c == '\n') {
				out += "\\n";
				++i;
				continue;
			}
			if (c == '\r') {
				out += "\\r";
				++i;
				continue;
			}
			if (c == '\t') {
				out += "\\t";
				++i;
				continue;
			}
			if (c == '\b') {
				out += "\\b";
				++i;
				continue;
			}
			if (c == '\f') {
				out += "\\f";
				++i;
				continue;
			}
			if (c >= 0x20 && c < 0x7F) {
				out += static_cast<char>(c);
				++i;
				continue;
			}
			if (c < 0x80) {
				appendEscapedCodepoint(out, c);
				++i;
				continue;
			}

			// UTF-8 -> code point; a broken sequence becomes U+FFFD, like a lenient decoder.
			uint32_t cp = 0xFFFD;
			size_t extra = 0;
			if ((c & 0xE0) == 0xC0) {
				cp = c & 0x1F;
				extra = 1;
			} else if ((c & 0xF0) == 0xE0) {
				cp = c & 0x0F;
				extra = 2;
			} else if ((c & 0xF8) == 0xF0) {
				cp = c & 0x07;
				extra = 3;
			}
			bool valid = extra > 0 && i + extra < text.size();
			if (valid) {
				for (size_t k = 1; k <= extra; ++k) {
					const unsigned char next = static_cast<unsigned char>(text[i + k]);
					if ((next & 0xC0) != 0x80) {
						valid = false;
						break;
					}
					cp = (cp << 6) | (next & 0x3F);
				}
			}
			if (!valid) {
				cp = 0xFFFD;
				extra = 0;
			}
			if (cp >= 0x10000) {
				const uint32_t v = cp - 0x10000;
				appendEscapedCodepoint(out, 0xD800 | ((v >> 10) & 0x3FF));
				appendEscapedCodepoint(out, 0xDC00 | (v & 0x3FF));
			} else {
				appendEscapedCodepoint(out, cp);
			}
			i += 1 + extra;
		}
		out += "\"";
		return out;
	}

	bool asInt(const json::json& value, int& out) {
		if (value.is_number_integer()) {
			out = static_cast<int>(value.get<int64_t>());
			return true;
		}
		if (value.is_number_float()) {
			out = static_cast<int>(value.get<double>());
			return true;
		}
		return false;
	}

	int readInt(const json::json& object, const char* key, int fallback) {
		const auto it = object.find(key);
		int out = fallback;
		if (it != object.end() && asInt(*it, out)) {
			return out;
		}
		return fallback;
	}

	// An old writer (the Lua script of the editor) wrote an empty list as `{}`.
	const json::json& listOf(const json::json& object, const char* key) {
		static const json::json empty = json::json::array();
		const auto it = object.find(key);
		if (it == object.end() || !it->is_array()) {
			return empty;
		}
		return *it;
	}

	struct Row {
		int z, y, x0, x1;
	};

	// Tiles -> [z, y, x0, x1] runs, sorted by (z, y, x): the canonical form, the same
	// one make_loot_zones.py builds (rows_from_tiles).
	std::vector<Row> rowsOf(std::vector<Position> tiles) {
		std::sort(tiles.begin(), tiles.end(), [](const Position& a, const Position& b) {
			if (a.z != b.z) {
				return a.z < b.z;
			}
			if (a.y != b.y) {
				return a.y < b.y;
			}
			return a.x < b.x;
		});
		tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());

		std::vector<Row> rows;
		for (const Position& p : tiles) {
			if (!rows.empty()) {
				Row& last = rows.back();
				if (last.z == p.z && last.y == p.y && last.x1 == p.x - 1) {
					last.x1 = p.x;
					continue;
				}
			}
			rows.push_back(Row { p.z, p.y, p.x, p.x });
		}
		return rows;
	}

	bool sameTiers(const std::map<uint32_t, BRLootZone>& a, const std::map<uint32_t, BRLootZone>& b) {
		if (a.size() != b.size()) {
			return false;
		}
		auto ia = a.begin();
		auto ib = b.begin();
		for (; ia != a.end(); ++ia, ++ib) {
			if (ia->first != ib->first || ia->second.tier != ib->second.tier) {
				return false;
			}
		}
		return true;
	}
} // namespace

void BRLootZones::clear() {
	state = BRLootZonesState {};
	next_id = 1;
	revision = 0;
	loaded_canonical.clear();
	loaded_disk.clear();
	load_failed = false;
	last_report.clear();
	++generation;
}

const BRLootZone* BRLootZones::getZone(uint32_t id) const {
	const auto it = state.zones.find(id);
	return it != state.zones.end() ? &it->second : nullptr;
}

int BRLootZones::tierOf(uint32_t id) const {
	const auto it = state.zones.find(id);
	return it != state.zones.end() ? it->second.tier : 0;
}

std::vector<const BRLootZone*> BRLootZones::getOrdered() const {
	std::vector<const BRLootZone*> ordered;
	ordered.reserve(state.zones.size());
	for (const auto& entry : state.zones) {
		ordered.push_back(&entry.second);
	}
	return ordered;
}

const BRLootItem* BRLootZones::itemAt(const Position& pos) const {
	for (const BRLootItem& item : state.items) {
		if (item.pos == pos) {
			return &item;
		}
	}
	return nullptr;
}

uint32_t BRLootZones::allocateId() {
	for (const auto& entry : state.zones) {
		if (entry.first >= next_id) {
			next_id = entry.first + 1;
		}
	}
	return next_id++;
}

void BRLootZones::swapState(BRLootZonesState& other) {
	// Only a tier change repaints the tint. Placing an item swaps the whole state too,
	// and re-baking every chunk of the map for it would be a stall for nothing.
	const bool tiers_changed = !sameTiers(state.zones, other.zones);
	std::swap(state, other);
	for (const auto& entry : state.zones) {
		if (entry.first >= next_id) {
			next_id = entry.first + 1;
		}
	}
	if (tiers_changed) {
		++generation;
	}
}

void BRLootZones::feedBounds(uint32_t id, int x, int y, int z) {
	const auto it = state.zones.find(id);
	if (it != state.zones.end()) {
		it->second.expandBounds(x, y, z);
	}
}

void BRLootZones::recalculateBounds(uint32_t id) {
	if (id == 0) {
		for (auto& entry : state.zones) {
			entry.second.clearBounds();
		}
	} else if (const auto it = state.zones.find(id); it != state.zones.end()) {
		it->second.clearBounds();
	} else {
		return;
	}

	for (MapIterator it = map.begin(); it != map.end(); ++it) {
		const Tile* tile = (*it).get();
		if (!tile || !tile->isBRLootZoneTile()) {
			continue;
		}
		const uint32_t tile_zone = tile->getBRLootZoneId();
		if (id != 0 && tile_zone != id) {
			continue;
		}
		const Position pos = tile->getPosition();
		feedBounds(tile_zone, pos.x, pos.y, pos.z);
	}
}

size_t BRLootZones::countPaintedTiles(uint32_t id) const {
	if (id == 0) {
		return 0;
	}
	size_t count = 0;
	for (MapIterator it = map.begin(); it != map.end(); ++it) {
		const Tile* tile = (*it).get();
		if (tile && tile->getBRLootZoneId() == id) {
			++count;
		}
	}
	return count;
}

BRLootZones::TilesByZone BRLootZones::collectTiles() const {
	TilesByZone tiles;
	for (MapIterator it = map.begin(); it != map.end(); ++it) {
		const Tile* tile = (*it).get();
		if (tile && tile->isBRLootZoneTile()) {
			tiles[tile->getBRLootZoneId()].push_back(tile->getPosition());
		}
	}
	return tiles;
}

std::string BRLootZones::canonicalText(uint32_t with_revision, const TilesByZone& tiles) const {
	std::string out;
	out += "{\n";
	out += "  \"format\": 1,\n";
	out += "  \"revision\": " + std::to_string(with_revision) + ",\n";
	out += "  \"nextId\": " + std::to_string(next_id) + ",\n";

	// A zone without a painted tile is not written: the generator rejects it, and it
	// could not drop anything anyway. It stays in the palette until the map closes.
	std::vector<std::pair<const BRLootZone*, std::vector<Row>>> written;
	for (const auto& entry : state.zones) {
		const auto it = tiles.find(entry.first);
		if (it == tiles.end() || it->second.empty()) {
			continue;
		}
		written.emplace_back(&entry.second, rowsOf(it->second));
	}

	if (written.empty()) {
		out += "  \"zones\": [],\n";
	} else {
		out += "  \"zones\": [\n";
		for (size_t i = 0; i < written.size(); ++i) {
			const BRLootZone& zone = *written[i].first;
			const std::vector<Row>& rows = written[i].second;
			out += "    {\"id\": " + std::to_string(zone.id) + ", \"tier\": " + std::to_string(zone.tier) + ", \"rows\": [\n";
			for (size_t r = 0; r < rows.size(); ++r) {
				const Row& row = rows[r];
				out += "      [" + std::to_string(row.z) + ", " + std::to_string(row.y) + ", " + std::to_string(row.x0) + ", " + std::to_string(row.x1) + "]";
				out += (r + 1 < rows.size()) ? ",\n" : "\n";
			}
			out += (i + 1 < written.size()) ? "    ]},\n" : "    ]}\n";
		}
		out += "  ],\n";
	}

	std::vector<const BRLootItem*> items;
	items.reserve(state.items.size());
	for (const BRLootItem& item : state.items) {
		items.push_back(&item);
	}
	std::stable_sort(items.begin(), items.end(), [](const BRLootItem* a, const BRLootItem* b) {
		if (a->pos.z != b->pos.z) {
			return a->pos.z < b->pos.z;
		}
		if (a->pos.y != b->pos.y) {
			return a->pos.y < b->pos.y;
		}
		return a->pos.x < b->pos.x;
	});

	if (items.empty()) {
		out += "  \"items\": []\n";
	} else {
		out += "  \"items\": [\n";
		for (size_t i = 0; i < items.size(); ++i) {
			const BRLootItem& item = *items[i];
			out += "    {\"x\": " + std::to_string(item.pos.x) + ", \"y\": " + std::to_string(item.pos.y) + ", \"z\": " + std::to_string(item.pos.z);
			out += ", \"name\": " + jsonQuote(item.name);
			out += ", \"min\": " + std::to_string(item.min) + ", \"max\": " + std::to_string(item.max) + ", \"chance\": " + std::to_string(item.chance) + "}";
			out += (i + 1 < items.size()) ? ",\n" : "\n";
		}
		out += "  ]\n";
	}

	out += "}\n";
	return out;
}

FileName BRLootZones::BuildSidecarPath(const FileName& mapFile) {
	// "<mapbase>-brloot.json" next to the map, like -house/-spawn/-waypoint.
	FileName sidecar(mapFile);
	if (sidecar.GetFullPath().empty()) {
		return sidecar;
	}
	sidecar.SetName(mapFile.GetName() + "-brloot");
	sidecar.SetExt("json");
	return sidecar;
}

bool BRLootZones::loadFromFile(const FileName& mapFile) {
	clear();
	const FileName sidecar = BuildSidecarPath(mapFile);
	if (sidecar.GetFullPath().empty() || !sidecar.FileExists()) {
		return false;
	}

	const wxString path = sidecar.GetFullPath();
	std::string text;
	if (!readFileBytes(path, text)) {
		load_failed = true;
		last_report = "Could not read " + nstr(path);
		spdlog::error("BR loot zones: {}", last_report);
		return false;
	}
	loaded_disk = text;

	const json::json doc = json::json::parse(text, nullptr, false);
	if (doc.is_discarded() || !doc.is_object()) {
		// Never write over a file we could not read: saving would replace every zone
		// in it with the (empty) table of this session.
		load_failed = true;
		last_report = nstr(sidecar.GetFullName()) + " is not valid JSON; it will not be overwritten until it is fixed";
		spdlog::error("BR loot zones: {}", last_report);
		return false;
	}

	revision = static_cast<uint32_t>(std::max(0, readInt(doc, "revision", 0)));
	const int file_next = readInt(doc, "nextId", 1);

	TilesByZone file_tiles;
	size_t painted = 0;
	size_t missing = 0;
	for (const json::json& entry : listOf(doc, "zones")) {
		if (!entry.is_object()) {
			continue;
		}
		const int id = readInt(entry, "id", 0);
		if (id <= 0) {
			continue;
		}
		BRLootZone zone;
		zone.id = static_cast<uint32_t>(id);
		zone.tier = std::max(1, readInt(entry, "tier", 1));

		std::vector<Position>& positions = file_tiles[zone.id];
		for (const json::json& row : listOf(entry, "rows")) {
			if (!row.is_array() || row.size() != 4) {
				continue;
			}
			int z = 0, y = 0, x0 = 0, x1 = 0;
			if (!asInt(row[0], z) || !asInt(row[1], y) || !asInt(row[2], x0) || !asInt(row[3], x1)) {
				continue;
			}
			if (x1 < x0 || x1 - x0 > MAX_ROW_WIDTH || z < 0 || z > MAP_MAX_LAYER) {
				continue;
			}
			for (int x = x0; x <= x1; ++x) {
				positions.emplace_back(x, y, z);
				if (Tile* tile = map.getTile(x, y, z)) {
					tile->setBRLootZoneId(zone.id);
					zone.expandBounds(x, y, z);
					++painted;
				} else {
					++missing;
				}
			}
		}
		state.zones[zone.id] = std::move(zone);
	}

	for (const json::json& entry : listOf(doc, "items")) {
		if (!entry.is_object()) {
			continue;
		}
		const auto name = entry.find("name");
		if (name == entry.end() || !name->is_string()) {
			continue;
		}
		BRLootItem item;
		item.pos = Position(readInt(entry, "x", 0), readInt(entry, "y", 0), readInt(entry, "z", 0));
		item.name = name->get<std::string>();
		item.min = std::max(1, readInt(entry, "min", 1));
		item.max = std::max(item.min, readInt(entry, "max", item.min));
		item.chance = std::clamp(readInt(entry, "chance", 100), 1, 100);

		// One item per tile. A second entry for the same tile replaces the first,
		// which is also what the palette does when you click a tile twice.
		const auto same = std::find_if(state.items.begin(), state.items.end(), [&](const BRLootItem& other) {
			return other.pos == item.pos;
		});
		if (same != state.items.end()) {
			*same = std::move(item);
		} else {
			state.items.push_back(std::move(item));
		}
	}

	uint32_t max_id = 0;
	for (const auto& entry : state.zones) {
		max_id = std::max(max_id, entry.first);
	}
	next_id = std::max<uint32_t>(file_next > 0 ? static_cast<uint32_t>(file_next) : 1u, max_id + 1);

	// Canonical text of the FILE's own data (and not of the tiles that exist now):
	// if a row pointed at a tile the map no longer has, the next save drops it, and
	// that is a content change -- the revision has to move.
	loaded_canonical = canonicalText(revision, file_tiles);
	++generation;

	if (missing > 0) {
		last_report = std::format("{} tile(s) of {} are not on the map; the next save drops them", missing, nstr(sidecar.GetFullName()));
		spdlog::warn("BR loot zones: {}", last_report);
	}
	spdlog::info("BR loot zones: {} zone(s), {} tile(s), {} item(s) placed by hand (revision {})", state.zones.size(), painted, state.items.size(), revision);
	return true;
}

bool BRLootZones::saveToFile(const FileName& mapFile) {
	const FileName sidecar = BuildSidecarPath(mapFile);
	if (sidecar.GetFullPath().empty()) {
		return false;
	}
	if (load_failed && sidecar.FileExists()) {
		spdlog::error("BR loot zones: not saving over {}: {}", nstr(sidecar.GetFullPath()), last_report);
		return false;
	}

	// A map that never had loot zones (every BlackTalon map) skips the walk: on a map
	// of millions of tiles it would cost every single save for nothing.
	if (state.zones.empty() && state.items.empty() && !sidecar.FileExists()) {
		return true;
	}

	TilesByZone tiles = collectTiles();

	// A tile can carry an id the table does not know (pasted from another map). It is
	// adopted as tier 1 instead of dropped: losing paint silently is worse than a zone
	// that shows up in the list and can be fixed there.
	size_t adopted = 0;
	for (const auto& entry : tiles) {
		if (state.zones.find(entry.first) == state.zones.end()) {
			BRLootZone zone;
			zone.id = entry.first;
			zone.tier = 1;
			for (const Position& p : entry.second) {
				zone.expandBounds(p.x, p.y, p.z);
			}
			state.zones.emplace(entry.first, std::move(zone));
			++adopted;
		}
		if (entry.first >= next_id) {
			next_id = entry.first + 1;
		}
	}
	if (adopted > 0) {
		++generation;
		spdlog::warn("BR loot zones: {} unknown zone id(s) on the tiles were adopted as tier 1", adopted);
	}

	const bool exists = sidecar.FileExists();
	if (tiles.empty() && state.items.empty() && !exists) {
		// Zones created but nothing painted or placed yet, and no file: nothing to write.
		return true;
	}

	std::string text = canonicalText(revision, tiles);
	if (text == loaded_canonical && text == loaded_disk && exists) {
		return true;
	}
	if (text != loaded_canonical) {
		++revision;
		text = canonicalText(revision, tiles);
	}

	const wxString path = sidecar.GetFullPath();
	if (!writeFileBytes(path, text)) {
		last_report = "Could not write " + nstr(path);
		spdlog::error("BR loot zones: {}", last_report);
		return false;
	}
	loaded_canonical = text;
	loaded_disk = text;
	last_report.clear();
	spdlog::info("BR loot zones: saved {} (revision {})", nstr(path), revision);
	return true;
}
