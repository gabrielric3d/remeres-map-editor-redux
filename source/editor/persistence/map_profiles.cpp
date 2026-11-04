//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "editor/persistence/map_profiles.h"

#include "app/client_version.h"
#include "app/settings.h"
#include "editor/editor.h"
#include "editor/editor_factory.h"
#include "editor/managers/editor_manager.h"
#include "editor/map_layers.h"
#include "editor/persistence/editor_persistence.h"
#include "game/creature_projects.h"
#include "io/iomap_otbm.h"
#include "ui/dialog_util.h"
#include "ui/gui.h"
#include "ui/main_frame.h"
#include "ui/map_layers_panel.h"
#include "ui/managers/minimap_manager.h"
#include "ui/managers/status_manager.h"
#include "ui/map_tab.h"
#include "ui/map_window.h"

#include <wx/filename.h>

#include <map>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace {
	std::string ToUTF8(const wxString& text) {
		return text.ToStdString(wxConvUTF8);
	}

	// Lower case on Windows so a file listed twice with a different case still matches.
	wxString CompareKey(const MapProfileEntry& entry) {
		wxFileName name(entry.file);
		name.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
		wxString key = name.GetFullPath();
#ifdef __WINDOWS__
		key.MakeLower();
#endif
		return key + wxString::Format("|%d|%d", entry.offset_x, entry.offset_y);
	}

	wxString FormatCount(uint64_t value) {
		wxString digits = wxString::Format("%llu", static_cast<unsigned long long>(value));
		wxString out;
		const int length = static_cast<int>(digits.length());
		for (int i = 0; i < length; ++i) {
			if (i > 0 && (length - i) % 3 == 0) {
				out += '.';
			}
			out += digits[i];
		}
		return out;
	}
}

namespace MapProfiles {

std::vector<MapProfile> Load() {
	std::vector<MapProfile> profiles;
	const std::string raw = g_settings.getString(Config::MAP_PROFILES);
	if (raw.empty()) {
		return profiles;
	}

	nlohmann::json root = nlohmann::json::parse(raw, nullptr, false);
	if (root.is_discarded() || !root.is_object() || !root.contains("profiles") || !root["profiles"].is_array()) {
		spdlog::warn("Map profiles: could not read the saved profiles, ignoring them");
		return profiles;
	}

	for (const auto& item : root["profiles"]) {
		if (!item.is_object()) {
			continue;
		}
		MapProfile profile;
		profile.name = wxString::FromUTF8(item.value("name", ""));
		profile.maps_xml = wxString::FromUTF8(item.value("maps_xml", ""));
		if (item.contains("maps") && item["maps"].is_array()) {
			for (const auto& map : item["maps"]) {
				if (!map.is_object()) {
					continue;
				}
				MapProfileEntry entry;
				entry.name = wxString::FromUTF8(map.value("name", ""));
				entry.file = wxString::FromUTF8(map.value("file", ""));
				entry.offset_x = map.value("offset_x", 0);
				entry.offset_y = map.value("offset_y", 0);
				entry.load_houses = map.value("load_houses", false);
				entry.enabled = map.value("enabled", true);
				profile.maps.push_back(std::move(entry));
			}
		}
		profiles.push_back(std::move(profile));
	}
	return profiles;
}

void Save(const std::vector<MapProfile>& profiles) {
	nlohmann::json list = nlohmann::json::array();
	for (const MapProfile& profile : profiles) {
		nlohmann::json maps = nlohmann::json::array();
		for (const MapProfileEntry& entry : profile.maps) {
			maps.push_back({
				{ "name", ToUTF8(entry.name) },
				{ "file", ToUTF8(entry.file) },
				{ "offset_x", entry.offset_x },
				{ "offset_y", entry.offset_y },
				{ "load_houses", entry.load_houses },
				{ "enabled", entry.enabled },
			});
		}
		list.push_back({
			{ "name", ToUTF8(profile.name) },
			{ "maps_xml", ToUTF8(profile.maps_xml) },
			{ "maps", maps },
		});
	}
	nlohmann::json root = { { "profiles", list } };
	g_settings.setString(Config::MAP_PROFILES, root.dump());
	g_settings.save();
}

bool ReadServerMapsXml(const wxString& path, std::vector<MapProfileEntry>& maps, wxString& error) {
	maps.clear();

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(path.wc_str()); // wide path: folders with accents
	if (!result) {
		error = wxString::Format("Could not read \"%s\": %s", path, wxString::FromUTF8(result.description()));
		return false;
	}
	pugi::xml_node root = doc.child("maps");
	if (!root) {
		error = wxString::Format("\"%s\" has no <maps> root node.", path);
		return false;
	}

	wxFileName xml_name(path);
	xml_name.MakeAbsolute();
	const wxString world_dir = xml_name.GetPath();
	// The server runs from the folder that holds data/world.
	wxFileName server_dir = wxFileName::DirName(world_dir);
	if (server_dir.GetDirCount() >= 2) {
		server_dir.RemoveLastDir();
		server_dir.RemoveLastDir();
	}
	const wxString server_path = server_dir.GetPath();

	bool any_houses = false;
	for (pugi::xml_node node : root.children("map")) {
		wxString file = wxString::FromUTF8(node.attribute("file").as_string());
		if (file.empty()) {
			continue;
		}
		file.Replace("\\", "/");
		if (!file.Lower().EndsWith(".otbm")) {
			file += ".otbm";
		}

		wxFileName resolved(file);
		if (!resolved.IsAbsolute()) {
			resolved = wxFileName((file.StartsWith("data/") ? server_path : world_dir) + wxFileName::GetPathSeparator() + file);
		}
		resolved.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);

		MapProfileEntry entry;
		entry.file = resolved.GetFullPath();
		entry.name = wxString::FromUTF8(node.attribute("name").as_string());
		if (entry.name.empty()) {
			entry.name = resolved.GetName();
		}
		entry.offset_x = node.attribute("offsetX").as_int();
		entry.offset_y = node.attribute("offsetY").as_int();
		entry.load_houses = node.attribute("load-house").as_bool(false);
		any_houses = any_houses || entry.load_houses;
		maps.push_back(std::move(entry));
	}

	if (!any_houses && !maps.empty()) {
		maps.front().load_houses = true;
	}
	return true;
}

bool SyncFromServer(MapProfile& profile, wxString& error) {
	std::vector<MapProfileEntry> fresh;
	if (!ReadServerMapsXml(profile.maps_xml, fresh, error)) {
		return false;
	}
	std::map<wxString, bool> enabled_by_key;
	for (const MapProfileEntry& entry : profile.maps) {
		enabled_by_key[CompareKey(entry)] = entry.enabled;
	}
	for (MapProfileEntry& entry : fresh) {
		if (auto it = enabled_by_key.find(CompareKey(entry)); it != enabled_by_key.end()) {
			entry.enabled = it->second;
		}
	}
	profile.maps = std::move(fresh);
	return true;
}

wxString FindServerMapsXml(const wxString& map_path) {
	if (map_path.empty()) {
		return "";
	}
	wxFileName dir = wxFileName::DirName(wxFileName(map_path).GetPath());
	for (int level = 0; level < 10 && dir.GetDirCount() > 0; ++level) {
		const wxString base = dir.GetPath() + wxFileName::GetPathSeparator();
		const wxString candidates[] = {
			base + "data" + wxFileName::GetPathSeparator() + "world" + wxFileName::GetPathSeparator() + "maps.xml",
			base + "maps.xml",
		};
		for (const wxString& candidate : candidates) {
			if (wxFileName::FileExists(candidate)) {
				return candidate;
			}
		}
		dir.RemoveLastDir();
	}
	return "";
}

}

namespace {
	// Re-reads a linked profile and lists the enabled maps that exist; reports and returns
	// false when there is none. The first map decides the client, like opening it alone would;
	// a client given in load_options (start screen) wins only when it reads that map, and is
	// dropped from load_options otherwise -- it may have been picked for another map.
	bool PrepareOpen(MapProfile& profile, MapLoadOptions& load_options, std::vector<const MapProfileEntry*>& to_load, std::vector<std::string>& missing, std::vector<std::string>& summary) {
		g_gui.FinishWelcomeDialog();

		if (!profile.maps_xml.empty()) {
			wxString error;
			if (!MapProfiles::SyncFromServer(profile, error)) {
				summary.push_back("Could not re-read the server maps.xml, using the last known list:");
				summary.push_back("  " + ToUTF8(error));
				summary.push_back("");
			}
		}

		for (const MapProfileEntry& entry : profile.maps) {
			if (!entry.enabled) {
				continue;
			}
			if (wxFileName::FileExists(entry.file)) {
				to_load.push_back(&entry);
			} else {
				missing.push_back("  " + ToUTF8(entry.name) + ": " + ToUTF8(entry.file));
			}
		}
		if (to_load.empty()) {
			wxString message = "None of the enabled maps of \"" + profile.name + "\" could be found.";
			for (const std::string& line : missing) {
				message += "\n" + wxString::FromUTF8(line);
			}
			DialogUtil::PopupDialog("Map profile", message, wxOK | wxICON_WARNING);
			return false;
		}

		if (load_options.hasSelectedClient()) {
			const ClientVersion* client = ClientVersion::get(load_options.selected_client_id);
			MapVersion version;
			// Same test the map loader makes (EditorPersistence::loadMap).
			if (!client || !IOMapOTBM::getVersionInfo(FileName(to_load.front()->file), version) || client->getProtocolID() != version.client) {
				load_options.selected_client_id.clear();
			}
		}

		g_status.SetStatusText("Loading map profile...");
		try {
			if (!g_editors.EnsureClientForMap(FileName(to_load.front()->file), load_options)) {
				return false;
			}
		} catch (std::runtime_error& e) {
			g_status.SetStatusText("Failed to load map profile.");
			DialogUtil::PopupDialog(g_gui.root, "Error!", wxString(e.what(), wxConvUTF8), wxOK);
			return false;
		}
		return true;
	}

	// A tab that already has that file open (and is not a layer yet).
	MapTab* FindOpenMapTab(const wxString& file) {
		const FileName wanted(file);
		for (int index = 0; index < g_gui.tabbook->GetTabCount(); ++index) {
			auto* tab = dynamic_cast<MapTab*>(g_gui.tabbook->GetTab(index));
			if (!tab || MapLayers::IsLayer(tab->GetEditor()) || !tab->GetMap()->hasFile()) {
				continue;
			}
			if (FileName(wxstr(tab->GetMap()->getFilename())).SameAs(wanted)) {
				return tab;
			}
		}
		return nullptr;
	}

	void AppendSection(std::vector<std::string>& summary, const char* title, const std::vector<std::string>& lines) {
		if (lines.empty()) {
			return;
		}
		summary.push_back("");
		summary.push_back(title);
		summary.insert(summary.end(), lines.begin(), lines.end());
	}
}

namespace MapProfiles {

bool OpenAsLayers(MapProfile& profile, const MapLoadOptions& requested_options) {
	MapLoadOptions load_options = requested_options;
	std::vector<const MapProfileEntry*> to_load;
	std::vector<std::string> missing;
	std::vector<std::string> summary;
	if (!PrepareOpen(profile, load_options, to_load, missing, summary)) {
		return false;
	}

	MapLayerGroup& group = MapLayers::CreateGroup(profile.name);
	std::vector<MapTab*> tabs;
	std::vector<std::string> failed;
	std::vector<std::string> warnings;
	for (size_t i = 0; i < to_load.size(); ++i) {
		const MapProfileEntry& entry = *to_load[i];
		g_status.SetStatusText(wxString::Format("Opening layer %s (%zu/%zu)...", entry.name, i + 1, to_load.size()));

		// Already open on its own: that editor becomes the layer. A second copy of the file
		// would be a trap -- saving one would overwrite the other's changes.
		if (MapTab* open_tab = FindOpenMapTab(entry.file)) {
			MapLayer layer;
			layer.editor = open_tab->GetEditor();
			layer.tab = open_tab;
			layer.name = entry.name;
			layer.offset_x = entry.offset_x;
			layer.offset_y = entry.offset_y;
			group.layers.push_back(layer);
			tabs.push_back(open_tab);
			continue;
		}

		std::unique_ptr<Editor> editor;
		try {
			editor = EditorFactory::LoadFromFile(g_gui.copybuffer, FileName(entry.file), load_options);
		} catch (std::runtime_error& e) {
			failed.push_back("  " + ToUTF8(entry.name) + ": " + e.what());
			spdlog::warn("Map profile \"{}\": failed to open layer {}: {}", ToUTF8(profile.name), ToUTF8(entry.file), e.what());
			continue;
		}
		for (const std::string& warning : editor->map.getWarnings()) {
			warnings.push_back(ToUTF8(entry.name) + ": " + warning);
		}
		editor->map.clearChanges();

		// Into the group before the tab exists: the tab title and scroll range come from it.
		MapLayer layer;
		layer.editor = editor.get();
		layer.name = entry.name;
		layer.offset_x = entry.offset_x;
		layer.offset_y = entry.offset_y;
		group.layers.push_back(layer);

		auto* tab = newd MapTab(g_gui.tabbook, editor.release());
		tab->OnSwitchEditorMode(SELECTION_MODE);
		group.layers.back().tab = tab;
		tabs.push_back(tab);
	}

	if (tabs.empty()) {
		MapLayers::DiscardEmptyGroups();
		g_status.SetStatusText("Failed to load map profile.");
		failed.insert(failed.begin(), "No map of \"" + ToUTF8(profile.name) + "\" could be opened:");
		DialogUtil::ListDialog("Map profile", failed);
		return false;
	}

	// Every layer now scrolls over the size of the biggest map.
	for (MapTab* tab : tabs) {
		tab->FitToMap();
	}

	// The first layer starts as the active one, and the group keeps that single page: the
	// other layers leave the tab bar (alive, hidden) and come back through the layers panel.
	MapTab* first = tabs.front();
	MapLayers::Activate(first->GetEditor());
	for (size_t i = 1; i < tabs.size(); ++i) {
		const int page = g_gui.tabbook->GetTabIndex(tabs[i]->GetWindow());
		if (page != wxNOT_FOUND) {
			g_gui.tabbook->DetachTab(page);
		}
	}
	g_gui.FitViewToMap(first);
	first->GetView()->ResetMinimapViewportToCurrentView();
	g_status.UpdateTitle();

	CreatureProjects::LoadForMap(wxstr(first->GetMap()->getFilename()));
	g_gui.root->DoQueryImportCreatures();
	g_gui.RefreshPalettes(first->GetMap());
	g_gui.root->UpdateMenubar();
	g_minimap.InvalidateAll(*first->GetMap());
	g_gui.UpdateMinimap(true);
	MapLayersPanel::ShowPane();

	g_status.SetStatusText(wxString::Format("Map profile \"%s\": %zu layer(s) open. The tab in front is the layer you edit.", profile.name, tabs.size()));
	if (!missing.empty() || !failed.empty() || !summary.empty()) {
		summary.push_back(wxString::Format("\"%s\": %zu of %zu map(s) opened as layers.", profile.name, tabs.size(), to_load.size() + missing.size()).ToStdString(wxConvUTF8));
		AppendSection(summary, "Not found (skipped):", missing);
		AppendSection(summary, "Failed to open:", failed);
		DialogUtil::ListDialog("Map profile", summary);
	}
	if (!warnings.empty()) {
		DialogUtil::ListDialog("Map loader warnings", warnings);
	}
	return true;
}

bool OpenMerged(MapProfile& profile, const MapLoadOptions& requested_options) {
	MapLoadOptions load_options = requested_options;
	std::vector<const MapProfileEntry*> to_load;
	std::vector<std::string> missing;
	std::vector<std::string> summary;
	if (!PrepareOpen(profile, load_options, to_load, missing, summary)) {
		return false;
	}

	std::unique_ptr<Editor> editor = EditorFactory::CreateEmptyForCurrentClient(g_gui.copybuffer);
	editor->map.setName(ToUTF8(profile.name) + ".otbm");

	std::vector<std::string> warnings;
	std::vector<std::string> failed;
	std::vector<std::string> loaded_lines;
	wxString first_loaded_file;
	uint64_t total_tiles = 0;
	for (size_t i = 0; i < to_load.size(); ++i) {
		const MapProfileEntry& entry = *to_load[i];
		const wxString title = wxString::Format("Loading %s (%zu/%zu)...", entry.name, i + 1, to_load.size());
		g_status.SetStatusText(title);

		MapImportReport report;
		const ImportType house_import = entry.load_houses ? IMPORT_MERGE : IMPORT_DONT;
		if (!EditorPersistence::importMapQuiet(*editor, FileName(entry.file), entry.offset_x, entry.offset_y, house_import, IMPORT_MERGE, title, report)) {
			failed.push_back("  " + ToUTF8(entry.name) + ": " + ToUTF8(report.error));
			spdlog::warn("Map profile \"{}\": failed to load {}: {}", ToUTF8(profile.name), ToUTF8(entry.file), ToUTF8(report.error));
			continue;
		}
		if (first_loaded_file.empty()) {
			first_loaded_file = entry.file;
		}
		total_tiles += report.tiles_imported;

		wxString line = "  " + entry.name + ": " + FormatCount(report.tiles_imported) + " tiles";
		if (entry.offset_x != 0 || entry.offset_y != 0) {
			line += wxString::Format(" (offset %d, %d)", entry.offset_x, entry.offset_y);
		}
		if (report.tiles_overlapped > 0) {
			line += wxString::Format(", %s on top of earlier maps (first at %d, %d, %d)", FormatCount(report.tiles_overlapped),
				report.first_overlap.x, report.first_overlap.y, report.first_overlap.z);
		}
		if (report.tiles_discarded > 0) {
			line += ", " + FormatCount(report.tiles_discarded) + " discarded (invalid position)";
		}
		loaded_lines.push_back(ToUTF8(line));
		for (const std::string& warning : report.warnings) {
			warnings.push_back(ToUTF8(entry.name) + ": " + warning);
		}
	}

	if (loaded_lines.empty()) {
		g_status.SetStatusText("Failed to load map profile.");
		failed.insert(failed.begin(), "No map of \"" + ToUTF8(profile.name) + "\" could be loaded:");
		DialogUtil::ListDialog("Map profile", failed);
		return false;
	}

	editor->map.clearChanges();

	auto* mapTab = newd MapTab(g_gui.tabbook, editor.release());
	mapTab->OnSwitchEditorMode(SELECTION_MODE);
	mapTab->GetView()->FitToMap();
	g_status.UpdateTitle();

	// Creatures of the project the maps belong to, before asking about missing ones.
	CreatureProjects::LoadForMap(first_loaded_file);
	g_gui.root->DoQueryImportCreatures();

	g_gui.FitViewToMap(mapTab);
	g_gui.RefreshPalettes();
	g_gui.root->UpdateMenubar();
	mapTab->GetView()->ResetMinimapViewportToCurrentView();
	g_minimap.InvalidateAll(*mapTab->GetMap());
	g_gui.UpdateMinimap(true);

	summary.push_back(wxString::Format("\"%s\": %zu of %zu map(s) loaded, %s tiles.", profile.name, loaded_lines.size(), to_load.size() + missing.size(), FormatCount(total_tiles)).ToStdString(wxConvUTF8));
	summary.push_back("This is a new unsaved map: Save asks for a file, the source maps are left alone.");
	summary.push_back("");
	summary.insert(summary.end(), loaded_lines.begin(), loaded_lines.end());
	AppendSection(summary, "Not found (skipped):", missing);
	AppendSection(summary, "Failed to load:", failed);

	g_status.SetStatusText(wxString::Format("Map profile \"%s\" loaded: %zu map(s).", profile.name, loaded_lines.size()));
	DialogUtil::ListDialog("Map profile loaded", summary);
	if (!warnings.empty()) {
		DialogUtil::ListDialog("Map loader warnings", warnings);
	}
	return true;
}

}
