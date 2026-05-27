#include "game/stair_overlays.h"
#include "ext/pugixml.hpp"
#include <format>

StairOverlayDatabase g_stair_overlays;

namespace {

int parseDirection(const std::string& value) {
	if (value == "north" || value == "n") return 0;
	if (value == "south" || value == "s") return 1;
	if (value == "east"  || value == "e") return 2;
	if (value == "west"  || value == "w") return 3;
	if (value == "down") return 4;
	if (value == "up_double"   || value == "up2")   return 5;
	if (value == "down_double" || value == "down2") return 6;
	return -1;
}

} // namespace

void StairOverlayDatabase::clear() {
	id_to_direction.clear();
}

bool StairOverlayDatabase::load(const wxFileName& filename, wxString& error, std::vector<std::string>& warnings) {
	clear();

	if (!filename.FileExists()) {
		return true; // Optional file
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(filename.GetFullPath().mb_str());
	if (!result) {
		error = "Couldn't open file \"" + filename.GetFullName() + "\": " + wxString(result.description(), wxConvUTF8);
		return false;
	}

	pugi::xml_node root = doc.child("stair_overlays");
	if (!root) {
		error = "Invalid stair_overlays.xml: missing <stair_overlays> root.";
		return false;
	}

	for (pugi::xml_node node = root.first_child(); node; node = node.next_sibling()) {
		if (std::string(node.name()) != "stair") continue;

		pugi::xml_attribute dirAttr = node.attribute("direction");
		if (!dirAttr) {
			warnings.push_back("stair_overlays.xml: <stair> missing 'direction' attribute.");
			continue;
		}

		int direction = parseDirection(dirAttr.as_string());
		if (direction < 0) {
			warnings.push_back(std::format("stair_overlays.xml: unknown direction \"{}\".", dirAttr.as_string()));
			continue;
		}

		pugi::xml_attribute idAttr = node.attribute("id");
		if (idAttr) {
			int id = idAttr.as_int(-1);
			if (id > 0 && id <= 0xFFFF) {
				id_to_direction[static_cast<uint16_t>(id)] = direction;
			} else {
				warnings.push_back(std::format("stair_overlays.xml: invalid id \"{}\".", idAttr.as_string()));
			}
			continue;
		}

		pugi::xml_attribute fromAttr = node.attribute("fromid");
		pugi::xml_attribute toAttr = node.attribute("toid");
		if (fromAttr && toAttr) {
			int from = fromAttr.as_int(-1);
			int to = toAttr.as_int(-1);
			if (from > 0 && to >= from && to <= 0xFFFF) {
				for (int i = from; i <= to; ++i) {
					id_to_direction[static_cast<uint16_t>(i)] = direction;
				}
			} else {
				warnings.push_back(std::format("stair_overlays.xml: invalid range fromid=\"{}\" toid=\"{}\".", fromAttr.as_string(), toAttr.as_string()));
			}
			continue;
		}

		warnings.push_back("stair_overlays.xml: <stair> needs either 'id' or 'fromid'+'toid'.");
	}

	return true;
}

int StairOverlayDatabase::getDirection(uint16_t serverId) const {
	auto it = id_to_direction.find(serverId);
	return (it != id_to_direction.end()) ? it->second : -1;
}
