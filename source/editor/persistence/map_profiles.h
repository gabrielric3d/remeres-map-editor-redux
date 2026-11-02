//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_EDITOR_MAP_PROFILES_H_
#define RME_EDITOR_MAP_PROFILES_H_

#include <wx/string.h>

#include <vector>

/**
 * A map profile opens several .otbm files as one map, the way a server that splits its world
 * into many files loads them at boot (e.g. data/world/maps.xml). The result is a new unsaved map
 * (Save asks for a file), so the source maps are never overwritten by accident.
 * Profiles are kept in Config::MAP_PROFILES as JSON.
 */
struct MapProfileEntry {
	wxString name;
	wxString file; // absolute path of the .otbm
	int offset_x = 0;
	int offset_y = 0;
	bool load_houses = false;
	bool enabled = true;
};

struct MapProfile {
	wxString name;
	// Server maps.xml the list comes from. When set, the list is read again every time the
	// profile is opened, so maps added to or removed from the server boot need no profile edit.
	wxString maps_xml;
	std::vector<MapProfileEntry> maps;
};

namespace MapProfiles {
	std::vector<MapProfile> Load();
	void Save(const std::vector<MapProfile>& profiles);

	// Reads the <map> entries of a server maps.xml. "file" is resolved like the server does:
	// absolute paths and paths starting with "data/" are relative to the server folder (two levels
	// above data/world), anything else to the maps.xml folder; ".otbm" is added when missing.
	// As on the server, the first map loads houses when no entry asks for them.
	bool ReadServerMapsXml(const wxString& path, std::vector<MapProfileEntry>& maps, wxString& error);

	// Re-reads profile.maps_xml, keeping maps the user unchecked unchecked.
	bool SyncFromServer(MapProfile& profile, wxString& error);

	// maps.xml of the server a map belongs to (searched upwards from the map folder), or "".
	wxString FindServerMapsXml(const wxString& map_path);

	// Imports every enabled map of the profile, in order, into a new map tab and reports the result.
	// A linked profile is re-read first, so profile.maps may change (the caller saves it).
	bool Open(MapProfile& profile);
}

#endif // RME_EDITOR_MAP_PROFILES_H_
