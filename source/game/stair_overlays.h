#ifndef RME_STAIR_OVERLAYS_H_
#define RME_STAIR_OVERLAYS_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <wx/filename.h>
#include <wx/string.h>

// Direction codes shared with StairDirectionDrawer:
//   0=N, 1=S, 2=E, 3=W, 4=Down (single), 5=UpDouble, 6=DownDouble
class StairOverlayDatabase {
public:
	StairOverlayDatabase() = default;
	~StairOverlayDatabase() = default;

	void clear();

	// Loads an XML file of the form:
	//   <stair_overlays>
	//     <stair id="459" direction="down_double"/>
	//     <stair fromid="1948" toid="1952" direction="up_double"/>
	//   </stair_overlays>
	// Missing file is not an error — returns true with no entries.
	bool load(const wxFileName& filename, wxString& error, std::vector<std::string>& warnings);

	// Returns direction code (0..6) for the given server id, or -1 if not mapped.
	int getDirection(uint16_t serverId) const;

private:
	std::unordered_map<uint16_t, int> id_to_direction;
};

extern StairOverlayDatabase g_stair_overlays;

#endif
