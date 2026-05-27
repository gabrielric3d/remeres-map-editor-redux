#include "ui/menubar/file_menu_handler.h"
#include "app/application.h"
#include "app/main.h"
#include "ui/gui.h"
#include "ui/map/export_tilesets_window.h"
#include "ui/map/export_minimap_window.h"
#include "ui/map/merge_maps_minimap_window.h"

#include "ui/map/import_map_window.h"
#include "ui/dialog_util.h"
#include "ui/about_window.h"
#include "ui/dat_debug_view.h"
#include "app/preferences.h"
#include "ui/extension_window.h"
#include "game/creatures.h"
#include "app/managers/version_manager.h"
#include "ui/controls/sortable_list_box.h"
#include "editor/editor.h"
#include "io/map_xml_io.h"
#include "map/map.h"

FileMenuHandler::FileMenuHandler(MainFrame* frame, MainMenuBar* menubar) :
	frame(frame), menubar(menubar) {
}

FileMenuHandler::~FileMenuHandler() = default;

void FileMenuHandler::OnNew(wxCommandEvent& WXUNUSED(event)) {
	g_gui.NewMap();
}

void FileMenuHandler::OnOpen(wxCommandEvent& WXUNUSED(event)) {
	g_gui.OpenMap();
}

void FileMenuHandler::OnSave(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SaveMap();
}

void FileMenuHandler::OnSaveAs(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SaveMapAs();
}

void FileMenuHandler::OnClose(wxCommandEvent& WXUNUSED(event)) {
	frame->DoQuerySave(true); // It closes the editor too
}

void FileMenuHandler::OnQuit(wxCommandEvent& WXUNUSED(event)) {
	g_gui.root->Close();
}

void FileMenuHandler::OnGenerateMap(wxCommandEvent& WXUNUSED(event)) {
	// Not implemented in original code
}

void FileMenuHandler::OnImportMap(wxCommandEvent& WXUNUSED(event)) {
	ASSERT(g_gui.GetCurrentEditor());
	wxDialog* importmap = newd ImportMapWindow(frame, *g_gui.GetCurrentEditor());
	importmap->ShowModal();
	importmap->Destroy();
}

void FileMenuHandler::OnImportMonsterData(wxCommandEvent& WXUNUSED(event)) {
	wxFileDialog dlg(g_gui.root, "Import monster/npc file", "", "", "*.xml", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		wxArrayString paths;
		dlg.GetPaths(paths);
		for (uint32_t i = 0; i < paths.GetCount(); ++i) {
			wxString error;
			std::vector<std::string> warnings;
			bool ok = g_creatures.importXMLFromOT(FileName(paths[i]), error, warnings);
			if (ok) {
				DialogUtil::ListDialog("Monster loader errors", warnings);
			} else {
				wxMessageBox("Error OT data file \"" + paths[i] + "\".\n" + error, "Error", wxOK | wxICON_INFORMATION, g_gui.root);
			}
		}
	}
}

void FileMenuHandler::OnImportMonsterJSON(wxCommandEvent& WXUNUSED(event)) {
	wxFileDialog dlg(g_gui.root, "Import monster/npc JSON file", "", "", "*.json", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() == wxID_OK) {
		wxArrayString paths;
		dlg.GetPaths(paths);
		bool anyImported = false;
		for (uint32_t i = 0; i < paths.GetCount(); ++i) {
			wxString error;
			std::vector<std::string> warnings;
			bool ok = g_creatures.loadFromJSON(FileName(paths[i]), false, error, warnings);
			if (ok) {
				anyImported = true;
				if (!warnings.empty()) {
					DialogUtil::ListDialog("Monster JSON loader warnings", warnings);
				} else {
					wxMessageBox("Monsters imported successfully from \"" + paths[i] + "\".", "Success", wxOK | wxICON_INFORMATION, g_gui.root);
				}
			} else {
				wxMessageBox("Error loading JSON monster file \"" + paths[i] + "\".\n" + error, "Error", wxOK | wxICON_ERROR, g_gui.root);
			}
		}
		if (anyImported) {
			g_gui.RefreshView();
		}
	}
}

void FileMenuHandler::OnImportCreaturesSpawn(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	wxFileDialog dlg(g_gui.root, "Force Import Creatures Spawn", "", "", "Spawn XML (*.xml)|*.xml", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	wxArrayString paths;
	dlg.GetPaths(paths);
	if (paths.IsEmpty()) {
		return;
	}

	int total_loaded = 0;
	std::vector<std::string> failures;

	for (uint32_t i = 0; i < paths.GetCount(); ++i) {
		const wxString& path = paths[i];
		pugi::xml_document doc;
		pugi::xml_parse_result result = doc.load_file(nstr(path).c_str());
		if (!result) {
			failures.push_back(nstr(path) + ": " + result.description());
			continue;
		}

		if (!MapXMLIO::loadSpawns(editor->map, doc)) {
			failures.push_back(nstr(path) + ": file is not a valid spawn XML (missing <spawns> root)");
			continue;
		}

		++total_loaded;
	}

	if (total_loaded > 0) {
		editor->map.doChange();
		g_gui.RefreshView();
		g_gui.RefreshPalettes();
	}

	if (!failures.empty()) {
		DialogUtil::ListDialog("Force Import Creatures Spawn - Errors", failures);
	} else {
		DialogUtil::PopupDialog("Success", "Spawns imported successfully from " + i2ws(total_loaded) + " file(s).", wxOK);
	}
}

void FileMenuHandler::OnImportMinimap(wxCommandEvent& WXUNUSED(event)) {
	ASSERT(g_gui.IsEditorOpen());
}

void FileMenuHandler::OnExportMinimap(wxCommandEvent& WXUNUSED(event)) {
	if (g_gui.GetCurrentEditor()) {
		ExportMinimapWindow dlg(frame, *g_gui.GetCurrentEditor());
		dlg.ShowModal();
		dlg.Destroy();
	}
}

void FileMenuHandler::OnMergeMapsMinimap(wxCommandEvent& WXUNUSED(event)) {
	MergeMapsMinimapWindow dlg(frame);
	dlg.ShowModal();
	dlg.Destroy();
}

void FileMenuHandler::OnExportTilesets(wxCommandEvent& WXUNUSED(event)) {
	if (g_gui.GetCurrentEditor()) {
		ExportTilesetsWindow dlg(frame, *g_gui.GetCurrentEditor());
		dlg.ShowModal();
		dlg.Destroy();
	}
}

void FileMenuHandler::OnReloadDataFiles(wxCommandEvent& WXUNUSED(event)) {
	wxString error;
	std::vector<std::string> warnings;
	g_version.LoadVersion(g_version.GetCurrentVersionID(), error, warnings, true);
	DialogUtil::PopupDialog("Error", error, wxOK);
	DialogUtil::ListDialog("Warnings", warnings);
}

void FileMenuHandler::OnReloadBrushes(wxCommandEvent& WXUNUSED(event)) {
	wxString error;
	std::vector<std::string> warnings;
	if (!g_version.ReloadBrushes(error, warnings)) {
		DialogUtil::PopupDialog("Error", error, wxOK);
	}
	if (!warnings.empty()) {
		DialogUtil::ListDialog("Warnings", warnings);
	}
}

void FileMenuHandler::OnPreferences(wxCommandEvent& WXUNUSED(event)) {
	PreferencesWindow dialog(frame);
	dialog.ShowModal();
	dialog.Destroy();
}

void FileMenuHandler::OnOpenGraphicsPreferences(wxCommandEvent& WXUNUSED(event)) {
	PreferencesWindow dialog(frame, false, PreferencesWindow::PAGE_GRAPHICS);
	dialog.ShowModal();
	dialog.Destroy();
}

void FileMenuHandler::OnListExtensions(wxCommandEvent& WXUNUSED(event)) {
	ExtensionsDialog exts(frame);
	exts.ShowModal();
}

void FileMenuHandler::OnGotoWebsite(wxCommandEvent& WXUNUSED(event)) {
	::wxLaunchDefaultBrowser(__SITE_URL__, wxBROWSER_NEW_WINDOW);
}

void FileMenuHandler::OnAbout(wxCommandEvent& WXUNUSED(event)) {
	AboutWindow about(frame);
	about.ShowModal();
}
