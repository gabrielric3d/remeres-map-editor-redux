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
	wxSizer* MakeItemRow(wxWindow* parent, const wxString& title,
	                     std::initializer_list<std::pair<wxString, ItemFieldControl**>> fields,
	                     const std::function<uint16_t(const wxString&)>& getter);

	// Detail/hangable helpers
	void RefreshDetailGrid();
	void RefreshHangableGrid(const wxString& key);

	// Event handlers
	void OnSave(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);
	void OnCloseWindow(wxCloseEvent& event);
	void OnDetailGroupChanged(wxCommandEvent& event);
	void OnAddDetailItem(wxCommandEvent& event);
	void OnRemoveDetailItem(wxCommandEvent& event);
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
	ItemFieldControl* m_patchField;
	ItemFieldControl* m_fillField;
	ItemFieldControl* m_brushField;

	// Walls tab
	ItemFieldControl* m_wallN;
	ItemFieldControl* m_wallS;
	ItemFieldControl* m_wallE;
	ItemFieldControl* m_wallW;
	ItemFieldControl* m_wallNW;
	ItemFieldControl* m_wallNE;
	ItemFieldControl* m_wallSW;
	ItemFieldControl* m_wallSE;
	ItemFieldControl* m_wallPillar;

	// Borders tab
	ItemFieldControl* m_borderN;
	ItemFieldControl* m_borderS;
	ItemFieldControl* m_borderE;
	ItemFieldControl* m_borderW;
	ItemFieldControl* m_borderNW;
	ItemFieldControl* m_borderNE;
	ItemFieldControl* m_borderSW;
	ItemFieldControl* m_borderSE;
	ItemFieldControl* m_borderInnerNW;
	ItemFieldControl* m_borderInnerNE;
	ItemFieldControl* m_borderInnerSW;
	ItemFieldControl* m_borderInnerSE;

	// Brush borders
	ItemFieldControl* m_bbN;
	ItemFieldControl* m_bbS;
	ItemFieldControl* m_bbE;
	ItemFieldControl* m_bbW;
	ItemFieldControl* m_bbNW;
	ItemFieldControl* m_bbNE;
	ItemFieldControl* m_bbSW;
	ItemFieldControl* m_bbSE;
	ItemFieldControl* m_bbInnerNW;
	ItemFieldControl* m_bbInnerNE;
	ItemFieldControl* m_bbInnerSW;
	ItemFieldControl* m_bbInnerSE;

	// Details tab
	wxChoice* m_detailGroupChoice;
	wxSpinCtrlDouble* m_detailChanceSpin;
	wxChoice* m_detailPlacementChoice;
	wxListCtrl* m_detailItemsList;
	wxImageList* m_detailImageList;
	int m_currentDetailGroup;
	std::vector<DungeonGen::DetailGroup> m_editDetails;

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
