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

#ifndef RME_AREA_DECORATION_DIALOG_H
#define RME_AREA_DECORATION_DIALOG_H

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/notebook.h>
#include <wx/slider.h>
#include <wx/dnd.h>
#include <functional>
#include "editor/area_decoration.h"

class Editor;
class Brush;
class DoodadBrush;

//=============================================================================
// ItemListDropTarget - Drop target for items list
//=============================================================================
class FloorRuleEditDialog;

class ItemListDropTarget : public wxTextDropTarget {
public:
	ItemListDropTarget(FloorRuleEditDialog* dialog);
	bool OnDropText(wxCoord x, wxCoord y, const wxString& data) override;
private:
	FloorRuleEditDialog* m_dialog;
};

//=============================================================================
// FloorRuleEditDialog - Edit a single floor rule (non-modal)
//=============================================================================
class FloorRuleEditDialog : public wxDialog {
	friend class ItemListDropTarget;
public:
	FloorRuleEditDialog(wxWindow* parent, AreaDecoration::FloorRule& rule,
	                    std::function<void(bool)> onCloseCallback = nullptr);
	virtual ~FloorRuleEditDialog();

	bool TransferDataFromWindow() override;

	// Add item by ID (used by drop target and other sources)
	void AddItemById(uint16_t itemId, int weight = 100);
	// Add items from doodad brush
	void AddItemsFromDoodad(DoodadBrush* doodad);

private:
	AreaDecoration::FloorRule& m_rule;
	std::function<void(bool)> m_onCloseCallback;

	wxTextCtrl* m_nameInput;
	wxRadioButton* m_singleFloorRadio;
	wxRadioButton* m_floorRangeRadio;
	wxRadioButton* m_clusterRadio;
	wxSpinCtrl* m_singleFloorSpin;
	wxSpinCtrl* m_fromFloorSpin;
	wxSpinCtrl* m_toFloorSpin;

	// Floor preview panel
	wxPanel* m_floorPreviewPanelFrom;
	wxPanel* m_floorPreviewPanelTo;

	// Cluster mode controls panel (shown only in cluster mode)
	wxPanel* m_clusterControlsPanel;
	wxListCtrl* m_clusterTilesListCtrl;
	wxImageList* m_clusterTilesImageList;
	wxSpinCtrl* m_clusterTileOffsetXSpin;
	wxSpinCtrl* m_clusterTileOffsetYSpin;
	wxSpinCtrl* m_clusterNewItemIdSpin;
	wxButton* m_clusterBrowseItemBtn;
	wxButton* m_clusterPreviewBtn;
	wxButton* m_clusterFromSelectionBtn;
	wxSpinCtrl* m_instanceCountSpin;
	wxSpinCtrl* m_instanceMinDistSpin;
	wxCheckBox* m_requireGroundCheck;
	wxStaticText* m_clusterCenterLabel;

	wxListCtrl* m_itemsListCtrl;
	wxImageList* m_itemsImageList;
	wxButton* m_editItemBtn;
	wxButton* m_previewClusterItemBtn;
	wxButton* m_replaceClusterBtn;
	wxSpinCtrl* m_newItemIdSpin;
	wxSpinCtrl* m_newItemWeightSpin;
	wxSpinCtrl* m_clusterCountSpin;
	wxSpinCtrl* m_clusterRadiusSpin;
	wxSpinCtrl* m_clusterMinDistanceSpin;

	wxSpinCtrl* m_densitySpin;
	wxSpinCtrl* m_maxPlacementsSpin;
	wxSpinCtrl* m_prioritySpin;

	// Border item controls
	wxSpinCtrl* m_borderItemSpin;
	wxPanel* m_borderPreviewPanel;

	// Friend floor controls
	wxRadioButton* m_friendSingleFloorRadio;
	wxRadioButton* m_friendRangeRadio;
	wxSpinCtrl* m_friendSingleFloorSpin;
	wxSpinCtrl* m_friendFromFloorSpin;
	wxSpinCtrl* m_friendToFloorSpin;
	wxPanel* m_friendPreviewPanelFrom;
	wxPanel* m_friendPreviewPanelTo;
	wxSpinCtrl* m_friendChanceSpin;
	wxSpinCtrl* m_friendStrengthSpin;

	// Doodad selection with pagination and search
	wxTextCtrl* m_doodadSearchCtrl;
	wxListCtrl* m_doodadListCtrl;
	wxImageList* m_doodadImageList;
	wxButton* m_prevPageBtn;
	wxButton* m_nextPageBtn;
	wxStaticText* m_pageInfoText;

	std::vector<DoodadBrush*> m_allDoodads;        // All available doodads
	std::vector<DoodadBrush*> m_filteredDoodads;   // Filtered by search
	int m_currentPage;
	static const int DOODADS_PER_PAGE = 50;

	void CreateControls();
	void LoadRuleData();
	void UpdateItemsList();
	void UpdateFloorPreview();
	void UpdateFriendPreview();
	void UpdateBorderPreview();
	void LoadDoodadList();
	void UpdateDoodadListDisplay();
	void FilterDoodads(const wxString& filter);
	bool EditItemDialog(size_t index);
	bool BuildClusterTilesFromSelection(std::vector<AreaDecoration::CompositeTile>& outTiles);
	void PrepareClusterPaste(const AreaDecoration::ItemEntry& entry);

	// Get sprite bitmap for item
	wxBitmap GetItemBitmap(uint16_t itemId, int size = 32);

	void OnFloorTypeChanged(wxCommandEvent& event);
	void OnFloorIdChanged(wxSpinEvent& event);
	void OnFriendFloorTypeChanged(wxCommandEvent& event);
	void OnFriendFloorIdChanged(wxSpinEvent& event);

	// Cluster mode event handlers
	void OnClusterAddTile(wxCommandEvent& event);
	void OnClusterRemoveTile(wxCommandEvent& event);
	void OnClusterAddItem(wxCommandEvent& event);
	void OnClusterRemoveItem(wxCommandEvent& event);
	void OnClusterFromSelection(wxCommandEvent& event);
	void OnClusterPreview(wxCommandEvent& event);
	void OnClusterBrowseItem(wxCommandEvent& event);
	void UpdateClusterTilesList();
	void UpdateClusterControls();
	void OnAddItem(wxCommandEvent& event);
	void OnEditItem(wxCommandEvent& event);
	void OnPreviewClusterItem(wxCommandEvent& event);
	void OnReplaceClusterFromSelection(wxCommandEvent& event);
	void OnRemoveItem(wxCommandEvent& event);
	void OnClearItems(wxCommandEvent& event);
	void OnBrowseItem(wxCommandEvent& event);
	void OnAddDoodad(wxCommandEvent& event);
	void OnDoodadDoubleClick(wxListEvent& event);
	void OnAddClusterFromSelection(wxCommandEvent& event);
	void OnItemsListSelected(wxListEvent& event);
	void OnItemsListActivated(wxListEvent& event);
	void OnDoodadSearch(wxCommandEvent& event);
	void OnPrevPage(wxCommandEvent& event);
	void OnNextPage(wxCommandEvent& event);
	void OnPaintFloorPreview(wxPaintEvent& event);
	void OnPaintFriendPreview(wxPaintEvent& event);
	void OnPaintBorderPreview(wxPaintEvent& event);
	void OnBorderItemChanged(wxSpinEvent& event);
	void OnBrowseBorderItem(wxCommandEvent& event);
	void OnOK(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);

	wxDECLARE_EVENT_TABLE();
};

//=============================================================================
// AreaDecorationDialog - Main dialog
//=============================================================================
class AreaDecorationDialog : public wxDialog {
public:
	AreaDecorationDialog(wxWindow* parent);
	virtual ~AreaDecorationDialog();
	void SetSeedInputValue(uint64_t seed);

	// Update the engine when editor changes (called when dialog is shown again)
	void UpdateEngine();

	// Add a rule generated from an external source (e.g., context menu workflow)
	void AddRuleFromExternal(const AreaDecoration::FloorRule& rule);

private:
	// UI Controls - Preset Management
	wxChoice* m_presetChoice;
	wxTextCtrl* m_presetNameInput;

	// UI Controls - Area Tab
	wxChoice* m_areaTypeChoice;
	wxStaticText* m_areaInfoText;
	wxButton* m_selectAreaButton;
	wxSpinCtrl* m_rectX1Spin;
	wxSpinCtrl* m_rectY1Spin;
	wxSpinCtrl* m_rectX2Spin;
	wxSpinCtrl* m_rectY2Spin;
	wxSpinCtrl* m_rectZ1Spin;
	wxSpinCtrl* m_rectZ2Spin;
	wxStaticText* m_zCountText;
	wxStaticText* m_pickStatusText;

	// UI Controls - Floor Rules Tab
	wxListCtrl* m_rulesListCtrl;
	wxImageList* m_rulesImageList;

	// UI Controls - Settings Tab
	wxSpinCtrl* m_minDistanceSpin;
	wxSpinCtrl* m_sameItemDistanceSpin;
	wxCheckBox* m_checkDiagonalsCheck;
	wxChoice* m_distributionChoice;
	wxSlider* m_clusterStrengthSlider;
	wxSpinCtrl* m_clusterCountSpin;
	wxSpinCtrl* m_gridSpacingXSpin;
	wxSpinCtrl* m_gridSpacingYSpin;
	wxSpinCtrl* m_gridJitterSpin;
	wxSpinCtrl* m_maxItemsSpin;
	wxCheckBox* m_skipBlockedCheck;

	// UI Controls - Seed Tab
	wxTextCtrl* m_seedInput;
	wxCheckBox* m_useSeedCheck;

	// UI Controls - Preview
	wxStaticText* m_statsText;
	wxButton* m_removeLastApplyBtn = nullptr;
	wxButton* m_applyBtn = nullptr;

	// Engine
	std::unique_ptr<AreaDecoration::DecorationEngine> m_engine;
	AreaDecoration::AreaDefinition m_area;
	AreaDecoration::DecorationPreset m_preset;

	// Track whether a FloorRuleEditDialog is currently open to prevent
	// add/remove operations that would invalidate the edit dialog's rule reference
	bool m_editDialogOpen = false;
	void UpdateRuleButtons();

	void CreateControls();
	void CreatePresetControls(wxBoxSizer* mainSizer);
	void CreateAreaTab(wxNotebook* notebook);
	void CreateRulesTab(wxNotebook* notebook);
	void CreateSettingsTab(wxNotebook* notebook);
	void CreateSeedTab(wxNotebook* notebook);
	void CreatePreviewControls(wxBoxSizer* mainSizer);

	void UpdateUI();
	void UpdateStats();
	void UpdateRulesList();
	void UpdatePresetList();
	void UpdateZCountText(int count, int minZ = -1, int maxZ = -1);
	void BuildPresetFromUI();
	void BuildAreaFromUI();
	void LoadPresetToUI(const AreaDecoration::DecorationPreset& preset);

	// Event handlers
	void OnAreaTypeChanged(wxCommandEvent& event);
	void OnSelectFromMap(wxCommandEvent& event);
	void OnUseSelection(wxCommandEvent& event);
	void OnRectangleCoordsChanged(wxCommandEvent& event);

	void OnAddRule(wxCommandEvent& event);
	void OnEditRule(wxCommandEvent& event);
	void OnRemoveRule(wxCommandEvent& event);
	void OnRuleDoubleClick(wxListEvent& event);
	void OnRuleCheckChanged(wxListEvent& event);

	void OnDistributionChanged(wxCommandEvent& event);

	void OnPreview(wxCommandEvent& event);
	void OnReroll(wxCommandEvent& event);
	void OnRerollApply(wxCommandEvent& event);
	void OnApply(wxCommandEvent& event);
	void OnRevert(wxCommandEvent& event);
	void OnRemoveLastApply(wxCommandEvent& event);

	// Preset event handlers
	void OnPresetSelected(wxCommandEvent& event);
	void OnSavePreset(wxCommandEvent& event);
	void OnRefreshPresets(wxCommandEvent& event);
	void OnDeletePreset(wxCommandEvent& event);
	void OnExportPreset(wxCommandEvent& event);
	void OnImportPreset(wxCommandEvent& event);

	void OnClose(wxCloseEvent& event);

	wxDECLARE_EVENT_TABLE();
};

#endif // RME_AREA_DECORATION_DIALOG_H
