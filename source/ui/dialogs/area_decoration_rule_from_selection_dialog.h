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

#ifndef RME_AREA_DECORATION_RULE_FROM_SELECTION_DIALOG_H
#define RME_AREA_DECORATION_RULE_FROM_SELECTION_DIALOG_H

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include "editor/area_decoration.h"

class Editor;

//=============================================================================
// SelectionItemInfo - Item extracted from the current map selection
//=============================================================================
struct SelectionItemInfo {
	uint16_t itemId = 0;
	Position tilePosition;      // Absolute position on the map
	Position relativeOffset;    // Relative to selection center
	int zOrderLayer = 3;        // 0=ground, 1=border, 2=bottom, 3=regular

	SelectionItemInfo() = default;
	SelectionItemInfo(uint16_t id, const Position& tilePos, const Position& offset, int layer)
		: itemId(id), tilePosition(tilePos), relativeOffset(offset), zOrderLayer(layer) {}
};

//=============================================================================
// ZOrderLayerPanel - Panel for one Z-order layer showing items and role
//=============================================================================
class ZOrderLayerPanel : public wxPanel {
public:
	ZOrderLayerPanel(wxWindow* parent, const wxString& layerName,
	                 int layerIndex, const wxColour& headerColor);
	virtual ~ZOrderLayerPanel();

	// Item management
	void SetItems(const std::vector<SelectionItemInfo>& items);
	const std::vector<SelectionItemInfo>& GetItems() const { return m_items; }

	// Role: 0 = Floor Cluster, 1 = Items List
	void SetRole(int role);
	int GetRole() const;

	// Summary helpers
	std::map<uint16_t, int> GetUniqueItemCounts() const;
	int GetTotalItemCount() const;

private:
	int m_layerIndex;
	wxString m_layerName;
	std::vector<SelectionItemInfo> m_items;

	// UI controls
	wxListCtrl* m_itemsListCtrl;
	wxImageList* m_itemsImageList;
	wxRadioButton* m_floorClusterRadio;
	wxRadioButton* m_itemsListRadio;
	wxStaticText* m_summaryText;

	void CreateControls(const wxColour& headerColor);
	void UpdateItemsList();
	void UpdateSummary();

	// Get sprite bitmap for item
	wxBitmap GetItemBitmap(uint16_t itemId, int size = 32);

	// Event handlers
	void OnRoleChanged(wxCommandEvent& event);

	wxDECLARE_EVENT_TABLE();
};

//=============================================================================
// AreaDecorationRuleFromSelectionDialog - Generate a FloorRule from selection
//=============================================================================
class AreaDecorationRuleFromSelectionDialog : public wxDialog {
public:
	AreaDecorationRuleFromSelectionDialog(wxWindow* parent, Editor& editor);
	virtual ~AreaDecorationRuleFromSelectionDialog();

	// Results
	const AreaDecoration::FloorRule& GetGeneratedRule() const { return m_generatedRule; }
	bool WasAccepted() const { return m_accepted; }

private:
	Editor& m_editor;
	bool m_accepted = false;
	AreaDecoration::FloorRule m_generatedRule;

	// Z-order layer panels (one per layer)
	ZOrderLayerPanel* m_groundPanel;    // Layer 0: ground items
	ZOrderLayerPanel* m_bordersPanel;   // Layer 1: border items
	ZOrderLayerPanel* m_bottomPanel;    // Layer 2: bottom items
	ZOrderLayerPanel* m_regularPanel;   // Layer 3: regular items

	// Rule settings
	wxTextCtrl* m_nameInput;
	wxSpinCtrl* m_instanceCountSpin;
	wxSpinCtrl* m_minDistanceSpin;
	wxSpinCtrl* m_densitySpin;

	// Summary and preview
	wxStaticText* m_clusterSummaryText;
	wxStaticText* m_itemsSummaryText;
	wxButton* m_previewBtn;

	// All extracted items
	std::vector<SelectionItemInfo> m_allItems;

	// Core methods
	void ExtractItemsFromSelection();
	void DistributeItemsToPanels();
	void UpdateSummary();
	AreaDecoration::FloorRule BuildRule();

	void CreateControls();

	// Event handlers
	void OnPreviewCluster(wxCommandEvent& event);
	void OnOK(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);
	void OnRoleChanged(wxCommandEvent& event);

	wxDECLARE_EVENT_TABLE();
};

#endif // RME_AREA_DECORATION_RULE_FROM_SELECTION_DIALOG_H
