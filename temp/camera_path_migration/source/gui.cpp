//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include <wx/display.h>
#include <wx/choicdlg.h>
#include <wx/toolbar.h>
#include <wx/generic/msgdlgg.h>

#include "gui.h"
#include "main_menubar.h"

#include "editor.h"
#include "brush.h"
#include "map.h"
#include "sprites.h"
#include "materials.h"
#include "doodad_brush.h"
#include "spawn_brush.h"
#include "theme.h"

#include "common_windows.h"
#include "result_window.h"
#include "duplicated_items_window.h"
#include "minimap_window.h"
#include "palette_window.h"
#include "palette_camera_paths.h"
#include "browse_tile_window.h"
#include "map_display.h"
#include "application.h"
#include "welcome_dialog.h"
#include "actions_history_window.h"
#include "recent_brushes_window.h"

#include "live_client.h"
#include "live_tab.h"
#include "live_server.h"
#include "sprite_cache.h"
#include "filehandle.h"
#include "camera_path.h"
#include "area_decoration_dialog.h"
#include "area_creature_spawn_dialog.h"
#include "brush_manager_panel.h"

#include <algorithm>

#ifdef __WXOSX__
#include <AGL/agl.h>
#endif

const wxEventType EVT_UPDATE_MENUS = wxNewEventType();
const wxEventType EVT_UPDATE_ACTIONS = wxNewEventType();

namespace {

constexpr size_t kMaxRecentBrushesPerCategory = 24;

TilesetCategoryType GetRecentBrushCategory(const Brush* brush)
{
	if(!brush) {
		return TILESET_UNKNOWN;
	}

	if(brush->isRaw())
		return TILESET_RAW;
	if(brush->isDoodad())
		return TILESET_DOODAD;
	if(brush->isCreature() || brush->isSpawn())
		return TILESET_CREATURE;
	if(brush->isHouse() || brush->isHouseExit())
		return TILESET_HOUSE;
	if(brush->isWaypoint())
		return TILESET_WAYPOINT;
	if(brush->isCameraPath())
		return TILESET_CAMERA_PATH;
	if(brush->isTerrain() || brush->isGround() || brush->isWall() || brush->isDoor() || brush->isOptionalBorder() ||
		brush->isFlag() || brush->isCarpet() || brush->isTable() || brush->isEraser())
		return TILESET_TERRAIN;

	return TILESET_ITEM;
}

}

// Global GUI instance
GUI g_gui;

// GUI class implementation
GUI::GUI() :
	aui_manager(nullptr),
	root(nullptr),
	minimap(nullptr),
	gem(nullptr),
	search_result_window(nullptr),
	duplicated_items_window(nullptr),
	actions_history_window(nullptr),
	recent_brushes_window(nullptr),
	browse_field_notebook(nullptr),
	brush_manager_panel(nullptr),
	secondary_map(nullptr),
	doodad_buffer_map(nullptr),

	house_brush(nullptr),
	house_exit_brush(nullptr),
	waypoint_brush(nullptr),
	camera_path_brush(nullptr),
	npc_path_brush(nullptr),
	optional_brush(nullptr),
	eraser(nullptr),
	normal_door_brush(nullptr),
	locked_door_brush(nullptr),
	magic_door_brush(nullptr),
	quest_door_brush(nullptr),
	hatch_door_brush(nullptr),
	window_door_brush(nullptr),

	area_decoration_dialog(nullptr),
	area_creature_spawn_dialog(nullptr),

	OGLContext(nullptr),
	loaded_version(CLIENT_VERSION_NONE),
	mode(SELECTION_MODE),
	pasting(false),
	keep_pasting(false),
	hotkeys_enabled(true),

	current_brush(nullptr),
	previous_brush(nullptr),
	brush_shape(BRUSHSHAPE_SQUARE),
	brush_size(0),
	brush_variation(0),

	creature_spawntime(0),
	use_custom_thickness(false),
	custom_thickness_mod(0.0),
	progressBar(nullptr),
	disabled_counter(0)
{
	doodad_buffer_map = newd BaseMap();
}

GUI::~GUI()
{
	for(auto& view_pair : detached_views) {
		for(wxFrame* frame : view_pair.second) {
			frame->Close(true);
		}
	}
	detached_views.clear();

	for(auto& view_pair : dockable_views) {
		for(MapWindow* window : view_pair.second) {
			if(aui_manager && aui_manager->GetPane(window).IsOk()) {
				aui_manager->DetachPane(window);
				window->Destroy();
			}
		}
	}
	dockable_views.clear();

	// Destroy persistent dialogs
	if (area_decoration_dialog) {
		area_decoration_dialog->Destroy();
		area_decoration_dialog = nullptr;
	}
	if (area_creature_spawn_dialog) {
		area_creature_spawn_dialog->Destroy();
		area_creature_spawn_dialog = nullptr;
	}

	delete doodad_buffer_map;
	delete g_gui.aui_manager;
	delete OGLContext;
}

wxGLContext* GUI::GetGLContext(wxGLCanvas* win)
{
	if(OGLContext == nullptr) {
#ifdef __WXOSX__
        /*
        wxGLContext(AGLPixelFormat fmt, wxGLCanvas *win,
                    const wxPalette& WXUNUSED(palette),
                    const wxGLContext *other
                    );
        */
		OGLContext = new wxGLContext(win, nullptr);
#else
		OGLContext = newd wxGLContext(win);
#endif
    }

	return OGLContext;
}

wxString GUI::GetDataDirectory()
{
	std::string cfg_str = g_settings.getString(Config::DATA_DIRECTORY);
	if(!cfg_str.empty()) {
		FileName dir;
		dir.Assign(wxstr(cfg_str));
		wxString path;
		if(dir.DirExists()) {
			path = dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
			return path;
		}
	}

	// Silently reset directory
	FileName exec_directory;
	try
	{
		exec_directory = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
	}
	catch(const std::bad_cast&)
	{
		throw; // Crash application (this should never happend anyways...)
	}

	exec_directory.AppendDir("data");
	return exec_directory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

wxString GUI::GetExecDirectory()
{
	// Silently reset directory
	FileName exec_directory;
	try
	{
		exec_directory = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
	}
	catch(const std::bad_cast&)
	{
		wxLogError("Could not fetch executable directory.");
	}
	return exec_directory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

wxString GUI::GetLocalDataDirectory()
{
	if(g_settings.getInteger(Config::INDIRECTORY_INSTALLATION)) {
		FileName dir = GetDataDirectory();
		dir.AppendDir("user");
		dir.AppendDir("data");
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);;
	} else {
		FileName dir = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetUserDataDir();
#ifdef __WINDOWS__
		dir.AppendDir("Remere's Map Editor");
#else
		dir.AppendDir(".rme");
#endif
		dir.AppendDir("data");
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	}
}

wxString GUI::GetLocalDirectory()
{
	if(g_settings.getInteger(Config::INDIRECTORY_INSTALLATION)) {
		FileName dir = GetDataDirectory();
		dir.AppendDir("user");
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);;
	} else {
		FileName dir = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetUserDataDir();
#ifdef __WINDOWS__
		dir.AppendDir("Remere's Map Editor");
#else
		dir.AppendDir(".rme");
#endif
		dir.Mkdir(0755, wxPATH_MKDIR_FULL);
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	}
}

wxString GUI::GetExtensionsDirectory()
{
	std::string cfg_str = g_settings.getString(Config::EXTENSIONS_DIRECTORY);
	if(!cfg_str.empty()) {
		FileName dir;
		dir.Assign(wxstr(cfg_str));
		wxString path;
		if(dir.DirExists()) {
			path = dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
			return path;
		}
	}

	// Silently reset directory
	FileName local_directory = GetLocalDirectory();
	local_directory.AppendDir("extensions");
	local_directory.Mkdir(0755, wxPATH_MKDIR_FULL);
	return local_directory.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

void GUI::discoverDataDirectory(const wxString& existentFile)
{
	wxString currentDir = wxGetCwd();
	wxString execDir = GetExecDirectory();

	wxString possiblePaths[] = {
		execDir,
		currentDir + "/",

		// these are used usually when running from build directories
		execDir + "/../",
		execDir + "/../../",
		execDir + "/../../../",
		currentDir + "/../",
	};

	bool found = false;
	for(const wxString& path : possiblePaths) {
		if(wxFileName(path + "data/" + existentFile).FileExists()) {
			m_dataDirectory = path + "data/";
			found = true;
			break;
		}
	}

	if(!found)
		wxLogError(wxString() + "Could not find data directory.\n");
}

bool GUI::LoadVersion(ClientVersionID version, wxString& error, wxArrayString& warnings, bool force)
{
	if(ClientVersion::get(version) == nullptr) {
		error = "Unsupported client version! (8)";
		return false;
	}

	if(version != loaded_version || force) {
		if(getLoadedVersion() != nullptr)
			// There is another version loaded right now, save window layout
			g_gui.SavePerspective();

		// Disable all rendering so the data is not accessed while reloading
		UnnamedRenderingLock();
		DestroyPalettes();
		DestroyMinimap();

		// Destroy the previous version
		UnloadVersion();

		loaded_version = version;
		if(!getLoadedVersion()->hasValidPaths()) {
			if(!getLoadedVersion()->loadValidPaths()) {
				error = "Couldn't load relevant asset files";
				loaded_version = CLIENT_VERSION_NONE;
				return false;
			}
		}

		bool ret = LoadDataFiles(error, warnings);
		if(ret) {
			g_gui.LoadPerspective();
			g_gui.SetStartupWarnings(warnings);
		}
		else
			loaded_version = CLIENT_VERSION_NONE;

		return ret;
	}
	return true;
}

void GUI::EnableHotkeys()
{
	hotkeys_enabled = true;
	if(root && root->menu_bar) {
		root->menu_bar->SetAcceleratorsEnabled(true);
	}
}

void GUI::DisableHotkeys()
{
	hotkeys_enabled = false;
	if(root && root->menu_bar) {
		root->menu_bar->SetAcceleratorsEnabled(false);
	}
}

bool GUI::AreHotkeysEnabled() const
{
	return hotkeys_enabled;
}

ClientVersionID GUI::GetCurrentVersionID() const
{
	if(loaded_version != CLIENT_VERSION_NONE) {
		return getLoadedVersion()->getID();
	}
	return CLIENT_VERSION_NONE;
}

const ClientVersion& GUI::GetCurrentVersion() const
{
	assert(loaded_version);
	return *getLoadedVersion();
}

void GUI::CycleTab(bool forward)
{
	tabbook->CycleTab(forward);
}

bool GUI::LoadDataFiles(wxString& error, wxArrayString& warnings)
{
	FileName data_path = getLoadedVersion()->getDataPath();
	FileName items_path = getLoadedVersion()->getItemsDataPath();
	FileName client_path = getLoadedVersion()->getClientPath();
	FileName extension_path = GetExtensionsDirectory();

	FileName exec_directory;
	try
	{
		exec_directory = dynamic_cast<wxStandardPaths&>(wxStandardPaths::Get()).GetExecutablePath();
	}
	catch(std::bad_cast&)
	{
		error = "Couldn't establish working directory...";
		return false;
	}

	g_gui.gfx.client_version = getLoadedVersion();

	if(!g_gui.gfx.loadOTFI(client_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR), error, warnings)) {
		error = "Couldn't load otfi file: " + error;
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.CreateLoadBar("Loading asset files");
	GUI::LoadingStats stats;
	g_gui.SetLoadDone(0, "Loading metadata file...");

	wxFileName metadata_path = g_gui.gfx.getMetadataFileName();
	if(!g_gui.gfx.loadSpriteMetadata(metadata_path, error, warnings)) {
		error = "Couldn't load metadata: " + error;
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.SetLoadDone(10, "Loading sprites file...");

	wxFileName sprites_path = g_gui.gfx.getSpritesFileName();

	// Update stats with sprite count
	stats.current_file = sprites_path.GetFullName();
	g_gui.SetLoadingStats(stats);

	// Try to load from disk cache if enabled
	bool loaded_from_cache = false;
	bool use_disk_cache = g_settings.getBoolean(Config::USE_DISK_SPRITE_CACHE) &&
	                      g_settings.getBoolean(Config::USE_MEMCACHED_SPRITES);
	std::string cache_path = g_gui.gfx.getSpriteCachePath();

	if(use_disk_cache && !cache_path.empty()) {
		SpriteCache cache_validator;
		std::string spr_path_str = nstr(sprites_path.GetFullPath());
		std::string dat_path_str = nstr(metadata_path.GetFullPath());

		// We need to read signatures from the files first
		FileReadHandle spr_fh(spr_path_str);
		FileReadHandle dat_fh(dat_path_str);
		uint32_t spr_sig = 0, dat_sig = 0;
		if(spr_fh.isOk()) spr_fh.getU32(spr_sig);
		if(dat_fh.isOk()) dat_fh.getU32(dat_sig);

		// Use the is_extended and has_transparency flags that were determined during loadSpriteMetadata
		if(cache_validator.isValidCache(cache_path, spr_path_str, dat_path_str, spr_sig, dat_sig,
		                                 g_gui.gfx.isExtended(), g_gui.gfx.hasTransparency())) {
			g_gui.SetLoadDone(10, "Loading sprites from cache...");
			if(g_gui.gfx.loadSpriteDataFromCache(cache_path, error)) {
				loaded_from_cache = true;
			} else {
				// Cache load failed, will fall back to normal loading
				error.Clear();
			}
		}
	}

	if(!loaded_from_cache) {
		if(!g_gui.gfx.loadSpriteData(sprites_path.GetFullPath(), error, warnings)) {
			error = "Couldn't load sprites: " + error;
			g_gui.DestroyLoadBar();
			UnloadVersion();
			return false;
		}

		// Save to cache if enabled and we loaded sprites into memory
		if(use_disk_cache && !cache_path.empty() && g_settings.getBoolean(Config::USE_MEMCACHED_SPRITES)) {
			g_gui.SetLoadDone(19, "Saving sprite cache...");
			g_gui.gfx.saveSpriteDataToCache(cache_path, sprites_path, metadata_path);
		}
	}

	g_gui.SetLoadDone(20, "Loading items.otb file...");
	stats.current_file = "items.otb";
	stats.total_sprites = g_gui.gfx.getItemSpriteMaxID(); // Get sprite count
	g_gui.SetLoadingStats(stats);
	if(!g_items.loadFromOtb(wxString(items_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + "items.otb"), error, warnings)) {
		error = "Couldn't load items.otb: " + error;
		g_gui.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_gui.SetLoadDone(30, "Loading items.xml ...");
	stats.current_file = "items.xml";
	stats.total_items = g_items.getMaxID(); // Use maxItemId from items database
	g_gui.SetLoadingStats(stats);
	if(!g_items.loadFromGameXml(wxString(items_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + "items.xml"), error, warnings)) {
		warnings.push_back("Couldn't load items.xml: " + error);
	}

	g_gui.SetLoadDone(45, "Loading creatures.xml ...");
	stats.current_file = "creatures.xml";
	g_gui.SetLoadingStats(stats);
	if(!g_creatures.loadFromXML(wxString(data_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + "creatures.xml"), true, error, warnings)) {
		warnings.push_back("Couldn't load creatures.xml: " + error);
	}

	g_gui.SetLoadDone(45, "Loading user creatures.xml ...");
	// Count creatures by iterating the map
	uint32_t creature_count = 0;
	for(auto it = g_creatures.begin(); it != g_creatures.end(); ++it) {
		creature_count++;
	}
	stats.total_creatures = creature_count;
	g_gui.SetLoadingStats(stats);
	{
		FileName cdb = getLoadedVersion()->getLocalDataPath();
		cdb.SetFullName("creatures.xml");
		wxString nerr;
		wxArrayString nwarn;
		g_creatures.loadFromXML(cdb, false, nerr, nwarn);
	}

	// Load creature behaviors (wander radius, etc.) after all creatures are loaded
	{
		wxString bhvErr;
		wxArrayString bhvWarn;
		g_creatures.loadBehaviors(g_gui.GetDataDirectory() + "creature_behaviors.xml", bhvErr, bhvWarn);
		// Silently ignore if file doesn't exist yet (first run)
	}

	g_gui.SetLoadDone(50, "Loading materials.xml ...");
	stats.current_file = "materials.xml";
	g_gui.SetLoadingStats(stats);
	if(!g_materials.loadMaterials(wxString(data_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + "materials.xml"), error, warnings)) {
		warnings.push_back("Couldn't load materials.xml: " + error);
	}

	g_gui.SetLoadDone(70, "Loading extensions...");
	stats.current_file = "extensions";
	g_gui.SetLoadingStats(stats);
	if(!g_materials.loadExtensions(extension_path, error, warnings)) {
		//warnings.push_back("Couldn't load extensions: " + error);
	}

	g_gui.SetLoadDone(70, "Finishing...");
	g_brushes.init();
	g_materials.createOtherTileset();

	g_gui.DestroyLoadBar();
	return true;
}

bool GUI::ReloadBrushes(wxString& error, wxArrayString& warnings)
{
	if(loaded_version == CLIENT_VERSION_NONE) {
		error = "No version loaded";
		return false;
	}

	// Disable rendering while reloading
	UnnamedRenderingLock();

	// Save and destroy palettes
	SavePerspective();
	DestroyPalettes();

	// Clear only brushes and materials (not items, sprites, or creatures)
	current_brush = nullptr;
	previous_brush = nullptr;
	house_brush = nullptr;
	house_exit_brush = nullptr;
	waypoint_brush = nullptr;
	camera_path_brush = nullptr;
	npc_path_brush = nullptr;
	optional_brush = nullptr;
	eraser = nullptr;
	normal_door_brush = nullptr;
	locked_door_brush = nullptr;
	magic_door_brush = nullptr;
	quest_door_brush = nullptr;
	hatch_door_brush = nullptr;
	window_door_brush = nullptr;

	// Clear materials and brushes
	g_materials.clear();
	g_brushes.clear();

	// Reset item brush references (but keep items loaded)
	for(int32_t id = 0; id <= g_items.getMaxID(); ++id) {
		ItemType* type = g_items.getRawItemType(id);
		if(type) {
			type->brush = nullptr;
			type->doodad_brush = nullptr;
			type->raw_brush = nullptr;
			type->has_raw = false;
			type->in_other_tileset = false;
			// Reset border/brush flags that are set during brush loading
			type->isBorder = false;
			type->isOptionalBorder = false;
			type->isWall = false;
			type->isBrushDoor = false;
			type->isTable = false;
			type->isCarpet = false;
			type->ground_equivalent = 0;
			type->border_group = 0;
			type->has_equivalent = false;
			type->wall_hate_me = false;
			type->border_alignment = BORDER_NONE;
		}
	}

	// Reset creature brush references
	for(CreatureMap::iterator iter = g_creatures.begin(); iter != g_creatures.end(); ++iter) {
		CreatureType* type = iter->second;
		type->brush = nullptr;
		type->in_other_tileset = false;
	}

	// Reload materials.xml and all included files
	FileName data_path = getLoadedVersion()->getDataPath();
	if(!g_materials.loadMaterials(wxString(data_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + "materials.xml"), error, warnings)) {
		warnings.push_back("Couldn't reload materials.xml: " + error);
	}

	// Reload extensions
	FileName extension_path = GetExtensionsDirectory();
	if(!g_materials.loadExtensions(extension_path, error, warnings)) {
		// Extensions are optional
	}

	// Reinitialize brushes and create Other tileset
	g_brushes.init();
	g_materials.createOtherTileset();

	// Restore palettes
	LoadPerspective();

	// Refresh view
	RefreshView();

	return true;
}

void GUI::UnloadVersion()
{
	UnnamedRenderingLock();
	gfx.clear();
	current_brush = nullptr;
	previous_brush = nullptr;

	house_brush = nullptr;
	house_exit_brush = nullptr;
	waypoint_brush = nullptr;
	camera_path_brush = nullptr;
	npc_path_brush = nullptr;
	optional_brush = nullptr;
	eraser = nullptr;
	normal_door_brush = nullptr;
	locked_door_brush = nullptr;
	magic_door_brush = nullptr;
	quest_door_brush = nullptr;
	hatch_door_brush = nullptr;
	window_door_brush = nullptr;

	if(loaded_version != CLIENT_VERSION_NONE) {
		for(auto it = detached_views.begin(); it != detached_views.end();) {
			auto editor_it = it;
			++it;
			CloseDetachedViews(editor_it->first);
		}

		//g_gui.UnloadVersion();
		g_materials.clear();
		g_brushes.clear();
		g_items.clear();
		gfx.clear();

		FileName cdb = getLoadedVersion()->getLocalDataPath();
		cdb.SetFullName("creatures.xml");
		g_creatures.saveToXML(cdb);
		g_creatures.clear();

		loaded_version = CLIENT_VERSION_NONE;
	}
}

void GUI::SaveCurrentMap(FileName filename, bool showdialog)
{
	MapTab* mapTab = GetCurrentMapTab();
	if(mapTab) {
		Editor* editor = mapTab->GetEditor();
		if(editor) {
			editor->saveMap(filename, showdialog);

			const std::string& filename = editor->getMap().getFilename();
			const Position& position = mapTab->GetScreenCenterPosition();
			std::ostringstream stream;
			stream << position;
			g_settings.setString(Config::RECENT_EDITED_MAP_PATH, filename);
			g_settings.setString(Config::RECENT_EDITED_MAP_POSITION, stream.str());
		}
	}

	UpdateTitle();
	root->UpdateMenubar();
	root->Refresh();
}

bool GUI::IsEditorOpen() const
{
	return tabbook != nullptr && GetCurrentMapTab();
}

double GUI::GetCurrentZoom()
{
	MapTab* tab = GetCurrentMapTab();
	if(tab)
		return tab->GetCanvas()->GetZoom();
	return 1.0;
}

void GUI::SetCurrentZoom(double zoom)
{
	MapTab* tab = GetCurrentMapTab();
	if(tab)
		tab->GetCanvas()->SetZoom(zoom);
}

void GUI::FitViewToMap()
{
	for(int index = 0; index < tabbook->GetTabCount(); ++index) {
		if(auto *tab = dynamic_cast<MapTab*>(tabbook->GetTab(index))) {
			tab->GetView()->FitToMap();
		}
	}
}

void GUI::FitViewToMap(MapTab* mt)
{
	for(int index = 0; index < tabbook->GetTabCount(); ++index) {
		if(auto *tab = dynamic_cast<MapTab*>(tabbook->GetTab(index))) {
			if(tab->HasSameReference(mt)) {
				tab->GetView()->FitToMap();
			}
		}
	}
}

bool GUI::NewMap()
{
    FinishWelcomeDialog();

	Editor* editor;
	try
	{
		editor = newd Editor(copybuffer);
	}
	catch(std::runtime_error& e)
	{
		PopupDialog(root, "Error!", wxString(e.what(), wxConvUTF8), wxOK);
		return false;
	}

	auto *mapTab = newd MapTab(tabbook, editor);
	mapTab->OnSwitchEditorMode(mode);
    editor->clearChanges();

	SetStatusText("Created new map");
	UpdateTitle();
	RefreshPalettes();
	root->UpdateMenubar();
	root->Refresh();
	return true;
}

void GUI::OpenMap()
{
	wxString wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? MAP_LOAD_FILE_WILDCARD_OTGZ : MAP_LOAD_FILE_WILDCARD;
	wxFileDialog dialog(root, "Open map file", wxEmptyString, wxEmptyString, wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if(dialog.ShowModal() == wxID_OK)
		LoadMap(dialog.GetPath());
}

void GUI::SaveMap()
{
	if(!IsEditorOpen())
		return;

	if(GetCurrentMap().hasFile()) {
		SaveCurrentMap(true);
	} else {
		wxString wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? MAP_SAVE_FILE_WILDCARD_OTGZ : MAP_SAVE_FILE_WILDCARD;
		wxFileDialog dialog(root, "Save...", wxEmptyString, wxEmptyString, wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

		if(dialog.ShowModal() == wxID_OK)
			SaveCurrentMap(dialog.GetPath(), true);
	}
}

void GUI::SaveMapAs()
{
	if(!IsEditorOpen())
		return;

	wxString wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ? MAP_SAVE_FILE_WILDCARD_OTGZ : MAP_SAVE_FILE_WILDCARD;
	wxFileDialog dialog(root, "Save As...", "", "", wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if(dialog.ShowModal() == wxID_OK) {
		SaveCurrentMap(dialog.GetPath(), true);
		UpdateTitle();
		root->menu_bar->AddRecentFile(dialog.GetPath());
		root->UpdateMenubar();
	}
}

bool GUI::LoadMap(const FileName& fileName)
{
    FinishWelcomeDialog();

	if(GetCurrentEditor() && !GetCurrentMap().hasChanged() && !GetCurrentMap().hasFile())
		g_gui.CloseCurrentEditor();

	Editor* editor;
	try
	{
		editor = newd Editor(copybuffer, fileName);
	}
	catch(std::runtime_error& e)
	{
		PopupDialog(root, "Error!", wxString(e.what(), wxConvUTF8), wxOK);
		return false;
	}

	auto *mapTab = newd MapTab(tabbook, editor);
	mapTab->OnSwitchEditorMode(mode);

	root->AddRecentFile(fileName);

	mapTab->GetView()->FitToMap();
	UpdateTitle();
	ListDialog("Map loader errors", mapTab->GetMap()->getWarnings());
	root->DoQueryImportCreatures();

	FitViewToMap(mapTab);
	root->UpdateMenubar();

	std::string path = g_settings.getString(Config::RECENT_EDITED_MAP_PATH);
	if(!path.empty()) {
		FileName file(path);
		if(file == fileName) {
			std::istringstream stream(g_settings.getString(Config::RECENT_EDITED_MAP_POSITION));
			Position position;
			stream >> position;
			mapTab->SetScreenCenterPosition(position);
		}
	}
	return true;
}

Editor* GUI::GetCurrentEditor()
{
	MapTab* mapTab = GetCurrentMapTab();
	if(mapTab)
		return mapTab->GetEditor();
	return nullptr;
}

EditorTab* GUI::GetTab(int idx)
{
	return tabbook->GetTab(idx);
}

int GUI::GetTabCount() const
{
	return tabbook->GetTabCount();
}

EditorTab* GUI::GetCurrentTab()
{
	return tabbook->GetCurrentTab();
}

MapTab* GUI::GetCurrentMapTab() const
{
	if(tabbook && tabbook->GetTabCount() > 0) {
		EditorTab* editorTab = tabbook->GetCurrentTab();
		auto *mapTab = dynamic_cast<MapTab*>(editorTab);
		return mapTab;
	}
	return nullptr;
}

Map& GUI::GetCurrentMap()
{
	Editor* editor = GetCurrentEditor();
	ASSERT(editor);
	return editor->getMap();
}

int GUI::GetOpenMapCount()
{
	std::set<Map*> open_maps;

	for(int i = 0; i < tabbook->GetTabCount(); ++i) {
		auto *tab = dynamic_cast<MapTab*>(tabbook->GetTab(i));
		if(tab)
			open_maps.insert(open_maps.begin(), tab->GetMap());

	}

	return static_cast<int>(open_maps.size());
}

bool GUI::ShouldSave()
{
	Editor* editor = GetCurrentEditor();
	ASSERT(editor);
	return editor->hasChanges();
}

void GUI::AddPendingCanvasEvent(wxEvent& event)
{
	MapTab* mapTab = GetCurrentMapTab();
	if(mapTab)
		mapTab->GetCanvas()->GetEventHandler()->AddPendingEvent(event);
}

void GUI::CloseCurrentEditor()
{
	MapTab* mapTab = GetCurrentMapTab();
	if(mapTab) {
		if(HasDetachedViews(mapTab->GetEditor())) {
			wxString message = "This map has one or more detached views open.\n";
			message += "You must close all detached views before closing the map.";

			int choice = wxMessageBox(
				message,
				"Detached Views Open",
				wxOK | wxCANCEL | wxICON_EXCLAMATION
			);

			if(choice == wxOK) {
				CloseDetachedViews(mapTab->GetEditor());
			} else {
				return;
			}
		}
	}

	RefreshPalettes();
	tabbook->DeleteTab(tabbook->GetSelection());
	root->UpdateMenubar();

	if(duplicated_items_window) {
		duplicated_items_window->Clear();
	}
	if(browse_field_notebook && browse_field_notebook->GetBrowseTilePanel()) {
		browse_field_notebook->GetBrowseTilePanel()->ClearSelection();
	}

	// Hide Area Decoration dialog if no editor is open
	if (!IsEditorOpen() && area_decoration_dialog) {
		area_decoration_dialog->Hide();
	}
	if (!IsEditorOpen() && area_creature_spawn_dialog) {
		area_creature_spawn_dialog->Hide();
	}
}

bool GUI::CloseLiveEditors(LiveSocket* sock)
{
	for(int i = 0; i < tabbook->GetTabCount(); ++i) {
		auto *mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(i));
		if(mapTab) {
			Editor* editor = mapTab->GetEditor();
			if(editor->GetLiveClient() == sock)
				tabbook->DeleteTab(i--);
		}
		auto *liveLogTab = dynamic_cast<LiveLogTab*>(tabbook->GetTab(i));
		if(liveLogTab) {
			if(liveLogTab->GetSocket() == sock) {
				liveLogTab->Disconnect();
				tabbook->DeleteTab(i--);
			}
		}
	}
	if(browse_field_notebook && browse_field_notebook->GetBrowseTilePanel()) {
		browse_field_notebook->GetBrowseTilePanel()->ClearSelection();
	}
	root->UpdateMenubar();
	return true;
}


bool GUI::CloseAllEditors()
{
	for(int i = 0; i < tabbook->GetTabCount(); ++i) {
		auto *mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(i));
		if(mapTab) {
			if(HasDetachedViews(mapTab->GetEditor())) {
				tabbook->SetFocusedTab(i);

				wxString message = "This map has one or more detached views open.\n";
				message += "You must close all detached views before closing the map.";

				int choice = wxMessageBox(
					message,
					"Detached Views Open",
					wxOK | wxCANCEL | wxICON_EXCLAMATION
				);

				if(choice == wxOK) {
					CloseDetachedViews(mapTab->GetEditor());
				} else {
					return false;
				}
			}

			if(mapTab->IsUniqueReference() && mapTab->GetMap() && mapTab->GetMap()->hasChanged()) {
				tabbook->SetFocusedTab(i);
				if(!root->DoQuerySave(false)) {
					return false;
				} else {
					RefreshPalettes();
					tabbook->DeleteTab(i--);
				}
			} else {
				tabbook->DeleteTab(i--);
			}
		}
	}

	if(root)
		root->UpdateMenubar();

	if(duplicated_items_window) {
		duplicated_items_window->Clear();
	}
	if(browse_field_notebook && browse_field_notebook->GetBrowseTilePanel()) {
		browse_field_notebook->GetBrowseTilePanel()->ClearSelection();
	}

	// Hide Area Decoration dialog when all editors are closed
	if (area_decoration_dialog) {
		area_decoration_dialog->Hide();
	}
	if (area_creature_spawn_dialog) {
		area_creature_spawn_dialog->Hide();
	}

	return true;
}

void GUI::NewMapView()
{
	MapTab* mapTab = GetCurrentMapTab();
	if(mapTab) {
		auto *newMapTab = newd MapTab(mapTab);
		newMapTab->OnSwitchEditorMode(mode);

		SetStatusText("Created new view");
		UpdateTitle();
		RefreshPalettes();
		root->UpdateMenubar();
		root->Refresh();
	}
}

void GUI::NewDetachedMapView()
{
	MapTab* mapTab = GetCurrentMapTab();
	if(!mapTab) {
		return;
	}

	wxArrayString choices;
	choices.Add("Detached Window (Can be moved to another monitor)");
	choices.Add("Always-on-top Window (Will stay on top of other windows)");
	choices.Add("Dockable Panel (Can be attached like palette/minimap)");

	wxSingleChoiceDialog dialog(root, "Select type of view:", "Map View Options", choices);
	if(dialog.ShowModal() != wxID_OK) {
		return;
	}

	const int selection = dialog.GetSelection();
	if(selection == 0 || selection == 1) {
		wxFrame* detachedFrame = newd wxFrame(
			root,
			wxID_ANY,
			"Detached Map View",
			wxDefaultPosition,
			wxSize(800, 600),
			wxDEFAULT_FRAME_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX | wxMINIMIZE_BOX
		);

		MapWindow* newMapWindow = newd MapWindow(detachedFrame, *mapTab->GetEditor());
		wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);
		sizer->Add(newMapWindow, 1, wxEXPAND);
		detachedFrame->SetSizer(sizer);

		newMapWindow->FitToMap();
		newMapWindow->SetScreenCenterPosition(mapTab->GetScreenCenterPosition());

		if(mode == SELECTION_MODE) {
			newMapWindow->GetCanvas()->EnterSelectionMode();
		} else {
			newMapWindow->GetCanvas()->EnterDrawingMode();
		}

		wxToolBar* toolbar = new wxToolBar(detachedFrame, wxID_ANY);
		wxButton* syncButton = new wxButton(toolbar, wxID_ANY, "Sync View");
		wxCheckBox* pinCheckbox = new wxCheckBox(toolbar, wxID_ANY, "Keep on Top");
		wxCheckBox* keepOpenCheckbox = new wxCheckBox(toolbar, wxID_ANY, "Keep Open");
		toolbar->AddControl(syncButton);
		toolbar->AddSeparator();
		toolbar->AddControl(pinCheckbox);
		toolbar->AddSeparator();
		toolbar->AddControl(keepOpenCheckbox);
		toolbar->Realize();
		sizer->Insert(0, toolbar, 0, wxEXPAND);

		syncButton->Bind(wxEVT_BUTTON, [this, newMapWindow](wxCommandEvent& WXUNUSED(event)) {
			MapTab* currentTab = GetCurrentMapTab();
			if(currentTab) {
				newMapWindow->SetScreenCenterPosition(currentTab->GetScreenCenterPosition());
			}
		});

		pinCheckbox->Bind(wxEVT_CHECKBOX, [detachedFrame](wxCommandEvent& event) {
			bool checked = event.IsChecked();
			if(checked) {
				detachedFrame->SetWindowStyleFlag(detachedFrame->GetWindowStyleFlag() | wxSTAY_ON_TOP);
			} else {
				detachedFrame->SetWindowStyleFlag(detachedFrame->GetWindowStyleFlag() & ~wxSTAY_ON_TOP);
			}
			detachedFrame->Refresh();
		});

		struct WindowData : public wxClientData {
			bool keepOpen = false;
		};
		WindowData* windowData = new WindowData();
		detachedFrame->SetClientObject(windowData);

		detachedFrame->Bind(wxEVT_CLOSE_WINDOW, [detachedFrame](wxCloseEvent& event) {
			WindowData* data = static_cast<WindowData*>(detachedFrame->GetClientObject());
			if(data && data->keepOpen && event.CanVeto()) {
				event.Veto();
				detachedFrame->Iconize(true);
			} else {
				detachedFrame->Destroy();
			}
		});

		keepOpenCheckbox->Bind(wxEVT_CHECKBOX, [detachedFrame](wxCommandEvent& event) {
			wxCheckBox* cb = static_cast<wxCheckBox*>(event.GetEventObject());
			WindowData* data = static_cast<WindowData*>(detachedFrame->GetClientObject());
			if(data) {
				data->keepOpen = cb->GetValue();
			}
		});

		newMapWindow->Bind(wxEVT_RIGHT_DOWN, [newMapWindow](wxMouseEvent& WXUNUSED(event)) {
			wxMenu popupMenu;
			wxMenu* floorMenu = new wxMenu();
			for(int floor = 0; floor <= 15; ++floor) {
				wxMenuItem* floorItem = floorMenu->Append(wxID_ANY, wxString::Format("Floor %d", floor));
				floorMenu->Bind(wxEVT_COMMAND_MENU_SELECTED, [newMapWindow, floor](wxCommandEvent& WXUNUSED(event)) {
					newMapWindow->GetCanvas()->ChangeFloor(floor);
				}, floorItem->GetId());
			}
			popupMenu.Append(wxID_ANY, "Go to Floor", floorMenu);
			newMapWindow->PopupMenu(&popupMenu);
		});

		detachedFrame->SetTitle(wxString::Format("Detached View: %s", wxstr(mapTab->GetEditor()->getMap().getName())));

		if(selection == 1) {
			detachedFrame->SetWindowStyleFlag(detachedFrame->GetWindowStyleFlag() | wxSTAY_ON_TOP);
			detachedFrame->SetTitle(wxString::Format("Always-on-top View: %s", wxstr(mapTab->GetEditor()->getMap().getName())));
			pinCheckbox->SetValue(true);
		}

		RegisterDetachedView(mapTab->GetEditor(), detachedFrame);

		detachedFrame->Bind(wxEVT_DESTROY, [this, mapTab, detachedFrame](wxWindowDestroyEvent& WXUNUSED(event)) {
			UnregisterDetachedView(mapTab->GetEditor(), detachedFrame);
		});

		detachedFrame->Show();
		SetStatusText(selection == 0 ? "Created new detached view" : "Created new always-on-top view");
	} else if(selection == 2) {
		MapWindow* newMapWindow = newd MapWindow(root, *mapTab->GetEditor());

		wxAuiPaneInfo paneInfo;
		paneInfo.Caption("Map View")
			.Float()
			.Floatable(true)
			.Dockable(true)
			.Movable(true)
			.Resizable(true)
			.MinSize(400, 300)
			.BestSize(640, 480);

		aui_manager->AddPane(newMapWindow, paneInfo);
		aui_manager->Update();

		newMapWindow->FitToMap();
		newMapWindow->SetScreenCenterPosition(mapTab->GetScreenCenterPosition());

		if(mode == SELECTION_MODE) {
			newMapWindow->GetCanvas()->EnterSelectionMode();
		} else {
			newMapWindow->GetCanvas()->EnterDrawingMode();
		}

		RegisterDockableView(mapTab->GetEditor(), newMapWindow);

		newMapWindow->Bind(wxEVT_DESTROY, [this, mapTab, newMapWindow](wxWindowDestroyEvent& event) {
			UnregisterDockableView(mapTab->GetEditor(), newMapWindow);
			event.Skip();
		});

		SetStatusText("Created new dockable map view");
	}
}

void GUI::RegisterDetachedView(Editor* editor, wxFrame* frame)
{
	if(!editor || !frame) {
		return;
	}

	detached_views[editor].push_back(frame);
	frame->Bind(wxEVT_IDLE, [this, editor, frame](wxIdleEvent& event) {
		if(detached_views.find(editor) == detached_views.end()) {
			frame->Close(true);
		}
		event.Skip();
	});
}

void GUI::RegisterDockableView(Editor* editor, MapWindow* window)
{
	if(!editor || !window) {
		return;
	}

	dockable_views[editor].push_back(window);
	window->Bind(wxEVT_IDLE, [this, editor, window](wxIdleEvent& event) {
		bool editorExists = false;
		for(int i = 0; i < tabbook->GetTabCount(); ++i) {
			auto* mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(i));
			if(mapTab && mapTab->GetEditor() == editor) {
				editorExists = true;
				break;
			}
		}

		if(!editorExists && dockable_views.find(editor) != dockable_views.end()) {
			if(aui_manager->GetPane(window).IsOk()) {
				aui_manager->DetachPane(window);
				window->Destroy();
			}
		}

		event.Skip();
	});
}

void GUI::UnregisterDetachedView(Editor* editor, wxFrame* frame)
{
	auto it = detached_views.find(editor);
	if(it == detached_views.end()) {
		return;
	}

	it->second.remove(frame);
	if(it->second.empty()) {
		detached_views.erase(it);
	}
}

void GUI::UnregisterDockableView(Editor* editor, MapWindow* window)
{
	auto it = dockable_views.find(editor);
	if(it == dockable_views.end()) {
		return;
	}

	it->second.remove(window);
	if(it->second.empty()) {
		dockable_views.erase(it);
	}
}

bool GUI::HasDetachedViews(Editor* editor) const
{
	auto detached_it = detached_views.find(editor);
	auto dockable_it = dockable_views.find(editor);

	return (detached_it != detached_views.end() && !detached_it->second.empty()) ||
		(dockable_it != dockable_views.end() && !dockable_it->second.empty());
}

bool GUI::CloseDetachedViews(Editor* editor)
{
	bool had_views = false;

	auto frame_it = detached_views.find(editor);
	if(frame_it != detached_views.end()) {
		std::list<wxFrame*> frames_to_close = frame_it->second;
		for(wxFrame* frame : frames_to_close) {
			frame->Close(true);
		}
		detached_views.erase(editor);
		had_views = true;
	}

	auto dock_it = dockable_views.find(editor);
	if(dock_it != dockable_views.end()) {
		std::list<MapWindow*> windows_to_close = dock_it->second;
		for(MapWindow* window : windows_to_close) {
			if(aui_manager->GetPane(window).IsOk()) {
				aui_manager->DetachPane(window);
				window->Destroy();
			}
		}
		dockable_views.erase(editor);
		had_views = true;
		if(aui_manager) {
			aui_manager->Update();
		}
	}

	wxTheApp->ProcessPendingEvents();
	return had_views;
}

void GUI::UpdateDetachedViewsTitle(Editor* editor)
{
	auto it = detached_views.find(editor);
	if(it == detached_views.end()) {
		return;
	}

	for(wxFrame* frame : it->second) {
		wxString title = frame->GetTitle();
		if(title.Contains("Always-on-top View:")) {
			frame->SetTitle(wxString::Format("Always-on-top View: %s", wxstr(editor->getMap().getName())));
		} else {
			frame->SetTitle(wxString::Format("Detached View: %s", wxstr(editor->getMap().getName())));
		}
	}
}

void GUI::LoadPerspective()
{
	if(!IsVersionLoaded()) {
		if(g_settings.getInteger(Config::WINDOW_MAXIMIZED)) {
			root->Maximize();
		} else {
			root->SetSize(wxSize(
				g_settings.getInteger(Config::WINDOW_WIDTH),
				g_settings.getInteger(Config::WINDOW_HEIGHT)
			));
		}
	} else {
		std::string tmp;
		std::string layout = g_settings.getString(Config::PALETTE_LAYOUT);

		std::vector<std::string> palette_list;
		for(char c : layout) {
			if(c == '|') {
				palette_list.push_back(tmp);
				tmp.clear();
			} else {
				tmp.push_back(c);
			}
		}

		if(!tmp.empty()) {
			palette_list.push_back(tmp);
		}

		for(const std::string& name : palette_list) {
			PaletteWindow* palette = CreatePalette();

			wxAuiPaneInfo& info = aui_manager->GetPane(palette);
			aui_manager->LoadPaneInfo(wxstr(name), info);

			if(info.IsFloatable()) {
				bool offscreen = true;
				for(uint32_t index = 0; index < wxDisplay::GetCount(); ++index) {
					wxDisplay display(index);
					wxRect rect = display.GetClientArea();
					if(rect.Contains(info.floating_pos)) {
						offscreen = false;
						break;
					}
				}

				if(offscreen) {
					info.Dock();
				}
			}
		}

		if(g_settings.getInteger(Config::MINIMAP_VISIBLE)) {
			if(!minimap) {
				wxAuiPaneInfo info;

				const wxString& data = wxstr(g_settings.getString(Config::MINIMAP_LAYOUT));
				aui_manager->LoadPaneInfo(data, info);

				minimap = newd MinimapWindow(root);
				aui_manager->AddPane(minimap, info);
			} else {
				wxAuiPaneInfo& info = aui_manager->GetPane(minimap);

				const wxString& data = wxstr(g_settings.getString(Config::MINIMAP_LAYOUT));
				aui_manager->LoadPaneInfo(data, info);
			}

			wxAuiPaneInfo& info = aui_manager->GetPane(minimap);
			if(info.IsFloatable()) {
				bool offscreen = true;
				for(uint32_t index = 0; index < wxDisplay::GetCount(); ++index) {
					wxDisplay display(index);
					wxRect rect = display.GetClientArea();
					if(rect.Contains(info.floating_pos)) {
						offscreen = false;
						break;
					}
				}

				if(offscreen) {
					info.Dock();
				}
			}
		}

		if(g_settings.getInteger(Config::ACTIONS_HISTORY_VISIBLE)) {
			if(!actions_history_window) {
				wxAuiPaneInfo info;

				const wxString& data = wxstr(g_settings.getString(Config::ACTIONS_HISTORY_LAYOUT));
				aui_manager->LoadPaneInfo(data, info);

				actions_history_window = new ActionsHistoryWindow(root);
				aui_manager->AddPane(actions_history_window, info);
			} else {
				wxAuiPaneInfo& info = aui_manager->GetPane(actions_history_window);
				const wxString& data = wxstr(g_settings.getString(Config::ACTIONS_HISTORY_LAYOUT));
				aui_manager->LoadPaneInfo(data, info);
			}

			wxAuiPaneInfo& info = aui_manager->GetPane(actions_history_window);
			if(info.IsFloatable()) {
				bool offscreen = true;
				for(uint32_t index = 0; index < wxDisplay::GetCount(); ++index) {
					wxDisplay display(index);
					wxRect rect = display.GetClientArea();
					if(rect.Contains(info.floating_pos)) {
						offscreen = false;
						break;
					}
				}

				if(offscreen) {
					info.Dock();
				}
			}
		}

		if(g_settings.getInteger(Config::RECENT_BRUSHES_VISIBLE)) {
			if(!recent_brushes_window) {
				wxAuiPaneInfo info;
				const wxString& data = wxstr(g_settings.getString(Config::RECENT_BRUSHES_LAYOUT));
				aui_manager->LoadPaneInfo(data, info);

				recent_brushes_window = newd RecentBrushesWindow(root);
				recent_brushes_window->UpdateBrushes(recent_brushes);
				aui_manager->AddPane(recent_brushes_window, info);
			} else {
				wxAuiPaneInfo& info = aui_manager->GetPane(recent_brushes_window);
				const wxString& data = wxstr(g_settings.getString(Config::RECENT_BRUSHES_LAYOUT));
				aui_manager->LoadPaneInfo(data, info);
			}

			wxAuiPaneInfo& info = aui_manager->GetPane(recent_brushes_window);
			if(info.IsFloatable()) {
				bool offscreen = true;
				for(uint32_t index = 0; index < wxDisplay::GetCount(); ++index) {
					wxDisplay display(index);
					wxRect rect = display.GetClientArea();
					if(rect.Contains(info.floating_pos)) {
						offscreen = false;
						break;
					}
				}

				if(offscreen) {
					info.Dock();
				}
			}
		}

		aui_manager->Update();
		root->UpdateMenubar();
	}

	root->GetAuiToolBar()->LoadPerspective();
}

void GUI::SavePerspective()
{
	g_settings.setInteger(Config::WINDOW_MAXIMIZED, root->IsMaximized());
	g_settings.setInteger(Config::WINDOW_WIDTH, root->GetSize().GetWidth());
	g_settings.setInteger(Config::WINDOW_HEIGHT, root->GetSize().GetHeight());
	g_settings.setInteger(Config::MINIMAP_VISIBLE, minimap? 1: 0);
	g_settings.setInteger(Config::ACTIONS_HISTORY_VISIBLE, actions_history_window ? 1 : 0);
	g_settings.setInteger(Config::RECENT_BRUSHES_VISIBLE, recent_brushes_window ? 1 : 0);

	wxString pinfo;
	for(auto &palette : palettes) {
		if(aui_manager->GetPane(palette).IsShown())
			pinfo << aui_manager->SavePaneInfo(aui_manager->GetPane(palette)) << "|";
	}
	g_settings.setString(Config::PALETTE_LAYOUT, nstr(pinfo));

	if(minimap) {
		wxString s = aui_manager->SavePaneInfo(aui_manager->GetPane(minimap));
		g_settings.setString(Config::MINIMAP_LAYOUT, nstr(s));
	}

	if(actions_history_window) {
		wxString info = aui_manager->SavePaneInfo(aui_manager->GetPane(actions_history_window));
		g_settings.setString(Config::ACTIONS_HISTORY_LAYOUT, nstr(info));
	}

	if(recent_brushes_window) {
		wxString info = aui_manager->SavePaneInfo(aui_manager->GetPane(recent_brushes_window));
		g_settings.setString(Config::RECENT_BRUSHES_LAYOUT, nstr(info));
	}

	root->GetAuiToolBar()->SavePerspective();
}

void GUI::HideSearchWindow()
{
	if(search_result_window) {
		aui_manager->GetPane(search_result_window).Show(false);
		aui_manager->Update();
	}
}

SearchResultWindow* GUI::ShowSearchWindow()
{
	if(search_result_window == nullptr) {
		search_result_window = newd SearchResultWindow(root);
		Theme::ApplyText(search_result_window, true);
		aui_manager->AddPane(search_result_window, wxAuiPaneInfo().Caption("Search Results"));
	} else {
		aui_manager->GetPane(search_result_window).Show();
	}
	aui_manager->Update();
	return search_result_window;
}

DuplicatedItemsWindow* GUI::ShowDuplicatedItemsWindow()
{
	if(!duplicated_items_window) {
		duplicated_items_window = new DuplicatedItemsWindow(root);
		Theme::ApplyText(duplicated_items_window, true);
		aui_manager->AddPane(duplicated_items_window, wxAuiPaneInfo().Caption("Duplicated Items"));
	} else {
		aui_manager->GetPane(duplicated_items_window).Show();
	}
	aui_manager->Update();
	return duplicated_items_window;
}

void GUI::HideDuplicatedItemsWindow()
{
	if(duplicated_items_window) {
		aui_manager->GetPane(duplicated_items_window).Show(false);
		aui_manager->Update();
	}
}

ActionsHistoryWindow* GUI::ShowActionsWindow()
{
	if(!actions_history_window) {
		actions_history_window = new ActionsHistoryWindow(root);
		Theme::ApplyText(actions_history_window, true);
		aui_manager->AddPane(actions_history_window, wxAuiPaneInfo().Caption("Actions History"));
	} else {
		aui_manager->GetPane(actions_history_window).Show();
	}

	aui_manager->Update();
	actions_history_window->RefreshActions();
	return actions_history_window;
}

void GUI::HideActionsWindow()
{
	if(actions_history_window) {
		aui_manager->GetPane(actions_history_window).Show(false);
		aui_manager->Update();
	}
}

RecentBrushesWindow* GUI::ShowRecentBrushesWindow()
{
	if(!recent_brushes_window) {
		recent_brushes_window = newd RecentBrushesWindow(root);
		Theme::ApplyText(recent_brushes_window, true);
		recent_brushes_window->UpdateBrushes(recent_brushes);
		aui_manager->AddPane(recent_brushes_window,
			wxAuiPaneInfo().Caption("Recent Brushes").Right().BestSize(260, 400));
	} else {
		aui_manager->GetPane(recent_brushes_window).Show();
	}
	recent_brushes_window->UpdateBrushes(recent_brushes);
	recent_brushes_window->SetSelectedBrush(current_brush);
	aui_manager->Update();
	return recent_brushes_window;
}

void GUI::HideRecentBrushesWindow()
{
	if(recent_brushes_window) {
		aui_manager->GetPane(recent_brushes_window).Show(false);
		aui_manager->Update();
	}
}

BrowseTilePanel* GUI::ShowBrowseFieldPanel()
{
	if(!browse_field_notebook) {
		browse_field_notebook = newd BrowseFieldNotebook(root);
		Theme::ApplyText(browse_field_notebook, true);
		aui_manager->AddPane(browse_field_notebook,
			wxAuiPaneInfo().Caption("Browse Field").Right().BestSize(320, 520));
	} else {
		aui_manager->GetPane(browse_field_notebook).Show();
	}

	aui_manager->Update();
	return browse_field_notebook->GetBrowseTilePanel();
}

void GUI::HideBrowseFieldPanel()
{
	if(browse_field_notebook) {
		aui_manager->GetPane(browse_field_notebook).Show(false);
		aui_manager->Update();
	}
}

BrushManagerPanel* GUI::ShowBrushManager()
{
	if(!IsVersionLoaded()) {
		return nullptr;
	}

	if(!brush_manager_panel) {
		brush_manager_panel = newd BrushManagerPanel(root);
		Theme::ApplyText(brush_manager_panel, true);
		aui_manager->AddPane(brush_manager_panel,
			wxAuiPaneInfo().Caption("Brush Manager").Right().BestSize(350, 500));
	} else {
		// Toggle visibility - if already shown, hide it
		wxAuiPaneInfo& pane = aui_manager->GetPane(brush_manager_panel);
		if(pane.IsShown()) {
			pane.Show(false);
		} else {
			pane.Show(true);
			brush_manager_panel->RefreshBrushList();
		}
	}

	aui_manager->Update();
	return brush_manager_panel;
}

void GUI::HideBrushManager()
{
	if(brush_manager_panel) {
		aui_manager->GetPane(brush_manager_panel).Show(false);
		aui_manager->Update();
	}
}

//=============================================================================
// Area Decoration Dialog management

void GUI::ShowAreaDecorationDialog()
{
	if (!IsEditorOpen()) {
		return;
	}

	if (area_decoration_dialog) {
		// Dialog already exists, update engine and show it
		area_decoration_dialog->UpdateEngine();
		area_decoration_dialog->Show();
		area_decoration_dialog->Raise();
	} else {
		// Create new dialog
		area_decoration_dialog = newd AreaDecorationDialog(root);
		area_decoration_dialog->Show();
	}
}

void GUI::DestroyAreaDecorationDialog()
{
	if (area_decoration_dialog) {
		area_decoration_dialog->Destroy();
		area_decoration_dialog = nullptr;
	}
}

void GUI::ShowAreaCreatureSpawnDialog()
{
	if (!IsEditorOpen()) {
		return;
	}

	if (area_creature_spawn_dialog) {
		area_creature_spawn_dialog->UpdateEngine();
		area_creature_spawn_dialog->Show();
		area_creature_spawn_dialog->Raise();
	} else {
		area_creature_spawn_dialog = newd AreaCreatureSpawnDialog(root);
		area_creature_spawn_dialog->Show();
	}
}

void GUI::DestroyAreaCreatureSpawnDialog()
{
	if (area_creature_spawn_dialog) {
		area_creature_spawn_dialog->Destroy();
		area_creature_spawn_dialog = nullptr;
	}
}

// Palette Window Interface implementation

PaletteWindow* GUI::GetPalette()
{
	if(palettes.empty())
		return nullptr;
	return palettes.front();
}

PaletteWindow* GUI::NewPalette()
{
	return CreatePalette();
}

void GUI::RefreshPalettes(Map* m, bool usedefault)
{
	for(auto&palette : palettes) {
		palette->OnUpdate(m? m : (usedefault? (IsEditorOpen()? &GetCurrentMap() : nullptr): nullptr));
	}
	SelectBrush();

	if(duplicated_items_window) {
		duplicated_items_window->UpdateButtons();
	}

	RefreshActions();
}

void GUI::RefreshOtherPalettes(PaletteWindow* p)
{
	for(auto &palette : palettes) {
		if(palette != p)
			palette->OnUpdate(IsEditorOpen()? &GetCurrentMap() : nullptr);
	}
	SelectBrush();
}

PaletteWindow* GUI::CreatePalette()
{
	if(!IsVersionLoaded())
		return nullptr;

	auto *palette = newd PaletteWindow(root, g_materials.tilesets);
	Theme::ApplyText(palette, true);
	aui_manager->AddPane(palette, wxAuiPaneInfo().Caption("Palette").TopDockable(false).BottomDockable(false));
	aui_manager->Update();

	// Make us the active palette
	palettes.push_front(palette);
	// Select brush from this palette
	SelectBrushInternal(palette->GetSelectedBrush());

	return palette;
}

void GUI::ActivatePalette(PaletteWindow* p)
{
	palettes.erase(std::find(palettes.begin(), palettes.end(), p));
	palettes.push_front(p);
}

void GUI::DestroyPalettes()
{
	for(auto palette : palettes) {
		aui_manager->DetachPane(palette);
		palette->Destroy();
		palette = nullptr;
	}
	palettes.clear();
	aui_manager->Update();
}

void GUI::RebuildPalettes()
{
	// Palette lits might be modified due to active palette changes
	// Use a temporary list for iterating
	PaletteList tmp = palettes;
	for(auto &piter : tmp) {
		piter->ReloadSettings(IsEditorOpen()? &GetCurrentMap() : nullptr);
	}
	aui_manager->Update();
}

void GUI::ShowPalette()
{
	if(palettes.empty())
		return;

	for(auto &palette : palettes) {
		if(aui_manager->GetPane(palette).IsShown())
			return;
	}

	aui_manager->GetPane(palettes.front()).Show(true);
	aui_manager->Update();
}

void GUI::SelectPalettePage(PaletteType pt)
{
	if(palettes.empty())
		CreatePalette();
	PaletteWindow* p = GetPalette();
	if(!p)
		return;

	ShowPalette();
	p->SelectPage(pt);
	aui_manager->Update();
	SelectBrushInternal(p->GetSelectedBrush());
}

//=============================================================================
// Minimap Window Interface Implementation

void GUI::CreateMinimap()
{
	if(!IsVersionLoaded())
		return;

	if(minimap) {
		aui_manager->GetPane(minimap).Show(true);
	} else {
		minimap = newd MinimapWindow(root);
		Theme::ApplyText(minimap, true);
		minimap->Show(true);
		aui_manager->AddPane(minimap, wxAuiPaneInfo().Caption("Minimap"));
	}
	aui_manager->Update();
}

void GUI::HideMinimap()
{
	if(minimap) {
		aui_manager->GetPane(minimap).Show(false);
		aui_manager->Update();
	}
}

void GUI::DestroyMinimap()
{
	if(minimap) {
		aui_manager->DetachPane(minimap);
		aui_manager->Update();
		minimap->Destroy();
		minimap = nullptr;
	}
}

void GUI::UpdateMinimap(bool immediate)
{
	if(IsMinimapVisible()) {
		if(immediate) {
			minimap->Refresh();
		} else {
			minimap->DelayedUpdate();
		}
	}
}

bool GUI::IsMinimapVisible() const
{
	if(minimap) {
		const wxAuiPaneInfo& pi = aui_manager->GetPane(minimap);
		if(pi.IsShown()) {
			return true;
		}
	}
	return false;
}

//=============================================================================

void GUI::RefreshView()
{
	EditorTab* editorTab = GetCurrentTab();
	if(!editorTab) {
		return;
	}

	if(!dynamic_cast<MapTab*>(editorTab)) {
		editorTab->GetWindow()->Refresh();
		return;
	}

	std::vector<EditorTab*> editorTabs;
	for(int32_t index = 0; index < tabbook->GetTabCount(); ++index) {
		auto * mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(index));
		if(mapTab) {
			editorTabs.push_back(mapTab);
		}
	}

	for(EditorTab* editorTab : editorTabs) {
		editorTab->GetWindow()->Refresh();
	}
}

void GUI::ToggleCameraPathPlayback()
{
	MapTab* mapTab = GetCurrentMapTab();
	if(!mapTab) {
		return;
	}
	MapCanvas* canvas = mapTab->GetCanvas();
	if(canvas) {
		canvas->ToggleCameraPathPlayback();
	}
}

void GUI::AddCameraPathKeyframeAtCursor()
{
	MapTab* mapTab = GetCurrentMapTab();
	if(!mapTab) {
		return;
	}

	Editor* editor = mapTab->GetEditor();
	if(!editor) {
		return;
	}

	CameraPaths temp = editor->getMap().camera_paths;
	CameraPath* path = temp.getActivePath();
	if(!path) {
		path = temp.addPath("Path");
	}

	Position pos = mapTab->GetCanvas()->GetCursorPosition();
	if(!pos.isValid()) {
		pos = mapTab->GetScreenCenterPosition();
	}
	if(!pos.isValid()) {
		return;
	}

	CameraKeyframe key;
	key.pos = pos;
	key.pos.z = GetKeyframeZ();
	key.duration = GetKeyframeDuration();
	key.speed = GetKeyframeSpeed();
	key.zoom = GetKeyframeZoom();
	key.easing = static_cast<CameraEasing>(GetKeyframeEasing());

	int insertIndex = static_cast<int>(path->keyframes.size());
	int activeIndex = temp.getActiveKeyframe();
	if(activeIndex >= 0 && activeIndex < static_cast<int>(path->keyframes.size())) {
		insertIndex = activeIndex + 1;
	}
	path->keyframes.insert(path->keyframes.begin() + insertIndex, key);
	temp.setActiveKeyframe(insertIndex);

	editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_DRAW);
	editor->resetActionsTimer();
	editor->updateActions();
}

void GUI::CreateLoadBar(wxString message, bool canCancel /* = false */ )
{
	progressText = message;
	loadingStats.Reset();

	progressFrom = 0;
	progressTo = 100;
	currentProgress = -1;

	// Create progress dialog with improved appearance
	wxString initialMessage = progressText + "\n";
	progressBar = newd wxGenericProgressDialog(
		"Remere's Map Editor - Loading Assets",
		initialMessage + "Please wait...",
		100,
		root,
		wxPD_APP_MODAL | wxPD_SMOOTH | wxPD_ELAPSED_TIME | (canCancel ? wxPD_CAN_ABORT : 0)
	);
	
	// Set larger size for better visibility
	progressBar->SetSize(450, -1);
	
	// Try to set icon (same as main window if available)
	if(root && root->GetIcon().IsOk()) {
		progressBar->SetIcon(root->GetIcon());
	}
	
	progressBar->Show(true);

	for(int idx = 0; idx < tabbook->GetTabCount(); ++idx) {
		auto * mt = dynamic_cast<MapTab*>(tabbook->GetTab(idx));
		if(mt && mt->GetEditor()->IsLiveServer())
			mt->GetEditor()->GetLiveServer()->startOperation(progressText);
	}
	progressBar->Update(0);
}

void GUI::SetLoadScale(int32_t from, int32_t to)
{
	progressFrom = from;
	progressTo = to;
}

bool GUI::SetLoadDone(int32_t done, const wxString& newMessage)
{
	if(done == 100) {
		DestroyLoadBar();
		return true;
	} else if(done == currentProgress) {
		return true;
	}

	if(!newMessage.empty()) {
		progressText = newMessage;
	}

	int32_t newProgress = progressFrom + static_cast<int32_t>((done / 100.f) * (progressTo - progressFrom));
	newProgress = std::max<int32_t>(0, std::min<int32_t>(100, newProgress));

	bool skip = false;
	if(progressBar) {
		// Build detailed message
		wxString detailedMessage = progressText;
		
		// Add statistics if available
		if(loadingStats.total_sprites > 0 || loadingStats.total_items > 0 || loadingStats.total_creatures > 0) {
			detailedMessage += "\n";
			if(loadingStats.total_sprites > 0) {
				detailedMessage += wxString::Format("Sprites: %u  ", loadingStats.total_sprites);
			}
			if(loadingStats.total_items > 0) {
				detailedMessage += wxString::Format("Items: %u  ", loadingStats.total_items);
			}
			if(loadingStats.total_creatures > 0) {
				detailedMessage += wxString::Format("Creatures: %u", loadingStats.total_creatures);
			}
		}
		
		if(!loadingStats.current_file.empty()) {
			detailedMessage += "\n" + loadingStats.current_file;
		}
		
		progressBar->Update(
			newProgress,
			detailedMessage,
			&skip
		);
		currentProgress = newProgress;
	}

	for(int32_t index = 0; index < tabbook->GetTabCount(); ++index) {
		auto * mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(index));
		if(mapTab && mapTab->GetEditor()) {
			LiveServer* server = mapTab->GetEditor()->GetLiveServer();
			if(server) {
				server->updateOperation(newProgress);
			}
		}
	}

	return skip;
}

void GUI::DestroyLoadBar()
{
	if(progressBar) {
		progressBar->Show(false);
		currentProgress = -1;

		progressBar->Destroy();
		progressBar = nullptr;

		if(root->IsActive()) {
			root->Raise();
		} else {
			root->RequestUserAttention();
		}
	}
	loadingStats.Reset();
}

void GUI::SetLoadingStats(const LoadingStats& stats)
{
	loadingStats = stats;
	// Force refresh of progress bar with updated stats
	if(progressBar && currentProgress >= 0) {
		SetLoadDone(currentProgress * 100 / std::max(1, progressTo - progressFrom), wxEmptyString);
	}
}


void GUI::ShowWelcomeDialog(const wxBitmap &icon) {
    std::vector<wxString> recent_files = root->GetRecentFiles();
    welcomeDialog = newd WelcomeDialog(__W_RME_APPLICATION_NAME__, "Version " + __W_RME_VERSION__, FROM_DIP(root, wxSize(800, 480)), icon, recent_files);
    welcomeDialog->Bind(wxEVT_CLOSE_WINDOW, &GUI::OnWelcomeDialogClosed, this);
    welcomeDialog->Bind(WELCOME_DIALOG_ACTION, &GUI::OnWelcomeDialogAction, this);
    welcomeDialog->Show();
    UpdateMenubar();
}

void GUI::FinishWelcomeDialog() {
    if(welcomeDialog != nullptr) {
        welcomeDialog->Hide();
		root->Show();
        welcomeDialog->Destroy();
        welcomeDialog = nullptr;
    }
}

bool GUI::IsWelcomeDialogShown() {
    return welcomeDialog != nullptr && welcomeDialog->IsShown();
}

void GUI::OnWelcomeDialogClosed(wxCloseEvent &event)
{
    welcomeDialog->Destroy();
    root->Close();
}

void GUI::OnWelcomeDialogAction(wxCommandEvent &event)
{
    if(event.GetId() == wxID_NEW) {
        NewMap();
    } else if(event.GetId() == wxID_OPEN) {
        LoadMap(FileName(event.GetString()));
    }
}

void GUI::UpdateMenubar()
{
	root->UpdateMenubar();
}

void GUI::SetScreenCenterPosition(const Position& position, bool showIndicator)
{
	MapTab* mapTab = GetCurrentMapTab();
	if(mapTab)
		mapTab->SetScreenCenterPosition(position, showIndicator);
}

void GUI::DoCut()
{
	if(!IsSelectionMode())
		return;

	Editor* editor = GetCurrentEditor();
	if(!editor)
		return;

	editor->copybuffer.cut(*editor, GetCurrentFloor());
	RefreshView();
	root->UpdateMenubar();
}

void GUI::DoCopy()
{
	if(!IsSelectionMode())
		return;

	Editor* editor = GetCurrentEditor();
	if(!editor)
		return;

	editor->copybuffer.copy(*editor, GetCurrentFloor());
	RefreshView();
	root->UpdateMenubar();
}

void GUI::DoPaste()
{
	MapTab* mapTab = GetCurrentMapTab();
	if(mapTab)
		copybuffer.paste(*mapTab->GetEditor(), mapTab->GetCanvas()->GetCursorPosition());
}

void GUI::PreparePaste(bool keep)
{
	Editor* editor = GetCurrentEditor();
	if(editor) {
		keep_pasting = keep;
		SetSelectionMode();
		Selection& selection = editor->getSelection();
		selection.start();
		selection.clear();
		selection.finish();
		StartPasting();
		RefreshView();
	}
}

void GUI::StartPasting()
{
	if(GetCurrentEditor()) {
		pasting = true;
		secondary_map = &copybuffer.getBufferMap();
	}
}

void GUI::EndPasting()
{
	if(pasting) {
		pasting = false;
		keep_pasting = false;
		secondary_map = nullptr;
	}
}

bool GUI::CanUndo()
{
	Editor* editor = GetCurrentEditor();
	return (editor && editor->canUndo());
}

bool GUI::CanRedo()
{
	Editor* editor = GetCurrentEditor();
	return (editor && editor->canRedo());
}

bool GUI::DoUndo()
{
	Editor* editor = GetCurrentEditor();
	if(editor && editor->canUndo()) {
		editor->undo();
		if(editor->hasSelection())
			SetSelectionMode();
		SetStatusText("Undo action");
		UpdateMinimap();
		root->UpdateMenubar();
		root->Refresh();
		return true;
	}
	return false;
}

bool GUI::DoRedo()
{
	Editor* editor = GetCurrentEditor();
	if(editor && editor->canRedo()) {
		editor->redo();
		if(editor->hasSelection())
			SetSelectionMode();
		SetStatusText("Redo action");
		UpdateMinimap();
		root->UpdateMenubar();
		root->Refresh();
		return true;
	}
	return false;
}

int GUI::GetCurrentFloor()
{
	MapTab* tab = GetCurrentMapTab();
	ASSERT(tab);
	return tab->GetCanvas()->GetFloor();
}

void GUI::ChangeFloor(int new_floor)
{
	MapTab* tab = GetCurrentMapTab();
	if(tab) {
		int old_floor = GetCurrentFloor();
		if(new_floor < rme::MapMinLayer || new_floor > rme::MapMaxLayer)
			return;

		if(old_floor != new_floor)
			tab->GetCanvas()->ChangeFloor(new_floor);
	}
}

void GUI::SetStatusText(wxString text)
{
	g_gui.root->SetStatusText(text, 0);
}

void GUI::SetTitle(wxString title)
{
	if(g_gui.root == nullptr)
		return;

#ifdef NIGHTLY_BUILD
#  ifdef SVN_BUILD
#     define TITLE_APPEND (wxString(" (Nightly Build #") << i2ws(SVN_BUILD) << ")")
#  else
#     define TITLE_APPEND (wxString(" (Nightly Build)"))
#  endif
#else
#  ifdef SVN_BUILD
#     define TITLE_APPEND (wxString(" (Build #") << i2ws(SVN_BUILD) << ")")
#  else
#     define TITLE_APPEND (wxString(""))
#  endif
#endif
#ifdef __EXPERIMENTAL__
	if(title != "") {
		g_gui.root->SetTitle(title << " - Remere's Map Editor BETA" << TITLE_APPEND);
	} else {
		g_gui.root->SetTitle(wxString("Remere's Map Editor BETA") << TITLE_APPEND);
	}
#elif __SNAPSHOT__
	if(title != "") {
		g_gui.root->SetTitle(title << " - Remere's Map Editor - SNAPSHOT" << TITLE_APPEND);
	}
	else {
		g_gui.root->SetTitle(wxString("Remere's Map Editor - SNAPSHOT") << TITLE_APPEND);
	}
#else
	if(!title.empty()) {
		g_gui.root->SetTitle(title << " - Remere's Map Editor" << TITLE_APPEND);
	} else {
		g_gui.root->SetTitle(wxString("Remere's Map Editor") << TITLE_APPEND);
	}
#endif
}

void GUI::UpdateTitle()
{
	if(tabbook->GetTabCount() > 0) {
		SetTitle(tabbook->GetCurrentTab()->GetTitle());
		for(int idx = 0; idx < tabbook->GetTabCount(); ++idx) {
			if(tabbook->GetTab(idx))
				tabbook->SetTabLabel(idx, tabbook->GetTab(idx)->GetTitle());
		}
		tabbook->UpdatePulseState();
		for(int idx = 0; idx < tabbook->GetTabCount(); ++idx) {
			auto* mapTab = dynamic_cast<MapTab*>(tabbook->GetTab(idx));
			if(mapTab) {
				UpdateDetachedViewsTitle(mapTab->GetEditor());
			}
		}
	} else {
		SetTitle("");
	}
}

void GUI::UpdateMenus()
{
	wxCommandEvent evt(EVT_UPDATE_MENUS);
	g_gui.root->AddPendingEvent(evt);
}

void GUI::UpdateActions()
{
	wxCommandEvent evt(EVT_UPDATE_ACTIONS);
	g_gui.root->AddPendingEvent(evt);
}

void GUI::RefreshActions()
{
	if(actions_history_window)
		actions_history_window->RefreshActions();
}

void GUI::AddRecentBrush(const Brush* brush)
{
	if(!brush) {
		return;
	}

	const TilesetCategoryType category = GetRecentBrushCategory(brush);
	if(category == TILESET_UNKNOWN) {
		return;
	}

	auto& list = recent_brushes[category];
	if(std::find(list.begin(), list.end(), brush) != list.end()) {
		return;
	}

	if(list.size() >= kMaxRecentBrushesPerCategory) {
		list.erase(list.begin());
	}
	list.push_back(brush);

	if(recent_brushes_window) {
		recent_brushes_window->UpdateBrushes(recent_brushes);
	}
}

void GUI::ClearRecentBrushes()
{
	recent_brushes.clear();
	if(recent_brushes_window) {
		recent_brushes_window->UpdateBrushes(recent_brushes);
	}
}

void GUI::ShowToolbar(ToolBarID id, bool show)
{
	if(root && root->GetAuiToolBar())
		root->GetAuiToolBar()->Show(id, show);
}

void GUI::SwitchMode()
{
	if(mode == DRAWING_MODE) {
		SetSelectionMode();
	} else {
		SetDrawingMode();
	}
}

void GUI::BeginRectanglePick(std::function<void(const Position&, const Position&)> onComplete,
                             std::function<void()> onCancel,
                             std::function<void(const Position&)> onFirstClick)
{
	rect_pick.active = true;
	rect_pick.hasFirst = false;
	rect_pick.onComplete = std::move(onComplete);
	rect_pick.onCancel = std::move(onCancel);
	rect_pick.onFirstClick = std::move(onFirstClick);
	SetStatusText("Select first corner on the map (Esc cancels).");
}

void GUI::CancelRectanglePick()
{
	if(!rect_pick.active) {
		return;
	}
	if(rect_pick.onCancel) {
		rect_pick.onCancel();
	}
	rect_pick = RectanglePickState();
	SetStatusText("Rectangle selection cancelled.");
}

bool GUI::HandleRectanglePickClick(const Position& pos)
{
	if(!rect_pick.active) {
		return false;
	}
	if(!rect_pick.hasFirst) {
		rect_pick.first = pos;
		rect_pick.hasFirst = true;
		// Notify first click callback
		if(rect_pick.onFirstClick) {
			rect_pick.onFirstClick(pos);
		}
		SetStatusText("Select second corner on the map.");
		return true;
	}

	Position first = rect_pick.first;
	auto onComplete = rect_pick.onComplete;
	rect_pick = RectanglePickState();
	if(onComplete) {
		onComplete(first, pos);
	}
	SetStatusText("Rectangle selected.");
	return true;
}

void GUI::SetSelectionMode()
{
	if(mode == SELECTION_MODE)
		return;

	if(current_brush && current_brush->isDoodad()) {
		secondary_map = nullptr;
	}

	tabbook->OnSwitchEditorMode(SELECTION_MODE);
	mode = SELECTION_MODE;
}

void GUI::SetDrawingMode()
{
	if(mode == DRAWING_MODE)
		return;

	std::set<MapTab*> al;
	for(int idx = 0; idx < tabbook->GetTabCount(); ++idx) {
		EditorTab* editorTab = tabbook->GetTab(idx);
		if(MapTab* mapTab = dynamic_cast<MapTab*>(editorTab)) {
			if(al.find(mapTab) != al.end())
				continue;

			Editor* editor = mapTab->GetEditor();
			Selection& selection = editor->getSelection();
			selection.start(Selection::NONE, ACTION_UNSELECT);
			selection.clear();
			selection.finish();
			selection.updateSelectionCount();
			al.insert(mapTab);
		}
	}

	if(current_brush && current_brush->isDoodad()) {
		secondary_map = doodad_buffer_map;
	} else {
		secondary_map = nullptr;
	}

	tabbook->OnSwitchEditorMode(DRAWING_MODE);
	mode = DRAWING_MODE;
}

void GUI::SetBrushSizeInternal(int nz)
{
	if(nz != brush_size && current_brush && current_brush->isDoodad() && !current_brush->oneSizeFitsAll()) {
		brush_size = nz;
		FillDoodadPreviewBuffer();
		secondary_map = doodad_buffer_map;
	} else {
		brush_size = nz;
	}
}

void GUI::SetBrushSize(int nz)
{
	SetBrushSizeInternal(nz);

	for(auto &palette : palettes) {
		palette->OnUpdateBrushSize(brush_shape, brush_size);
	}

	if(current_brush && (current_brush->isSpawn() || current_brush->isCreature())) {
		g_settings.setInteger(Config::CURRENT_SPAWN_RADIUS, brush_size);
	}

	root->GetAuiToolBar()->UpdateBrushSize(brush_shape, brush_size);
}

void GUI::SetBrushVariation(int nz)
{
	if(nz != brush_variation && current_brush && current_brush->isDoodad()) {
		// Monkey!
		brush_variation = nz;
		FillDoodadPreviewBuffer();
		secondary_map = doodad_buffer_map;
	}
}

void GUI::SetBrushShape(BrushShape bs)
{
	if(bs != brush_shape && current_brush && current_brush->isDoodad() && !current_brush->oneSizeFitsAll()) {
		// Donkey!
		brush_shape = bs;
		FillDoodadPreviewBuffer();
		secondary_map = doodad_buffer_map;
	}
	brush_shape = bs;

	for(auto &palette : palettes) {
		palette->OnUpdateBrushSize(brush_shape, brush_size);
	}

	root->GetAuiToolBar()->UpdateBrushSize(brush_shape, brush_size);
}

void GUI::SetBrushThickness(bool on, int x, int y)
{
	use_custom_thickness = on;

	if(x != -1 || y != -1) {
		custom_thickness_mod = std::max<float>(x, 1.f) / std::max<float>(y, 1.f);
	}

	if(current_brush && current_brush->isDoodad()) {
		FillDoodadPreviewBuffer();
	}

	RefreshView();
}

void GUI::SetBrushThickness(int low, int ceil)
{
	custom_thickness_mod = std::max<float>(low, 1.f) / std::max<float>(ceil, 1.f);

	if(use_custom_thickness && current_brush && current_brush->isDoodad()) {
		FillDoodadPreviewBuffer();
	}

	RefreshView();
}

void GUI::DecreaseBrushSize(bool wrap)
{
	switch(brush_size) {
		case 0: {
			if(wrap) {
				SetBrushSize(11);
			}
			break;
		}
		case 1: {
			SetBrushSize(0);
			break;
		}
		case 2:
		case 3: {
			SetBrushSize(1);
			break;
		}
		case 4:
		case 5: {
			SetBrushSize(2);
			break;
		}
		case 6:
		case 7: {
			SetBrushSize(4);
			break;
		}
		case 8:
		case 9:
		case 10: {
			SetBrushSize(6);
			break;
		}
		case 11:
		default: {
			SetBrushSize(8);
			break;
		}
	}
}

void GUI::IncreaseBrushSize(bool wrap)
{
	switch(brush_size) {
		case 0: {
			SetBrushSize(1);
			break;
		}
		case 1: {
			SetBrushSize(2);
			break;
		}
		case 2:
		case 3: {
			SetBrushSize(4);
			break;
		}
		case 4:
		case 5: {
			SetBrushSize(6);
			break;
		}
		case 6:
		case 7: {
			SetBrushSize(8);
			break;
		}
		case 8:
		case 9:
		case 10: {
			SetBrushSize(11);
			break;
		}
		case 11:
		default: {
			if(wrap) {
				SetBrushSize(0);
			}
			break;
		}
	}
}

Brush* GUI::GetCurrentBrush() const
{
	return current_brush;
}

BrushShape GUI::GetBrushShape() const
{
	if(current_brush == spawn_brush)
		return BRUSHSHAPE_SQUARE;

	return brush_shape;
}

int GUI::GetBrushSize() const
{
	return brush_size;
}

int GUI::GetBrushVariation() const
{
	return brush_variation;
}

int GUI::GetSpawnTime() const
{
	return creature_spawntime;
}

double GUI::GetKeyframeDuration() const
{
	if(!palettes.empty()) {
		PaletteWindow* palette = palettes.front();
		if(palette && palette->GetCameraPathPalette()) {
			return palette->GetCameraPathPalette()->GetKeyframeDuration();
		}
	}
	return 1.0;
}

double GUI::GetKeyframeSpeed() const
{
	if(!palettes.empty()) {
		PaletteWindow* palette = palettes.front();
		if(palette && palette->GetCameraPathPalette()) {
			return palette->GetCameraPathPalette()->GetKeyframeSpeed();
		}
	}
	return 0.0;
}

double GUI::GetKeyframeZoom() const
{
	if(!palettes.empty()) {
		PaletteWindow* palette = palettes.front();
		if(palette && palette->GetCameraPathPalette()) {
			return palette->GetCameraPathPalette()->GetKeyframeZoom();
		}
	}
	return 1.0;
}

int GUI::GetKeyframeZ() const
{
	if(!palettes.empty()) {
		PaletteWindow* palette = palettes.front();
		if(palette && palette->GetCameraPathPalette()) {
			return palette->GetCameraPathPalette()->GetKeyframeZ();
		}
	}
	return rme::MapGroundLayer;
}

int GUI::GetKeyframeEasing() const
{
	if(!palettes.empty()) {
		PaletteWindow* palette = palettes.front();
		if(palette && palette->GetCameraPathPalette()) {
			return palette->GetCameraPathPalette()->GetKeyframeEasing();
		}
	}
	return 1; // Default to EaseInOut
}

void GUI::SelectBrush()
{
	if(palettes.empty())
		return;

	SelectBrushInternal(palettes.front()->GetSelectedBrush());

	RefreshView();
}

bool GUI::SelectBrush(const Brush* whatbrush, PaletteType primary)
{
	if(palettes.empty())
		if(!CreatePalette())
			return false;

	if(!palettes.front()->OnSelectBrush(whatbrush, primary))
		return false;

	SelectBrushInternal(const_cast<Brush*>(whatbrush));
	root->GetAuiToolBar()->UpdateBrushButtons();
	return true;
}

void GUI::ActivateBrush(const Brush* brush)
{
	if(!brush)
		return;

	if(palettes.empty() && !CreatePalette())
		return;

	SelectBrushInternal(const_cast<Brush*>(brush));
	if(root && root->GetAuiToolBar()) {
		root->GetAuiToolBar()->UpdateBrushButtons();
	}
}

void GUI::SelectBrushInternal(Brush* brush)
{
	// Fear no evil don't you say no evil
	if(current_brush != brush && brush)
		previous_brush = current_brush;

	current_brush = brush;
	if(!current_brush)
		return;

	brush_variation = std::min(brush_variation, brush->getMaxVariation());
	FillDoodadPreviewBuffer();
	if(brush->isDoodad())
		secondary_map = doodad_buffer_map;

	SetDrawingMode();
	if(recent_brushes_window) {
		recent_brushes_window->SetSelectedBrush(current_brush);
	}
	RefreshView();
}

void GUI::SelectPreviousBrush()
{
	if(previous_brush)
		SelectBrush(previous_brush);
}

void GUI::FillDoodadPreviewBuffer()
{
	if(!current_brush || !current_brush->isDoodad())
		return;

	doodad_buffer_map->clear();

	DoodadBrush* brush = current_brush->asDoodad();
	if(brush->isEmpty(GetBrushVariation()))
		return;

	int object_count = 0;
	int area;
	if(GetBrushShape() == BRUSHSHAPE_SQUARE) {
		area = 2*GetBrushSize();
		area = area*area + 1;
	} else {
		if(GetBrushSize() == 1) {
			// There is a huge deviation here with the other formula.
			area = 5;
		} else {
			area = int(0.5 + GetBrushSize() * GetBrushSize() * rme::PI);
		}
	}
	const int object_range = (use_custom_thickness ? int(area*custom_thickness_mod) : brush->getThickness() * area / std::max(1, brush->getThicknessCeiling()));
	const int final_object_count = std::max(1, object_range + random(object_range));

	Position center_pos(0x8000, 0x8000, 0x8);

	if(brush_size > 0 && !brush->oneSizeFitsAll()) {
		while(object_count < final_object_count) {
			int retries = 0;
			bool exit = false;

			// Try to place objects 5 times
			while(retries < 5 && !exit) {

				int pos_retries = 0;
				int xpos = 0, ypos = 0;
				bool found_pos = false;
				if(GetBrushShape() == BRUSHSHAPE_CIRCLE) {
					while(pos_retries < 5 && !found_pos) {
						xpos = random(-brush_size, brush_size);
						ypos = random(-brush_size, brush_size);
						float distance = sqrt(float(xpos*xpos) + float(ypos*ypos));
						if(distance < g_gui.GetBrushSize() + 0.005) {
							found_pos = true;
						} else {
							++pos_retries;
						}
					}
				} else {
					found_pos = true;
					xpos = random(-brush_size, brush_size);
					ypos = random(-brush_size, brush_size);
				}

				if(!found_pos) {
					++retries;
					continue;
				}

				// Decide whether the zone should have a composite or several single objects.
				bool fail = false;
				if(random(brush->getTotalChance(GetBrushVariation())) <= brush->getCompositeChance(GetBrushVariation())) {
					// Composite
					const CompositeTileList& composites = brush->getComposite(GetBrushVariation());

					// Figure out if the placement is valid
					for(const auto &composite : composites) {
						Position pos = center_pos + composite.first + Position(xpos, ypos, 0);
						if(Tile* tile = doodad_buffer_map->getTile(pos)) {
							if(!tile->empty()) {
								fail = true;
								break;
							}
						}
					}
					if(fail) {
						++retries;
						break;
					}

					// Transfer items to the stack
					for(const auto &composite : composites) {
						Position pos = center_pos + composite.first + Position(xpos, ypos, 0);
						const ItemVector& items = composite.second;
						Tile* tile = doodad_buffer_map->getTile(pos);

						if(!tile)
							tile = doodad_buffer_map->allocator(doodad_buffer_map->createTileL(pos));

						for(auto item : items) {
							tile->addItem(item->deepCopy());
						}
						doodad_buffer_map->setTile(tile->getPosition(), tile);
					}
					exit = true;
				} else if(brush->hasSingleObjects(GetBrushVariation())) {
					Position pos = center_pos + Position(xpos, ypos, 0);
					Tile* tile = doodad_buffer_map->getTile(pos);
					if(tile) {
						if(!tile->empty()) {
							fail = true;
							break;
						}
					} else {
						tile = doodad_buffer_map->allocator(doodad_buffer_map->createTileL(pos));
					}
					int variation = GetBrushVariation();
					brush->draw(doodad_buffer_map, tile, &variation);
					//std::cout << "\tpos: " << tile->getPosition() << std::endl;
					doodad_buffer_map->setTile(tile->getPosition(), tile);
					exit = true;
				}
				if(fail) {
					++retries;
					break;
				}
			}
			++object_count;
		}
	} else {
		if(brush->hasCompositeObjects(GetBrushVariation()) &&
				random(brush->getTotalChance(GetBrushVariation())) <= brush->getCompositeChance(GetBrushVariation())) {
			// Composite
			const CompositeTileList& composites = brush->getComposite(GetBrushVariation());

			// All placement is valid...

			// Transfer items to the buffer
			for(const auto &composite : composites) {
				Position pos = center_pos + composite.first;
				const ItemVector& items = composite.second;

				// Check if tile already exists at this position (for multiple entries with same offset)
				Tile* tile = doodad_buffer_map->getTile(pos);
				if(!tile) {
					tile = doodad_buffer_map->allocator(doodad_buffer_map->createTileL(pos));
				}
				//std::cout << pos << " = " << center_pos << " + " << buffer_tile->getPosition() << std::endl;

				for(auto item : items) {
					tile->addItem(item->deepCopy());
				}
				doodad_buffer_map->setTile(tile->getPosition(), tile);
			}
		} else if(brush->hasSingleObjects(GetBrushVariation())) {
			Tile* tile = doodad_buffer_map->allocator(doodad_buffer_map->createTileL(center_pos));
			int variation = GetBrushVariation();
			brush->draw(doodad_buffer_map, tile, &variation);
			doodad_buffer_map->setTile(center_pos, tile);
		}
	}
}

long GUI::PopupDialog(wxWindow* parent, wxString title, wxString text, long style, wxString confisavename, uint32_t configsavevalue)
{
	if(text.empty())
		return wxID_ANY;

	// Use wxGenericMessageDialog for dark mode support (native wxMessageDialog doesn't support dark mode)
	wxGenericMessageDialog dlg(parent, text, title, style);
	return dlg.ShowModal();
}

long GUI::PopupDialog(wxString title, wxString text, long style, wxString configsavename, uint32_t configsavevalue)
{
	return g_gui.PopupDialog(g_gui.root, title, text, style, configsavename, configsavevalue);
}

void GUI::ListDialog(wxWindow* parent, wxString title, const wxArrayString& param_items)
{
	if(param_items.empty())
		return;

	wxArrayString list_items(param_items);

	// Create the window
	wxDialog* dlg = newd wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxRESIZE_BORDER | wxCAPTION | wxCLOSE_BOX);

	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);
	wxListBox* item_list = newd wxListBox(dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	item_list->SetMinSize(wxSize(500, 300));

	for(size_t i = 0; i != list_items.GetCount();) {
		wxString str = list_items[i];
		size_t pos = str.find("\n");
		if(pos != wxString::npos) {
			// Split string!
			item_list->Append(str.substr(0, pos));
			list_items[i] = str.substr(pos+1);
			continue;
		}
		item_list->Append(list_items[i]);
		++i;
	}
	sizer->Add(item_list, 1, wxEXPAND);

	wxSizer* stdsizer = newd wxBoxSizer(wxHORIZONTAL);
	stdsizer->Add(newd wxButton(dlg, wxID_OK, "OK"), wxSizerFlags(1).Center());
	sizer->Add(stdsizer, wxSizerFlags(0).Center());

	dlg->SetSizerAndFit(sizer);

	// Show the window
	dlg->ShowModal();
	delete dlg;
}

void GUI::ShowStartupWarnings()
{
	if(startup_warnings.empty()) {
		PopupDialog("Warnings", "No warnings to display.", wxOK);
		return;
	}
	ListDialog("Startup Warnings", startup_warnings);
}

void GUI::ShowTextBox(wxWindow* parent, wxString title, wxString content)
{
	wxDialog* dlg = newd wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxRESIZE_BORDER | wxCAPTION | wxCLOSE_BOX);
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);
	wxTextCtrl* text_field = newd wxTextCtrl(dlg, wxID_ANY, content, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	text_field->SetMinSize(wxSize(400, 550));
	topsizer->Add(text_field, wxSizerFlags(5).Expand());

	wxSizer* choicesizer = newd wxBoxSizer(wxHORIZONTAL);
	choicesizer->Add(newd wxButton(dlg, wxID_CANCEL, "OK"), wxSizerFlags(1).Center());
	topsizer->Add(choicesizer, wxSizerFlags(0).Center());
	dlg->SetSizerAndFit(topsizer);

	dlg->ShowModal();
}

void GUI::SetHotkey(int index, Hotkey& hotkey)
{
	ASSERT(index >= 0 && index <= 9);
	hotkeys[index] = hotkey;
	SetStatusText("Set hotkey " + i2ws(index) + ".");
}

const Hotkey& GUI::GetHotkey(int index) const
{
	ASSERT(index >= 0 && index <= 9);
	return hotkeys[index];
}

void GUI::SaveHotkeys() const
{
	std::ostringstream os;
	for(const auto &hotkey : hotkeys) {
		os << hotkey << '\n';
	}
	g_settings.setString(Config::NUMERICAL_HOTKEYS, os.str());
}

void GUI::LoadHotkeys()
{
	std::istringstream is;
	is.str(g_settings.getString(Config::NUMERICAL_HOTKEYS));

	std::string line;
	int index = 0;
	while(getline(is, line)) {
		std::istringstream line_is;
		line_is.str(line);
		line_is >> hotkeys[index];

		++index;
	}
}

Hotkey::Hotkey() :
	type(NONE)
{
	////
}

Hotkey::Hotkey(Position _pos) : type(POSITION), pos(_pos)
{
	////
}

Hotkey::Hotkey(Brush* brush) : type(BRUSH), brushname(brush->getName())
{
	////
}

Hotkey::Hotkey(std::string _name) : type(BRUSH), brushname(_name)
{
	////
}

Hotkey::~Hotkey()
{
	////
}

std::ostream& operator<<(std::ostream& os, const Hotkey& hotkey)
{
	switch(hotkey.type) {
		case Hotkey::POSITION: {
			os << "pos:{" << hotkey.pos << "}";
		} break;
		case Hotkey::BRUSH: {
			if(hotkey.brushname.find('{') != std::string::npos ||
					hotkey.brushname.find('}') != std::string::npos) {
				break;
			}
			os << "brush:{" << hotkey.brushname << "}";
		} break;
		default: {
			os << "none:{}";
		} break;
	}
	return os;
}

std::istream& operator>>(std::istream& is, Hotkey& hotkey)
{
	std::string type;
	getline(is, type, ':');
	if(type == "none") {
		is.ignore(2); // ignore "{}"
	} else if(type == "pos") {
		is.ignore(1); // ignore "{"
		Position pos;
		is >> pos;
		hotkey = Hotkey(pos);
		is.ignore(1); // ignore "}"
	} else if(type == "brush") {
		is.ignore(1); // ignore "{"
		std::string brushname;
		getline(is, brushname, '}');
		hotkey = Hotkey(brushname);
	} else {
		// Do nothing...
	}

	return is;
}

void SetWindowToolTip(wxWindow* a, const wxString& tip)
{
	a->SetToolTip(tip);
}

void SetWindowToolTip(wxWindow* a, wxWindow* b, const wxString& tip)
{
	a->SetToolTip(tip);
	b->SetToolTip(tip);
}
