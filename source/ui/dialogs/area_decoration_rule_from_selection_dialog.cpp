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
#include "ui/dialogs/area_decoration_rule_from_selection_dialog.h"
#include "ui/gui.h"
#include "rendering/core/graphics.h"
#include "item_definitions/core/item_definition_store.h"
#include "editor/editor.h"
#include "map/map.h"
#include "map/tile.h"
#include "editor/selection.h"
#include "ui/dialogs/cluster_preview_window.h"
#include <wx/dcbuffer.h>
#include <algorithm>
#include <climits>
#include <set>

namespace {
	enum {
		ID_RULE_NAME = wxID_HIGHEST + 7000,
		ID_LAYER_ROLE_BASE,      // Base ID for layer role radio buttons
		// Reserve 8 IDs for radio buttons (4 layers x 2 radios each)
		ID_INSTANCE_COUNT = ID_LAYER_ROLE_BASE + 10,
		ID_MIN_DISTANCE,
		ID_DENSITY,
		ID_PREVIEW_CLUSTER,
		ID_RULE_OK,
		ID_RULE_CANCEL
	};

	const int ITEM_ICON_SIZE = 32;
}

//=============================================================================
// ZOrderLayerPanel
//=============================================================================

wxBEGIN_EVENT_TABLE(ZOrderLayerPanel, wxPanel)
	EVT_RADIOBUTTON(wxID_ANY, ZOrderLayerPanel::OnRoleChanged)
wxEND_EVENT_TABLE()

ZOrderLayerPanel::ZOrderLayerPanel(wxWindow* parent, const wxString& layerName,
                                   int layerIndex, const wxColour& headerColor)
	: wxPanel(parent, wxID_ANY)
	, m_layerIndex(layerIndex)
	, m_layerName(layerName)
	, m_itemsListCtrl(nullptr)
	, m_itemsImageList(nullptr)
	, m_floorClusterRadio(nullptr)
	, m_itemsListRadio(nullptr)
	, m_summaryText(nullptr)
{
	CreateControls(headerColor);
}

ZOrderLayerPanel::~ZOrderLayerPanel() {
}

void ZOrderLayerPanel::CreateControls(const wxColour& headerColor) {
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// -- Colored header label --
	wxPanel* headerPanel = new wxPanel(this, wxID_ANY);
	headerPanel->SetBackgroundColour(headerColor);
	wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText* headerLabel = new wxStaticText(headerPanel, wxID_ANY, m_layerName);
	headerLabel->SetForegroundColour(*wxWHITE);
	wxFont headerFont = headerLabel->GetFont();
	headerFont.SetWeight(wxFONTWEIGHT_BOLD);
	headerLabel->SetFont(headerFont);
	headerSizer->Add(headerLabel, 0, wxALL, 4);
	headerPanel->SetSizer(headerSizer);
	mainSizer->Add(headerPanel, 0, wxEXPAND);

	// -- Items list control --
	m_itemsImageList = new wxImageList(ITEM_ICON_SIZE, ITEM_ICON_SIZE, true);
	m_itemsListCtrl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 120),
	                                  wxLC_REPORT | wxLC_SINGLE_SEL);
	m_itemsListCtrl->AssignImageList(m_itemsImageList, wxIMAGE_LIST_SMALL);
	m_itemsListCtrl->InsertColumn(0, "", wxLIST_FORMAT_LEFT, 40);
	m_itemsListCtrl->InsertColumn(1, "Item ID", wxLIST_FORMAT_LEFT, 70);
	m_itemsListCtrl->InsertColumn(2, "Count", wxLIST_FORMAT_LEFT, 60);
	mainSizer->Add(m_itemsListCtrl, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 2);

	// -- Role radio buttons --
	wxBoxSizer* roleSizer = new wxBoxSizer(wxHORIZONTAL);
	roleSizer->Add(new wxStaticText(this, wxID_ANY, "Role:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

	// Use unique IDs based on layer index so events can be distinguished
	int floorRadioId = ID_LAYER_ROLE_BASE + m_layerIndex * 2;
	int itemsRadioId = ID_LAYER_ROLE_BASE + m_layerIndex * 2 + 1;

	m_floorClusterRadio = new wxRadioButton(this, floorRadioId, "Floor Cluster", wxDefaultPosition,
	                                         wxDefaultSize, wxRB_GROUP);
	m_itemsListRadio = new wxRadioButton(this, itemsRadioId, "Items List");
	roleSizer->Add(m_floorClusterRadio, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
	roleSizer->Add(m_itemsListRadio, 0, wxALIGN_CENTER_VERTICAL);
	mainSizer->Add(roleSizer, 0, wxEXPAND | wxALL, 4);

	// -- Summary text --
	m_summaryText = new wxStaticText(this, wxID_ANY, "No items");
	wxFont summaryFont = m_summaryText->GetFont();
	summaryFont.SetPointSize(summaryFont.GetPointSize() - 1);
	m_summaryText->SetFont(summaryFont);
	m_summaryText->SetForegroundColour(wxColour(160, 160, 160));
	mainSizer->Add(m_summaryText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

	SetSizer(mainSizer);
}

void ZOrderLayerPanel::SetItems(const std::vector<SelectionItemInfo>& items) {
	m_items = items;
	UpdateItemsList();
	UpdateSummary();
}

void ZOrderLayerPanel::UpdateItemsList() {
	m_itemsListCtrl->DeleteAllItems();
	m_itemsImageList->RemoveAll();

	if (m_items.empty()) return;

	// Deduplicate items by ID, counting occurrences
	std::map<uint16_t, int> uniqueCounts = GetUniqueItemCounts();

	int row = 0;
	for (const auto& pair : uniqueCounts) {
		uint16_t itemId = pair.first;
		int count = pair.second;

		// Add sprite to image list
		wxBitmap bmp = GetItemBitmap(itemId, ITEM_ICON_SIZE);
		int imgIdx = m_itemsImageList->Add(bmp);

		// Insert row
		long idx = m_itemsListCtrl->InsertItem(row, "", imgIdx);
		m_itemsListCtrl->SetItem(idx, 1, wxString::Format("%d", itemId));
		m_itemsListCtrl->SetItem(idx, 2, wxString::Format("%d", count));
		row++;
	}
}

void ZOrderLayerPanel::UpdateSummary() {
	if (m_items.empty()) {
		m_summaryText->SetLabel("No items");
		return;
	}

	std::map<uint16_t, int> uniqueCounts = GetUniqueItemCounts();
	int totalCount = GetTotalItemCount();

	wxString summary = wxString::Format("%d unique item(s), %d total", (int)uniqueCounts.size(), totalCount);
	if (GetRole() == 0) {
		summary += " [Floor Cluster]";
	} else {
		summary += " [Items List]";
	}
	m_summaryText->SetLabel(summary);
}

wxBitmap ZOrderLayerPanel::GetItemBitmap(uint16_t itemId, int size) {
	wxBitmap bmp(size, size, 32);
	wxMemoryDC dc(bmp);

	// Fill with dark background
	dc.SetBackground(wxBrush(wxColour(0x0C, 0x14, 0x2A)));
	dc.Clear();

	// Get the item definition to find the client sprite ID
	const auto itemDef = g_item_definitions.get(itemId);
	Sprite* spr = nullptr;
	if (itemDef) {
		spr = g_gui.gfx.getSprite(itemDef.clientId());
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

void ZOrderLayerPanel::SetRole(int role) {
	if (role == 0) {
		m_floorClusterRadio->SetValue(true);
	} else {
		m_itemsListRadio->SetValue(true);
	}
	UpdateSummary();
}

int ZOrderLayerPanel::GetRole() const {
	if (m_floorClusterRadio && m_floorClusterRadio->GetValue()) {
		return 0;
	}
	return 1;
}

std::map<uint16_t, int> ZOrderLayerPanel::GetUniqueItemCounts() const {
	std::map<uint16_t, int> counts;
	for (const auto& item : m_items) {
		counts[item.itemId] += 1;
	}
	return counts;
}

int ZOrderLayerPanel::GetTotalItemCount() const {
	return (int)m_items.size();
}

void ZOrderLayerPanel::OnRoleChanged(wxCommandEvent& event) {
	UpdateSummary();

	// Notify the dialog (grandparent: panel -> scrolled window -> dialog)
	wxWindow* dialog = GetParent() ? GetParent()->GetParent() : nullptr;
	if (dialog) {
		wxCommandEvent notifyEvent(wxEVT_RADIOBUTTON, GetId());
		notifyEvent.SetEventObject(this);
		wxPostEvent(dialog, notifyEvent);
	}
}

//=============================================================================
// AreaDecorationRuleFromSelectionDialog
//=============================================================================

wxBEGIN_EVENT_TABLE(AreaDecorationRuleFromSelectionDialog, wxDialog)
	EVT_BUTTON(ID_PREVIEW_CLUSTER, AreaDecorationRuleFromSelectionDialog::OnPreviewCluster)
	EVT_BUTTON(ID_RULE_OK, AreaDecorationRuleFromSelectionDialog::OnOK)
	EVT_BUTTON(ID_RULE_CANCEL, AreaDecorationRuleFromSelectionDialog::OnCancel)
	EVT_CLOSE(AreaDecorationRuleFromSelectionDialog::OnClose)
	EVT_RADIOBUTTON(wxID_ANY, AreaDecorationRuleFromSelectionDialog::OnRoleChanged)
wxEND_EVENT_TABLE()

AreaDecorationRuleFromSelectionDialog::AreaDecorationRuleFromSelectionDialog(
	wxWindow* parent, Editor& editor)
	: wxDialog(parent, wxID_ANY, "Generate Rule from Selection", wxDefaultPosition,
	           wxSize(600, 750), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, m_editor(editor)
	, m_accepted(false)
	, m_groundPanel(nullptr)
	, m_bordersPanel(nullptr)
	, m_bottomPanel(nullptr)
	, m_regularPanel(nullptr)
	, m_nameInput(nullptr)
	, m_instanceCountSpin(nullptr)
	, m_minDistanceSpin(nullptr)
	, m_densitySpin(nullptr)
	, m_clusterSummaryText(nullptr)
	, m_itemsSummaryText(nullptr)
	, m_previewBtn(nullptr)
{
	SetMinSize(wxSize(500, 600));
	CreateControls();
	ExtractItemsFromSelection();
	DistributeItemsToPanels();
	UpdateSummary();
	Centre(wxBOTH);
}

AreaDecorationRuleFromSelectionDialog::~AreaDecorationRuleFromSelectionDialog() {
}

void AreaDecorationRuleFromSelectionDialog::CreateControls() {
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// -- Rule name input --
	wxBoxSizer* nameSizer = new wxBoxSizer(wxHORIZONTAL);
	nameSizer->Add(new wxStaticText(this, wxID_ANY, "Rule Name:"), 0,
	               wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	m_nameInput = new wxTextCtrl(this, ID_RULE_NAME, "Selection Rule");
	nameSizer->Add(m_nameInput, 1, wxEXPAND);
	mainSizer->Add(nameSizer, 0, wxEXPAND | wxALL, 6);

	// -- Scrolled window containing the four Z-order layer panels --
	wxScrolledWindow* scrolledWin = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
	                                                      wxDefaultSize, wxVSCROLL);
	scrolledWin->SetScrollRate(0, 10);
	wxBoxSizer* scrollSizer = new wxBoxSizer(wxVERTICAL);

	// Layer 0: Ground (green)
	m_groundPanel = new ZOrderLayerPanel(scrolledWin, "Ground", 0, wxColour(60, 120, 60));
	m_groundPanel->SetRole(0); // Default: Floor Cluster
	scrollSizer->Add(m_groundPanel, 0, wxEXPAND | wxALL, 2);

	// Layer 1: Borders (blue)
	m_bordersPanel = new ZOrderLayerPanel(scrolledWin, "Borders", 1, wxColour(60, 80, 140));
	m_bordersPanel->SetRole(0); // Default: Floor Cluster
	scrollSizer->Add(m_bordersPanel, 0, wxEXPAND | wxALL, 2);

	// Layer 2: Bottom Items (orange)
	m_bottomPanel = new ZOrderLayerPanel(scrolledWin, "Bottom Items", 2, wxColour(160, 100, 40));
	m_bottomPanel->SetRole(1); // Default: Items List
	scrollSizer->Add(m_bottomPanel, 0, wxEXPAND | wxALL, 2);

	// Layer 3: Regular Items (purple)
	m_regularPanel = new ZOrderLayerPanel(scrolledWin, "Regular Items", 3, wxColour(120, 60, 140));
	m_regularPanel->SetRole(1); // Default: Items List
	scrollSizer->Add(m_regularPanel, 0, wxEXPAND | wxALL, 2);

	scrolledWin->SetSizer(scrollSizer);
	mainSizer->Add(scrolledWin, 1, wxEXPAND | wxLEFT | wxRIGHT, 4);

	// -- Settings section --
	wxStaticBoxSizer* settingsBox = new wxStaticBoxSizer(wxVERTICAL, this, "Rule Settings");
	wxFlexGridSizer* settingsGrid = new wxFlexGridSizer(3, 2, 4, 8);
	settingsGrid->AddGrowableCol(1, 1);

	// Instance Count
	settingsGrid->Add(new wxStaticText(this, wxID_ANY, "Instance Count:"), 0,
	                  wxALIGN_CENTER_VERTICAL);
	m_instanceCountSpin = new wxSpinCtrl(this, ID_INSTANCE_COUNT, "1", wxDefaultPosition,
	                                      wxSize(80, -1), wxSP_ARROW_KEYS, 1, 100, 1);
	settingsGrid->Add(m_instanceCountSpin, 0);

	// Min Distance
	settingsGrid->Add(new wxStaticText(this, wxID_ANY, "Min Distance:"), 0,
	                  wxALIGN_CENTER_VERTICAL);
	m_minDistanceSpin = new wxSpinCtrl(this, ID_MIN_DISTANCE, "5", wxDefaultPosition,
	                                    wxSize(80, -1), wxSP_ARROW_KEYS, 1, 50, 5);
	settingsGrid->Add(m_minDistanceSpin, 0);

	// Density
	settingsGrid->Add(new wxStaticText(this, wxID_ANY, "Density (%):"), 0,
	                  wxALIGN_CENTER_VERTICAL);
	m_densitySpin = new wxSpinCtrl(this, ID_DENSITY, "100", wxDefaultPosition,
	                                wxSize(80, -1), wxSP_ARROW_KEYS, 1, 100, 100);
	settingsGrid->Add(m_densitySpin, 0);

	settingsBox->Add(settingsGrid, 0, wxEXPAND | wxALL, 4);
	mainSizer->Add(settingsBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

	// -- Summary section --
	wxStaticBoxSizer* summaryBox = new wxStaticBoxSizer(wxVERTICAL, this, "Summary");
	m_clusterSummaryText = new wxStaticText(this, wxID_ANY, "Cluster: 0 tiles");
	m_itemsSummaryText = new wxStaticText(this, wxID_ANY, "Items: 0 unique items");
	summaryBox->Add(m_clusterSummaryText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);
	summaryBox->Add(m_itemsSummaryText, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
	mainSizer->Add(summaryBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

	// -- Preview Cluster button --
	m_previewBtn = new wxButton(this, ID_PREVIEW_CLUSTER, "Preview Cluster");
	mainSizer->Add(m_previewBtn, 0, wxALIGN_CENTER | wxALL, 6);

	// -- OK / Cancel buttons --
	wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
	btnSizer->AddStretchSpacer();
	wxButton* okBtn = new wxButton(this, ID_RULE_OK, "OK");
	wxButton* cancelBtn = new wxButton(this, ID_RULE_CANCEL, "Cancel");
	btnSizer->Add(okBtn, 0, wxRIGHT, 4);
	btnSizer->Add(cancelBtn, 0);
	mainSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 6);

	SetSizer(mainSizer);
	Layout();
}

void AreaDecorationRuleFromSelectionDialog::ExtractItemsFromSelection() {
	m_allItems.clear();

	Selection& selection = m_editor.selection;
	const auto& tiles = selection.getTiles();

	if (tiles.empty()) return;

	// Calculate the center position from selection bounds
	int minX = INT_MAX, minY = INT_MAX, minZ = INT_MAX;
	int maxX = INT_MIN, maxY = INT_MIN, maxZ = INT_MIN;

	for (Tile* tile : tiles) {
		if (!tile) continue;
		const Position& pos = tile->getPosition();
		if (pos.x < minX) minX = pos.x;
		if (pos.y < minY) minY = pos.y;
		if (pos.z < minZ) minZ = pos.z;
		if (pos.x > maxX) maxX = pos.x;
		if (pos.y > maxY) maxY = pos.y;
		if (pos.z > maxZ) maxZ = pos.z;
	}

	Position center((minX + maxX) / 2, (minY + maxY) / 2, (minZ + maxZ) / 2);

	// Extract items from each selected tile
	for (Tile* tile : tiles) {
		if (!tile) continue;
		const Position& tilePos = tile->getPosition();
		Position relOffset(tilePos.x - center.x, tilePos.y - center.y, tilePos.z - center.z);

		// Ground item -> layer 0
		if (tile->ground) {
			uint16_t groundId = tile->ground->getID();
			if (groundId > 0) {
				m_allItems.emplace_back(groundId, tilePos, relOffset, 0);
			}
		}

		// Other items
		for (const auto& item : tile->items) {
			if (!item) continue;
			uint16_t itemId = item->getID();
			if (itemId == 0) continue;

			int layer;
			if (item->isBorder()) {
				layer = 1; // Border
			} else if (item->isAlwaysOnBottom()) {
				layer = 2; // Bottom (non-border, already excluded above)
			} else {
				layer = 3; // Regular
			}

			m_allItems.emplace_back(itemId, tilePos, relOffset, layer);
		}
	}
}

void AreaDecorationRuleFromSelectionDialog::DistributeItemsToPanels() {
	std::vector<SelectionItemInfo> groundItems;
	std::vector<SelectionItemInfo> borderItems;
	std::vector<SelectionItemInfo> bottomItems;
	std::vector<SelectionItemInfo> regularItems;

	for (const auto& item : m_allItems) {
		switch (item.zOrderLayer) {
			case 0: groundItems.push_back(item); break;
			case 1: borderItems.push_back(item); break;
			case 2: bottomItems.push_back(item); break;
			case 3: regularItems.push_back(item); break;
		}
	}

	m_groundPanel->SetItems(groundItems);
	m_bordersPanel->SetItems(borderItems);
	m_bottomPanel->SetItems(bottomItems);
	m_regularPanel->SetItems(regularItems);
}

void AreaDecorationRuleFromSelectionDialog::UpdateSummary() {
	// Count cluster tiles (panels with role 0) and items (panels with role 1)
	int clusterTileCount = 0;
	int clusterItemCount = 0;
	int itemsUniqueCount = 0;
	int itemsTotalCount = 0;

	ZOrderLayerPanel* panels[] = { m_groundPanel, m_bordersPanel, m_bottomPanel, m_regularPanel };

	for (ZOrderLayerPanel* panel : panels) {
		if (!panel) continue;
		if (panel->GetRole() == 0) {
			// Floor Cluster role - count unique tile positions
			std::set<std::pair<int, int>> uniquePositions;
			for (const auto& item : panel->GetItems()) {
				uniquePositions.insert({item.relativeOffset.x, item.relativeOffset.y});
			}
			clusterTileCount += (int)uniquePositions.size();
			clusterItemCount += panel->GetTotalItemCount();
		} else {
			// Items List role
			std::map<uint16_t, int> uniqueCounts = panel->GetUniqueItemCounts();
			itemsUniqueCount += (int)uniqueCounts.size();
			itemsTotalCount += panel->GetTotalItemCount();
		}
	}

	if (m_clusterSummaryText) {
		m_clusterSummaryText->SetLabel(wxString::Format("Cluster: %d tile(s), %d item(s)",
		                                                clusterTileCount, clusterItemCount));
	}
	if (m_itemsSummaryText) {
		m_itemsSummaryText->SetLabel(wxString::Format("Items: %d unique, %d total",
		                                              itemsUniqueCount, itemsTotalCount));
	}
}

AreaDecoration::FloorRule AreaDecorationRuleFromSelectionDialog::BuildRule() {
	AreaDecoration::FloorRule rule;
	rule.ruleMode = AreaDecoration::RuleMode::Cluster;
	rule.name = m_nameInput->GetValue().ToStdString();
	rule.instanceCount = m_instanceCountSpin->GetValue();
	rule.instanceMinDistance = m_minDistanceSpin->GetValue();
	rule.density = (float)m_densitySpin->GetValue() / 100.0f;

	// Build cluster tiles from panels with role 0 (Floor Cluster)
	// Group items by their relative offset to form CompositeTile objects
	std::map<std::pair<int, int>, std::vector<uint16_t>> clusterMap;

	ZOrderLayerPanel* panels[] = { m_groundPanel, m_bordersPanel, m_bottomPanel, m_regularPanel };

	for (ZOrderLayerPanel* panel : panels) {
		if (!panel) continue;
		if (panel->GetRole() == 0) {
			// Floor Cluster: group items by relative offset
			for (const auto& item : panel->GetItems()) {
				auto key = std::make_pair(item.relativeOffset.x, item.relativeOffset.y);
				clusterMap[key].push_back(item.itemId);
			}
		}
	}

	// Convert grouped items to CompositeTile objects
	for (const auto& pair : clusterMap) {
		AreaDecoration::CompositeTile ct;
		ct.offset = Position(pair.first.first, pair.first.second, 0);
		ct.itemIds = pair.second;
		rule.clusterTiles.push_back(ct);
	}

	// Build items list from panels with role 1 (Items List)
	// Deduplicate by ID, weight = count * 100
	std::map<uint16_t, int> itemCounts;

	for (ZOrderLayerPanel* panel : panels) {
		if (!panel) continue;
		if (panel->GetRole() == 1) {
			std::map<uint16_t, int> panelCounts = panel->GetUniqueItemCounts();
			for (const auto& pair : panelCounts) {
				itemCounts[pair.first] += pair.second;
			}
		}
	}

	for (const auto& pair : itemCounts) {
		AreaDecoration::ItemEntry entry(pair.first, pair.second * 100);
		rule.items.push_back(entry);
	}

	return rule;
}

void AreaDecorationRuleFromSelectionDialog::OnPreviewCluster(wxCommandEvent& event) {
	m_generatedRule = BuildRule();

	if (m_generatedRule.clusterTiles.empty()) {
		wxMessageBox("No cluster tiles to preview. Assign at least one layer panel to 'Floor Cluster' role.",
		             "Preview", wxOK | wxICON_INFORMATION, this);
		return;
	}

	ClusterPreviewWindow* preview = new ClusterPreviewWindow(this, m_generatedRule, nullptr, "Cluster Preview");
	preview->ShowModal();
	preview->Destroy();
}

void AreaDecorationRuleFromSelectionDialog::OnOK(wxCommandEvent& event) {
	wxString name = m_nameInput->GetValue().Trim();
	if (name.IsEmpty()) {
		wxMessageBox("Please enter a rule name.", "Validation", wxOK | wxICON_WARNING, this);
		return;
	}

	m_generatedRule = BuildRule();

	if (m_generatedRule.clusterTiles.empty() && m_generatedRule.items.empty()) {
		wxMessageBox("The rule has no cluster tiles and no items.\nPlease assign at least one layer as Floor Cluster or Items List.",
			"Validation", wxOK | wxICON_WARNING, this);
		return;
	}

	m_accepted = true;
	EndModal(wxID_OK);
}

void AreaDecorationRuleFromSelectionDialog::OnCancel(wxCommandEvent& event) {
	EndModal(wxID_CANCEL);
}

void AreaDecorationRuleFromSelectionDialog::OnClose(wxCloseEvent& event) {
	EndModal(wxID_CANCEL);
}

void AreaDecorationRuleFromSelectionDialog::OnRoleChanged(wxCommandEvent& event) {
	UpdateSummary();
}
