#ifndef RME_EDITOR_PERSISTENCE_H
#define RME_EDITOR_PERSISTENCE_H

#include "util/common.h"
#include "app/rme_forward_declarations.h"
#include "editor/persistence/map_load_options.h"
#include "io/iomap.h"
#include "map/position.h"
#include <wx/string.h>
#include <unordered_map>
#include <map>
#include <string>
#include <vector>

class Editor;
class Map;
class Spawn;

// Outcome of a quiet import (map profiles load several maps in a row and report once at the end).
struct MapImportReport {
	uint64_t tiles_imported = 0;
	// Tiles that landed where an earlier map already had a tile (the imported one replaces it).
	uint64_t tiles_overlapped = 0;
	Position first_overlap;
	int tiles_discarded = 0;
	std::vector<std::string> warnings;
	wxString error;
};

class EditorPersistence {
public:
	static void saveMap(Editor& editor, FileName filename, bool showdialog);
	static void loadMap(Editor& editor, const FileName& filename, const MapLoadOptions& load_options = {});
	static bool importMap(Editor& editor, FileName filename, int import_x_offset, int import_y_offset, ImportType house_import_type, ImportType spawn_import_type);
	// importMap without any dialog: the map grows to fit the imported tiles, errors and warnings go
	// to the report, and the load bar (titled load_bar_title) also covers reading the file.
	// The caller refreshes palettes and the view once it is done importing.
	static bool importMapQuiet(Editor& editor, const FileName& filename, int import_x_offset, int import_y_offset, ImportType house_import_type, ImportType spawn_import_type, const wxString& load_bar_title, MapImportReport& report);
	static bool importMiniMap(Editor& editor, FileName filename, int import, int import_x_offset, int import_y_offset, int import_z_offset);

private:
	// report == nullptr is the interactive import of File > Import Map.
	static bool importMapImpl(Editor& editor, const FileName& filename, int import_x_offset, int import_y_offset, ImportType house_import_type, ImportType spawn_import_type, const wxString& load_bar_title, MapImportReport* report);
	static void importTowns(Editor& editor, Map& imported_map, const Position& offset, ImportType house_import_type, std::unordered_map<uint32_t, uint32_t>& town_id_map);
	static void importHouses(Editor& editor, Map& imported_map, const Position& offset, ImportType house_import_type, const std::unordered_map<uint32_t, uint32_t>& town_id_map, std::unordered_map<uint32_t, uint32_t>& house_id_map);
	static void importSpawns(Editor& editor, Map& imported_map, const Position& offset, ImportType spawn_import_type, std::map<Position, std::unique_ptr<Spawn>>& spawn_map);
};

#endif
