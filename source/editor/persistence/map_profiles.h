//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_EDITOR_MAP_PROFILES_H_
#define RME_EDITOR_MAP_PROFILES_H_

#include "editor/persistence/map_load_options.h"

#include <wx/string.h>

#include <vector>

/**
 * A map profile opens several .otbm files together, the way a server that splits its world into
 * many files loads them at boot (e.g. data/world/maps.xml): either as map layers (one tab and one
 * file per map, drawn together; see MapLayers) or as one merged unsaved copy.
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

	// Both open the enabled maps in profile order. A linked profile is re-read first, so
	// profile.maps may change (the caller saves it). load_options.selected_client_id (the
	// start screen's client) is used when that client reads the first map; otherwise the
	// client is picked from the map like File > Open does.

	// Every map as a layer of a new layer group: its own file, undo and Save, one shared tab.
	bool OpenAsLayers(MapProfile& profile, const MapLoadOptions& load_options = {});
	// Every map imported into one new unsaved map (Save asks for a file; sources untouched).
	bool OpenMerged(MapProfile& profile, const MapLoadOptions& load_options = {});
}

#endif // RME_EDITOR_MAP_PROFILES_H_
