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

#include <ctime>

#include "main_menubar.h"
#include "application.h"
#include "preferences.h"
#include "floor_fading_dialog.h"
#include "about_window.h"
#include "minimap_window.h"
#include "dat_debug_view.h"
#include "result_window.h"
#include "extension_window.h"
#include "find_item_window.h"
#include "duplicated_items_window.h"
#include "brush_manager_window.h"
#include "brush_tips_window.h"
#include "structure_manager_window.h"
#include "area_decoration_dialog.h"
#include "settings.h"
#include "browse_tile_window.h"
#include "hotkey_window.h"
#include "hotkey_utils.h"
#include "welcome_dialog.h"
#include "region_scan_dialog.h"

#include "gui.h"

#include <wx/chartype.h>
#include <wx/choicdlg.h>
#include <wx/numdlg.h>
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/scrolwin.h>
#include <sstream>
#include <algorithm>
#include <vector>
#include <cctype>

#include "items.h"
#include "editor.h"
#include "materials.h"
#include "live_client.h"
#include "live_server.h"
#include "theme.h"
#include "iomap_btmap.h"
#include <wx/dirdlg.h>

namespace
{
	const int kRecentFilesLimit = 20;
	const char* kOpenDialogTitle = "Open Map";

	std::string TrimString(const std::string& text)
	{
		if(text.empty())
			return text;

		size_t start = 0;
		while(start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
			++start;
		}
		size_t end = text.size();
		while(end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
			--end;
		}
		return text.substr(start, end - start);
	}

	std::string ReplaceMnemonicMarkers(std::string text)
	{
		std::replace(text.begin(), text.end(), '$', '&');
		return text;
	}

	std::string StripMenuFormatting(const std::string& text)
	{
		std::string cleaned;
		cleaned.reserve(text.size());
		for(char ch : text) {
			if(ch == '&')
				continue;
			cleaned.push_back(ch);
		}
		cleaned = TrimString(cleaned);
		if(cleaned.size() > 3 && cleaned.compare(cleaned.size() - 3, 3, "...") == 0) {
			cleaned.erase(cleaned.size() - 3);
			cleaned = TrimString(cleaned);
		}
		return cleaned;
	}

	std::string BuildMenuLabel(const std::string& base, const std::string& hotkey)
	{
		if(hotkey.empty())
			return base;
		return base + '\t' + hotkey;
	}

	bool PathEquals(const wxString& left, const wxString& right)
	{
#ifdef __WINDOWS__
		return left.CmpNoCase(right) == 0;
#else
		return left == right;
#endif
	}

	void AddUniquePath(std::vector<wxString>& paths, const wxString& value)
	{
		if(value.empty())
			return;
		for(const wxString& existing : paths) {
			if(PathEquals(existing, value)) {
				return;
			}
		}
		paths.push_back(value);
	}

	std::vector<wxString> LoadFavoriteFiles()
	{
		std::vector<wxString> favorites;
		std::string raw = g_settings.getString(Config::FAVORITE_FILES);
		std::istringstream stream(raw);
		std::string line;
		while(std::getline(stream, line)) {
			if(!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if(!line.empty()) {
				AddUniquePath(favorites, wxstr(line));
			}
		}
		return favorites;
	}

	void SaveFavoriteFiles(const std::vector<wxString>& favorites)
	{
		std::ostringstream stream;
		for(size_t i = 0; i < favorites.size(); ++i) {
			if(i > 0) {
				stream << "\n";
			}
			stream << nstr(favorites[i]);
		}
		g_settings.setString(Config::FAVORITE_FILES, stream.str());
	}
}

BEGIN_EVENT_TABLE(MainMenuBar, wxEvtHandler)
END_EVENT_TABLE()

MainMenuBar::MainMenuBar(MainFrame *frame) : frame(frame), recentFiles(kRecentFilesLimit)
{
	using namespace MenuBar;
	checking_programmaticly = false;

#define MAKE_ACTION(id, kind, handler) \
	actions[#id] = new MenuBar::Action(#id, id, kind, wxCommandEventFunction(&MainMenuBar::handler)); \
	actions_by_id[MenuBar::id] = actions[#id]
#define MAKE_SET_ACTION(id, kind, setting_, handler) \
	actions[#id] = new MenuBar::Action(#id, id, kind, wxCommandEventFunction(&MainMenuBar::handler)); \
	actions[#id].setting = setting_; \
	actions_by_id[MenuBar::id] = actions[#id]

	MAKE_ACTION(NEW, wxITEM_NORMAL, OnNew);
	MAKE_ACTION(OPEN, wxITEM_NORMAL, OnOpen);
	MAKE_ACTION(SAVE, wxITEM_NORMAL, OnSave);
	MAKE_ACTION(SAVE_AS, wxITEM_NORMAL, OnSaveAs);
	MAKE_ACTION(SAVE_AS_BTMAP, wxITEM_NORMAL, OnSaveAsBTMap);
	MAKE_ACTION(GENERATE_MAP, wxITEM_NORMAL, OnGenerateMap);
	MAKE_ACTION(CLOSE, wxITEM_NORMAL, OnClose);

	MAKE_ACTION(IMPORT_MAP, wxITEM_NORMAL, OnImportMap);
	MAKE_ACTION(IMPORT_MONSTERS, wxITEM_NORMAL, OnImportMonsterData);
	MAKE_ACTION(IMPORT_MONSTERSJSON, wxITEM_NORMAL, OnImportMonsterJson);
	MAKE_ACTION(IMPORT_MINIMAP, wxITEM_NORMAL, OnImportMinimap);
	MAKE_ACTION(EXPORT_MINIMAP, wxITEM_NORMAL, OnExportMinimap);
	MAKE_ACTION(EXPORT_TO_OTBM, wxITEM_NORMAL, OnExportToOTBM);

	MAKE_ACTION(RELOAD_DATA, wxITEM_NORMAL, OnReloadDataFiles);
	MAKE_ACTION(RELOAD_BRUSHES, wxITEM_NORMAL, OnReloadBrushes);
	//MAKE_ACTION(RECENT_FILES, wxITEM_NORMAL, OnRecent);
	MAKE_ACTION(PREFERENCES, wxITEM_NORMAL, OnPreferences);
	MAKE_ACTION(CONFIGURE_HOTKEYS, wxITEM_NORMAL, OnConfigureHotkeys);
	MAKE_ACTION(EXIT, wxITEM_NORMAL, OnQuit);

	MAKE_ACTION(UNDO, wxITEM_NORMAL, OnUndo);
	MAKE_ACTION(REDO, wxITEM_NORMAL, OnRedo);

	MAKE_ACTION(FIND_ITEM, wxITEM_NORMAL, OnSearchForItem);
	MAKE_ACTION(REPLACE_ITEMS, wxITEM_NORMAL, OnReplaceItems);
	MAKE_ACTION(SET_ACTION_ID_ON_SELECTION, wxITEM_NORMAL, OnSetActionIdOnSelection);
	MAKE_ACTION(SEARCH_ON_MAP_EVERYTHING, wxITEM_NORMAL, OnSearchForStuffOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_UNIQUE, wxITEM_NORMAL, OnSearchForUniqueOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_ACTION, wxITEM_NORMAL, OnSearchForActionOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_CONTAINER, wxITEM_NORMAL, OnSearchForContainerOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_WRITEABLE, wxITEM_NORMAL, OnSearchForWriteableOnMap);
	MAKE_ACTION(SEARCH_ON_MAP_DUPLICATED_ITEMS, wxITEM_NORMAL, OnSearchForDuplicatedItemsOnMap);
	MAKE_ACTION(SEARCH_ON_SELECTION_EVERYTHING, wxITEM_NORMAL, OnSearchForStuffOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_UNIQUE, wxITEM_NORMAL, OnSearchForUniqueOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_ACTION, wxITEM_NORMAL, OnSearchForActionOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_CONTAINER, wxITEM_NORMAL, OnSearchForContainerOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_WRITEABLE, wxITEM_NORMAL, OnSearchForWriteableOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_ITEM, wxITEM_NORMAL, OnSearchForItemOnSelection);
	MAKE_ACTION(SEARCH_ON_SELECTION_DUPLICATED_ITEMS, wxITEM_NORMAL, OnSearchForDuplicatedItemsOnSelection);
	MAKE_ACTION(REPLACE_ON_SELECTION_ITEMS, wxITEM_NORMAL, OnReplaceItemsOnSelection);
	MAKE_ACTION(REMOVE_ON_SELECTION_ITEM, wxITEM_NORMAL, OnRemoveItemOnSelection);
	MAKE_ACTION(SELECT_MODE_COMPENSATE, wxITEM_RADIO, OnSelectionTypeChange);
	MAKE_ACTION(SELECT_MODE_LOWER, wxITEM_RADIO, OnSelectionTypeChange);
	MAKE_ACTION(SELECT_MODE_CURRENT, wxITEM_RADIO, OnSelectionTypeChange);
	MAKE_ACTION(SELECT_MODE_VISIBLE, wxITEM_RADIO, OnSelectionTypeChange);
	MAKE_ACTION(SELECT_MODE_LASSO, wxITEM_CHECK, OnSelectionLassoToggle);

	MAKE_ACTION(AUTOMAGIC, wxITEM_CHECK, OnToggleAutomagic);
	MAKE_ACTION(USE_GROUND_CARPET_BORDER, wxITEM_CHECK, OnToggleGroundCarpetBorder);
	MAKE_ACTION(CARPET_DONT_INTERFERE_BORDERS, wxITEM_CHECK, OnToggleCarpetDontInterfereBorders);
	MAKE_ACTION(BORDERIZE_SELECTION, wxITEM_NORMAL, OnBorderizeSelection);
	MAKE_ACTION(BORDERIZE_MAP, wxITEM_NORMAL, OnBorderizeMap);
	MAKE_ACTION(RANDOMIZE_SELECTION, wxITEM_NORMAL, OnRandomizeSelection);
	MAKE_ACTION(RANDOMIZE_MAP, wxITEM_NORMAL, OnRandomizeMap);
	MAKE_ACTION(GOTO_PREVIOUS_POSITION, wxITEM_NORMAL, OnGotoPreviousPosition);
	MAKE_ACTION(GOTO_POSITION, wxITEM_NORMAL, OnGotoPosition);
	MAKE_ACTION(JUMP_TO_BRUSH, wxITEM_NORMAL, OnJumpToBrush);
	MAKE_ACTION(JUMP_TO_ITEM_BRUSH, wxITEM_NORMAL, OnJumpToItemBrush);

	MAKE_ACTION(CUT, wxITEM_NORMAL, OnCut);
	MAKE_ACTION(COPY, wxITEM_NORMAL, OnCopy);
	MAKE_ACTION(PASTE, wxITEM_NORMAL, OnPaste);
	MAKE_ACTION(ROTATE_SELECTION_CW, wxITEM_NORMAL, OnRotateSelectionCW);
	MAKE_ACTION(ROTATE_SELECTION_CCW, wxITEM_NORMAL, OnRotateSelectionCCW);
	MAKE_ACTION(ROTATE_SELECTION_180, wxITEM_NORMAL, OnRotateSelection180);

	MAKE_ACTION(EDIT_TOWNS, wxITEM_NORMAL, OnMapEditTowns);
	MAKE_ACTION(EDIT_ITEMS, wxITEM_NORMAL, OnMapEditItems);
	MAKE_ACTION(EDIT_MONSTERS, wxITEM_NORMAL, OnMapEditMonsters);

	MAKE_ACTION(CLEAR_INVALID_HOUSES, wxITEM_NORMAL, OnClearHouseTiles);
	MAKE_ACTION(CLEAR_MODIFIED_STATE, wxITEM_NORMAL, OnClearModifiedState);
	MAKE_ACTION(MAP_REMOVE_ITEMS, wxITEM_NORMAL, OnMapRemoveItems);
	MAKE_ACTION(MAP_REMOVE_CORPSES, wxITEM_NORMAL, OnMapRemoveCorpses);
	MAKE_ACTION(MAP_REMOVE_UNREACHABLE_TILES, wxITEM_NORMAL, OnMapRemoveUnreachable);
	MAKE_ACTION(MAP_REMOVE_EMPTY_SPAWNS, wxITEM_NORMAL, OnMapRemoveEmptySpawns);
	MAKE_ACTION(MAP_CLEANUP, wxITEM_NORMAL, OnMapCleanup);
	MAKE_ACTION(MAP_CLEAN_HOUSE_ITEMS, wxITEM_NORMAL, OnMapCleanHouseItems);
	MAKE_ACTION(MAP_PROPERTIES, wxITEM_NORMAL, OnMapProperties);
	MAKE_ACTION(MAP_STATISTICS, wxITEM_NORMAL, OnMapStatistics);
	MAKE_ACTION(SCAN_REGION, wxITEM_NORMAL, OnScanRegion);

	MAKE_ACTION(VIEW_TOOLBARS_BRUSHES, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(VIEW_TOOLBARS_POSITION, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(VIEW_TOOLBARS_SIZES, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(VIEW_TOOLBARS_INDICATORS, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(VIEW_TOOLBARS_STANDARD, wxITEM_CHECK, OnToolbars);
	MAKE_ACTION(NEW_VIEW, wxITEM_NORMAL, OnNewView);
	MAKE_ACTION(NEW_DETACHED_VIEW, wxITEM_NORMAL, OnNewDetachedView);
	MAKE_ACTION(TOGGLE_FULLSCREEN, wxITEM_NORMAL, OnToggleFullscreen);

	MAKE_ACTION(ZOOM_IN, wxITEM_NORMAL, OnZoomIn);
	MAKE_ACTION(ZOOM_OUT, wxITEM_NORMAL, OnZoomOut);
	MAKE_ACTION(ZOOM_NORMAL, wxITEM_NORMAL, OnZoomNormal);

	MAKE_ACTION(SHOW_SHADE, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_ALL_FLOORS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(GHOST_ITEMS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(GHOST_GROUND_ITEMS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(GHOST_HIGHER_FLOORS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(HIGHLIGHT_ITEMS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_EXTRA, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_INGAME_BOX, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_LIGHTS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SET_LIGHT_HOUR, wxITEM_NORMAL, OnSetLightHour);
	MAKE_ACTION(SHOW_TECHNICAL_ITEMS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_GRID, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_CREATURES, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_CREATURE_IDLE_ANIMATION, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_SPAWNS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_SPAWN_CREATURESLIST, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_SPECIAL, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_AS_MINIMAP, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_ONLY_COLORS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_ONLY_MODIFIED, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_HOUSES, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_PATHING, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_TOOLTIPS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_PREVIEW, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_WALL_HOOKS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_PICKUPABLES, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_MOVEABLES, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_WALL_BORDERS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_MOUNTAIN_OVERLAY, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_STAIR_DIRECTION, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_CAMERA_PATHS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_NPC_PATHS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_CREATURE_WANDER_RADIUS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(ANIMATE_CREATURE_WALK, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_ONLY_GROUNDS, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(SHOW_CHUNK_BOUNDARIES, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(FLOOR_FADING, wxITEM_CHECK, OnChangeViewSettings);
	MAKE_ACTION(FLOOR_FADING_SETTINGS, wxITEM_NORMAL, OnFloorFadingSettings);
	MAKE_ACTION(CAMERA_PLAY_PAUSE, wxITEM_NORMAL, OnCameraPlayPause);
	MAKE_ACTION(CAMERA_ADD_KEYFRAME, wxITEM_NORMAL, OnCameraAddKeyframe);

	MAKE_ACTION(WIN_MINIMAP, wxITEM_NORMAL, OnMinimapWindow);
	MAKE_ACTION(WIN_ACTIONS_HISTORY, wxITEM_NORMAL, OnActionsHistoryWindow);
	MAKE_ACTION(WIN_RECENT_BRUSHES, wxITEM_NORMAL, OnRecentBrushesWindow);
	MAKE_ACTION(WIN_BROWSE_FIELD, wxITEM_NORMAL, OnBrowseFieldWindow);
	MAKE_ACTION(BRUSH_MANAGER, wxITEM_NORMAL, OnBrushManager);
	MAKE_ACTION(STRUCTURE_MANAGER, wxITEM_NORMAL, OnStructureManager);
	MAKE_ACTION(BRUSH_TIPS, wxITEM_NORMAL, OnBrushTipsWindow);
	MAKE_ACTION(AREA_DECORATION, wxITEM_NORMAL, OnAreaDecoration);
	MAKE_ACTION(AREA_CREATURE_SPAWN, wxITEM_NORMAL, OnAreaCreatureSpawn);
	MAKE_ACTION(NEW_PALETTE, wxITEM_NORMAL, OnNewPalette);
	MAKE_ACTION(TAKE_SCREENSHOT, wxITEM_NORMAL, OnTakeScreenshot);
	MAKE_ACTION(TAKE_REGION_SCREENSHOT, wxITEM_NORMAL, OnTakeRegionScreenshot);
	MAKE_ACTION(RECORD_GIF, wxITEM_NORMAL, OnRecordGif);

	MAKE_ACTION(LIVE_START, wxITEM_NORMAL, OnStartLive);
	MAKE_ACTION(LIVE_JOIN, wxITEM_NORMAL, OnJoinLive);
	MAKE_ACTION(LIVE_CLOSE, wxITEM_NORMAL, OnCloseLive);

	MAKE_ACTION(SELECT_TERRAIN, wxITEM_NORMAL, OnSelectTerrainPalette);
	MAKE_ACTION(SELECT_DOODAD, wxITEM_NORMAL, OnSelectDoodadPalette);
	MAKE_ACTION(SELECT_ITEM, wxITEM_NORMAL, OnSelectItemPalette);
	MAKE_ACTION(SELECT_CREATURE, wxITEM_NORMAL, OnSelectCreaturePalette);
	MAKE_ACTION(SELECT_HOUSE, wxITEM_NORMAL, OnSelectHousePalette);
	MAKE_ACTION(SELECT_WAYPOINT, wxITEM_NORMAL, OnSelectWaypointPalette);
	MAKE_ACTION(SELECT_CAMERA_PATH, wxITEM_NORMAL, OnSelectCameraPathPalette);
	MAKE_ACTION(SELECT_RAW, wxITEM_NORMAL, OnSelectRawPalette);

	MAKE_ACTION(FLOOR_0, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_1, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_2, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_3, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_4, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_5, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_6, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_7, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_8, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_9, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_10, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_11, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_12, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_13, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_14, wxITEM_RADIO, OnChangeFloor);
	MAKE_ACTION(FLOOR_15, wxITEM_RADIO, OnChangeFloor);

	MAKE_ACTION(DEBUG_VIEW_DAT, wxITEM_NORMAL, OnDebugViewDat);
	MAKE_ACTION(EXTENSIONS, wxITEM_NORMAL, OnListExtensions);
	MAKE_ACTION(GOTO_WEBSITE, wxITEM_NORMAL, OnGotoWebsite);
	MAKE_ACTION(ABOUT, wxITEM_NORMAL, OnAbout);
	MAKE_ACTION(SHOW_WARNINGS, wxITEM_NORMAL, OnShowWarnings);

	// A deleter, this way the frame does not need
	// to bother deleting us.
	class CustomMenuBar : public wxMenuBar
	{
	public:
		CustomMenuBar(MainMenuBar* mb) : mb(mb) {}
		~CustomMenuBar()
		{
			delete mb;
		}
	private:
		MainMenuBar* mb;
	};

	menubar = newd CustomMenuBar(this);
	frame->SetMenuBar(menubar);

	// Tie all events to this handler!

	for(std::map<std::string, MenuBar::Action*>::iterator ai = actions.begin(); ai != actions.end(); ++ai) {
		frame->Connect(MAIN_FRAME_MENU + ai->second->id, wxEVT_COMMAND_MENU_SELECTED,
			(wxObjectEventFunction)(wxEventFunction)(ai->second->handler), nullptr, this);
	}
	for(int i = 0; i < kRecentFilesLimit; ++i) {
		frame->Connect(recentFiles.GetBaseId() + i, wxEVT_COMMAND_MENU_SELECTED,
			wxCommandEventHandler(MainMenuBar::OnOpenRecent), nullptr, this);
	}
}

MainMenuBar::~MainMenuBar()
{
	// Don't need to delete menubar, it's owned by the frame

	for(std::map<std::string, MenuBar::Action*>::iterator ai = actions.begin(); ai != actions.end(); ++ai) {
		delete ai->second;
	}
}

namespace OnMapRemoveItems
{
	struct RemoveItemCondition
	{
		RemoveItemCondition(uint16_t itemId) :
			itemId(itemId) { }

		uint16_t itemId;

		bool operator()(Map& map, Item* item, int64_t removed, int64_t done) {
			if(done % 0x8000 == 0)
				g_gui.SetLoadDone((uint32_t)(100 * done / map.getTileCount()));
			return item->getID() == itemId && !item->isComplex();
		}
	};
}

void MainMenuBar::EnableItem(MenuBar::ActionID id, bool enable)
{
	std::map<MenuBar::ActionID, std::list<wxMenuItem*> >::iterator fi = items.find(id);
	if(fi == items.end())
		return;

	std::list<wxMenuItem*>& li = fi->second;

	for(std::list<wxMenuItem*>::iterator i = li.begin(); i !=li.end(); ++i)
		(*i)->Enable(enable);
}

void MainMenuBar::CheckItem(MenuBar::ActionID id, bool enable)
{
	std::map<MenuBar::ActionID, std::list<wxMenuItem*> >::iterator fi = items.find(id);
	if(fi == items.end())
		return;

	std::list<wxMenuItem*>& li = fi->second;

	checking_programmaticly = true;
	for(std::list<wxMenuItem*>::iterator i = li.begin(); i !=li.end(); ++i)
		(*i)->Check(enable);
	checking_programmaticly = false;
}

bool MainMenuBar::IsItemChecked(MenuBar::ActionID id) const
{
	std::map<MenuBar::ActionID, std::list<wxMenuItem*> >::const_iterator fi = items.find(id);
	if(fi == items.end())
		return false;

	const std::list<wxMenuItem*>& li = fi->second;

	for(std::list<wxMenuItem*>::const_iterator i = li.begin(); i !=li.end(); ++i)
		if((*i)->IsChecked())
			return true;

	return false;
}

void MainMenuBar::Update()
{
	using namespace MenuBar;
	// This updates all buttons and sets them to proper enabled/disabled state

	bool enable = !g_gui.IsWelcomeDialogShown();
	menubar->Enable(enable);
    if(!enable) {
        return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if(editor) {
		EnableItem(UNDO, editor->canUndo());
		EnableItem(REDO, editor->canRedo());
		EnableItem(PASTE, editor->copybuffer.canPaste());
	} else {
		EnableItem(UNDO, false);
		EnableItem(REDO, false);
		EnableItem(PASTE, false);
	}

	bool loaded = g_gui.IsVersionLoaded();
	bool has_map = editor != nullptr;
	bool has_selection = editor && editor->hasSelection();
	bool is_live = editor && editor->IsLive();
	bool is_host = has_map && !editor->IsLiveClient();
	bool is_local = has_map && !is_live;

	EnableItem(CLOSE, is_local);
	EnableItem(SAVE, is_host);
	EnableItem(SAVE_AS, is_host);
	EnableItem(GENERATE_MAP, false);

	EnableItem(IMPORT_MAP, is_local);
	EnableItem(IMPORT_MONSTERS, is_local);
	EnableItem(IMPORT_MONSTERSJSON, is_local);
	EnableItem(IMPORT_MINIMAP, false);
	EnableItem(EXPORT_MINIMAP, is_local);

	EnableItem(FIND_ITEM, is_host);
	EnableItem(REPLACE_ITEMS, is_local);
	EnableItem(SET_ACTION_ID_ON_SELECTION, has_selection && is_host);
	EnableItem(SEARCH_ON_MAP_EVERYTHING, is_host);
	EnableItem(SEARCH_ON_MAP_UNIQUE, is_host);
	EnableItem(SEARCH_ON_MAP_ACTION, is_host);
	EnableItem(SEARCH_ON_MAP_CONTAINER, is_host);
	EnableItem(SEARCH_ON_MAP_WRITEABLE, is_host);
	EnableItem(SEARCH_ON_MAP_DUPLICATED_ITEMS, is_host);
	EnableItem(SEARCH_ON_SELECTION_EVERYTHING, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_UNIQUE, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_ACTION, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_CONTAINER, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_WRITEABLE, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_DUPLICATED_ITEMS, has_selection && is_host);
	EnableItem(SEARCH_ON_SELECTION_ITEM, has_selection && is_host);
	EnableItem(REPLACE_ON_SELECTION_ITEMS, has_selection && is_host);
	EnableItem(REMOVE_ON_SELECTION_ITEM, has_selection && is_host);

	EnableItem(CUT, has_map);
	EnableItem(COPY, has_map);
	{
		bool canRotateSelection = has_map && has_selection && editor->getSelection().size() >= 2;
		bool structurePasteActive = StructureManagerDialog::CanRotatePaste();
		EnableItem(ROTATE_SELECTION_CW, canRotateSelection || structurePasteActive);
		EnableItem(ROTATE_SELECTION_CCW, canRotateSelection);
		EnableItem(ROTATE_SELECTION_180, canRotateSelection);
	}

	EnableItem(BORDERIZE_SELECTION, has_map && has_selection);
	EnableItem(BORDERIZE_MAP, is_local);
	EnableItem(RANDOMIZE_SELECTION, has_map && has_selection);
	EnableItem(RANDOMIZE_MAP, is_local);

	EnableItem(GOTO_PREVIOUS_POSITION, has_map);
	EnableItem(GOTO_POSITION, has_map);
	EnableItem(JUMP_TO_BRUSH, loaded);
	EnableItem(JUMP_TO_ITEM_BRUSH, loaded);

	EnableItem(MAP_REMOVE_ITEMS, is_host);
	EnableItem(MAP_REMOVE_CORPSES, is_local);
	EnableItem(MAP_REMOVE_UNREACHABLE_TILES, is_local);
	EnableItem(MAP_REMOVE_EMPTY_SPAWNS, is_local);
	EnableItem(CLEAR_INVALID_HOUSES, is_local);
	EnableItem(CLEAR_MODIFIED_STATE, is_local);

	EnableItem(EDIT_TOWNS, is_local);
	EnableItem(EDIT_ITEMS, false);
	EnableItem(EDIT_MONSTERS, false);

	EnableItem(MAP_CLEANUP, is_local);
	EnableItem(MAP_PROPERTIES, is_local);
	EnableItem(MAP_STATISTICS, is_local);
	EnableItem(SCAN_REGION, has_map);

	EnableItem(NEW_VIEW, has_map);
	EnableItem(NEW_DETACHED_VIEW, has_map);
	EnableItem(ZOOM_IN, has_map);
	EnableItem(ZOOM_OUT, has_map);
	EnableItem(ZOOM_NORMAL, has_map);
	EnableItem(TAKE_SCREENSHOT, has_map);
	EnableItem(TAKE_REGION_SCREENSHOT, has_map && has_selection);
	EnableItem(RECORD_GIF, has_map);
	bool allow_multi_floor_selection = g_settings.getBoolean(Config::SHOW_ALL_FLOORS) ||
		g_settings.getBoolean(Config::SELECTION_LASSO);
	EnableItem(SELECT_MODE_VISIBLE, allow_multi_floor_selection);
	EnableItem(SELECT_MODE_LOWER, allow_multi_floor_selection);
	if(!allow_multi_floor_selection) {
		CheckItem(SELECT_MODE_CURRENT, true);
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_CURRENT_FLOOR);
	}

	if(has_map)
		CheckItem(SHOW_SPAWNS, g_settings.getBoolean(Config::SHOW_SPAWNS));

	EnableItem(WIN_MINIMAP, loaded);
	EnableItem(WIN_RECENT_BRUSHES, loaded);
	EnableItem(WIN_BROWSE_FIELD, loaded);
	EnableItem(BRUSH_MANAGER, loaded);
	EnableItem(STRUCTURE_MANAGER, has_map);
	EnableItem(BRUSH_TIPS, true);
	EnableItem(AREA_DECORATION, loaded);
	EnableItem(NEW_PALETTE, loaded);
	EnableItem(SELECT_TERRAIN, loaded);
	EnableItem(SELECT_DOODAD, loaded);
	EnableItem(SELECT_ITEM, loaded);
	EnableItem(SELECT_HOUSE, loaded);
	EnableItem(SELECT_CREATURE, loaded);
	EnableItem(SELECT_WAYPOINT, loaded);
	EnableItem(SELECT_CAMERA_PATH, loaded);
	EnableItem(SELECT_RAW, loaded);

	EnableItem(CAMERA_PLAY_PAUSE, has_map);
	EnableItem(CAMERA_ADD_KEYFRAME, has_map);

	EnableItem(LIVE_START, is_local);
	EnableItem(LIVE_JOIN, loaded);
	EnableItem(LIVE_CLOSE, is_live);

	EnableItem(DEBUG_VIEW_DAT, loaded);

	UpdateFloorMenu();
	UpdateIndicatorsMenu();
}

void MainMenuBar::LoadValues()
{
	using namespace MenuBar;

	CheckItem(VIEW_TOOLBARS_BRUSHES, g_settings.getBoolean(Config::SHOW_TOOLBAR_BRUSHES));
	CheckItem(VIEW_TOOLBARS_POSITION, g_settings.getBoolean(Config::SHOW_TOOLBAR_POSITION));
	CheckItem(VIEW_TOOLBARS_SIZES, g_settings.getBoolean(Config::SHOW_TOOLBAR_SIZES));
	CheckItem(VIEW_TOOLBARS_INDICATORS, g_settings.getBoolean(Config::SHOW_TOOLBAR_INDICATORS));
	CheckItem(VIEW_TOOLBARS_STANDARD, g_settings.getBoolean(Config::SHOW_TOOLBAR_STANDARD));

	CheckItem(SELECT_MODE_COMPENSATE, g_settings.getBoolean(Config::COMPENSATED_SELECT));

	if(IsItemChecked(MenuBar::SELECT_MODE_CURRENT))
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_CURRENT_FLOOR);
	else if(IsItemChecked(MenuBar::SELECT_MODE_LOWER))
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_ALL_FLOORS);
	else if(IsItemChecked(MenuBar::SELECT_MODE_VISIBLE))
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_VISIBLE_FLOORS);

	switch(g_settings.getInteger(Config::SELECTION_TYPE)) {
		case SELECT_CURRENT_FLOOR:
			CheckItem(SELECT_MODE_CURRENT, true);
			break;
		case SELECT_ALL_FLOORS:
			CheckItem(SELECT_MODE_LOWER, true);
			break;
		default:
		case SELECT_VISIBLE_FLOORS:
			CheckItem(SELECT_MODE_VISIBLE, true);
			break;
	}
	CheckItem(SELECT_MODE_LASSO, g_settings.getBoolean(Config::SELECTION_LASSO));

	CheckItem(AUTOMAGIC, g_settings.getBoolean(Config::USE_AUTOMAGIC));
	CheckItem(USE_GROUND_CARPET_BORDER, g_settings.getBoolean(Config::USE_GROUND_CARPET_BORDER));
	CheckItem(CARPET_DONT_INTERFERE_BORDERS, g_settings.getBoolean(Config::CARPET_DONT_INTERFERE_BORDERS));

	CheckItem(SHOW_SHADE, g_settings.getBoolean(Config::SHOW_SHADE));
	CheckItem(SHOW_INGAME_BOX, g_settings.getBoolean(Config::SHOW_INGAME_BOX));
	CheckItem(SHOW_LIGHTS, g_settings.getBoolean(Config::SHOW_LIGHTS));
	CheckItem(SHOW_TECHNICAL_ITEMS, g_settings.getBoolean(Config::SHOW_TECHNICAL_ITEMS));
	CheckItem(SHOW_ALL_FLOORS, g_settings.getBoolean(Config::SHOW_ALL_FLOORS));
	CheckItem(GHOST_ITEMS, g_settings.getBoolean(Config::TRANSPARENT_ITEMS));
	CheckItem(GHOST_GROUND_ITEMS, g_settings.getBoolean(Config::TRANSPARENT_GROUND_ITEMS));
	CheckItem(GHOST_HIGHER_FLOORS, g_settings.getBoolean(Config::TRANSPARENT_FLOORS));
	CheckItem(SHOW_EXTRA, !g_settings.getBoolean(Config::SHOW_EXTRA));
	CheckItem(SHOW_GRID, g_settings.getBoolean(Config::SHOW_GRID));
	CheckItem(HIGHLIGHT_ITEMS, g_settings.getBoolean(Config::HIGHLIGHT_ITEMS));
	CheckItem(SHOW_CREATURES, g_settings.getBoolean(Config::SHOW_CREATURES));
	CheckItem(SHOW_CREATURE_IDLE_ANIMATION, g_settings.getBoolean(Config::SHOW_CREATURE_IDLE_ANIMATION));
	CheckItem(SHOW_SPAWNS, g_settings.getBoolean(Config::SHOW_SPAWNS));
	CheckItem(SHOW_SPAWN_CREATURESLIST, g_settings.getBoolean(Config::SHOW_SPAWN_CREATURESLIST));
	CheckItem(SHOW_SPECIAL, g_settings.getBoolean(Config::SHOW_SPECIAL_TILES));
	CheckItem(SHOW_AS_MINIMAP, g_settings.getBoolean(Config::SHOW_AS_MINIMAP));
	CheckItem(SHOW_ONLY_COLORS, g_settings.getBoolean(Config::SHOW_ONLY_TILEFLAGS));
	CheckItem(SHOW_ONLY_MODIFIED, g_settings.getBoolean(Config::SHOW_ONLY_MODIFIED_TILES));
	CheckItem(SHOW_HOUSES, g_settings.getBoolean(Config::SHOW_HOUSES));
	CheckItem(SHOW_PATHING, g_settings.getBoolean(Config::SHOW_BLOCKING));
	CheckItem(SHOW_TOOLTIPS, g_settings.getBoolean(Config::SHOW_TOOLTIPS));
	CheckItem(SHOW_PREVIEW, g_settings.getBoolean(Config::SHOW_PREVIEW));
	CheckItem(SHOW_WALL_HOOKS, g_settings.getBoolean(Config::SHOW_WALL_HOOKS));
	CheckItem(SHOW_PICKUPABLES, g_settings.getBoolean(Config::SHOW_PICKUPABLES));
	CheckItem(SHOW_MOVEABLES, g_settings.getBoolean(Config::SHOW_MOVEABLES));
	CheckItem(SHOW_CHUNK_BOUNDARIES, g_settings.getBoolean(Config::SHOW_CHUNK_BOUNDARIES));
	CheckItem(FLOOR_FADING, g_settings.getBoolean(Config::FLOOR_FADING));
}

void MainMenuBar::LoadRecentFiles()
{
	recentFiles.Load(g_settings.getConfigObject());
}

void MainMenuBar::SaveRecentFiles()
{
	recentFiles.Save(g_settings.getConfigObject());
}

void MainMenuBar::AddRecentFile(FileName file)
{
	recentFiles.AddFileToHistory(file.GetFullPath());
}

std::vector<wxString> MainMenuBar::GetRecentFiles()
{
    std::vector<wxString> files(recentFiles.GetCount());
    for(size_t i = 0; i < recentFiles.GetCount(); ++i) {
        files[i] = recentFiles.GetHistoryFile(i);
    }
    return files;
}

void MainMenuBar::SetAcceleratorsEnabled(bool enabled)
{
	if(!frame) {
		return;
	}

	if(enabled) {
		RefreshAcceleratorTable();
	} else {
		frame->SetAcceleratorTable(wxAcceleratorTable());
	}
}

bool MainMenuBar::MatchesActionHotkey(MenuBar::ActionID id, const wxKeyEvent& event) const
{
	auto it = menu_hotkeys.find(id);
	if(it == menu_hotkeys.end() || it->second.currentHotkey.empty()) {
		return false;
	}

	HotkeyData configured;
	if(!ParseHotkeyText(it->second.currentHotkey, configured)) {
		return false;
	}

	HotkeyData pressed;
	if(!EventToHotkey(event, pressed)) {
		return false;
	}

	return pressed.flags == configured.flags && pressed.keycode == configured.keycode;
}

void MainMenuBar::UpdateFloorMenu()
{
	using namespace MenuBar;

	if(!g_gui.IsEditorOpen()) {
		return;
	}

	for(int i = 0; i < rme::MapLayers; ++i)
		CheckItem(static_cast<ActionID>(MenuBar::FLOOR_0 + i), false);

	CheckItem(static_cast<ActionID>(MenuBar::FLOOR_0 + g_gui.GetCurrentFloor()), true);
}

void MainMenuBar::UpdateIndicatorsMenu()
{
	using namespace MenuBar;

	if(!g_gui.IsEditorOpen()) {
		return;
	}

	CheckItem(SHOW_WALL_HOOKS, g_settings.getBoolean(Config::SHOW_WALL_HOOKS));
	CheckItem(SHOW_PICKUPABLES, g_settings.getBoolean(Config::SHOW_PICKUPABLES));
	CheckItem(SHOW_MOVEABLES, g_settings.getBoolean(Config::SHOW_MOVEABLES));
	CheckItem(SHOW_WALL_BORDERS, g_settings.getBoolean(Config::SHOW_WALL_BORDERS));
	CheckItem(SHOW_MOUNTAIN_OVERLAY, g_settings.getBoolean(Config::SHOW_MOUNTAIN_OVERLAY));
	CheckItem(SHOW_STAIR_DIRECTION, g_settings.getBoolean(Config::SHOW_STAIR_DIRECTION));
	CheckItem(SHOW_CAMERA_PATHS, g_settings.getBoolean(Config::SHOW_CAMERA_PATHS));
	CheckItem(SHOW_NPC_PATHS, g_settings.getBoolean(Config::SHOW_NPC_PATHS));
	CheckItem(SHOW_CREATURE_WANDER_RADIUS, g_settings.getBoolean(Config::SHOW_CREATURE_WANDER_RADIUS));
	CheckItem(ANIMATE_CREATURE_WALK, g_settings.getBoolean(Config::ANIMATE_CREATURE_WALK));
}

bool MainMenuBar::Load(const FileName& path, wxArrayString& warnings, wxString& error)
{
	// Open the XML file
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(path.GetFullPath().mb_str());
	if(!result) {
		error = "Could not open " + path.GetFullName() + " (file not found or syntax error)";
		return false;
	}

	pugi::xml_node node = doc.child("menubar");
	if(!node) {
		error = path.GetFullName() + ": Invalid rootheader.";
		return false;
	}

	menu_hotkeys.clear();
	base_menu_labels.clear();
	stored_hotkey_overrides.clear();
	LoadHotkeyOverrides();

	// Clear the menu
	while(menubar->GetMenuCount() > 0) {
		menubar->Remove(0);
	}

	// Load succeded
	for(pugi::xml_node menuNode = node.first_child(); menuNode; menuNode = menuNode.next_sibling()) {
		// For each child node, load it
		pugi::xml_attribute attribute = menuNode.attribute("name");
		std::string menuName = attribute ? ReplaceMnemonicMarkers(attribute.as_string()) : std::string();
		std::string topLevelName = StripMenuFormatting(menuName);
		wxObject* i = LoadItem(menuNode, nullptr, topLevelName, warnings, error);
		wxMenu* m = dynamic_cast<wxMenu*>(i);
		if(m) {
			menubar->Append(m, m->GetTitle());
#ifdef __APPLE__
			m->SetTitle(m->GetTitle());
#else
			m->SetTitle("");
#endif
		} else if(i) {
			delete i;
			warnings.push_back(path.GetFullName() + ": Only menus can be subitems of main menu");
		}
	}

	RefreshAcceleratorTable();

	recentFiles.AddFilesToMenu();
	Update();
	LoadValues();
	Theme::ApplyMenu(menubar);
	return true;
}

wxObject* MainMenuBar::LoadItem(pugi::xml_node node, wxMenu* parent, const std::string& topLevel, wxArrayString& warnings, wxString& error)
{
	pugi::xml_attribute attribute;

	const std::string& nodeName = as_lower_str(node.name());
	if(nodeName == "menu") {
		if(!(attribute = node.attribute("name"))) {
			return nullptr;
		}

		std::string name = ReplaceMnemonicMarkers(attribute.as_string());

		wxMenu* menu = newd wxMenu;
		if((attribute = node.attribute("special")) && std::string(attribute.as_string()) == "RECENT_FILES") {
			recentFiles.UseMenu(menu);
		} else {
			std::string nextTopLevel = parent ? topLevel : StripMenuFormatting(name);
			for(pugi::xml_node menuNode = node.first_child(); menuNode; menuNode = menuNode.next_sibling()) {
				// Load an add each item in order
				LoadItem(menuNode, menu, nextTopLevel, warnings, error);
			}
		}

		// If we have a parent, add ourselves.
		// If not, we just return the item and the parent function
		// is responsible for adding us to wherever
		if(parent) {
			parent->AppendSubMenu(menu, wxstr(name));
		} else {
			menu->SetTitle((name));
		}
		return menu;
	} else if(nodeName == "item") {
		// We must have a parent when loading items
		if(!parent) {
			return nullptr;
		} else if(!(attribute = node.attribute("name"))) {
			return nullptr;
		}

		std::string name = ReplaceMnemonicMarkers(attribute.as_string());
		if(!(attribute = node.attribute("action"))) {
			return nullptr;
		}

		const std::string& action = attribute.as_string();
		std::string hotkey = node.attribute("hotkey").as_string();

		const std::string& help = node.attribute("help").as_string();

		auto it = actions.find(action);
		if(it == actions.end()) {
			warnings.push_back("Invalid action type '" + wxstr(action) + "'.");
			return nullptr;
		}

		const MenuBar::Action& act = *it->second;
		MenuBar::ActionID actionId = static_cast<MenuBar::ActionID>(act.id);
		MenuHotkeyEntry& info = menu_hotkeys[actionId];
		if(info.action.empty()) {
			info.id = actionId;
			info.menu = topLevel.empty() ? StripMenuFormatting(name) : topLevel;
			info.action = StripMenuFormatting(name);
			info.defaultHotkey = hotkey;
			info.currentHotkey = ResolveHotkeyValue(actionId, hotkey);
		} else if(info.defaultHotkey.empty() && !hotkey.empty()) {
			info.defaultHotkey = hotkey;
			if(stored_hotkey_overrides.find(actionId) == stored_hotkey_overrides.end()) {
				info.currentHotkey = hotkey;
			}
		}

		wxMenuItem* tmp = parent->Append(
			MAIN_FRAME_MENU + act.id, // ID
			wxstr(BuildMenuLabel(name, info.currentHotkey)), // Title of button
			wxstr(help), // Help text
			act.kind // Kind of item
		);
		base_menu_labels[tmp] = name;
		items[MenuBar::ActionID(act.id)].push_back(tmp);
		return tmp;
	} else if(nodeName == "separator") {
		// We must have a parent when loading items
		if(!parent) {
			return nullptr;
		}
		return parent->AppendSeparator();
	}
	return nullptr;
}

void MainMenuBar::LoadHotkeyOverrides()
{
	const std::string serialized = g_settings.getString(Config::MENU_ACTION_HOTKEYS);
	std::istringstream stream(serialized);
	std::string line;
	while(std::getline(stream, line)) {
		line = TrimString(line);
		if(line.empty())
			continue;
		size_t delimiter = line.find('=');
		if(delimiter == std::string::npos)
			continue;

		std::string actionName = TrimString(line.substr(0, delimiter));
		std::string hotkey = line.substr(delimiter + 1);

		auto it = actions.find(actionName);
		if(it == actions.end())
			continue;

		stored_hotkey_overrides[MenuBar::ActionID(it->second->id)] = hotkey;
	}
}

std::string MainMenuBar::ResolveHotkeyValue(MenuBar::ActionID id, const std::string& defaultHotkey) const
{
	auto it = stored_hotkey_overrides.find(id);
	if(it != stored_hotkey_overrides.end())
		return it->second;
	return defaultHotkey;
}

void MainMenuBar::UpdateMenuItemHotkey(MenuBar::ActionID id)
{
	auto entryIt = menu_hotkeys.find(id);
	if(entryIt == menu_hotkeys.end())
		return;

	auto menuItems = items.find(id);
	if(menuItems == items.end())
		return;

	const std::string& hotkey = entryIt->second.currentHotkey;

	for(wxMenuItem* item : menuItems->second) {
		std::string base;
		auto baseIt = base_menu_labels.find(item);
		if(baseIt != base_menu_labels.end())
			base = baseIt->second;
		else
			base = nstr(item->GetItemLabel());

		item->SetItemLabel(wxstr(BuildMenuLabel(base, hotkey)));
	}
}

void MainMenuBar::UpdateAllMenuLabels()
{
	for(const auto& entry : menu_hotkeys) {
		UpdateMenuItemHotkey(entry.first);
	}
}

void MainMenuBar::RefreshAcceleratorTable()
{
	std::vector<wxAcceleratorEntry> entries;
	for(const auto& pair : menu_hotkeys) {
		const std::string& hotkey = pair.second.currentHotkey;
		if(hotkey.empty())
			continue;

		HotkeyData parsed;
		if(!ParseHotkeyText(hotkey, parsed))
			continue;

		entries.emplace_back(parsed.flags, parsed.keycode, MAIN_FRAME_MENU + pair.first);
	}

	if(entries.empty()) {
		frame->SetAcceleratorTable(wxAcceleratorTable());
	} else {
		frame->SetAcceleratorTable(wxAcceleratorTable(entries.size(), entries.data()));
	}
}

void MainMenuBar::PersistHotkeyOverrides() const
{
	std::ostringstream output;
	for(const auto& pair : menu_hotkeys) {
		const MenuHotkeyEntry& entry = pair.second;
		if(entry.action.empty())
			continue;

		if(entry.currentHotkey == entry.defaultHotkey)
			continue;

		auto actionIt = actions_by_id.find(pair.first);
		if(actionIt == actions_by_id.end() || !actionIt->second)
			continue;

		output << actionIt->second->name << '=' << entry.currentHotkey << '\n';
	}

	g_settings.setString(Config::MENU_ACTION_HOTKEYS, output.str());
}

std::vector<MenuHotkeyEntry> MainMenuBar::GetMenuHotkeys() const
{
	std::vector<MenuHotkeyEntry> entries;
	entries.reserve(menu_hotkeys.size());
	for(const auto& pair : menu_hotkeys) {
		if(pair.second.action.empty())
			continue;
		entries.push_back(pair.second);
	}

	std::sort(entries.begin(), entries.end(), [](const MenuHotkeyEntry& a, const MenuHotkeyEntry& b) {
		if(a.menu == b.menu)
			return a.action < b.action;
		return a.menu < b.menu;
	});

	return entries;
}

void MainMenuBar::ApplyMenuHotkeys(const std::vector<MenuHotkeyEntry>& entries)
{
	for(const auto& entry : entries) {
		auto it = menu_hotkeys.find(entry.id);
		if(it == menu_hotkeys.end())
			continue;
		it->second.currentHotkey = entry.currentHotkey;
	}

	PersistHotkeyOverrides();
	UpdateAllMenuLabels();
	RefreshAcceleratorTable();
}

void MainMenuBar::OnNew(wxCommandEvent& WXUNUSED(event))
{
	g_gui.NewMap();
}

void MainMenuBar::OnGenerateMap(wxCommandEvent& WXUNUSED(event))
{
	/*
	if(!DoQuerySave()) return;

	std::ostringstream os;
	os << "Untitled-" << untitled_counter << ".otbm";
	++untitled_counter;

	editor.generateMap(wxstr(os.str()));

	g_gui.SetStatusText("Generated newd map");

	g_gui.UpdateTitle();
	g_gui.RefreshPalettes();
	g_gui.UpdateMinimap();
	g_gui.FitViewToMap();
	UpdateMenubar();
	Refresh();
	*/
}

void MainMenuBar::OnOpenRecent(wxCommandEvent& event)
{
	FileName fn(recentFiles.GetHistoryFile(event.GetId() - recentFiles.GetBaseId()));
	frame->LoadMap(fn);
}

void MainMenuBar::OnOpen(wxCommandEvent& WXUNUSED(event))
{
	std::vector<wxString> favorites = LoadFavoriteFiles();
	std::vector<wxString> recent;
	for(size_t i = 0; i < recentFiles.GetCount(); ++i) {
		AddUniquePath(recent, recentFiles.GetHistoryFile(i));
	}

	class OpenMapDialog : public wxDialog
	{
	public:
		OpenMapDialog(wxWindow* parent,
			const std::vector<wxString>& favorites,
			const std::vector<wxString>& recent)
			: wxDialog(parent, wxID_ANY, kOpenDialogTitle, wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
			  m_theme(Theme::Dark()),
			  m_list_window(nullptr),
			  m_list_sizer(nullptr),
			  open_button(nullptr),
			  m_selected_item(nullptr),
			  m_recent_files(recent),
			  m_favorite_files(favorites)
		{
			SetBackgroundColour(m_theme.surfaceAlt);
			wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

			m_list_window = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
			m_list_window->SetScrollRate(0, FROM_DIP(this, 8));
			m_list_window->SetBackgroundColour(m_theme.surfaceAlt);
			m_list_sizer = new wxBoxSizer(wxVERTICAL);
			m_list_window->SetSizer(m_list_sizer);
			m_list_window->Bind(WELCOME_DIALOG_FAVORITE, &OpenMapDialog::OnFavoriteToggled, this);

			BuildList();
			root->Add(m_list_window, 1, wxEXPAND | wxALL, FROM_DIP(this, 8));

			wxBoxSizer* button_sizer = new wxBoxSizer(wxHORIZONTAL);
			wxButton* browse_button = new wxButton(this, wxID_ANY, "Browse...");
			wxButton* browse_btmap_button = new wxButton(this, wxID_ANY, "Open BTMap...");
			open_button = new wxButton(this, wxID_OK, "Open");
			wxButton* cancel_button = new wxButton(this, wxID_CANCEL, "Cancel");
			open_button->Enable(false);
			button_sizer->Add(browse_button, 0, wxRIGHT, FROM_DIP(this, 8));
			button_sizer->Add(browse_btmap_button, 0, wxRIGHT, FROM_DIP(this, 8));
			button_sizer->AddStretchSpacer(1);
			button_sizer->Add(open_button, 0, wxRIGHT, FROM_DIP(this, 8));
			button_sizer->Add(cancel_button, 0);
			root->Add(button_sizer, 0, wxEXPAND | wxALL, FROM_DIP(this, 10));

			SetSizerAndFit(root);
			SetSize(FROM_DIP(this, wxSize(800, 600)));
			SetMinSize(FROM_DIP(this, wxSize(800, 600)));
			browse_button->Bind(wxEVT_BUTTON, &OpenMapDialog::OnBrowse, this);
			browse_btmap_button->Bind(wxEVT_BUTTON, &OpenMapDialog::OnBrowseBTMap, this);
		}

		wxString GetSelectedPath() const { return selected_path; }

	private:
		void BuildList()
		{
			m_list_sizer->Clear(true);
			m_selected_item = nullptr;
			auto add_divider = [this]() {
				auto *divider = newd wxPanel(m_list_window, wxID_ANY);
				divider->SetBackgroundColour(m_theme.border);
				divider->SetMinSize(wxSize(-1, FROM_DIP(m_list_window, 1)));
				m_list_sizer->Add(divider, 0, wxEXPAND | wxLEFT | wxRIGHT, FROM_DIP(m_list_window, 8));
			};
			auto add_title = [this, &add_divider](const wxString& title) {
				auto *label = newd wxStaticText(m_list_window, wxID_ANY, title);
				wxFont font = label->GetFont().Bold();
				font.SetPointSize(font.GetPointSize() + 1);
				label->SetFont(font);
				label->SetForegroundColour(m_theme.textMuted);
				m_list_sizer->Add(label, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FROM_DIP(m_list_window, 8));
				add_divider();
			};

			std::vector<wxString> unique_favorites;
			for(const wxString& favorite : m_favorite_files) {
				AddUniquePath(unique_favorites, favorite);
			}
			std::vector<wxString> unique_recent;
			for(const wxString& file : m_recent_files) {
				bool is_favorite = false;
				for(const wxString& favorite : unique_favorites) {
					if(PathEquals(favorite, file)) {
						is_favorite = true;
						break;
					}
				}
				if(!is_favorite) {
					AddUniquePath(unique_recent, file);
				}
			}

			if(!unique_favorites.empty()) {
				add_title("Favorites");
				for(const wxString& file : unique_favorites) {
					auto *recent_item = newd RecentItem(m_list_window, m_theme, file, true);
					m_list_sizer->Add(recent_item, 0, wxEXPAND);
					recent_item->Bind(wxEVT_LEFT_UP, &OpenMapDialog::OnRecentItemClicked, this);
					if(PathEquals(file, selected_path)) {
						recent_item->SetSelected(true);
						m_selected_item = recent_item;
					}
					add_divider();
				}
			}

			if(!unique_recent.empty()) {
				add_title("Recent Maps");
				for(const wxString& file : unique_recent) {
					auto *recent_item = newd RecentItem(m_list_window, m_theme, file, false);
					m_list_sizer->Add(recent_item, 0, wxEXPAND);
					recent_item->Bind(wxEVT_LEFT_UP, &OpenMapDialog::OnRecentItemClicked, this);
					if(PathEquals(file, selected_path)) {
						recent_item->SetSelected(true);
						m_selected_item = recent_item;
					}
					add_divider();
				}
			}

			m_list_window->Layout();
			m_list_window->FitInside();
		}

		void OnRecentItemClicked(wxMouseEvent& event)
		{
			auto *recent_item = dynamic_cast<RecentItem*>(event.GetEventObject());
			if(!recent_item) {
				return;
			}
			if(m_selected_item && m_selected_item != recent_item) {
				m_selected_item->SetSelected(false);
			}
			recent_item->SetSelected(true);
			m_selected_item = recent_item;
			selected_path = recent_item->GetText();
			open_button->Enable(!selected_path.empty());
		}

		void OnFavoriteToggled(wxCommandEvent& event)
		{
			wxString path = event.GetString();
			if(event.GetInt() != 0) {
				AddUniquePath(m_favorite_files, path);
			} else {
				m_favorite_files.erase(
					std::remove_if(m_favorite_files.begin(), m_favorite_files.end(),
						[&path](const wxString& current) { return PathEquals(current, path); }),
					m_favorite_files.end());
			}
			SaveFavoriteFiles(m_favorite_files);
			BuildList();
		}

		void OnBrowse(wxCommandEvent& WXUNUSED(event))
		{
			wxString wildcard = g_settings.getInteger(Config::USE_OTGZ) != 0 ?
				MAP_LOAD_FILE_WILDCARD_OTGZ : MAP_LOAD_FILE_WILDCARD;
			wxFileDialog dialog(this, "Open map file", wxEmptyString, wxEmptyString, wildcard,
				wxFD_OPEN | wxFD_FILE_MUST_EXIST);
			if(dialog.ShowModal() == wxID_OK) {
				selected_path = dialog.GetPath();
				EndModal(wxID_OK);
			}
		}

		void OnBrowseBTMap(wxCommandEvent& WXUNUSED(event))
		{
			wxDirDialog dialog(this, "Open BTMap directory", "",
				wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
			if(dialog.ShowModal() == wxID_OK) {
				wxString dirPath = dialog.GetPath();
				// Verify it's a valid .btmap directory
				if(!dirPath.EndsWith(".btmap")) {
					wxMessageBox("Please select a directory ending with .btmap", "Invalid BTMap Directory",
						wxOK | wxICON_WARNING, this);
					return;
				}
				selected_path = dirPath;
				EndModal(wxID_OK);
			}
		}

		ThemeColors m_theme;
		wxScrolledWindow* m_list_window;
		wxBoxSizer* m_list_sizer;
		wxButton* open_button;
		RecentItem* m_selected_item;
		wxString selected_path;
		std::vector<wxString> m_recent_files;
		std::vector<wxString> m_favorite_files;
	};

	OpenMapDialog dialog(frame, favorites, recent);
	if(dialog.ShowModal() == wxID_OK) {
		wxString path = dialog.GetSelectedPath();
		if(!path.empty()) {
			frame->LoadMap(FileName(path));
		}
	}
}

void MainMenuBar::OnClose(wxCommandEvent& WXUNUSED(event))
{
	frame->DoQuerySave(true); // It closes the editor too
}

void MainMenuBar::OnSave(wxCommandEvent& WXUNUSED(event))
{
	g_gui.SaveMap();
}

void MainMenuBar::OnSaveAs(wxCommandEvent& WXUNUSED(event))
{
	g_gui.SaveMapAs();
}

void MainMenuBar::OnSaveAsBTMap(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	wxDirDialog dialog(frame, "Choose directory for BTMap save", "",
		wxDD_DEFAULT_STYLE);

	if(dialog.ShowModal() == wxID_OK) {
		wxString dirPath = dialog.GetPath();

		// Append .btmap extension if not present
		if(!dirPath.EndsWith(".btmap")) {
			dirPath += ".btmap";
		}

		g_gui.SaveCurrentMap(FileName(dirPath), true);
		g_gui.UpdateTitle();
	}
}

void MainMenuBar::OnExportToOTBM(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	wxFileDialog dialog(frame, "Export to OTBM", "", "",
		"OpenTibia Binary Map (*.otbm)|*.otbm",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

	if(dialog.ShowModal() == wxID_OK) {
		g_gui.CreateLoadBar("Exporting to OTBM...");
		bool success = IOMapBTMap::exportToOTBM(
			g_gui.GetCurrentMap(),
			FileName(dialog.GetPath())
		);
		g_gui.DestroyLoadBar();

		if(!success) {
			g_gui.PopupDialog("Error", "Could not export to OTBM.", wxOK);
		} else {
			g_gui.PopupDialog("Success", "Map exported to OTBM successfully!", wxOK);
		}
	}
}

void MainMenuBar::OnPreferences(wxCommandEvent& WXUNUSED(event))
{
	PreferencesWindow dialog(frame);
	dialog.ShowModal();
	dialog.Destroy();
}

void MainMenuBar::OnConfigureHotkeys(wxCommandEvent& WXUNUSED(event))
{
	HotkeysDialog dialog(frame, *this);
	dialog.ShowModal();
}

void MainMenuBar::OnQuit(wxCommandEvent& WXUNUSED(event))
{
	/*
	while(g_gui.IsEditorOpen())
		if(!frame->DoQuerySave(true))
			return;
			*/
	//((Application*)wxTheApp)->Unload();
	g_gui.root->Close();
}

void MainMenuBar::OnImportMap(wxCommandEvent& WXUNUSED(event))
{
	ASSERT(g_gui.GetCurrentEditor());
	wxDialog* importmap = newd ImportMapWindow(frame, *g_gui.GetCurrentEditor());
	importmap->ShowModal();
}

void MainMenuBar::OnImportMonsterData(wxCommandEvent& WXUNUSED(event))
{
	wxFileDialog dlg(g_gui.root, "Import monster/npc file", "","","*.xml", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
	if(dlg.ShowModal() == wxID_OK) {
		wxArrayString paths;
		dlg.GetPaths(paths);
		for(uint32_t i = 0; i < paths.GetCount(); ++i) {
			wxString error;
			wxArrayString warnings;
			bool ok = g_creatures.importXMLFromOT(FileName(paths[i]), error, warnings);
			if(ok)
				g_gui.ListDialog("Monster loader errors", warnings);
			else
				wxMessageBox("Error OT data file \"" + paths[i] + "\".\n" + error, "Error", wxOK | wxICON_INFORMATION, g_gui.root);
		}
	}
}

void MainMenuBar::OnImportMonsterJson(wxCommandEvent& WXUNUSED(event))
{
	wxFileDialog dlg(g_gui.root, "Import monster/npc JSON file", "", "", "*.json", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);

	if(dlg.ShowModal() == wxID_OK) {
		wxArrayString paths;
		dlg.GetPaths(paths);
		for(uint32_t i = 0; i < paths.GetCount(); ++i) {
			wxString error;
			wxArrayString warnings;
			bool ok = g_creatures.loadFromJSON(FileName(paths[i]), false, error, warnings);
			if(ok) {
				if(!warnings.empty())
					g_gui.ListDialog("Monster JSON loader warnings", warnings);
				else
					wxMessageBox("Monsters imported successfully from \"" + paths[i] + "\".", "Success", wxOK | wxICON_INFORMATION, g_gui.root);
			} else {
				wxMessageBox("Error loading JSON monster file \"" + paths[i] + "\".\n" + error, "Error", wxOK | wxICON_ERROR, g_gui.root);
			}
		}
	}
}


void MainMenuBar::OnImportMinimap(wxCommandEvent& WXUNUSED(event))
{
	ASSERT(g_gui.IsEditorOpen());
	//wxDialog* importmap = newd ImportMapWindow();
	//importmap->ShowModal();
}

void MainMenuBar::OnExportMinimap(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen()) {
		return;
	}

	ExportMiniMapWindow dialog(frame, *g_gui.GetCurrentEditor());
	dialog.ShowModal();
}

void MainMenuBar::OnDebugViewDat(wxCommandEvent& WXUNUSED(event))
{
	wxDialog dlg(frame, wxID_ANY, "Debug .dat file", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	new DatDebugView(&dlg);
	dlg.ShowModal();
}

void MainMenuBar::OnReloadDataFiles(wxCommandEvent& WXUNUSED(event))
{
	wxString error;
	wxArrayString warnings;
	g_gui.LoadVersion(g_gui.GetCurrentVersionID(), error, warnings, true);
	g_gui.PopupDialog("Error", error, wxOK);
	g_gui.ListDialog("Warnings", warnings);
}

void MainMenuBar::OnReloadBrushes(wxCommandEvent& WXUNUSED(event))
{
	wxString error;
	wxArrayString warnings;
	if(!g_gui.ReloadBrushes(error, warnings)) {
		g_gui.PopupDialog("Error", error, wxOK);
	}
	if(!warnings.IsEmpty()) {
		g_gui.ListDialog("Warnings", warnings);
	}
}

void MainMenuBar::OnListExtensions(wxCommandEvent& WXUNUSED(event))
{
	ExtensionsDialog exts(frame);
	exts.ShowModal();
}

void MainMenuBar::OnGotoWebsite(wxCommandEvent& WXUNUSED(event))
{
	::wxLaunchDefaultBrowser("http://www.remeresmapeditor.com/",  wxBROWSER_NEW_WINDOW);
}

void MainMenuBar::OnAbout(wxCommandEvent& WXUNUSED(event))
{
	AboutWindow about(frame);
	about.ShowModal();
}

void MainMenuBar::OnShowWarnings(wxCommandEvent& WXUNUSED(event))
{
	g_gui.ShowStartupWarnings();
}

void MainMenuBar::OnUndo(wxCommandEvent& WXUNUSED(event))
{
	g_gui.DoUndo();
}

void MainMenuBar::OnRedo(wxCommandEvent& WXUNUSED(event))
{
	g_gui.DoRedo();
}

namespace OnSearchForItem
{
	struct Finder
	{
		Finder(uint16_t itemId, uint32_t maxCount) :
			itemId(itemId), maxCount(maxCount) {}

		uint16_t itemId;
		uint32_t maxCount;
		std::vector< std::pair<Tile*, Item*> > result;

		bool limitReached() const { return result.size() >= (size_t)maxCount; }

		void operator()(Map& map, Tile* tile, Item* item, long long done)
		{
			if(result.size() >= (size_t)maxCount)
				return;

			if(done % 0x8000 == 0)
				g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));

			if(item->getID() == itemId)
				result.push_back(std::make_pair(tile, item));
		}
	};
}

void MainMenuBar::OnSearchForItem(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	FindItemDialog dialog(frame, "Search for Item");
	dialog.setSearchMode((FindItemDialog::SearchMode)g_settings.getInteger(Config::FIND_ITEM_MODE));
	if(dialog.ShowModal() == wxID_OK) {
		OnSearchForItem::Finder finder(dialog.getResultID(), (uint32_t)g_settings.getInteger(Config::REPLACE_SIZE));
		g_gui.CreateLoadBar("Searching map...");

		foreach_ItemOnMap(g_gui.GetCurrentMap(), finder, false);
		std::vector< std::pair<Tile*, Item*> >& result = finder.result;

		g_gui.DestroyLoadBar();

		if(finder.limitReached()) {
			wxString msg;
			msg << "The configured limit has been reached. Only " << finder.maxCount << " results will be displayed.";
			g_gui.PopupDialog("Notice", msg, wxOK);
		}

		SearchResultWindow* window = g_gui.ShowSearchWindow();
		window->Clear();
		for(std::vector<std::pair<Tile*, Item*> >::const_iterator iter = result.begin(); iter != result.end(); ++iter) {
			Tile* tile = iter->first;
			Item* item = iter->second;
			window->AddPosition(wxstr(item->getName()), tile->getPosition());
		}

		g_settings.setInteger(Config::FIND_ITEM_MODE, (int)dialog.getSearchMode());
	}
	dialog.Destroy();
}

void MainMenuBar::OnReplaceItems(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsVersionLoaded())
		return;

	if(MapTab* tab = g_gui.GetCurrentMapTab()) {
		if(MapWindow* window = tab->GetView()) {
			window->ShowAdvancedReplaceDialog();
		}
	}
}

void MainMenuBar::OnSetActionIdOnSelection(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen()) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if(!editor || !editor->hasSelection()) {
		return;
	}

	wxArrayString scopeChoices;
	scopeChoices.Add("Items on selected tiles (excluding ground)");
	scopeChoices.Add("Ground on selected tiles");
	scopeChoices.Add("Items and ground on selected tiles");

	wxSingleChoiceDialog scopeDialog(
		frame,
		"Choose what should receive the Action ID in the current selection.",
		"Set Action ID on Selection",
		scopeChoices
	);
	scopeDialog.SetSelection(0);
	if(scopeDialog.ShowModal() != wxID_OK) {
		return;
	}

	const int scopeSelection = scopeDialog.GetSelection();
	const bool applyToItems = scopeSelection == 0 || scopeSelection == 2;
	const bool applyToGround = scopeSelection == 1 || scopeSelection == 2;

	static int lastActionId = rme::MinActionId;
	wxNumberEntryDialog actionDialog(
		frame,
		"Enter the Action ID to apply (0 clears Action ID).",
		"Action ID:",
		"Set Action ID on Selection",
		lastActionId,
		0,
		rme::MaxActionId
	);
	if(actionDialog.ShowModal() != wxID_OK) {
		return;
	}

	const int enteredActionId = actionDialog.GetValue();
	if(enteredActionId != 0 && (enteredActionId < rme::MinActionId || enteredActionId > rme::MaxActionId)) {
		const wxString message = wxString::Format(
			"Action ID must be between %d and %d (or 0 to clear).",
			rme::MinActionId,
			rme::MaxActionId);
		g_gui.PopupDialog("Error", message, wxOK);
		return;
	}

	lastActionId = enteredActionId;
	const uint16_t actionId = static_cast<uint16_t>(enteredActionId);

	struct ApplySelectionActionId
	{
		Action* action = nullptr;
		uint16_t actionId = 0;
		bool applyToItems = false;
		bool applyToGround = false;
		size_t changedTiles = 0;
		size_t changedEntries = 0;

		void operator()(Map& map, Tile* tile, long long done)
		{
			(void)done;

			if(!tile || !tile->isSelected()) {
				return;
			}

			Tile* newTile = nullptr;
			bool tileChanged = false;

			if(applyToGround && tile->ground && tile->ground->getActionID() != actionId) {
				if(!newTile) {
					newTile = tile->deepCopy(map);
				}
				newTile->ground->setActionID(actionId);
				++changedEntries;
				tileChanged = true;
			}

			if(applyToItems) {
				for(size_t i = 0; i < tile->items.size(); ++i) {
					Item* oldItem = tile->items[i];
					if(!oldItem || oldItem->getActionID() == actionId) {
						continue;
					}

					if(!newTile) {
						newTile = tile->deepCopy(map);
					}

					Item* newItem = newTile->items[i];
					if(!newItem) {
						continue;
					}

					newItem->setActionID(actionId);
					++changedEntries;
					tileChanged = true;
				}
			}

			if(tileChanged) {
				action->addChange(new Change(newTile));
				++changedTiles;
			} else if(newTile) {
				delete newTile;
			}
		}
	};

	BatchAction* batch = editor->createBatch(ACTION_CHANGE_PROPERTIES);
	Action* action = editor->createAction(batch);
	ApplySelectionActionId applier;
	applier.action = action;
	applier.actionId = actionId;
	applier.applyToItems = applyToItems;
	applier.applyToGround = applyToGround;

	foreach_TileOnMap(editor->getMap(), applier);

	batch->addAndCommitAction(action);
	editor->addBatch(batch);
	editor->updateActions();

	if(applier.changedTiles == 0) {
		g_gui.PopupDialog("Set Action ID on Selection", "No selected entries required changes.", wxOK);
		return;
	}

	wxString message;
	message << "Updated " << applier.changedEntries << " entr";
	message << (applier.changedEntries == 1 ? "y" : "ies");
	message << " on " << applier.changedTiles << " tile";
	message << (applier.changedTiles == 1 ? "." : "s.");
	g_gui.PopupDialog("Set Action ID on Selection", message, wxOK);
	g_gui.RefreshView();
}

namespace OnSearchForStuff
{
	struct Searcher
	{
		Searcher() :
			search_unique(false),
			search_action(false),
			search_container(false),
			search_writeable(false) {}

		bool search_unique;
		bool search_action;
		bool search_container;
		bool search_writeable;
		std::vector<std::pair<Tile*, Item*> > found;

		void operator()(Map& map, Tile* tile, Item* item, long long done)
		{
			if(done % 0x8000 == 0) {
				g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));
			}
			Container* container;
			if((search_unique && item->getUniqueID() > 0) ||
				(search_action && item->getActionID() > 0) ||
				(search_container && ((container = dynamic_cast<Container*>(item)) && container->getItemCount())) ||
				(search_writeable && item->getText().length() > 0)) {
				found.push_back(std::make_pair(tile, item));
			}
		}

		wxString desc(Item* item)
		{
			wxString label;
			if(item->getUniqueID() > 0)
				label << "UID:" << item->getUniqueID() << " ";

			if(item->getActionID() > 0)
				label << "AID:" << item->getActionID() << " ";

			label << wxstr(item->getName());

			if(dynamic_cast<Container*>(item))
				label << " (Container) ";

			if(item->getText().length() > 0)
				label << " (Text: " << wxstr(item->getText()) << ") ";

			return label;
		}

		void sort()
		{
			if(search_unique || search_action)
				std::sort(found.begin(), found.end(), Searcher::compare);
		}

		static bool compare(const std::pair<Tile*, Item*>& pair1, const std::pair<Tile*, Item*>& pair2)
		{
			const Item* item1 = pair1.second;
			const Item* item2 = pair2.second;

			if(item1->getActionID() != 0 || item2->getActionID() != 0)
				return item1->getActionID() < item2->getActionID();
			else if(item1->getUniqueID() != 0 || item2->getUniqueID() != 0)
				return item1->getUniqueID() < item2->getUniqueID();

			return false;
		}
	};
}

void MainMenuBar::OnSearchForStuffOnMap(wxCommandEvent& WXUNUSED(event))
{
	SearchItems(true, true, true, true);
}

void MainMenuBar::OnSearchForUniqueOnMap(wxCommandEvent& WXUNUSED(event))
{
	SearchItems(true, false, false, false);
}

void MainMenuBar::OnSearchForActionOnMap(wxCommandEvent& WXUNUSED(event))
{
	SearchItems(false, true, false, false);
}

void MainMenuBar::OnSearchForContainerOnMap(wxCommandEvent& WXUNUSED(event))
{
	SearchItems(false, false, true, false);
}

void MainMenuBar::OnSearchForWriteableOnMap(wxCommandEvent& WXUNUSED(event))
{
	SearchItems(false, false, false, true);
}

void MainMenuBar::OnSearchForDuplicatedItemsOnMap(wxCommandEvent& WXUNUSED(event))
{
	SearchDuplicatedItems(false);
}

void MainMenuBar::OnSearchForStuffOnSelection(wxCommandEvent& WXUNUSED(event))
{
	SearchItems(true, true, true, true, true);
}

void MainMenuBar::OnSearchForUniqueOnSelection(wxCommandEvent& WXUNUSED(event))
{
	SearchItems(true, false, false, false, true);
}

void MainMenuBar::OnSearchForActionOnSelection(wxCommandEvent& WXUNUSED(event))
{
	SearchItems(false, true, false, false, true);
}

void MainMenuBar::OnSearchForContainerOnSelection(wxCommandEvent& WXUNUSED(event))
{
	SearchItems(false, false, true, false, true);
}

void MainMenuBar::OnSearchForWriteableOnSelection(wxCommandEvent& WXUNUSED(event))
{
	SearchItems(false, false, false, true, true);
}

void MainMenuBar::OnSearchForItemOnSelection(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	FindItemDialog dialog(frame, "Search on Selection");
	dialog.setSearchMode((FindItemDialog::SearchMode)g_settings.getInteger(Config::FIND_ITEM_MODE));
	if(dialog.ShowModal() == wxID_OK) {
		OnSearchForItem::Finder finder(dialog.getResultID(), (uint32_t)g_settings.getInteger(Config::REPLACE_SIZE));
		g_gui.CreateLoadBar("Searching on selected area...");

		foreach_ItemOnMap(g_gui.GetCurrentMap(), finder, true);
		std::vector<std::pair<Tile*, Item*> >& result = finder.result;

		g_gui.DestroyLoadBar();

		if(finder.limitReached()) {
			wxString msg;
			msg << "The configured limit has been reached. Only " << finder.maxCount << " results will be displayed.";
			g_gui.PopupDialog("Notice", msg, wxOK);
		}

		SearchResultWindow* window = g_gui.ShowSearchWindow();
		window->Clear();
		for(std::vector<std::pair<Tile*, Item*> >::const_iterator iter = result.begin(); iter != result.end(); ++iter) {
			Tile* tile = iter->first;
			Item* item = iter->second;
			window->AddPosition(wxstr(item->getName()), tile->getPosition());
		}

		g_settings.setInteger(Config::FIND_ITEM_MODE, (int)dialog.getSearchMode());
	}

	dialog.Destroy();
}

void MainMenuBar::OnSearchForDuplicatedItemsOnSelection(wxCommandEvent& WXUNUSED(event))
{
	SearchDuplicatedItems(true);
}

void MainMenuBar::OnReplaceItemsOnSelection(wxCommandEvent& event)
{
	// Now opens the same unified Replace Items dialog
	OnReplaceItems(event);
}

void MainMenuBar::OnRemoveItemOnSelection(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	FindItemDialog dialog(frame, "Remove Item on Selection");
	if(dialog.ShowModal() == wxID_OK) {
		g_gui.GetCurrentEditor()->clearActions();
		g_gui.CreateLoadBar("Searching item on selection to remove...");
		OnMapRemoveItems::RemoveItemCondition condition(dialog.getResultID());
		int64_t count = RemoveItemOnMap(g_gui.GetCurrentMap(), condition, true);
		g_gui.DestroyLoadBar();

		wxString msg;
		msg << count << " items removed.";
		g_gui.PopupDialog("Remove Item", msg, wxOK);
		g_gui.GetCurrentMap().doChange();
		g_gui.RefreshView();
	}
	dialog.Destroy();
}

void MainMenuBar::OnSelectionTypeChange(wxCommandEvent& WXUNUSED(event))
{
	g_settings.setInteger(Config::COMPENSATED_SELECT, IsItemChecked(MenuBar::SELECT_MODE_COMPENSATE));

	if(IsItemChecked(MenuBar::SELECT_MODE_CURRENT))
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_CURRENT_FLOOR);
	else if(IsItemChecked(MenuBar::SELECT_MODE_LOWER))
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_ALL_FLOORS);
	else if(IsItemChecked(MenuBar::SELECT_MODE_VISIBLE))
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_VISIBLE_FLOORS);
}

void MainMenuBar::OnSelectionLassoToggle(wxCommandEvent& WXUNUSED(event))
{
	g_settings.setInteger(Config::SELECTION_LASSO, IsItemChecked(MenuBar::SELECT_MODE_LASSO));
	bool allow_multi_floor_selection = IsItemChecked(MenuBar::SHOW_ALL_FLOORS) ||
		g_settings.getBoolean(Config::SELECTION_LASSO);
	EnableItem(MenuBar::SELECT_MODE_VISIBLE, allow_multi_floor_selection);
	EnableItem(MenuBar::SELECT_MODE_LOWER, allow_multi_floor_selection);
	if(!allow_multi_floor_selection) {
		CheckItem(MenuBar::SELECT_MODE_CURRENT, true);
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_CURRENT_FLOOR);
	}
	g_gui.RefreshView();
}

void MainMenuBar::OnCopy(wxCommandEvent& WXUNUSED(event))
{
	g_gui.DoCopy();
}

void MainMenuBar::OnCut(wxCommandEvent& WXUNUSED(event))
{
	g_gui.DoCut();
}

void MainMenuBar::OnPaste(wxCommandEvent& WXUNUSED(event))
{
	g_gui.PreparePaste();
}

void MainMenuBar::OnRotateSelectionCW(wxCommandEvent& WXUNUSED(event))
{
	if(StructureManagerDialog::RotatePaste()) {
		return;
	}

	if(!g_gui.IsEditorOpen()) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if(!editor || !editor->hasSelection() || editor->getSelection().size() < 2) {
		return;
	}

	editor->rotateSelection(1);
	g_gui.RefreshView();
}

void MainMenuBar::OnRotateSelectionCCW(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen()) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if(!editor || !editor->hasSelection() || editor->getSelection().size() < 2) {
		return;
	}

	editor->rotateSelection(3);
	g_gui.RefreshView();
}

void MainMenuBar::OnRotateSelection180(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen()) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if(!editor || !editor->hasSelection() || editor->getSelection().size() < 2) {
		return;
	}

	editor->rotateSelection(2);
	g_gui.RefreshView();
}

void MainMenuBar::OnToggleAutomagic(wxCommandEvent& WXUNUSED(event))
{
	g_settings.setInteger(Config::USE_AUTOMAGIC, IsItemChecked(MenuBar::AUTOMAGIC));
	g_settings.setInteger(Config::BORDER_IS_GROUND, IsItemChecked(MenuBar::AUTOMAGIC));
	if(g_settings.getInteger(Config::USE_AUTOMAGIC))
		g_gui.SetStatusText("Automagic enabled.");
	else
		g_gui.SetStatusText("Automagic disabled.");
}

void MainMenuBar::OnToggleGroundCarpetBorder(wxCommandEvent& WXUNUSED(event))
{
	g_settings.setInteger(Config::USE_GROUND_CARPET_BORDER, IsItemChecked(MenuBar::USE_GROUND_CARPET_BORDER));
	if(g_settings.getInteger(Config::USE_GROUND_CARPET_BORDER))
		g_gui.SetStatusText("Ground carpet border enabled.");
	else
		g_gui.SetStatusText("Ground carpet border disabled.");
}

void MainMenuBar::OnToggleCarpetDontInterfereBorders(wxCommandEvent& WXUNUSED(event))
{
	g_settings.setInteger(Config::CARPET_DONT_INTERFERE_BORDERS, IsItemChecked(MenuBar::CARPET_DONT_INTERFERE_BORDERS));
	if(g_settings.getInteger(Config::CARPET_DONT_INTERFERE_BORDERS))
		g_gui.SetStatusText("Carpet don't interfere borders enabled.");
	else
		g_gui.SetStatusText("Carpet don't interfere borders disabled.");
}

void MainMenuBar::OnBorderizeSelection(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	g_gui.GetCurrentEditor()->borderizeSelection();
	g_gui.RefreshView();
}

void MainMenuBar::OnBorderizeMap(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	int ret = g_gui.PopupDialog("Borderize Map", "Are you sure you want to borderize the entire map (this action cannot be undone)?", wxYES | wxNO);
	if(ret == wxID_YES)
		g_gui.GetCurrentEditor()->borderizeMap(true);

	g_gui.RefreshView();
}

void MainMenuBar::OnRandomizeSelection(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	g_gui.GetCurrentEditor()->randomizeSelection();
	g_gui.RefreshView();
}

void MainMenuBar::OnRandomizeMap(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	int ret = g_gui.PopupDialog("Randomize Map", "Are you sure you want to randomize the entire map (this action cannot be undone)?", wxYES | wxNO);
	if(ret == wxID_YES)
		g_gui.GetCurrentEditor()->randomizeMap(true);

	g_gui.RefreshView();
}

void MainMenuBar::OnJumpToBrush(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsVersionLoaded())
		return;

	// Create the jump to dialog
	FindDialog* dlg = newd FindBrushDialog(frame);

	// Display dialog to user
	dlg->ShowModal();

	// Retrieve result, if null user canceled
	const Brush* brush = dlg->getResult();
	if(brush) {
		g_gui.SelectBrush(brush, TILESET_UNKNOWN);
	}
	delete dlg;
}

void MainMenuBar::OnJumpToItemBrush(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsVersionLoaded())
		return;

	// Create the jump to dialog
	FindItemDialog dialog(frame, "Jump to Item");
	dialog.setSearchMode((FindItemDialog::SearchMode)g_settings.getInteger(Config::JUMP_TO_ITEM_MODE));
	if(dialog.ShowModal() == wxID_OK) {
		// Retrieve result, if null user canceled
		const Brush* brush = dialog.getResult();
		if(brush)
			g_gui.SelectBrush(brush, TILESET_RAW);
		g_settings.setInteger(Config::JUMP_TO_ITEM_MODE, (int)dialog.getSearchMode());
	}
	dialog.Destroy();
}

void MainMenuBar::OnGotoPreviousPosition(wxCommandEvent& WXUNUSED(event))
{
	MapTab* mapTab = g_gui.GetCurrentMapTab();
	if(mapTab)
		mapTab->GoToPreviousCenterPosition();
}

void MainMenuBar::OnGotoPosition(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	// Display dialog, it also controls the actual jump
	GotoPositionDialog dlg(frame, *g_gui.GetCurrentEditor());
	dlg.ShowModal();
}

void MainMenuBar::OnMapRemoveItems(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	FindItemDialog dialog(frame, "Item Type to Remove");
	if(dialog.ShowModal() == wxID_OK) {
		uint16_t itemid = dialog.getResultID();

		g_gui.GetCurrentEditor()->getSelection().clear();
		g_gui.GetCurrentEditor()->clearActions();

		OnMapRemoveItems::RemoveItemCondition condition(itemid);
		g_gui.CreateLoadBar("Searching map for items to remove...");

		int64_t count = RemoveItemOnMap(g_gui.GetCurrentMap(), condition, false);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << count << " items deleted.";

		g_gui.PopupDialog("Search completed", msg, wxOK);
		g_gui.GetCurrentMap().doChange();
		g_gui.RefreshView();
	}
	dialog.Destroy();
}

namespace OnMapRemoveCorpses
{
	struct condition
	{
		condition() {}

		bool operator()(Map& map, Item* item, long long removed, long long done){
			if(done % 0x800 == 0)
				g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));

			return g_materials.isInTileset(item, "Corpses") & !item->isComplex();
		}
	};
}

void MainMenuBar::OnMapRemoveCorpses(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	int ok = g_gui.PopupDialog("Remove Corpses", "Do you want to remove all corpses from the map?", wxYES | wxNO);

	if(ok == wxID_YES) {
		g_gui.GetCurrentEditor()->getSelection().clear();
		g_gui.GetCurrentEditor()->clearActions();

		OnMapRemoveCorpses::condition func;
		g_gui.CreateLoadBar("Searching map for items to remove...");

		int64_t count = RemoveItemOnMap(g_gui.GetCurrentMap(), func, false);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << count << " items deleted.";
		g_gui.PopupDialog("Search completed", msg, wxOK);
		g_gui.GetCurrentMap().doChange();
	}
}

namespace OnMapRemoveUnreachable
{
	struct condition
	{
		condition() {}

		bool isReachable(Tile* tile)
		{
			if(tile == nullptr)
				return false;
			if(!tile->isBlocking())
				return true;
			return false;
		}

		bool operator()(Map& map, Tile* tile, long long removed, long long done, long long total)
		{
			if(done % 0x1000 == 0)
				g_gui.SetLoadDone((unsigned int)(100 * done / total));

			const Position& pos = tile->getPosition();
			int sx = std::max(pos.x - 10, 0);
			int ex = std::min(pos.x + 10, 65535);
			int sy = std::max(pos.y - 8,  0);
			int ey = std::min(pos.y + 8,  65535);
			int sz, ez;

			if(pos.z < 8) {
				sz = 0;
				ez = 9;
			} else {
				// underground
				sz = std::max(pos.z - 2, rme::MapGroundLayer);
				ez = std::min(pos.z + 2, rme::MapMaxLayer);
			}

			for(int z = sz; z <= ez; ++z) {
				for(int y = sy; y <= ey; ++y) {
					for(int x = sx; x <= ex; ++x) {
						if(isReachable(map.getTile(x, y, z)))
							return false;
					}
				}
			}
			return true;
		}
	};
}

void MainMenuBar::OnMapRemoveUnreachable(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	int ok = g_gui.PopupDialog("Remove Unreachable Tiles", "Do you want to remove all unreachable items from the map?", wxYES | wxNO);

	if(ok == wxID_YES) {
		g_gui.GetCurrentEditor()->getSelection().clear();
		g_gui.GetCurrentEditor()->clearActions();

		OnMapRemoveUnreachable::condition func;
		g_gui.CreateLoadBar("Searching map for tiles to remove...");

		long long removed = remove_if_TileOnMap(g_gui.GetCurrentMap(), func);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << removed << " tiles deleted.";

		g_gui.PopupDialog("Search completed", msg, wxOK);

		g_gui.GetCurrentMap().doChange();
	}
}

void MainMenuBar::OnMapRemoveEmptySpawns(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen()) {
		return;
	}

	int ok = g_gui.PopupDialog("Remove Empty Spawns", "Do you want to remove all empty spawns from the map?", wxYES | wxNO);
	if(ok == wxID_YES) {
		Editor* editor = g_gui.GetCurrentEditor();
		editor->getSelection().clear();

		g_gui.CreateLoadBar("Searching map for empty spawns to remove...");

		Map& map = g_gui.GetCurrentMap();
		CreatureVector creatures;
		TileVector toDeleteSpawns;
		for(const auto& spawnPosition : map.spawns) {
			Tile* tile = map.getTile(spawnPosition);
			if(!tile || !tile->spawn) {
				continue;
			}

			const int32_t radius = tile->spawn->getSize();

			bool empty = true;
			for(int32_t y = -radius; y <= radius; ++y) {
				for(int32_t x = -radius; x <= radius; ++x) {
					Tile* creature_tile = map.getTile(spawnPosition + Position(x, y, 0));
					if(creature_tile && creature_tile->creature && !creature_tile->creature->isSaved()) {
						creature_tile->creature->save();
						creatures.push_back(creature_tile->creature);
						empty = false;
					}
				}
			}

			if(empty) {
				toDeleteSpawns.push_back(tile);
			}
		}

		for(Creature* creature : creatures) {
			creature->reset();
		}

		BatchAction* batch = editor->createBatch(ACTION_DELETE_TILES);
		Action* action = editor->createAction(batch);

		const size_t count = toDeleteSpawns.size();
		size_t removed = 0;
		for(const auto& tile : toDeleteSpawns) {
			Tile* newtile = tile->deepCopy(map);
			map.removeSpawn(newtile);
			delete newtile->spawn;
			newtile->spawn = nullptr;
			if(++removed % 5 == 0) {
				// update progress bar for each 5 spawns removed
				g_gui.SetLoadDone(100 * removed / count);
			}
			action->addChange(newd Change(newtile));
		}

		batch->addAndCommitAction(action);
		editor->addBatch(batch);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << removed << " empty spawns removed.";
		g_gui.PopupDialog("Search completed", msg, wxOK);
		g_gui.GetCurrentMap().doChange();
	}
}

void MainMenuBar::OnClearHouseTiles(wxCommandEvent& WXUNUSED(event))
{
	Editor* editor = g_gui.GetCurrentEditor();
	if(!editor)
		return;

	int ret = g_gui.PopupDialog(
		"Clear Invalid House Tiles",
		"Are you sure you want to remove all house tiles that do not belong to a house (this action cannot be undone)?",
		wxYES | wxNO
	);

	if(ret == wxID_YES) {
		// Editor will do the work
		editor->clearInvalidHouseTiles(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnClearModifiedState(wxCommandEvent& WXUNUSED(event))
{
	Editor* editor = g_gui.GetCurrentEditor();
	if(!editor)
		return;

	int ret = g_gui.PopupDialog(
		"Clear Modified State",
		"This will have the same effect as closing the map and opening it again. Do you want to proceed?",
		wxYES | wxNO
	);

	if(ret == wxID_YES) {
		// Editor will do the work
		editor->clearModifiedTileState(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnMapCleanHouseItems(wxCommandEvent& WXUNUSED(event))
{
	Editor* editor = g_gui.GetCurrentEditor();
	if(!editor)
		return;

	int ret = g_gui.PopupDialog(
		"Clear Moveable House Items",
		"Are you sure you want to remove all items inside houses that can be moved (this action cannot be undone)?",
		wxYES | wxNO
	);

	if(ret == wxID_YES) {
		// Editor will do the work
		//editor->removeHouseItems(true);
	}

	g_gui.RefreshView();
}

void MainMenuBar::OnMapEditTowns(wxCommandEvent& WXUNUSED(event))
{
	if(g_gui.GetCurrentEditor()) {
		wxDialog* town_dialog = newd EditTownsDialog(frame, *g_gui.GetCurrentEditor());
		town_dialog->ShowModal();
		town_dialog->Destroy();
	}
}

void MainMenuBar::OnMapEditItems(wxCommandEvent& WXUNUSED(event))
{
	;
}

void MainMenuBar::OnMapEditMonsters(wxCommandEvent& WXUNUSED(event))
{
	;
}

void MainMenuBar::OnMapStatistics(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	g_gui.CreateLoadBar("Collecting data...");

	Map* map = &g_gui.GetCurrentMap();

	int load_counter = 0;

	uint64_t tile_count = 0;
	uint64_t detailed_tile_count = 0;
	uint64_t blocking_tile_count = 0;
	uint64_t walkable_tile_count = 0;
	double percent_pathable = 0.0;
	double percent_detailed = 0.0;
	uint64_t spawn_count = 0;
	uint64_t creature_count = 0;
	double creatures_per_spawn = 0.0;

	uint64_t item_count = 0;
	uint64_t loose_item_count = 0;
	uint64_t depot_count = 0;
	uint64_t action_item_count = 0;
	uint64_t unique_item_count = 0;
	uint64_t container_count = 0; // Only includes containers containing more than 1 item

	int town_count = map->towns.count();
	int house_count = map->houses.count();
	std::map<uint32_t, uint32_t> town_sqm_count;
	const Town* largest_town = nullptr;
	uint64_t largest_town_size = 0;
	uint64_t total_house_sqm = 0;
	const House* largest_house = nullptr;
	uint64_t largest_house_size = 0;
	double houses_per_town = 0.0;
	double sqm_per_house = 0.0;
	double sqm_per_town = 0.0;

	for(MapIterator mit = map->begin(); mit != map->end(); ++mit) {
		Tile* tile = (*mit)->get();
		if(load_counter % 8192 == 0) {
			g_gui.SetLoadDone((unsigned int)(int64_t(load_counter) * 95ll / int64_t(map->getTileCount())));
		}

		if(tile->empty())
			continue;

		tile_count += 1;

		bool is_detailed = false;
#define ANALYZE_ITEM(_item) {\
	item_count += 1; \
	if(!(_item)->isGroundTile() && !(_item)->isBorder()) { \
		is_detailed = true; \
		const ItemType& it = g_items.getItemType((_item)->getID()); \
		if(it.moveable) { \
			loose_item_count += 1; \
		} \
		if(it.isDepot()) { \
			depot_count += 1; \
		} \
		if((_item)->getActionID() > 0) { \
			action_item_count += 1; \
		} \
		if((_item)->getUniqueID() > 0) { \
			unique_item_count += 1; \
		} \
		if(Container* c = dynamic_cast<Container*>((_item))) { \
			if(c->getVector().size()) { \
				container_count += 1; \
			} \
		} \
	} \
}
		if(tile->ground) {
			ANALYZE_ITEM(tile->ground);
		}

		for(Item* item : tile->items) {
			ANALYZE_ITEM(item);
		}
#undef ANALYZE_ITEM

		if(tile->spawn)
			spawn_count += 1;

		if(tile->creature)
			creature_count += 1;

		if(tile->isBlocking())
			blocking_tile_count += 1;
		else
			walkable_tile_count += 1;

		if(is_detailed)
			detailed_tile_count += 1;

		load_counter += 1;
	}

	creatures_per_spawn = (spawn_count != 0 ? double(creature_count) / double(spawn_count) : -1.0);
	percent_pathable = 100.0*(tile_count != 0 ? double(walkable_tile_count) / double(tile_count) : -1.0);
	percent_detailed = 100.0*(tile_count != 0 ? double(detailed_tile_count) / double(tile_count) : -1.0);

	load_counter = 0;
	Houses& houses = map->houses;
	for(HouseMap::const_iterator hit = houses.begin(); hit != houses.end(); ++hit) {
		const House* house = hit->second;

		if(load_counter % 64)
			g_gui.SetLoadDone((unsigned int)(95ll + int64_t(load_counter) * 5ll / int64_t(house_count)));

		if(house->size() > largest_house_size) {
			largest_house = house;
			largest_house_size = house->size();
		}
		total_house_sqm += house->size();
		town_sqm_count[house->townid] += house->size();
	}

	houses_per_town = (town_count != 0?  double(house_count) /     double(town_count)  : -1.0);
	sqm_per_house   = (house_count != 0? double(total_house_sqm) / double(house_count) : -1.0);
	sqm_per_town    = (town_count != 0?  double(total_house_sqm) / double(town_count)  : -1.0);

	Towns& towns = map->towns;
	for(std::map<uint32_t, uint32_t>::iterator town_iter = town_sqm_count.begin();
			town_iter != town_sqm_count.end();
			++town_iter)
	{
		// No load bar for this, load is non-existant
		uint32_t town_id = town_iter->first;
		uint32_t town_sqm = town_iter->second;
		Town* town = towns.getTown(town_id);
		if(town && town_sqm > largest_town_size) {
			largest_town = town;
			largest_town_size = town_sqm;
		} else {
			// Non-existant town!
		}
	}

	g_gui.DestroyLoadBar();

	std::ostringstream os;
	os.setf(std::ios::fixed, std::ios::floatfield);
	os.precision(2);
	os << "Map statistics for the map \"" << map->getMapDescription() << "\"\n";
	os << "\tTile data:\n";
	os << "\t\tTotal number of tiles: " << tile_count << "\n";
	os << "\t\tNumber of pathable tiles: " << walkable_tile_count << "\n";
	os << "\t\tNumber of unpathable tiles: " << blocking_tile_count << "\n";
	if(percent_pathable >= 0.0)
		os << "\t\tPercent walkable tiles: " << percent_pathable << "%\n";
	os << "\t\tDetailed tiles: " << detailed_tile_count << "\n";
	if(percent_detailed >= 0.0)
		os << "\t\tPercent detailed tiles: " << percent_detailed << "%\n";

	os << "\tItem data:\n";
	os << "\t\tTotal number of items: " << item_count << "\n";
	os << "\t\tNumber of moveable tiles: " << loose_item_count << "\n";
	os << "\t\tNumber of depots: " << depot_count << "\n";
	os << "\t\tNumber of containers: " << container_count << "\n";
	os << "\t\tNumber of items with Action ID: " << action_item_count << "\n";
	os << "\t\tNumber of items with Unique ID: " << unique_item_count << "\n";

	os << "\tCreature data:\n";
	os << "\t\tTotal creature count: " << creature_count << "\n";
	os << "\t\tTotal spawn count: " << spawn_count << "\n";
	if(creatures_per_spawn >= 0)
		os << "\t\tMean creatures per spawn: " << creatures_per_spawn << "\n";

	os << "\tTown/House data:\n";
	os << "\t\tTotal number of towns: " << town_count << "\n";
	os << "\t\tTotal number of houses: " << house_count << "\n";
	if(houses_per_town >= 0)
		os << "\t\tMean houses per town: " << houses_per_town << "\n";
	os << "\t\tTotal amount of housetiles: " << total_house_sqm << "\n";
	if(sqm_per_house >= 0)
		os << "\t\tMean tiles per house: " << sqm_per_house << "\n";
	if(sqm_per_town >= 0)
		os << "\t\tMean tiles per town: " << sqm_per_town << "\n";

	if(largest_town)
		os << "\t\tLargest Town: \"" << largest_town->getName() << "\" (" << largest_town_size << " sqm)\n";
	if(largest_house)
		os << "\t\tLargest House: \"" << largest_house->name << "\" (" << largest_house_size << " sqm)\n";

	os << "\n";
	os << "Generated by Remere's Map Editor version " + __RME_VERSION__ + "\n";


    wxDialog* dg = newd wxDialog(frame, wxID_ANY, "Map Statistics", wxDefaultPosition, wxDefaultSize, wxRESIZE_BORDER | wxCAPTION | wxCLOSE_BOX);
	dg->SetBackgroundColour(wxColour(5, 20, 50));
	dg->SetForegroundColour(*wxWHITE);
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);
	wxTextCtrl* text_field = newd wxTextCtrl(dg, wxID_ANY, wxstr(os.str()), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
	text_field->SetMinSize(wxSize(400, 300));
	text_field->SetBackgroundColour(wxColour(5, 20, 50));
	text_field->SetForegroundColour(*wxWHITE);
	topsizer->Add(text_field, wxSizerFlags(5).Expand());

	wxSizer* choicesizer = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* export_button = newd wxButton(dg, wxID_OK, "Export as XML");
	choicesizer->Add(export_button, wxSizerFlags(1).Center());
	export_button->Enable(false);
	wxButton* ok_button = newd wxButton(dg, wxID_CANCEL, "OK");
	choicesizer->Add(ok_button, wxSizerFlags(1).Center());
	topsizer->Add(choicesizer, wxSizerFlags(1).Center());
	dg->SetSizerAndFit(topsizer);
	dg->CentreOnParent();

	int ret = dg->ShowModal();

	if(ret == wxID_OK) {
		//std::cout << "XML EXPORT";
	} else if(ret == wxID_CANCEL) {
		//std::cout << "OK";
	}
}

void MainMenuBar::OnScanRegion(wxCommandEvent& WXUNUSED(event))
{
	RegionScanDialog::Show(frame);
}

void MainMenuBar::OnMapCleanup(wxCommandEvent& WXUNUSED(event))
{
	int ok = g_gui.PopupDialog("Clean map", "Do you want to remove all invalid items from the map?", wxYES | wxNO);

	if(ok == wxID_YES)
		g_gui.GetCurrentMap().cleanInvalidTiles(true);
}

void MainMenuBar::OnMapProperties(wxCommandEvent& WXUNUSED(event))
{
	wxDialog* properties = newd MapPropertiesWindow(
		frame,
		static_cast<MapTab*>(g_gui.GetCurrentTab()),
		*g_gui.GetCurrentEditor());

	if(properties->ShowModal() == 0) {
		// FAIL!
		g_gui.CloseAllEditors();
	}
	properties->Destroy();
}

void MainMenuBar::OnToolbars(wxCommandEvent& event)
{
	using namespace MenuBar;

	ActionID id = static_cast<ActionID>(event.GetId() - (wxID_HIGHEST + 1));
	switch (id) {
		case VIEW_TOOLBARS_BRUSHES:
			g_gui.ShowToolbar(TOOLBAR_BRUSHES, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_BRUSHES, event.IsChecked());
			break;
		case VIEW_TOOLBARS_POSITION:
			g_gui.ShowToolbar(TOOLBAR_POSITION, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_POSITION, event.IsChecked());
			break;
		case VIEW_TOOLBARS_SIZES:
			g_gui.ShowToolbar(TOOLBAR_SIZES, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_SIZES, event.IsChecked());
			break;
		case VIEW_TOOLBARS_INDICATORS:
			g_gui.ShowToolbar(TOOLBAR_INDICATORS, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_INDICATORS, event.IsChecked());
			break;
		case VIEW_TOOLBARS_STANDARD:
			g_gui.ShowToolbar(TOOLBAR_STANDARD, event.IsChecked());
			g_settings.setInteger(Config::SHOW_TOOLBAR_STANDARD, event.IsChecked());
			break;
	    default:
	        break;
	}
}

void MainMenuBar::OnNewView(wxCommandEvent& WXUNUSED(event))
{
	g_gui.NewMapView();
}

void MainMenuBar::OnNewDetachedView(wxCommandEvent& WXUNUSED(event))
{
	g_gui.NewDetachedMapView();
}

void MainMenuBar::OnToggleFullscreen(wxCommandEvent& WXUNUSED(event))
{
	if(frame->IsFullScreen())
		frame->ShowFullScreen(false);
	else
		frame->ShowFullScreen(true, wxFULLSCREEN_NOBORDER | wxFULLSCREEN_NOCAPTION);
}

void MainMenuBar::OnTakeScreenshot(wxCommandEvent& WXUNUSED(event))
{
	wxString path = wxstr(g_settings.getString(Config::SCREENSHOT_DIRECTORY));
	if(path.size() > 0 && (path.Last() == '/' || path.Last() == '\\'))
		path = path + "/";

	g_gui.GetCurrentMapTab()->GetView()->GetCanvas()->TakeScreenshot(
		path, wxstr(g_settings.getString(Config::SCREENSHOT_FORMAT))
	);

}

void MainMenuBar::OnTakeRegionScreenshot(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	Editor* editor = g_gui.GetCurrentEditor();
	if(!editor)
		return;

	Selection& selection = editor->getSelection();
	if(selection.empty()) {
		g_gui.SetStatusText("Select an area before taking an area screenshot.");
		return;
	}

	Position from = selection.minPosition();
	Position to = selection.maxPosition();

	wxString path = wxstr(g_settings.getString(Config::SCREENSHOT_DIRECTORY));
	if(path.size() > 0 && (path.Last() == '/' || path.Last() == '\\'))
		path = path + "/";

	MapTab* tab = g_gui.GetCurrentMapTab();
	if(!tab)
		return;

	MapCanvas* canvas = tab->GetView()->GetCanvas();
	canvas->TakeRegionScreenshot(
		path, wxstr(g_settings.getString(Config::SCREENSHOT_FORMAT)), from, to
	);
}

void MainMenuBar::OnRecordGif(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen())
		return;

	MapTab* tab = g_gui.GetCurrentMapTab();
	if(!tab)
		return;

	MapCanvas* canvas = tab->GetView()->GetCanvas();
	if(!canvas)
		return;

	if(canvas->IsGifRecording()) {
		canvas->StopGifRecording(true);
		return;
	}

	wxString directory = wxstr(g_settings.getString(Config::SCREENSHOT_DIRECTORY));
	if(directory.empty())
		directory = wxFileName::GetCwd();

	time_t t = time(nullptr);
	struct tm* current_time = localtime(&t);
	wxString timestamp("gif_capture");
	if(current_time) {
		timestamp.Printf("gif_%04d-%02d-%02d-%02d-%02d-%02d",
			1900 + current_time->tm_year,
			current_time->tm_mon + 1,
			current_time->tm_mday,
			current_time->tm_hour,
			current_time->tm_min,
			current_time->tm_sec);
	}

	wxFileName defaultPath(directory, "");
	defaultPath.SetFullName(timestamp + ".gif");

	wxFileDialog saveDialog(
		frame,
		"Save animated GIF",
		defaultPath.GetPath(),
		defaultPath.GetFullName(),
		"GIF files (*.gif)|*.gif",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT
	);

	if(saveDialog.ShowModal() != wxID_OK)
		return;

	wxNumberEntryDialog fpsDialog(
		frame,
		"Frames per second for the recording (1 - 30).",
		"FPS:",
		"Animated GIF Recording",
		8,
		1,
		30
	);

	if(fpsDialog.ShowModal() != wxID_OK)
		return;

	wxFileName output(saveDialog.GetPath());
	if(!canvas->StartGifRecording(output, fpsDialog.GetValue())) {
		g_gui.PopupDialog("GIF recording failed", "Unable to start GIF recording with the current settings.", wxOK);
		return;
	}

	g_gui.SetStatusText("Recording GIF... Press F12 again to stop.");
}

void MainMenuBar::OnZoomIn(wxCommandEvent& event)
{
	double zoom = g_gui.GetCurrentZoom();
	g_gui.SetCurrentZoom(zoom - 0.1);
}

void MainMenuBar::OnZoomOut(wxCommandEvent& event)
{
	double zoom = g_gui.GetCurrentZoom();
	g_gui.SetCurrentZoom(zoom + 0.1);
}

void MainMenuBar::OnZoomNormal(wxCommandEvent& event)
{
	g_gui.SetCurrentZoom(1.0);
}

void MainMenuBar::OnChangeViewSettings(wxCommandEvent& event)
{
	g_settings.setInteger(Config::SHOW_ALL_FLOORS, IsItemChecked(MenuBar::SHOW_ALL_FLOORS));
	bool allow_multi_floor_selection = IsItemChecked(MenuBar::SHOW_ALL_FLOORS) ||
		g_settings.getBoolean(Config::SELECTION_LASSO);
	EnableItem(MenuBar::SELECT_MODE_VISIBLE, allow_multi_floor_selection);
	EnableItem(MenuBar::SELECT_MODE_LOWER, allow_multi_floor_selection);
	if(!allow_multi_floor_selection) {
		CheckItem(MenuBar::SELECT_MODE_CURRENT, true);
		g_settings.setInteger(Config::SELECTION_TYPE, SELECT_CURRENT_FLOOR);
	}
	g_settings.setInteger(Config::TRANSPARENT_FLOORS, IsItemChecked(MenuBar::GHOST_HIGHER_FLOORS));
	g_settings.setInteger(Config::TRANSPARENT_ITEMS, IsItemChecked(MenuBar::GHOST_ITEMS));
	g_settings.setInteger(Config::TRANSPARENT_GROUND_ITEMS, IsItemChecked(MenuBar::GHOST_GROUND_ITEMS));
	g_settings.setInteger(Config::SHOW_INGAME_BOX, IsItemChecked(MenuBar::SHOW_INGAME_BOX));
	g_settings.setInteger(Config::SHOW_LIGHTS, IsItemChecked(MenuBar::SHOW_LIGHTS));
	g_settings.setInteger(Config::SHOW_TECHNICAL_ITEMS, IsItemChecked(MenuBar::SHOW_TECHNICAL_ITEMS));
	g_settings.setInteger(Config::SHOW_GRID, IsItemChecked(MenuBar::SHOW_GRID));
	g_settings.setInteger(Config::SHOW_EXTRA, !IsItemChecked(MenuBar::SHOW_EXTRA));

	g_settings.setInteger(Config::SHOW_SHADE, IsItemChecked(MenuBar::SHOW_SHADE));
	g_settings.setInteger(Config::SHOW_SPECIAL_TILES, IsItemChecked(MenuBar::SHOW_SPECIAL));
	g_settings.setInteger(Config::SHOW_AS_MINIMAP, IsItemChecked(MenuBar::SHOW_AS_MINIMAP));
	g_settings.setInteger(Config::SHOW_ONLY_TILEFLAGS, IsItemChecked(MenuBar::SHOW_ONLY_COLORS));
	g_settings.setInteger(Config::SHOW_ONLY_MODIFIED_TILES, IsItemChecked(MenuBar::SHOW_ONLY_MODIFIED));
	g_settings.setInteger(Config::SHOW_CREATURES, IsItemChecked(MenuBar::SHOW_CREATURES));
	g_settings.setInteger(Config::SHOW_CREATURE_IDLE_ANIMATION, IsItemChecked(MenuBar::SHOW_CREATURE_IDLE_ANIMATION));
	g_settings.setInteger(Config::SHOW_SPAWNS, IsItemChecked(MenuBar::SHOW_SPAWNS));
	g_settings.setInteger(Config::SHOW_SPAWN_CREATURESLIST, IsItemChecked(MenuBar::SHOW_SPAWN_CREATURESLIST));
	g_settings.setInteger(Config::SHOW_HOUSES, IsItemChecked(MenuBar::SHOW_HOUSES));
	g_settings.setInteger(Config::HIGHLIGHT_ITEMS, IsItemChecked(MenuBar::HIGHLIGHT_ITEMS));
	g_settings.setInteger(Config::SHOW_BLOCKING, IsItemChecked(MenuBar::SHOW_PATHING));
	g_settings.setInteger(Config::SHOW_TOOLTIPS, IsItemChecked(MenuBar::SHOW_TOOLTIPS));
	g_settings.setInteger(Config::SHOW_PREVIEW, IsItemChecked(MenuBar::SHOW_PREVIEW));
	g_settings.setInteger(Config::SHOW_WALL_HOOKS, IsItemChecked(MenuBar::SHOW_WALL_HOOKS));
	g_settings.setInteger(Config::SHOW_PICKUPABLES, IsItemChecked(MenuBar::SHOW_PICKUPABLES));
	g_settings.setInteger(Config::SHOW_MOVEABLES, IsItemChecked(MenuBar::SHOW_MOVEABLES));
	g_settings.setInteger(Config::SHOW_WALL_BORDERS, IsItemChecked(MenuBar::SHOW_WALL_BORDERS));
	g_settings.setInteger(Config::SHOW_MOUNTAIN_OVERLAY, IsItemChecked(MenuBar::SHOW_MOUNTAIN_OVERLAY));
	g_settings.setInteger(Config::SHOW_STAIR_DIRECTION, IsItemChecked(MenuBar::SHOW_STAIR_DIRECTION));
	g_settings.setInteger(Config::SHOW_CAMERA_PATHS, IsItemChecked(MenuBar::SHOW_CAMERA_PATHS));
	g_settings.setInteger(Config::SHOW_NPC_PATHS, IsItemChecked(MenuBar::SHOW_NPC_PATHS));
	g_settings.setInteger(Config::SHOW_CREATURE_WANDER_RADIUS, IsItemChecked(MenuBar::SHOW_CREATURE_WANDER_RADIUS));
	g_settings.setInteger(Config::ANIMATE_CREATURE_WALK, IsItemChecked(MenuBar::ANIMATE_CREATURE_WALK));
	g_settings.setInteger(Config::SHOW_ONLY_GROUNDS, IsItemChecked(MenuBar::SHOW_ONLY_GROUNDS));
	g_settings.setInteger(Config::SHOW_CHUNK_BOUNDARIES, IsItemChecked(MenuBar::SHOW_CHUNK_BOUNDARIES));
	g_settings.setInteger(Config::FLOOR_FADING, IsItemChecked(MenuBar::FLOOR_FADING));

	g_gui.RefreshView();
	g_gui.root->GetAuiToolBar()->UpdateIndicators();
}

void MainMenuBar::OnSetLightHour(wxCommandEvent& WXUNUSED(event))
{
	int currentHour = g_settings.getInteger(Config::LIGHT_HOUR);
	if(currentHour < 0 || currentHour > 23) {
		currentHour = 12;
	}

	wxNumberEntryDialog dialog(
		frame,
		"Select the simulated in-game hour (0 - 23).",
		"Hour:",
		"Ambient Lighting",
		currentHour,
		0,
		23
	);

	if(dialog.ShowModal() == wxID_OK) {
		g_settings.setInteger(Config::LIGHT_HOUR, dialog.GetValue());
		g_gui.RefreshView();
	}
}

void MainMenuBar::OnChangeFloor(wxCommandEvent& event)
{
	// Workaround to stop events from looping
	if(checking_programmaticly)
		return;

	for(int i = 0; i < 16; ++i) {
		if(IsItemChecked(MenuBar::ActionID(MenuBar::FLOOR_0 + i))) {
			g_gui.ChangeFloor(i);
		}
	}
}

void MainMenuBar::OnMinimapWindow(wxCommandEvent& event)
{
	g_gui.CreateMinimap();
}

void MainMenuBar::OnActionsHistoryWindow(wxCommandEvent& WXUNUSED(event))
{
	g_gui.ShowActionsWindow();
}

void MainMenuBar::OnRecentBrushesWindow(wxCommandEvent& WXUNUSED(event))
{
	g_gui.ShowRecentBrushesWindow();
}

void MainMenuBar::OnBrowseFieldWindow(wxCommandEvent& WXUNUSED(event))
{
	g_gui.ShowBrowseFieldPanel();
}

void MainMenuBar::OnBrushManager(wxCommandEvent& WXUNUSED(event))
{
	g_gui.ShowBrushManager();
}

void MainMenuBar::OnStructureManager(wxCommandEvent& WXUNUSED(event))
{
	if(!g_gui.IsEditorOpen()) {
		return;
	}

	StructureManagerDialog* dialog = newd StructureManagerDialog(frame);
	dialog->Show();
}

void MainMenuBar::OnBrushTipsWindow(wxCommandEvent& WXUNUSED(event))
{
	BrushTipsDialog dialog(frame);
	dialog.ShowModal();
}

void MainMenuBar::OnAreaDecoration(wxCommandEvent& WXUNUSED(event))
{
	g_gui.ShowAreaDecorationDialog();
}

void MainMenuBar::OnFloorFadingSettings(wxCommandEvent& WXUNUSED(event))
{
	FloorFadingDialog dialog(frame);
	dialog.ShowModal();
	dialog.Destroy();
}

void MainMenuBar::OnAreaCreatureSpawn(wxCommandEvent& WXUNUSED(event))
{
	g_gui.ShowAreaCreatureSpawnDialog();
}

void MainMenuBar::OnNewPalette(wxCommandEvent& event)
{
	g_gui.NewPalette();
}

void MainMenuBar::OnSelectTerrainPalette(wxCommandEvent& WXUNUSED(event))
{
	g_gui.SelectPalettePage(TILESET_TERRAIN);
}

void MainMenuBar::OnSelectDoodadPalette(wxCommandEvent& WXUNUSED(event))
{
	g_gui.SelectPalettePage(TILESET_DOODAD);
}

void MainMenuBar::OnSelectItemPalette(wxCommandEvent& WXUNUSED(event))
{
	g_gui.SelectPalettePage(TILESET_ITEM);
}

void MainMenuBar::OnSelectHousePalette(wxCommandEvent& WXUNUSED(event))
{
	g_gui.SelectPalettePage(TILESET_HOUSE);
}

void MainMenuBar::OnSelectCreaturePalette(wxCommandEvent& WXUNUSED(event))
{
	g_gui.SelectPalettePage(TILESET_CREATURE);
}

void MainMenuBar::OnSelectWaypointPalette(wxCommandEvent& WXUNUSED(event))
{
	g_gui.SelectPalettePage(TILESET_WAYPOINT);
}

void MainMenuBar::OnSelectCameraPathPalette(wxCommandEvent& WXUNUSED(event))
{
	g_gui.SelectPalettePage(TILESET_CAMERA_PATH);
}

void MainMenuBar::OnSelectRawPalette(wxCommandEvent& WXUNUSED(event))
{
	g_gui.SelectPalettePage(TILESET_RAW);
}

void MainMenuBar::OnCameraPlayPause(wxCommandEvent& WXUNUSED(event))
{
	g_gui.ToggleCameraPathPlayback();
}

void MainMenuBar::OnCameraAddKeyframe(wxCommandEvent& WXUNUSED(event))
{
	g_gui.AddCameraPathKeyframeAtCursor();
}

void MainMenuBar::OnStartLive(wxCommandEvent& event)
{
	Editor* editor = g_gui.GetCurrentEditor();
	if(!editor) {
		g_gui.PopupDialog("Error", "You need to have a map open to start a live mapping session.", wxOK);
		return;
	}
	if(editor->IsLive()) {
		g_gui.PopupDialog("Error", "You can not start two live servers on the same map (or a server using a remote map).", wxOK);
		return;
	}

	wxDialog* live_host_dlg = newd wxDialog(frame, wxID_ANY, "Host Live Server", wxDefaultPosition, wxDefaultSize);

	wxSizer* top_sizer = newd wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* gsizer = newd wxFlexGridSizer(2, 10, 10);
	gsizer->AddGrowableCol(0, 2);
	gsizer->AddGrowableCol(1, 3);

	// Data fields
	wxTextCtrl* hostname;
	wxSpinCtrl* port;
	wxTextCtrl* password;
	wxCheckBox* allow_copy;

	gsizer->Add(newd wxStaticText(live_host_dlg, wxID_ANY, "Server Name:"));
	gsizer->Add(hostname = newd wxTextCtrl(live_host_dlg, wxID_ANY, "RME Live Server"), 0, wxEXPAND);

	gsizer->Add(newd wxStaticText(live_host_dlg, wxID_ANY, "Port:"));
	gsizer->Add(port = newd wxSpinCtrl(live_host_dlg, wxID_ANY, "31313", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, 31313), 0, wxEXPAND);

	gsizer->Add(newd wxStaticText(live_host_dlg, wxID_ANY, "Password:"));
	gsizer->Add(password = newd wxTextCtrl(live_host_dlg, wxID_ANY), 0, wxEXPAND);

	top_sizer->Add(gsizer, 0, wxALL, 20);

	top_sizer->Add(allow_copy = newd wxCheckBox(live_host_dlg, wxID_ANY, "Allow copy & paste between maps."), 0, wxRIGHT | wxLEFT, 20);
	allow_copy->SetToolTip("Allows remote clients to copy & paste from the hosted map to local maps.");

	wxSizer* ok_sizer = newd wxBoxSizer(wxHORIZONTAL);
	ok_sizer->Add(newd wxButton(live_host_dlg, wxID_OK, "OK"), 1, wxCENTER);
	ok_sizer->Add(newd wxButton(live_host_dlg, wxID_CANCEL, "Cancel"), wxCENTER, 1);
	top_sizer->Add(ok_sizer, 0, wxCENTER | wxALL, 20);

	live_host_dlg->SetSizerAndFit(top_sizer);

	while(true) {
		int ret = live_host_dlg->ShowModal();
		if(ret == wxID_OK) {
			LiveServer* liveServer = editor->StartLiveServer();
			liveServer->setName(hostname->GetValue());
			liveServer->setPassword(password->GetValue());
			liveServer->setPort(port->GetValue());

			const wxString& error = liveServer->getLastError();
			if(!error.empty()) {
				g_gui.PopupDialog(live_host_dlg, "Error", error, wxOK);
				editor->CloseLiveServer();
				continue;
			}

			if(!liveServer->bind()) {
				g_gui.PopupDialog("Socket Error", "Could not bind socket! Try another port?", wxOK);
				editor->CloseLiveServer();
			} else {
				liveServer->createLogWindow(g_gui.tabbook);
			}
			break;
		} else
			break;
	}
	live_host_dlg->Destroy();
	Update();
}

void MainMenuBar::OnJoinLive(wxCommandEvent& event)
{
	wxDialog* live_join_dlg = newd wxDialog(frame, wxID_ANY, "Join Live Server", wxDefaultPosition, wxDefaultSize);

	wxSizer* top_sizer = newd wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* gsizer = newd wxFlexGridSizer(2, 10, 10);
	gsizer->AddGrowableCol(0, 2);
	gsizer->AddGrowableCol(1, 3);

	// Data fields
	wxTextCtrl* name;
	wxTextCtrl* ip;
	wxSpinCtrl* port;
	wxTextCtrl* password;

	gsizer->Add(newd wxStaticText(live_join_dlg, wxID_ANY, "Name:"));
	gsizer->Add(name = newd wxTextCtrl(live_join_dlg, wxID_ANY, ""), 0, wxEXPAND);

	gsizer->Add(newd wxStaticText(live_join_dlg, wxID_ANY, "IP:"));
	gsizer->Add(ip = newd wxTextCtrl(live_join_dlg, wxID_ANY, "localhost"), 0, wxEXPAND);

	gsizer->Add(newd wxStaticText(live_join_dlg, wxID_ANY, "Port:"));
	gsizer->Add(port = newd wxSpinCtrl(live_join_dlg, wxID_ANY, "31313", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, 31313), 0, wxEXPAND);

	gsizer->Add(newd wxStaticText(live_join_dlg, wxID_ANY, "Password:"));
	gsizer->Add(password = newd wxTextCtrl(live_join_dlg, wxID_ANY), 0, wxEXPAND);

	top_sizer->Add(gsizer, 0, wxALL, 20);

	wxSizer* ok_sizer = newd wxBoxSizer(wxHORIZONTAL);
	ok_sizer->Add(newd wxButton(live_join_dlg, wxID_OK, "OK"), 1, wxRIGHT);
	ok_sizer->Add(newd wxButton(live_join_dlg, wxID_CANCEL, "Cancel"), 1, wxRIGHT);
	top_sizer->Add(ok_sizer, 0, wxCENTER | wxALL, 20);

	live_join_dlg->SetSizerAndFit(top_sizer);

	while(true) {
		int ret = live_join_dlg->ShowModal();
		if(ret == wxID_OK) {
			LiveClient* liveClient = newd LiveClient();
			liveClient->setPassword(password->GetValue());

			wxString tmp = name->GetValue();
			if(tmp.empty()) {
				tmp = "User";
			}
			liveClient->setName(tmp);

			const wxString& error = liveClient->getLastError();
			if(!error.empty()) {
				g_gui.PopupDialog(live_join_dlg, "Error", error, wxOK);
				delete liveClient;
				continue;
			}

			const wxString& address = ip->GetValue();
			int32_t portNumber = port->GetValue();

			liveClient->createLogWindow(g_gui.tabbook);
			if(!liveClient->connect(nstr(address), portNumber)) {
				g_gui.PopupDialog("Connection Error", liveClient->getLastError(), wxOK);
				delete liveClient;
			}

			break;
		} else
			break;
	}
	live_join_dlg->Destroy();
	Update();
}

void MainMenuBar::OnCloseLive(wxCommandEvent& event)
{
	Editor* editor = g_gui.GetCurrentEditor();
	if(editor && editor->IsLive())
		g_gui.CloseLiveEditors(&editor->GetLive());

	Update();
}

void MainMenuBar::SearchItems(bool unique, bool action, bool container, bool writable, bool onSelection/* = false*/)
{
	if(!unique && !action && !container && !writable)
		return;

	if(!g_gui.IsEditorOpen())
		return;

	if(onSelection)
		g_gui.CreateLoadBar("Searching on selected area...");
	else
		g_gui.CreateLoadBar("Searching on map...");

	OnSearchForStuff::Searcher searcher;
	searcher.search_unique = unique;
	searcher.search_action = action;
	searcher.search_container = container;
	searcher.search_writeable = writable;

	foreach_ItemOnMap(g_gui.GetCurrentMap(), searcher, onSelection);
	searcher.sort();
	std::vector<std::pair<Tile*, Item*> >& found = searcher.found;

	g_gui.DestroyLoadBar();

	SearchResultWindow* result = g_gui.ShowSearchWindow();
	result->Clear();
	for(std::vector<std::pair<Tile*, Item*> >::iterator iter = found.begin(); iter != found.end(); ++iter) {
		result->AddPosition(searcher.desc(iter->second), iter->first->getPosition());
	}
}

void MainMenuBar::SearchDuplicatedItems(bool selection)
{
	if(!g_gui.IsEditorOpen()) {
		return;
	}

	auto dialog = g_gui.ShowDuplicatedItemsWindow();
	dialog->StartSearch(g_gui.GetCurrentMapTab(), selection);
}
