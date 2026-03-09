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

#include "app/main.h"
#include "ui/dialogs/area_decoration_dialog.h"
#include "editor/editor.h"
#include "ui/gui.h"
#include "map/map.h"
#include "map/tile.h"
#include "editor/selection.h"
#include "ui/find_item_window.h"
#include "rendering/core/graphics.h"
#include "game/items.h"
#include "brushes/brush.h"
#include "ui/theme.h"
#include "brushes/doodad/doodad_brush.h"
#include "brushes/raw/raw_brush.h"
#include "game/materials.h"
#include "map/tileset.h"
#include <wx/dcbuffer.h>
#include <algorithm>
#include <climits>
#include <memory>
#include <unordered_set>
#include "ui/dialogs/cluster_preview_window.h"

namespace {
	enum {
		ID_AREA_TYPE_CHOICE = wxID_HIGHEST + 5000,
		ID_SELECT_FROM_MAP,
		ID_USE_SELECTION,

		ID_ADD_RULE,
		ID_EDIT_RULE,
		ID_REMOVE_RULE,
		ID_RULES_LIST,

		ID_DISTRIBUTION_CHOICE,

		ID_PREVIEW,
		ID_REROLL,
		ID_REROLL_APPLY,
		ID_APPLY,
		ID_REVERT,
		ID_REMOVE_LAST_APPLY,

		ID_FLOOR_TYPE_SINGLE,
		ID_FLOOR_TYPE_RANGE,
		ID_ADD_ITEM,
		ID_EDIT_ITEM,
		ID_REMOVE_ITEM,
		ID_CLEAR_ITEMS,
		ID_BROWSE_ITEM,
		ID_ADD_CLUSTER,
		ID_REPLACE_CLUSTER,
		ID_PREVIEW_CLUSTER_ITEM,
		ID_ITEMS_LIST,
		ID_ADD_DOODAD,
		ID_DOODAD_LIST,
		ID_DOODAD_SEARCH,
		ID_DOODAD_PREV_PAGE,
		ID_DOODAD_NEXT_PAGE,
		ID_FLOOR_PREVIEW,
		ID_BORDER_PREVIEW,
		ID_BORDER_ITEM_SPIN,
		ID_BROWSE_BORDER_ITEM,
		ID_SINGLE_FLOOR_SPIN,
		ID_FROM_FLOOR_SPIN,
		ID_TO_FLOOR_SPIN,
		ID_FRIEND_FLOOR_TYPE_SINGLE,
		ID_FRIEND_FLOOR_TYPE_RANGE,
		ID_FRIEND_SINGLE_FLOOR_SPIN,
		ID_FRIEND_FROM_FLOOR_SPIN,
		ID_FRIEND_TO_FLOOR_SPIN,
		ID_FRIEND_PREVIEW_FROM,
		ID_FRIEND_PREVIEW_TO,
		ID_RULE_OK,
		ID_RULE_CANCEL,

		// Cluster mode IDs (FloorRuleEditDialog)
		ID_FLOOR_TYPE_CLUSTER,
		ID_CLUSTER_ADD_TILE,
		ID_CLUSTER_REMOVE_TILE,
		ID_CLUSTER_ADD_ITEM,
		ID_CLUSTER_REMOVE_ITEM,
		ID_CLUSTER_FROM_SELECTION,
		ID_CLUSTER_PREVIEW,
		ID_CLUSTER_BROWSE_ITEM,
		ID_CLUSTER_TILES_LIST,
		ID_CLUSTER_INSTANCE_COUNT,
		ID_CLUSTER_INSTANCE_DIST,
		ID_CLUSTER_NEW_ITEM_ID,
		ID_CLUSTER_OFFSET_X,
		ID_CLUSTER_OFFSET_Y,

		// Preset management IDs
		ID_PRESET_CHOICE,
		ID_REFRESH_PRESET,
		ID_SAVE_PRESET,
		ID_DELETE_PRESET,
		ID_EXPORT_PRESET,
		ID_IMPORT_PRESET
	};

// Constants for icon sizes
const int ITEM_ICON_SIZE = 32;
const int RULE_ICON_SIZE = 24;

wxBitmap CreatePreviewBitmap(uint16_t itemId, int size) {
	wxBitmap bmp(size, size, 32);
	wxMemoryDC dc(bmp);

	dc.SetBackground(wxBrush(wxColour(0x0C, 0x14, 0x2A)));
	dc.Clear();

	const ItemType& itemType = g_items.getItemType(itemId);
	Sprite* spr = nullptr;
	if (itemType.id != 0) {
		spr = g_gui.gfx.getSprite(itemType.clientID);
	}

	if (spr) {
		spr->DrawTo(&dc, SPRITE_SIZE_32x32, 0, 0, size, size);
	} else {
		dc.SetBrush(wxBrush(wxColour(100, 100, 100)));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(2, 2, size - 4, size - 4);
		dc.SetTextForeground(*wxWHITE);
		dc.DrawText("?", size / 2 - 4, size / 2 - 8);
	}

	dc.SelectObject(wxNullBitmap);
	return bmp;
}
}

//=============================================================================
// ItemListDropTarget Implementation
//=============================================================================

ItemListDropTarget::ItemListDropTarget(FloorRuleEditDialog* dialog)
	: m_dialog(dialog)
{
}

bool ItemListDropTarget::OnDropText(wxCoord x, wxCoord y, const wxString& data) {
	if (!m_dialog) return false;

	// Handle ITEM_ID:xxx format (from palette drag)
	if (data.StartsWith("ITEM_ID:")) {
		wxString idStr = data.Mid(8);
		long itemId = 0;
		if (idStr.ToLong(&itemId) && itemId > 0 && itemId <= 0xFFFF) {
			m_dialog->AddItemById(static_cast<uint16_t>(itemId));
			return true;
		}
	}

	// Handle BRUSH:xxx format (brush name)
	if (data.StartsWith("BRUSH:")) {
		wxString brushName = data.Mid(6);
		Brush* brush = g_brushes.getBrush(brushName.ToStdString());
		if (brush) {
			if (brush->is<DoodadBrush>()) {
				m_dialog->AddItemsFromDoodad(brush->as<DoodadBrush>());
				return true;
			} else if (brush->is<RAWBrush>()) {
				RAWBrush* raw = brush->as<RAWBrush>();
				if (raw) {
					m_dialog->AddItemById(raw->getItemID());
					return true;
				}
			}
		}
	}

	// Try to parse as plain number
	long itemId = 0;
	if (data.ToLong(&itemId) && itemId > 0 && itemId <= 0xFFFF) {
		m_dialog->AddItemById(static_cast<uint16_t>(itemId));
		return true;
	}

	return false;
}

//=============================================================================
// FloorRuleEditDialog
//=============================================================================

wxBEGIN_EVENT_TABLE(FloorRuleEditDialog, wxDialog)
	EVT_RADIOBUTTON(ID_FLOOR_TYPE_SINGLE, FloorRuleEditDialog::OnFloorTypeChanged)
	EVT_RADIOBUTTON(ID_FLOOR_TYPE_RANGE, FloorRuleEditDialog::OnFloorTypeChanged)
	EVT_RADIOBUTTON(ID_FLOOR_TYPE_CLUSTER, FloorRuleEditDialog::OnFloorTypeChanged)
	EVT_BUTTON(ID_CLUSTER_ADD_TILE, FloorRuleEditDialog::OnClusterAddTile)
	EVT_BUTTON(ID_CLUSTER_REMOVE_TILE, FloorRuleEditDialog::OnClusterRemoveTile)
	EVT_BUTTON(ID_CLUSTER_ADD_ITEM, FloorRuleEditDialog::OnClusterAddItem)
	EVT_BUTTON(ID_CLUSTER_REMOVE_ITEM, FloorRuleEditDialog::OnClusterRemoveItem)
	EVT_BUTTON(ID_CLUSTER_FROM_SELECTION, FloorRuleEditDialog::OnClusterFromSelection)
	EVT_BUTTON(ID_CLUSTER_PREVIEW, FloorRuleEditDialog::OnClusterPreview)
	EVT_BUTTON(ID_CLUSTER_BROWSE_ITEM, FloorRuleEditDialog::OnClusterBrowseItem)
	EVT_BUTTON(ID_ADD_ITEM, FloorRuleEditDialog::OnAddItem)
	EVT_BUTTON(ID_EDIT_ITEM, FloorRuleEditDialog::OnEditItem)
	EVT_BUTTON(ID_PREVIEW_CLUSTER_ITEM, FloorRuleEditDialog::OnPreviewClusterItem)
	EVT_BUTTON(ID_REPLACE_CLUSTER, FloorRuleEditDialog::OnReplaceClusterFromSelection)
	EVT_BUTTON(ID_REMOVE_ITEM, FloorRuleEditDialog::OnRemoveItem)
	EVT_BUTTON(ID_CLEAR_ITEMS, FloorRuleEditDialog::OnClearItems)
	EVT_BUTTON(ID_BROWSE_ITEM, FloorRuleEditDialog::OnBrowseItem)
	EVT_BUTTON(ID_ADD_DOODAD, FloorRuleEditDialog::OnAddDoodad)
	EVT_BUTTON(ID_ADD_CLUSTER, FloorRuleEditDialog::OnAddClusterFromSelection)
	EVT_LIST_ITEM_SELECTED(ID_ITEMS_LIST, FloorRuleEditDialog::OnItemsListSelected)
	EVT_LIST_ITEM_ACTIVATED(ID_ITEMS_LIST, FloorRuleEditDialog::OnItemsListActivated)
	EVT_LIST_ITEM_ACTIVATED(ID_DOODAD_LIST, FloorRuleEditDialog::OnDoodadDoubleClick)
	EVT_TEXT(ID_DOODAD_SEARCH, FloorRuleEditDialog::OnDoodadSearch)
	EVT_BUTTON(ID_DOODAD_PREV_PAGE, FloorRuleEditDialog::OnPrevPage)
	EVT_BUTTON(ID_DOODAD_NEXT_PAGE, FloorRuleEditDialog::OnNextPage)
	EVT_SPINCTRL(ID_SINGLE_FLOOR_SPIN, FloorRuleEditDialog::OnFloorIdChanged)
	EVT_SPINCTRL(ID_FROM_FLOOR_SPIN, FloorRuleEditDialog::OnFloorIdChanged)
	EVT_SPINCTRL(ID_TO_FLOOR_SPIN, FloorRuleEditDialog::OnFloorIdChanged)
	EVT_RADIOBUTTON(ID_FRIEND_FLOOR_TYPE_SINGLE, FloorRuleEditDialog::OnFriendFloorTypeChanged)
	EVT_RADIOBUTTON(ID_FRIEND_FLOOR_TYPE_RANGE, FloorRuleEditDialog::OnFriendFloorTypeChanged)
	EVT_SPINCTRL(ID_FRIEND_SINGLE_FLOOR_SPIN, FloorRuleEditDialog::OnFriendFloorIdChanged)
	EVT_SPINCTRL(ID_FRIEND_FROM_FLOOR_SPIN, FloorRuleEditDialog::OnFriendFloorIdChanged)
	EVT_SPINCTRL(ID_FRIEND_TO_FLOOR_SPIN, FloorRuleEditDialog::OnFriendFloorIdChanged)
	EVT_SPINCTRL(ID_BORDER_ITEM_SPIN, FloorRuleEditDialog::OnBorderItemChanged)
	EVT_BUTTON(ID_BROWSE_BORDER_ITEM, FloorRuleEditDialog::OnBrowseBorderItem)
	EVT_BUTTON(ID_RULE_OK, FloorRuleEditDialog::OnOK)
	EVT_BUTTON(ID_RULE_CANCEL, FloorRuleEditDialog::OnCancel)
	EVT_CLOSE(FloorRuleEditDialog::OnClose)
wxEND_EVENT_TABLE()

FloorRuleEditDialog::FloorRuleEditDialog(wxWindow* parent, AreaDecoration::FloorRule& rule,
                                         std::function<void(bool)> onCloseCallback)
	: wxDialog(parent, wxID_ANY, "Edit Floor Rule", wxDefaultPosition, wxSize(1200, 800),
	           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)  // Non-modal friendly style
	, m_rule(rule)
	, m_onCloseCallback(onCloseCallback)
	, m_clusterRadio(nullptr)
	, m_floorPreviewPanelFrom(nullptr)
	, m_floorPreviewPanelTo(nullptr)
	, m_clusterControlsPanel(nullptr)
	, m_clusterTilesListCtrl(nullptr)
	, m_clusterTilesImageList(nullptr)
	, m_clusterTileOffsetXSpin(nullptr)
	, m_clusterTileOffsetYSpin(nullptr)
	, m_clusterNewItemIdSpin(nullptr)
	, m_clusterBrowseItemBtn(nullptr)
	, m_clusterPreviewBtn(nullptr)
	, m_clusterFromSelectionBtn(nullptr)
	, m_instanceCountSpin(nullptr)
	, m_instanceMinDistSpin(nullptr)
	, m_clusterCenterLabel(nullptr)
	, m_itemsImageList(nullptr)
	, m_borderItemSpin(nullptr)
	, m_borderPreviewPanel(nullptr)
	, m_friendSingleFloorRadio(nullptr)
	, m_friendRangeRadio(nullptr)
	, m_friendSingleFloorSpin(nullptr)
	, m_friendFromFloorSpin(nullptr)
	, m_friendToFloorSpin(nullptr)
	, m_friendPreviewPanelFrom(nullptr)
	, m_friendPreviewPanelTo(nullptr)
	, m_friendChanceSpin(nullptr)
	, m_friendStrengthSpin(nullptr)
	, m_doodadSearchCtrl(nullptr)
	, m_doodadListCtrl(nullptr)
	, m_doodadImageList(nullptr)
	, m_prevPageBtn(nullptr)
	, m_nextPageBtn(nullptr)
	, m_pageInfoText(nullptr)
	, m_currentPage(0)
{
	CreateControls();
	LoadDoodadList();
	LoadRuleData();
	Centre();

	// Set drop target for items list
	m_itemsListCtrl->SetDropTarget(new ItemListDropTarget(this));
}

FloorRuleEditDialog::~FloorRuleEditDialog() {
	if (m_doodadImageList) {
		delete m_doodadImageList;
		m_doodadImageList = nullptr;
	}
	if (m_clusterTilesImageList) {
		delete m_clusterTilesImageList;
		m_clusterTilesImageList = nullptr;
	}
	if (m_itemsImageList) {
		delete m_itemsImageList;
		m_itemsImageList = nullptr;
	}
}

void FloorRuleEditDialog::CreateControls() {
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// Two-column layout
	wxBoxSizer* columnsSizer = new wxBoxSizer(wxHORIZONTAL);

	// ==================== LEFT COLUMN ====================
	wxBoxSizer* leftColumn = new wxBoxSizer(wxVERTICAL);

	// Name
	wxStaticBoxSizer* nameBox = new wxStaticBoxSizer(wxVERTICAL, this, "Rule Name");
	m_nameInput = new wxTextCtrl(this, wxID_ANY, "");
	nameBox->Add(m_nameInput, 0, wxALL | wxEXPAND, 5);
	leftColumn->Add(nameBox, 0, wxALL | wxEXPAND, 5);

	// Floor Selection with Preview
	wxStaticBoxSizer* floorBox = new wxStaticBoxSizer(wxVERTICAL, this, "Floor Selection");

	// Floor preview at top
	wxBoxSizer* previewSizer = new wxBoxSizer(wxHORIZONTAL);
	previewSizer->Add(new wxStaticText(this, wxID_ANY, "Preview:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
	wxBoxSizer* floorPreviewSizer = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* floorFromSizer = new wxBoxSizer(wxVERTICAL);
	floorFromSizer->Add(new wxStaticText(this, wxID_ANY, "From"), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 2);
	m_floorPreviewPanelFrom = new wxPanel(this, ID_FLOOR_PREVIEW, wxDefaultPosition, wxSize(48, 48));
	m_floorPreviewPanelFrom->SetBackgroundStyle(wxBG_STYLE_PAINT);
	m_floorPreviewPanelFrom->Bind(wxEVT_PAINT, &FloorRuleEditDialog::OnPaintFloorPreview, this);
	floorFromSizer->Add(m_floorPreviewPanelFrom, 0);
	floorPreviewSizer->Add(floorFromSizer, 0, wxRIGHT, 8);

	wxBoxSizer* floorToSizer = new wxBoxSizer(wxVERTICAL);
	floorToSizer->Add(new wxStaticText(this, wxID_ANY, "To"), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 2);
	m_floorPreviewPanelTo = new wxPanel(this, ID_FLOOR_PREVIEW, wxDefaultPosition, wxSize(48, 48));
	m_floorPreviewPanelTo->SetBackgroundStyle(wxBG_STYLE_PAINT);
	m_floorPreviewPanelTo->Bind(wxEVT_PAINT, &FloorRuleEditDialog::OnPaintFloorPreview, this);
	floorToSizer->Add(m_floorPreviewPanelTo, 0);
	floorPreviewSizer->Add(floorToSizer, 0);

	previewSizer->Add(floorPreviewSizer, 0);
	floorBox->Add(previewSizer, 0, wxALL, 5);

	// Single floor option
	m_singleFloorRadio = new wxRadioButton(this, ID_FLOOR_TYPE_SINGLE, "Single Floor ID", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	wxBoxSizer* singleSizer = new wxBoxSizer(wxHORIZONTAL);
	singleSizer->Add(m_singleFloorRadio, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
	m_singleFloorSpin = new wxSpinCtrl(this, ID_SINGLE_FLOOR_SPIN, "0", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	singleSizer->Add(m_singleFloorSpin, 0, wxALIGN_CENTER_VERTICAL);
	floorBox->Add(singleSizer, 0, wxALL, 5);

	// Floor range option
	m_floorRangeRadio = new wxRadioButton(this, ID_FLOOR_TYPE_RANGE, "Floor Range");
	wxBoxSizer* rangeSizer = new wxBoxSizer(wxHORIZONTAL);
	rangeSizer->Add(m_floorRangeRadio, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
	rangeSizer->Add(new wxStaticText(this, wxID_ANY, "From:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	m_fromFloorSpin = new wxSpinCtrl(this, ID_FROM_FLOOR_SPIN, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	rangeSizer->Add(m_fromFloorSpin, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	rangeSizer->Add(new wxStaticText(this, wxID_ANY, "To:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	m_toFloorSpin = new wxSpinCtrl(this, ID_TO_FLOOR_SPIN, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	rangeSizer->Add(m_toFloorSpin, 0, wxALIGN_CENTER_VERTICAL);
	floorBox->Add(rangeSizer, 0, wxALL, 5);

	// Cluster option (3rd radio - no wxRB_GROUP since it continues the group from singleFloorRadio)
	m_clusterRadio = new wxRadioButton(this, ID_FLOOR_TYPE_CLUSTER, "Cluster");
	floorBox->Add(m_clusterRadio, 0, wxALL, 5);

	leftColumn->Add(floorBox, 0, wxALL | wxEXPAND, 5);

	// ---- Cluster Controls Panel (hidden by default) ----
	m_clusterControlsPanel = new wxPanel(this, wxID_ANY);
	wxBoxSizer* clusterPanelSizer = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* clusterTilesBox = new wxStaticBoxSizer(wxVERTICAL, m_clusterControlsPanel, "Cluster Tiles");

	// Cluster tiles list
	m_clusterTilesImageList = new wxImageList(ITEM_ICON_SIZE, ITEM_ICON_SIZE, true);
	m_clusterTilesListCtrl = new wxListCtrl(m_clusterControlsPanel, ID_CLUSTER_TILES_LIST,
		wxDefaultPosition, wxSize(-1, 120), wxLC_REPORT | wxLC_SINGLE_SEL);
	m_clusterTilesListCtrl->SetImageList(m_clusterTilesImageList, wxIMAGE_LIST_SMALL);
	m_clusterTilesListCtrl->InsertColumn(0, "", wxLIST_FORMAT_LEFT, 40);
	m_clusterTilesListCtrl->InsertColumn(1, "Offset", wxLIST_FORMAT_LEFT, 80);
	m_clusterTilesListCtrl->InsertColumn(2, "Items", wxLIST_FORMAT_LEFT, 160);
	clusterTilesBox->Add(m_clusterTilesListCtrl, 1, wxALL | wxEXPAND, 5);

	// Tile offset + item controls row
	wxBoxSizer* clusterTileCtrlSizer = new wxBoxSizer(wxHORIZONTAL);
	clusterTileCtrlSizer->Add(new wxStaticText(m_clusterControlsPanel, wxID_ANY, "Offset X:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	m_clusterTileOffsetXSpin = new wxSpinCtrl(m_clusterControlsPanel, ID_CLUSTER_OFFSET_X, "0",
		wxDefaultPosition, wxSize(55, -1), wxSP_ARROW_KEYS, -50, 50, 0);
	clusterTileCtrlSizer->Add(m_clusterTileOffsetXSpin, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	clusterTileCtrlSizer->Add(new wxStaticText(m_clusterControlsPanel, wxID_ANY, "Y:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	m_clusterTileOffsetYSpin = new wxSpinCtrl(m_clusterControlsPanel, ID_CLUSTER_OFFSET_Y, "0",
		wxDefaultPosition, wxSize(55, -1), wxSP_ARROW_KEYS, -50, 50, 0);
	clusterTileCtrlSizer->Add(m_clusterTileOffsetYSpin, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

	clusterTileCtrlSizer->Add(new wxStaticText(m_clusterControlsPanel, wxID_ANY, "Item:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	m_clusterNewItemIdSpin = new wxSpinCtrl(m_clusterControlsPanel, ID_CLUSTER_NEW_ITEM_ID, "0",
		wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	clusterTileCtrlSizer->Add(m_clusterNewItemIdSpin, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	m_clusterBrowseItemBtn = new wxButton(m_clusterControlsPanel, ID_CLUSTER_BROWSE_ITEM, "...", wxDefaultPosition, wxSize(25, -1));
	clusterTileCtrlSizer->Add(m_clusterBrowseItemBtn, 0, wxALIGN_CENTER_VERTICAL);
	clusterTilesBox->Add(clusterTileCtrlSizer, 0, wxALL, 5);

	// Tile/item buttons row
	wxBoxSizer* clusterBtnRow = new wxBoxSizer(wxHORIZONTAL);
	clusterBtnRow->Add(new wxButton(m_clusterControlsPanel, ID_CLUSTER_ADD_TILE, "Add Tile", wxDefaultPosition, wxSize(70, -1)), 0, wxRIGHT, 3);
	clusterBtnRow->Add(new wxButton(m_clusterControlsPanel, ID_CLUSTER_REMOVE_TILE, "Remove Tile", wxDefaultPosition, wxSize(85, -1)), 0, wxRIGHT, 8);
	clusterBtnRow->Add(new wxButton(m_clusterControlsPanel, ID_CLUSTER_ADD_ITEM, "Add Item", wxDefaultPosition, wxSize(70, -1)), 0, wxRIGHT, 3);
	clusterBtnRow->Add(new wxButton(m_clusterControlsPanel, ID_CLUSTER_REMOVE_ITEM, "Remove Item", wxDefaultPosition, wxSize(90, -1)), 0);
	clusterTilesBox->Add(clusterBtnRow, 0, wxALL, 5);

	// From Selection + Preview buttons
	wxBoxSizer* clusterActionRow = new wxBoxSizer(wxHORIZONTAL);
	m_clusterFromSelectionBtn = new wxButton(m_clusterControlsPanel, ID_CLUSTER_FROM_SELECTION, "Add From Selection");
	m_clusterFromSelectionBtn->SetToolTip("Populate cluster tiles from current map selection");
	clusterActionRow->Add(m_clusterFromSelectionBtn, 0, wxRIGHT, 5);
	m_clusterPreviewBtn = new wxButton(m_clusterControlsPanel, ID_CLUSTER_PREVIEW, "Preview Cluster");
	m_clusterPreviewBtn->SetToolTip("Open cluster preview/editor window");
	clusterActionRow->Add(m_clusterPreviewBtn, 0);
	clusterTilesBox->Add(clusterActionRow, 0, wxALL, 5);

	clusterPanelSizer->Add(clusterTilesBox, 1, wxEXPAND);

	// Instance settings
	wxStaticBoxSizer* instanceBox = new wxStaticBoxSizer(wxVERTICAL, m_clusterControlsPanel, "Instance Settings");
	wxFlexGridSizer* instanceGrid = new wxFlexGridSizer(2, 5, 10);

	instanceGrid->Add(new wxStaticText(m_clusterControlsPanel, wxID_ANY, "Instance Count:"), 0, wxALIGN_CENTER_VERTICAL);
	m_instanceCountSpin = new wxSpinCtrl(m_clusterControlsPanel, ID_CLUSTER_INSTANCE_COUNT, "1",
		wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, 100, 1);
	m_instanceCountSpin->SetToolTip("Number of cluster instances to place");
	instanceGrid->Add(m_instanceCountSpin, 0);

	instanceGrid->Add(new wxStaticText(m_clusterControlsPanel, wxID_ANY, "Min Distance:"), 0, wxALIGN_CENTER_VERTICAL);
	m_instanceMinDistSpin = new wxSpinCtrl(m_clusterControlsPanel, ID_CLUSTER_INSTANCE_DIST, "5",
		wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 100, 5);
	m_instanceMinDistSpin->SetToolTip("Minimum distance between cluster instances");
	instanceGrid->Add(m_instanceMinDistSpin, 0);

	instanceBox->Add(instanceGrid, 0, wxALL, 5);

	// Center point label
	m_clusterCenterLabel = new wxStaticText(m_clusterControlsPanel, wxID_ANY, "No center defined");
	instanceBox->Add(m_clusterCenterLabel, 0, wxLEFT | wxBOTTOM, 5);

	clusterPanelSizer->Add(instanceBox, 0, wxTOP | wxEXPAND, 5);

	m_clusterControlsPanel->SetSizer(clusterPanelSizer);
	m_clusterControlsPanel->Hide();  // Hidden by default
	leftColumn->Add(m_clusterControlsPanel, 0, wxALL | wxEXPAND, 5);

	// Settings
	wxStaticBoxSizer* settingsBox = new wxStaticBoxSizer(wxVERTICAL, this, "Settings");
	wxFlexGridSizer* settingsGrid = new wxFlexGridSizer(2, 5, 10);

	settingsGrid->Add(new wxStaticText(this, wxID_ANY, "Density (%):"), 0, wxALIGN_CENTER_VERTICAL);
	m_densitySpin = new wxSpinCtrl(this, wxID_ANY, "100", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, 100, 100);
	settingsGrid->Add(m_densitySpin, 0);

	settingsGrid->Add(new wxStaticText(this, wxID_ANY, "Max Placements:"), 0, wxALIGN_CENTER_VERTICAL);
	m_maxPlacementsSpin = new wxSpinCtrl(this, wxID_ANY, "-1", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, -1, 10000, -1);
	settingsGrid->Add(m_maxPlacementsSpin, 0);

	settingsGrid->Add(new wxStaticText(this, wxID_ANY, "Priority:"), 0, wxALIGN_CENTER_VERTICAL);
	m_prioritySpin = new wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 100, 0);
	settingsGrid->Add(m_prioritySpin, 0);

	settingsBox->Add(settingsGrid, 0, wxALL, 5);
	settingsBox->Add(new wxStaticText(this, wxID_ANY, "(-1 = unlimited placements)"), 0, wxLEFT | wxBOTTOM, 5);
	leftColumn->Add(settingsBox, 0, wxALL | wxEXPAND, 5);

	// Border Item (placed on top of decorations)
	wxStaticBoxSizer* borderBox = new wxStaticBoxSizer(wxVERTICAL, this, "Border Item (placed on top)");

	wxBoxSizer* borderRowSizer = new wxBoxSizer(wxHORIZONTAL);

	// Border preview panel
	m_borderPreviewPanel = new wxPanel(this, ID_BORDER_PREVIEW, wxDefaultPosition, wxSize(48, 48));
	m_borderPreviewPanel->SetBackgroundStyle(wxBG_STYLE_PAINT);
	m_borderPreviewPanel->Bind(wxEVT_PAINT, &FloorRuleEditDialog::OnPaintBorderPreview, this);
	borderRowSizer->Add(m_borderPreviewPanel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

	// Border item ID input
	borderRowSizer->Add(new wxStaticText(this, wxID_ANY, "Item ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_borderItemSpin = new wxSpinCtrl(this, ID_BORDER_ITEM_SPIN, "0", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	m_borderItemSpin->SetToolTip("Border item ID to place on top of decorations (0 = none)");
	borderRowSizer->Add(m_borderItemSpin, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

	// Browse button for border item
	wxButton* browseBorderBtn = new wxButton(this, ID_BROWSE_BORDER_ITEM, "...", wxDefaultPosition, wxSize(25, -1));
	browseBorderBtn->SetToolTip("Browse for border item");
	borderRowSizer->Add(browseBorderBtn, 0, wxALIGN_CENTER_VERTICAL);

	borderBox->Add(borderRowSizer, 0, wxALL, 5);
	borderBox->Add(new wxStaticText(this, wxID_ANY, "(0 = no border item)"), 0, wxLEFT | wxBOTTOM, 5);
	leftColumn->Add(borderBox, 0, wxALL | wxEXPAND, 5);

	// Friend floor bias
	wxStaticBoxSizer* friendBox = new wxStaticBoxSizer(wxVERTICAL, this, "Friend Floor (bias placement)");
	wxBoxSizer* friendPreviewSizer = new wxBoxSizer(wxHORIZONTAL);
	friendPreviewSizer->Add(new wxStaticText(this, wxID_ANY, "Preview:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

	wxBoxSizer* friendPreviewPanels = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* friendFromSizer = new wxBoxSizer(wxVERTICAL);
	friendFromSizer->Add(new wxStaticText(this, wxID_ANY, "From"), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 2);
	m_friendPreviewPanelFrom = new wxPanel(this, ID_FRIEND_PREVIEW_FROM, wxDefaultPosition, wxSize(48, 48));
	m_friendPreviewPanelFrom->SetBackgroundStyle(wxBG_STYLE_PAINT);
	m_friendPreviewPanelFrom->Bind(wxEVT_PAINT, &FloorRuleEditDialog::OnPaintFriendPreview, this);
	friendFromSizer->Add(m_friendPreviewPanelFrom, 0);
	friendPreviewPanels->Add(friendFromSizer, 0, wxRIGHT, 8);

	wxBoxSizer* friendToSizer = new wxBoxSizer(wxVERTICAL);
	friendToSizer->Add(new wxStaticText(this, wxID_ANY, "To"), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 2);
	m_friendPreviewPanelTo = new wxPanel(this, ID_FRIEND_PREVIEW_TO, wxDefaultPosition, wxSize(48, 48));
	m_friendPreviewPanelTo->SetBackgroundStyle(wxBG_STYLE_PAINT);
	m_friendPreviewPanelTo->Bind(wxEVT_PAINT, &FloorRuleEditDialog::OnPaintFriendPreview, this);
	friendToSizer->Add(m_friendPreviewPanelTo, 0);
	friendPreviewPanels->Add(friendToSizer, 0);

	friendPreviewSizer->Add(friendPreviewPanels, 0);
	friendBox->Add(friendPreviewSizer, 0, wxALL, 5);

	m_friendSingleFloorRadio = new wxRadioButton(this, ID_FRIEND_FLOOR_TYPE_SINGLE, "Single Friend Floor ID", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	wxBoxSizer* friendSingleSizer = new wxBoxSizer(wxHORIZONTAL);
	friendSingleSizer->Add(m_friendSingleFloorRadio, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
	m_friendSingleFloorSpin = new wxSpinCtrl(this, ID_FRIEND_SINGLE_FLOOR_SPIN, "0", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	m_friendSingleFloorSpin->SetToolTip("Ground tile ID to bias placement toward (0 = none)");
	friendSingleSizer->Add(m_friendSingleFloorSpin, 0, wxALIGN_CENTER_VERTICAL);
	friendBox->Add(friendSingleSizer, 0, wxALL, 5);

	m_friendRangeRadio = new wxRadioButton(this, ID_FRIEND_FLOOR_TYPE_RANGE, "Friend Floor Range");
	wxBoxSizer* friendRangeSizer = new wxBoxSizer(wxHORIZONTAL);
	friendRangeSizer->Add(m_friendRangeRadio, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
	friendRangeSizer->Add(new wxStaticText(this, wxID_ANY, "From:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	m_friendFromFloorSpin = new wxSpinCtrl(this, ID_FRIEND_FROM_FLOOR_SPIN, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	friendRangeSizer->Add(m_friendFromFloorSpin, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	friendRangeSizer->Add(new wxStaticText(this, wxID_ANY, "To:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	m_friendToFloorSpin = new wxSpinCtrl(this, ID_FRIEND_TO_FLOOR_SPIN, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	friendRangeSizer->Add(m_friendToFloorSpin, 0, wxALIGN_CENTER_VERTICAL);
	friendBox->Add(friendRangeSizer, 0, wxALL, 5);

	wxFlexGridSizer* friendGrid = new wxFlexGridSizer(2, 5, 10);
	friendGrid->Add(new wxStaticText(this, wxID_ANY, "Friend Chance (%):"), 0, wxALIGN_CENTER_VERTICAL);
	m_friendChanceSpin = new wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 100, 0);
	m_friendChanceSpin->SetToolTip("Chance to favor tiles closer to the friend floor (0 = off)");
	friendGrid->Add(m_friendChanceSpin, 0);

	friendGrid->Add(new wxStaticText(this, wxID_ANY, "Friend Strength (%):"), 0, wxALIGN_CENTER_VERTICAL);
	m_friendStrengthSpin = new wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 100, 0);
	m_friendStrengthSpin->SetToolTip("Higher values make the bias tighter to the friend floor");
	friendGrid->Add(m_friendStrengthSpin, 0);

	friendBox->Add(friendGrid, 0, wxALL, 5);
	friendBox->Add(new wxStaticText(this, wxID_ANY, "(0 = disabled)"), 0, wxLEFT | wxBOTTOM, 5);
	leftColumn->Add(friendBox, 0, wxALL | wxEXPAND, 5);

	// Add doodad browser to left column
	wxStaticBoxSizer* doodadBox = new wxStaticBoxSizer(wxVERTICAL, this, "Doodad Browser (double-click to add)");
	if (wxStaticBox* doodadStatic = doodadBox->GetStaticBox()) {
		doodadStatic->SetMinSize(wxSize(300, -1));
		doodadStatic->SetMaxSize(wxSize(300, -1));
	}

	// Search row
	wxBoxSizer* searchSizer = new wxBoxSizer(wxHORIZONTAL);
	m_doodadSearchCtrl = new wxTextCtrl(this, ID_DOODAD_SEARCH, "", wxDefaultPosition, wxDefaultSize);
	m_doodadSearchCtrl->SetHint("Search doodads...");
	searchSizer->Add(m_doodadSearchCtrl, 1, wxRIGHT, 5);
	wxButton* addDoodadBtn = new wxButton(this, ID_ADD_DOODAD, "Add", wxDefaultPosition, wxSize(50, -1));
	addDoodadBtn->SetToolTip("Add doodad entries (singles + composites)");
	searchSizer->Add(addDoodadBtn, 0);
	doodadBox->Add(searchSizer, 0, wxALL | wxEXPAND, 5);

	// Doodad list
	m_doodadImageList = new wxImageList(ITEM_ICON_SIZE, ITEM_ICON_SIZE, true);
	m_doodadListCtrl = new wxListCtrl(this, ID_DOODAD_LIST, wxDefaultPosition, wxSize(-1, 100),
	                                    wxLC_REPORT | wxLC_SINGLE_SEL);
	m_doodadListCtrl->SetImageList(m_doodadImageList, wxIMAGE_LIST_SMALL);
	m_doodadListCtrl->InsertColumn(0, "", wxLIST_FORMAT_LEFT, 40);
	m_doodadListCtrl->InsertColumn(1, "Name", wxLIST_FORMAT_LEFT, 150);
	m_doodadListCtrl->InsertColumn(2, "#", wxLIST_FORMAT_LEFT, 35);
	doodadBox->Add(m_doodadListCtrl, 1, wxLEFT | wxRIGHT | wxEXPAND, 5);

	// Pagination
	wxBoxSizer* pageSizer = new wxBoxSizer(wxHORIZONTAL);
	m_prevPageBtn = new wxButton(this, ID_DOODAD_PREV_PAGE, "<", wxDefaultPosition, wxSize(30, -1));
	m_pageInfoText = new wxStaticText(this, wxID_ANY, "1/1", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
	m_nextPageBtn = new wxButton(this, ID_DOODAD_NEXT_PAGE, ">", wxDefaultPosition, wxSize(30, -1));
	pageSizer->Add(m_prevPageBtn, 0);
	pageSizer->Add(m_pageInfoText, 1, wxALIGN_CENTER_VERTICAL);
	pageSizer->Add(m_nextPageBtn, 0);
	doodadBox->Add(pageSizer, 0, wxALL | wxEXPAND, 5);

	columnsSizer->Add(leftColumn, 0, wxEXPAND);

	// ==================== RIGHT COLUMN ====================
	wxBoxSizer* rightColumn = new wxBoxSizer(wxVERTICAL);

	// Items list (main focus - larger area)
	wxStaticBoxSizer* itemsBox = new wxStaticBoxSizer(wxVERTICAL, this, "Items (drag & drop supported)");

	// Create image list for icons
	m_itemsImageList = new wxImageList(ITEM_ICON_SIZE, ITEM_ICON_SIZE, true);
	m_itemsListCtrl = new wxListCtrl(this, ID_ITEMS_LIST, wxDefaultPosition, wxSize(350, -1), wxLC_REPORT | wxLC_SINGLE_SEL);
	m_itemsListCtrl->SetImageList(m_itemsImageList, wxIMAGE_LIST_SMALL);
	m_itemsListCtrl->InsertColumn(0, "", wxLIST_FORMAT_LEFT, 40);
	m_itemsListCtrl->InsertColumn(1, "Item ID", wxLIST_FORMAT_LEFT, 80);
	m_itemsListCtrl->InsertColumn(2, "Weight", wxLIST_FORMAT_LEFT, 60);
	m_itemsListCtrl->InsertColumn(3, "Name", wxLIST_FORMAT_LEFT, 150);
	itemsBox->Add(m_itemsListCtrl, 1, wxALL | wxEXPAND, 5);

	// ---- Highlighted panel: Add Single Item ----
	wxPanel* singleItemPanel = new wxPanel(this, wxID_ANY);
	singleItemPanel->SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	wxBoxSizer* singleItemSizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* singleItemLabel = new wxStaticText(singleItemPanel, wxID_ANY, "Add Single Item");
	singleItemLabel->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	wxFont labelFont = singleItemLabel->GetFont();
	labelFont.SetWeight(wxFONTWEIGHT_BOLD);
	singleItemLabel->SetFont(labelFont);
	singleItemSizer->Add(singleItemLabel, 0, wxLEFT | wxTOP, 6);

	wxBoxSizer* singleItemRow = new wxBoxSizer(wxHORIZONTAL);
	singleItemRow->Add(new wxStaticText(singleItemPanel, wxID_ANY, "ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	m_newItemIdSpin = new wxSpinCtrl(singleItemPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	singleItemRow->Add(m_newItemIdSpin, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

	wxButton* browseBtn = new wxButton(singleItemPanel, ID_BROWSE_ITEM, "...", wxDefaultPosition, wxSize(25, -1));
	browseBtn->SetToolTip("Browse for item");
	singleItemRow->Add(browseBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

	singleItemRow->Add(new wxStaticText(singleItemPanel, wxID_ANY, "Weight:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	m_newItemWeightSpin = new wxSpinCtrl(singleItemPanel, wxID_ANY, "100", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 1, 1000, 100);
	singleItemRow->Add(m_newItemWeightSpin, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

	wxButton* addBtn = new wxButton(singleItemPanel, ID_ADD_ITEM, "Add", wxDefaultPosition, wxSize(50, -1));
	singleItemRow->Add(addBtn, 0, wxALIGN_CENTER_VERTICAL);

	singleItemSizer->Add(singleItemRow, 0, wxALL, 6);
	singleItemPanel->SetSizer(singleItemSizer);
	itemsBox->Add(singleItemPanel, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 5);

	// ---- Action buttons row (below the single item panel) ----
	wxBoxSizer* actionBtnSizer = new wxBoxSizer(wxHORIZONTAL);
	m_editItemBtn = new wxButton(this, ID_EDIT_ITEM, "Edit", wxDefaultPosition, wxSize(50, -1));
	m_previewClusterItemBtn = new wxButton(this, ID_PREVIEW_CLUSTER_ITEM, "Preview", wxDefaultPosition, wxSize(60, -1));
	wxButton* removeBtn = new wxButton(this, ID_REMOVE_ITEM, "Remove", wxDefaultPosition, wxSize(60, -1));
	wxButton* clearBtn = new wxButton(this, ID_CLEAR_ITEMS, "Clear All", wxDefaultPosition, wxSize(70, -1));
	actionBtnSizer->Add(m_editItemBtn, 0, wxRIGHT, 5);
	actionBtnSizer->Add(m_previewClusterItemBtn, 0, wxRIGHT, 5);
	actionBtnSizer->Add(removeBtn, 0, wxRIGHT, 5);
	actionBtnSizer->Add(clearBtn, 0);

	m_editItemBtn->Enable(false);
	m_editItemBtn->SetToolTip("Apply the fields above to the selected item/cluster");
	m_previewClusterItemBtn->Enable(false);
	m_previewClusterItemBtn->SetToolTip("Preview the selected cluster item");

	itemsBox->Add(actionBtnSizer, 0, wxALL, 5);

	wxBoxSizer* itemsDoodadSizer = new wxBoxSizer(wxHORIZONTAL);
	itemsDoodadSizer->Add(itemsBox, 1, wxALL | wxEXPAND, 5);
	itemsDoodadSizer->Add(doodadBox, 0, wxALL | wxEXPAND, 5);
	rightColumn->Add(itemsDoodadSizer, 1, wxEXPAND);

	wxStaticBoxSizer* clusterBox = new wxStaticBoxSizer(wxVERTICAL, this, "Cluster From Selection");
	wxBoxSizer* clusterRow = new wxBoxSizer(wxHORIZONTAL);

	clusterRow->Add(new wxStaticText(this, wxID_ANY, "Count:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_clusterCountSpin = new wxSpinCtrl(this, wxID_ANY, "1", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 1, 100, 1);
	clusterRow->Add(m_clusterCountSpin, 0, wxRIGHT, 15);

	clusterRow->Add(new wxStaticText(this, wxID_ANY, "Radius:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_clusterRadiusSpin = new wxSpinCtrl(this, wxID_ANY, "3", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 0, 50, 3);
	clusterRow->Add(m_clusterRadiusSpin, 0, wxRIGHT, 15);

	clusterRow->Add(new wxStaticText(this, wxID_ANY, "Min Dist:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_clusterMinDistanceSpin = new wxSpinCtrl(this, wxID_ANY, "2", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 0, 50, 2);
	clusterRow->Add(m_clusterMinDistanceSpin, 0);

	clusterBox->Add(clusterRow, 0, wxALL, 5);
	wxBoxSizer* clusterBtnSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addClusterBtn = new wxButton(this, ID_ADD_CLUSTER, "Add Cluster From Selection");
	m_replaceClusterBtn = new wxButton(this, ID_REPLACE_CLUSTER, "Replace Selected Cluster");
	m_replaceClusterBtn->Enable(false);
	clusterBtnSizer->Add(addClusterBtn, 0, wxRIGHT, 5);
	clusterBtnSizer->Add(m_replaceClusterBtn, 0);
	clusterBox->Add(clusterBtnSizer, 0, wxALL, 5);
	rightColumn->Add(clusterBox, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

	columnsSizer->Add(rightColumn, 1, wxEXPAND);

	mainSizer->Add(columnsSizer, 1, wxEXPAND);

	// Buttons at bottom
	wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
	btnSizer->AddStretchSpacer();
	wxButton* okBtn = new wxButton(this, ID_RULE_OK, "OK");
	wxButton* cancelBtn = new wxButton(this, ID_RULE_CANCEL, "Cancel");
	btnSizer->Add(okBtn, 0, wxRIGHT, 5);
	btnSizer->Add(cancelBtn, 0);
	mainSizer->Add(btnSizer, 0, wxALL | wxEXPAND, 10);

	SetSizer(mainSizer);
}

void FloorRuleEditDialog::LoadRuleData() {
	m_nameInput->SetValue(m_rule.name);

	if (m_rule.isClusterRule()) {
		m_clusterRadio->SetValue(true);
		m_singleFloorSpin->Enable(false);
		m_fromFloorSpin->Enable(false);
		m_toFloorSpin->Enable(false);
		// Load cluster-specific settings
		if (m_instanceCountSpin) {
			m_instanceCountSpin->SetValue(m_rule.instanceCount);
		}
		if (m_instanceMinDistSpin) {
			m_instanceMinDistSpin->SetValue(m_rule.instanceMinDistance);
		}
		// Update center label
		if (m_clusterCenterLabel) {
			if (m_rule.hasCenterPoint) {
				m_clusterCenterLabel->SetLabel(wxString::Format("Center: (%d, %d)",
					m_rule.centerOffset.x, m_rule.centerOffset.y));
			} else {
				m_clusterCenterLabel->SetLabel("No center defined");
			}
		}
		UpdateClusterTilesList();
	} else if (m_rule.isRangeRule()) {
		m_floorRangeRadio->SetValue(true);
		m_fromFloorSpin->SetValue(m_rule.fromFloorId);
		m_toFloorSpin->SetValue(m_rule.toFloorId);
		m_singleFloorSpin->Enable(false);
	} else {
		m_singleFloorRadio->SetValue(true);
		m_singleFloorSpin->SetValue(m_rule.floorId);
		m_fromFloorSpin->Enable(false);
		m_toFloorSpin->Enable(false);
	}

	UpdateClusterControls();

	m_densitySpin->SetValue(static_cast<int>(m_rule.density * 100));
	m_maxPlacementsSpin->SetValue(m_rule.maxPlacements);
	m_prioritySpin->SetValue(m_rule.priority);

	// Load border item
	m_borderItemSpin->SetValue(m_rule.borderItemId);
	if (m_rule.isFriendRange()) {
		m_friendRangeRadio->SetValue(true);
		m_friendFromFloorSpin->SetValue(m_rule.friendFromFloorId);
		m_friendToFloorSpin->SetValue(m_rule.friendToFloorId);
		m_friendSingleFloorSpin->Enable(false);
	} else {
		m_friendSingleFloorRadio->SetValue(true);
		m_friendSingleFloorSpin->SetValue(m_rule.friendFloorId);
		m_friendFromFloorSpin->Enable(false);
		m_friendToFloorSpin->Enable(false);
	}
	m_friendChanceSpin->SetValue(m_rule.friendChance);
	m_friendStrengthSpin->SetValue(m_rule.friendStrength);

	UpdateItemsList();
	UpdateFloorPreview();
	UpdateFriendPreview();
	UpdateBorderPreview();
}

void FloorRuleEditDialog::UpdateItemsList() {
	m_itemsListCtrl->DeleteAllItems();
	m_itemsImageList->RemoveAll();

	for (size_t i = 0; i < m_rule.items.size(); ++i) {
		const auto& item = m_rule.items[i];

		uint16_t iconItemId = item.isCompositeEntry() ? item.getRepresentativeItemId() : item.itemId;

		// Get item bitmap and add to image list
		wxBitmap bmp = GetItemBitmap(iconItemId, ITEM_ICON_SIZE);
		int imgIdx = m_itemsImageList->Add(bmp);

		// Get item name and display ID
		wxString itemName = "Unknown";
		wxString itemIdText;
		if (item.isCompositeEntry()) {
			if (item.isClusterEntry()) {
				itemIdText = "Cluster";
				itemName = wxString::Format("Cluster (%zu tiles, %zu items) x%d",
					item.getCompositeTileCount(),
					item.getCompositeItemCount(),
					item.clusterCount);
			} else {
				itemIdText = "Composite";
				itemName = wxString::Format("Composite (%zu tiles, %zu items)",
					item.getCompositeTileCount(),
					item.getCompositeItemCount());
			}
		} else {
			itemIdText = wxString::Format("%d", item.itemId);
			const ItemType& iType = g_items.getItemType(item.itemId);
			if (iType.id != 0) {
				itemName = wxstr(iType.name);
				if (itemName.IsEmpty()) {
					itemName = wxString::Format("Item #%d", item.itemId);
				}
			}
		}

		// Insert item with icon
		long idx = m_itemsListCtrl->InsertItem(i, "", imgIdx);
		m_itemsListCtrl->SetItem(idx, 1, itemIdText);
		m_itemsListCtrl->SetItem(idx, 2, wxString::Format("%d", item.weight));
		m_itemsListCtrl->SetItem(idx, 3, itemName);
	}

	if (m_editItemBtn) {
		m_editItemBtn->Enable(false);
	}
	if (m_previewClusterItemBtn) {
		m_previewClusterItemBtn->Enable(false);
	}
	if (m_replaceClusterBtn) {
		m_replaceClusterBtn->Enable(false);
	}
}

wxBitmap FloorRuleEditDialog::GetItemBitmap(uint16_t itemId, int size) {
	wxBitmap bmp(size, size, 32);
	wxMemoryDC dc(bmp);

	// Fill with dark background
	dc.SetBackground(wxBrush(wxColour(0x0C, 0x14, 0x2A)));
	dc.Clear();

	// Get the ItemType to find the client sprite ID
	const ItemType& itemType = g_items.getItemType(itemId);
	Sprite* spr = nullptr;
	if (itemType.id != 0) {
		spr = g_gui.gfx.getSprite(itemType.clientID);
	}

	if (spr) {
		spr->DrawTo(&dc, SPRITE_SIZE_32x32, 0, 0, size, size);
	} else {
		// Draw placeholder
		dc.SetBrush(wxBrush(wxColour(100, 100, 100)));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(2, 2, size - 4, size - 4);
		dc.SetTextForeground(*wxWHITE);
		dc.DrawText("?", size / 2 - 4, size / 2 - 8);
	}

	dc.SelectObject(wxNullBitmap);
	return bmp;
}

void FloorRuleEditDialog::UpdateFloorPreview() {
	if (m_floorPreviewPanelFrom) {
		m_floorPreviewPanelFrom->Refresh();
	}
	if (m_floorPreviewPanelTo) {
		m_floorPreviewPanelTo->Refresh();
	}
}

void FloorRuleEditDialog::UpdateFriendPreview() {
	if (m_friendPreviewPanelFrom) {
		m_friendPreviewPanelFrom->Refresh();
	}
	if (m_friendPreviewPanelTo) {
		m_friendPreviewPanelTo->Refresh();
	}
}

void FloorRuleEditDialog::UpdateBorderPreview() {
	if (m_borderPreviewPanel) {
		m_borderPreviewPanel->Refresh();
	}
}

void FloorRuleEditDialog::OnPaintFloorPreview(wxPaintEvent& event) {
	// In cluster mode, no floor to preview
	if (m_clusterRadio && m_clusterRadio->GetValue()) {
		return;
	}

	wxPanel* panel = wxDynamicCast(event.GetEventObject(), wxPanel);
	if (!panel) {
		return;
	}

	wxBufferedPaintDC dc(panel);
	wxSize size = panel->GetSize();

	dc.SetBackground(wxBrush(wxColour(0x0C, 0x14, 0x2A)));
	dc.Clear();

	uint16_t floorId = 0;
	if (panel == m_floorPreviewPanelFrom) {
		floorId = m_singleFloorRadio->GetValue() ? m_singleFloorSpin->GetValue()
		                                         : m_fromFloorSpin->GetValue();
	} else if (panel == m_floorPreviewPanelTo) {
		floorId = m_singleFloorRadio->GetValue() ? m_singleFloorSpin->GetValue()
		                                         : m_toFloorSpin->GetValue();
	}

	if (floorId > 0) {
		const ItemType& itemType = g_items.getItemType(floorId);
		Sprite* spr = nullptr;
		if (itemType.id != 0) {
			spr = g_gui.gfx.getSprite(itemType.clientID);
		}
		if (spr) {
			int drawSize = std::min(size.GetWidth(), size.GetHeight()) - 4;
			int x = (size.GetWidth() - drawSize) / 2;
			int y = (size.GetHeight() - drawSize) / 2;
			spr->DrawTo(&dc, SPRITE_SIZE_32x32, x, y, drawSize, drawSize);
		}
	}

	dc.SetPen(wxPen(wxColour(80, 80, 120), 1));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());
}

void FloorRuleEditDialog::OnPaintFriendPreview(wxPaintEvent& event) {
	wxPanel* panel = wxDynamicCast(event.GetEventObject(), wxPanel);
	if (!panel) {
		return;
	}

	wxBufferedPaintDC dc(panel);
	wxSize size = panel->GetSize();

	dc.SetBackground(wxBrush(wxColour(0x0C, 0x14, 0x2A)));
	dc.Clear();

	uint16_t floorId = 0;
	if (panel == m_friendPreviewPanelFrom) {
		floorId = m_friendSingleFloorRadio->GetValue() ? m_friendSingleFloorSpin->GetValue()
		                                               : m_friendFromFloorSpin->GetValue();
	} else if (panel == m_friendPreviewPanelTo) {
		floorId = m_friendSingleFloorRadio->GetValue() ? m_friendSingleFloorSpin->GetValue()
		                                               : m_friendToFloorSpin->GetValue();
	}

	if (floorId > 0) {
		const ItemType& itemType = g_items.getItemType(floorId);
		Sprite* spr = nullptr;
		if (itemType.id != 0) {
			spr = g_gui.gfx.getSprite(itemType.clientID);
		}
		if (spr) {
			int drawSize = std::min(size.GetWidth(), size.GetHeight()) - 4;
			int x = (size.GetWidth() - drawSize) / 2;
			int y = (size.GetHeight() - drawSize) / 2;
			spr->DrawTo(&dc, SPRITE_SIZE_32x32, x, y, drawSize, drawSize);
		}
	}

	dc.SetPen(wxPen(wxColour(80, 80, 120), 1));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());
}

void FloorRuleEditDialog::OnPaintBorderPreview(wxPaintEvent& event) {
	wxBufferedPaintDC dc(m_borderPreviewPanel);

	// Get panel size
	wxSize size = m_borderPreviewPanel->GetSize();

	// Fill with dark background
	dc.SetBackground(wxBrush(wxColour(0x0C, 0x14, 0x2A)));
	dc.Clear();

	uint16_t borderItemId = static_cast<uint16_t>(m_borderItemSpin->GetValue());

	if (borderItemId > 0) {
		// Get the ItemType to find the client sprite ID
		const ItemType& itemType = g_items.getItemType(borderItemId);
		Sprite* spr = nullptr;
		if (itemType.id != 0) {
			spr = g_gui.gfx.getSprite(itemType.clientID);
		}
		if (spr) {
			// Draw centered
			int drawSize = std::min(size.GetWidth(), size.GetHeight()) - 4;
			int x = (size.GetWidth() - drawSize) / 2;
			int y = (size.GetHeight() - drawSize) / 2;
			spr->DrawTo(&dc, SPRITE_SIZE_32x32, x, y, drawSize, drawSize);
		}
	}

	// Draw border
	dc.SetPen(wxPen(wxColour(80, 80, 120), 1));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());
}

void FloorRuleEditDialog::OnBorderItemChanged(wxSpinEvent& event) {
	UpdateBorderPreview();
}

void FloorRuleEditDialog::OnBrowseBorderItem(wxCommandEvent& event) {
	FindItemDialog dialog(this, "Select Border Item");
	if (dialog.ShowModal() == wxID_OK) {
		uint16_t itemId = dialog.getResultID();
		if (itemId > 0) {
			m_borderItemSpin->SetValue(itemId);
			UpdateBorderPreview();
		}
	}
}

void FloorRuleEditDialog::LoadDoodadList() {
	m_allDoodads.clear();

	// Get all doodad brushes from materials
	for (auto& pair : g_materials.tilesets) {
		Tileset* tileset = pair.second;
		if (!tileset) continue;

		TilesetCategory* category = tileset->getCategory(TILESET_DOODAD);
		if (!category) continue;

		for (Brush* brush : category->brushlist) {
			if (brush && brush->is<DoodadBrush>()) {
				DoodadBrush* doodad = brush->as<DoodadBrush>();
				if (doodad) {
					m_allDoodads.push_back(doodad);
				}
			}
		}
	}

	// Sort by name for easier browsing
	std::sort(m_allDoodads.begin(), m_allDoodads.end(),
		[](DoodadBrush* a, DoodadBrush* b) {
			return a->getName() < b->getName();
		});

	// Initialize filtered list with all doodads
	m_filteredDoodads = m_allDoodads;
	m_currentPage = 0;

	UpdateDoodadListDisplay();
}

void FloorRuleEditDialog::FilterDoodads(const wxString& filter) {
	m_filteredDoodads.clear();
	wxString lowerFilter = filter.Lower();

	if (lowerFilter.IsEmpty()) {
		m_filteredDoodads = m_allDoodads;
	} else {
		for (DoodadBrush* doodad : m_allDoodads) {
			wxString name = wxstr(doodad->getName()).Lower();
			if (name.Contains(lowerFilter)) {
				m_filteredDoodads.push_back(doodad);
			}
		}
	}

	m_currentPage = 0;
	UpdateDoodadListDisplay();
}

void FloorRuleEditDialog::UpdateDoodadListDisplay() {
	if (!m_doodadListCtrl || !m_doodadImageList) return;

	m_doodadListCtrl->DeleteAllItems();
	m_doodadImageList->RemoveAll();

	int totalDoodads = static_cast<int>(m_filteredDoodads.size());
	int totalPages = (totalDoodads + DOODADS_PER_PAGE - 1) / DOODADS_PER_PAGE;
	if (totalPages == 0) totalPages = 1;

	// Clamp current page
	if (m_currentPage >= totalPages) m_currentPage = totalPages - 1;
	if (m_currentPage < 0) m_currentPage = 0;

	// Calculate range for this page
	int startIdx = m_currentPage * DOODADS_PER_PAGE;
	int endIdx = std::min(startIdx + DOODADS_PER_PAGE, totalDoodads);

	// Populate list for current page
	for (int i = startIdx; i < endIdx; ++i) {
		DoodadBrush* doodad = m_filteredDoodads[i];

		// Get first item ID for icon and count total entries (singles + composites)
		uint16_t iconItemId = 0;
		int itemCount = 0;
		const auto& alternatives = doodad->getItems().getAlternatives();
		for (const auto& alt : alternatives) {
			// Count single items
			itemCount += alt->single_items.size();
			if (iconItemId == 0 && !alt->single_items.empty() && alt->single_items[0].item) {
				iconItemId = alt->single_items[0].item->getID();
			}

			// Count composite items
			itemCount += alt->composite_items.size();
			if (iconItemId == 0 && !alt->composite_items.empty()) {
				const CompositeTileList& composite = alt->composite_items[0].items;
				for (const auto& tilePair : composite) {
					if (!tilePair.second.empty() && tilePair.second.front()) {
						iconItemId = tilePair.second.front()->getID();
						break;
					}
				}
			}
		}

		// Add icon to image list
		wxBitmap bmp = GetItemBitmap(iconItemId, ITEM_ICON_SIZE);
		int imgIdx = m_doodadImageList->Add(bmp);

		// Insert item
		long idx = m_doodadListCtrl->InsertItem(i - startIdx, "", imgIdx);
		m_doodadListCtrl->SetItem(idx, 1, wxstr(doodad->getName()));
		m_doodadListCtrl->SetItem(idx, 2, wxString::Format("%d", itemCount));
		m_doodadListCtrl->SetItemData(idx, i);  // Store index in filtered list
	}

	// Update pagination controls
	if (m_pageInfoText) {
		m_pageInfoText->SetLabel(wxString::Format("%d/%d (%d)",
			m_currentPage + 1, totalPages, totalDoodads));
	}
	if (m_prevPageBtn) {
		m_prevPageBtn->Enable(m_currentPage > 0);
	}
	if (m_nextPageBtn) {
		m_nextPageBtn->Enable(m_currentPage < totalPages - 1);
	}
}

void FloorRuleEditDialog::AddItemById(uint16_t itemId, int weight) {
	if (itemId == 0) return;

	// Check if item already exists
	for (const auto& item : m_rule.items) {
		if (!item.isCompositeEntry() && item.itemId == itemId) {
			wxMessageBox(wxString::Format("Item %d is already in the list", itemId), "Info", wxOK | wxICON_INFORMATION);
			return;
		}
	}

	m_rule.items.push_back(AreaDecoration::ItemEntry(itemId, weight));
	UpdateItemsList();
}

void FloorRuleEditDialog::AddItemsFromDoodad(DoodadBrush* doodad) {
	if (!doodad) return;

	int addedSingles = 0;
	int addedComposites = 0;
	int defaultWeight = m_newItemWeightSpin->GetValue();

	// Helper lambda to add single item if not already in list
	auto tryAddSingle = [&](uint16_t itemId, int itemWeight) {
		if (itemId == 0) return false;

		for (const auto& item : m_rule.items) {
			if (!item.isCompositeEntry() && item.itemId == itemId) {
				return false;
			}
		}

		if (itemWeight <= 0) itemWeight = defaultWeight;
		m_rule.items.push_back(AreaDecoration::ItemEntry(itemId, itemWeight));
		return true;
	};

	// Add single items from all variations
	const auto& alternatives = doodad->getItems().getAlternatives();
	for (const auto& alt : alternatives) {
		// Add single items
		for (const auto& single : alt->single_items) {
			if (!single.item) continue;
			uint16_t itemId = single.item->getID();
			int itemWeight = single.chance;
			if (tryAddSingle(itemId, itemWeight)) {
				addedSingles++;
			}
		}

		// Add composites as grouped entries
		int prevChance = 0;
		for (const auto& comp : alt->composite_items) {
			const CompositeTileList& composite = comp.items;
			if (composite.empty()) { prevChance = comp.chance; continue; }

			int compositeWeight = comp.chance - prevChance;
			if (compositeWeight <= 0) compositeWeight = defaultWeight;
			prevChance = comp.chance;

			std::vector<AreaDecoration::CompositeTile> tiles;
			for (const auto& tilePair : composite) {
				AreaDecoration::CompositeTile tile;
				tile.offset = tilePair.first;
				const auto& items = tilePair.second;
				for (const auto& item : items) {
					if (item) {
						tile.itemIds.push_back(item->getID());
					}
				}
				if (!tile.itemIds.empty()) {
					tiles.push_back(tile);
				}
			}

			if (!tiles.empty()) {
				m_rule.items.push_back(AreaDecoration::ItemEntry::MakeComposite(tiles, compositeWeight));
				addedComposites++;
			}
		}
	}

	int addedCount = addedSingles + addedComposites;
	if (addedCount > 0) {
		UpdateItemsList();
		wxMessageBox(wxString::Format("Added %d entries (%d singles, %d composites) from doodad '%s'",
			addedCount, addedSingles, addedComposites, wxstr(doodad->getName())),
			"Success", wxOK | wxICON_INFORMATION);
	} else {
		wxMessageBox("No new entries to add from this doodad (items may already exist in the list)", "Info", wxOK | wxICON_INFORMATION);
	}
}

bool FloorRuleEditDialog::TransferDataFromWindow() {
	m_rule.name = m_nameInput->GetValue().ToStdString();

	if (m_clusterRadio->GetValue()) {
		m_rule.ruleMode = AreaDecoration::RuleMode::Cluster;
		m_rule.floorId = 0;
		m_rule.fromFloorId = 0;
		m_rule.toFloorId = 0;
		m_rule.instanceCount = m_instanceCountSpin ? m_instanceCountSpin->GetValue() : 1;
		m_rule.instanceMinDistance = m_instanceMinDistSpin ? m_instanceMinDistSpin->GetValue() : 5;
		if (m_rule.clusterTiles.empty()) {
			wxMessageBox("Cluster must have at least one tile with items.", "Validation Error",
			             wxOK | wxICON_ERROR, this);
			return false;
		}
	} else if (m_floorRangeRadio->GetValue()) {
		m_rule.ruleMode = AreaDecoration::RuleMode::FloorRange;
		m_rule.floorId = 0;
		m_rule.fromFloorId = m_fromFloorSpin->GetValue();
		m_rule.toFloorId = m_toFloorSpin->GetValue();

		if (m_rule.fromFloorId > m_rule.toFloorId) {
			wxMessageBox("From floor ID must be <= To floor ID", "Error", wxOK | wxICON_ERROR);
			return false;
		}
	} else {
		m_rule.ruleMode = AreaDecoration::RuleMode::SingleFloor;
		m_rule.floorId = m_singleFloorSpin->GetValue();
		m_rule.fromFloorId = 0;
		m_rule.toFloorId = 0;

		if (m_rule.floorId == 0) {
			wxMessageBox("Floor ID cannot be 0", "Error", wxOK | wxICON_ERROR);
			return false;
		}
	}

	// For non-cluster modes, items must be present
	if (!m_clusterRadio->GetValue() && m_rule.items.empty()) {
		wxMessageBox("Rule must have at least one item", "Error", wxOK | wxICON_ERROR);
		return false;
	}

	m_rule.density = m_densitySpin->GetValue() / 100.0f;
	m_rule.maxPlacements = m_maxPlacementsSpin->GetValue();
	m_rule.priority = m_prioritySpin->GetValue();
	m_rule.borderItemId = static_cast<uint16_t>(m_borderItemSpin->GetValue());
	if (m_friendRangeRadio->GetValue()) {
		m_rule.friendFloorId = 0;
		m_rule.friendFromFloorId = static_cast<uint16_t>(m_friendFromFloorSpin->GetValue());
		m_rule.friendToFloorId = static_cast<uint16_t>(m_friendToFloorSpin->GetValue());
		if (m_rule.friendFromFloorId > 0 && m_rule.friendToFloorId > 0 &&
		    m_rule.friendFromFloorId > m_rule.friendToFloorId) {
			wxMessageBox("Friend floor range: From must be <= To", "Error", wxOK | wxICON_ERROR);
			return false;
		}
	} else {
		m_rule.friendFloorId = static_cast<uint16_t>(m_friendSingleFloorSpin->GetValue());
		m_rule.friendFromFloorId = 0;
		m_rule.friendToFloorId = 0;
	}
	m_rule.friendChance = m_friendChanceSpin->GetValue();
	m_rule.friendStrength = m_friendStrengthSpin->GetValue();

	return true;
}

void FloorRuleEditDialog::OnFloorTypeChanged(wxCommandEvent& event) {
	bool isSingle = m_singleFloorRadio->GetValue();
	bool isRange = m_floorRangeRadio->GetValue();
	bool isCluster = m_clusterRadio->GetValue();

	// Floor controls
	m_singleFloorSpin->Enable(isSingle);
	m_fromFloorSpin->Enable(isRange);
	m_toFloorSpin->Enable(isRange);

	// Cluster controls
	UpdateClusterControls();

	UpdateFloorPreview();

	// Re-layout to accommodate shown/hidden panels
	if (GetSizer()) {
		GetSizer()->Layout();
	}
	Fit();
}

void FloorRuleEditDialog::OnFloorIdChanged(wxSpinEvent& event) {
	UpdateFloorPreview();
}

void FloorRuleEditDialog::OnFriendFloorTypeChanged(wxCommandEvent& event) {
	bool isSingle = m_friendSingleFloorRadio->GetValue();
	m_friendSingleFloorSpin->Enable(isSingle);
	m_friendFromFloorSpin->Enable(!isSingle);
	m_friendToFloorSpin->Enable(!isSingle);
	UpdateFriendPreview();
}

void FloorRuleEditDialog::OnFriendFloorIdChanged(wxSpinEvent& event) {
	UpdateFriendPreview();
}

void FloorRuleEditDialog::OnAddItem(wxCommandEvent& event) {
	uint16_t itemId = m_newItemIdSpin->GetValue();
	int weight = m_newItemWeightSpin->GetValue();

	if (itemId == 0) {
		wxMessageBox("Item ID cannot be 0", "Error", wxOK | wxICON_ERROR);
		return;
	}

	m_rule.items.push_back(AreaDecoration::ItemEntry(itemId, weight));
	UpdateItemsList();
}

void FloorRuleEditDialog::OnEditItem(wxCommandEvent& event) {
	long selected = m_itemsListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected < 0 || selected >= static_cast<long>(m_rule.items.size())) {
		wxMessageBox("Select an item to edit", "Edit Item", wxOK | wxICON_INFORMATION);
		return;
	}

	AreaDecoration::ItemEntry& entry = m_rule.items[selected];
	if (entry.isCompositeEntry()) {
		EditItemDialog(static_cast<size_t>(selected));
		return;
	}
	int weight = m_newItemWeightSpin->GetValue();

	uint16_t itemId = m_newItemIdSpin->GetValue();
	if (itemId == 0) {
		wxMessageBox("Item ID cannot be 0", "Error", wxOK | wxICON_ERROR);
		return;
	}
	entry.itemId = itemId;
	entry.weight = weight;

	UpdateItemsList();

	if (selected >= 0 && selected < m_itemsListCtrl->GetItemCount()) {
		m_itemsListCtrl->SetItemState(selected, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		m_itemsListCtrl->EnsureVisible(selected);
		if (m_editItemBtn) {
			m_editItemBtn->Enable(true);
		}
	}
}

void FloorRuleEditDialog::OnPreviewClusterItem(wxCommandEvent& event) {
	long selected = m_itemsListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected < 0 || selected >= static_cast<long>(m_rule.items.size())) {
		wxMessageBox("Select a cluster/composite item to preview.", "Preview", wxOK | wxICON_INFORMATION);
		return;
	}

	AreaDecoration::ItemEntry& entry = m_rule.items[selected];
	if (!entry.isCompositeEntry() || entry.compositeTiles.empty()) {
		wxMessageBox("Selected item has no tiles to preview.", "Preview", wxOK | wxICON_INFORMATION);
		return;
	}

	// Use a temporary FloorRule to display the preview, restoring saved center point
	AreaDecoration::FloorRule tempRule;
	tempRule.clusterTiles = entry.compositeTiles;
	tempRule.hasCenterPoint = entry.hasCenterPoint;
	tempRule.centerOffset = entry.centerOffset;

	ClusterPreviewWindow* preview = new ClusterPreviewWindow(this, tempRule, nullptr, "Item Cluster Preview");
	preview->ShowModal();
	preview->Destroy();

	// Copy back any changes
	entry.compositeTiles = tempRule.clusterTiles;
	entry.hasCenterPoint = tempRule.hasCenterPoint;
	entry.centerOffset = tempRule.centerOffset;
	UpdateItemsList();
}

void FloorRuleEditDialog::OnReplaceClusterFromSelection(wxCommandEvent& event) {
	long selected = m_itemsListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected < 0 || selected >= static_cast<long>(m_rule.items.size())) {
		wxMessageBox("Select a cluster to replace", "Replace Cluster", wxOK | wxICON_INFORMATION);
		return;
	}

	AreaDecoration::ItemEntry& entry = m_rule.items[selected];
	if (!entry.isClusterEntry()) {
		wxMessageBox("Selected item is not a cluster entry.", "Replace Cluster", wxOK | wxICON_INFORMATION);
		return;
	}

	std::vector<AreaDecoration::CompositeTile> clusterTiles;
	if (!BuildClusterTilesFromSelection(clusterTiles)) {
		return;
	}

	entry.compositeTiles = std::move(clusterTiles);
	entry.weight = m_newItemWeightSpin ? m_newItemWeightSpin->GetValue() : entry.weight;
	entry.clusterCount = m_clusterCountSpin ? m_clusterCountSpin->GetValue() : entry.clusterCount;
	entry.clusterRadius = m_clusterRadiusSpin ? m_clusterRadiusSpin->GetValue() : entry.clusterRadius;
	entry.clusterMinDistance = m_clusterMinDistanceSpin ? m_clusterMinDistanceSpin->GetValue() : entry.clusterMinDistance;

	UpdateItemsList();
	if (selected >= 0 && selected < m_itemsListCtrl->GetItemCount()) {
		m_itemsListCtrl->SetItemState(selected, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		m_itemsListCtrl->EnsureVisible(selected);
		if (m_editItemBtn) {
			m_editItemBtn->Enable(true);
		}
		if (m_replaceClusterBtn) {
			m_replaceClusterBtn->Enable(true);
		}
	}
}

void FloorRuleEditDialog::OnRemoveItem(wxCommandEvent& event) {
	long selected = m_itemsListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected >= 0 && selected < static_cast<long>(m_rule.items.size())) {
		m_rule.items.erase(m_rule.items.begin() + selected);
		UpdateItemsList();
	}
}

void FloorRuleEditDialog::OnClearItems(wxCommandEvent& event) {
	if (m_rule.items.empty()) return;
	int result = wxMessageBox("Remove all items from this rule?", "Confirm", wxYES_NO | wxICON_WARNING);
	if (result != wxYES) return;
	m_rule.items.clear();
	UpdateItemsList();
}

void FloorRuleEditDialog::OnBrowseItem(wxCommandEvent& event) {
	FindItemDialog dialog(this, "Select Item");
	if (dialog.ShowModal() == wxID_OK) {
		uint16_t itemId = dialog.getResultID();
		if (itemId > 0) {
			m_newItemIdSpin->SetValue(itemId);
		}
	}
}

bool FloorRuleEditDialog::BuildClusterTilesFromSelection(std::vector<AreaDecoration::CompositeTile>& outTiles) {
	outTiles.clear();

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		wxMessageBox("No editor available", "Error", wxOK | wxICON_ERROR);
		return false;
	}

	const Selection& selection = editor->selection;
	if (selection.size() == 0) {
		wxMessageBox("No tiles selected. Select some tiles first.", "Error", wxOK | wxICON_ERROR);
		return false;
	}

	Position minPos(INT_MAX, INT_MAX, INT_MAX);
	Position maxPos(INT_MIN, INT_MIN, INT_MIN);

	const auto& tiles = selection.getTiles();
	for (Tile* tile : tiles) {
		const Position& pos = tile->getPosition();
		minPos.x = std::min(minPos.x, pos.x);
		minPos.y = std::min(minPos.y, pos.y);
		minPos.z = std::min(minPos.z, pos.z);
		maxPos.x = std::max(maxPos.x, pos.x);
		maxPos.y = std::max(maxPos.y, pos.y);
		maxPos.z = std::max(maxPos.z, pos.z);
	}

	Position center((minPos.x + maxPos.x) / 2,
	                (minPos.y + maxPos.y) / 2,
	                (minPos.z + maxPos.z) / 2);

	outTiles.reserve(tiles.size());

	for (Tile* tile : tiles) {
		const Position& pos = tile->getPosition();
		AreaDecoration::CompositeTile compTile;
		compTile.offset = Position(pos.x - center.x, pos.y - center.y, pos.z - center.z);

		if (tile->ground && tile->ground->isSelected()) {
			compTile.itemIds.push_back(tile->ground->getID());
		}
		for (const auto& item : tile->items) {
			if (item && item->isSelected()) {
				compTile.itemIds.push_back(item->getID());
			}
		}

		if (!compTile.itemIds.empty()) {
			outTiles.push_back(compTile);
		}
	}

	if (outTiles.empty()) {
		wxMessageBox("Selected tiles contain no items to add.", "Error", wxOK | wxICON_ERROR);
		return false;
	}

	return true;
}

void FloorRuleEditDialog::PrepareClusterPaste(const AreaDecoration::ItemEntry& entry) {
	if (!entry.isCompositeEntry() || entry.compositeTiles.empty()) {
		wxMessageBox("Cluster has no structure to paste.", "Error", wxOK | wxICON_ERROR);
		return;
	}

	int minX = INT_MAX;
	int minY = INT_MAX;
	int minZ = INT_MAX;
	for (const auto& tile : entry.compositeTiles) {
		minX = std::min(minX, tile.offset.x);
		minY = std::min(minY, tile.offset.y);
		minZ = std::min(minZ, tile.offset.z);
	}

	std::unique_ptr<BaseMap> buffer(new BaseMap());

	for (const auto& tile : entry.compositeTiles) {
		Position pos(tile.offset.x - minX, tile.offset.y - minY, tile.offset.z - minZ);
		Tile* newTile = buffer->createTile(pos.x, pos.y, pos.z);
		if (!newTile) {
			continue;
		}
		for (uint16_t id : tile.itemIds) {
			if (id == 0) continue;
			auto newItem = Item::Create(id);
			if (newItem) {
				newTile->addItem(std::move(newItem));
			}
		}
	}

	// TODO: CopyBuffer::setBuffer not available in rme_redux - paste cluster to map not yet supported
	g_gui.SetStatusText("Cluster preview generated (paste to map not yet supported).");
}

void FloorRuleEditDialog::OnAddClusterFromSelection(wxCommandEvent& event) {
	std::vector<AreaDecoration::CompositeTile> clusterTiles;
	if (!BuildClusterTilesFromSelection(clusterTiles)) {
		return;
	}

	// Use a temporary FloorRule to open the preview and let user define center point
	AreaDecoration::FloorRule tempRule;
	tempRule.clusterTiles = std::move(clusterTiles);

	ClusterPreviewWindow* preview = new ClusterPreviewWindow(this, tempRule, nullptr, "Item Cluster Preview");
	preview->ShowModal();
	preview->Destroy();

	// Copy back the (possibly modified) cluster tiles and center point
	clusterTiles = std::move(tempRule.clusterTiles);

	int weight = m_newItemWeightSpin ? m_newItemWeightSpin->GetValue() : 100;
	int count = m_clusterCountSpin ? m_clusterCountSpin->GetValue() : 3;
	int radius = m_clusterRadiusSpin ? m_clusterRadiusSpin->GetValue() : 3;
	int minDist = m_clusterMinDistanceSpin ? m_clusterMinDistanceSpin->GetValue() : 2;

	AreaDecoration::ItemEntry newEntry = AreaDecoration::ItemEntry::MakeCluster(clusterTiles, weight, count, radius, minDist);
	newEntry.hasCenterPoint = tempRule.hasCenterPoint;
	newEntry.centerOffset = tempRule.centerOffset;
	m_rule.items.push_back(std::move(newEntry));
	UpdateItemsList();
}

//=============================================================================
// Cluster Mode Event Handlers
//=============================================================================

void FloorRuleEditDialog::OnClusterAddTile(wxCommandEvent& event) {
	int offsetX = m_clusterTileOffsetXSpin ? m_clusterTileOffsetXSpin->GetValue() : 0;
	int offsetY = m_clusterTileOffsetYSpin ? m_clusterTileOffsetYSpin->GetValue() : 0;

	// Check if a tile with this offset already exists
	for (const auto& tile : m_rule.clusterTiles) {
		if (tile.offset.x == offsetX && tile.offset.y == offsetY) {
			wxMessageBox(wxString::Format("A tile at offset (%d, %d) already exists.", offsetX, offsetY),
			             "Duplicate Tile", wxOK | wxICON_WARNING, this);
			return;
		}
	}

	AreaDecoration::CompositeTile newTile;
	newTile.offset = Position(offsetX, offsetY, 0);
	m_rule.clusterTiles.push_back(newTile);
	UpdateClusterTilesList();
}

void FloorRuleEditDialog::OnClusterRemoveTile(wxCommandEvent& event) {
	if (!m_clusterTilesListCtrl) return;
	long selected = m_clusterTilesListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected < 0 || selected >= static_cast<long>(m_rule.clusterTiles.size())) {
		wxMessageBox("Select a tile to remove.", "Remove Tile", wxOK | wxICON_INFORMATION, this);
		return;
	}
	m_rule.clusterTiles.erase(m_rule.clusterTiles.begin() + selected);
	UpdateClusterTilesList();
}

void FloorRuleEditDialog::OnClusterAddItem(wxCommandEvent& event) {
	if (!m_clusterTilesListCtrl || !m_clusterNewItemIdSpin) return;
	long selected = m_clusterTilesListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected < 0 || selected >= static_cast<long>(m_rule.clusterTiles.size())) {
		wxMessageBox("Select a tile first, then add an item to it.", "Add Item", wxOK | wxICON_INFORMATION, this);
		return;
	}

	uint16_t itemId = static_cast<uint16_t>(m_clusterNewItemIdSpin->GetValue());
	if (itemId == 0) {
		wxMessageBox("Item ID cannot be 0.", "Error", wxOK | wxICON_ERROR, this);
		return;
	}

	m_rule.clusterTiles[selected].itemIds.push_back(itemId);
	UpdateClusterTilesList();

	// Re-select the tile
	if (selected < m_clusterTilesListCtrl->GetItemCount()) {
		m_clusterTilesListCtrl->SetItemState(selected, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		m_clusterTilesListCtrl->EnsureVisible(selected);
	}
}

void FloorRuleEditDialog::OnClusterRemoveItem(wxCommandEvent& event) {
	if (!m_clusterTilesListCtrl) return;
	long selected = m_clusterTilesListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected < 0 || selected >= static_cast<long>(m_rule.clusterTiles.size())) {
		wxMessageBox("Select a tile first.", "Remove Item", wxOK | wxICON_INFORMATION, this);
		return;
	}

	auto& tileItems = m_rule.clusterTiles[selected].itemIds;
	if (tileItems.empty()) {
		wxMessageBox("Selected tile has no items to remove.", "Remove Item", wxOK | wxICON_INFORMATION, this);
		return;
	}

	// Remove the last item from the selected tile
	tileItems.pop_back();
	UpdateClusterTilesList();

	// Re-select the tile
	if (selected < m_clusterTilesListCtrl->GetItemCount()) {
		m_clusterTilesListCtrl->SetItemState(selected, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		m_clusterTilesListCtrl->EnsureVisible(selected);
	}
}

void FloorRuleEditDialog::OnClusterFromSelection(wxCommandEvent& event) {
	std::vector<AreaDecoration::CompositeTile> tiles;
	if (!BuildClusterTilesFromSelection(tiles)) {
		return;
	}

	m_rule.clusterTiles = std::move(tiles);
	UpdateClusterTilesList();

	// Open the preview window immediately so the user can define the center point
	ClusterPreviewWindow* preview = new ClusterPreviewWindow(this, m_rule, [this]() {
		UpdateClusterTilesList();
		if (m_clusterCenterLabel) {
			if (m_rule.hasCenterPoint) {
				m_clusterCenterLabel->SetLabel(wxString::Format("Center: (%d, %d)",
					m_rule.centerOffset.x, m_rule.centerOffset.y));
			} else {
				m_clusterCenterLabel->SetLabel("No center defined");
			}
		}
	}, "Floor Selection - Cluster Preview");
	preview->ShowModal();
	preview->Destroy();
}

void FloorRuleEditDialog::OnClusterPreview(wxCommandEvent& event) {
	ClusterPreviewWindow* preview = new ClusterPreviewWindow(this, m_rule, [this]() {
		// Callback when cluster is modified in the preview window
		UpdateClusterTilesList();
		if (m_clusterCenterLabel) {
			if (m_rule.hasCenterPoint) {
				m_clusterCenterLabel->SetLabel(wxString::Format("Center: (%d, %d)",
					m_rule.centerOffset.x, m_rule.centerOffset.y));
			} else {
				m_clusterCenterLabel->SetLabel("No center defined");
			}
		}
	}, "Floor Selection - Cluster Preview");
	preview->ShowModal();
	preview->Destroy();
}

void FloorRuleEditDialog::OnClusterBrowseItem(wxCommandEvent& event) {
	FindItemDialog dialog(this, "Select Item");
	if (dialog.ShowModal() == wxID_OK) {
		uint16_t itemId = dialog.getResultID();
		if (itemId > 0 && m_clusterNewItemIdSpin) {
			m_clusterNewItemIdSpin->SetValue(itemId);
		}
	}
}

void FloorRuleEditDialog::UpdateClusterTilesList() {
	if (!m_clusterTilesListCtrl || !m_clusterTilesImageList) return;

	m_clusterTilesListCtrl->DeleteAllItems();
	m_clusterTilesImageList->RemoveAll();

	for (size_t i = 0; i < m_rule.clusterTiles.size(); ++i) {
		const auto& tile = m_rule.clusterTiles[i];

		// Get representative item for icon
		uint16_t iconItemId = 0;
		if (!tile.itemIds.empty()) {
			iconItemId = tile.itemIds[0];
		}

		wxBitmap bmp = GetItemBitmap(iconItemId, ITEM_ICON_SIZE);
		int imgIdx = m_clusterTilesImageList->Add(bmp);

		// Offset column
		wxString offsetStr = wxString::Format("(%d, %d)", tile.offset.x, tile.offset.y);

		// Items column - list all item IDs
		wxString itemsStr;
		for (size_t j = 0; j < tile.itemIds.size(); ++j) {
			if (j > 0) itemsStr += ", ";
			itemsStr += wxString::Format("%d", tile.itemIds[j]);
			if (j >= 5 && tile.itemIds.size() > 6) {
				itemsStr += wxString::Format(" +%zu more", tile.itemIds.size() - j - 1);
				break;
			}
		}
		if (tile.itemIds.empty()) {
			itemsStr = "(empty)";
		}

		long idx = m_clusterTilesListCtrl->InsertItem(i, "", imgIdx);
		m_clusterTilesListCtrl->SetItem(idx, 1, offsetStr);
		m_clusterTilesListCtrl->SetItem(idx, 2, itemsStr);
	}
}

void FloorRuleEditDialog::UpdateClusterControls() {
	bool isCluster = m_clusterRadio ? m_clusterRadio->GetValue() : false;

	if (m_clusterControlsPanel) {
		m_clusterControlsPanel->Show(isCluster);
	}

	// Floor preview and floor spins are not relevant in cluster mode
	if (m_floorPreviewPanelFrom) {
		m_floorPreviewPanelFrom->Show(!isCluster);
	}
	if (m_floorPreviewPanelTo) {
		m_floorPreviewPanelTo->Show(!isCluster);
	}
}

bool FloorRuleEditDialog::EditItemDialog(size_t index) {
	if (index >= m_rule.items.size()) {
		return false;
	}

	AreaDecoration::ItemEntry& entry = m_rule.items[index];

	wxDialog dialog(this, wxID_ANY, "Edit Item", wxDefaultPosition, wxDefaultSize,
		wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER);

	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer* grid = new wxFlexGridSizer(2, 6, 6);
	grid->AddGrowableCol(1, 1);

	wxSpinCtrl* idSpin = nullptr;
	wxButton* browseBtn = nullptr;

	if (!entry.isCompositeEntry()) {
		grid->Add(new wxStaticText(&dialog, wxID_ANY, "Item ID:"), 0, wxALIGN_CENTER_VERTICAL);
		idSpin = new wxSpinCtrl(&dialog, wxID_ANY, i2ws(entry.itemId), wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 0, 65535, entry.itemId);
		grid->Add(idSpin, 0, wxALIGN_CENTER_VERTICAL);

		grid->Add(new wxStaticText(&dialog, wxID_ANY, "Browse:"), 0, wxALIGN_CENTER_VERTICAL);
		browseBtn = new wxButton(&dialog, wxID_ANY, "...", wxDefaultPosition, wxSize(30, -1));
		grid->Add(browseBtn, 0, wxALIGN_CENTER_VERTICAL);
	} else {
		wxString typeLabel = entry.isClusterEntry() ? "Cluster" : "Composite";
		grid->Add(new wxStaticText(&dialog, wxID_ANY, "Type:"), 0, wxALIGN_CENTER_VERTICAL);
		grid->Add(new wxStaticText(&dialog, wxID_ANY, typeLabel), 0, wxALIGN_CENTER_VERTICAL);
	}

	grid->Add(new wxStaticText(&dialog, wxID_ANY, "Weight:"), 0, wxALIGN_CENTER_VERTICAL);
	wxSpinCtrl* weightSpin = new wxSpinCtrl(&dialog, wxID_ANY, i2ws(entry.weight), wxDefaultPosition, wxSize(80, -1),
		wxSP_ARROW_KEYS, 1, 1000, entry.weight);
	grid->Add(weightSpin, 0, wxALIGN_CENTER_VERTICAL);

	wxSpinCtrl* countSpin = nullptr;
	wxSpinCtrl* radiusSpin = nullptr;
	wxSpinCtrl* minDistSpin = nullptr;
	wxButton* changeStructureBtn = nullptr;

	if (entry.isClusterEntry()) {
		grid->Add(new wxStaticText(&dialog, wxID_ANY, "Count:"), 0, wxALIGN_CENTER_VERTICAL);
		countSpin = new wxSpinCtrl(&dialog, wxID_ANY, i2ws(entry.clusterCount), wxDefaultPosition, wxSize(80, -1),
			wxSP_ARROW_KEYS, 1, 100, entry.clusterCount);
		grid->Add(countSpin, 0, wxALIGN_CENTER_VERTICAL);

		grid->Add(new wxStaticText(&dialog, wxID_ANY, "Radius:"), 0, wxALIGN_CENTER_VERTICAL);
		radiusSpin = new wxSpinCtrl(&dialog, wxID_ANY, i2ws(entry.clusterRadius), wxDefaultPosition, wxSize(80, -1),
			wxSP_ARROW_KEYS, 0, 50, entry.clusterRadius);
		grid->Add(radiusSpin, 0, wxALIGN_CENTER_VERTICAL);

		grid->Add(new wxStaticText(&dialog, wxID_ANY, "Min Dist:"), 0, wxALIGN_CENTER_VERTICAL);
		minDistSpin = new wxSpinCtrl(&dialog, wxID_ANY, i2ws(entry.clusterMinDistance), wxDefaultPosition, wxSize(80, -1),
			wxSP_ARROW_KEYS, 0, 50, entry.clusterMinDistance);
		grid->Add(minDistSpin, 0, wxALIGN_CENTER_VERTICAL);
	}

	topSizer->Add(grid, 0, wxALL | wxEXPAND, 10);

	if (entry.isClusterEntry()) {
		wxBoxSizer* clusterBtnSizer = new wxBoxSizer(wxHORIZONTAL);
		changeStructureBtn = new wxButton(&dialog, wxID_ANY, "Change Structure");
		changeStructureBtn->SetToolTip("Paste this cluster on the map to edit its structure");
		clusterBtnSizer->Add(changeStructureBtn, 0);
		topSizer->Add(clusterBtnSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
	}

	wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
	btnSizer->AddStretchSpacer();
	btnSizer->Add(new wxButton(&dialog, wxID_OK, "OK"), 0, wxRIGHT, 5);
	btnSizer->Add(new wxButton(&dialog, wxID_CANCEL, "Cancel"), 0);
	topSizer->Add(btnSizer, 0, wxALL | wxEXPAND, 10);

	dialog.SetSizerAndFit(topSizer);
	dialog.CentreOnParent();

	if (browseBtn && idSpin) {
		browseBtn->Bind(wxEVT_BUTTON, [this, &dialog, idSpin](wxCommandEvent&) {
			FindItemDialog findDialog(&dialog, "Select Item");
			if (findDialog.ShowModal() == wxID_OK) {
				uint16_t itemId = findDialog.getResultID();
				if (itemId > 0) {
					idSpin->SetValue(itemId);
				}
			}
		});
	}

	if (changeStructureBtn) {
		changeStructureBtn->Bind(wxEVT_BUTTON, [this, &dialog, &entry](wxCommandEvent&) {
			PrepareClusterPaste(entry);
			dialog.EndModal(wxID_CANCEL);
		});
	}

	if (dialog.ShowModal() != wxID_OK) {
		return false;
	}

	if (!entry.isCompositeEntry()) {
		uint16_t newId = idSpin ? static_cast<uint16_t>(idSpin->GetValue()) : 0;
		if (newId == 0) {
			wxMessageBox("Item ID cannot be 0", "Error", wxOK | wxICON_ERROR, this);
			return false;
		}
		entry.itemId = newId;
	}

	entry.weight = weightSpin->GetValue();

	if (entry.isClusterEntry()) {
		entry.clusterCount = countSpin ? countSpin->GetValue() : entry.clusterCount;
		entry.clusterRadius = radiusSpin ? radiusSpin->GetValue() : entry.clusterRadius;
		entry.clusterMinDistance = minDistSpin ? minDistSpin->GetValue() : entry.clusterMinDistance;
	}

	UpdateItemsList();
	if (index < static_cast<size_t>(m_itemsListCtrl->GetItemCount())) {
		long idx = static_cast<long>(index);
		m_itemsListCtrl->SetItemState(idx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		m_itemsListCtrl->EnsureVisible(idx);
		if (m_editItemBtn) {
			m_editItemBtn->Enable(true);
		}
	}

	return true;
}

void FloorRuleEditDialog::OnItemsListSelected(wxListEvent& event) {
	long selected = event.GetIndex();
	if (selected < 0 || selected >= static_cast<long>(m_rule.items.size())) {
		return;
	}

	const AreaDecoration::ItemEntry& entry = m_rule.items[selected];
	if (m_newItemWeightSpin) {
		m_newItemWeightSpin->SetValue(entry.weight);
	}

	if (!entry.isCompositeEntry()) {
		if (m_newItemIdSpin) {
			m_newItemIdSpin->SetValue(entry.itemId);
		}
	} else {
		if (m_newItemIdSpin) {
			m_newItemIdSpin->SetValue(0);
		}
		if (entry.isClusterEntry()) {
			if (m_clusterCountSpin) m_clusterCountSpin->SetValue(entry.clusterCount);
			if (m_clusterRadiusSpin) m_clusterRadiusSpin->SetValue(entry.clusterRadius);
			if (m_clusterMinDistanceSpin) m_clusterMinDistanceSpin->SetValue(entry.clusterMinDistance);
		}
	}

	if (m_editItemBtn) {
		m_editItemBtn->Enable(true);
	}
	if (m_previewClusterItemBtn) {
		m_previewClusterItemBtn->Enable(entry.isCompositeEntry());
	}
	if (m_replaceClusterBtn) {
		m_replaceClusterBtn->Enable(entry.isClusterEntry());
	}
}

void FloorRuleEditDialog::OnItemsListActivated(wxListEvent& event) {
	const long selected = event.GetIndex();
	if (selected >= 0 && selected < static_cast<long>(m_rule.items.size())) {
		EditItemDialog(static_cast<size_t>(selected));
	} else {
		OnItemsListSelected(event);
	}
}

void FloorRuleEditDialog::OnAddDoodad(wxCommandEvent& event) {
	long sel = m_doodadListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0) {
		wxMessageBox("Please select a doodad brush first", "Error", wxOK | wxICON_ERROR);
		return;
	}

	int filteredIdx = static_cast<int>(m_doodadListCtrl->GetItemData(sel));
	if (filteredIdx >= 0 && filteredIdx < static_cast<int>(m_filteredDoodads.size())) {
		DoodadBrush* doodad = m_filteredDoodads[filteredIdx];
		if (doodad) {
			AddItemsFromDoodad(doodad);
		}
	}
}

void FloorRuleEditDialog::OnDoodadDoubleClick(wxListEvent& event) {
	int filteredIdx = static_cast<int>(event.GetData());
	if (filteredIdx >= 0 && filteredIdx < static_cast<int>(m_filteredDoodads.size())) {
		DoodadBrush* doodad = m_filteredDoodads[filteredIdx];
		if (doodad) {
			AddItemsFromDoodad(doodad);
		}
	}
}

void FloorRuleEditDialog::OnDoodadSearch(wxCommandEvent& event) {
	FilterDoodads(m_doodadSearchCtrl->GetValue());
}

void FloorRuleEditDialog::OnPrevPage(wxCommandEvent& event) {
	if (m_currentPage > 0) {
		m_currentPage--;
		UpdateDoodadListDisplay();
	}
}

void FloorRuleEditDialog::OnNextPage(wxCommandEvent& event) {
	int totalPages = (static_cast<int>(m_filteredDoodads.size()) + DOODADS_PER_PAGE - 1) / DOODADS_PER_PAGE;
	if (m_currentPage < totalPages - 1) {
		m_currentPage++;
		UpdateDoodadListDisplay();
	}
}

void FloorRuleEditDialog::OnOK(wxCommandEvent& event) {
	if (TransferDataFromWindow()) {
		if (m_onCloseCallback) {
			m_onCloseCallback(true);
		}
		Destroy();
	}
}

void FloorRuleEditDialog::OnCancel(wxCommandEvent& event) {
	if (m_onCloseCallback) {
		m_onCloseCallback(false);
	}
	Destroy();
}

void FloorRuleEditDialog::OnClose(wxCloseEvent& event) {
	if (m_onCloseCallback) {
		m_onCloseCallback(false);
	}
	Destroy();
}

//=============================================================================
// AreaDecorationDialog
//=============================================================================

wxBEGIN_EVENT_TABLE(AreaDecorationDialog, wxDialog)
	EVT_CHOICE(ID_AREA_TYPE_CHOICE, AreaDecorationDialog::OnAreaTypeChanged)
	EVT_BUTTON(ID_SELECT_FROM_MAP, AreaDecorationDialog::OnSelectFromMap)
	EVT_BUTTON(ID_USE_SELECTION, AreaDecorationDialog::OnUseSelection)

	EVT_BUTTON(ID_ADD_RULE, AreaDecorationDialog::OnAddRule)
	EVT_BUTTON(ID_EDIT_RULE, AreaDecorationDialog::OnEditRule)
	EVT_BUTTON(ID_REMOVE_RULE, AreaDecorationDialog::OnRemoveRule)
	EVT_LIST_ITEM_ACTIVATED(ID_RULES_LIST, AreaDecorationDialog::OnRuleDoubleClick)
	EVT_LIST_ITEM_CHECKED(ID_RULES_LIST, AreaDecorationDialog::OnRuleCheckChanged)
	EVT_LIST_ITEM_UNCHECKED(ID_RULES_LIST, AreaDecorationDialog::OnRuleCheckChanged)

	EVT_CHOICE(ID_DISTRIBUTION_CHOICE, AreaDecorationDialog::OnDistributionChanged)

	EVT_BUTTON(ID_PREVIEW, AreaDecorationDialog::OnPreview)
	EVT_BUTTON(ID_REROLL, AreaDecorationDialog::OnReroll)
	EVT_BUTTON(ID_REROLL_APPLY, AreaDecorationDialog::OnRerollApply)
	EVT_BUTTON(ID_APPLY, AreaDecorationDialog::OnApply)
	EVT_BUTTON(ID_REVERT, AreaDecorationDialog::OnRevert)
	EVT_BUTTON(ID_REMOVE_LAST_APPLY, AreaDecorationDialog::OnRemoveLastApply)

	// Preset events
	EVT_CHOICE(ID_PRESET_CHOICE, AreaDecorationDialog::OnPresetSelected)
	EVT_BUTTON(ID_SAVE_PRESET, AreaDecorationDialog::OnSavePreset)
	EVT_BUTTON(ID_REFRESH_PRESET, AreaDecorationDialog::OnRefreshPresets)
	EVT_BUTTON(ID_DELETE_PRESET, AreaDecorationDialog::OnDeletePreset)
	EVT_BUTTON(ID_EXPORT_PRESET, AreaDecorationDialog::OnExportPreset)
	EVT_BUTTON(ID_IMPORT_PRESET, AreaDecorationDialog::OnImportPreset)

	EVT_CLOSE(AreaDecorationDialog::OnClose)
wxEND_EVENT_TABLE()

AreaDecorationDialog::AreaDecorationDialog(wxWindow* parent)
	: wxDialog(parent, wxID_ANY, "Area Decoration", wxDefaultPosition, wxSize(620, 800),
	           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_presetChoice(nullptr)
	, m_presetNameInput(nullptr)
	, m_rulesImageList(nullptr)
{
	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		m_engine = std::make_unique<AreaDecoration::DecorationEngine>(editor);
	}

	// Load presets
	AreaDecoration::PresetManager::getInstance().loadPresets();

	CreateControls();
	UpdatePresetList();
	UpdateUI();
	Centre();
}

AreaDecorationDialog::~AreaDecorationDialog() {
	if (m_engine) {
		m_engine->clearPreview();
	}
	if (m_rulesImageList) {
		delete m_rulesImageList;
		m_rulesImageList = nullptr;
	}
}

void AreaDecorationDialog::SetSeedInputValue(uint64_t seed) {
	if (m_seedInput) {
		m_seedInput->SetValue(wxString::Format("%llu", seed));
	}
}

void AreaDecorationDialog::UpdateEngine() {
	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		// Recreate engine with the current editor
		m_engine = std::make_unique<AreaDecoration::DecorationEngine>(editor);
	} else {
		m_engine.reset();
	}
	UpdateStats();
}

void AreaDecorationDialog::UpdateZCountText(int count, int minZ, int maxZ) {
	if (!m_zCountText) {
		return;
	}
	const bool hasRange = (minZ >= 0 && maxZ >= 0);
	if (count <= 1) {
		if (hasRange) {
			if (minZ == maxZ) {
				m_zCountText->SetLabel(wxString::Format("Z Floors: 1 (Z %d)", minZ));
			} else {
				m_zCountText->SetLabel(wxString::Format("Z Floors: 1 (Z %d-%d)", minZ, maxZ));
			}
		} else {
			m_zCountText->SetLabel("Z Floors: 1");
		}
		return;
	}
	if (hasRange) {
		m_zCountText->SetLabel(wxString::Format("Z Floors: %d (Z %d-%d)", count, minZ, maxZ));
	} else {
		m_zCountText->SetLabel(wxString::Format("Z Floors: %d", count));
	}
}

void AreaDecorationDialog::CreateControls() {
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// Preset management section at the top
	CreatePresetControls(mainSizer);

	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);

	CreateAreaTab(notebook);
	CreateRulesTab(notebook);
	CreateSettingsTab(notebook);
	CreateSeedTab(notebook);

	mainSizer->Add(notebook, 1, wxALL | wxEXPAND, 5);

	CreatePreviewControls(mainSizer);

	mainSizer->Add(CreateButtonSizer(wxCLOSE), 0, wxALL | wxALIGN_RIGHT, 5);

	SetSizer(mainSizer);
}

void AreaDecorationDialog::CreatePresetControls(wxBoxSizer* mainSizer) {
	wxStaticBoxSizer* presetBox = new wxStaticBoxSizer(wxVERTICAL, this, "Preset Configuration");

	// Row 1: Load preset
	wxBoxSizer* loadRow = new wxBoxSizer(wxHORIZONTAL);
	loadRow->Add(new wxStaticText(this, wxID_ANY, "Load Preset:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_presetChoice = new wxChoice(this, ID_PRESET_CHOICE, wxDefaultPosition, wxSize(200, -1));
	loadRow->Add(m_presetChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

	wxButton* refreshBtn = new wxButton(this, ID_REFRESH_PRESET, "Refresh", wxDefaultPosition, wxSize(70, -1));
	refreshBtn->SetToolTip("Reload presets from disk");
	loadRow->Add(refreshBtn, 0, wxRIGHT, 5);

	wxButton* deleteBtn = new wxButton(this, ID_DELETE_PRESET, "Delete", wxDefaultPosition, wxSize(60, -1));
	deleteBtn->SetToolTip("Delete the selected preset");
	loadRow->Add(deleteBtn, 0, wxRIGHT, 5);

	wxButton* importBtn = new wxButton(this, ID_IMPORT_PRESET, "Import...", wxDefaultPosition, wxSize(70, -1));
	importBtn->SetToolTip("Import preset from file");
	loadRow->Add(importBtn, 0);

	presetBox->Add(loadRow, 0, wxALL | wxEXPAND, 5);

	// Row 2: Save preset
	wxBoxSizer* saveRow = new wxBoxSizer(wxHORIZONTAL);
	saveRow->Add(new wxStaticText(this, wxID_ANY, "Save As:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_presetNameInput = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(200, -1));
	m_presetNameInput->SetHint("Enter preset name...");
	saveRow->Add(m_presetNameInput, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

	wxButton* saveBtn = new wxButton(this, ID_SAVE_PRESET, "Save", wxDefaultPosition, wxSize(60, -1));
	saveBtn->SetToolTip("Save current configuration as a preset");
	saveRow->Add(saveBtn, 0, wxRIGHT, 5);

	wxButton* exportBtn = new wxButton(this, ID_EXPORT_PRESET, "Export...", wxDefaultPosition, wxSize(70, -1));
	exportBtn->SetToolTip("Export current configuration to file");
	saveRow->Add(exportBtn, 0);

	presetBox->Add(saveRow, 0, wxALL | wxEXPAND, 5);

	mainSizer->Add(presetBox, 0, wxALL | wxEXPAND, 5);
}

void AreaDecorationDialog::CreateAreaTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	// Area type selection
	wxStaticBoxSizer* typeBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Area Type");

	wxArrayString areaTypes;
	areaTypes.Add("Rectangle");
	areaTypes.Add("Flood Fill");
	areaTypes.Add("Current Selection");

	m_areaTypeChoice = new wxChoice(panel, ID_AREA_TYPE_CHOICE, wxDefaultPosition, wxDefaultSize, areaTypes);
	m_areaTypeChoice->SetSelection(0);
	typeBox->Add(m_areaTypeChoice, 0, wxALL | wxEXPAND, 5);

	sizer->Add(typeBox, 0, wxALL | wxEXPAND, 5);

	// Rectangle coordinates
	wxStaticBoxSizer* rectBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Rectangle Coordinates");

	wxFlexGridSizer* coordGrid = new wxFlexGridSizer(4, 5, 5);

	coordGrid->Add(new wxStaticText(panel, wxID_ANY, "X1:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rectX1Spin = new wxSpinCtrl(panel, wxID_ANY, "0", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	coordGrid->Add(m_rectX1Spin, 0);

	coordGrid->Add(new wxStaticText(panel, wxID_ANY, "Y1:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rectY1Spin = new wxSpinCtrl(panel, wxID_ANY, "0", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	coordGrid->Add(m_rectY1Spin, 0);

	coordGrid->Add(new wxStaticText(panel, wxID_ANY, "X2:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rectX2Spin = new wxSpinCtrl(panel, wxID_ANY, "0", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	coordGrid->Add(m_rectX2Spin, 0);

	coordGrid->Add(new wxStaticText(panel, wxID_ANY, "Y2:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rectY2Spin = new wxSpinCtrl(panel, wxID_ANY, "0", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 65535, 0);
	coordGrid->Add(m_rectY2Spin, 0);

	coordGrid->Add(new wxStaticText(panel, wxID_ANY, "Z1:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rectZ1Spin = new wxSpinCtrl(panel, wxID_ANY, "7", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 15, 7);
	coordGrid->Add(m_rectZ1Spin, 0);

	coordGrid->Add(new wxStaticText(panel, wxID_ANY, "Z2:"), 0, wxALIGN_CENTER_VERTICAL);
	m_rectZ2Spin = new wxSpinCtrl(panel, wxID_ANY, "7", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 15, 7);
	coordGrid->Add(m_rectZ2Spin, 0);

	m_rectX1Spin->Bind(wxEVT_SPINCTRL, &AreaDecorationDialog::OnRectangleCoordsChanged, this);
	m_rectY1Spin->Bind(wxEVT_SPINCTRL, &AreaDecorationDialog::OnRectangleCoordsChanged, this);
	m_rectX2Spin->Bind(wxEVT_SPINCTRL, &AreaDecorationDialog::OnRectangleCoordsChanged, this);
	m_rectY2Spin->Bind(wxEVT_SPINCTRL, &AreaDecorationDialog::OnRectangleCoordsChanged, this);
	m_rectZ1Spin->Bind(wxEVT_SPINCTRL, &AreaDecorationDialog::OnRectangleCoordsChanged, this);
	m_rectZ2Spin->Bind(wxEVT_SPINCTRL, &AreaDecorationDialog::OnRectangleCoordsChanged, this);

	m_rectX1Spin->Bind(wxEVT_TEXT, &AreaDecorationDialog::OnRectangleCoordsChanged, this);
	m_rectY1Spin->Bind(wxEVT_TEXT, &AreaDecorationDialog::OnRectangleCoordsChanged, this);
	m_rectX2Spin->Bind(wxEVT_TEXT, &AreaDecorationDialog::OnRectangleCoordsChanged, this);
	m_rectY2Spin->Bind(wxEVT_TEXT, &AreaDecorationDialog::OnRectangleCoordsChanged, this);
	m_rectZ1Spin->Bind(wxEVT_TEXT, &AreaDecorationDialog::OnRectangleCoordsChanged, this);
	m_rectZ2Spin->Bind(wxEVT_TEXT, &AreaDecorationDialog::OnRectangleCoordsChanged, this);

	rectBox->Add(coordGrid, 0, wxALL, 5);

	m_zCountText = new wxStaticText(panel, wxID_ANY, "Z Floors: 1");
	rectBox->Add(m_zCountText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
	UpdateZCountText(1, m_rectZ1Spin->GetValue(), m_rectZ2Spin->GetValue());

	wxBoxSizer* selectBtnSizer = new wxBoxSizer(wxHORIZONTAL);
	m_selectAreaButton = new wxButton(panel, ID_SELECT_FROM_MAP, "Select from Map...");
	wxButton* useSelectionBtn = new wxButton(panel, ID_USE_SELECTION, "Use Current Selection");
	selectBtnSizer->Add(m_selectAreaButton, 0, wxRIGHT, 5);
	selectBtnSizer->Add(useSelectionBtn, 0);
	rectBox->Add(selectBtnSizer, 0, wxALL, 5);

	// Pick status label for visual feedback
	m_pickStatusText = new wxStaticText(panel, wxID_ANY, "");
	m_pickStatusText->SetForegroundColour(wxColour(0, 128, 0)); // Green color
	rectBox->Add(m_pickStatusText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

	sizer->Add(rectBox, 0, wxALL | wxEXPAND, 5);

	// Area info
	m_areaInfoText = new wxStaticText(panel, wxID_ANY, "No area defined");
	sizer->Add(m_areaInfoText, 0, wxALL, 10);

	panel->SetSizer(sizer);
	notebook->AddPage(panel, "Area");
}

void AreaDecorationDialog::CreateRulesTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	// Rules list
	m_rulesListCtrl = new wxListCtrl(panel, ID_RULES_LIST, wxDefaultPosition, wxSize(-1, 250),
	                                   wxLC_REPORT | wxLC_SINGLE_SEL);
	m_rulesListCtrl->EnableCheckBoxes(true);
	m_rulesImageList = new wxImageList(RULE_ICON_SIZE, RULE_ICON_SIZE, true);
	m_rulesListCtrl->SetImageList(m_rulesImageList, wxIMAGE_LIST_SMALL);

	m_rulesListCtrl->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 120);
	m_rulesListCtrl->InsertColumn(1, "Floor", wxLIST_FORMAT_LEFT, 50);
	m_rulesListCtrl->InsertColumn(2, "Friend", wxLIST_FORMAT_LEFT, 50);
	m_rulesListCtrl->InsertColumn(3, "Floor(s)", wxLIST_FORMAT_LEFT, 80);
	m_rulesListCtrl->InsertColumn(4, "Items", wxLIST_FORMAT_LEFT, 50);
	m_rulesListCtrl->InsertColumn(5, "Density", wxLIST_FORMAT_LEFT, 60);
	m_rulesListCtrl->InsertColumn(6, "Priority", wxLIST_FORMAT_LEFT, 50);
	m_rulesListCtrl->InsertColumn(7, "Border", wxLIST_FORMAT_LEFT, 50);

	sizer->Add(m_rulesListCtrl, 1, wxALL | wxEXPAND, 5);

	wxStaticText* rulesHint = new wxStaticText(panel, wxID_ANY,
		"Note: Multiple rules matching the same floor are applied by priority.\n"
		"To allow overlapping placements on the same tile, set Min Distance to 0.");
	sizer->Add(rulesHint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

	// Buttons
	wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addBtn = new wxButton(panel, ID_ADD_RULE, "Add Rule");
	wxButton* editBtn = new wxButton(panel, ID_EDIT_RULE, "Edit");
	wxButton* removeBtn = new wxButton(panel, ID_REMOVE_RULE, "Remove");

	btnSizer->Add(addBtn, 0, wxRIGHT, 5);
	btnSizer->Add(editBtn, 0, wxRIGHT, 5);
	btnSizer->Add(removeBtn, 0);

	sizer->Add(btnSizer, 0, wxALL, 5);

	panel->SetSizer(sizer);
	notebook->AddPage(panel, "Floor Rules");
}

void AreaDecorationDialog::CreateSettingsTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	// Spacing
	wxStaticBoxSizer* spacingBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Spacing");

	wxFlexGridSizer* spacingGrid = new wxFlexGridSizer(2, 5, 5);

	spacingGrid->Add(new wxStaticText(panel, wxID_ANY, "Min Distance:"), 0, wxALIGN_CENTER_VERTICAL);
	m_minDistanceSpin = new wxSpinCtrl(panel, wxID_ANY, "1", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 20, 1);
	spacingGrid->Add(m_minDistanceSpin, 0);

	spacingGrid->Add(new wxStaticText(panel, wxID_ANY, "Same Item Distance:"), 0, wxALIGN_CENTER_VERTICAL);
	m_sameItemDistanceSpin = new wxSpinCtrl(panel, wxID_ANY, "2", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 20, 2);
	spacingGrid->Add(m_sameItemDistanceSpin, 0);

	spacingBox->Add(spacingGrid, 0, wxALL, 5);

	m_checkDiagonalsCheck = new wxCheckBox(panel, wxID_ANY, "Check Diagonals");
	m_checkDiagonalsCheck->SetValue(true);
	spacingBox->Add(m_checkDiagonalsCheck, 0, wxALL, 5);

	sizer->Add(spacingBox, 0, wxALL | wxEXPAND, 5);

	// Distribution
	wxStaticBoxSizer* distBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Distribution");

	wxArrayString distModes;
	distModes.Add("Pure Random");
	distModes.Add("Clustered");
	distModes.Add("Grid Based");

	wxBoxSizer* modeSizer = new wxBoxSizer(wxHORIZONTAL);
	modeSizer->Add(new wxStaticText(panel, wxID_ANY, "Mode:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_distributionChoice = new wxChoice(panel, ID_DISTRIBUTION_CHOICE, wxDefaultPosition, wxDefaultSize, distModes);
	m_distributionChoice->SetSelection(0);
	modeSizer->Add(m_distributionChoice, 1);
	distBox->Add(modeSizer, 0, wxALL | wxEXPAND, 5);

	// Cluster settings
	wxFlexGridSizer* clusterGrid = new wxFlexGridSizer(2, 5, 5);

	clusterGrid->Add(new wxStaticText(panel, wxID_ANY, "Cluster Strength:"), 0, wxALIGN_CENTER_VERTICAL);
	m_clusterStrengthSlider = new wxSlider(panel, wxID_ANY, 50, 0, 100, wxDefaultPosition, wxSize(150, -1));
	clusterGrid->Add(m_clusterStrengthSlider, 0);

	clusterGrid->Add(new wxStaticText(panel, wxID_ANY, "Cluster Count:"), 0, wxALIGN_CENTER_VERTICAL);
	m_clusterCountSpin = new wxSpinCtrl(panel, wxID_ANY, "3", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1, 20, 3);
	clusterGrid->Add(m_clusterCountSpin, 0);

	distBox->Add(clusterGrid, 0, wxALL, 5);

	// Grid settings
	wxFlexGridSizer* gridGrid = new wxFlexGridSizer(2, 5, 5);

	gridGrid->Add(new wxStaticText(panel, wxID_ANY, "Grid Spacing X:"), 0, wxALIGN_CENTER_VERTICAL);
	m_gridSpacingXSpin = new wxSpinCtrl(panel, wxID_ANY, "3", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1, 20, 3);
	gridGrid->Add(m_gridSpacingXSpin, 0);

	gridGrid->Add(new wxStaticText(panel, wxID_ANY, "Grid Spacing Y:"), 0, wxALIGN_CENTER_VERTICAL);
	m_gridSpacingYSpin = new wxSpinCtrl(panel, wxID_ANY, "3", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1, 20, 3);
	gridGrid->Add(m_gridSpacingYSpin, 0);

	gridGrid->Add(new wxStaticText(panel, wxID_ANY, "Grid Jitter:"), 0, wxALIGN_CENTER_VERTICAL);
	m_gridJitterSpin = new wxSpinCtrl(panel, wxID_ANY, "1", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 5, 1);
	gridGrid->Add(m_gridJitterSpin, 0);

	distBox->Add(gridGrid, 0, wxALL, 5);

	sizer->Add(distBox, 0, wxALL | wxEXPAND, 5);

	// Limits
	wxStaticBoxSizer* limitsBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Limits");

	wxFlexGridSizer* limitsGrid = new wxFlexGridSizer(2, 5, 5);

	limitsGrid->Add(new wxStaticText(panel, wxID_ANY, "Max Items Total:"), 0, wxALIGN_CENTER_VERTICAL);
	m_maxItemsSpin = new wxSpinCtrl(panel, wxID_ANY, "-1", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, -1, 10000, -1);
	limitsGrid->Add(m_maxItemsSpin, 0);

	limitsBox->Add(limitsGrid, 0, wxALL, 5);

	m_skipBlockedCheck = new wxCheckBox(panel, wxID_ANY, "Skip Blocked Tiles");
	m_skipBlockedCheck->SetValue(true);
	limitsBox->Add(m_skipBlockedCheck, 0, wxALL, 5);

	sizer->Add(limitsBox, 0, wxALL | wxEXPAND, 5);

	panel->SetSizer(sizer);
	notebook->AddPage(panel, "Settings");
}

void AreaDecorationDialog::CreateSeedTab(wxNotebook* notebook) {
	wxPanel* panel = new wxPanel(notebook);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* seedBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Random Seed");

	m_useSeedCheck = new wxCheckBox(panel, wxID_ANY, "Use Specific Seed");
	seedBox->Add(m_useSeedCheck, 0, wxALL, 5);

	wxBoxSizer* seedInputSizer = new wxBoxSizer(wxHORIZONTAL);
	seedInputSizer->Add(new wxStaticText(panel, wxID_ANY, "Seed:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_seedInput = new wxTextCtrl(panel, wxID_ANY, "0", wxDefaultPosition, wxSize(150, -1));
	seedInputSizer->Add(m_seedInput, 0);
	seedBox->Add(seedInputSizer, 0, wxALL, 5);

	seedBox->Add(new wxStaticText(panel, wxID_ANY,
		"Using the same seed will produce identical results.\n"
		"Leave unchecked for random seed each preview."),
		0, wxALL, 5);

	sizer->Add(seedBox, 0, wxALL | wxEXPAND, 5);

	panel->SetSizer(sizer);
	notebook->AddPage(panel, "Seed");
}

void AreaDecorationDialog::CreatePreviewControls(wxBoxSizer* mainSizer) {
	wxStaticBoxSizer* previewBox = new wxStaticBoxSizer(wxVERTICAL, this, "Preview");

	wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);

	wxButton* previewBtn = new wxButton(this, ID_PREVIEW, "Apply Changes");
	wxButton* rerollBtn = new wxButton(this, ID_REROLL, "Reroll");
	wxButton* rerollApplyBtn = new wxButton(this, ID_REROLL_APPLY, "Reroll and Apply");
	m_applyBtn = new wxButton(this, ID_APPLY, "Apply to Map");
	wxButton* revertBtn = new wxButton(this, ID_REVERT, "Clear Preview");
	m_removeLastApplyBtn = new wxButton(this, ID_REMOVE_LAST_APPLY, "Remove Last Apply");
	m_removeLastApplyBtn->Enable(false);
	m_applyBtn->Enable(false);

	btnSizer->Add(previewBtn, 0, wxRIGHT, 5);
	btnSizer->Add(rerollBtn, 0, wxRIGHT, 5);
	btnSizer->Add(rerollApplyBtn, 0, wxRIGHT, 5);
	btnSizer->Add(m_applyBtn, 0, wxRIGHT, 5);
	btnSizer->Add(revertBtn, 0, wxRIGHT, 5);
	btnSizer->Add(m_removeLastApplyBtn, 0);

	previewBox->Add(btnSizer, 0, wxALL, 5);

	m_statsText = new wxStaticText(this, wxID_ANY, "No preview generated");
	previewBox->Add(m_statsText, 0, wxALL | wxEXPAND, 5);

	mainSizer->Add(previewBox, 0, wxALL | wxEXPAND, 5);
}

void AreaDecorationDialog::UpdateUI() {
	int areaType = m_areaTypeChoice->GetSelection();

	bool isRect = (areaType == 0);
	m_rectX1Spin->Enable(isRect);
	m_rectY1Spin->Enable(isRect);
	m_rectX2Spin->Enable(isRect);
	m_rectY2Spin->Enable(isRect);
	m_rectZ1Spin->Enable(isRect);
	m_rectZ2Spin->Enable(isRect);
	m_selectAreaButton->Enable(isRect);

	int distMode = m_distributionChoice->GetSelection();
	bool isClustered = (distMode == 1);
	bool isGrid = (distMode == 2);

	m_clusterStrengthSlider->Enable(isClustered);
	m_clusterCountSpin->Enable(isClustered);
	m_gridSpacingXSpin->Enable(isGrid);
	m_gridSpacingYSpin->Enable(isGrid);
	m_gridJitterSpin->Enable(isGrid);

	UpdateRulesList();
}

void AreaDecorationDialog::UpdateRuleButtons() {
	bool enable = !m_editDialogOpen;
	wxWindow* addBtn = FindWindowById(ID_ADD_RULE, this);
	wxWindow* editBtn = FindWindowById(ID_EDIT_RULE, this);
	wxWindow* removeBtn = FindWindowById(ID_REMOVE_RULE, this);
	if (addBtn) addBtn->Enable(enable);
	if (editBtn) editBtn->Enable(enable);
	if (removeBtn) removeBtn->Enable(enable);
}

void AreaDecorationDialog::UpdateRulesList() {
	m_rulesListCtrl->DeleteAllItems();
	if (m_rulesImageList) {
		m_rulesImageList->RemoveAll();
	}

	for (size_t i = 0; i < m_preset.floorRules.size(); ++i) {
		const auto& rule = m_preset.floorRules[i];

		wxString floorStr;
		if (rule.isClusterRule()) {
			Position minP, maxP;
			rule.getClusterBounds(minP, maxP);
			int w = maxP.x - minP.x + 1;
			int h = maxP.y - minP.y + 1;
			floorStr = wxString::Format("Cluster %dx%d", w, h);
		} else if (rule.isRangeRule()) {
			floorStr = wxString::Format("%d - %d", rule.fromFloorId, rule.toFloorId);
		} else {
			floorStr = wxString::Format("%d", rule.floorId);
		}

		uint16_t floorPreviewId = 0;
		if (rule.isClusterRule()) {
			floorPreviewId = rule.getClusterRepresentativeItemId();
		} else {
			floorPreviewId = rule.isRangeRule() ? rule.fromFloorId : rule.floorId;
		}
		uint16_t friendPreviewId = rule.isFriendRange() ? rule.friendFromFloorId : rule.friendFloorId;

		int floorImg = -1;
		int friendImg = -1;
		if (m_rulesImageList) {
			if (floorPreviewId > 0) {
				floorImg = m_rulesImageList->Add(CreatePreviewBitmap(floorPreviewId, RULE_ICON_SIZE));
			}
			if (friendPreviewId > 0) {
				friendImg = m_rulesImageList->Add(CreatePreviewBitmap(friendPreviewId, RULE_ICON_SIZE));
			}
		}

		long idx = m_rulesListCtrl->InsertItem(i, rule.name);
		m_rulesListCtrl->CheckItem(idx, rule.enabled);
		if (floorImg >= 0) {
			m_rulesListCtrl->SetItemColumnImage(idx, 1, floorImg);
		}
		if (friendImg >= 0) {
			m_rulesListCtrl->SetItemColumnImage(idx, 2, friendImg);
		}
		m_rulesListCtrl->SetItem(idx, 3, floorStr);
		if (rule.isClusterRule()) {
			m_rulesListCtrl->SetItem(idx, 4, wxString::Format("%zu tiles", rule.clusterTiles.size()));
		} else {
			m_rulesListCtrl->SetItem(idx, 4, wxString::Format("%zu", rule.items.size()));
		}
		m_rulesListCtrl->SetItem(idx, 5, wxString::Format("%.0f%%", rule.density * 100));
		m_rulesListCtrl->SetItem(idx, 6, wxString::Format("%d", rule.priority));
		m_rulesListCtrl->SetItem(idx, 7, rule.borderItemId > 0 ? wxString::Format("%d", rule.borderItemId) : wxString("-"));
	}
}

void AreaDecorationDialog::UpdateStats() {
	if (!m_engine) {
		m_statsText->SetLabel("No editor available");
		if (m_removeLastApplyBtn) {
			m_removeLastApplyBtn->Enable(false);
		}
		if (m_applyBtn) {
			m_applyBtn->Enable(false);
		}
		return;
	}

	const auto& state = m_engine->getPreviewState();

	if (!state.isValid) {
		m_statsText->SetLabel("No preview generated");
		if (m_removeLastApplyBtn) {
			m_removeLastApplyBtn->Enable(m_engine->hasLastApplied());
		}
		if (m_applyBtn) {
			m_applyBtn->Enable(false);
		}
		return;
	}

	wxString stats;
	stats << "Items placed: " << state.totalItemsPlaced << "\n";
	stats << "Seed: " << state.seed;

	m_statsText->SetLabel(stats);
	if (m_removeLastApplyBtn) {
		m_removeLastApplyBtn->Enable(m_engine->hasLastApplied());
	}
	if (m_applyBtn) {
		m_applyBtn->Enable(true);
	}
}

void AreaDecorationDialog::BuildPresetFromUI() {
	m_preset.spacing.minDistance = m_minDistanceSpin->GetValue();
	m_preset.spacing.minSameItemDistance = m_sameItemDistanceSpin->GetValue();
	m_preset.spacing.checkDiagonals = m_checkDiagonalsCheck->GetValue();

	int distMode = m_distributionChoice->GetSelection();
	m_preset.distribution.mode = static_cast<AreaDecoration::DistributionMode>(distMode);
	m_preset.distribution.clusterStrength = m_clusterStrengthSlider->GetValue() / 100.0f;
	m_preset.distribution.clusterCount = m_clusterCountSpin->GetValue();
	m_preset.distribution.gridSpacingX = m_gridSpacingXSpin->GetValue();
	m_preset.distribution.gridSpacingY = m_gridSpacingYSpin->GetValue();
	m_preset.distribution.gridJitter = m_gridJitterSpin->GetValue();

	m_preset.maxItemsTotal = m_maxItemsSpin->GetValue();
	m_preset.skipBlockedTiles = m_skipBlockedCheck->GetValue();

	if (m_useSeedCheck->GetValue()) {
		try {
			m_preset.defaultSeed = std::stoull(m_seedInput->GetValue().ToStdString());
		} catch (...) {
			m_preset.defaultSeed = 0;
		}
	} else {
		m_preset.defaultSeed = 0;
	}

	BuildAreaFromUI();
	m_preset.area = m_area;
	m_preset.hasArea = true;
}

void AreaDecorationDialog::BuildAreaFromUI() {
	int areaType = m_areaTypeChoice->GetSelection();

	m_area.rectMin = Position(m_rectX1Spin->GetValue(), m_rectY1Spin->GetValue(), m_rectZ1Spin->GetValue());
	m_area.rectMax = Position(m_rectX2Spin->GetValue(), m_rectY2Spin->GetValue(), m_rectZ2Spin->GetValue());

	switch (areaType) {
		case 0: // Rectangle
			m_area.type = AreaDecoration::AreaDefinition::Type::Rectangle;
			break;
		case 1: // Flood Fill - need to implement selection
			m_area.type = AreaDecoration::AreaDefinition::Type::FloodFill;
			break;
		case 2: // Selection
			m_area.type = AreaDecoration::AreaDefinition::Type::Selection;
			break;
	}
}

void AreaDecorationDialog::OnAreaTypeChanged(wxCommandEvent& event) {
	int areaType = m_areaTypeChoice ? m_areaTypeChoice->GetSelection() : 0;
	if (areaType == 2) {
		Editor* editor = g_gui.GetCurrentEditor();
		if (editor) {
			const Selection& selection = editor->selection;
			if (selection.size() > 0) {
				Position minPos(INT_MAX, INT_MAX, INT_MAX);
				Position maxPos(INT_MIN, INT_MIN, INT_MIN);
				std::unordered_set<int> zLevels;
				const auto& tiles = selection.getTiles();
				for (Tile* tile : tiles) {
					const Position& pos = tile->getPosition();
					minPos.x = std::min(minPos.x, pos.x);
					minPos.y = std::min(minPos.y, pos.y);
					minPos.z = std::min(minPos.z, pos.z);
					maxPos.x = std::max(maxPos.x, pos.x);
					maxPos.y = std::max(maxPos.y, pos.y);
					maxPos.z = std::max(maxPos.z, pos.z);
					zLevels.insert(pos.z);
				}
				UpdateZCountText(static_cast<int>(zLevels.size()), minPos.z, maxPos.z);
			} else {
				UpdateZCountText(1);
			}
		} else {
			UpdateZCountText(1);
		}
	} else {
		int z1 = m_rectZ1Spin ? m_rectZ1Spin->GetValue() : 7;
		int z2 = m_rectZ2Spin ? m_rectZ2Spin->GetValue() : 7;
		int minZ = std::min(z1, z2);
		int maxZ = std::max(z1, z2);
		int zCount = maxZ - minZ + 1;
		UpdateZCountText(zCount, minZ, maxZ);
	}
	UpdateUI();
}

void AreaDecorationDialog::OnRectangleCoordsChanged(wxCommandEvent& event) {
	if (m_areaTypeChoice && m_areaTypeChoice->GetSelection() != 0) {
		m_areaTypeChoice->SetSelection(0);
		UpdateUI();
	}

	int z1 = m_rectZ1Spin ? m_rectZ1Spin->GetValue() : 7;
	int z2 = m_rectZ2Spin ? m_rectZ2Spin->GetValue() : 7;
	int minZ = std::min(z1, z2);
	int maxZ = std::max(z1, z2);
	int zCount = maxZ - minZ + 1;

	if (m_areaInfoText) {
		if (minZ == maxZ) {
			m_areaInfoText->SetLabel(wxString::Format("Rectangle: (%d,%d) to (%d,%d) Z:%d",
				m_rectX1Spin->GetValue(),
				m_rectY1Spin->GetValue(),
				m_rectX2Spin->GetValue(),
				m_rectY2Spin->GetValue(),
				minZ));
		} else {
			m_areaInfoText->SetLabel(wxString::Format("Rectangle: (%d,%d) to (%d,%d) Z:%d-%d (%d floors)",
				m_rectX1Spin->GetValue(),
				m_rectY1Spin->GetValue(),
				m_rectX2Spin->GetValue(),
				m_rectY2Spin->GetValue(),
				minZ, maxZ, zCount));
		}
	}

	UpdateZCountText(zCount, minZ, maxZ);
}

void AreaDecorationDialog::OnSelectFromMap(wxCommandEvent& event) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		wxMessageBox("No editor available", "Error", wxOK | wxICON_ERROR);
		return;
	}

	g_gui.SetSelectionMode();

	// Update status to show we're waiting for first click
	if (m_pickStatusText) {
		m_pickStatusText->SetForegroundColour(wxColour(0, 100, 200)); // Blue
		m_pickStatusText->SetLabel("Waiting for first corner click...");
	}

	g_gui.BeginRectanglePick(
		// On complete (second click)
		[this](const Position& first, const Position& second) {
			int minX = std::min(first.x, second.x);
			int minY = std::min(first.y, second.y);
			int maxX = std::max(first.x, second.x);
			int maxY = std::max(first.y, second.y);
			int minZ = std::min(first.z, second.z);
			int maxZ = std::max(first.z, second.z);

			m_rectX1Spin->SetValue(minX);
			m_rectY1Spin->SetValue(minY);
			m_rectX2Spin->SetValue(maxX);
			m_rectY2Spin->SetValue(maxY);
			m_rectZ1Spin->SetValue(minZ);
			m_rectZ2Spin->SetValue(maxZ);

			m_areaTypeChoice->SetSelection(0);

			int zCount = std::abs(maxZ - minZ) + 1;
			if (m_areaInfoText) {
				if (minZ == maxZ) {
					m_areaInfoText->SetLabel(wxString::Format("Rectangle: (%d,%d) to (%d,%d) Z:%d",
						minX, minY, maxX, maxY, minZ));
				} else {
					m_areaInfoText->SetLabel(wxString::Format("Rectangle: (%d,%d) to (%d,%d) Z:%d-%d (%d floors)",
						minX, minY, maxX, maxY, minZ, maxZ, zCount));
				}
			}

			// Update pick status with success
			if (m_pickStatusText) {
				m_pickStatusText->SetForegroundColour(wxColour(0, 128, 0)); // Green
				m_pickStatusText->SetLabel(wxString::Format("Selection complete: (%d,%d,Z%d) to (%d,%d,Z%d)",
					first.x, first.y, first.z, second.x, second.y, second.z));
			}

			UpdateZCountText(zCount, minZ, maxZ);
			UpdateUI();
		},
		// On cancel
		[this]() {
			if (m_areaInfoText) {
				m_areaInfoText->SetLabel("Rectangle selection cancelled");
			}
			if (m_pickStatusText) {
				m_pickStatusText->SetForegroundColour(wxColour(180, 0, 0)); // Red
				m_pickStatusText->SetLabel("Selection cancelled");
			}
		},
		// On first click
		[this](const Position& first) {
			if (m_pickStatusText) {
				m_pickStatusText->SetForegroundColour(wxColour(200, 130, 0)); // Orange
				m_pickStatusText->SetLabel(wxString::Format("First corner: (%d, %d, Z%d) - Click second corner...",
					first.x, first.y, first.z));
			}
		}
	);
}

void AreaDecorationDialog::OnUseSelection(wxCommandEvent& event) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		wxMessageBox("No editor available", "Error", wxOK | wxICON_ERROR);
		return;
	}

	const Selection& selection = editor->selection;
	if (selection.size() == 0) {
		wxMessageBox("No tiles selected. Select some tiles first.", "Error", wxOK | wxICON_ERROR);
		return;
	}

	Position minPos(INT_MAX, INT_MAX, INT_MAX);
	Position maxPos(INT_MIN, INT_MIN, INT_MIN);
	std::unordered_set<int> zLevels;

	const auto& tiles = selection.getTiles();
	for (Tile* tile : tiles) {
		const Position& pos = tile->getPosition();
		minPos.x = std::min(minPos.x, pos.x);
		minPos.y = std::min(minPos.y, pos.y);
		minPos.z = std::min(minPos.z, pos.z);
		maxPos.x = std::max(maxPos.x, pos.x);
		maxPos.y = std::max(maxPos.y, pos.y);
		maxPos.z = std::max(maxPos.z, pos.z);
		zLevels.insert(pos.z);
	}

	m_rectX1Spin->SetValue(minPos.x);
	m_rectY1Spin->SetValue(minPos.y);
	m_rectX2Spin->SetValue(maxPos.x);
	m_rectY2Spin->SetValue(maxPos.y);
	m_rectZ1Spin->SetValue(minPos.z);
	m_rectZ2Spin->SetValue(maxPos.z);

	m_areaTypeChoice->SetSelection(2); // Selection type

	int zCount = static_cast<int>(zLevels.size());
	if (minPos.z == maxPos.z) {
		m_areaInfoText->SetLabel(wxString::Format("Selection: %zu tiles (%d,%d) to (%d,%d) Z:%d",
			selection.size(), minPos.x, minPos.y, maxPos.x, maxPos.y, minPos.z));
	} else {
		m_areaInfoText->SetLabel(wxString::Format("Selection: %zu tiles (%d,%d) to (%d,%d) Z:%d-%d (%d floors)",
			selection.size(), minPos.x, minPos.y, maxPos.x, maxPos.y, minPos.z, maxPos.z, zCount));
	}

	UpdateZCountText(zCount, minPos.z, maxPos.z);

	UpdateUI();
}

void AreaDecorationDialog::OnAddRule(wxCommandEvent& event) {
	// Create a new rule and add it to the list first
	AreaDecoration::FloorRule newRule;
	newRule.name = "New Rule";
	newRule.floorId = 0;
	newRule.density = 1.0f;

	m_preset.floorRules.push_back(newRule);
	size_t ruleIndex = m_preset.floorRules.size() - 1;
	UpdateRulesList();

	// Disable rule buttons while edit dialog is open
	m_editDialogOpen = true;
	UpdateRuleButtons();

	// Open non-modal dialog for editing
	FloorRuleEditDialog* dialog = new FloorRuleEditDialog(
		g_gui.root,  // Use root window to allow palette interaction
		m_preset.floorRules[ruleIndex],
		[this, ruleIndex](bool accepted) {
			if (!accepted) {
				// User cancelled, remove the rule
				if (ruleIndex < m_preset.floorRules.size()) {
					m_preset.floorRules.erase(m_preset.floorRules.begin() + ruleIndex);
				}
			}
			m_editDialogOpen = false;
			UpdateRuleButtons();
			UpdateRulesList();
		}
	);
	dialog->Show();
}

void AreaDecorationDialog::OnEditRule(wxCommandEvent& event) {
	long selected = m_rulesListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected < 0 || selected >= static_cast<long>(m_preset.floorRules.size())) {
		wxMessageBox("Select a rule to edit", "Error", wxOK | wxICON_ERROR);
		return;
	}

	size_t ruleIndex = static_cast<size_t>(selected);

	// Store a backup copy in case user cancels
	AreaDecoration::FloorRule backupRule = m_preset.floorRules[ruleIndex];

	// Disable rule buttons while edit dialog is open
	m_editDialogOpen = true;
	UpdateRuleButtons();

	FloorRuleEditDialog* dialog = new FloorRuleEditDialog(
		g_gui.root,  // Use root window to allow palette interaction
		m_preset.floorRules[ruleIndex],
		[this, ruleIndex, backupRule](bool accepted) {
			if (!accepted) {
				// User cancelled, restore backup
				if (ruleIndex < m_preset.floorRules.size()) {
					m_preset.floorRules[ruleIndex] = backupRule;
				}
			}
			m_editDialogOpen = false;
			UpdateRuleButtons();
			UpdateRulesList();
		}
	);
	dialog->Show();
}

void AreaDecorationDialog::OnRemoveRule(wxCommandEvent& event) {
	long selected = m_rulesListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected >= 0 && selected < static_cast<long>(m_preset.floorRules.size())) {
		m_preset.floorRules.erase(m_preset.floorRules.begin() + selected);
		UpdateRulesList();
	}
}

void AreaDecorationDialog::OnRuleDoubleClick(wxListEvent& event) {
	long idx = event.GetIndex();
	if (idx >= 0 && idx < static_cast<long>(m_preset.floorRules.size())) {
		size_t ruleIndex = static_cast<size_t>(idx);

		// Store a backup copy in case user cancels
		AreaDecoration::FloorRule backupRule = m_preset.floorRules[ruleIndex];

		// Disable rule buttons while edit dialog is open
		m_editDialogOpen = true;
		UpdateRuleButtons();

		FloorRuleEditDialog* dialog = new FloorRuleEditDialog(
			g_gui.root,  // Use root window to allow palette interaction
			m_preset.floorRules[ruleIndex],
			[this, ruleIndex, backupRule](bool accepted) {
				if (!accepted) {
					// User cancelled, restore backup
					if (ruleIndex < m_preset.floorRules.size()) {
						m_preset.floorRules[ruleIndex] = backupRule;
					}
				}
				m_editDialogOpen = false;
				UpdateRuleButtons();
				UpdateRulesList();
			}
		);
		dialog->Show();
	}
}

void AreaDecorationDialog::OnRuleCheckChanged(wxListEvent& event) {
	long idx = event.GetIndex();
	if (idx < 0 || idx >= static_cast<long>(m_preset.floorRules.size())) {
		return;
	}

	bool checked = m_rulesListCtrl->IsItemChecked(idx);
	m_preset.floorRules[idx].enabled = checked;
}

void AreaDecorationDialog::OnDistributionChanged(wxCommandEvent& event) {
	UpdateUI();
}

void AreaDecorationDialog::OnPreview(wxCommandEvent& event) {
	if (!m_engine) {
		wxMessageBox("No editor available", "Error", wxOK | wxICON_ERROR);
		return;
	}

	if (m_preset.floorRules.empty()) {
		wxMessageBox("Add at least one floor rule first", "Error", wxOK | wxICON_ERROR);
		return;
	}

	BuildPresetFromUI();
	BuildAreaFromUI();

	m_engine->setArea(m_area);
	m_engine->setPreset(m_preset);

	uint64_t seed = 0;
	if (m_useSeedCheck->GetValue()) {
		try {
			seed = std::stoull(m_seedInput->GetValue().ToStdString());
		} catch (...) {
			seed = 0;
		}
	}

	if (!m_engine->generatePreview(seed)) {
		wxMessageBox("Failed to generate preview:\n" + m_engine->getLastError(),
		             "Error", wxOK | wxICON_ERROR);
		UpdateStats();
		return;
	}

	UpdateStats();
	g_gui.RefreshView();
}

void AreaDecorationDialog::OnReroll(wxCommandEvent& event) {
	if (!m_engine) return;

	if (!m_engine->getPreviewState().isValid) {
		BuildPresetFromUI();
		BuildAreaFromUI();
		m_engine->setArea(m_area);
		m_engine->setPreset(m_preset);
	}

	if (m_engine->generatePreview(0)) {
		m_seedInput->SetValue(wxString::Format("%llu", m_engine->getPreviewState().seed));
		UpdateStats();
		g_gui.RefreshView();
	} else {
		wxMessageBox("Failed to reroll preview:\n" + m_engine->getLastError(),
		             "Error", wxOK | wxICON_ERROR);
	}
}

void AreaDecorationDialog::OnRerollApply(wxCommandEvent& event) {
	if (!m_engine) return;

	bool removedLast = false;
	if (m_engine->hasLastApplied()) {
		if (!m_engine->removeLastApplied()) {
			wxMessageBox("Failed to remove last apply:\n" + m_engine->getLastError(),
			             "Error", wxOK | wxICON_ERROR);
			if (m_removeLastApplyBtn) {
				m_removeLastApplyBtn->Enable(m_engine->hasLastApplied());
			}
			return;
		}
		removedLast = true;
		if (m_removeLastApplyBtn) {
			m_removeLastApplyBtn->Enable(false);
		}
	}

	if (!m_engine->getPreviewState().isValid) {
		BuildPresetFromUI();
		BuildAreaFromUI();
		m_engine->setArea(m_area);
		m_engine->setPreset(m_preset);
	}

	if (!m_engine->generatePreview(0)) {
		wxMessageBox("Failed to reroll preview:\n" + m_engine->getLastError(),
		             "Error", wxOK | wxICON_ERROR);
		UpdateStats();
		if (removedLast) {
			g_gui.RefreshView();
		}
		return;
	}

	SetSeedInputValue(m_engine->getPreviewState().seed);
	OnApply(event);
}

void AreaDecorationDialog::OnApply(wxCommandEvent& event) {
	if (!m_engine) return;

	if (!m_engine->getPreviewState().isValid) {
		wxMessageBox("Apply changes first to generate a preview.", "Area Decoration",
		             wxOK | wxICON_INFORMATION);
		return;
	}

	const int kMaxAutoBatches = 20;
	int batchesApplied = 0;
	int totalItemsApplied = 0;

	while (true) {
		const bool wasCapped = m_engine->wasPreviewCapped();
		const int batchCount = static_cast<int>(m_engine->getPreviewState().items.size());

		if (m_engine->applyPreview()) {
			totalItemsApplied += batchCount;
			batchesApplied++;
		} else {
			wxMessageBox("Failed to apply preview:\n" + m_engine->getLastError(),
			             "Error", wxOK | wxICON_ERROR);
			return;
		}

		if (!wasCapped || batchesApplied >= kMaxAutoBatches) {
			break;
		}

		if (!m_engine->generatePreview(0)) {
			break;
		}
	}

	if (batchesApplied > 1) {
		m_statsText->SetLabel(wxString::Format("Applied in %d batches (%d items).", batchesApplied, totalItemsApplied));
	} else {
		m_statsText->SetLabel("Preview applied successfully!");
	}
	if (m_removeLastApplyBtn) {
		m_removeLastApplyBtn->Enable(m_engine->hasLastApplied());
	}
	g_gui.RefreshView();
}

void AreaDecorationDialog::OnRevert(wxCommandEvent& event) {
	if (!m_engine) return;

	m_engine->clearPreview();
	m_statsText->SetLabel("Preview reverted");
	g_gui.RefreshView();
	if (m_applyBtn) {
		m_applyBtn->Enable(false);
	}
}

void AreaDecorationDialog::OnRemoveLastApply(wxCommandEvent& event) {
	if (!m_engine) return;

	if (m_engine->removeLastApplied()) {
		m_statsText->SetLabel("Last apply removed");
		if (m_removeLastApplyBtn) {
			m_removeLastApplyBtn->Enable(m_engine->hasLastApplied());
		}
		g_gui.RefreshView();
	} else {
		wxMessageBox("Failed to remove last apply:\n" + m_engine->getLastError(),
		             "Error", wxOK | wxICON_ERROR);
	}
}

void AreaDecorationDialog::OnClose(wxCloseEvent& event) {
	g_gui.CancelRectanglePick();
	if (m_engine) {
		m_engine->clearPreview();
		g_gui.RefreshView();
	}
	// Hide instead of destroy to preserve state
	Hide();
	// Veto the event to prevent destruction
	event.Veto();
}

void AreaDecorationDialog::UpdatePresetList() {
	if (!m_presetChoice) return;

	m_presetChoice->Clear();
	m_presetChoice->Append("(None - Custom)");

	auto& manager = AreaDecoration::PresetManager::getInstance();
	std::vector<std::string> names = manager.getPresetNames();

	for (const auto& name : names) {
		m_presetChoice->Append(wxstr(name));
	}

	m_presetChoice->SetSelection(0);
}

void AreaDecorationDialog::OnRefreshPresets(wxCommandEvent& event) {
	if (!m_presetChoice) return;

	wxString currentSelection = m_presetChoice->GetStringSelection();
	auto& manager = AreaDecoration::PresetManager::getInstance();
	manager.loadPresets();

	UpdatePresetList();

	if (!currentSelection.IsEmpty()) {
		int idx = m_presetChoice->FindString(currentSelection);
		if (idx != wxNOT_FOUND) {
			m_presetChoice->SetSelection(idx);
			if (idx > 0) {
				const AreaDecoration::DecorationPreset* preset =
					manager.getPreset(currentSelection.ToStdString());
				if (preset) {
					LoadPresetToUI(*preset);
				}
			}
		}
	}
}

void AreaDecorationDialog::LoadPresetToUI(const AreaDecoration::DecorationPreset& preset) {
	// Clear existing rules and load from preset
	m_preset = preset;

	// Load spacing settings
	m_minDistanceSpin->SetValue(preset.spacing.minDistance);
	m_sameItemDistanceSpin->SetValue(preset.spacing.minSameItemDistance);
	m_checkDiagonalsCheck->SetValue(preset.spacing.checkDiagonals);

	// Load distribution settings
	m_distributionChoice->SetSelection(static_cast<int>(preset.distribution.mode));
	m_clusterStrengthSlider->SetValue(static_cast<int>(preset.distribution.clusterStrength * 100));
	m_clusterCountSpin->SetValue(preset.distribution.clusterCount);
	m_gridSpacingXSpin->SetValue(preset.distribution.gridSpacingX);
	m_gridSpacingYSpin->SetValue(preset.distribution.gridSpacingY);
	m_gridJitterSpin->SetValue(preset.distribution.gridJitter);

	// Load limits
	m_maxItemsSpin->SetValue(preset.maxItemsTotal);
	m_skipBlockedCheck->SetValue(preset.skipBlockedTiles);

	// Load seed settings
	if (preset.defaultSeed != 0) {
		m_useSeedCheck->SetValue(true);
		m_seedInput->SetValue(wxString::Format("%llu", preset.defaultSeed));
	} else {
		m_useSeedCheck->SetValue(false);
		m_seedInput->SetValue("0");
	}

	// Load area settings (if present)
	if (preset.hasArea) {
		m_area = preset.area;
		m_rectX1Spin->SetValue(m_area.rectMin.x);
		m_rectY1Spin->SetValue(m_area.rectMin.y);
		m_rectX2Spin->SetValue(m_area.rectMax.x);
		m_rectY2Spin->SetValue(m_area.rectMax.y);
		m_rectZ1Spin->SetValue(m_area.rectMin.z);
		m_rectZ2Spin->SetValue(m_area.rectMax.z);

		int areaType = static_cast<int>(m_area.type);
		if (areaType < 0 || areaType > 2) {
			areaType = 0;
		}
		m_areaTypeChoice->SetSelection(areaType);

		if (areaType == 0) {
			wxCommandEvent dummy;
			OnRectangleCoordsChanged(dummy);
		} else if (areaType == 1) {
			if (m_areaInfoText) {
				m_areaInfoText->SetLabel(wxString::Format("Flood Fill: (%d,%d,Z%d)",
					m_area.floodOrigin.x, m_area.floodOrigin.y, m_area.floodOrigin.z));
			}
			UpdateZCountText(1, m_area.floodOrigin.z, m_area.floodOrigin.z);
		} else {
			int minZ = std::min(m_area.rectMin.z, m_area.rectMax.z);
			int maxZ = std::max(m_area.rectMin.z, m_area.rectMax.z);
			int zCount = maxZ - minZ + 1;
			if (m_areaInfoText) {
				if (minZ == maxZ) {
					m_areaInfoText->SetLabel(wxString::Format("Selection: (%d,%d) to (%d,%d) Z:%d",
						m_area.rectMin.x, m_area.rectMin.y, m_area.rectMax.x, m_area.rectMax.y, minZ));
				} else {
					m_areaInfoText->SetLabel(wxString::Format("Selection: (%d,%d) to (%d,%d) Z:%d-%d (%d floors)",
						m_area.rectMin.x, m_area.rectMin.y, m_area.rectMax.x, m_area.rectMax.y, minZ, maxZ, zCount));
				}
			}
			UpdateZCountText(zCount, minZ, maxZ);
		}
	} else {
		BuildAreaFromUI();
		m_preset.area = m_area;
	}

	// Update preset name input
	m_presetNameInput->SetValue(wxstr(preset.name));

	// Update UI
	UpdateRulesList();
	UpdateUI();
}

void AreaDecorationDialog::OnPresetSelected(wxCommandEvent& event) {
	int sel = m_presetChoice->GetSelection();
	if (sel <= 0) {
		// "(None - Custom)" selected, don't load anything
		return;
	}

	wxString presetName = m_presetChoice->GetString(sel);
	auto& manager = AreaDecoration::PresetManager::getInstance();
	const AreaDecoration::DecorationPreset* preset = manager.getPreset(presetName.ToStdString());

	if (preset) {
		LoadPresetToUI(*preset);
	}
}

void AreaDecorationDialog::OnSavePreset(wxCommandEvent& event) {
	wxString name = m_presetNameInput->GetValue().Trim().Trim(false);
	if (name.IsEmpty()) {
		wxMessageBox("Please enter a preset name", "Error", wxOK | wxICON_ERROR);
		return;
	}

	// Build current settings into preset
	BuildPresetFromUI();
	m_preset.name = name.ToStdString();

	auto& manager = AreaDecoration::PresetManager::getInstance();

	// Check if preset already exists
	if (manager.getPreset(name.ToStdString()) != nullptr) {
		int result = wxMessageBox(
			wxString::Format("Preset '%s' already exists. Overwrite?", name),
			"Confirm Overwrite",
			wxYES_NO | wxICON_QUESTION
		);
		if (result != wxYES) {
			return;
		}
		// Remove old preset first
		manager.removePreset(name.ToStdString());
	}

	if (manager.addPreset(m_preset)) {
		if (manager.savePresets()) {
			wxMessageBox(wxString::Format("Preset '%s' saved successfully", name), "Success", wxOK | wxICON_INFORMATION);
			UpdatePresetList();

			// Select the newly saved preset
			int idx = m_presetChoice->FindString(name);
			if (idx != wxNOT_FOUND) {
				m_presetChoice->SetSelection(idx);
			}
		} else {
			wxMessageBox("Failed to save presets to disk", "Error", wxOK | wxICON_ERROR);
		}
	} else {
		wxMessageBox("Failed to add preset", "Error", wxOK | wxICON_ERROR);
	}
}

void AreaDecorationDialog::OnDeletePreset(wxCommandEvent& event) {
	int sel = m_presetChoice->GetSelection();
	if (sel <= 0) {
		wxMessageBox("Select a preset to delete", "Error", wxOK | wxICON_ERROR);
		return;
	}

	wxString presetName = m_presetChoice->GetString(sel);

	int result = wxMessageBox(
		wxString::Format("Are you sure you want to delete preset '%s'?", presetName),
		"Confirm Delete",
		wxYES_NO | wxICON_WARNING
	);

	if (result != wxYES) {
		return;
	}

	auto& manager = AreaDecoration::PresetManager::getInstance();
	if (manager.removePreset(presetName.ToStdString())) {
		if (manager.savePresets()) {
			wxMessageBox(wxString::Format("Preset '%s' deleted", presetName), "Success", wxOK | wxICON_INFORMATION);
			UpdatePresetList();
		} else {
			wxMessageBox("Failed to save changes to disk", "Error", wxOK | wxICON_ERROR);
		}
	} else {
		wxMessageBox("Failed to delete preset", "Error", wxOK | wxICON_ERROR);
	}
}

void AreaDecorationDialog::OnExportPreset(wxCommandEvent& event) {
	wxString name = m_presetNameInput->GetValue().Trim().Trim(false);
	if (name.IsEmpty()) {
		name = "decoration_preset";
	}

	wxFileDialog saveDialog(
		this,
		"Export Preset",
		"",
		name + ".xml",
		"XML files (*.xml)|*.xml|All files (*.*)|*.*",
		wxFD_SAVE | wxFD_OVERWRITE_PROMPT
	);

	if (saveDialog.ShowModal() == wxID_CANCEL) {
		return;
	}

	// Build current settings
	BuildPresetFromUI();
	m_preset.name = name.ToStdString();

	if (m_preset.saveToFile(saveDialog.GetPath().ToStdString())) {
		wxMessageBox("Preset exported successfully", "Success", wxOK | wxICON_INFORMATION);
	} else {
		wxMessageBox("Failed to export preset", "Error", wxOK | wxICON_ERROR);
	}
}

void AreaDecorationDialog::OnImportPreset(wxCommandEvent& event) {
	wxFileDialog openDialog(
		this,
		"Import Preset",
		"",
		"",
		"XML files (*.xml)|*.xml|All files (*.*)|*.*",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST
	);

	if (openDialog.ShowModal() == wxID_CANCEL) {
		return;
	}

	AreaDecoration::DecorationPreset importedPreset;
	if (importedPreset.loadFromFile(openDialog.GetPath().ToStdString())) {
		// Load into UI
		LoadPresetToUI(importedPreset);

		// Ask if user wants to save to presets list
		int result = wxMessageBox(
			wxString::Format("Preset '%s' imported. Would you like to save it to your presets list?", wxstr(importedPreset.name)),
			"Save Imported Preset?",
			wxYES_NO | wxICON_QUESTION
		);

		if (result == wxYES) {
			auto& manager = AreaDecoration::PresetManager::getInstance();

			// Check if already exists
			if (manager.getPreset(importedPreset.name) != nullptr) {
				int overwrite = wxMessageBox(
					wxString::Format("Preset '%s' already exists. Overwrite?", wxstr(importedPreset.name)),
					"Confirm Overwrite",
					wxYES_NO | wxICON_QUESTION
				);
				if (overwrite == wxYES) {
					manager.removePreset(importedPreset.name);
				} else {
					return;
				}
			}

			if (manager.addPreset(importedPreset) && manager.savePresets()) {
				wxMessageBox("Preset saved to your presets list", "Success", wxOK | wxICON_INFORMATION);
				UpdatePresetList();

				int idx = m_presetChoice->FindString(wxstr(importedPreset.name));
				if (idx != wxNOT_FOUND) {
					m_presetChoice->SetSelection(idx);
				}
			}
		}
	} else {
		wxMessageBox("Failed to import preset. The file may be invalid or corrupted.", "Error", wxOK | wxICON_ERROR);
	}
}

void AreaDecorationDialog::AddRuleFromExternal(const AreaDecoration::FloorRule& rule) {
	m_preset.floorRules.push_back(rule);
	UpdateRulesList();
	UpdateUI();
}
