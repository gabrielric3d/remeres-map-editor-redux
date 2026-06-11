//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Border Editor Dialog - Visual editor for auto borders
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/dialogs/border_editor_dialog.h"
#include "ui/dialogs/border_scan_dialog.h"
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
#include <wx/listbox.h>
#include <wx/radiobox.h>
#include <wx/dcbuffer.h>
#include <wx/dcmemory.h>
#include <wx/dnd.h>
#include <wx/statline.h>
#include <sstream>
#include <algorithm>
#include <set>
#include <vector>
#include <string>

#define BORDER_PREVIEW_SIZE 192
#define ID_BORDER_GRID_SELECT wxID_HIGHEST + 1
#define ID_GROUND_ITEM_LIST wxID_HIGHEST + 2
#define ID_UPDATE_CHANCE wxID_HIGHEST + 3
#define ID_UPDATE_GROUND_CHANCE wxID_HIGHEST + 4
#define ID_ADD_GROUND_BORDER wxID_HIGHEST + 5
#define ID_REMOVE_GROUND_BORDER wxID_HIGHEST + 6
#define ID_EXISTING_BORDERS_COMBO wxID_HIGHEST + 7
#define ID_EXISTING_GROUNDS_COMBO wxID_HIGHEST + 8
#define ID_GROUND_BORDER_ID_COMBO wxID_HIGHEST + 9
#define ID_ADD_TO_TILESET wxID_HIGHEST + 10
#define ID_MOVE_GROUND_BORDER_UP wxID_HIGHEST + 11
#define ID_MOVE_GROUND_BORDER_DOWN wxID_HIGHEST + 12
#define ID_FIND_BORDER_BY_ITEM_ID wxID_HIGHEST + 13
#define ID_FIND_GROUND_BY_ITEM_ID wxID_HIGHEST + 14
#define ID_TILESET_COMBO wxID_HIGHEST + 15
#define ID_TILESET_BRUSH_LIST wxID_HIGHEST + 16
#define ID_GROUND_BORDER_TO_COMBO wxID_HIGHEST + 17
#define ID_MODIFY_GROUND_BORDER wxID_HIGHEST + 18
#define ID_BORDER_SCAN wxID_HIGHEST + 19

// ============================================================================
// Local layout helpers
// ============================================================================

namespace {

// Minimalist section header: bold accent-colored "▸ TITLE", no box.
// Followed in callers by a thin horizontal separator for visual structure.
wxStaticText* MakeSectionHeader(wxWindow* parent, const wxString& title) {
	wxStaticText* h = newd wxStaticText(parent, wxID_ANY, wxString(wxT("▸ ")) + title.Upper());
	wxFont f = h->GetFont();
	f.SetWeight(wxFONTWEIGHT_BOLD);
	h->SetFont(f);
	h->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	return h;
}

// Adds a header + thin separator pair to a vertical sizer.
void AddSectionHeader(wxBoxSizer* sizer, wxWindow* parent, const wxString& title) {
	sizer->Add(MakeSectionHeader(parent, title), 0, wxLEFT | wxRIGHT | wxTOP, 6);
	sizer->Add(newd wxStaticLine(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
}

} // namespace

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

static std::string TrimWhitespace(const std::string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

static std::string ExtractLegacyBorderName(const std::string& pcdataValue) {
	// Legacy files store the border name as Lua-style `-- name --` PCDATA inside
	// <border>. Recover that name so we can promote it to a real XML comment and
	// strip the dead text from inside the element.
	std::string trimmed = TrimWhitespace(pcdataValue);
	if (trimmed.size() < 4) return "";
	if (trimmed.substr(0, 2) != "--") return "";
	if (trimmed.substr(trimmed.size() - 2) != "--") return "";
	return TrimWhitespace(trimmed.substr(2, trimmed.size() - 4));
}

// Cleans PCDATA accumulation and legacy `-- name --` text from every <border>
// in `materials`. Without this, pugixml re-emits the preserved PCDATA (plus a
// fresh indent prefix) on every save, so each round trip multiplies the blank
// lines inside the border.
static void NormalizeBordersDocument(pugi::xml_node materials) {
	if (!materials) return;

	for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
		std::vector<pugi::xml_node> pcdataChildren;
		std::string extractedName;

		for (pugi::xml_node child = borderNode.first_child(); child; child = child.next_sibling()) {
			if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
				pcdataChildren.push_back(child);
				if (extractedName.empty()) {
					std::string name = ExtractLegacyBorderName(child.value());
					if (!name.empty()) extractedName = name;
				}
			}
		}

		if (pcdataChildren.empty()) continue;

		if (!extractedName.empty()) {
			pugi::xml_node prev = borderNode.previous_sibling();
			bool hasComment = (prev && prev.type() == pugi::node_comment);
			if (!hasComment) {
				pugi::xml_node newComment = materials.insert_child_before(pugi::node_comment, borderNode);
				newComment.set_value((" " + extractedName + " ").c_str());
			}
		}

		for (pugi::xml_node& pc : pcdataChildren) {
			borderNode.remove_child(pc);
		}
	}
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
// GroundItemsDropTarget
// ============================================================================

GroundItemsDropTarget::GroundItemsDropTarget(GroundItemsPanel* panel) : m_panel(panel) {
}

bool GroundItemsDropTarget::OnDropText(wxCoord x, wxCoord y, const wxString& data) {
	if (!m_panel) return false;

	wxString itemData = data;
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

	m_panel->AddItemFromDrop(static_cast<uint16_t>(idVal));
	return true;
}

// ============================================================================
// Event tables
// ============================================================================

BEGIN_EVENT_TABLE(BorderEditorDialog, wxPanel)
	EVT_BUTTON(wxID_ADD, BorderEditorDialog::OnAddItem)
	EVT_BUTTON(ID_UPDATE_CHANCE, BorderEditorDialog::OnUpdateChance)
	EVT_BUTTON(wxID_CLEAR, BorderEditorDialog::OnClear)
	EVT_BUTTON(wxID_SAVE, BorderEditorDialog::OnSave)
	EVT_BUTTON(wxID_FIND, BorderEditorDialog::OnBrowse)
	EVT_COMBOBOX(ID_EXISTING_BORDERS_COMBO, BorderEditorDialog::OnLoadBorder)
	EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, BorderEditorDialog::OnPageChanged)
	EVT_BUTTON(wxID_ADD + 100, BorderEditorDialog::OnAddGroundItem)
	EVT_BUTTON(ID_UPDATE_GROUND_CHANCE, BorderEditorDialog::OnUpdateGroundChance)
	EVT_BUTTON(wxID_FIND + 100, BorderEditorDialog::OnGroundBrowse)
	EVT_COMBOBOX(ID_EXISTING_GROUNDS_COMBO, BorderEditorDialog::OnLoadGroundBrush)
	EVT_TEXT_ENTER(ID_EXISTING_GROUNDS_COMBO, BorderEditorDialog::OnLoadGroundBrush)
	EVT_TEXT(ID_EXISTING_GROUNDS_COMBO, BorderEditorDialog::OnGroundLoadTextChanged)
	EVT_COMBOBOX(ID_GROUND_BORDER_TO_COMBO, BorderEditorDialog::OnGroundBorderToChanged)
	EVT_TEXT(ID_GROUND_BORDER_TO_COMBO, BorderEditorDialog::OnGroundBorderToChanged)
	EVT_BUTTON(ID_ADD_GROUND_BORDER, BorderEditorDialog::OnAddGroundBorder)
	EVT_BUTTON(ID_MODIFY_GROUND_BORDER, BorderEditorDialog::OnModifyGroundBorder)
	EVT_BUTTON(ID_REMOVE_GROUND_BORDER, BorderEditorDialog::OnRemoveGroundBorder)
	EVT_COMBOBOX(ID_GROUND_BORDER_ID_COMBO, BorderEditorDialog::OnGroundBorderIdChanged)
	EVT_TEXT(ID_GROUND_BORDER_ID_COMBO, BorderEditorDialog::OnGroundBorderIdChanged)
	EVT_BUTTON(ID_ADD_TO_TILESET, BorderEditorDialog::OnAddGroundToTileset)
	EVT_COMBOBOX(ID_TILESET_COMBO, BorderEditorDialog::OnTilesetSelectionChanged)
	EVT_TEXT(ID_TILESET_COMBO, BorderEditorDialog::OnTilesetSelectionChanged)
	EVT_BUTTON(ID_MOVE_GROUND_BORDER_UP, BorderEditorDialog::OnMoveGroundBorderUp)
	EVT_BUTTON(ID_MOVE_GROUND_BORDER_DOWN, BorderEditorDialog::OnMoveGroundBorderDown)
	EVT_BUTTON(ID_FIND_BORDER_BY_ITEM_ID, BorderEditorDialog::OnFindBorderByItemId)
	EVT_BUTTON(ID_FIND_GROUND_BY_ITEM_ID, BorderEditorDialog::OnFindGroundByItemId)
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

BEGIN_EVENT_TABLE(EdgeItemsPanel, wxPanel)
	EVT_PAINT(EdgeItemsPanel::OnPaint)
	EVT_LEFT_UP(EdgeItemsPanel::OnMouseClick)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(GroundItemsPanel, wxPanel)
	EVT_PAINT(GroundItemsPanel::OnPaint)
	EVT_LEFT_UP(GroundItemsPanel::OnMouseClick)
	EVT_SIZE(GroundItemsPanel::OnSize)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(BorderNorthPreview, wxPanel)
	EVT_PAINT(BorderNorthPreview::OnPaint)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(GroundBordersPanel, wxPanel)
	EVT_PAINT(GroundBordersPanel::OnPaint)
	EVT_LEFT_UP(GroundBordersPanel::OnMouseClick)
	EVT_LEFT_DCLICK(GroundBordersPanel::OnMouseDClick)
	EVT_SIZE(GroundBordersPanel::OnSize)
END_EVENT_TABLE()

// ============================================================================
// BorderEditorDialog
// ============================================================================

BorderEditorDialog::BorderEditorDialog(wxWindow* parent) :
	wxPanel(parent, wxID_ANY),
	m_nextBorderId(1),
	m_activeTab(0) {

	SetBackgroundColour(Theme::Get(Theme::Role::Surface));

	// Compact font (-1pt) cascades to every child control unless they override it.
	// Keeps the dialog manageable when users have many panels open behind it.
	wxFont compactFont = GetFont();
	compactFont.SetPointSize(std::max(6, compactFont.GetPointSize() - 1));
	SetFont(compactFont);

	CreateGUIControls();
	LoadExistingBorders();
	// Ground tab data (grounds.xml + tilesets.xml) is loaded lazily on first tab switch —
	// saves ~hundreds of ms when the user just wants to edit borders.

	m_gridPanel->LoadSampleBorder();

	m_idCtrl->SetValue(m_nextBorderId);
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

	// ============================================================================
	// Inner notebook (Border / Ground sub-tabs)
	// ============================================================================
	m_notebook = newd wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);
	wxFont tabFont = m_notebook->GetFont();
	tabFont.SetPointSize(tabFont.GetPointSize() + 3);
	tabFont.SetWeight(wxFONTWEIGHT_BOLD);
	m_notebook->SetFont(tabFont);
	m_notebook->SetPadding(wxSize(16, 8));

	// ============================================================================
	// BORDER TAB — 2-column layout (Grid 60% / Edge Items 40%)
	// ============================================================================
	m_borderPanel = newd wxPanel(m_notebook);
	wxBoxSizer* borderSizer = newd wxBoxSizer(wxVERTICAL);

	// --- Properties bar (single row, no box) ---
	wxBoxSizer* borderPropsRow = newd wxBoxSizer(wxHORIZONTAL);

	borderPropsRow->Add(newd wxStaticText(m_borderPanel, wxID_ANY, "Group:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_groupCtrl = newd wxSpinCtrl(m_borderPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 0, 65535);
	m_groupCtrl->SetToolTip("Optional group identifier (0 = no group)");
	borderPropsRow->Add(m_groupCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

	m_isOptionalCheck = newd wxCheckBox(m_borderPanel, wxID_ANY, "Optional");
	m_isOptionalCheck->SetToolTip("Marks this border as optional");
	borderPropsRow->Add(m_isOptionalCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	m_isGroundCheck = newd wxCheckBox(m_borderPanel, wxID_ANY, "Ground");
	m_isGroundCheck->SetToolTip("Marks this border as a ground border");
	borderPropsRow->Add(m_isGroundCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

	borderPropsRow->Add(newd wxStaticText(m_borderPanel, wxID_ANY, "Load:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_existingBordersCombo = newd wxComboBox(m_borderPanel, ID_EXISTING_BORDERS_COMBO, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY | wxCB_DROPDOWN);
	m_existingBordersCombo->SetToolTip("Load an existing border as template");
	borderPropsRow->Add(m_existingBordersCombo, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

	borderPropsRow->Add(newd wxStaticText(m_borderPanel, wxID_ANY, "Find ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_findBorderItemIdCtrl = newd wxSpinCtrl(m_borderPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535);
	m_findBorderItemIdCtrl->SetToolTip("Enter an item ID to locate the border that uses it");
	borderPropsRow->Add(m_findBorderItemIdCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	wxButton* findBorderButton = newd wxButton(m_borderPanel, ID_FIND_BORDER_BY_ITEM_ID, "Find", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	findBorderButton->SetToolTip("Find and load the border that uses this item ID");
	borderPropsRow->Add(findBorderButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	m_scanButton = newd wxButton(m_borderPanel, ID_BORDER_SCAN, "Scan...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	if (g_gui.gfx.isUnloaded()) {
		m_scanButton->Enable(false);
		m_scanButton->SetToolTip("Requires a loaded client (sprites)");
	} else {
		m_scanButton->SetToolTip("Classify candidate items into border edges by sprite shape");
	}
	m_scanButton->Bind(wxEVT_BUTTON, &BorderEditorDialog::OnScanBorder, this);
	borderPropsRow->Add(m_scanButton, 0, wxALIGN_CENTER_VERTICAL);

	borderSizer->Add(borderPropsRow, 0, wxEXPAND | wxALL, 8);

	// --- Two columns: Grid (left 60%) | Edge Items (right 40%) ---
	wxBoxSizer* borderColsRow = newd wxBoxSizer(wxHORIZONTAL);

	// LEFT — Border Grid (canvas)
	wxBoxSizer* borderLeftCol = newd wxBoxSizer(wxVERTICAL);
	AddSectionHeader(borderLeftCol, m_borderPanel, "Border Grid");

	m_gridPanel = newd BorderGridPanel(m_borderPanel);
	m_gridPanel->SetDropTarget(new BorderGridDropTarget(m_gridPanel));
	borderLeftCol->Add(m_gridPanel, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

	// Hidden preview panel (data carrier only)
	m_previewPanel = newd BorderPreviewPanel(m_borderPanel);
	m_previewPanel->Hide();

	wxStaticText* instructions = newd wxStaticText(m_borderPanel, wxID_ANY,
		"Click to place brush | Right-click to remove | Drag item from palette");
	instructions->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	borderLeftCol->Add(instructions, 0, wxEXPAND | wxALL, 6);

	borderColsRow->Add(borderLeftCol, 60, wxEXPAND | wxRIGHT, 8);

	// RIGHT — Edge Items for selected direction
	wxBoxSizer* borderRightCol = newd wxBoxSizer(wxVERTICAL);
	AddSectionHeader(borderRightCol, m_borderPanel, "Edge Items");

	m_edgeItemsLabel = newd wxStaticText(m_borderPanel, wxID_ANY, "Items for selected direction:");
	borderRightCol->Add(m_edgeItemsLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

	m_edgeItemsPanel = newd EdgeItemsPanel(m_borderPanel);
	borderRightCol->Add(m_edgeItemsPanel, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

	// Item ID/Chance row
	wxBoxSizer* edgeIdRow = newd wxBoxSizer(wxHORIZONTAL);
	edgeIdRow->Add(newd wxStaticText(m_borderPanel, wxID_ANY, "Item ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_itemIdCtrl = newd wxSpinCtrl(m_borderPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535);
	m_itemIdCtrl->SetToolTip("Enter an item ID manually");
	edgeIdRow->Add(m_itemIdCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	edgeIdRow->Add(newd wxStaticText(m_borderPanel, wxID_ANY, "Chance:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_itemChanceCtrl = newd wxSpinCtrl(m_borderPanel, wxID_ANY, "100", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 1, 10000);
	m_itemChanceCtrl->SetToolTip("Chance weight for this item variant");
	edgeIdRow->Add(m_itemChanceCtrl, 0, wxALIGN_CENTER_VERTICAL);

	borderRightCol->Add(edgeIdRow, 0, wxEXPAND | wxALL, 6);

	// Edge buttons row
	wxBoxSizer* edgeBtnRow = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* browseButton = newd wxButton(m_borderPanel, wxID_FIND, "Browse...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	browseButton->SetToolTip("Browse for an item");
	edgeBtnRow->Add(browseButton, 0, wxRIGHT, 4);
	wxButton* addButton = newd wxButton(m_borderPanel, wxID_ADD, "+ Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	addButton->SetToolTip("Add the item to the currently selected direction");
	edgeBtnRow->Add(addButton, 0, wxRIGHT, 4);
	wxButton* updateChanceButton = newd wxButton(m_borderPanel, ID_UPDATE_CHANCE, "Set Chance", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	updateChanceButton->SetToolTip("Update the chance of the selected item");
	edgeBtnRow->Add(updateChanceButton, 0);
	borderRightCol->Add(edgeBtnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	borderColsRow->Add(borderRightCol, 40, wxEXPAND);

	borderSizer->Add(borderColsRow, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
	m_borderPanel->SetSizer(borderSizer);

	// ============================================================================
	// GROUND TAB — 2-column layout (Items 60% / Borders+Tileset 40%)
	// ============================================================================
	m_groundPanel = newd wxPanel(m_notebook);
	wxBoxSizer* groundSizer = newd wxBoxSizer(wxVERTICAL);

	// --- Properties bar (single row, no box) ---
	wxBoxSizer* groundPropsRow = newd wxBoxSizer(wxHORIZONTAL);

	groundPropsRow->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Look ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_serverLookIdCtrl = newd wxSpinCtrl(m_groundPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535);
	m_serverLookIdCtrl->SetToolTip("Server-side item ID");
	groundPropsRow->Add(m_serverLookIdCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

	groundPropsRow->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Z-Order:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_zOrderCtrl = newd wxSpinCtrl(m_groundPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 10000);
	m_zOrderCtrl->SetToolTip("Z-Order for display");
	groundPropsRow->Add(m_zOrderCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

	groundPropsRow->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Load:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_existingGroundBrushesCombo = newd wxComboBox(m_groundPanel, ID_EXISTING_GROUNDS_COMBO, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_DROPDOWN | wxTE_PROCESS_ENTER);
	m_existingGroundBrushesCombo->SetToolTip("Type to search, or pick from the list. Press Enter or select to load.");
	groundPropsRow->Add(m_existingGroundBrushesCombo, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_groundLoadPreview = newd BorderNorthPreview(m_groundPanel);
	m_groundLoadPreview->SetToolTip("Preview: look item of the selected ground brush.");
	groundPropsRow->Add(m_groundLoadPreview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

	groundPropsRow->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Find ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_findGroundItemIdCtrl = newd wxSpinCtrl(m_groundPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535);
	m_findGroundItemIdCtrl->SetToolTip("Enter an item ID to locate the ground brush that uses it");
	groundPropsRow->Add(m_findGroundItemIdCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	wxButton* findGroundButton = newd wxButton(m_groundPanel, ID_FIND_GROUND_BY_ITEM_ID, "Find", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	findGroundButton->SetToolTip("Find and load the ground brush that uses this item ID");
	groundPropsRow->Add(findGroundButton, 0, wxALIGN_CENTER_VERTICAL);

	groundSizer->Add(groundPropsRow, 0, wxEXPAND | wxALL, 8);

	// --- Two columns: Items (left 60%) | Borders+Tileset (right 40%) ---
	wxBoxSizer* groundColsRow = newd wxBoxSizer(wxHORIZONTAL);

	// LEFT — Ground Items (canvas)
	wxBoxSizer* groundLeftCol = newd wxBoxSizer(wxVERTICAL);
	AddSectionHeader(groundLeftCol, m_groundPanel, "Ground Items");

	m_groundItemsPanel = newd GroundItemsPanel(m_groundPanel);
	m_groundItemsPanel->SetDropTarget(new GroundItemsDropTarget(m_groundItemsPanel));
	m_groundItemsPanel->SetToolTip("Drag items here from the palette, or use the Add button below.");
	groundLeftCol->Add(m_groundItemsPanel, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

	// Ground item controls row (ID + Chance)
	wxBoxSizer* groundItemIdRow = newd wxBoxSizer(wxHORIZONTAL);
	groundItemIdRow->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Item ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_groundItemIdCtrl = newd wxSpinCtrl(m_groundPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535);
	m_groundItemIdCtrl->SetToolTip("ID of the item to add");
	groundItemIdRow->Add(m_groundItemIdCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	groundItemIdRow->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Chance:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_groundItemChanceCtrl = newd wxSpinCtrl(m_groundPanel, wxID_ANY, "10", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 1, 10000);
	m_groundItemChanceCtrl->SetToolTip("Chance of this item appearing");
	groundItemIdRow->Add(m_groundItemChanceCtrl, 0, wxALIGN_CENTER_VERTICAL);
	groundLeftCol->Add(groundItemIdRow, 0, wxEXPAND | wxALL, 6);

	// Ground item buttons row
	wxBoxSizer* groundItemBtnRow = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* groundBrowseButton = newd wxButton(m_groundPanel, wxID_FIND + 100, "Browse...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	groundBrowseButton->SetToolTip("Browse for an item");
	groundItemBtnRow->Add(groundBrowseButton, 0, wxRIGHT, 4);

	m_addGroundItemButton = newd wxButton(m_groundPanel, wxID_ADD + 100, "+ Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_addGroundItemButton->SetToolTip("Add this item to the list");
	groundItemBtnRow->Add(m_addGroundItemButton, 0, wxRIGHT, 4);

	m_updateGroundItemButton = newd wxButton(m_groundPanel, ID_UPDATE_GROUND_CHANCE, "Update Chance", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_updateGroundItemButton->SetToolTip("Update the chance of the selected item");
	groundItemBtnRow->Add(m_updateGroundItemButton, 0);
	groundLeftCol->Add(groundItemBtnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	groundColsRow->Add(groundLeftCol, 60, wxEXPAND | wxRIGHT, 8);

	// RIGHT — Borders (top) + Tileset (bottom)
	wxBoxSizer* groundRightCol = newd wxBoxSizer(wxVERTICAL);

	// ▸ BORDERS
	AddSectionHeader(groundRightCol, m_groundPanel, "Borders");

	wxBoxSizer* groundBordersListRow = newd wxBoxSizer(wxHORIZONTAL);
	m_groundBordersList = newd GroundBordersPanel(m_groundPanel);
	m_groundBordersList->SetToolTip("Borders used by this ground brush. Order matters: earlier entries are applied first.\nDouble-click an entry to edit it in the fields below, click the X to remove.");
	groundBordersListRow->Add(m_groundBordersList, 1, wxEXPAND | wxRIGHT, 4);

	wxBoxSizer* orderBtnsSizer = newd wxBoxSizer(wxVERTICAL);
	wxButton* moveUpBtn = newd wxButton(m_groundPanel, ID_MOVE_GROUND_BORDER_UP, wxT("▲"), wxDefaultPosition, wxSize(32, -1), wxBU_EXACTFIT);
	moveUpBtn->SetToolTip("Move the selected border up in the list.");
	orderBtnsSizer->Add(moveUpBtn, 0, wxBOTTOM, 2);
	wxButton* moveDownBtn = newd wxButton(m_groundPanel, ID_MOVE_GROUND_BORDER_DOWN, wxT("▼"), wxDefaultPosition, wxSize(32, -1), wxBU_EXACTFIT);
	moveDownBtn->SetToolTip("Move the selected border down in the list.");
	orderBtnsSizer->Add(moveDownBtn, 0);
	groundBordersListRow->Add(orderBtnsSizer, 0, wxALIGN_CENTER_VERTICAL);

	groundRightCol->Add(groundBordersListRow, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

	// Border config row 1: Align + To
	wxBoxSizer* borderCfgRow1 = newd wxBoxSizer(wxHORIZONTAL);
	borderCfgRow1->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Align:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	wxString alignChoices[] = { "outer", "inner" };
	m_groundBorderAlignCtrl = newd wxChoice(m_groundPanel, wxID_ANY, wxDefaultPosition, wxSize(70, -1), 2, alignChoices);
	m_groundBorderAlignCtrl->SetSelection(0);
	m_groundBorderAlignCtrl->SetToolTip("outer: when ground touches another type.\ninner: when ground touches empty (to=none) or specific brush.");
	borderCfgRow1->Add(m_groundBorderAlignCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	borderCfgRow1->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "To:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_groundBorderToCtrl = newd wxComboBox(m_groundPanel, ID_GROUND_BORDER_TO_COMBO, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_DROPDOWN);
	m_groundBorderToCtrl->SetToolTip("Target: leave empty for default, 'none' for empty tile, or a brush name.");
	borderCfgRow1->Add(m_groundBorderToCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_groundToPreview = newd BorderNorthPreview(m_groundPanel);
	m_groundToPreview->SetToolTip("Preview: look item of the target ground brush.");
	borderCfgRow1->Add(m_groundToPreview, 0, wxALIGN_CENTER_VERTICAL);
	groundRightCol->Add(borderCfgRow1, 0, wxEXPAND | wxALL, 6);

	// Border config row 2: Border combo + preview
	wxBoxSizer* borderCfgRow2 = newd wxBoxSizer(wxHORIZONTAL);
	borderCfgRow2->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Border:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_groundBorderIdCtrl = newd wxComboBox(m_groundPanel, ID_GROUND_BORDER_ID_COMBO, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_DROPDOWN);
	m_groundBorderIdCtrl->SetToolTip("Select an existing border, or type a numeric ID.");
	borderCfgRow2->Add(m_groundBorderIdCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_groundBorderPreview = newd BorderNorthPreview(m_groundPanel);
	m_groundBorderPreview->SetToolTip("Preview: north-facing edge of the selected border.");
	borderCfgRow2->Add(m_groundBorderPreview, 0, wxALIGN_CENTER_VERTICAL);
	groundRightCol->Add(borderCfgRow2, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	// Border add/remove buttons
	wxBoxSizer* borderAddRow = newd wxBoxSizer(wxHORIZONTAL);
	m_addGroundBorderButton = newd wxButton(m_groundPanel, ID_ADD_GROUND_BORDER, "+ Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	borderAddRow->Add(m_addGroundBorderButton, 0, wxRIGHT, 4);
	m_modifyGroundBorderButton = newd wxButton(m_groundPanel, ID_MODIFY_GROUND_BORDER, wxT("✎ Modify"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_modifyGroundBorderButton->SetToolTip("Apply the Align/To/Border fields to the selected entry. Double-click an entry to load it into the fields.");
	borderAddRow->Add(m_modifyGroundBorderButton, 0, wxRIGHT, 4);
	m_removeGroundBorderButton = newd wxButton(m_groundPanel, ID_REMOVE_GROUND_BORDER, wxT("− Remove"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	borderAddRow->Add(m_removeGroundBorderButton, 0);
	groundRightCol->Add(borderAddRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	// ▸ TILESET (publish)
	AddSectionHeader(groundRightCol, m_groundPanel, "Tileset (publish)");

	wxBoxSizer* tilesetRow = newd wxBoxSizer(wxHORIZONTAL);
	tilesetRow->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Tileset:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_tilesetCombo = newd wxComboBox(m_groundPanel, ID_TILESET_COMBO, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_DROPDOWN);
	m_tilesetCombo->SetToolTip("Pick a tileset. Only tilesets that contain (or can contain) Terrain brushes are listed.");
	tilesetRow->Add(m_tilesetCombo, 1, wxALIGN_CENTER_VERTICAL);
	groundRightCol->Add(tilesetRow, 0, wxEXPAND | wxALL, 6);

	groundRightCol->Add(newd wxStaticText(m_groundPanel, wxID_ANY, "Existing brushes (Terrain):"), 0, wxLEFT | wxRIGHT, 6);
	m_tilesetBrushList = newd wxListBox(m_groundPanel, ID_TILESET_BRUSH_LIST, wxDefaultPosition, wxSize(-1, 80));
	m_tilesetBrushList->SetToolTip("Select a brush to use as reference for the 'After selected' insert option.");
	groundRightCol->Add(m_tilesetBrushList, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

	wxString positions[] = { "At start", "After selected", "At end" };
	m_tilesetInsertPosition = newd wxRadioBox(m_groundPanel, wxID_ANY, "Insert position",
		wxDefaultPosition, wxDefaultSize, 3, positions, 1, wxRA_SPECIFY_ROWS);
	m_tilesetInsertPosition->SetSelection(2); // default: At end
	groundRightCol->Add(m_tilesetInsertPosition, 0, wxEXPAND | wxALL, 6);

	m_addToTilesetButton = newd wxButton(m_groundPanel, ID_ADD_TO_TILESET, "+ Add brush to Tileset");
	m_addToTilesetButton->SetToolTip("Adds the current ground brush to the selected tileset's <terrain> section at the chosen position.");
	groundRightCol->Add(m_addToTilesetButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	groundColsRow->Add(groundRightCol, 40, wxEXPAND);

	groundSizer->Add(groundColsRow, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
	m_groundPanel->SetSizer(groundSizer);

	// ============================================================================
	// Assemble notebook
	// ============================================================================
	m_notebook->AddPage(m_borderPanel, "  Border  ");
	m_notebook->AddPage(m_groundPanel, "  Ground  ");
	topSizer->Add(m_notebook, 1, wxEXPAND | wxALL, 5);

	// ============================================================================
	// Common Properties (shared across tabs) — single compact row
	// ============================================================================
	wxBoxSizer* commonRow = newd wxBoxSizer(wxHORIZONTAL);
	commonRow->Add(newd wxStaticText(this, wxID_ANY, "Name:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_nameCtrl = newd wxTextCtrl(this, wxID_ANY);
	m_nameCtrl->SetToolTip("Descriptive name for the border");
	commonRow->Add(m_nameCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

	commonRow->Add(newd wxStaticText(this, wxID_ANY, "ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_idCtrl = newd wxSpinCtrl(this, wxID_ANY, "1", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, 65535);
	m_idCtrl->SetToolTip("Unique identifier for this border");
	commonRow->Add(m_idCtrl, 0, wxALIGN_CENTER_VERTICAL);

	topSizer->Add(commonRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

	// Separator above the action bar
	topSizer->Add(newd wxStaticLine(this), 0, wxEXPAND | wxTOP | wxBOTTOM, 6);

	// ============================================================================
	// Action bar — Clear (secondary, left) + Save (primary green, right)
	// ============================================================================
	wxBoxSizer* actionBar = newd wxBoxSizer(wxHORIZONTAL);

	m_clearButton = newd wxButton(this, wxID_CLEAR, wxT("✕ Clear"));
	m_clearButton->SetToolTip("Clear the current form");
	actionBar->Add(m_clearButton, 0, wxALIGN_CENTER_VERTICAL);

	actionBar->AddStretchSpacer(1);

	m_saveButton = newd wxButton(this, wxID_SAVE, wxT("✓ Save Border"));
	m_saveButton->SetBackgroundColour(Theme::Get(Theme::Role::Success));
	m_saveButton->SetForegroundColour(Theme::Get(Theme::Role::TextOnAccent));
	wxFont saveFont = m_saveButton->GetFont();
	saveFont.SetWeight(wxFONTWEIGHT_BOLD);
	m_saveButton->SetFont(saveFont);
	m_saveButton->SetToolTip("Save the border or ground brush currently being edited");
	actionBar->Add(m_saveButton, 0, wxALIGN_CENTER_VERTICAL);

	topSizer->Add(actionBar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	SetSizer(topSizer);
	Layout();
}

// ============================================================================
// Load/Save
// ============================================================================

void BorderEditorDialog::LoadExistingBorders() {
	// Batch updates — a few hundred Append() calls with paint-per-item would take ~hundreds of ms.
	m_existingBordersCombo->Freeze();
	if (m_groundBorderIdCtrl) m_groundBorderIdCtrl->Freeze();

	m_existingBordersCombo->Clear();
	m_existingBordersCombo->Append("<Create New>");
	m_existingBordersCombo->SetSelection(0);

	if (m_groundBorderIdCtrl) {
		m_groundBorderIdCtrl->Clear();
	}

	auto cleanup = [&]() {
		m_existingBordersCombo->Thaw();
		if (m_groundBorderIdCtrl) m_groundBorderIdCtrl->Thaw();
	};

	wxString dataDir = GetVersionDataDirectory();
	if (dataDir.IsEmpty()) { cleanup(); return; }

	wxString bordersFile = dataDir + "borders.xml";
	if (!wxFileExists(bordersFile)) {
		cleanup();
		wxMessageBox("Cannot find borders.xml file in the data directory.", "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(bordersFile.ToStdString().c_str());
	if (!result) {
		cleanup();
		wxMessageBox("Failed to load borders.xml: " + wxString(result.description()), "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		cleanup();
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

		if (m_groundBorderIdCtrl) {
			m_groundBorderIdCtrl->Append(label, newd wxStringClientData(wxString::Format("%d", id)));
		}
	}

	m_nextBorderId = highestId + 1;
	m_idCtrl->SetValue(m_nextBorderId);

	cleanup();
}

void BorderEditorDialog::LoadExistingGroundBrushes() {
	m_existingGroundBrushesCombo->Freeze();
	if (m_groundBorderToCtrl) m_groundBorderToCtrl->Freeze();

	m_existingGroundBrushesCombo->Clear();
	m_existingGroundBrushesCombo->Append("<Create New>");
	m_existingGroundBrushesCombo->SetSelection(0);
	m_groundLookIds.clear();

	if (m_groundBorderToCtrl) {
		m_groundBorderToCtrl->Clear();
		m_groundBorderToCtrl->Append("");       // default / any
		m_groundBorderToCtrl->Append("none");   // empty tile
	}

	auto cleanup = [&]() {
		m_existingGroundBrushesCombo->Thaw();
		if (m_groundBorderToCtrl) m_groundBorderToCtrl->Thaw();
	};

	wxString dataDir = GetVersionDataDirectory();
	if (dataDir.IsEmpty()) { cleanup(); return; }

	wxString groundsFile = dataDir + "grounds.xml";
	if (!wxFileExists(groundsFile)) { cleanup(); return; }

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(groundsFile.ToStdString().c_str());
	if (!result) { cleanup(); return; }

	pugi::xml_node materials = doc.child("materials");
	if (!materials) { cleanup(); return; }

	// Collect first, then batch-append via wxArrayString — much faster than per-item Append.
	wxArrayString brushNames;
	std::vector<int> serverIds; // parallel to brushNames for client data
	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		pugi::xml_attribute serverLookIdAttr = brushNode.attribute("server_lookid");
		pugi::xml_attribute typeAttr = brushNode.attribute("type");

		if (!typeAttr || std::string(typeAttr.as_string()) != "ground") continue;
		if (!nameAttr || !serverLookIdAttr) continue;

		brushNames.Add(wxString(nameAttr.as_string()));
		serverIds.push_back(serverLookIdAttr.as_int());
		m_groundLookIds[wxString(nameAttr.as_string()).Lower()] = static_cast<uint16_t>(serverLookIdAttr.as_uint());
	}

	for (size_t i = 0; i < brushNames.GetCount(); ++i) {
		m_existingGroundBrushesCombo->Append(brushNames[i], newd wxStringClientData(wxString::Format("%d", serverIds[i])));
		if (m_groundBorderToCtrl) {
			m_groundBorderToCtrl->Append(brushNames[i]);
		}
	}

	// Inline autocomplete while typing a brush name (native on MSW).
	m_existingGroundBrushesCombo->AutoComplete(brushNames);
	if (m_groundBorderToCtrl) {
		wxArrayString toNames;
		toNames.Add("none");
		for (size_t i = 0; i < brushNames.GetCount(); ++i) {
			toNames.Add(brushNames[i]);
		}
		m_groundBorderToCtrl->AutoComplete(toNames);
	}

	cleanup();
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

		// Get the name from the comment, or fallback to "Border <id>"
		std::string borderName;
		pugi::xml_node commentNode = borderNode.previous_sibling();
		if (commentNode && commentNode.type() == pugi::node_comment) {
			borderName = commentNode.value();
			size_t first = borderName.find_first_not_of(" \t\n\r");
			if (first != std::string::npos) {
				borderName = borderName.substr(first);
				size_t last = borderName.find_last_not_of(" \t\n\r");
				if (last != std::string::npos) {
					borderName = borderName.substr(0, last + 1);
				}
			} else {
				borderName.clear();
			}
		}
		if (borderName.empty()) {
			borderName = "Border " + std::to_string(borderId);
		}
		m_nameCtrl->SetValue(wxString(borderName));

		// Load all border items
		for (pugi::xml_node itemNode = borderNode.child("borderitem"); itemNode; itemNode = itemNode.next_sibling("borderitem")) {
			pugi::xml_attribute edgeAttr = itemNode.attribute("edge");
			if (!edgeAttr) continue;

			BorderEdgePosition pos = edgeStringToPosition(edgeAttr.as_string());
			if (pos == EDGE_NONE) continue;

			// New format: <borderitem edge="n"> <item id="123" chance="80" /> ... </borderitem>
			pugi::xml_node firstItemChild = itemNode.child("item");
			if (firstItemChild) {
				for (pugi::xml_node subItem = firstItemChild; subItem; subItem = subItem.next_sibling("item")) {
					pugi::xml_attribute subIdAttr = subItem.attribute("id");
					if (!subIdAttr) continue;

					uint16_t itemId = subIdAttr.as_uint();
					int chance = subItem.attribute("chance") ? subItem.attribute("chance").as_int() : 100;

					if (itemId > 0) {
						m_borderItems[pos].push_back(BorderEdgeItem(itemId, chance));
					}
				}
			} else {
				// Legacy format: <borderitem edge="n" item="123" />
				pugi::xml_attribute itemAttr = itemNode.attribute("item");
				if (!itemAttr) continue;

				uint16_t itemId = itemAttr.as_uint();
				if (itemId > 0) {
					m_borderItems[pos].push_back(BorderEdgeItem(itemId, 100));
				}
			}
		}

		// Update the grid with first item from each direction
		for (const auto& [pos, items] : m_borderItems) {
			if (!items.empty()) {
				m_gridPanel->SetItemId(pos, items[0].itemId);
				m_gridPanel->SetItemCount(pos, static_cast<int>(items.size()));
			}
		}

		break;
	}

	UpdatePreview();
	UpdateEdgeItemsList();
	m_existingBordersCombo->SetSelection(selection);
}

void BorderEditorDialog::OnScanBorder(wxCommandEvent& WXUNUSED(event)) {
	// Re-check: sprites may have been unloaded after the button was created.
	if (g_gui.gfx.isUnloaded()) {
		wxMessageBox("Requires a loaded client (sprites).", "Border Scan", wxICON_WARNING);
		return;
	}

	BorderScanDialog dlg(this);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	// REPLACE semantics per returned edge: only edges present in the result are
	// touched; every other slot keeps its current items. Saving stays in SaveBorder().
	for (const auto& [pos, itemId] : dlg.GetEdgeAssignments()) {
		m_borderItems[pos] = { BorderEdgeItem(itemId, 100) };
		m_gridPanel->SetItemId(pos, itemId);
		m_gridPanel->SetItemCount(pos, 1);
	}

	// Mirror the grid-refresh sequence at the end of OnLoadBorder.
	UpdatePreview();
	UpdateEdgeItemsList();
}

void BorderEditorDialog::ApplyItemToPosition(BorderEdgePosition pos, uint16_t itemId) {
	if (pos == EDGE_NONE || itemId == 0) return;

	if (m_itemIdCtrl) {
		m_itemIdCtrl->SetValue(itemId);
	}

	int chance = m_itemChanceCtrl ? m_itemChanceCtrl->GetValue() : 100;

	// Add item to this direction
	m_borderItems[pos].push_back(BorderEdgeItem(itemId, chance));

	// Update grid to show first item and count
	m_gridPanel->SetItemId(pos, m_borderItems[pos][0].itemId);
	m_gridPanel->SetItemCount(pos, static_cast<int>(m_borderItems[pos].size()));
	UpdatePreview();
	UpdateEdgeItemsList();
}

void BorderEditorDialog::OnPositionSelected(wxCommandEvent& event) {
	UpdateEdgeItemsList();
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

void BorderEditorDialog::OnRemoveBorderItem(int index) {
	BorderEdgePosition selectedPos = m_gridPanel->GetSelectedPosition();
	if (selectedPos == EDGE_NONE) return;

	auto it = m_borderItems.find(selectedPos);
	if (it != m_borderItems.end() && index >= 0 && index < static_cast<int>(it->second.size())) {
		it->second.erase(it->second.begin() + index);

		if (it->second.empty()) {
			m_borderItems.erase(it);
			m_gridPanel->SetItemId(selectedPos, 0);
			m_gridPanel->SetItemCount(selectedPos, 0);
		} else {
			m_gridPanel->SetItemId(selectedPos, it->second[0].itemId);
			m_gridPanel->SetItemCount(selectedPos, static_cast<int>(it->second.size()));
		}

		UpdatePreview();
		UpdateEdgeItemsList();
	}
}

void BorderEditorDialog::OnUpdateChance(wxCommandEvent& event) {
	BorderEdgePosition selectedPos = m_gridPanel->GetSelectedPosition();
	if (selectedPos == EDGE_NONE) return;

	int selection = m_edgeItemsPanel->GetSelectedIndex();
	if (selection < 0) return;

	auto it = m_borderItems.find(selectedPos);
	if (it != m_borderItems.end() && selection < static_cast<int>(it->second.size())) {
		it->second[selection].chance = m_itemChanceCtrl->GetValue();
		UpdateEdgeItemsList();
	}
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
	UpdateEdgeItemsList();
}

void BorderEditorDialog::UpdatePreview() {
	// Build flat list with first item of each direction for preview
	std::vector<BorderItem> previewItems;
	for (const auto& [pos, items] : m_borderItems) {
		if (!items.empty()) {
			previewItems.push_back(BorderItem(pos, items[0].itemId));
		}
	}
	m_gridPanel->SetPreviewItems(previewItems);
}

void BorderEditorDialog::UpdateEdgeItemsList() {
	BorderEdgePosition selectedPos = m_gridPanel->GetSelectedPosition();
	if (selectedPos == EDGE_NONE) {
		m_edgeItemsLabel->SetLabel("Items for selected direction:");
		m_edgeItemsPanel->Clear();
		return;
	}

	wxString dirName = wxString(edgePositionToString(selectedPos));
	m_edgeItemsLabel->SetLabel(wxString::Format("Items for direction '%s':", dirName));

	auto it = m_borderItems.find(selectedPos);
	if (it == m_borderItems.end()) {
		m_edgeItemsPanel->Clear();
		return;
	}

	m_edgeItemsPanel->SetItems(it->second);
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
	for (const auto& [pos, items] : m_borderItems) {
		if (items.empty()) continue;

		std::string edgeStr = edgePositionToString(pos);

		if (items.size() == 1) {
			// Single item: use legacy format for simplicity
			pugi::xml_node itemNode = borderNode.append_child("borderitem");
			itemNode.append_attribute("edge").set_value(edgeStr.c_str());
			itemNode.append_attribute("item").set_value(items[0].itemId);
		} else {
			// Multiple items: use new table format
			pugi::xml_node edgeNode = borderNode.append_child("borderitem");
			edgeNode.append_attribute("edge").set_value(edgeStr.c_str());

			for (const auto& item : items) {
				pugi::xml_node subItem = edgeNode.append_child("item");
				subItem.append_attribute("id").set_value(item.itemId);
				subItem.append_attribute("chance").set_value(item.chance);
			}
		}
	}

	// Strip legacy `-- name --` PCDATA and blank-line accumulation from every
	// border before writing, otherwise pugixml's indenter would multiply the
	// whitespace on every save.
	NormalizeBordersDocument(materials);

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

void BorderEditorDialog::OnPageChanged(wxBookCtrlEvent& event) {
	// Only react to our own sub-notebook — the outer BrushesEditorDialog notebook fires page
	// events that would otherwise bubble up through the event table with wxID_ANY.
	if (event.GetEventObject() != m_notebook) {
		event.Skip();
		return;
	}

	m_activeTab = event.GetSelection();

	// Update the shared Save button label to reflect the active sub-tab.
	if (m_saveButton) {
		m_saveButton->SetLabel(m_activeTab == 1 ? wxT("✓ Save Ground") : wxT("✓ Save Border"));
	}

	// Lazy-load Ground tab data on first activation (Ground tab index is 1)
	if (m_activeTab == 1 && !m_groundTabLoaded) {
		m_groundTabLoaded = true;
		wxBusyCursor busy;
		LoadExistingGroundBrushes();
		LoadExistingTilesets();
	}
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

void BorderEditorDialog::AddGroundItemById(uint16_t itemId) {
	if (itemId == 0) return;

	int chance = m_groundItemChanceCtrl ? m_groundItemChanceCtrl->GetValue() : 10;
	m_groundItems.push_back(GroundItem(itemId, chance));
	UpdateGroundItemsList();
}

void BorderEditorDialog::OnRemoveGroundItem(int index) {
	if (index < 0 || index >= static_cast<int>(m_groundItems.size())) {
		return;
	}

	m_groundItems.erase(m_groundItems.begin() + index);
	UpdateGroundItemsList();
}

void BorderEditorDialog::OnUpdateGroundChance(wxCommandEvent& event) {
	int selection = m_groundItemsPanel ? m_groundItemsPanel->GetSelectedIndex() : -1;
	if (selection < 0 || selection >= static_cast<int>(m_groundItems.size())) {
		wxMessageBox("Select an item first to update its chance.", "Error", wxICON_ERROR);
		return;
	}

	m_groundItems[selection].chance = m_groundItemChanceCtrl->GetValue();
	UpdateGroundItemsList();
}

// Resolves the border id from the Border combo: selected item's client data first,
// then the text parsed as a number. Returns 0 if neither yields a valid id.
static int ResolveBorderIdFromCombo(wxComboBox* combo) {
	int borderId = 0;

	int sel = combo->GetSelection();
	if (sel != wxNOT_FOUND) {
		wxStringClientData* data = static_cast<wxStringClientData*>(combo->GetClientObject(sel));
		if (data) {
			borderId = wxAtoi(data->GetData());
		}
	}

	if (borderId <= 0) {
		wxString txt = combo->GetValue();
		long v = 0;
		if (txt.ToLong(&v) && v > 0) {
			borderId = static_cast<int>(v);
		}
	}

	return borderId;
}

void BorderEditorDialog::OnAddGroundBorder(wxCommandEvent& event) {
	int borderId = ResolveBorderIdFromCombo(m_groundBorderIdCtrl);
	if (borderId <= 0) {
		wxMessageBox("Please select or type a valid border ID.", "Error", wxICON_ERROR);
		return;
	}

	std::string align = m_groundBorderAlignCtrl->GetSelection() == 1 ? "inner" : "outer";
	std::string to = m_groundBorderToCtrl->GetValue().ToStdString();

	m_groundBorders.push_back(GroundBorderRef(align, to, borderId));
	RefreshGroundBordersList(static_cast<int>(m_groundBorders.size()) - 1);
}

void BorderEditorDialog::OnModifyGroundBorder(wxCommandEvent& event) {
	int selection = m_groundBordersList ? m_groundBordersList->GetSelectedIndex() : -1;
	if (selection < 0 || selection >= static_cast<int>(m_groundBorders.size())) {
		wxMessageBox("Select a border entry to modify (double-click loads it into the fields).", "Error", wxICON_ERROR);
		return;
	}

	int borderId = ResolveBorderIdFromCombo(m_groundBorderIdCtrl);
	if (borderId <= 0) {
		wxMessageBox("Please select or type a valid border ID.", "Error", wxICON_ERROR);
		return;
	}

	GroundBorderRef& ref = m_groundBorders[selection];
	ref.align = m_groundBorderAlignCtrl->GetSelection() == 1 ? "inner" : "outer";
	ref.to = m_groundBorderToCtrl->GetValue().ToStdString();
	ref.borderId = borderId;
	// 'enabled' is intentionally preserved — toggled via the checkmark on the cell.

	RefreshGroundBordersList(selection);
}

void BorderEditorDialog::EditGroundBorder(int index) {
	if (index < 0 || index >= static_cast<int>(m_groundBorders.size())) return;
	const GroundBorderRef& ref = m_groundBorders[index];

	m_groundBorderAlignCtrl->SetSelection(ref.align == "inner" ? 1 : 0);
	m_groundBorderToCtrl->SetValue(wxString(ref.to)); // fires EVT_TEXT -> To preview updates

	// Prefer the combo entry whose client data matches the id (shows the full label);
	// fall back to the bare number for ids not present in borders.xml.
	bool found = false;
	for (unsigned int i = 0; i < m_groundBorderIdCtrl->GetCount(); ++i) {
		wxStringClientData* data = static_cast<wxStringClientData*>(m_groundBorderIdCtrl->GetClientObject(i));
		if (data && wxAtoi(data->GetData()) == ref.borderId) {
			m_groundBorderIdCtrl->SetSelection(i);
			found = true;
			break;
		}
	}
	if (!found) {
		m_groundBorderIdCtrl->SetValue(wxString::Format("%d", ref.borderId));
	}

	// Programmatic SetSelection doesn't fire EVT_COMBOBOX — refresh the preview directly.
	if (m_groundBorderPreview) {
		m_groundBorderPreview->SetItemId(LookupNorthItemId(ref.borderId));
	}
}

uint16_t BorderEditorDialog::LookupGroundLookId(const wxString& brushName) const {
	wxString name = brushName;
	name.Trim(true).Trim(false);
	if (name.IsEmpty() || name == "none" || name == "<Create New>") return 0;

	auto it = m_groundLookIds.find(name.Lower());
	return it != m_groundLookIds.end() ? it->second : 0;
}

void BorderEditorDialog::OnGroundLoadTextChanged(wxCommandEvent& event) {
	if (!m_groundLoadPreview || !m_existingGroundBrushesCombo) return;
	m_groundLoadPreview->SetItemId(LookupGroundLookId(m_existingGroundBrushesCombo->GetValue()));
}

void BorderEditorDialog::OnGroundBorderToChanged(wxCommandEvent& event) {
	if (!m_groundToPreview || !m_groundBorderToCtrl) return;
	m_groundToPreview->SetItemId(LookupGroundLookId(m_groundBorderToCtrl->GetValue()));
}

uint16_t BorderEditorDialog::LookupNorthItemId(int borderId) {
	if (borderId <= 0) return 0;

	wxString dataDir = GetVersionDataDirectory();
	if (dataDir.IsEmpty()) return 0;

	wxString bordersFile = dataDir + "borders.xml";
	if (!wxFileExists(bordersFile)) return 0;

	pugi::xml_document doc;
	if (!doc.load_file(bordersFile.ToStdString().c_str())) return 0;

	pugi::xml_node materials = doc.child("materials");
	for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
		pugi::xml_attribute idAttr = borderNode.attribute("id");
		if (!idAttr || idAttr.as_int() != borderId) continue;

		for (pugi::xml_node item = borderNode.child("borderitem"); item; item = item.next_sibling("borderitem")) {
			pugi::xml_attribute edgeAttr = item.attribute("edge");
			pugi::xml_attribute itemAttr = item.attribute("item");
			if (edgeAttr && itemAttr && std::string(edgeAttr.as_string()) == "n") {
				return static_cast<uint16_t>(itemAttr.as_uint());
			}
		}
		break;
	}
	return 0;
}

void BorderEditorDialog::OnGroundBorderIdChanged(wxCommandEvent& event) {
	if (!m_groundBorderPreview || !m_groundBorderIdCtrl) return;

	int borderId = 0;

	int sel = m_groundBorderIdCtrl->GetSelection();
	if (sel != wxNOT_FOUND) {
		wxStringClientData* data = static_cast<wxStringClientData*>(m_groundBorderIdCtrl->GetClientObject(sel));
		if (data) {
			borderId = wxAtoi(data->GetData());
		}
	}

	if (borderId <= 0) {
		wxString txt = m_groundBorderIdCtrl->GetValue();
		long v = 0;
		if (txt.ToLong(&v) && v > 0) {
			borderId = static_cast<int>(v);
		}
	}

	if (borderId <= 0) {
		m_groundBorderPreview->Clear();
		return;
	}

	uint16_t northItem = LookupNorthItemId(borderId);
	m_groundBorderPreview->SetItemId(northItem);
}

void BorderEditorDialog::OnRemoveGroundBorder(wxCommandEvent& event) {
	int selection = m_groundBordersList->GetSelectedIndex();
	if (selection < 0) {
		wxMessageBox("Select a border to remove.", "Error", wxICON_ERROR);
		return;
	}

	m_groundBorders.erase(m_groundBorders.begin() + selection);
	int newSel = std::min(selection, static_cast<int>(m_groundBorders.size()) - 1);
	RefreshGroundBordersList(newSel);
}

void BorderEditorDialog::OnMoveGroundBorderUp(wxCommandEvent& event) {
	int sel = m_groundBordersList->GetSelectedIndex();
	if (sel <= 0 || sel >= static_cast<int>(m_groundBorders.size())) return;

	std::swap(m_groundBorders[sel - 1], m_groundBorders[sel]);
	RefreshGroundBordersList(sel - 1);
}

void BorderEditorDialog::OnMoveGroundBorderDown(wxCommandEvent& event) {
	int sel = m_groundBordersList->GetSelectedIndex();
	if (sel < 0 || sel >= static_cast<int>(m_groundBorders.size()) - 1) return;

	std::swap(m_groundBorders[sel], m_groundBorders[sel + 1]);
	RefreshGroundBordersList(sel + 1);
}

void BorderEditorDialog::RefreshGroundBordersList(int selectAt) {
	if (!m_groundBordersList) return;
	m_groundBordersList->SetItems(m_groundBorders);
	if (selectAt >= 0 && selectAt < static_cast<int>(m_groundBorders.size())) {
		m_groundBordersList->SetSelectedIndex(selectAt);
	}
}

// Strips the " (Terrain)" / " (Terrain - new)" suffix from a combobox label, returning
// the bare tileset name. Returns the input unchanged if no recognized suffix is present
// (the user may have typed a fresh name).
static wxString StripTerrainSuffix(const wxString& label) {
	wxString trimmed = label;
	trimmed.Trim(true).Trim(false);
	size_t parenIdx = trimmed.rfind(" (Terrain");
	if (parenIdx != wxString::npos) {
		return wxString(trimmed.Mid(0, parenIdx)).Trim(true).Trim(false);
	}
	return trimmed;
}

void BorderEditorDialog::LoadExistingTilesets() {
	if (!m_tilesetCombo) return;
	m_tilesetCombo->Clear();

	wxString dataDir = GetVersionDataDirectory();
	if (dataDir.IsEmpty()) return;

	wxString tilesetsFile = dataDir + "tilesets.xml";
	if (!wxFileExists(tilesetsFile)) return;

	pugi::xml_document doc;
	if (!doc.load_file(tilesetsFile.ToStdString().c_str())) return;

	pugi::xml_node materials = doc.child("materials");
	if (!materials) return;

	// First pass: collapse duplicate tileset entries — a tileset name can appear
	// in multiple <tileset name="X"> blocks (one with <terrain>, another with
	// <doodad>, etc.). We only care whether any of them has a <terrain> section.
	std::map<std::string, bool> nameHasTerrain;
	std::vector<std::string> orderedNames;
	for (pugi::xml_node tilesetNode = materials.child("tileset"); tilesetNode; tilesetNode = tilesetNode.next_sibling("tileset")) {
		pugi::xml_attribute nameAttr = tilesetNode.attribute("name");
		if (!nameAttr) continue;
		std::string name = nameAttr.as_string();
		bool hasTerrain = static_cast<bool>(tilesetNode.child("terrain"));
		auto it = nameHasTerrain.find(name);
		if (it == nameHasTerrain.end()) {
			nameHasTerrain[name] = hasTerrain;
			orderedNames.push_back(name);
		} else if (hasTerrain) {
			it->second = true;
		}
	}

	for (const std::string& name : orderedNames) {
		wxString label = wxString(name);
		label += nameHasTerrain[name] ? " (Terrain)" : " (Terrain - new)";
		m_tilesetCombo->Append(label);
	}

	if (m_tilesetBrushList) m_tilesetBrushList->Clear();
}

void BorderEditorDialog::RefreshTilesetBrushList() {
	if (!m_tilesetBrushList) return;
	m_tilesetBrushList->Clear();

	wxString tilesetName = StripTerrainSuffix(m_tilesetCombo->GetValue());
	if (tilesetName.IsEmpty()) return;

	wxString dataDir = GetVersionDataDirectory();
	if (dataDir.IsEmpty()) return;
	wxString tilesetsFile = dataDir + "tilesets.xml";
	if (!wxFileExists(tilesetsFile)) return;

	pugi::xml_document doc;
	if (!doc.load_file(tilesetsFile.ToStdString().c_str())) return;
	pugi::xml_node materials = doc.child("materials");
	if (!materials) return;

	for (pugi::xml_node tilesetNode = materials.child("tileset"); tilesetNode; tilesetNode = tilesetNode.next_sibling("tileset")) {
		pugi::xml_attribute nameAttr = tilesetNode.attribute("name");
		if (!nameAttr || wxString(nameAttr.as_string()) != tilesetName) continue;
		pugi::xml_node terrain = tilesetNode.child("terrain");
		if (!terrain) continue;
		for (pugi::xml_node brushNode = terrain.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
			pugi::xml_attribute bn = brushNode.attribute("name");
			if (bn) m_tilesetBrushList->Append(wxString(bn.as_string()));
		}
	}
}

void BorderEditorDialog::OnTilesetSelectionChanged(wxCommandEvent& WXUNUSED(event)) {
	RefreshTilesetBrushList();
}

void BorderEditorDialog::OnAddGroundToTileset(wxCommandEvent& event) {
	wxString brushName = m_nameCtrl->GetValue().Trim(true).Trim(false);
	if (brushName.IsEmpty()) {
		wxMessageBox("Please enter a name for the ground brush first.", "Error", wxICON_ERROR);
		return;
	}

	wxString tilesetName = StripTerrainSuffix(m_tilesetCombo->GetValue());
	if (tilesetName.IsEmpty()) {
		wxMessageBox("Please select or type a tileset name.", "Error", wxICON_ERROR);
		return;
	}

	// Capture insertion intent before we touch the XML.
	int insertMode = m_tilesetInsertPosition ? m_tilesetInsertPosition->GetSelection() : 2;
	wxString afterBrushName;
	if (insertMode == 1) { // After selected
		int sel = m_tilesetBrushList ? m_tilesetBrushList->GetSelection() : wxNOT_FOUND;
		if (sel == wxNOT_FOUND) {
			wxMessageBox("'After selected' was chosen but no brush is selected in the list.\n"
				"Pick a reference brush, or switch to 'At start' / 'At end'.",
				"Error", wxICON_ERROR);
			return;
		}
		afterBrushName = m_tilesetBrushList->GetString(sel);
	}

	wxString dataDir = GetVersionDataDirectory();
	wxString tilesetsFile = dataDir + "tilesets.xml";

	if (!wxFileExists(tilesetsFile)) {
		wxMessageBox("Cannot find tilesets.xml in the data directory.", "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(tilesetsFile.ToStdString().c_str());
	if (!result) {
		wxMessageBox("Failed to load tilesets.xml: " + wxString(result.description()), "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		wxMessageBox("Invalid tilesets.xml: missing 'materials' node.", "Error", wxICON_ERROR);
		return;
	}

	// Prefer a tileset block that already has <terrain>; otherwise fall back to
	// the first block with the matching name (so we can append a <terrain> to it).
	pugi::xml_node targetTileset;
	pugi::xml_node fallbackTileset;
	for (pugi::xml_node tilesetNode = materials.child("tileset"); tilesetNode; tilesetNode = tilesetNode.next_sibling("tileset")) {
		pugi::xml_attribute nameAttr = tilesetNode.attribute("name");
		if (!nameAttr || wxString(nameAttr.as_string()) != tilesetName) continue;
		if (!fallbackTileset) fallbackTileset = tilesetNode;
		if (tilesetNode.child("terrain")) {
			targetTileset = tilesetNode;
			break;
		}
	}
	if (!targetTileset) targetTileset = fallbackTileset;

	if (!targetTileset) {
		if (wxMessageBox("Tileset '" + tilesetName + "' does not exist. Create it?",
				"Create Tileset", wxYES_NO | wxICON_QUESTION) != wxYES) {
			return;
		}
		targetTileset = materials.append_child("tileset");
		targetTileset.append_attribute("name").set_value(tilesetName.ToStdString().c_str());
	}

	// Find or create the <terrain> child (ground brushes live under terrain)
	pugi::xml_node terrain = targetTileset.child("terrain");
	if (!terrain) {
		terrain = targetTileset.append_child("terrain");
	}

	// Check for duplicates
	for (pugi::xml_node brushNode = terrain.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (nameAttr && wxString(nameAttr.as_string()) == brushName) {
			wxMessageBox("Brush '" + brushName + "' is already in tileset '" + tilesetName + "'.",
				"Already Exists", wxICON_INFORMATION);
			return;
		}
	}

	// Insert at the chosen position.
	pugi::xml_node newBrush;
	if (insertMode == 0) { // At start
		newBrush = terrain.prepend_child("brush");
	} else if (insertMode == 1) { // After selected
		pugi::xml_node anchor;
		for (pugi::xml_node brushNode = terrain.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
			pugi::xml_attribute nameAttr = brushNode.attribute("name");
			if (nameAttr && wxString(nameAttr.as_string()) == afterBrushName) {
				anchor = brushNode;
				break;
			}
		}
		if (anchor) {
			newBrush = terrain.insert_child_after("brush", anchor);
		} else {
			// Anchor disappeared between refresh and click — fall back to append.
			newBrush = terrain.append_child("brush");
		}
	} else { // At end (default)
		newBrush = terrain.append_child("brush");
	}
	newBrush.append_attribute("name").set_value(brushName.ToStdString().c_str());

	if (!doc.save_file(tilesetsFile.ToStdString().c_str())) {
		wxMessageBox("Failed to save tilesets.xml.", "Error", wxICON_ERROR);
		return;
	}

	wxMessageBox("Brush '" + brushName + "' added to tileset '" + tilesetName + "'.\n"
		"Restart the editor (or reload the client) to see it in the palette.",
		"Success", wxICON_INFORMATION);

	LoadExistingTilesets();
	// Restore the "(Terrain)" suffix on the combobox so the brush list refreshes
	// with the up-to-date contents.
	m_tilesetCombo->SetValue(tilesetName + " (Terrain)");
	RefreshTilesetBrushList();
}

void BorderEditorDialog::OnLoadGroundBrush(wxCommandEvent& event) {
	// Resolve which brush the user wants. Prefer the selected index (dropdown click);
	// fall back to matching the typed text against the brush names.
	wxString wantedName;
	int selection = m_existingGroundBrushesCombo->GetSelection();
	if (selection > 0) {
		wantedName = m_existingGroundBrushesCombo->GetString(selection);
	} else {
		wantedName = m_existingGroundBrushesCombo->GetValue().Trim(true).Trim(false);

		// Typed text doesn't need to be the full name: accept an exact match,
		// otherwise a unique prefix, otherwise a unique substring.
		if (!wantedName.IsEmpty()) {
			wxString typedLower = wantedName.Lower();
			wxString prefixMatch, substringMatch;
			int prefixCount = 0, substringCount = 0;
			bool exact = false;
			for (unsigned int i = 1; i < m_existingGroundBrushesCombo->GetCount(); ++i) {
				wxString itemName = m_existingGroundBrushesCombo->GetString(i);
				wxString itemLower = itemName.Lower();
				if (itemLower == typedLower) {
					exact = true;
					break;
				}
				if (itemLower.StartsWith(typedLower)) {
					prefixMatch = itemName;
					++prefixCount;
				} else if (itemLower.Contains(typedLower)) {
					substringMatch = itemName;
					++substringCount;
				}
			}
			if (!exact) {
				if (prefixCount == 1) {
					wantedName = prefixMatch;
				} else if (prefixCount == 0 && substringCount == 1) {
					wantedName = substringMatch;
				}
			}
		}
	}

	if (wantedName.IsEmpty() || wantedName == "<Create New>") {
		ClearGroundItems();
		return;
	}

	wxString dataDir = GetVersionDataDirectory();
	wxString groundsFile = dataDir + "grounds.xml";

	if (!wxFileExists(groundsFile)) return;

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(groundsFile.ToStdString().c_str());
	if (!result) return;

	ClearGroundItems();

	bool found = false;
	pugi::xml_node materials = doc.child("materials");
	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute typeAttr = brushNode.attribute("type");
		if (!typeAttr || std::string(typeAttr.as_string()) != "ground") continue;

		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (!nameAttr) continue;

		if (wxString(nameAttr.as_string()).CmpNoCase(wantedName) != 0) continue;

		found = true;
		m_nameCtrl->SetValue(wxString(nameAttr.as_string()));

		pugi::xml_attribute serverLookIdAttr = brushNode.attribute("server_lookid");
		if (serverLookIdAttr) {
			m_serverLookIdCtrl->SetValue(serverLookIdAttr.as_int());
		}

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

		// Load border references
		for (pugi::xml_node borderNode = brushNode.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
			pugi::xml_attribute idAttr = borderNode.attribute("id");
			if (!idAttr) continue;

			pugi::xml_attribute alignAttr = borderNode.attribute("align");
			pugi::xml_attribute toAttr = borderNode.attribute("to");
			pugi::xml_attribute enabledAttr = borderNode.attribute("enabled");

			GroundBorderRef ref;
			ref.borderId = idAttr.as_int();
			ref.align = alignAttr ? alignAttr.as_string() : "outer";
			ref.to = toAttr ? toAttr.as_string() : "";
			ref.enabled = enabledAttr ? enabledAttr.as_bool() : true;

			m_groundBorders.push_back(ref);
		}

		break;
	}

	if (!found) {
		wxMessageBox(wxString::Format("Ground brush '%s' not found in grounds.xml.", wantedName),
			"Not Found", wxICON_WARNING);
		return;
	}

	UpdateGroundItemsList();
	RefreshGroundBordersList();

	m_existingGroundBrushesCombo->SetValue(wantedName);

	// Look up which tileset(s) this brush belongs to and fill the tileset combo
	if (m_tilesetCombo) {
		m_tilesetCombo->SetValue("");
		wxString tilesetsFile = dataDir + "tilesets.xml";
		if (wxFileExists(tilesetsFile)) {
			pugi::xml_document tsDoc;
			if (tsDoc.load_file(tilesetsFile.ToStdString().c_str())) {
				pugi::xml_node tsMaterials = tsDoc.child("materials");
				if (tsMaterials) {
					for (pugi::xml_node tsNode = tsMaterials.child("tileset"); tsNode; tsNode = tsNode.next_sibling("tileset")) {
						pugi::xml_attribute tsNameAttr = tsNode.attribute("name");
						if (!tsNameAttr) continue;

						// Ground brushes live under <terrain><brush name=".."/></terrain>
						pugi::xml_node terrain = tsNode.child("terrain");
						if (!terrain) continue;

						for (pugi::xml_node brushRef = terrain.child("brush"); brushRef; brushRef = brushRef.next_sibling("brush")) {
							pugi::xml_attribute brNameAttr = brushRef.attribute("name");
							if (brNameAttr && wantedName.Cmp(brNameAttr.as_string()) == 0) {
								m_tilesetCombo->SetValue(tsNameAttr.as_string());
								break;
							}
						}

						if (!m_tilesetCombo->GetValue().IsEmpty()) break;
					}
				}
			}
		}
	}
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

void BorderEditorDialog::OnFindBorderByItemId(wxCommandEvent& event) {
	if (!m_findBorderItemIdCtrl) return;

	uint16_t wantedId = static_cast<uint16_t>(m_findBorderItemIdCtrl->GetValue());
	if (wantedId == 0) {
		wxMessageBox("Enter an item ID greater than 0.", "Find by Item ID", wxICON_INFORMATION);
		return;
	}

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
	int foundBorderId = -1;
	for (pugi::xml_node borderNode = materials.child("border"); borderNode && foundBorderId < 0; borderNode = borderNode.next_sibling("border")) {
		pugi::xml_attribute idAttr = borderNode.attribute("id");
		if (!idAttr) continue;

		for (pugi::xml_node itemNode = borderNode.child("borderitem"); itemNode; itemNode = itemNode.next_sibling("borderitem")) {
			// New format: <borderitem edge="n"><item id="..." .../>...</borderitem>
			for (pugi::xml_node subItem = itemNode.child("item"); subItem; subItem = subItem.next_sibling("item")) {
				pugi::xml_attribute subIdAttr = subItem.attribute("id");
				if (subIdAttr && subIdAttr.as_uint() == wantedId) {
					foundBorderId = idAttr.as_int();
					break;
				}
			}
			if (foundBorderId >= 0) break;

			// Legacy format: <borderitem edge="n" item="..."/>
			pugi::xml_attribute itemAttr = itemNode.attribute("item");
			if (itemAttr && itemAttr.as_uint() == wantedId) {
				foundBorderId = idAttr.as_int();
				break;
			}
		}
	}

	if (foundBorderId < 0) {
		wxMessageBox(wxString::Format("No border uses item ID %u.", wantedId),
			"Not Found", wxICON_WARNING);
		return;
	}

	// Find the matching entry in the combo and select it
	for (unsigned int i = 1; i < m_existingBordersCombo->GetCount(); ++i) {
		wxStringClientData* data = static_cast<wxStringClientData*>(m_existingBordersCombo->GetClientObject(i));
		if (data && wxAtoi(data->GetData()) == foundBorderId) {
			m_existingBordersCombo->SetSelection(i);
			wxCommandEvent evt(wxEVT_COMBOBOX, ID_EXISTING_BORDERS_COMBO);
			evt.SetEventObject(m_existingBordersCombo);
			OnLoadBorder(evt);
			return;
		}
	}

	wxMessageBox(wxString::Format("Border %d uses item %u, but it is not listed in the dropdown.", foundBorderId, wantedId),
		"Not Found", wxICON_WARNING);
}

void BorderEditorDialog::OnFindGroundByItemId(wxCommandEvent& event) {
	if (!m_findGroundItemIdCtrl) return;

	uint16_t wantedId = static_cast<uint16_t>(m_findGroundItemIdCtrl->GetValue());
	if (wantedId == 0) {
		wxMessageBox("Enter an item ID greater than 0.", "Find by Item ID", wxICON_INFORMATION);
		return;
	}

	wxString dataDir = GetVersionDataDirectory();
	if (dataDir.IsEmpty()) return;

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
	wxString foundName;
	for (pugi::xml_node brushNode = materials.child("brush"); brushNode && foundName.IsEmpty(); brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute typeAttr = brushNode.attribute("type");
		if (!typeAttr || std::string(typeAttr.as_string()) != "ground") continue;

		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (!nameAttr) continue;

		// Match against server_lookid or any <item id="..."/>
		pugi::xml_attribute serverLookIdAttr = brushNode.attribute("server_lookid");
		if (serverLookIdAttr && serverLookIdAttr.as_uint() == wantedId) {
			foundName = nameAttr.as_string();
			break;
		}

		for (pugi::xml_node itemNode = brushNode.child("item"); itemNode; itemNode = itemNode.next_sibling("item")) {
			pugi::xml_attribute idAttr = itemNode.attribute("id");
			if (idAttr && idAttr.as_uint() == wantedId) {
				foundName = nameAttr.as_string();
				break;
			}
		}
	}

	if (foundName.IsEmpty()) {
		wxMessageBox(wxString::Format("No ground brush uses item ID %u.", wantedId),
			"Not Found", wxICON_WARNING);
		return;
	}

	// Select in combo and trigger load
	m_existingGroundBrushesCombo->SetValue(foundName);
	wxCommandEvent evt(wxEVT_COMBOBOX, ID_EXISTING_GROUNDS_COMBO);
	evt.SetEventObject(m_existingGroundBrushesCombo);
	OnLoadGroundBrush(evt);
}

void BorderEditorDialog::ClearGroundItems() {
	m_groundItems.clear();
	m_groundBorders.clear();
	m_nameCtrl->SetValue("");
	m_idCtrl->SetValue(m_nextBorderId);
	m_serverLookIdCtrl->SetValue(0);
	m_zOrderCtrl->SetValue(0);
	m_groundItemIdCtrl->SetValue(0);
	m_groundItemChanceCtrl->SetValue(10);
	if (m_groundBordersList) {
		m_groundBordersList->Clear();
	}
	if (m_groundLoadPreview) {
		m_groundLoadPreview->Clear();
	}
	UpdateGroundItemsList();
}

void BorderEditorDialog::UpdateGroundItemsList() {
	if (m_groundItemsPanel) {
		m_groundItemsPanel->SetItems(m_groundItems);
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

	// Add explicit border references first
	for (const GroundBorderRef& ref : m_groundBorders) {
		if (ref.borderId <= 0) continue;
		pugi::xml_node borderRef = brushNode.append_child("border");
		if (!ref.align.empty()) {
			borderRef.append_attribute("align").set_value(ref.align.c_str());
		}
		if (!ref.to.empty()) {
			borderRef.append_attribute("to").set_value(ref.to.c_str());
		}
		borderRef.append_attribute("id").set_value(ref.borderId);
		if (!ref.enabled) {
			borderRef.append_attribute("enabled").set_value("false");
		}
	}

	// Legacy fallback: if no explicit borders were added and the user authored a border in the Border tab,
	// reference it so the ground brush still works.
	if (m_groundBorders.empty() && borderId > 0 && !m_borderItems.empty()) {
		pugi::xml_node borderRef = brushNode.append_child("border");
		borderRef.append_attribute("align").set_value("outer");
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

void BorderGridPanel::SetItemCount(BorderEdgePosition pos, int count) {
	if (pos >= 0 && pos < EDGE_COUNT) {
		m_itemCounts[pos] = count;
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
	m_itemCounts.clear();
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
			if (!edgeAttr) continue;

			uint16_t itemId = 0;
			// Support both formats for sample loading
			pugi::xml_node firstChild = itemNode.child("item");
			if (firstChild) {
				pugi::xml_attribute subIdAttr = firstChild.attribute("id");
				if (subIdAttr) itemId = subIdAttr.as_uint();
			} else {
				pugi::xml_attribute itemAttr = itemNode.attribute("item");
				if (itemAttr) itemId = itemAttr.as_uint();
			}

			BorderEdgePosition pos = edgeStringToPosition(edgeAttr.as_string());
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
			if (!edgeAttr) continue;

			uint16_t itemId = 0;
			pugi::xml_node firstChild = itemNode.child("item");
			if (firstChild) {
				pugi::xml_attribute subIdAttr = firstChild.attribute("id");
				if (subIdAttr) itemId = subIdAttr.as_uint();
			} else {
				pugi::xml_attribute itemAttr = itemNode.attribute("item");
				if (itemAttr) itemId = itemAttr.as_uint();
			}

			BorderEdgePosition pos = edgeStringToPosition(edgeAttr.as_string());
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

			// Draw item count badge if more than 1
			auto countIt = m_itemCounts.find(pos);
			if (countIt != m_itemCounts.end() && countIt->second > 1) {
				wxString badge = wxString::Format("x%d", countIt->second);
				dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
				wxSize badgeSize = dc.GetTextExtent(badge);

				int badgeX = x + cellW - badgeSize.GetWidth() - 1;
				int badgeY = y;

				// Badge background
				dc.SetBrush(wxBrush(wxColour(40, 40, 40, 200)));
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.DrawRoundedRectangle(badgeX - 2, badgeY, badgeSize.GetWidth() + 4, badgeSize.GetHeight() + 2, 3);

				dc.SetTextForeground(wxColour(100, 200, 255));
				dc.DrawText(badge, badgeX, badgeY);

				// Reset
				dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
				dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
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

	// Update the edge items list when selecting a position
	wxWindow* parent = GetParent();
	while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}
	BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);
	if (dialog) {
		dialog->UpdateEdgeItemsList();
	}

	event.Skip();
}

void BorderGridPanel::OnRightClick(wxMouseEvent& event) {
	BorderEdgePosition pos = GetPositionFromCoordinates(event.GetX(), event.GetY());
	if (pos == EDGE_NONE) return;

	uint16_t itemId = GetItemId(pos);
	if (itemId == 0) return;

	// Remove all items from this position
	m_items.erase(pos);
	m_itemCounts.erase(pos);
	SetSelectedPosition(EDGE_NONE);

	// Remove from parent dialog's border items list and update preview
	wxWindow* parent = GetParent();
	while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}

	BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);
	if (dialog) {
		dialog->m_borderItems.erase(pos);
		dialog->UpdatePreview();
		dialog->UpdateEdgeItemsList();
	}

	Refresh();
}

// ============================================================================
// EdgeItemsPanel
// ============================================================================

EdgeItemsPanel::EdgeItemsPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxSize(-1, CELL_SIZE + 2 * CELL_MARGIN), wxBORDER_NONE) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(-1, CELL_SIZE + 2 * CELL_MARGIN));
}

void EdgeItemsPanel::SetItems(const std::vector<BorderEdgeItem>& items) {
	m_items = items;
	if (m_selectedIndex >= static_cast<int>(m_items.size())) {
		m_selectedIndex = -1;
	}
	RecalcLayout();
	Refresh();
}

void EdgeItemsPanel::Clear() {
	m_items.clear();
	m_cells.clear();
	m_selectedIndex = -1;
	Refresh();
}

void EdgeItemsPanel::RecalcLayout() {
	m_cells.clear();
	int x = CELL_MARGIN;
	int y = CELL_MARGIN;
	for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
		CellRect cell;
		cell.index = i;
		cell.bounds = wxRect(x, y, CELL_SIZE, CELL_SIZE);
		cell.closeBtn = wxRect(x + CELL_SIZE - CLOSE_BTN_SIZE - 2, y + 2, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
		m_cells.push_back(cell);
		x += CELL_SIZE + CELL_MARGIN;
	}
}

void EdgeItemsPanel::OnPaint(wxPaintEvent& event) {
	wxAutoBufferedPaintDC dc(this);

	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	if (m_items.empty()) {
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
		dc.DrawText("No items. Select a direction and add items.", CELL_MARGIN, CELL_MARGIN + 4);
		return;
	}

	const int SPRITE_PADDING = 4;
	const int spriteArea = CELL_SIZE - 2 * SPRITE_PADDING;

	for (const auto& cell : m_cells) {
		const auto& item = m_items[cell.index];

		// Cell background
		bool selected = (cell.index == m_selectedIndex);
		if (selected) {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Selected)));
		} else {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
		}
		dc.DrawRoundedRectangle(cell.bounds, 3);

		// Sprite
		const auto itemDef = g_item_definitions.get(item.itemId);
		if (itemDef) {
			Sprite* sprite = g_gui.gfx.getSprite(itemDef.clientId());
			if (sprite) {
				sprite->DrawTo(&dc, SPRITE_SIZE_32x32,
					cell.bounds.x + SPRITE_PADDING,
					cell.bounds.y + SPRITE_PADDING,
					spriteArea, spriteArea);
			}
		}

		// Chance label at bottom
		dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		wxString chanceLabel = wxString::Format("%d%%", item.chance);
		wxSize ts = dc.GetTextExtent(chanceLabel);
		dc.DrawText(chanceLabel,
			cell.bounds.x + (CELL_SIZE - ts.GetWidth()) / 2,
			cell.bounds.y + CELL_SIZE - ts.GetHeight() - 2);

		// Close button (X)
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(wxColour(180, 50, 50)));
		dc.DrawRoundedRectangle(cell.closeBtn, 2);

		dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
		dc.SetTextForeground(*wxWHITE);
		wxSize xSize = dc.GetTextExtent("X");
		dc.DrawText("X",
			cell.closeBtn.x + (cell.closeBtn.width - xSize.GetWidth()) / 2,
			cell.closeBtn.y + (cell.closeBtn.height - xSize.GetHeight()) / 2);
	}
}

void EdgeItemsPanel::OnMouseClick(wxMouseEvent& event) {
	int mx = event.GetX();
	int my = event.GetY();

	for (const auto& cell : m_cells) {
		// Check close button first
		if (cell.closeBtn.Contains(mx, my)) {
			// Find parent BorderEditorDialog and call remove
			wxWindow* parent = GetParent();
			while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
				parent = parent->GetParent();
			}
			BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);
			if (dialog) {
				dialog->OnRemoveBorderItem(cell.index);
			}
			return;
		}

		// Select cell
		if (cell.bounds.Contains(mx, my)) {
			m_selectedIndex = cell.index;

			// Update chance control in parent dialog
			wxWindow* parent = GetParent();
			while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
				parent = parent->GetParent();
			}
			BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);
			if (dialog && cell.index < static_cast<int>(m_items.size())) {
				dialog->m_itemChanceCtrl->SetValue(m_items[cell.index].chance);
			}

			Refresh();
			return;
		}
	}

	// Clicked outside any cell - deselect
	m_selectedIndex = -1;
	Refresh();
}

// ============================================================================
// GroundItemsPanel
// ============================================================================

GroundItemsPanel::GroundItemsPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxSize(-1, 3 * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN), wxBORDER_NONE) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	// Reserve space for 3 rows of sprite cells so the panel never collapses to a thin strip
	// when the parent dialog opens at its default size.
	SetMinSize(wxSize(-1, 3 * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN));
}

void GroundItemsPanel::SetItems(const std::vector<GroundItem>& items) {
	m_items = items;
	if (m_selectedIndex >= static_cast<int>(m_items.size())) {
		m_selectedIndex = -1;
	}
	RecalcLayout();
	Refresh();
}

void GroundItemsPanel::Clear() {
	m_items.clear();
	m_cells.clear();
	m_selectedIndex = -1;
	Refresh();
}

void GroundItemsPanel::AddItemFromDrop(uint16_t itemId) {
	wxWindow* parent = GetParent();
	while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}
	BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);
	if (dialog) {
		dialog->AddGroundItemById(itemId);
	}
}

void GroundItemsPanel::RecalcLayout() {
	m_cells.clear();

	int clientWidth = GetClientSize().GetWidth();
	if (clientWidth <= 0) clientWidth = CELL_SIZE + 2 * CELL_MARGIN;

	int perRow = std::max(1, (clientWidth - CELL_MARGIN) / (CELL_SIZE + CELL_MARGIN));

	int x = CELL_MARGIN;
	int y = CELL_MARGIN;
	int col = 0;

	for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
		CellRect cell;
		cell.index = i;
		cell.bounds = wxRect(x, y, CELL_SIZE, CELL_SIZE);
		cell.closeBtn = wxRect(x + CELL_SIZE - CLOSE_BTN_SIZE - 2, y + 2, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
		m_cells.push_back(cell);

		col++;
		if (col >= perRow) {
			col = 0;
			x = CELL_MARGIN;
			y += CELL_SIZE + CELL_MARGIN;
		} else {
			x += CELL_SIZE + CELL_MARGIN;
		}
	}
}

void GroundItemsPanel::OnSize(wxSizeEvent& event) {
	RecalcLayout();
	Refresh();
	event.Skip();
}

void GroundItemsPanel::OnPaint(wxPaintEvent& event) {
	wxAutoBufferedPaintDC dc(this);

	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	if (m_items.empty()) {
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
		dc.DrawText("No items. Drag from the palette or use the Add button.", CELL_MARGIN, CELL_MARGIN + 4);
		return;
	}

	const int SPRITE_PADDING = 4;
	const int spriteArea = CELL_SIZE - 2 * SPRITE_PADDING;

	// Compute total chance for percentage display
	int totalChance = 0;
	for (const auto& item : m_items) totalChance += item.chance;
	if (totalChance <= 0) totalChance = 1;

	for (const auto& cell : m_cells) {
		const auto& item = m_items[cell.index];

		// Cell background
		bool selected = (cell.index == m_selectedIndex);
		if (selected) {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Selected)));
		} else {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
		}
		dc.DrawRoundedRectangle(cell.bounds, 3);

		// Sprite
		const auto itemDef = g_item_definitions.get(item.itemId);
		if (itemDef) {
			Sprite* sprite = g_gui.gfx.getSprite(itemDef.clientId());
			if (sprite) {
				sprite->DrawTo(&dc, SPRITE_SIZE_32x32,
					cell.bounds.x + SPRITE_PADDING,
					cell.bounds.y + SPRITE_PADDING,
					spriteArea, spriteArea);
			}
		}

		// Chance label at bottom: "raw (pct%)"
		dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		int pct = static_cast<int>((static_cast<double>(item.chance) / totalChance) * 100.0 + 0.5);
		wxString chanceLabel = wxString::Format("%d (%d%%)", item.chance, pct);
		wxSize ts = dc.GetTextExtent(chanceLabel);
		dc.DrawText(chanceLabel,
			cell.bounds.x + (CELL_SIZE - ts.GetWidth()) / 2,
			cell.bounds.y + CELL_SIZE - ts.GetHeight() - 2);

		// Close button (X)
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(wxColour(180, 50, 50)));
		dc.DrawRoundedRectangle(cell.closeBtn, 2);

		dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
		dc.SetTextForeground(*wxWHITE);
		wxSize xSize = dc.GetTextExtent("X");
		dc.DrawText("X",
			cell.closeBtn.x + (cell.closeBtn.width - xSize.GetWidth()) / 2,
			cell.closeBtn.y + (cell.closeBtn.height - xSize.GetHeight()) / 2);
	}
}

void GroundItemsPanel::OnMouseClick(wxMouseEvent& event) {
	int mx = event.GetX();
	int my = event.GetY();

	wxWindow* parent = GetParent();
	while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}
	BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);

	for (const auto& cell : m_cells) {
		// Check close button first
		if (cell.closeBtn.Contains(mx, my)) {
			if (dialog) {
				dialog->OnRemoveGroundItem(cell.index);
			}
			return;
		}

		// Select cell
		if (cell.bounds.Contains(mx, my)) {
			m_selectedIndex = cell.index;

			if (dialog && cell.index < static_cast<int>(m_items.size())) {
				dialog->m_groundItemIdCtrl->SetValue(m_items[cell.index].itemId);
				dialog->m_groundItemChanceCtrl->SetValue(m_items[cell.index].chance);
			}

			Refresh();
			return;
		}
	}

	// Clicked outside any cell - deselect
	m_selectedIndex = -1;
	Refresh();
}

// ============================================================================
// BorderNorthPreview
// ============================================================================

BorderNorthPreview::BorderNorthPreview(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxSize(40, 40), wxBORDER_SIMPLE) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(40, 40));
}

void BorderNorthPreview::SetItemId(uint16_t itemId) {
	if (m_itemId == itemId) return;
	m_itemId = itemId;
	Refresh();
}

void BorderNorthPreview::Clear() {
	m_itemId = 0;
	Refresh();
}

void BorderNorthPreview::OnPaint(wxPaintEvent& event) {
	wxAutoBufferedPaintDC dc(this);

	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Surface)));
	dc.Clear();

	if (m_itemId == 0) return;

	const auto itemDef = g_item_definitions.get(m_itemId);
	if (!itemDef) return;

	Sprite* sprite = g_gui.gfx.getSprite(itemDef.clientId());
	if (!sprite) return;

	wxSize size = GetClientSize();
	sprite->DrawTo(&dc, SPRITE_SIZE_32x32, 0, 0, size.GetWidth(), size.GetHeight());
}

// ============================================================================
// GroundBordersPanel
// ============================================================================

static uint16_t LookupBorderNorthItem(int borderId, const wxString& dataDir) {
	if (borderId <= 0 || dataDir.IsEmpty()) return 0;

	wxString bordersFile = dataDir + "borders.xml";
	if (!wxFileExists(bordersFile)) return 0;

	pugi::xml_document doc;
	if (!doc.load_file(bordersFile.ToStdString().c_str())) return 0;

	pugi::xml_node materials = doc.child("materials");
	for (pugi::xml_node borderNode = materials.child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
		pugi::xml_attribute idAttr = borderNode.attribute("id");
		if (!idAttr || idAttr.as_int() != borderId) continue;

		for (pugi::xml_node item = borderNode.child("borderitem"); item; item = item.next_sibling("borderitem")) {
			pugi::xml_attribute edgeAttr = item.attribute("edge");
			pugi::xml_attribute itemAttr = item.attribute("item");
			if (edgeAttr && itemAttr && std::string(edgeAttr.as_string()) == "n") {
				return static_cast<uint16_t>(itemAttr.as_uint());
			}
		}
		break;
	}
	return 0;
}

GroundBordersPanel::GroundBordersPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxSize(-1, 3 * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN), wxBORDER_NONE) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	// Reserve space for 3 rows so the Borders list is visible without having to resize.
	SetMinSize(wxSize(-1, 3 * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN));
}

void GroundBordersPanel::SetItems(const std::vector<GroundBorderRef>& items) {
	m_items = items;
	if (m_selectedIndex >= static_cast<int>(m_items.size())) {
		m_selectedIndex = -1;
	}
	RecalcNorthSprites();
	RecalcLayout();
	Refresh();
}

void GroundBordersPanel::Clear() {
	m_items.clear();
	m_northItemIds.clear();
	m_cells.clear();
	m_selectedIndex = -1;
	Refresh();
}

void GroundBordersPanel::SetSelectedIndex(int idx) {
	if (idx < -1 || idx >= static_cast<int>(m_items.size())) return;
	m_selectedIndex = idx;
	Refresh();
}

void GroundBordersPanel::RecalcNorthSprites() {
	m_northItemIds.clear();
	m_northItemIds.reserve(m_items.size());

	wxWindow* parent = GetParent();
	while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}
	BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);
	wxString dataDir = dialog ? dialog->GetVersionDataDirectory() : wxString();

	for (const auto& ref : m_items) {
		m_northItemIds.push_back(LookupBorderNorthItem(ref.borderId, dataDir));
	}
}

void GroundBordersPanel::RecalcLayout() {
	m_cells.clear();

	int clientWidth = GetClientSize().GetWidth();
	if (clientWidth <= 0) clientWidth = CELL_SIZE + 2 * CELL_MARGIN;

	int perRow = std::max(1, (clientWidth - CELL_MARGIN) / (CELL_SIZE + CELL_MARGIN));

	int x = CELL_MARGIN;
	int y = CELL_MARGIN;
	int col = 0;

	for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
		CellRect cell;
		cell.index = i;
		cell.bounds = wxRect(x, y, CELL_SIZE, CELL_SIZE);
		cell.closeBtn = wxRect(x + CELL_SIZE - CLOSE_BTN_SIZE - 2, y + 2, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
		cell.enableBtn = wxRect(x + 2, y + 2, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
		m_cells.push_back(cell);

		col++;
		if (col >= perRow) {
			col = 0;
			x = CELL_MARGIN;
			y += CELL_SIZE + CELL_MARGIN;
		} else {
			x += CELL_SIZE + CELL_MARGIN;
		}
	}
}

void GroundBordersPanel::OnSize(wxSizeEvent& event) {
	RecalcLayout();
	Refresh();
	event.Skip();
}

void GroundBordersPanel::OnPaint(wxPaintEvent& event) {
	wxAutoBufferedPaintDC dc(this);

	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	if (m_items.empty()) {
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
		dc.DrawText("No borders. Add one using the controls below.", CELL_MARGIN, CELL_MARGIN + 4);
		return;
	}

	const int SPRITE_PADDING = 4;
	const int LABEL_HEIGHT = 24;
	const int spriteAreaSide = CELL_SIZE - 2 * SPRITE_PADDING - LABEL_HEIGHT;

	for (const auto& cell : m_cells) {
		const auto& ref = m_items[cell.index];
		uint16_t northId = (cell.index < static_cast<int>(m_northItemIds.size())) ? m_northItemIds[cell.index] : 0;

		// Cell background
		bool selected = (cell.index == m_selectedIndex);
		bool disabled = !ref.enabled;
		if (selected) {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Selected)));
		} else if (disabled) {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Background)));
		} else {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
		}
		dc.DrawRoundedRectangle(cell.bounds, 3);

		// Sprite of the north edge
		if (northId > 0) {
			const auto itemDef = g_item_definitions.get(northId);
			if (itemDef) {
				Sprite* sprite = g_gui.gfx.getSprite(itemDef.clientId());
				if (sprite) {
					int sx = cell.bounds.x + (CELL_SIZE - spriteAreaSide) / 2;
					int sy = cell.bounds.y + SPRITE_PADDING;
					sprite->DrawTo(&dc, SPRITE_SIZE_32x32, sx, sy, spriteAreaSide, spriteAreaSide);
				}
			}
		} else {
			// Placeholder "?" when we can't find the sprite
			dc.SetFont(wxFont(18, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
			dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
			wxSize qs = dc.GetTextExtent("?");
			dc.DrawText("?",
				cell.bounds.x + (CELL_SIZE - qs.GetWidth()) / 2,
				cell.bounds.y + (spriteAreaSide - qs.GetHeight()) / 2 + SPRITE_PADDING);
		}

		// Labels at bottom: line 1 = align (+ to), line 2 = id
		dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));

		wxString line1 = wxString(ref.align);
		if (!ref.to.empty()) {
			line1 += "\xE2\x86\x92" + wxString(ref.to); // arrow
		}
		wxString line2 = wxString::Format("id=%d", ref.borderId);

		// Clip long lines with ellipsis if needed
		int maxTextW = CELL_SIZE - 4;
		auto clip = [&](wxString s) {
			if (dc.GetTextExtent(s).GetWidth() <= maxTextW) return s;
			while (s.length() > 1 && dc.GetTextExtent(s + "...").GetWidth() > maxTextW) {
				s.RemoveLast();
			}
			return s + "...";
		};
		line1 = clip(line1);
		line2 = clip(line2);

		wxSize t1 = dc.GetTextExtent(line1);
		wxSize t2 = dc.GetTextExtent(line2);

		int line2Y = cell.bounds.y + CELL_SIZE - t2.GetHeight() - 2;
		int line1Y = line2Y - t1.GetHeight() - 1;

		dc.DrawText(line1, cell.bounds.x + (CELL_SIZE - t1.GetWidth()) / 2, line1Y);
		dc.DrawText(line2, cell.bounds.x + (CELL_SIZE - t2.GetWidth()) / 2, line2Y);

		// Close button (X)
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(wxColour(180, 50, 50)));
		dc.DrawRoundedRectangle(cell.closeBtn, 2);

		dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
		dc.SetTextForeground(*wxWHITE);
		wxSize xSize = dc.GetTextExtent("X");
		dc.DrawText("X",
			cell.closeBtn.x + (cell.closeBtn.width - xSize.GetWidth()) / 2,
			cell.closeBtn.y + (cell.closeBtn.height - xSize.GetHeight()) / 2);

		// Enable toggle (green check when enabled, gray dash when disabled)
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(ref.enabled ? wxColour(60, 150, 80) : wxColour(90, 90, 90)));
		dc.DrawRoundedRectangle(cell.enableBtn, 2);

		dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
		dc.SetTextForeground(*wxWHITE);
		wxString toggleGlyph = ref.enabled ? wxString::FromUTF8("\xE2\x9C\x93") : wxString("-");
		wxSize tSize = dc.GetTextExtent(toggleGlyph);
		dc.DrawText(toggleGlyph,
			cell.enableBtn.x + (cell.enableBtn.width - tSize.GetWidth()) / 2,
			cell.enableBtn.y + (cell.enableBtn.height - tSize.GetHeight()) / 2);
	}
}

void GroundBordersPanel::OnMouseClick(wxMouseEvent& event) {
	int mx = event.GetX();
	int my = event.GetY();

	wxWindow* parent = GetParent();
	while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}
	BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);

	for (const auto& cell : m_cells) {
		if (cell.closeBtn.Contains(mx, my)) {
			// Remove via dialog so it can also update its internal vector
			if (dialog && cell.index < static_cast<int>(m_items.size())) {
				m_selectedIndex = cell.index;
				wxCommandEvent evt(wxEVT_BUTTON, wxID_ANY);
				dialog->OnRemoveGroundBorder(evt);
			}
			return;
		}

		if (cell.enableBtn.Contains(mx, my)) {
			if (cell.index < static_cast<int>(m_items.size()) && dialog) {
				// Keep dialog's m_groundBorders in sync with our m_items
				m_items[cell.index].enabled = !m_items[cell.index].enabled;
				if (cell.index < static_cast<int>(dialog->m_groundBorders.size())) {
					dialog->m_groundBorders[cell.index].enabled = m_items[cell.index].enabled;
				}
				Refresh();
			}
			return;
		}

		if (cell.bounds.Contains(mx, my)) {
			m_selectedIndex = cell.index;
			Refresh();
			return;
		}
	}

	m_selectedIndex = -1;
	Refresh();
}

void GroundBordersPanel::OnMouseDClick(wxMouseEvent& event) {
	int mx = event.GetX();
	int my = event.GetY();

	wxWindow* parent = GetParent();
	while (parent && !dynamic_cast<BorderEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}
	BorderEditorDialog* dialog = dynamic_cast<BorderEditorDialog*>(parent);

	for (const auto& cell : m_cells) {
		// The X and enable buttons keep their single-click behavior
		if (cell.closeBtn.Contains(mx, my) || cell.enableBtn.Contains(mx, my)) {
			return;
		}

		if (cell.bounds.Contains(mx, my)) {
			m_selectedIndex = cell.index;
			Refresh();
			if (dialog) {
				dialog->EditGroundBorder(cell.index);
			}
			return;
		}
	}
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
