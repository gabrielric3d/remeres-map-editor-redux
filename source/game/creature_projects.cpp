//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "game/creature_projects.h"

#include "app/settings.h"
#include "game/creatures.h"
#include "rendering/ui/toast_renderer.h"
#include "ui/gui.h"
#include "ui/managers/status_manager.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>

#include <algorithm>
#include <map>
#include <set>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace {
	// Creature database generation each project was last imported into (by ProjectKey).
	std::map<std::string, uint64_t> loaded_projects;

	std::string ToUTF8(const wxString& text) {
		return text.ToStdString(wxConvUTF8);
	}

	// Absolute path with one separator style and no trailing separator, lower case on
	// Windows, so a map path can be matched against a project folder by prefix.
	wxString NormalizeForCompare(const wxString& path) {
		wxFileName name(path);
		name.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_TILDE);
		const wxString separator(wxFileName::GetPathSeparator());
		wxString full = name.GetFullPath();
		full.Replace("/", separator);
		while (full.length() > 1 && full.EndsWith(separator)) {
			full.RemoveLast();
		}
#ifdef __WINDOWS__
		full.MakeLower();
#endif
		return full;
	}

	std::string ProjectKey(const CreatureProject& project) {
		std::string key = ToUTF8(project.name) + "|" + ToUTF8(NormalizeForCompare(project.map_folder)) + "|" + ToUTF8(project.exclude_folders);
		for (const wxString& source : project.sources) {
			key += "|" + ToUTF8(source);
		}
		return key;
	}

	std::set<wxString> ExcludedNames(const CreatureProject& project) {
		std::set<wxString> names;
		wxStringTokenizer tokens(project.exclude_folders, ",;");
		while (tokens.HasMoreTokens()) {
			wxString token = tokens.GetNextToken().Trim().Trim(false).Lower();
			if (!token.empty()) {
				names.insert(token);
			}
		}
		return names;
	}

	// True when a folder between the source root and the file has an excluded name.
	bool IsInExcludedFolder(const wxString& root, const wxString& file, const std::set<wxString>& excluded) {
		if (excluded.empty()) {
			return false;
		}
		wxFileName relative(file);
		relative.MakeRelativeTo(root);
		for (const wxString& dir : relative.GetDirs()) {
			if (excluded.contains(dir.Lower())) {
				return true;
			}
		}
		return false;
	}
}

namespace CreatureProjects {

std::vector<CreatureProject> Load() {
	std::vector<CreatureProject> projects;
	const std::string raw = g_settings.getString(Config::CREATURE_PROJECTS);
	if (raw.empty()) {
		return projects;
	}

	nlohmann::json root = nlohmann::json::parse(raw, nullptr, false);
	if (root.is_discarded() || !root.is_object() || !root.contains("projects") || !root["projects"].is_array()) {
		spdlog::warn("Creature projects: could not read the saved projects, ignoring them");
		return projects;
	}

	for (const auto& entry : root["projects"]) {
		if (!entry.is_object()) {
			continue;
		}
		CreatureProject project;
		project.name = wxString::FromUTF8(entry.value("name", ""));
		project.map_folder = wxString::FromUTF8(entry.value("map_folder", ""));
		project.exclude_folders = wxString::FromUTF8(entry.value("exclude_folders", "backup"));
		project.enabled = entry.value("enabled", true);
		if (entry.contains("sources") && entry["sources"].is_array()) {
			for (const auto& source : entry["sources"]) {
				if (source.is_string()) {
					project.sources.push_back(wxString::FromUTF8(source.get<std::string>()));
				}
			}
		}
		projects.push_back(std::move(project));
	}
	return projects;
}

void Save(const std::vector<CreatureProject>& projects) {
	nlohmann::json list = nlohmann::json::array();
	for (const CreatureProject& project : projects) {
		nlohmann::json sources = nlohmann::json::array();
		for (const wxString& source : project.sources) {
			sources.push_back(ToUTF8(source));
		}
		list.push_back({
			{ "name", ToUTF8(project.name) },
			{ "map_folder", ToUTF8(project.map_folder) },
			{ "sources", sources },
			{ "exclude_folders", ToUTF8(project.exclude_folders) },
			{ "enabled", project.enabled },
		});
	}
	nlohmann::json root = { { "projects", list } };
	g_settings.setString(Config::CREATURE_PROJECTS, root.dump());
	g_settings.save();
}

const CreatureProject* FindForMap(const std::vector<CreatureProject>& projects, const wxString& map_path) {
	const wxString map = NormalizeForCompare(map_path);
	const wxString separator(wxFileName::GetPathSeparator());

	const CreatureProject* best = nullptr;
	size_t best_length = 0;
	for (const CreatureProject& project : projects) {
		if (!project.enabled || project.map_folder.empty()) {
			continue;
		}
		const wxString folder = NormalizeForCompare(project.map_folder);
		if (folder.length() > best_length && map.StartsWith(folder + separator)) {
			best = &project;
			best_length = folder.length();
		}
	}
	return best;
}

wxArrayString CollectFiles(const CreatureProject& project) {
	const std::set<wxString> excluded = ExcludedNames(project);

	wxArrayString files;
	for (const wxString& source : project.sources) {
		if (wxFileName::DirExists(source)) {
			wxArrayString found;
			wxDir::GetAllFiles(source, &found, "*.xml", wxDIR_FILES | wxDIR_DIRS);
			wxDir::GetAllFiles(source, &found, "*.json", wxDIR_FILES | wxDIR_DIRS);
			// Sorted so that, inside one source, which duplicate wins does not depend on the file system.
			found.Sort();
			for (const wxString& file : found) {
				if (!IsInExcludedFolder(source, file, excluded)) {
					files.Add(file);
				}
			}
		} else if (wxFileName::FileExists(source)) {
			files.Add(source);
		} else {
			spdlog::warn("Creature project '{}': source not found '{}'", ToUTF8(project.name), ToUTF8(source));
		}
	}
	return files;
}

CreatureImportResult ImportFiles(const wxArrayString& files, const wxString& load_bar_title) {
	CreatureImportResult result;
	result.files_scanned = files.GetCount();
	result.missing_before = g_creatures.countMissing();
	const size_t total_before = g_creatures.size();

	g_gui.CreateLoadBar(load_bar_title);

	for (size_t i = 0; i < files.GetCount(); ++i) {
		const wxString& path = files[i];
		const int percent = static_cast<int>((i * 100) / files.GetCount());
		g_gui.SetLoadDone(percent, wxString::Format("Loading %zu of %zu: %s", i + 1, files.GetCount(), wxFileName(path).GetFullName()));

		wxString error;
		std::vector<std::string> warnings;
		const wxString ext = wxFileName(path).GetExt().Lower();

		bool ok = false;
		if (ext == "json") {
			ok = g_creatures.loadFromJSON(FileName(path), false, error, warnings);
		} else if (ext == "xml") {
			ok = g_creatures.importXMLFromOT(FileName(path), error, warnings);
		} else {
			continue;
		}

		if (ok) {
			++result.files_loaded;
			result.warnings.insert(result.warnings.end(), warnings.begin(), warnings.end());
		} else {
			result.failed_files.push_back(path + ": " + error);
		}
	}

	g_gui.SetLoadDone(100, "Done");
	g_gui.DestroyLoadBar();

	result.missing_after = g_creatures.countMissing();
	const size_t total_after = g_creatures.size();
	result.creatures_added = total_after > total_before ? total_after - total_before : 0;
	return result;
}

CreatureImportResult LoadProject(const CreatureProject& project) {
	const wxArrayString files = CollectFiles(project);
	CreatureImportResult result = ImportFiles(files, "Loading creatures of " + project.name + "...");
	loaded_projects[ProjectKey(project)] = g_creatures.getGeneration();

	spdlog::info("Creature project '{}': scanned={}, loaded={}, failed={}, added={}, missing {}->{}",
		ToUTF8(project.name), result.files_scanned, result.files_loaded, result.failed_files.size(),
		result.creatures_added, result.missing_before, result.missing_after);
	for (const wxString& failed : result.failed_files) {
		spdlog::warn("Creature project '{}': failed {}", ToUTF8(project.name), ToUTF8(failed));
	}
	return result;
}

void LoadForMap(const wxString& map_path) {
	const std::vector<CreatureProject> projects = Load();
	const CreatureProject* project = FindForMap(projects, map_path);
	if (!project) {
		return;
	}

	const auto loaded = loaded_projects.find(ProjectKey(*project));
	if (loaded != loaded_projects.end() && loaded->second == g_creatures.getGeneration()) {
		return;
	}

	const CreatureImportResult result = LoadProject(*project);

	wxString message = wxString::Format("%s: %zu creature files loaded", project->name, result.files_loaded);
	if (result.missing_after > 0) {
		message += wxString::Format(", %zu still missing", result.missing_after);
	}
	g_status.SetStatusText(message);
	g_toast.Show(ToUTF8(message), 4000);
}

wxString Describe(const CreatureImportResult& result) {
	wxString summary;
	summary << "Files scanned: " << result.files_scanned << "\n";
	summary << "Files loaded successfully: " << result.files_loaded << "\n";
	summary << "Files failed: " << result.failed_files.size() << "\n\n";
	summary << "New creatures added: " << result.creatures_added << "\n";
	const size_t resolved = result.missing_before > result.missing_after ? result.missing_before - result.missing_after : 0;
	summary << "Missing creatures resolved: " << resolved << " of " << result.missing_before << "\n";
	summary << "Still missing: " << result.missing_after;

	if (!result.failed_files.empty()) {
		summary << "\n\nFailed files:";
		for (size_t i = 0; i < result.failed_files.size() && i < 10; ++i) {
			summary << "\n  - " << result.failed_files[i];
		}
		if (result.failed_files.size() > 10) {
			summary << wxString::Format("\n  ... and %zu more", result.failed_files.size() - 10);
		}
	}
	return summary;
}

} // namespace CreatureProjects
