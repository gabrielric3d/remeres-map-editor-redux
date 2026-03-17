//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Border Editor Dialog - Visual editor for auto borders
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/dialogs/border_editor_dialog.h"
#include "ui/gui.h"
#include "ui/theme.h"
#include "ui/find_item_window.h"
#include "rendering/core/graphics.h"
#include "item_definitions/core/item_definition_store.h"
#include "brushes/brush.h"
#include "brushes/raw/raw_brush.h"
#include "brushes/ground/ground_brush.h"
#include "app/managers/version_manager.h"
#include "app/client_version.h"
#include "ext/pugixml.hpp"

#include <wx/sizer.h>
#include <wx/gbsizer.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/dnd.h>
#include <sstream>
#include <algorithm>
#include <set>

#define BORDER_PREVIEW_SIZE 192
#define ID_BORDER_GRID_SELECT wxID_HIGHEST + 1
#define ID_GROUND_ITEM_LIST wxID_HIGHEST + 2

// ============================================================================
// Utility functions
// ============================================================================

BorderEdgePosition edgeStringToPosition(const std::string& edgeStr) {
	if (edgeStr == "n") return EDGE_N;
	if (edgeStr == "e") return EDGE_E;
	if (edgeStr == "s") return EDGE_S;
	if (edgeStr == "w") return EDGE_W;
	if (edgeStr == "cnw") return EDGE_CNW;
	if (edgeStr == "cne") return EDGE_CNE;
	if (edgeStr == "cse") return EDGE_CSE;
	if (edgeStr == "csw") return EDGE_CSW;
	if (edgeStr == "dnw") return EDGE_DNW;
	if (edgeStr == "dne") return EDGE_DNE;
	if (edgeStr == "dse") return EDGE_DSE;
	if (edgeStr == "dsw") return EDGE_DSW;
	return EDGE_NONE;
}

std::string edgePositionToString(BorderEdgePosition pos) {
	switch (pos) {
		case EDGE_N: return "n";
		case EDGE_E: return "e";
		case EDGE_S: return "s";
		case EDGE_W: return "w";
		case EDGE_CNW: return "cnw";
		case EDGE_CNE: return "cne";
		case EDGE_CSE: return "cse";
		case EDGE_CSW: return "csw";
		case EDGE_DNW: return "dnw";
		case EDGE_DNE: return "dne";
		case EDGE_DSE: return "dse";
		case EDGE_DSW: return "dsw";
		default: return "";
	}
}

static uint16_t GetItemIDFromBrush(Brush* brush) {
	if (!brush) return 0;

	RAWBrush* rawBrush = brush->as<RAWBrush>();
	if (rawBrush) {
		uint16_t id = rawBrush->getItemID();
		if (id > 0) return id;
	}

	int lookId = brush->getLookID();
	if (lookId > 0) return static_cast<uint16_t>(lookId);

	return 0;
}

// ============================================================================
// BorderGridDropTarget
// ============================================================================

BorderGridDropTarget::BorderGridDropTarget(BorderGridPanel* grid) : m_grid(grid) {
}

bool BorderGridDropTarget::OnDropText(wxCoord x, wxCoord y, const wxString& data) {
	if (!m_grid) return false;

	wxString itemData = data;

	// Accept both formats from different drag sources
	if (itemData.StartsWith("RME_ITEM:")) {
		itemData = itemData.Mid(9);
	} else if (itemData.StartsWith("ITEM_ID:")) {
		itemData = itemData.Mid(8);
	} else {
		return false;
	}

	unsigned long idVal = 0;
	if (!itemData.ToULong(&idVal) || idVal == 0 || idVal > 0xFFFF) {
		return false;
	}

	BorderEdgePosition pos = m_grid->GetPositionFromCoordinates(x, y);
	if (pos == EDGE_NONE) {
		return false;
	}

	m_grid->SetSelectedPosition(pos);

	// Find the parent BorderEditorDialog
	wxWindow* parent = m_grid->GetParent();
	while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}

	BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);
	if (!dialog) {
		return false;
	}

	dialog->ApplyItemToPosition(pos, static_cast<uint16_t>(idVal));
	return true;
}

// ============================================================================
// Event tables
// ============================================================================

BEGIN_EVENT_TABLE(BorderEditorDialog, wxDialog)
	EVT_BUTTON(wxID_ADD, BorderEditorDialog::OnAddItem)
	EVT_BUTTON(wxID_CLEAR, BorderEditorDialog::OnClear)
	EVT_BUTTON(wxID_SAVE, BorderEditorDialog::OnSave)
	EVT_BUTTON(wxID_CLOSE, BorderEditorDialog::OnClose)
	EVT_BUTTON(wxID_FIND, BorderEditorDialog::OnBrowse)
	EVT_COMBOBOX(wxID_ANY, BorderEditorDialog::OnLoadBorder)
	EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, BorderEditorDialog::OnPageChanged)
	EVT_BUTTON(wxID_ADD + 100, BorderEditorDialog::OnAddGroundItem)
	EVT_BUTTON(wxID_REMOVE, BorderEditorDialog::OnRemoveGroundItem)
	EVT_BUTTON(wxID_FIND + 100, BorderEditorDialog::OnGroundBrowse)
	EVT_COMBOBOX(wxID_ANY + 100, BorderEditorDialog::OnLoadGroundBrush)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(BorderGridPanel, wxPanel)
	EVT_PAINT(BorderGridPanel::OnPaint)
	EVT_LEFT_UP(BorderGridPanel::OnMouseClick)
	EVT_LEFT_DOWN(BorderGridPanel::OnMouseDown)
	EVT_RIGHT_UP(BorderGridPanel::OnRightClick)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(BorderPreviewPanel, wxPanel)
	EVT_PAINT(BorderPreviewPanel::OnPaint)
END_EVENT_TABLE()

// ============================================================================
// BorderEditorDialog
// ============================================================================

BorderEditorDialog::BorderEditorDialog(wxWindow* parent, const wxString& title) :
	wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(1000, 650),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	m_nextBorderId(1),
	m_activeTab(0) {

	SetBackgroundColour(Theme::Get(Theme::Role::Surface));

	CreateGUIControls();
	LoadExistingBorders();
	LoadExistingGroundBrushes();

	m_gridPanel->LoadSampleBorder();

	m_idCtrl->SetValue(m_nextBorderId);

	CenterOnParent();
}

BorderEditorDialog::~BorderEditorDialog() {
}

wxString BorderEditorDialog::GetVersionDataDirectory() {
	ClientVersion* version = g_version.getLoadedVersion();
	if (!version) return wxString();
	FileName data_path = version->getDataPath();
	return data_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

void BorderEditorDialog::CreateGUIControls() {
	wxBoxSizer* topSizer = newd wxBoxSizer(wxVERTICAL);

	// Common properties
	wxStaticBoxSizer* commonPropertiesSizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Common Properties");
	wxBoxSizer* commonPropertiesHorizSizer = newd wxBoxSizer(wxHORIZONTAL);

	// Name field
	wxBoxSizer* nameSizer = newd wxBoxSizer(wxVERTICAL);
	nameSizer->Add(newd wxStaticText(this, wxID_ANY, "Name:"), 0);
	m_nameCtrl = newd wxTextCtrl(this, wxID_ANY);
	m_nameCtrl->SetToolTip("Descriptive name for the border");
	nameSizer->Add(m_nameCtrl, 0, wxEXPAND | wxTOP, 2);
	commonPropertiesHorizSizer->Add(nameSizer, 1, wxEXPAND | wxRIGHT, 10);

	// ID field
	wxBoxSizer* idSizer = newd wxBoxSizer(wxVERTICAL);
	idSizer->Add(newd wxStaticText(this, wxID_ANY, "ID:"), 0);
	m_idCtrl = newd wxSpinCtrl(this, wxID_ANY, "1", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535);
	m_idCtrl->SetToolTip("Unique identifier for this border");
	idSizer->Add(m_idCtrl, 0, wxEXPAND | wxTOP, 2);
	commonPropertiesHorizSizer->Add(idSizer, 0, wxEXPAND);

	commonPropertiesSizer->Add(commonPropertiesHorizSizer, 0, wxEXPAND | wxALL, 5);
	topSizer->Add(commonPropertiesSizer, 0, wxEXPAND | wxALL, 5);

	// Create notebook with Border and Ground tabs
	m_notebook = newd wxNotebook(this, wxID_ANY);

	// ========== BORDER TAB ==========
	m_borderPanel = newd wxPanel(m_notebook);
	wxBoxSizer* borderSizer = newd wxBoxSizer(wxVERTICAL);

	// Border Properties
	wxStaticBoxSizer* borderPropertiesSizer = newd wxStaticBoxSizer(wxVERTICAL, m_borderPanel, "Border Properties");
	wxBoxSizer* borderPropsHorizSizer = newd wxBoxSizer(wxHORIZONTAL);

	// Left column - Group and Type
	wxBoxSizer* leftColSizer = newd wxBoxSizer(wxVERTICAL);

	wxBoxSizer* groupSizer = newd wxBoxSizer(wxVERTICAL);
	groupSizer->Add(newd wxStaticText(m_borderPanel, wxID_ANY, "Group:"), 0);
	m_groupCtrl = newd wxSpinCtrl(m_borderPanel, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535);
	m_groupCtrl->SetToolTip("Optional group identifier (0 = no group)");
	groupSizer->Add(m_groupCtrl, 0, wxEXPAND | wxTOP, 2);
	leftColSizer->Add(groupSizer, 0, wxEXPAND | wxBOTTOM, 5);

	wxBoxSizer* typeSizer = newd wxBoxSizer(wxVERTICAL);
	typeSizer->Add(newd wxStaticText(m_borderPanel, wxID_ANY, "Type:"), 0);
	wxBoxSizer* checkboxSizer = newd wxBoxSizer(wxHORIZONTAL);
	m_isOptionalCheck = newd wxCheckBox(m_borderPanel, wxID_ANY, "Optional");
	m_isOptionalCheck->SetToolTip("Marks this border as optional");
	m_isGroundCheck = newd wxCheckBox(m_borderPanel, wxID_ANY, "Ground");
	m_isGroundCheck->SetToolTip("Marks this border as a ground border");
	checkboxSizer->Add(m_isOptionalCheck, 0, wxRIGHT, 10);
	checkboxSizer->Add(m_isGroundCheck, 0);
	typeSizer->Add(checkboxSizer, 0, wxEXPAND | wxTOP, 2);
	leftColSizer->Add(typeSizer, 0, wxEXPAND);

	borderPropsHorizSizer->Add(leftColSizer, 1, wxEXPAND | wxRIGHT, 10);

	// Right column - Load Existing
	wxBoxSizer* rightColSizer = newd wxBoxSizer(wxVERTICAL);
	rightColSizer->Add(newd wxStaticText(m_borderPanel, wxID_ANY, "Load Existing:"), 0);
	m_existingBordersCombo = newd wxComboBox(m_borderPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY | wxCB_DROPDOWN);
	m_existingBordersCombo->SetToolTip("Load an existing border as template");
	rightColSizer->Add(m_existingBordersCombo, 0, wxEXPAND | wxTOP, 2);

	borderPropsHorizSizer->Add(rightColSizer, 1, wxEXPAND);
	borderPropertiesSizer->Add(borderPropsHorizSizer, 0, wxEXPAND | wxALL, 5);
	borderSizer->Add(borderPropertiesSizer, 0, wxEXPAND | wxALL, 5);

	// Border Grid + Preview (unified panel)
	wxStaticBoxSizer* gridSizer = newd wxStaticBoxSizer(wxVERTICAL, m_borderPanel, "Border Grid");
	m_gridPanel = newd BorderGridPanel(m_borderPanel);
	m_gridPanel->SetDropTarget(new BorderGridDropTarget(m_gridPanel));
	gridSizer->Add(m_gridPanel, 1, wxEXPAND | wxALL, 5);

	// Hidden preview panel (used internally for data, drawn inside the grid)
	m_previewPanel = newd BorderPreviewPanel(m_borderPanel);
	m_previewPanel->Hide();

	wxStaticText* instructions = newd wxStaticText(m_borderPanel, wxID_ANY,
		"Click to place brush | Right-click to remove | Drag item from palette");
	instructions->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	gridSizer->Add(instructions, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

	// Current selected item controls
	wxBoxSizer* itemSizer = newd wxBoxSizer(wxHORIZONTAL);
	itemSizer->Add(newd wxStaticText(m_borderPanel, wxID_ANY, "Item ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_itemIdCtrl = newd wxSpinCtrl(m_borderPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 65535);
	m_itemIdCtrl->SetToolTip("Enter an item ID manually");
	itemSizer->Add(m_itemIdCtrl, 0, wxRIGHT, 5);
	wxButton* browseButton = newd wxButton(m_borderPanel, wxID_FIND, "Browse...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	browseButton->SetToolTip("Browse for an item");
	itemSizer->Add(browseButton, 0, wxRIGHT, 5);
	wxButton* addButton = newd wxButton(m_borderPanel, wxID_ADD, "Add Manually", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	addButton->SetToolTip("Add the item ID manually to the currently selected position");
	itemSizer->Add(addButton, 0);

	gridSizer->Add(itemSizer, 0, wxEXPAND | wxALL, 5);
	borderSizer->Add(gridSizer, 1, wxEXPAND | wxALL, 5);

	// Bottom buttons for border tab
	wxBoxSizer* borderButtonSizer = newd wxBoxSizer(wxHORIZONTAL);
	borderButtonSizer->Add(newd wxButton(m_borderPanel, wxID_CLEAR, "Clear"), 0, wxRIGHT, 5);
	borderButtonSizer->Add(newd wxButton(m_borderPanel, wxID_SAVE, "Save Border"), 0, wxRIGHT, 5);
	borderButtonSizer->AddStretchSpacer(1);
	borderButtonSizer->Add(newd wxButton(m_borderPanel, wxID_CLOSE, "Close"), 0);

	borderSizer->Add(borderButtonSizer, 0, wxEXPAND | wxALL, 5);
	m_borderPanel->SetSizer(borderSizer);

	// ========== GROUND TAB ==========
	m_groundPanel = newd wxPanel(m_notebook);
	wxBoxSizer* groundSizer = newd wxBoxSizer(wxVERTICAL);

	// Ground Brush Properties
	wxStaticBoxSizer* groundPropertiesSizer = newd wxStaticBoxSizer(wxVERTICAL, m_groundPanel, "Ground Brush Properties");

	wxBoxSizer* topRowSizer = newd wxBoxSizer(wxHORIZONTAL);

	// Server Look ID
	wxBoxSizer* serverIdSizer = newd wxBoxSizer(wxVERTICAL);
	serverIdSizer->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Server Look ID:"), 0);
	m_serverLookIdCtrl = newd wxSpinCtrl(m_groundPanel, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 65535);
	m_serverLookIdCtrl->SetToolTip("Server-side item ID");
	serverIdSizer->Add(m_serverLookIdCtrl, 0, wxEXPAND | wxTOP, 2);
	topRowSizer->Add(serverIdSizer, 1, wxEXPAND | wxRIGHT, 10);

	// Z-Order
	wxBoxSizer* zOrderSizer = newd wxBoxSizer(wxVERTICAL);
	zOrderSizer->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Z-Order:"), 0);
	m_zOrderCtrl = newd wxSpinCtrl(m_groundPanel, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 10000);
	m_zOrderCtrl->SetToolTip("Z-Order for display");
	zOrderSizer->Add(m_zOrderCtrl, 0, wxEXPAND | wxTOP, 2);
	topRowSizer->Add(zOrderSizer, 1, wxEXPAND | wxRIGHT, 10);

	// Existing ground brushes dropdown
	wxBoxSizer* existingSizer = newd wxBoxSizer(wxVERTICAL);
	existingSizer->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Load Existing:"), 0);
	m_existingGroundBrushesCombo = newd wxComboBox(m_groundPanel, wxID_ANY + 100, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY | wxCB_DROPDOWN);
	m_existingGroundBrushesCombo->SetToolTip("Load an existing ground brush as template");
	existingSizer->Add(m_existingGroundBrushesCombo, 0, wxEXPAND | wxTOP, 2);
	topRowSizer->Add(existingSizer, 1, wxEXPAND);

	groundPropertiesSizer->Add(topRowSizer, 0, wxEXPAND | wxALL, 5);
	groundSizer->Add(groundPropertiesSizer, 0, wxEXPAND | wxALL, 5);

	// Ground Items
	wxStaticBoxSizer* groundItemsSizer = newd wxStaticBoxSizer(wxVERTICAL, m_groundPanel, "Ground Items");

	m_groundItemsList = newd wxListBox(m_groundPanel, ID_GROUND_ITEM_LIST, wxDefaultPosition, wxSize(-1, 100), 0, nullptr, wxLB_SINGLE);
	groundItemsSizer->Add(m_groundItemsList, 0, wxEXPAND | wxALL, 5);

	wxBoxSizer* groundItemRowSizer = newd wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* itemDetailsSizer = newd wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* itemIdSizer2 = newd wxBoxSizer(wxVERTICAL);
	itemIdSizer2->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Item ID:"), 0);
	m_groundItemIdCtrl = newd wxSpinCtrl(m_groundPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 65535);
	m_groundItemIdCtrl->SetToolTip("ID of the item to add");
	itemIdSizer2->Add(m_groundItemIdCtrl, 0, wxEXPAND | wxTOP, 2);
	itemDetailsSizer->Add(itemIdSizer2, 0, wxEXPAND | wxRIGHT, 5);

	wxBoxSizer* chanceSizer = newd wxBoxSizer(wxVERTICAL);
	chanceSizer->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Chance:"), 0);
	m_groundItemChanceCtrl = newd wxSpinCtrl(m_groundPanel, wxID_ANY, "10", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 1, 10000);
	m_groundItemChanceCtrl->SetToolTip("Chance of this item appearing");
	chanceSizer->Add(m_groundItemChanceCtrl, 0, wxEXPAND | wxTOP, 2);
	itemDetailsSizer->Add(chanceSizer, 0, wxEXPAND);

	groundItemRowSizer->Add(itemDetailsSizer, 1, wxEXPAND | wxRIGHT, 10);

	wxBoxSizer* itemButtonsSizer = newd wxBoxSizer(wxVERTICAL);
	itemButtonsSizer->AddStretchSpacer();

	wxBoxSizer* buttonsSizer = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* groundBrowseButton = newd wxButton(m_groundPanel, wxID_FIND + 100, "Browse...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	groundBrowseButton->SetToolTip("Browse for an item");
	buttonsSizer->Add(groundBrowseButton, 0, wxRIGHT, 5);

	m_addGroundItemButton = newd wxButton(m_groundPanel, wxID_ADD + 100, "Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_addGroundItemButton->SetToolTip("Add this item to the list");
	buttonsSizer->Add(m_addGroundItemButton, 0, wxRIGHT, 5);

	m_removeGroundItemButton = newd wxButton(m_groundPanel, wxID_REMOVE, "Remove", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_removeGroundItemButton->SetToolTip("Remove the selected item");
	buttonsSizer->Add(m_removeGroundItemButton, 0);

	itemButtonsSizer->Add(buttonsSizer, 0, wxEXPAND);
	groundItemRowSizer->Add(itemButtonsSizer, 0, wxEXPAND);

	groundItemsSizer->Add(groundItemRowSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	groundSizer->Add(groundItemsSizer, 0, wxEXPAND | wxALL, 5);

	// Info label
	wxStaticText* gridInstructions = newd wxStaticText(m_groundPanel, wxID_ANY,
		"Use the grid in the Border tab to define borders for this ground brush.");
	gridInstructions->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	groundSizer->Add(gridInstructions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

	// Bottom buttons for ground tab
	wxBoxSizer* groundButtonSizer = newd wxBoxSizer(wxHORIZONTAL);
	groundButtonSizer->Add(newd wxButton(m_groundPanel, wxID_CLEAR, "Clear"), 0, wxRIGHT, 5);
	groundButtonSizer->Add(newd wxButton(m_groundPanel, wxID_SAVE, "Save Ground"), 0, wxRIGHT, 5);
	groundButtonSizer->AddStretchSpacer(1);
	groundButtonSizer->Add(newd wxButton(m_groundPanel, wxID_CLOSE, "Close"), 0);

	groundSizer->Add(groundButtonSizer, 0, wxEXPAND | wxALL, 5);
	m_groundPanel->SetSizer(groundSizer);

	// Add tabs to notebook
	m_notebook->AddPage(m_borderPanel, "Border");
	m_notebook->AddPage(m_groundPanel, "Ground");

	topSizer->Add(m_notebook, 1, wxEXPAND | wxALL, 5);

	SetSizer(topSizer);
	Layout();
}

// ============================================================================
// Load/Save
// ============================================================================

void BorderEditorDialog::LoadExistingBorders() {
	m_existingBordersCombo->Clear();
	m_existingBordersCombo->Append("<Create New>");
	m_existingBordersCombo->SetSelection(0);

	wxString dataDir = GetVersionDataDirectory();
	if (dataDir.IsEmpty()) return;

	wxString bordersFile = dataDir + "borders.xml";
	if (!wxFileExists(bordersFile)) {
		wxMessageBox("Cannot find borders.xml file in the data directory.", "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(bordersFile.ToStdString().c_str());
	if (!result) {
		wxMessageBox("Failed to load borders.xml: " + wxString(result.description()), "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		wxMessageBox("Invalid borders.xml file: missing 'materials' node", "Error", wxICON_ERROR);
		return;
	}

	int highestId = 0;

	for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
		pugi::xml_attribute idAttr = borderNode.attribute("id");
		if (!idAttr) continue;

		int id = idAttr.as_int();
		if (id > highestId) {
			highestId = id;
		}

		// Get the comment node before this border for its description
		std::string description;
		pugi::xml_node commentNode = borderNode.previous_sibling();
		if (commentNode && commentNode.type() == pugi::node_comment) {
			description = commentNode.value();
			size_t first = description.find_first_not_of(" \t\n\r");
			if (first != std::string::npos) {
				description = description.substr(first);
				size_t last = description.find_last_not_of(" \t\n\r");
				if (last != std::string::npos) {
					description = description.substr(0, last + 1);
				}
			} else {
				description.clear();
			}
		}

		wxString label = wxString::Format("Border %d", id);
		if (!description.empty()) {
			label += wxString::Format(" (%s)", wxString(description));
		}

		m_existingBordersCombo->Append(label, newd wxStringClientData(wxString::Format("%d", id)));
	}

	m_nextBorderId = highestId + 1;
	m_idCtrl->SetValue(m_nextBorderId);
}

void BorderEditorDialog::LoadExistingGroundBrushes() {
	m_existingGroundBrushesCombo->Clear();
	m_existingGroundBrushesCombo->Append("<Create New>");
	m_existingGroundBrushesCombo->SetSelection(0);

	wxString dataDir = GetVersionDataDirectory();
	if (dataDir.IsEmpty()) return;

	wxString groundsFile = dataDir + "grounds.xml";
	if (!wxFileExists(groundsFile)) return;

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(groundsFile.ToStdString().c_str());
	if (!result) return;

	pugi::xml_node materials = doc.child("materials");
	if (!materials) return;

	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		pugi::xml_attribute serverLookIdAttr = brushNode.attribute("server_lookid");
		pugi::xml_attribute typeAttr = brushNode.attribute("type");

		if (!typeAttr || std::string(typeAttr.as_string()) != "ground") continue;

		if (nameAttr && serverLookIdAttr) {
			wxString brushName = wxString(nameAttr.as_string());
			int serverId = serverLookIdAttr.as_int();
			m_existingGroundBrushesCombo->Append(brushName, newd wxStringClientData(wxString::Format("%d", serverId)));
		}
	}
}

void BorderEditorDialog::OnLoadBorder(wxCommandEvent& event) {
	int selection = m_existingBordersCombo->GetSelection();
	if (selection <= 0) {
		ClearItems();
		return;
	}

	wxStringClientData* data = static_cast<wxStringClientData*>(m_existingBordersCombo->GetClientObject(selection));
	if (!data) return;

	int borderId = wxAtoi(data->GetData());

	wxString dataDir = GetVersionDataDirectory();
	wxString bordersFile = dataDir + "borders.xml";

	if (!wxFileExists(bordersFile)) {
		wxMessageBox("Cannot find borders.xml file.", "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(bordersFile.ToStdString().c_str());
	if (!result) {
		wxMessageBox("Failed to load borders.xml: " + wxString(result.description()), "Error", wxICON_ERROR);
		return;
	}

	ClearItems();

	pugi::xml_node materials = doc.child("materials");
	for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
		pugi::xml_attribute idAttr = borderNode.attribute("id");
		if (!idAttr || idAttr.as_int() != borderId) continue;

		m_idCtrl->SetValue(borderId);

		pugi::xml_attribute typeAttr = borderNode.attribute("type");
		m_isOptionalCheck->SetValue(typeAttr && std::string(typeAttr.as_string()) == "optional");

		pugi::xml_attribute groundAttr = borderNode.attribute("ground");
		m_isGroundCheck->SetValue(groundAttr && std::string(groundAttr.as_string()) == "true");

		pugi::xml_attribute groupAttr = borderNode.attribute("group");
		m_groupCtrl->SetValue(groupAttr ? groupAttr.as_int() : 0);

		// Get the comment for the name
		pugi::xml_node commentNode = borderNode.previous_sibling();
		if (commentNode && commentNode.type() == pugi::node_comment) {
			std::string description = commentNode.value();
			size_t first = description.find_first_not_of(" \t\n\r");
			if (first != std::string::npos) {
				description = description.substr(first);
				size_t last = description.find_last_not_of(" \t\n\r");
				if (last != std::string::npos) {
					description = description.substr(0, last + 1);
				}
			} else {
				description.clear();
			}
			m_nameCtrl->SetValue(wxString(description));
		} else {
			m_nameCtrl->SetValue("");
		}

		// Load all border items
		for (pugi::xml_node itemNode = borderNode.child("borderitem"); itemNode; itemNode = itemNode.next_sibling("borderitem")) {
			pugi::xml_attribute edgeAttr = itemNode.attribute("edge");
			pugi::xml_attribute itemAttr = itemNode.attribute("item");

			if (!edgeAttr || !itemAttr) continue;

			BorderEdgePosition pos = edgeStringToPosition(edgeAttr.as_string());
			uint16_t itemId = itemAttr.as_uint();

			if (pos != EDGE_NONE && itemId > 0) {
				m_borderItems.push_back(BorderItem(pos, itemId));
				m_gridPanel->SetItemId(pos, itemId);
			}
		}

		break;
	}

	UpdatePreview();
	m_existingBordersCombo->SetSelection(selection);
}

void BorderEditorDialog::ApplyItemToPosition(BorderEdgePosition pos, uint16_t itemId) {
	if (pos == EDGE_NONE || itemId == 0) return;

	if (m_itemIdCtrl) {
		m_itemIdCtrl->SetValue(itemId);
	}

	bool updated = false;
	for (size_t i = 0; i < m_borderItems.size(); i++) {
		if (m_borderItems[i].position == pos) {
			m_borderItems[i].itemId = itemId;
			updated = true;
			break;
		}
	}

	if (!updated) {
		m_borderItems.push_back(BorderItem(pos, itemId));
	}

	m_gridPanel->SetItemId(pos, itemId);
	UpdatePreview();
}

void BorderEditorDialog::OnPositionSelected(wxCommandEvent& event) {
	BorderEdgePosition pos = static_cast<BorderEdgePosition>(event.GetInt());

	// Get the item ID from the current brush
	Brush* currentBrush = g_gui.GetCurrentBrush();
	uint16_t itemId = 0;

	if (currentBrush) {
		itemId = GetItemIDFromBrush(currentBrush);
	}

	if (itemId > 0) {
		ApplyItemToPosition(pos, itemId);
	} else {
		// Fallback to the item ID control
		itemId = m_itemIdCtrl->GetValue();
		if (itemId > 0) {
			ApplyItemToPosition(pos, itemId);
		} else {
			wxMessageBox("Please select a brush or enter an item ID first.", "No Item", wxICON_INFORMATION);
		}
	}
}

void BorderEditorDialog::OnAddItem(wxCommandEvent& event) {
	BorderEdgePosition selectedPos = m_gridPanel->GetSelectedPosition();

	if (selectedPos == EDGE_NONE) {
		wxMessageBox("Please select a position on the grid first by clicking on it.", "Error", wxICON_ERROR);
		return;
	}

	uint16_t itemId = m_itemIdCtrl->GetValue();
	if (itemId == 0) {
		wxMessageBox("Please enter a valid item ID or use the Browse button.", "Error", wxICON_ERROR);
		return;
	}

	ApplyItemToPosition(selectedPos, itemId);
}

void BorderEditorDialog::OnBrowse(wxCommandEvent& event) {
	FindItemDialog dialog(this, "Select Border Item");

	if (dialog.ShowModal() == wxID_OK) {
		uint16_t itemId = dialog.getResultID();
		if (m_itemIdCtrl && itemId > 0) {
			m_itemIdCtrl->SetValue(itemId);
		}
	}
}

void BorderEditorDialog::OnClear(wxCommandEvent& event) {
	if (m_activeTab == 0) {
		ClearItems();
	} else {
		ClearGroundItems();
	}
}

void BorderEditorDialog::ClearItems() {
	m_borderItems.clear();
	m_gridPanel->Clear();
	m_previewPanel->Clear();

	m_idCtrl->SetValue(m_nextBorderId);
	m_nameCtrl->SetValue("");
	m_isOptionalCheck->SetValue(false);
	m_isGroundCheck->SetValue(false);
	m_groupCtrl->SetValue(0);

	m_existingBordersCombo->SetSelection(0);
}

void BorderEditorDialog::UpdatePreview() {
	m_gridPanel->SetPreviewItems(m_borderItems);
}

bool BorderEditorDialog::ValidateBorder() {
	if (m_nameCtrl->GetValue().IsEmpty()) {
		wxMessageBox("Please enter a name for the border.", "Validation Error", wxICON_ERROR);
		return false;
	}

	if (m_borderItems.empty()) {
		wxMessageBox("The border must have at least one item.", "Validation Error", wxICON_ERROR);
		return false;
	}

	std::set<BorderEdgePosition> positions;
	for (const BorderItem& item : m_borderItems) {
		if (positions.find(item.position) != positions.end()) {
			wxMessageBox("The border contains duplicate positions.", "Validation Error", wxICON_ERROR);
			return false;
		}
		positions.insert(item.position);
	}

	int id = m_idCtrl->GetValue();
	if (id <= 0) {
		wxMessageBox("Border ID must be greater than 0.", "Validation Error", wxICON_ERROR);
		return false;
	}

	return true;
}

void BorderEditorDialog::SaveBorder() {
	if (!ValidateBorder()) return;

	int id = m_idCtrl->GetValue();
	wxString name = m_nameCtrl->GetValue();
	bool isOptional = m_isOptionalCheck->GetValue();
	bool isGround = m_isGroundCheck->GetValue();
	int group = m_groupCtrl->GetValue();

	wxString dataDir = GetVersionDataDirectory();
	wxString bordersFile = dataDir + "borders.xml";

	if (!wxFileExists(bordersFile)) {
		wxMessageBox("Cannot find borders.xml file in the data directory.", "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(bordersFile.ToStdString().c_str());
	if (!result) {
		wxMessageBox("Failed to load borders.xml: " + wxString(result.description()), "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		wxMessageBox("Invalid borders.xml file: missing 'materials' node", "Error", wxICON_ERROR);
		return;
	}

	// Check if a border with this ID already exists
	bool borderExists = false;
	pugi::xml_node existingBorder;

	for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
		pugi::xml_attribute idAttr = borderNode.attribute("id");
		if (idAttr && idAttr.as_int() == id) {
			borderExists = true;
			existingBorder = borderNode;
			break;
		}
	}

	if (borderExists) {
		pugi::xml_node commentNode = existingBorder.previous_sibling();
		bool hadComment = (commentNode && commentNode.type() == pugi::node_comment);

		if (wxMessageBox("A border with ID " + wxString::Format("%d", id) + " already exists. Do you want to overwrite it?",
				"Confirm Overwrite", wxYES_NO | wxICON_QUESTION) != wxYES) {
			return;
		}

		if (hadComment) {
			materials.remove_child(commentNode);
		}
		materials.remove_child(existingBorder);
	}

	// Add comment with the name
	if (!name.IsEmpty()) {
		materials.append_child(pugi::node_comment).set_value((" " + name.ToStdString() + " ").c_str());
	}

	// Create the new border node
	pugi::xml_node borderNode = materials.append_child("border");
	borderNode.append_attribute("id").set_value(id);

	if (isOptional) {
		borderNode.append_attribute("type").set_value("optional");
	}

	if (isGround) {
		borderNode.append_attribute("ground").set_value("true");
	}

	if (group > 0) {
		borderNode.append_attribute("group").set_value(group);
	}

	// Add all border items
	for (const BorderItem& item : m_borderItems) {
		pugi::xml_node itemNode = borderNode.append_child("borderitem");
		itemNode.append_attribute("edge").set_value(edgePositionToString(item.position).c_str());
		itemNode.append_attribute("item").set_value(item.itemId);
	}

	// Save the file
	if (!doc.save_file(bordersFile.ToStdString().c_str())) {
		wxMessageBox("Failed to save changes to borders.xml", "Error", wxICON_ERROR);
		return;
	}

	wxMessageBox("Border saved successfully.", "Success", wxICON_INFORMATION);

	// Reload the existing borders list
	LoadExistingBorders();
}

void BorderEditorDialog::OnSave(wxCommandEvent& event) {
	if (m_activeTab == 0) {
		SaveBorder();
	} else {
		SaveGroundBrush();
	}
}

void BorderEditorDialog::OnClose(wxCommandEvent& event) {
	Close();
}

void BorderEditorDialog::OnPageChanged(wxBookCtrlEvent& event) {
	m_activeTab = event.GetSelection();
}

// ============================================================================
// Ground tab handlers
// ============================================================================

void BorderEditorDialog::OnAddGroundItem(wxCommandEvent& event) {
	uint16_t itemId = m_groundItemIdCtrl->GetValue();
	int chance = m_groundItemChanceCtrl->GetValue();

	if (itemId == 0) {
		wxMessageBox("Please enter a valid item ID.", "Error", wxICON_ERROR);
		return;
	}

	m_groundItems.push_back(GroundItem(itemId, chance));
	UpdateGroundItemsList();
}

void BorderEditorDialog::OnRemoveGroundItem(wxCommandEvent& event) {
	int selection = m_groundItemsList->GetSelection();
	if (selection == wxNOT_FOUND) {
		wxMessageBox("Please select an item to remove.", "Error", wxICON_ERROR);
		return;
	}

	m_groundItems.erase(m_groundItems.begin() + selection);
	UpdateGroundItemsList();
}

void BorderEditorDialog::OnLoadGroundBrush(wxCommandEvent& event) {
	int selection = m_existingGroundBrushesCombo->GetSelection();
	if (selection <= 0) {
		ClearGroundItems();
		return;
	}

	wxStringClientData* data = static_cast<wxStringClientData*>(m_existingGroundBrushesCombo->GetClientObject(selection));
	if (!data) return;

	int serverId = wxAtoi(data->GetData());

	wxString dataDir = GetVersionDataDirectory();
	wxString groundsFile = dataDir + "grounds.xml";

	if (!wxFileExists(groundsFile)) return;

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(groundsFile.ToStdString().c_str());
	if (!result) return;

	ClearGroundItems();

	pugi::xml_node materials = doc.child("materials");
	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute serverLookIdAttr = brushNode.attribute("server_lookid");
		if (!serverLookIdAttr || serverLookIdAttr.as_int() != serverId) continue;

		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (nameAttr) {
			m_nameCtrl->SetValue(wxString(nameAttr.as_string()));
		}

		m_serverLookIdCtrl->SetValue(serverId);

		pugi::xml_attribute zOrderAttr = brushNode.attribute("z-order");
		if (zOrderAttr) {
			m_zOrderCtrl->SetValue(zOrderAttr.as_int());
		}

		// Load ground items
		for (pugi::xml_node itemNode = brushNode.child("item"); itemNode; itemNode = itemNode.next_sibling("item")) {
			pugi::xml_attribute idAttr = itemNode.attribute("id");
			pugi::xml_attribute chanceAttr = itemNode.attribute("chance");

			if (idAttr) {
				uint16_t itemId = idAttr.as_uint();
				int chance = chanceAttr ? chanceAttr.as_int() : 10;
				m_groundItems.push_back(GroundItem(itemId, chance));
			}
		}

		break;
	}

	UpdateGroundItemsList();
	m_existingGroundBrushesCombo->SetSelection(selection);
}

void BorderEditorDialog::OnGroundBrowse(wxCommandEvent& event) {
	FindItemDialog dialog(this, "Select Ground Item");

	if (dialog.ShowModal() == wxID_OK) {
		uint16_t itemId = dialog.getResultID();
		if (m_groundItemIdCtrl && itemId > 0) {
			m_groundItemIdCtrl->SetValue(itemId);
		}
	}
}

void BorderEditorDialog::ClearGroundItems() {
	m_groundItems.clear();
	m_nameCtrl->SetValue("");
	m_idCtrl->SetValue(m_nextBorderId);
	m_serverLookIdCtrl->SetValue(0);
	m_zOrderCtrl->SetValue(0);
	m_groundItemIdCtrl->SetValue(0);
	m_groundItemChanceCtrl->SetValue(10);
	UpdateGroundItemsList();
}

void BorderEditorDialog::UpdateGroundItemsList() {
	m_groundItemsList->Clear();

	for (const GroundItem& item : m_groundItems) {
		wxString itemText = wxString::Format("Item ID: %d, Chance: %d", item.itemId, item.chance);
		m_groundItemsList->Append(itemText);
	}
}

bool BorderEditorDialog::ValidateGroundBrush() {
	if (m_nameCtrl->GetValue().IsEmpty()) {
		wxMessageBox("Please enter a name for the ground brush.", "Validation Error", wxICON_ERROR);
		return false;
	}

	if (m_groundItems.empty()) {
		wxMessageBox("The ground brush must have at least one item.", "Validation Error", wxICON_ERROR);
		return false;
	}

	if (m_serverLookIdCtrl->GetValue() == 0) {
		wxMessageBox("Please enter a valid Server Look ID.", "Validation Error", wxICON_ERROR);
		return false;
	}

	return true;
}

void BorderEditorDialog::SaveGroundBrush() {
	if (!ValidateGroundBrush()) return;

	wxString name = m_nameCtrl->GetValue();
	int serverId = m_serverLookIdCtrl->GetValue();
	int zOrder = m_zOrderCtrl->GetValue();
	int borderId = m_idCtrl->GetValue();

	wxString dataDir = GetVersionDataDirectory();
	wxString groundsFile = dataDir + "grounds.xml";

	if (!wxFileExists(groundsFile)) {
		wxMessageBox("Cannot find grounds.xml file in the data directory.", "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(groundsFile.ToStdString().c_str());
	if (!result) {
		wxMessageBox("Failed to load grounds.xml: " + wxString(result.description()), "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		wxMessageBox("Invalid grounds.xml file: missing 'materials' node", "Error", wxICON_ERROR);
		return;
	}

	// Check if brush already exists
	bool brushExists = false;
	pugi::xml_node existingBrush;

	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (nameAttr && wxString(nameAttr.as_string()) == name) {
			brushExists = true;
			existingBrush = brushNode;
			break;
		}
	}

	if (brushExists) {
		if (wxMessageBox("A ground brush named '" + name + "' already exists. Do you want to overwrite it?",
				"Confirm Overwrite", wxYES_NO | wxICON_QUESTION) != wxYES) {
			return;
		}
		materials.remove_child(existingBrush);
	}

	// Create the new brush node
	pugi::xml_node brushNode = materials.append_child("brush");
	brushNode.append_attribute("name").set_value(name.ToStdString().c_str());
	brushNode.append_attribute("type").set_value("ground");
	brushNode.append_attribute("server_lookid").set_value(serverId);

	if (zOrder > 0) {
		brushNode.append_attribute("z-order").set_value(zOrder);
	}

	// Add ground items
	for (const GroundItem& item : m_groundItems) {
		pugi::xml_node itemNode = brushNode.append_child("item");
		itemNode.append_attribute("id").set_value(item.itemId);
		itemNode.append_attribute("chance").set_value(item.chance);
	}

	// Add border reference
	if (borderId > 0 && !m_borderItems.empty()) {
		pugi::xml_node borderRef = brushNode.append_child("border");
		borderRef.append_attribute("id").set_value(borderId);
	}

	// Save the file
	if (!doc.save_file(groundsFile.ToStdString().c_str())) {
		wxMessageBox("Failed to save changes to grounds.xml", "Error", wxICON_ERROR);
		return;
	}

	wxMessageBox("Ground brush saved successfully.", "Success", wxICON_INFORMATION);

	// Reload the existing ground brushes list
	LoadExistingGroundBrushes();
}

// ============================================================================
// BorderGridPanel
// ============================================================================

BorderGridPanel::BorderGridPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_SUNKEN) {
	m_items.clear();
	m_selectedPosition = EDGE_NONE;
	SetBackgroundStyle(wxBG_STYLE_PAINT);
}

BorderGridPanel::~BorderGridPanel() {
}

void BorderGridPanel::SetItemId(BorderEdgePosition pos, uint16_t itemId) {
	if (pos >= 0 && pos < EDGE_COUNT) {
		m_items[pos] = itemId;
		Refresh();
	}
}

uint16_t BorderGridPanel::GetItemId(BorderEdgePosition pos) const {
	auto it = m_items.find(pos);
	if (it != m_items.end()) {
		return it->second;
	}
	return 0;
}

void BorderGridPanel::Clear() {
	for (auto& item : m_items) {
		item.second = 0;
	}
	Refresh();
}

void BorderGridPanel::LoadSampleBorder() {
	m_sampleItems.clear();

	// Find parent dialog to get data directory
	wxWindow* parent = GetParent();
	while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}
	BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);
	if (!dialog) return;

	wxString dataDir = dialog->GetVersionDataDirectory();
	if (dataDir.IsEmpty()) return;

	wxString bordersFile = dataDir + "borders.xml";
	if (!wxFileExists(bordersFile)) return;

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(bordersFile.ToStdString().c_str());
	if (!result) return;

	pugi::xml_node materials = doc.child("materials");
	if (!materials) return;

	// Find the first border that has all 12 positions filled
	for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
		std::map<BorderEdgePosition, uint16_t> candidate;

		for (pugi::xml_node itemNode = borderNode.child("borderitem"); itemNode; itemNode = itemNode.next_sibling("borderitem")) {
			pugi::xml_attribute edgeAttr = itemNode.attribute("edge");
			pugi::xml_attribute itemAttr = itemNode.attribute("item");
			if (!edgeAttr || !itemAttr) continue;

			BorderEdgePosition pos = edgeStringToPosition(edgeAttr.as_string());
			uint16_t itemId = itemAttr.as_uint();
			if (pos != EDGE_NONE && itemId > 0) {
				candidate[pos] = itemId;
			}
		}

		if (candidate.size() >= 12) {
			m_sampleItems = candidate;
			return;
		}
	}

	// Fallback: use the first border with any items
	for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
		for (pugi::xml_node itemNode = borderNode.child("borderitem"); itemNode; itemNode = itemNode.next_sibling("borderitem")) {
			pugi::xml_attribute edgeAttr = itemNode.attribute("edge");
			pugi::xml_attribute itemAttr = itemNode.attribute("item");
			if (!edgeAttr || !itemAttr) continue;

			BorderEdgePosition pos = edgeStringToPosition(edgeAttr.as_string());
			uint16_t itemId = itemAttr.as_uint();
			if (pos != EDGE_NONE && itemId > 0) {
				m_sampleItems[pos] = itemId;
			}
		}
		if (!m_sampleItems.empty()) return;
	}
}

void BorderGridPanel::SetPreviewItems(const std::vector<BorderItem>& items) {
	m_previewItems = items;
	Refresh();
}

void BorderGridPanel::SetSelectedPosition(BorderEdgePosition pos) {
	m_selectedPosition = pos;
	Refresh();
}

void BorderGridPanel::OnPaint(wxPaintEvent& event) {
	wxAutoBufferedPaintDC dc(this);

	wxRect rect = GetClientRect();
	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);

	const int total_width = rect.GetWidth();
	const int total_height = rect.GetHeight();
	const int grid_cell_size = 64;
	const int CELL_PADDING = 4;

	// Layout: 3 grids on the left (~75%), preview on the right (~25%)
	const int preview_size = 3 * grid_cell_size; // 3x3 preview area
	const int grids_width = total_width - preview_size - 10;
	const int section_width = grids_width / 3;

	// Label height reservation
	const int label_height = 20;
	const int label_y = 6;
	const int grid_area_top = label_y + label_height + 4;
	const int grid_area_height = total_height - grid_area_top;

	// Section 1: Normal directions (N, E, S, W) - 2x2 grid
	const int normal_grid_width = 2 * grid_cell_size;
	const int normal_grid_height = 2 * grid_cell_size;
	const int normal_offset_x = (section_width - normal_grid_width) / 2;
	const int normal_offset_y = grid_area_top + (grid_area_height - normal_grid_height) / 2;

	// Section 2: Corner positions - 2x2 grid
	const int corner_grid_width = 2 * grid_cell_size;
	const int corner_grid_height = 2 * grid_cell_size;
	const int corner_offset_x = section_width + (section_width - corner_grid_width) / 2;
	const int corner_offset_y = grid_area_top + (grid_area_height - corner_grid_height) / 2;

	// Section 3: Diagonal positions - 2x2 grid
	const int diag_grid_width = 2 * grid_cell_size;
	const int diag_grid_height = 2 * grid_cell_size;
	const int diag_offset_x = 2 * section_width + (section_width - diag_grid_width) / 2;
	const int diag_offset_y = grid_area_top + (grid_area_height - diag_grid_height) / 2;

	// Section labels - drawn above the grids
	dc.SetTextForeground(Theme::Get(Theme::Role::Text));
	dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));

	dc.DrawText("Normal", normal_offset_x, label_y);
	dc.DrawText("Corner", corner_offset_x, label_y);
	dc.DrawText("Diagonal", diag_offset_x, label_y);

	// Draw grids
	auto drawGrid = [&](int offsetX, int offsetY, int gridSize, int cellSize) {
		for (int i = 0; i <= gridSize; i++) {
			dc.DrawLine(offsetX + i * cellSize, offsetY, offsetX + i * cellSize, offsetY + gridSize * cellSize);
			dc.DrawLine(offsetX, offsetY + i * cellSize, offsetX + gridSize * cellSize, offsetY + i * cellSize);
		}
	};

	drawGrid(normal_offset_x, normal_offset_y, 2, grid_cell_size);
	drawGrid(corner_offset_x, corner_offset_y, 2, grid_cell_size);
	drawGrid(diag_offset_x, diag_offset_y, 2, grid_cell_size);

	dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
	dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

	auto drawItemAtPos = [&](BorderEdgePosition pos, int gridX, int gridY, int offsetX, int offsetY) {
		int x = offsetX + gridX * grid_cell_size + CELL_PADDING;
		int y = offsetY + gridY * grid_cell_size + CELL_PADDING;
		int cellW = grid_cell_size - 2 * CELL_PADDING;
		int cellH = grid_cell_size - 2 * CELL_PADDING;

		if (pos == m_selectedPosition) {
			dc.SetPen(*wxRED_PEN);
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Selected)));
			dc.DrawRectangle(x - CELL_PADDING, y - CELL_PADDING, grid_cell_size, grid_cell_size);
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
		}

		uint16_t itemId = GetItemId(pos);
		if (itemId > 0) {
			const auto itemDef = g_item_definitions.get(itemId);
			if (itemDef) {
				Sprite* sprite = g_gui.gfx.getSprite(itemDef.clientId());
				if (sprite) {
					sprite->DrawTo(&dc, SPRITE_SIZE_32x32, x, y, cellW, cellH);
				}
			}
		} else {
			// Empty slot: draw sample sprite with reduced opacity
			auto sampleIt = m_sampleItems.find(pos);
			if (sampleIt != m_sampleItems.end() && sampleIt->second > 0) {
				const auto sampleDef = g_item_definitions.get(sampleIt->second);
				if (sampleDef) {
					Sprite* sampleSprite = g_gui.gfx.getSprite(sampleDef.clientId());
					if (sampleSprite) {
						// Draw to a temp bitmap, then blend at low alpha
						wxBitmap tmpBmp(cellW, cellH, 32);
						wxMemoryDC tmpDc(tmpBmp);
						tmpDc.SetBackground(dc.GetBackground());
						tmpDc.Clear();
						sampleSprite->DrawTo(&tmpDc, SPRITE_SIZE_32x32, 0, 0, cellW, cellH);
						tmpDc.SelectObject(wxNullBitmap);

						wxImage img = tmpBmp.ConvertToImage();
						if (!img.HasAlpha()) img.InitAlpha();
						unsigned char* alpha = img.GetAlpha();
						int total = img.GetWidth() * img.GetHeight();
						for (int i = 0; i < total; ++i) {
							alpha[i] = static_cast<unsigned char>(alpha[i] * 0.35f);
						}
						dc.DrawBitmap(wxBitmap(img, 32), x, y, true);
					}
				}
			}
		}

		// Draw position label at bottom
		wxString label = wxString(edgePositionToString(pos));
		wxSize textSize = dc.GetTextExtent(label);
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		dc.DrawText(label, x + (cellW - textSize.GetWidth()) / 2,
			y + cellH - textSize.GetHeight());
	};

	// Draw normal direction items
	drawItemAtPos(EDGE_N, 0, 0, normal_offset_x, normal_offset_y);
	drawItemAtPos(EDGE_E, 1, 0, normal_offset_x, normal_offset_y);
	drawItemAtPos(EDGE_S, 0, 1, normal_offset_x, normal_offset_y);
	drawItemAtPos(EDGE_W, 1, 1, normal_offset_x, normal_offset_y);

	// Draw corner items
	drawItemAtPos(EDGE_CNW, 0, 0, corner_offset_x, corner_offset_y);
	drawItemAtPos(EDGE_CNE, 1, 0, corner_offset_x, corner_offset_y);
	drawItemAtPos(EDGE_CSW, 0, 1, corner_offset_x, corner_offset_y);
	drawItemAtPos(EDGE_CSE, 1, 1, corner_offset_x, corner_offset_y);

	// Draw diagonal items
	drawItemAtPos(EDGE_DNW, 0, 0, diag_offset_x, diag_offset_y);
	drawItemAtPos(EDGE_DNE, 1, 0, diag_offset_x, diag_offset_y);
	drawItemAtPos(EDGE_DSW, 0, 1, diag_offset_x, diag_offset_y);
	drawItemAtPos(EDGE_DSE, 1, 1, diag_offset_x, diag_offset_y);

	// ========== Preview (right side) ==========
	// 5x5 grid: center 3x3 is ground, border items ring around it.
	// Corners and diagonals share the same corner cells (stacked).
	//
	//   Row 0:  CSW        S          CSE
	//   Row 1:  W    DNW   [ground]  DNE   E
	//   Row 2:       [ground][ground][ground]
	//   Row 3:  W    DSW   [ground]  DSE   E
	//   Row 4:  CNW        N          CNE
	//
	// But actually the standard Tibia layout is:
	//   (0,0) CNW+DNW  (1,0) N       (2,0) CNE+DNE
	//   (0,1) W        (1,1) ground  (2,1) E
	//   (0,2) CSW+DSW  (1,2) S       (2,2) CSE+DSE
	//
	// We use 5x5 to show it more spread out with ground filling center 3x3:
	//   col:  0       1       2       3       4
	//   r0:  CSW     S       S       S       CSE
	//   r1:  W      DNW    ground   DNE      E
	//   r2:  W     ground  ground  ground    E
	//   r3:  W      DSW   ground   DSE       E
	//   r4:  CNW     N       N       N       CNE

	const int preview_grid = 5;
	const int preview_cell = preview_size / preview_grid;
	const int preview_x = grids_width + 5;
	const int preview_total = preview_grid * preview_cell;
	const int preview_y = grid_area_top + (grid_area_height - preview_total) / 2;

	// Preview label
	dc.SetTextForeground(Theme::Get(Theme::Role::Text));
	dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
	dc.DrawText("Preview", preview_x, label_y);

	// Draw preview grid lines
	dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	for (int i = 0; i <= preview_grid; i++) {
		dc.DrawLine(preview_x + i * preview_cell, preview_y, preview_x + i * preview_cell, preview_y + preview_total);
		dc.DrawLine(preview_x, preview_y + i * preview_cell, preview_x + preview_total, preview_y + i * preview_cell);
	}

	// Draw center 3x3 ground block
	dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Success)));
	dc.SetPen(*wxTRANSPARENT_PEN);
	for (int gy = 1; gy <= 3; ++gy) {
		for (int gx = 1; gx <= 3; ++gx) {
			dc.DrawRectangle(preview_x + gx * preview_cell, preview_y + gy * preview_cell, preview_cell, preview_cell);
		}
	}

	// Helper to draw a sprite at a grid cell
	auto drawPrev = [&](uint16_t itemId, int gx, int gy) {
		if (itemId == 0) return;
		const auto def = g_item_definitions.get(itemId);
		if (!def) return;
		Sprite* spr = g_gui.gfx.getSprite(def.clientId());
		if (!spr) return;
		spr->DrawTo(&dc, SPRITE_SIZE_32x32,
			preview_x + gx * preview_cell, preview_y + gy * preview_cell,
			preview_cell, preview_cell);
	};

	// Build lookup
	std::map<BorderEdgePosition, uint16_t> previewMap;
	for (const BorderItem& item : m_previewItems) {
		previewMap[item.position] = item.itemId;
	}

	// Layout reference (looking from outside toward the ground center):
	//
	//   (0,0)       (1,0)       (2,0)       (3,0)       (4,0)
	//   CSW [2]     S [1]       S [1]       S [1]       CSE [2]
	//
	//   (0,1)       (1,1)       (2,1)       (3,1)       (4,1)
	//   E [1]       DSE [3]     ground      DSW [3]     W [1]
	//
	//   (0,2)       (1,2)       (2,2)       (3,2)       (4,2)
	//   E [1]       ground      ground      ground      W [1]
	//
	//   (0,3)       (1,3)       (2,3)       (3,3)       (4,3)
	//   E [1]       DNE [3]     ground      DNW [3]     W [1]
	//
	//   (0,4)       (1,4)       (2,4)       (3,4)       (4,4)
	//   CNE [2]     N [1]       N [1]       N [1]       CNW [2]

	// Draw debug numbers on each cell
	auto drawNum = [&](int num, int gx, int gy) {
		dc.SetTextForeground(wxColour(255, 80, 80));
		dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
		wxString label = wxString::Format("%d", num);
		wxSize ts = dc.GetTextExtent(label);
		dc.DrawText(label,
			preview_x + gx * preview_cell + (preview_cell - ts.GetWidth()) / 2,
			preview_y + gy * preview_cell + (preview_cell - ts.GetHeight()) / 2);
	};

	// Row 0:  [empty]  CSE      S        CSW      [empty]
	drawPrev(previewMap[EDGE_CSE], 1, 0);
	drawPrev(previewMap[EDGE_S],   2, 0);
	drawPrev(previewMap[EDGE_CSW], 3, 0);

	// Row 1:  CSE     DSE      ground   DSW      CSW
	drawPrev(previewMap[EDGE_CSE], 0, 1);
	drawPrev(previewMap[EDGE_DSE], 1, 1);
	drawPrev(previewMap[EDGE_DSW], 3, 1);
	drawPrev(previewMap[EDGE_CSW], 4, 1);

	// Row 2:  E       ground   ground   ground   W
	drawPrev(previewMap[EDGE_E],   0, 2);
	drawPrev(previewMap[EDGE_W],   4, 2);

	// Row 3:  CNE     DNE      ground   DNW      CNW
	drawPrev(previewMap[EDGE_CNE], 0, 3);
	drawPrev(previewMap[EDGE_DNE], 1, 3);
	drawPrev(previewMap[EDGE_DNW], 3, 3);
	drawPrev(previewMap[EDGE_CNW], 4, 3);

	// Row 4:  [empty]  CNE      N        CNW      [empty]
	drawPrev(previewMap[EDGE_CNE], 1, 4);
	drawPrev(previewMap[EDGE_N],   2, 4);
	drawPrev(previewMap[EDGE_CNW], 3, 4);
}

BorderEdgePosition BorderGridPanel::GetPositionFromCoordinates(int x, int y) const {
	wxSize size = GetClientSize();
	const int total_width = size.GetWidth();
	const int total_height = size.GetHeight();
	const int grid_cell_size = 64;

	const int preview_size = 3 * grid_cell_size;
	const int grids_width = total_width - preview_size - 10;
	const int section_width = grids_width / 3;

	const int grid_area_top = 30;
	const int grid_area_height = total_height - grid_area_top;

	const int normal_grid_width = 2 * grid_cell_size;
	const int normal_grid_height = 2 * grid_cell_size;
	const int normal_offset_x = (section_width - normal_grid_width) / 2;
	const int normal_offset_y = grid_area_top + (grid_area_height - normal_grid_height) / 2;

	const int corner_grid_width = 2 * grid_cell_size;
	const int corner_grid_height = 2 * grid_cell_size;
	const int corner_offset_x = section_width + (section_width - corner_grid_width) / 2;
	const int corner_offset_y = grid_area_top + (grid_area_height - corner_grid_height) / 2;

	const int diag_grid_width = 2 * grid_cell_size;
	const int diag_grid_height = 2 * grid_cell_size;
	const int diag_offset_x = 2 * section_width + (section_width - diag_grid_width) / 2;
	const int diag_offset_y = grid_area_top + (grid_area_height - diag_grid_height) / 2;

	// Normal grid
	if (x >= normal_offset_x && x < normal_offset_x + normal_grid_width &&
		y >= normal_offset_y && y < normal_offset_y + normal_grid_height) {
		int gridX = (x - normal_offset_x) / grid_cell_size;
		int gridY = (y - normal_offset_y) / grid_cell_size;
		if (gridX == 0 && gridY == 0) return EDGE_N;
		if (gridX == 1 && gridY == 0) return EDGE_E;
		if (gridX == 0 && gridY == 1) return EDGE_S;
		if (gridX == 1 && gridY == 1) return EDGE_W;
	}

	// Corner grid
	if (x >= corner_offset_x && x < corner_offset_x + corner_grid_width &&
		y >= corner_offset_y && y < corner_offset_y + corner_grid_height) {
		int gridX = (x - corner_offset_x) / grid_cell_size;
		int gridY = (y - corner_offset_y) / grid_cell_size;
		if (gridX == 0 && gridY == 0) return EDGE_CNW;
		if (gridX == 1 && gridY == 0) return EDGE_CNE;
		if (gridX == 0 && gridY == 1) return EDGE_CSW;
		if (gridX == 1 && gridY == 1) return EDGE_CSE;
	}

	// Diagonal grid
	if (x >= diag_offset_x && x < diag_offset_x + diag_grid_width &&
		y >= diag_offset_y && y < diag_offset_y + diag_grid_height) {
		int gridX = (x - diag_offset_x) / grid_cell_size;
		int gridY = (y - diag_offset_y) / grid_cell_size;
		if (gridX == 0 && gridY == 0) return EDGE_DNW;
		if (gridX == 1 && gridY == 0) return EDGE_DNE;
		if (gridX == 0 && gridY == 1) return EDGE_DSW;
		if (gridX == 1 && gridY == 1) return EDGE_DSE;
	}

	return EDGE_NONE;
}

void BorderGridPanel::OnMouseClick(wxMouseEvent& event) {
	int x = event.GetX();
	int y = event.GetY();

	BorderEdgePosition pos = GetPositionFromCoordinates(x, y);
	if (pos != EDGE_NONE) {
		SetSelectedPosition(pos);

		wxCommandEvent selEvent(wxEVT_COMMAND_BUTTON_CLICKED, ID_BORDER_GRID_SELECT);
		selEvent.SetInt(static_cast<int>(pos));

		// Find the parent BorderEditorDialog
		wxWindow* parent = GetParent();
		while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
			parent = parent->GetParent();
		}

		BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);
		if (dialog) {
			dialog->OnPositionSelected(selEvent);
		}
	}
}

void BorderGridPanel::OnMouseDown(wxMouseEvent& event) {
	BorderEdgePosition pos = GetPositionFromCoordinates(event.GetX(), event.GetY());
	SetSelectedPosition(pos);
	event.Skip();
}

void BorderGridPanel::OnRightClick(wxMouseEvent& event) {
	BorderEdgePosition pos = GetPositionFromCoordinates(event.GetX(), event.GetY());
	if (pos == EDGE_NONE) return;

	uint16_t itemId = GetItemId(pos);
	if (itemId == 0) return;

	// Remove the item from this position
	m_items.erase(pos);
	SetSelectedPosition(EDGE_NONE);

	// Remove from parent dialog's border items list and update preview
	wxWindow* parent = GetParent();
	while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}

	BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);
	if (dialog) {
		auto& items = dialog->m_borderItems;
		items.erase(
			std::remove_if(items.begin(), items.end(),
				[pos](const BorderItem& item) { return item.position == pos; }),
			items.end());
		dialog->UpdatePreview();
	}

	Refresh();
}

// ============================================================================
// BorderPreviewPanel
// ============================================================================

BorderPreviewPanel::BorderPreviewPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxSize(BORDER_PREVIEW_SIZE, BORDER_PREVIEW_SIZE)) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
}

BorderPreviewPanel::~BorderPreviewPanel() {
}

void BorderPreviewPanel::SetBorderItems(const std::vector<BorderItem>& items) {
	m_borderItems = items;
	Refresh();
}

void BorderPreviewPanel::Clear() {
	m_borderItems.clear();
	Refresh();
}

void BorderPreviewPanel::OnPaint(wxPaintEvent& event) {
	wxAutoBufferedPaintDC dc(this);

	wxRect rect = GetClientRect();
	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	const int GRID_SIZE = 5;
	const int preview_cell_size = BORDER_PREVIEW_SIZE / GRID_SIZE;

	// Draw grid lines
	dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
	for (int i = 0; i <= GRID_SIZE; i++) {
		dc.DrawLine(i * preview_cell_size, 0, i * preview_cell_size, BORDER_PREVIEW_SIZE);
		dc.DrawLine(0, i * preview_cell_size, BORDER_PREVIEW_SIZE, i * preview_cell_size);
	}

	// Draw a sample ground tile in the center
	dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Success)));
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.DrawRectangle((GRID_SIZE / 2) * preview_cell_size, (GRID_SIZE / 2) * preview_cell_size, preview_cell_size, preview_cell_size);

	// Draw border items around the center
	for (const BorderItem& item : m_borderItems) {
		wxPoint offset(0, 0);

		switch (item.position) {
			case EDGE_N: offset = wxPoint(0, -1); break;
			case EDGE_E: offset = wxPoint(1, 0); break;
			case EDGE_S: offset = wxPoint(0, 1); break;
			case EDGE_W: offset = wxPoint(-1, 0); break;
			case EDGE_CNW: offset = wxPoint(-1, -1); break;
			case EDGE_CNE: offset = wxPoint(1, -1); break;
			case EDGE_CSE: offset = wxPoint(1, 1); break;
			case EDGE_CSW: offset = wxPoint(-1, 1); break;
			case EDGE_DNW: offset = wxPoint(-1, -1); break;
			case EDGE_DNE: offset = wxPoint(1, -1); break;
			case EDGE_DSE: offset = wxPoint(1, 1); break;
			case EDGE_DSW: offset = wxPoint(-1, 1); break;
			default: continue;
		}

		int x = (GRID_SIZE / 2 + offset.x) * preview_cell_size;
		int y = (GRID_SIZE / 2 + offset.y) * preview_cell_size;

		const auto itemDef = g_item_definitions.get(item.itemId);
		if (itemDef) {
			Sprite* sprite = g_gui.gfx.getSprite(itemDef.clientId());
			if (sprite) {
				sprite->DrawTo(&dc, SPRITE_SIZE_32x32, x, y, preview_cell_size, preview_cell_size);
			}
		}
	}
}
