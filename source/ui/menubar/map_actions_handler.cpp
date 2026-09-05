#include "ui/menubar/map_actions_handler.h"
#include "ui/gui.h"
#include "ui/dialog_util.h"
#include "ui/find_item_window.h"
#include "ui/dialogs/structure_manager_window.h"
#include "editor/editor.h"
#include "editor/operations/clean_operations.h"
#include "editor/operations/search_operations.h"
#include "map/map.h"
#include "editor/action_queue.h"
#include "editor/hotkey_utils.h"
#include "ui/main_frame.h"
#include "ui/main_menubar.h"
#include "ui/map_tab.h"
#include "rendering/ui/map_display.h"

MapActionsHandler::MapActionsHandler(MainFrame* frame) :
	frame(frame) {
}

void MapActionsHandler::OnMapRemoveItems(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	FindItemDialog dialog(frame, "Item Type to Remove");
	if (dialog.ShowModal() == wxID_OK) {
		uint16_t itemid = dialog.getResultID();

		g_gui.GetCurrentEditor()->selection.clear();
		g_gui.GetCurrentEditor()->actionQueue->clear();

		EditorOperations::RemoveItemCondition condition(itemid);
		g_gui.CreateLoadBar("Searching map for items to remove...");

		int64_t count = RemoveItemOnMap(g_gui.GetCurrentMap(), condition, false);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << count << " items deleted.";

		g_gui.SetStatusText(msg);
		g_gui.GetCurrentMap().doChange();
		g_gui.RefreshView();
	}
	dialog.Destroy();
}

void MapActionsHandler::OnMapRemoveCorpses(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ok = DialogUtil::PopupDialog("Remove Corpses", "Do you want to remove all corpses from the map?", wxYES | wxNO);

	if (ok == wxID_YES) {
		g_gui.GetCurrentEditor()->selection.clear();
		g_gui.GetCurrentEditor()->actionQueue->clear();

		EditorOperations::RemoveCorpsesCondition func;
		g_gui.CreateLoadBar("Searching map for items to remove...");

		int64_t count = RemoveItemOnMap(g_gui.GetCurrentMap(), func, false);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << count << " items deleted.";
		g_gui.SetStatusText(msg);
		g_gui.GetCurrentMap().doChange();
	}
}

void MapActionsHandler::OnMapRemoveUnreachable(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ok = DialogUtil::PopupDialog("Remove Unreachable Tiles", "Do you want to remove all unreachable items from the map?", wxYES | wxNO);

	if (ok == wxID_YES) {
		g_gui.GetCurrentEditor()->selection.clear();
		g_gui.GetCurrentEditor()->actionQueue->clear();

		EditorOperations::RemoveUnreachableCondition func;
		g_gui.CreateLoadBar("Searching map for tiles to remove...");

		long long removed = remove_if_TileOnMap(g_gui.GetCurrentMap(), func);

		g_gui.DestroyLoadBar();

		wxString msg;
		msg << removed << " tiles deleted.";

		g_gui.SetStatusText(msg);

		g_gui.GetCurrentMap().doChange();
	}
}

void MapActionsHandler::OnClearHouseTiles(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	int ret = DialogUtil::PopupDialog(
		"Clear Invalid House Tiles",
		"Are you sure you want to remove all house tiles that do not belong to a house (this action cannot be undone)?",
		wxYES | wxNO
	);

	if (ret == wxID_YES) {
		// Editor will do the work
		editor->clearInvalidHouseTiles(true);
	}

	g_gui.RefreshView();
}

void MapActionsHandler::OnClearModifiedState(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	int ret = DialogUtil::PopupDialog(
		"Clear Modified State",
		"This will have the same effect as closing the map and opening it again. Do you want to proceed?",
		wxYES | wxNO
	);

	if (ret == wxID_YES) {
		// Editor will do the work
		editor->clearModifiedTileState(true);
	}

	g_gui.RefreshView();
}

void MapActionsHandler::OnMapCleanHouseItems(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	int ret = DialogUtil::PopupDialog(
		"Clear Moveable House Items",
		"Are you sure you want to remove all items inside houses that can be moved (this action cannot be undone)?",
		wxYES | wxNO
	);

	if (ret == wxID_YES) {
		// Editor will do the work
		// editor->removeHouseItems(true);
	}

	g_gui.RefreshView();
}

void MapActionsHandler::OnBorderizeSelection(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	g_gui.GetCurrentEditor()->borderizeSelection();
	g_gui.RefreshView();
}

void MapActionsHandler::OnBorderizeMap(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ret = DialogUtil::PopupDialog("Borderize Map", "Are you sure you want to borderize the entire map (this action cannot be undone)?", wxYES | wxNO);
	if (ret == wxID_YES) {
		g_gui.GetCurrentEditor()->borderizeMap(true);
	}

	g_gui.RefreshView();
}

void MapActionsHandler::OnRandomizeSelection(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	g_gui.GetCurrentEditor()->randomizeSelection();
	g_gui.RefreshView();
}

void MapActionsHandler::OnFillSelection(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (editor->hasSelection()) {
		if (editor->fillSelection()) {
			g_gui.RefreshView();
		}
		return;
	}

	// No selection. The menu accelerator (default Ctrl+D) consumed the key press
	// before the map canvas could see it, which is what used to arm the classic
	// "hold D + Ctrl+click on a ground brush" flood fill. Re-arm it here so both
	// behaviours share the same key, but only while the shortcut key is physically
	// held: a plain click on the menu item just explains what is missing.
	MainMenuBar* menubar = frame ? frame->GetMainMenuBar() : nullptr;
	MapTab* tab = g_gui.GetCurrentMapTab();
	MapCanvas* canvas = tab ? tab->GetCanvas() : nullptr;
	HotkeyData hotkey;
	if (menubar && canvas
		&& menubar->GetActionHotkey(MenuBar::FILL_SELECTION, hotkey)
		&& hotkey.mouseButton == HotkeyMouseButton::None && hotkey.keycode != 0
		&& wxGetKeyState(static_cast<wxKeyCode>(hotkey.keycode))) {
		canvas->keyCode = WXK_CONTROL_D;
		g_gui.SetStatusText("Nothing selected: keep the key held and Ctrl+click with a ground brush to flood fill.");
		return;
	}

	g_gui.SetStatusText("Nothing selected. Select an area first, then fill it with the current brush.");
}

void MapActionsHandler::OnRandomizeMap(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ret = DialogUtil::PopupDialog("Randomize Map", "Are you sure you want to randomize the entire map (this action cannot be undone)?", wxYES | wxNO);
	if (ret == wxID_YES) {
		g_gui.GetCurrentEditor()->randomizeMap(true);
	}

	g_gui.RefreshView();
}

void MapActionsHandler::OnRotateSelectionCW(wxCommandEvent& WXUNUSED(event)) {
	// If the Structure Manager paste preview is active, rotate the paste instead
	if (StructureManagerDialog::RotatePaste()) {
		return;
	}

	if (!g_gui.IsEditorOpen()) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || editor->selection.size() < 2) {
		return;
	}

	editor->rotateSelection(1);
	g_gui.RefreshView();
}

void MapActionsHandler::OnRotateSelectionCCW(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || editor->selection.size() < 2) {
		return;
	}

	editor->rotateSelection(3);
	g_gui.RefreshView();
}

void MapActionsHandler::OnRotateSelection180(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || editor->selection.size() < 2) {
		return;
	}

	editor->rotateSelection(2);
	g_gui.RefreshView();
}

void MapActionsHandler::OnMapCleanup(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ok = DialogUtil::PopupDialog("Cleanup invalid tiles", "Do you want to remove all invalid or unresolved items from the map?", wxYES | wxNO);

	if (ok == wxID_YES) {
		g_gui.GetCurrentMap().cleanInvalidTiles(true);
		g_gui.RefreshView();
	}
}

void MapActionsHandler::OnMapCleanInvalidZones(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	int ok = DialogUtil::PopupDialog("Cleanup invalid zones", "Do you want to remove all invalid tile flags and opaque OTBM tile fragments from the map?", wxYES | wxNO);

	if (ok == wxID_YES) {
		g_gui.GetCurrentMap().cleanInvalidZones(true);
		g_gui.RefreshView();
	}
}
