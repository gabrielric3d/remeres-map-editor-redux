#include "rendering/core/light_source_manager.h"
#include "app/settings.h"
#include <sstream>

LightSourceManager& LightSourceManager::instance() {
	static LightSourceManager mgr;
	return mgr;
}

// Format: "clientId,label,r,g,b;clientId,label,r,g,b;..."
void LightSourceManager::load() {
	std::string data = g_settings.getString(Config::LIGHT_SOURCES_DATA);
	entries.clear();

	if (data.empty()) {
		rebuildLookup();
		return;
	}

	std::istringstream stream(data);
	std::string token;
	while (std::getline(stream, token, ';')) {
		if (token.empty()) continue;

		std::istringstream entry_stream(token);
		std::string part;
		LightSourceEntry entry;

		if (std::getline(entry_stream, part, ',')) entry.clientId = static_cast<uint16_t>(std::stoi(part));
		if (std::getline(entry_stream, part, ',')) entry.label = part;
		if (std::getline(entry_stream, part, ',')) entry.r = static_cast<uint8_t>(std::stoi(part));
		if (std::getline(entry_stream, part, ',')) entry.g = static_cast<uint8_t>(std::stoi(part));
		if (std::getline(entry_stream, part, ',')) entry.b = static_cast<uint8_t>(std::stoi(part));

		if (entry.clientId > 0) {
			entries.push_back(entry);
		}
	}

	rebuildLookup();
}

void LightSourceManager::save() {
	std::ostringstream stream;
	for (size_t i = 0; i < entries.size(); ++i) {
		const auto& e = entries[i];
		if (i > 0) stream << ';';
		stream << e.clientId << ',' << e.label << ',' << (int)e.r << ',' << (int)e.g << ',' << (int)e.b;
	}
	g_settings.setString(Config::LIGHT_SOURCES_DATA, stream.str());
	g_settings.save();
}

void LightSourceManager::setEntries(const std::vector<LightSourceEntry>& newEntries) {
	entries = newEntries;
	rebuildLookup();
}

const LightSourceEntry* LightSourceManager::find(uint16_t clientId) const {
	auto it = lookup.find(clientId);
	if (it != lookup.end()) {
		return &entries[it->second];
	}
	return nullptr;
}

bool LightSourceManager::isLightSource(uint16_t clientId) const {
	return lookup.count(clientId) > 0;
}

void LightSourceManager::rebuildLookup() {
	lookup.clear();
	for (size_t i = 0; i < entries.size(); ++i) {
		lookup[entries[i].clientId] = i;
	}
}
