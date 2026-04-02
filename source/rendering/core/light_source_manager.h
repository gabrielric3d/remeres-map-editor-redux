#ifndef RME_LIGHT_SOURCE_MANAGER_H_
#define RME_LIGHT_SOURCE_MANAGER_H_

#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>

struct LightSourceEntry {
	uint16_t clientId = 0;
	std::string label;
	uint8_t r = 255;
	uint8_t g = 200;
	uint8_t b = 0;
};

class LightSourceManager {
public:
	static LightSourceManager& instance();

	void load();
	void save();

	const std::vector<LightSourceEntry>& getEntries() const { return entries; }
	void setEntries(const std::vector<LightSourceEntry>& newEntries);

	// Fast lookup during rendering
	const LightSourceEntry* find(uint16_t clientId) const;
	bool isLightSource(uint16_t clientId) const;

private:
	LightSourceManager() = default;

	void rebuildLookup();

	std::vector<LightSourceEntry> entries;
	std::unordered_map<uint16_t, size_t> lookup; // clientId -> index in entries
};

#endif
