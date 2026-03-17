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

#ifndef RME_DUNGEON_PRESET_EDITOR_DIALOG_H
#define RME_DUNGEON_PRESET_EDITOR_DIALOG_H

#include <wx/wx.h>
#include <wx/spinctrl.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>
#include <wx/dnd.h>
#include <functional>

#include "editor/dungeon_generator.h"
#include "ui/dialogs/dungeon_border_grid_panel.h"

//=============================================================================
// ItemFieldDropTarget - Accepts RME_ITEM drag from palette
//=============================================================================
class ItemFieldControl;

class ItemFieldDropTarget : public wxTextDropTarget {
public:
	ItemFieldDropTarget(ItemFieldControl* field);
	bool OnDropText(wxCoord x, wxCoord y, const wxString& data) override;
private:
	ItemFieldControl* m_field;
};

//=============================================================================
// ItemFieldControl - Sprite preview + SpinCtrl + pick button + drag target
//   Layout: [32x32 sprite] [spin id] [...]
//   Supports drag & drop from palette (RME_ITEM:xxx)
//=============================================================================
class ItemFieldControl : public wxPanel {
	friend class ItemFieldDropTarget;
public:
	ItemFieldControl(wxWindow* parent, wxWindowID id, const wxString& label,
	                 uint16_t initialId = 0, int spinWidth = 75);

	uint16_t GetItemId() const;
	void SetItemId(uint16_t id);

private:
	void OnSpinChanged(wxSpinEvent& event);
	void OnPickItem(wxCommandEvent& event);
	void OnPreviewPaint(wxPaintEvent& event);
	void UpdatePreview();

	wxStaticText* m_label;
	wxSpinCtrl* m_spin;
	wxPanel* m_previewPanel;
	uint16_t m_itemId;
};

//=============================================================================
// DungeonPresetEditorDialog - Advanced editor for dungeon presets
//=============================================================================
class DungeonPresetEditorDialog : public wxDialog {
public:
	DungeonPresetEditorDialog(wxWindow* parent, const wxString& editingPreset = "",
	                          std::function<void()> onSaveCallback = nullptr);
	~DungeonPresetEditorDialog() override;

private:
	void CreateControls();
	void LoadPresetData();
	void SavePresetData();

	// Tab creation helpers
	wxPanel* CreateGeneralTab(wxNotebook* notebook);
	wxPanel* CreateWallsTab(wxNotebook* notebook);
	wxPanel* CreateBordersTab(wxNotebook* notebook);
	wxPanel* CreateDetailsTab(wxNotebook* notebook);
	wxPanel* CreateHangablesTab(wxNotebook* notebook);

	// Helpers to build rows of ItemFieldControls
	// Detail/hangable helpers
	void RefreshDetailGrid();
	void RefreshHangableGrid(const wxString& key);

	// Event handlers
	void OnSave(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);
	void OnCloseWindow(wxCloseEvent& event);
	void OnLoadGroundBrush(wxCommandEvent& event);
	void OnLoadGroundBrushGeneral(wxCommandEvent& event);
	void OnLoadWallBrush(wxCommandEvent& event);
	void OnDetailGroupChanged(wxCommandEvent& event);
	void OnAddDetailItem(wxCommandEvent& event);
	void OnRemoveDetailItem(wxCommandEvent& event);
	void OnLoadDoodadBrush(wxCommandEvent& event);
	void OnAddHangableH(wxCommandEvent& event);
	void OnRemoveHangableH(wxCommandEvent& event);
	void OnAddHangableV(wxCommandEvent& event);
	void OnRemoveHangableV(wxCommandEvent& event);

	// Preset data
	wxString m_originalName;
	bool m_isNewPreset;
	DungeonGen::DungeonPreset m_preset;
	std::function<void()> m_onSaveCallback;

	// General tab
	wxTextCtrl* m_nameInput;
	ItemFieldControl* m_groundField;
	ItemFieldControl* m_fillField;

	// General tab - ground brush loader
	wxTextCtrl* m_generalGroundSearch;
	wxListCtrl* m_generalGroundBrushList;
	wxImageList* m_generalGroundBrushImageList;

	// Walls tab - brush loader
	wxTextCtrl* m_wallBrushSearch;
	wxListCtrl* m_wallBrushList;
	wxImageList* m_wallBrushImageList;

	// Borders tab - brush loader
	wxTextCtrl* m_groundBrushSearch;
	wxListCtrl* m_groundBrushList;
	wxImageList* m_groundBrushImageList;
	enum BorderTarget { TARGET_PATCH_BORDER = 0, TARGET_BRUSH_BORDER = 1, TARGET_PATCH_TERRAIN = 2, TARGET_BRUSH_TERRAIN = 3 };
	BorderTarget m_borderTarget;
	wxStaticText* m_borderTargetLabel;
	void UpdateBorderTarget(BorderTarget target);
	void RebuildGroundBrushList();
	void RebuildWallBrushList();
	void RebuildDoodadBrushList();
	void RebuildGeneralGroundBrushList();

	// Helper: create a search text ctrl that disables hotkeys on focus
	wxTextCtrl* CreateSearchField(wxWindow* parent);

	// Walls tab - visual grid panel
	DungeonSlotGridPanel* m_wallsGrid;

	// Borders tab
	ItemFieldControl* m_patchField;
	ItemFieldControl* m_brushField;
	DungeonSlotGridPanel* m_patchBordersGrid;
	DungeonSlotGridPanel* m_brushBordersGrid;

	// Details tab
	wxChoice* m_detailGroupChoice;
	wxSpinCtrlDouble* m_detailChanceSpin;
	wxChoice* m_detailPlacementChoice;
	wxListCtrl* m_detailItemsList;
	wxImageList* m_detailImageList;
	int m_currentDetailGroup;
	std::vector<DungeonGen::DetailGroup> m_editDetails;
	wxTextCtrl* m_doodadBrushSearch;
	wxListCtrl* m_doodadBrushList;
	wxImageList* m_doodadBrushImageList;

	// Hangables tab
	wxSpinCtrlDouble* m_hangableChanceSpin;
	wxCheckBox* m_hangableVerticalCheck;
	wxListCtrl* m_hangableHList;
	wxListCtrl* m_hangableVList;
	wxImageList* m_hangableHImageList;
	wxImageList* m_hangableVImageList;
	std::vector<uint16_t> m_editHorizontalIds;
	std::vector<uint16_t> m_editVerticalIds;

	wxDECLARE_EVENT_TABLE();
};

#endif // RME_DUNGEON_PRESET_EDITOR_DIALOG_H
