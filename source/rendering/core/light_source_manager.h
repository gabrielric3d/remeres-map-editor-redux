#ifndef RME_LIGHT_SOURCE_MANAGER_H_
#define RME_LIGHT_SOURCE_MANAGER_H_

#include <bitset>
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

	// Um bit por client id em vez de uma sondagem de hash. O caminho de desenho faz
	// esta pergunta uma vez POR ITEM desenhado -- centenas de milhares de vezes por
	// frame numa vista afastada -- e a resposta e "nao" em praticamente todas. O
	// mapa continua existindo para find(), que precisa da entrada inteira.
	bool isLightSource(uint16_t clientId) const {
		return is_light_source[clientId];
	}

private:
	LightSourceManager() = default;

	void rebuildLookup();

	std::vector<LightSourceEntry> entries;
	std::unordered_map<uint16_t, size_t> lookup; // clientId -> index in entries
	std::bitset<65536> is_light_source; // espelho de `lookup`, so para o teste rapido
};

#endif
