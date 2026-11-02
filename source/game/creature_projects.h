//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_GAME_CREATURE_PROJECTS_H_
#define RME_GAME_CREATURE_PROJECTS_H_

#include <wx/arrstr.h>
#include <wx/string.h>

#include <string>
#include <vector>

/**
 * A creature project ties a folder of maps to the monster/NPC files of its server.
 * Opening any map inside the project's map folder (at any depth) imports those
 * files, so the map's creatures and NPCs are resolved and show up in the palette.
 * Projects are kept in Config::CREATURE_PROJECTS as JSON.
 */
struct CreatureProject {
	wxString name;
	wxString map_folder;
	// Monster/NPC folders (scanned recursively for .xml and .json) or single files.
	// Listed order matters: when a creature appears twice, the later source wins.
	std::vector<wxString> sources;
	// Sub-folder names skipped while scanning the source folders, separated by ',' or ';'.
	wxString exclude_folders = "backup";
	bool enabled = true;
};

struct CreatureImportResult {
	size_t files_scanned = 0;
	size_t files_loaded = 0;
	std::vector<wxString> failed_files;
	std::vector<std::string> warnings;
	size_t creatures_added = 0;
	size_t missing_before = 0;
	size_t missing_after = 0;
};

namespace CreatureProjects {
	std::vector<CreatureProject> Load();
	void Save(const std::vector<CreatureProject>& projects);

	// The enabled project whose map folder holds the map; the deepest folder wins
	// when projects are nested. nullptr when the map belongs to none.
	const CreatureProject* FindForMap(const std::vector<CreatureProject>& projects, const wxString& map_path);

	// Every .xml/.json file the project points to, folders expanded and excluded names skipped.
	wxArrayString CollectFiles(const CreatureProject& project);

	// Imports OT monster/npc XML and creature JSON files into g_creatures behind a load bar.
	CreatureImportResult ImportFiles(const wxArrayString& files, const wxString& load_bar_title);

	// Imports the project's files right away and remembers it as loaded.
	CreatureImportResult LoadProject(const CreatureProject& project);

	// Called when a map is opened: imports the creatures of the project the map belongs to.
	// A project is imported once per creature database (changing the client clears it),
	// so opening more maps of the same project costs nothing.
	void LoadForMap(const wxString& map_path);

	// Multi-line summary of an import, for message boxes.
	wxString Describe(const CreatureImportResult& result);
}

#endif // RME_GAME_CREATURE_PROJECTS_H_
