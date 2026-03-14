//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "app/managers/version_manager.h"
#include "app/settings.h"

#include <spdlog/spdlog.h>

#include "ui/gui.h"
#include "util/file_system.h"
#include "game/sprites.h"
#include "game/creatures.h"
#include "game/materials.h"
#include "brushes/brush.h"
#include "brushes/managers/brush_manager.h"
#include "ui/managers/loading_manager.h"
#include "ui/tool_options_window.h"
#include "item_definitions/core/asset_bundle_loader.h"
#include "item_definitions/core/item_definition_store.h"

VersionManager g_version;

VersionManager::VersionManager() :
	loaded_version(CLIENT_VERSION_NONE) {
}

VersionManager::~VersionManager() {
	// UnloadVersion is handled explicitly by Application::Unload() during graceful shutdown.
	// Calling it here during static deinitialization causes use-after-free for singletons
	// like SpritePreloader that may have already been destructed.
}

bool VersionManager::LoadVersion(ClientVersionID version, wxString& error, std::vector<std::string>& warnings, bool force) {
	if (ClientVersion::get(version) == nullptr) {
		error = "Unsupported client version! (8)";
		return false;
	}

	if (version != loaded_version || force) {
		if (getLoadedVersion() != nullptr) {
			// There is another version loaded right now, save window layout
			g_gui.SavePerspective();
		}

		// Disable all rendering so the data is not accessed while reloading
		UnnamedRenderingLock();
		g_gui.DestroyPalettes();
		g_gui.DestroyMinimap();

		// Destroy the previous version
		UnloadVersion();

		loaded_version = version;
		if (!getLoadedVersion()->hasValidPaths()) {
			if (!getLoadedVersion()->loadValidPaths()) {
				error = "Couldn't load relevant asset files";
				loaded_version = CLIENT_VERSION_NONE;
				return false;
			}
		}

		bool ret = LoadDataFiles(error, warnings);
		if (ret) {
			g_gui.LoadPerspective();
		} else {
			loaded_version = CLIENT_VERSION_NONE;
		}

		return ret;
	}
	return true;
}

ClientVersionID VersionManager::GetCurrentVersionID() const {
	if (!loaded_version.empty()) {
		return getLoadedVersion()->getID();
	}
	return CLIENT_VERSION_NONE;
}

const ClientVersion& VersionManager::GetCurrentVersion() const {
	assert(!loaded_version.empty());
	return *getLoadedVersion();
}

bool VersionManager::LoadDataFiles(wxString& error, std::vector<std::string>& warnings) {
	FileName data_path = getLoadedVersion()->getDataPath();
	FileName extension_path = FileSystem::GetExtensionsDirectory();

	g_gui.gfx.client_version = getLoadedVersion();

	// OTFI loading removed. Metadata and sprite files are configured via clients.toml or defaults in ClientVersion.

	g_loading.CreateLoadBar("Loading asset files");
	g_loading.SetLoadDone(0, "Loading canonical asset bundle...");

	wxFileName metadata_path = getLoadedVersion()->getMetadataPath();
	wxFileName sprites_path = getLoadedVersion()->getSpritesPath();
	wxString base_data_path = data_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);

	AssetLoadRequest asset_request;
	asset_request.mode = getLoadedVersion()->getItemDefinitionMode();
	asset_request.client_version = getLoadedVersion();
	asset_request.dat_path = metadata_path;
	asset_request.spr_path = sprites_path;
	std::string otb_file = getLoadedVersion()->getOtbFile();
	if (otb_file.empty()) {
		otb_file = "items.otb";
	}
	wxFileName otb_path(otb_file);
	if (otb_path.IsAbsolute()) {
		asset_request.otb_path = otb_path;
	} else {
		asset_request.otb_path = wxFileName(base_data_path + wxString(otb_file));
	}
	asset_request.xml_path = wxFileName(base_data_path + "items.xml");

	AssetBundle bundle;
	AssetBundleLoader bundle_loader;
	if (!bundle_loader.load(asset_request, bundle, error, warnings)) {
		error = "Couldn't load canonical asset bundle: " + error;
		g_loading.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_loading.SetLoadDone(20, "Installing graphics...");
	if (!bundle_loader.install(bundle, g_gui.gfx, g_item_definitions, error, warnings)) {
		error = "Couldn't install canonical asset bundle: " + error;
		g_loading.DestroyLoadBar();
		UnloadVersion();
		return false;
	}

	g_loading.SetLoadDone(35, "Loading creatures.xml ...");
	if (!g_creatures.loadFromXML(base_data_path + "creatures.xml", true, error, warnings)) {
		warnings.push_back(std::format("Couldn't load creatures.xml: {}", error.ToStdString()));
	}

	// Load creatures.json from data directory if it exists
	if (wxFileName::FileExists(base_data_path + "creatures.json")) {
		g_loading.SetLoadDone(47, "Loading creatures.json ...");
		if (!g_creatures.loadFromJSON(base_data_path + "creatures.json", true, error, warnings)) {
			warnings.push_back(std::format("Couldn't load creatures.json: {}", error.ToStdString()));
		}
	}

	g_loading.SetLoadDone(50, "Loading materials.xml ...");
	if (!g_materials.loadMaterials(base_data_path + "materials.xml", error, warnings)) {
		warnings.push_back("Couldn't load materials.xml: " + std::string(error.mb_str()));
	}

	g_loading.SetLoadDone(70, "Loading extensions...");
	if (!g_materials.loadExtensions(extension_path, error, warnings)) {
		warnings.push_back("Couldn't load extensions: " + std::string(error.mb_str()));
		spdlog::warn("Couldn't load extensions: {}", error.ToStdString());
	}

	g_loading.SetLoadDone(70, "Finishing...");
	g_brushes.init();
	g_materials.createOtherTileset();

	// Load creatures from custom JSON path AFTER tilesets are created
	std::string custom_json_path = g_settings.getString(Config::CREATURES_JSON_PATH);
	if (!custom_json_path.empty()) {
		if (wxFileName::FileExists(custom_json_path)) {
			spdlog::info("Loading custom creatures from: {}", custom_json_path);
			if (!g_creatures.loadFromJSON(FileName(custom_json_path), false, error, warnings)) {
				spdlog::warn("Failed to load custom creatures.json: {}", error.ToStdString());
				warnings.push_back(std::format("Couldn't load custom creatures.json '{}': {}", custom_json_path, error.ToStdString()));
			}
		} else {
			spdlog::warn("Custom creatures.json path does not exist: {}", custom_json_path);
		}
	}

	g_loading.DestroyLoadBar();
	return true;
}

bool VersionManager::ReloadBrushes(wxString& error, std::vector<std::string>& warnings) {
	if (loaded_version.empty()) {
		error = "No version loaded";
		return false;
	}

	// Disable rendering while reloading
	UnnamedRenderingLock();

	// Save and destroy palettes
	g_gui.SavePerspective();
	g_gui.DestroyPalettes();

	// Clear brush manager (current_brush, previous_brush, all special brushes)
	g_brush_manager.Clear();

	if (g_gui.tool_options) {
		g_gui.tool_options->Clear();
	}

	// Clear materials and brushes (but NOT items, sprites, or creatures)
	g_materials.clear();
	g_brushes.clear();

	// Reset item brush references (keep items loaded)
	g_item_definitions.resetBrushData();

	// Reset creature brush references (keep creatures loaded)
	for (auto iter = g_creatures.begin(); iter != g_creatures.end(); ++iter) {
		CreatureType* type = iter->second;
		type->brush = nullptr;
		type->in_other_tileset = false;
	}

	// Reload materials.xml and all included files
	FileName data_path = getLoadedVersion()->getDataPath();
	wxString base_data_path = data_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);

	if (!g_materials.loadMaterials(base_data_path + "materials.xml", error, warnings)) {
		warnings.push_back("Couldn't reload materials.xml: " + std::string(error.mb_str()));
	}

	// Reload extensions
	FileName extension_path = FileSystem::GetExtensionsDirectory();
	if (!g_materials.loadExtensions(extension_path, error, warnings)) {
		// Extensions are optional
	}

	// Reinitialize brushes and create Other tileset
	g_brushes.init();
	g_materials.createOtherTileset();

	// Restore palettes
	g_gui.LoadPerspective();

	// Refresh view
	g_gui.RefreshView();

	return true;
}

void VersionManager::UnloadVersion() {
	UnnamedRenderingLock();
	g_gui.gfx.clear();
	if (g_gui.tool_options) {
		g_gui.tool_options->Clear();
	}
	g_brush_manager.Clear();

	if (!loaded_version.empty()) {
		g_materials.clear();
		g_brushes.clear();
		g_item_definitions.clear();
		g_gui.gfx.clear();

		g_creatures.clear();

		loaded_version = CLIENT_VERSION_NONE;
	}
}
