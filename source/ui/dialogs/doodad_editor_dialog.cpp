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
#include "ui/dialogs/doodad_editor_dialog.h"
#include "ui/find_item_window.h"
#include "rendering/core/graphics.h"
#include "ui/gui.h"
#include "editor/editor.h"
#include "editor/selection.h"
#include "map/tile.h"
#include "brushes/brush.h"
#include "brushes/doodad/doodad_brush.h"
#include "brushes/raw/raw_brush.h"
#include "item_definitions/core/item_definition_store.h"
#include "rendering/core/game_sprite.h"
#include "rendering/utilities/sprite_icon_generator.h"
#include "ui/theme.h"
#include "app/managers/version_manager.h"
#include "ext/pugixml.hpp"
#include <wx/sizer.h>
#include <wx/gbsizer.h>
#include <wx/listbox.h>
#include <wx/radiobox.h>
#include <wx/statline.h>
#include <wx/dcbuffer.h>
#include <wx/dnd.h>
#include <wx/clipbrd.h>
#include <algorithm>
#include <climits>
#include <map>
#include <set>
#include <sstream>

// IDs for controls
enum {
    ID_SINGLE_ITEM_LIST = wxID_HIGHEST + 1,
    ID_COMPOSITES_LIST,
    ID_ADD_SINGLE_ITEM,
    ID_REMOVE_SINGLE_ITEM,
    ID_BROWSE_SINGLE_ITEM,
    ID_NEW_COMPOSITE,
    ID_REMOVE_COMPOSITE,
    ID_CLEAR_GRID,
    ID_CLEAR_ALL_TILES,
    ID_COMPOSITE_FROM_SELECTION,
    ID_GRID_LAYER,
    ID_GRID_ORIGIN_X,
    ID_GRID_ORIGIN_Y,
    ID_GRID_FIT_VIEW,
    ID_COMPOSITE_CHANCE,
    ID_GRID_ITEM_ID,
    ID_BROWSE_GRID_ITEM,
    ID_LOAD_TIMER,
    ID_FILTER_TEXT,
    ID_FIND_SERVER_ID,
    ID_DOODAD_LIST,
    ID_PREV_PAGE,
    ID_NEXT_PAGE,
    ID_CREATE_NEW,
    ID_SAVE_TO_FILE,
    ID_ADD_TO_TILESET_DOODAD,
    ID_TILESET_COMBO_DOODAD,
    ID_TILESET_BRUSH_LIST_DOODAD
};

// Helper function to get item ID from current brush
static uint16_t GetItemIDFromCurrentBrush() {
    Brush* brush = g_gui.GetCurrentBrush();
    if (!brush) return 0;

    if (brush->is<RAWBrush>()) {
        auto* rawBrush = brush->as<RAWBrush>();
        if (rawBrush) return rawBrush->getItemID();
    }

    uint16_t id = brush->getID();
    if (id > 0) return id;

    return brush->getLookID();
}

// Drop target for the grid panel
class DoodadGridDropTarget : public wxTextDropTarget {
public:
    explicit DoodadGridDropTarget(DoodadGridPanel* grid, DoodadEditorDialog* dialog)
        : m_grid(grid), m_dialog(dialog) {}

    bool OnDropText(wxCoord x, wxCoord y, const wxString& data) override {
        if (!m_grid) {
            return false;
        }

        wxString idStr;
        if (data.StartsWith("ITEM_ID:")) {
            idStr = data.Mid(8);
        } else if (data.StartsWith("RME_ITEM:")) {
            idStr = data.Mid(9);
        } else {
            return false;
        }
        long itemId = 0;
        if (!idStr.ToLong(&itemId) || itemId <= 0 || itemId > 0xFFFF) {
            return false;
        }

        int gridX, gridY;
        m_grid->GetCellFromCoordinates(x, y, gridX, gridY);
        if (gridX < 0 || gridX >= DOODAD_GRID_SIZE || gridY < 0 || gridY >= DOODAD_GRID_SIZE) {
            return false;
        }

        m_grid->SetSelectedCell(gridX, gridY);
        m_dialog->ApplyItemToGridPosition(gridX, gridY, static_cast<uint16_t>(itemId));
        return true;
    }

private:
    DoodadGridPanel* m_grid;
    DoodadEditorDialog* m_dialog;
};

// Drop target for the single items list
class DoodadSingleItemDropTarget : public wxTextDropTarget {
public:
    explicit DoodadSingleItemDropTarget(DoodadEditorDialog* dialog) : m_dialog(dialog) {}

    bool OnDropText(wxCoord /*x*/, wxCoord /*y*/, const wxString& data) override {
        if (!m_dialog) return false;

        wxString idStr;
        if (data.StartsWith("ITEM_ID:")) {
            idStr = data.Mid(8);
        } else if (data.StartsWith("RME_ITEM:")) {
            idStr = data.Mid(9);
        } else {
            return false;
        }

        unsigned long itemId = 0;
        if (!idStr.ToULong(&itemId) || itemId == 0 || itemId > 0xFFFF) {
            return false;
        }

        m_dialog->AddSingleItemById(static_cast<uint16_t>(itemId));
        return true;
    }

private:
    DoodadEditorDialog* m_dialog;
};

// Event tables
BEGIN_EVENT_TABLE(DoodadEditorDialog, wxPanel)
    EVT_BUTTON(ID_ADD_SINGLE_ITEM, DoodadEditorDialog::OnAddSingleItem)
    EVT_BUTTON(ID_REMOVE_SINGLE_ITEM, DoodadEditorDialog::OnRemoveSingleItem)
    EVT_BUTTON(ID_BROWSE_SINGLE_ITEM, DoodadEditorDialog::OnBrowseSingleItem)
    EVT_BUTTON(ID_NEW_COMPOSITE, DoodadEditorDialog::OnNewComposite)
    EVT_BUTTON(ID_REMOVE_COMPOSITE, DoodadEditorDialog::OnRemoveComposite)
    EVT_BUTTON(ID_CLEAR_GRID, DoodadEditorDialog::OnClearGrid)
    EVT_BUTTON(ID_CLEAR_ALL_TILES, DoodadEditorDialog::OnClearAllTiles)
    EVT_BUTTON(ID_COMPOSITE_FROM_SELECTION, DoodadEditorDialog::OnAddCompositeFromSelection)
    EVT_BUTTON(ID_GRID_FIT_VIEW, DoodadEditorDialog::OnGridFitView)
    EVT_SPINCTRL(ID_GRID_LAYER, DoodadEditorDialog::OnGridViewChanged)
    EVT_SPINCTRL(ID_GRID_ORIGIN_X, DoodadEditorDialog::OnGridViewChanged)
    EVT_SPINCTRL(ID_GRID_ORIGIN_Y, DoodadEditorDialog::OnGridViewChanged)
    EVT_BUTTON(ID_BROWSE_GRID_ITEM, DoodadEditorDialog::OnBrowseGridItem)
    EVT_BUTTON(wxID_SAVE, DoodadEditorDialog::OnSave)
    EVT_BUTTON(ID_SAVE_TO_FILE, DoodadEditorDialog::OnSaveToFile)
    EVT_BUTTON(ID_PREV_PAGE, DoodadEditorDialog::OnPrevPage)
    EVT_BUTTON(ID_NEXT_PAGE, DoodadEditorDialog::OnNextPage)
    EVT_BUTTON(ID_CREATE_NEW, DoodadEditorDialog::OnCreateNew)
    EVT_LISTBOX(ID_COMPOSITES_LIST, DoodadEditorDialog::OnCompositeSelected)
    EVT_LIST_ITEM_SELECTED(ID_DOODAD_LIST, DoodadEditorDialog::OnDoodadListSelected)
    EVT_SPINCTRL(ID_COMPOSITE_CHANCE, DoodadEditorDialog::OnCompositeChanceChanged)
    EVT_SPINCTRL(ID_GRID_ITEM_ID, DoodadEditorDialog::OnGridItemIdChanged)
    EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, DoodadEditorDialog::OnPageChanged)
    EVT_TIMER(ID_LOAD_TIMER, DoodadEditorDialog::OnLoadTimer)
    EVT_TEXT(ID_FILTER_TEXT, DoodadEditorDialog::OnFilterChanged)
    EVT_BUTTON(ID_FIND_SERVER_ID, DoodadEditorDialog::OnFindByServerId)
    EVT_BUTTON(ID_ADD_TO_TILESET_DOODAD, DoodadEditorDialog::OnAddToTileset)
    EVT_COMBOBOX(ID_TILESET_COMBO_DOODAD, DoodadEditorDialog::OnTilesetSelectionChanged)
    EVT_TEXT(ID_TILESET_COMBO_DOODAD, DoodadEditorDialog::OnTilesetSelectionChanged)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(DoodadGridPanel, wxPanel)
    EVT_PAINT(DoodadGridPanel::OnPaint)
    EVT_LEFT_UP(DoodadGridPanel::OnMouseClick)
    EVT_LEFT_DOWN(DoodadGridPanel::OnMouseDown)
    EVT_RIGHT_UP(DoodadGridPanel::OnMouseRightUp)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(DoodadPreviewPanel, wxPanel)
    EVT_PAINT(DoodadPreviewPanel::OnPaint)
END_EVENT_TABLE()

// ============================================================================
// DoodadEditorDialog Implementation
// ============================================================================

DoodadEditorDialog::DoodadEditorDialog(wxWindow* parent) :
    wxPanel(parent, wxID_ANY),
    m_currentCompositeIndex(-1),
    m_activeTab(0),
    m_loadTimer(nullptr),
    m_isLoading(true),
    m_currentPage(0),
    m_totalPages(0) {

    // Nulled before CreateGUIControls() so the null guards in UpdateGridInfoLabel() and
    // friends are meaningful even if something calls them mid-construction.
    m_gridLayerCtrl = nullptr;
    m_gridOriginXCtrl = nullptr;
    m_gridOriginYCtrl = nullptr;
    m_gridInfoLabel = nullptr;
    m_gridStackCheck = nullptr;
    m_fromSelIncludeGroundCheck = nullptr;
    m_fromSelReplaceCheck = nullptr;
    m_fromSelAnchorChoice = nullptr;

    // Compact font (-1pt) cascades to every child control. Matches the Border editor.
    wxFont compactFont = GetFont();
    compactFont.SetPointSize(std::max(6, compactFont.GetPointSize() - 1));
    SetFont(compactFont);

    CreateGUIControls();
    LoadExistingTilesets();

    // Start async loading after window is shown
    m_loadTimer = new wxTimer(this, ID_LOAD_TIMER);
    m_loadTimer->StartOnce(50); // Load after 50ms to let window render first
}

DoodadEditorDialog::~DoodadEditorDialog() {
    if (m_loadTimer) {
        m_loadTimer->Stop();
        delete m_loadTimer;
    }
}

void DoodadEditorDialog::OnLoadTimer(wxTimerEvent& event) {
    LoadExistingDoodads();
    m_isLoading = false;
    m_filterCtrl->Enable(true);
    UpdateDoodadList();
}

void DoodadEditorDialog::CreateGUIControls() {
    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // ========== LEFT PANEL - Doodad List ==========
    wxPanel* leftPanel = new wxPanel(this);
    wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);

    // Filter
    wxStaticBoxSizer* filterSizer = new wxStaticBoxSizer(wxVERTICAL, leftPanel, "Search Doodads");
    m_filterCtrl = new wxTextCtrl(leftPanel, ID_FILTER_TEXT, "", wxDefaultPosition, wxDefaultSize);
    m_filterCtrl->SetHint("Type to filter...");
    m_filterCtrl->Enable(false); // Disabled until loaded
    filterSizer->Add(m_filterCtrl, 0, wxEXPAND | wxALL, 5);

    // Find the doodad brush that contains a given server (item) id — mirrors the
    // "Find by Item ID" search in the Border/Wall editors.
    wxBoxSizer* findServerSizer = new wxBoxSizer(wxHORIZONTAL);
    findServerSizer->Add(new wxStaticText(leftPanel, wxID_ANY, "Server ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_findServerIdCtrl = new wxSpinCtrl(leftPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 65535);
    m_findServerIdCtrl->SetToolTip("Load the doodad brush that uses this item id (single item or composite).");
    findServerSizer->Add(m_findServerIdCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    findServerSizer->Add(new wxButton(leftPanel, ID_FIND_SERVER_ID, "Find", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
    filterSizer->Add(findServerSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    leftSizer->Add(filterSizer, 0, wxEXPAND | wxALL, 5);

    // Doodad list
    wxStaticBoxSizer* listSizer = new wxStaticBoxSizer(wxVERTICAL, leftPanel, "Doodad Brushes");

    m_doodadListCtrl = new wxListCtrl(leftPanel, ID_DOODAD_LIST, wxDefaultPosition, wxSize(250, -1),
        wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    m_doodadListCtrl->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 180);
    m_doodadListCtrl->InsertColumn(1, "Info", wxLIST_FORMAT_LEFT, 60);

    // Add "Loading..." item
    m_doodadListCtrl->InsertItem(0, "Loading...");

    listSizer->Add(m_doodadListCtrl, 1, wxEXPAND | wxALL, 5);

    // Pagination controls
    wxBoxSizer* pageSizer = new wxBoxSizer(wxHORIZONTAL);
    m_prevPageBtn = new wxButton(leftPanel, ID_PREV_PAGE, "<", wxDefaultPosition, wxSize(30, -1));
    m_prevPageBtn->Enable(false);
    pageSizer->Add(m_prevPageBtn, 0, wxRIGHT, 5);

    m_pageLabel = new wxStaticText(leftPanel, wxID_ANY, "Page 0/0");
    pageSizer->Add(m_pageLabel, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);

    m_nextPageBtn = new wxButton(leftPanel, ID_NEXT_PAGE, ">", wxDefaultPosition, wxSize(30, -1));
    m_nextPageBtn->Enable(false);
    pageSizer->Add(m_nextPageBtn, 0, wxLEFT, 5);

    listSizer->Add(pageSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    // Create New button
    wxButton* createNewBtn = new wxButton(leftPanel, ID_CREATE_NEW, "Create New Doodad");
    listSizer->Add(createNewBtn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    leftSizer->Add(listSizer, 1, wxEXPAND | wxALL, 5);

    leftPanel->SetSizer(leftSizer);
    mainSizer->Add(leftPanel, 0, wxEXPAND);

    // ========== RIGHT PANEL - Editor ==========
    wxPanel* rightPanel = new wxPanel(this);
    wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);

    // === Doodad Properties Section ===
    wxStaticBoxSizer* propsSizer = new wxStaticBoxSizer(wxVERTICAL, rightPanel, "Doodad Properties");

    // Row 1: Name and Look ID
    wxBoxSizer* row1Sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* nameSizer = new wxBoxSizer(wxVERTICAL);
    nameSizer->Add(new wxStaticText(rightPanel, wxID_ANY, "Brush Name:"), 0);
    m_nameCtrl = new wxTextCtrl(rightPanel, wxID_ANY, "", wxDefaultPosition, wxSize(200, -1));
    nameSizer->Add(m_nameCtrl, 0, wxEXPAND | wxTOP, 2);
    row1Sizer->Add(nameSizer, 1, wxEXPAND | wxRIGHT, 10);

    wxBoxSizer* lookIdSizer = new wxBoxSizer(wxVERTICAL);
    lookIdSizer->Add(new wxStaticText(rightPanel, wxID_ANY, "Server Look ID:"), 0);
    m_lookIdCtrl = new wxSpinCtrl(rightPanel, wxID_ANY, "0", wxDefaultPosition, wxSize(100, -1), wxSP_ARROW_KEYS, 0, 65535);
    lookIdSizer->Add(m_lookIdCtrl, 0, wxEXPAND | wxTOP, 2);
    row1Sizer->Add(lookIdSizer, 0, wxEXPAND);

    propsSizer->Add(row1Sizer, 0, wxEXPAND | wxALL, 5);

    // Row 2: Options
    wxBoxSizer* row2Sizer = new wxBoxSizer(wxHORIZONTAL);

    m_draggableCheck = new wxCheckBox(rightPanel, wxID_ANY, "Draggable");
    m_draggableCheck->SetValue(true);
    row2Sizer->Add(m_draggableCheck, 0, wxRIGHT, 15);

    m_onBlockingCheck = new wxCheckBox(rightPanel, wxID_ANY, "On Blocking");
    row2Sizer->Add(m_onBlockingCheck, 0, wxRIGHT, 15);

    m_onDuplicateCheck = new wxCheckBox(rightPanel, wxID_ANY, "On Duplicate");
    row2Sizer->Add(m_onDuplicateCheck, 0, wxRIGHT, 15);

    m_redoBordersCheck = new wxCheckBox(rightPanel, wxID_ANY, "Redo Borders");
    row2Sizer->Add(m_redoBordersCheck, 0, wxRIGHT, 15);

    m_oneSizeCheck = new wxCheckBox(rightPanel, wxID_ANY, "One Size");
    row2Sizer->Add(m_oneSizeCheck, 0, wxRIGHT, 15);

    m_saveAsAlternateCheck = new wxCheckBox(rightPanel, wxID_ANY, "Save as Alternate");
    m_saveAsAlternateCheck->SetToolTip("Wraps items/composites in an <alternate> block when saving.");
    row2Sizer->Add(m_saveAsAlternateCheck, 0);

    propsSizer->Add(row2Sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    // Row 3: Thickness
    wxBoxSizer* row3Sizer = new wxBoxSizer(wxHORIZONTAL);
    row3Sizer->Add(new wxStaticText(rightPanel, wxID_ANY, "Thickness:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_thicknessCtrl = new wxSpinCtrl(rightPanel, wxID_ANY, "25", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 0, 100);
    row3Sizer->Add(m_thicknessCtrl, 0, wxRIGHT, 5);
    row3Sizer->Add(new wxStaticText(rightPanel, wxID_ANY, "/"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_thicknessCeilingCtrl = new wxSpinCtrl(rightPanel, wxID_ANY, "100", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 0, 100);
    row3Sizer->Add(m_thicknessCeilingCtrl, 0);

    propsSizer->Add(row3Sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    rightSizer->Add(propsSizer, 0, wxEXPAND | wxALL, 5);

    // === Notebook with tabs ===
    m_notebook = new wxNotebook(rightPanel, wxID_ANY);

    // ========== SINGLE ITEMS TAB ==========
    m_singlePanel = new wxPanel(m_notebook);
    wxBoxSizer* singleSizer = new wxBoxSizer(wxVERTICAL);

    wxStaticBoxSizer* singleItemsSizer = new wxStaticBoxSizer(wxVERTICAL, m_singlePanel, "Single Items (non-composite)");

    m_singleItemsList = new DoodadSingleItemsPanel(m_singlePanel, ID_SINGLE_ITEM_LIST);
    m_singleItemsList->SetDropTarget(new DoodadSingleItemDropTarget(this));
    m_singleItemsList->SetToolTip("Drag items from the palette here to add them, or use the Add button below.");
    singleItemsSizer->Add(m_singleItemsList, 1, wxEXPAND | wxALL, 5);

    wxBoxSizer* singleControlsSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* itemIdSizer = new wxBoxSizer(wxVERTICAL);
    itemIdSizer->Add(new wxStaticText(m_singlePanel, wxID_ANY, "Item ID:"), 0);
    m_singleItemIdCtrl = new wxSpinCtrl(m_singlePanel, wxID_ANY, "0", wxDefaultPosition, wxSize(100, -1), wxSP_ARROW_KEYS, 0, 65535);
    itemIdSizer->Add(m_singleItemIdCtrl, 0, wxEXPAND | wxTOP, 2);
    singleControlsSizer->Add(itemIdSizer, 0, wxRIGHT, 10);

    wxBoxSizer* chanceSizer = new wxBoxSizer(wxVERTICAL);
    chanceSizer->Add(new wxStaticText(m_singlePanel, wxID_ANY, "Chance:"), 0);
    m_singleItemChanceCtrl = new wxSpinCtrl(m_singlePanel, wxID_ANY, "10", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1, 10000);
    chanceSizer->Add(m_singleItemChanceCtrl, 0, wxEXPAND | wxTOP, 2);
    singleControlsSizer->Add(chanceSizer, 0, wxRIGHT, 10);

    wxBoxSizer* btnSizer = new wxBoxSizer(wxVERTICAL);
    btnSizer->AddStretchSpacer();
    wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
    btnRow->Add(new wxButton(m_singlePanel, ID_BROWSE_SINGLE_ITEM, "Browse...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 5);
    btnRow->Add(new wxButton(m_singlePanel, ID_ADD_SINGLE_ITEM, "Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 5);
    btnRow->Add(new wxButton(m_singlePanel, ID_REMOVE_SINGLE_ITEM, "Remove", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
    btnSizer->Add(btnRow, 0);
    singleControlsSizer->Add(btnSizer, 0, wxEXPAND);

    singleItemsSizer->Add(singleControlsSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
    singleSizer->Add(singleItemsSizer, 1, wxEXPAND | wxALL, 5);

    m_singlePanel->SetSizer(singleSizer);
    m_notebook->AddPage(m_singlePanel, "Single Items");

    // ========== COMPOSITES TAB ==========
    m_compositePanel = new wxPanel(m_notebook);
    wxBoxSizer* compositeSizer = new wxBoxSizer(wxHORIZONTAL);

    // Left side: Composites list
    wxStaticBoxSizer* compositeListSizer = new wxStaticBoxSizer(wxVERTICAL, m_compositePanel, "Composites");

    m_compositesList = new wxListBox(m_compositePanel, ID_COMPOSITES_LIST, wxDefaultPosition, wxSize(160, -1), 0, nullptr, wxLB_SINGLE);
    compositeListSizer->Add(m_compositesList, 1, wxEXPAND | wxALL, 5);

    wxBoxSizer* compositeCtrlSizer = new wxBoxSizer(wxHORIZONTAL);
    compositeCtrlSizer->Add(new wxButton(m_compositePanel, ID_NEW_COMPOSITE, "New", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 5);
    compositeCtrlSizer->Add(new wxButton(m_compositePanel, ID_REMOVE_COMPOSITE, "Remove", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
    compositeListSizer->Add(compositeCtrlSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    wxBoxSizer* chanceCtrlSizer = new wxBoxSizer(wxHORIZONTAL);
    chanceCtrlSizer->Add(new wxStaticText(m_compositePanel, wxID_ANY, "Chance:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_compositeChanceCtrl = new wxSpinCtrl(m_compositePanel, ID_COMPOSITE_CHANCE, "10", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, 10000);
    chanceCtrlSizer->Add(m_compositeChanceCtrl, 0);
    compositeListSizer->Add(chanceCtrlSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    // Build a whole composite out of the current map selection instead of placing every
    // item by hand. Mirrors "Add Cluster From Selection" in the Area Decoration dialog.
    compositeListSizer->Add(new wxStaticLine(m_compositePanel), 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

    wxButton* fromSelBtn = new wxButton(m_compositePanel, ID_COMPOSITE_FROM_SELECTION, "From Selection");
    fromSelBtn->SetToolTip("Turn the current map selection into a composite.\n"
                           "X/Y are anchored on the selection, Z on the floor you are standing on.\n"
                           "The Brushes Editor is not modal: select on the map, then click here.");
    compositeListSizer->Add(fromSelBtn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

    wxString anchorChoices[] = { "Anchor: Center", "Anchor: Top-left" };
    m_fromSelAnchorChoice = new wxChoice(m_compositePanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 2, anchorChoices);
    m_fromSelAnchorChoice->SetSelection(0);
    m_fromSelAnchorChoice->SetToolTip("Where the (0,0) tile of the composite lands inside the selection.");
    compositeListSizer->Add(m_fromSelAnchorChoice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

    m_fromSelIncludeGroundCheck = new wxCheckBox(m_compositePanel, wxID_ANY, "Include ground");
    m_fromSelIncludeGroundCheck->SetValue(false);
    m_fromSelIncludeGroundCheck->SetToolTip("Off by default: a ground inside a doodad overwrites the terrain where the brush is painted.");
    compositeListSizer->Add(m_fromSelIncludeGroundCheck, 0, wxLEFT | wxRIGHT | wxTOP, 5);

    m_fromSelReplaceCheck = new wxCheckBox(m_compositePanel, wxID_ANY, "Replace current");
    m_fromSelReplaceCheck->SetValue(false);
    m_fromSelReplaceCheck->SetToolTip("Overwrite the selected composite instead of appending a new one.");
    compositeListSizer->Add(m_fromSelReplaceCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

    compositeSizer->Add(compositeListSizer, 0, wxEXPAND | wxALL, 5);

    // Center: Grid Editor
    wxStaticBoxSizer* gridSizer = new wxStaticBoxSizer(wxVERTICAL, m_compositePanel, "Composite Grid (10x10)");

    m_gridPanel = new DoodadGridPanel(m_compositePanel);
    m_gridPanel->SetDropTarget(new DoodadGridDropTarget(m_gridPanel, this));
    gridSizer->Add(m_gridPanel, 0, wxALL | wxALIGN_CENTER, 5);

    wxStaticText* instructions = new wxStaticText(m_compositePanel, wxID_ANY,
        "Click cell to select, then use current brush or enter Item ID. Right-click removes the top item.\n"
        "Green cell = position (0,0). The grid is a window: use Layer/View to reach tiles outside it.");
    instructions->SetForegroundColour(wxColour(100, 100, 200));
    gridSizer->Add(instructions, 0, wxALL, 5);

    // Window controls: which floor and which 10x10 region of the composite is on screen.
    wxBoxSizer* viewCtrlSizer = new wxBoxSizer(wxHORIZONTAL);
    viewCtrlSizer->Add(new wxStaticText(m_compositePanel, wxID_ANY, "Layer z:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
    m_gridLayerCtrl = new wxSpinCtrl(m_compositePanel, ID_GRID_LAYER, "0", wxDefaultPosition, wxSize(55, -1),
        wxSP_ARROW_KEYS, DOODAD_MIN_Z, DOODAD_MAX_Z, 0);
    m_gridLayerCtrl->SetToolTip("Floor offset being edited. 0 = the floor the brush is painted on, -1 = one floor above.");
    viewCtrlSizer->Add(m_gridLayerCtrl, 0, wxRIGHT, 10);

    viewCtrlSizer->Add(new wxStaticText(m_compositePanel, wxID_ANY, "View X/Y:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
    m_gridOriginXCtrl = new wxSpinCtrl(m_compositePanel, ID_GRID_ORIGIN_X, "-5", wxDefaultPosition, wxSize(60, -1),
        wxSP_ARROW_KEYS, -DOODAD_MAX_ORIGIN, DOODAD_MAX_ORIGIN, -DOODAD_GRID_CENTER);
    m_gridOriginXCtrl->SetToolTip("Relative X shown by the leftmost column — pan the window over big composites.");
    viewCtrlSizer->Add(m_gridOriginXCtrl, 0, wxRIGHT, 3);
    m_gridOriginYCtrl = new wxSpinCtrl(m_compositePanel, ID_GRID_ORIGIN_Y, "-5", wxDefaultPosition, wxSize(60, -1),
        wxSP_ARROW_KEYS, -DOODAD_MAX_ORIGIN, DOODAD_MAX_ORIGIN, -DOODAD_GRID_CENTER);
    m_gridOriginYCtrl->SetToolTip("Relative Y shown by the topmost row.");
    viewCtrlSizer->Add(m_gridOriginYCtrl, 0, wxRIGHT, 5);

    wxButton* fitBtn = new wxButton(m_compositePanel, ID_GRID_FIT_VIEW, "Fit", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    fitBtn->SetToolTip("Move the window to the busiest layer/region of this composite.");
    viewCtrlSizer->Add(fitBtn, 0, wxRIGHT, 10);

    m_gridStackCheck = new wxCheckBox(m_compositePanel, wxID_ANY, "Stack");
    m_gridStackCheck->SetToolTip("When checked, painting adds the item on top of the cell instead of replacing it.");
    viewCtrlSizer->Add(m_gridStackCheck, 0, wxALIGN_CENTER_VERTICAL);
    gridSizer->Add(viewCtrlSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    m_gridInfoLabel = new wxStaticText(m_compositePanel, wxID_ANY, "");
    m_gridInfoLabel->SetForegroundColour(wxColour(100, 100, 200));
    gridSizer->Add(m_gridInfoLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);

    wxBoxSizer* gridCtrlSizer = new wxBoxSizer(wxHORIZONTAL);
    gridCtrlSizer->Add(new wxStaticText(m_compositePanel, wxID_ANY, "Item ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_gridItemIdCtrl = new wxSpinCtrl(m_compositePanel, ID_GRID_ITEM_ID, "0", wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 0, 65535);
    gridCtrlSizer->Add(m_gridItemIdCtrl, 0, wxRIGHT, 5);
    gridCtrlSizer->Add(new wxButton(m_compositePanel, ID_BROWSE_GRID_ITEM, "Browse...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 5);
    wxButton* clearLayerBtn = new wxButton(m_compositePanel, ID_CLEAR_GRID, "Clear Layer", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    clearLayerBtn->SetToolTip("Clears only what the grid window is showing right now.");
    gridCtrlSizer->Add(clearLayerBtn, 0, wxRIGHT, 5);
    wxButton* clearAllBtn = new wxButton(m_compositePanel, ID_CLEAR_ALL_TILES, "Clear All", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    clearAllBtn->SetToolTip("Clears every tile of this composite, on every layer.");
    gridCtrlSizer->Add(clearAllBtn, 0);
    gridSizer->Add(gridCtrlSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    compositeSizer->Add(gridSizer, 1, wxEXPAND | wxALL, 5);

    // Right: Preview
    wxStaticBoxSizer* previewSizer = new wxStaticBoxSizer(wxVERTICAL, m_compositePanel, "Preview");
    m_previewPanel = new DoodadPreviewPanel(m_compositePanel);
    previewSizer->Add(m_previewPanel, 1, wxEXPAND | wxALL, 5);
    compositeSizer->Add(previewSizer, 0, wxEXPAND | wxALL, 5);

    m_compositePanel->SetSizer(compositeSizer);
    m_notebook->AddPage(m_compositePanel, "Composites");

    rightSizer->Add(m_notebook, 1, wxEXPAND | wxALL, 5);

    // === Assign to Tileset ===
    wxStaticBoxSizer* tilesetAssignSizer = new wxStaticBoxSizer(wxVERTICAL, rightPanel, "Assign to Tileset");

    // Row 1: Tileset combobox
    wxBoxSizer* tilesetRow = new wxBoxSizer(wxHORIZONTAL);
    tilesetRow->Add(new wxStaticText(rightPanel, wxID_ANY, "Tileset:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_tilesetCombo = new wxComboBox(rightPanel, ID_TILESET_COMBO_DOODAD, "", wxDefaultPosition, wxSize(220, -1), 0, nullptr, wxCB_DROPDOWN);
    m_tilesetCombo->SetToolTip("Pick a tileset. The (Doodad) suffix shows where the brush will be inserted.");
    tilesetRow->Add(m_tilesetCombo, 1, wxEXPAND);
    tilesetAssignSizer->Add(tilesetRow, 0, wxEXPAND | wxALL, 5);

    // Row 2: Existing brushes list
    tilesetAssignSizer->Add(new wxStaticText(rightPanel, wxID_ANY, "Existing brushes in this tileset (Doodad):"), 0, wxLEFT | wxRIGHT, 5);
    m_tilesetBrushList = new wxListBox(rightPanel, ID_TILESET_BRUSH_LIST_DOODAD, wxDefaultPosition, wxSize(-1, 110));
    m_tilesetBrushList->SetToolTip("Select a brush to use as reference for the 'After selected' insert option.");
    tilesetAssignSizer->Add(m_tilesetBrushList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

    // Row 3: Insert position radio
    wxString positions[] = { "At start", "After selected", "At end" };
    m_tilesetInsertPosition = new wxRadioBox(rightPanel, wxID_ANY, "Insert position",
        wxDefaultPosition, wxDefaultSize, 3, positions, 3, wxRA_SPECIFY_COLS);
    m_tilesetInsertPosition->SetSelection(2); // default: At end
    tilesetAssignSizer->Add(m_tilesetInsertPosition, 0, wxEXPAND | wxALL, 5);

    // Row 4: Add button
    wxBoxSizer* tilesetBtnRow = new wxBoxSizer(wxHORIZONTAL);
    tilesetBtnRow->AddStretchSpacer();
    m_addToTilesetButton = new wxButton(rightPanel, ID_ADD_TO_TILESET_DOODAD, "Add brush to Tileset");
    m_addToTilesetButton->SetToolTip("Adds the current doodad brush to the selected tileset's <doodad> section at the chosen position.");
    tilesetBtnRow->Add(m_addToTilesetButton, 0);
    tilesetAssignSizer->Add(tilesetBtnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    rightSizer->Add(tilesetAssignSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    // === Bottom buttons ===
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->AddStretchSpacer();
    buttonSizer->Add(new wxButton(rightPanel, wxID_SAVE, "Save to Clipboard"), 0, wxRIGHT, 5);
    buttonSizer->Add(new wxButton(rightPanel, ID_SAVE_TO_FILE, "Save to File"), 0, wxRIGHT, 5);
    rightSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, 10);

    rightPanel->SetSizer(rightSizer);
    mainSizer->Add(rightPanel, 1, wxEXPAND);

    SetSizer(mainSizer);
}

void DoodadEditorDialog::LoadExistingDoodads() {
    m_allDoodads.clear();

    // Get all doodad brushes from the brush manager
    const BrushMap& brushMap = g_brushes.getMap();
    for (const auto& pair : brushMap) {
        Brush* brush = pair.second.get();
        if (brush && brush->is<DoodadBrush>()) {
            auto* doodad = brush->as<DoodadBrush>();
            if (doodad) {
                DoodadBrushInfo info;
                info.name = wxString(doodad->getName());

                const auto& alternatives = doodad->getItems().getAlternatives();
                if (!alternatives.empty()) {
                    const auto& alt = alternatives[0];
                    info.compositeCount = alt->composite_items.size();
                    info.singleCount = alt->single_items.size();
                }

                m_allDoodads.push_back(info);
            }
        }
    }

    // Sort alphabetically
    std::sort(m_allDoodads.begin(), m_allDoodads.end(),
        [](const DoodadBrushInfo& a, const DoodadBrushInfo& b) {
            return a.name.CmpNoCase(b.name) < 0;
        });

    // Initialize filtered list with all doodads
    m_filteredDoodads = m_allDoodads;
    m_currentPage = 0;
    m_totalPages = (m_filteredDoodads.size() + DOODADS_PER_PAGE - 1) / DOODADS_PER_PAGE;
    if (m_totalPages == 0) m_totalPages = 1;
}

void DoodadEditorDialog::UpdateDoodadList() {
    m_doodadListCtrl->DeleteAllItems();

    int startIdx = m_currentPage * DOODADS_PER_PAGE;
    int endIdx = std::min(startIdx + DOODADS_PER_PAGE, (int)m_filteredDoodads.size());

    for (int i = startIdx; i < endIdx; i++) {
        const DoodadBrushInfo& info = m_filteredDoodads[i];
        long itemIdx = m_doodadListCtrl->InsertItem(i - startIdx, info.name);

        wxString infoStr;
        if (info.compositeCount > 0 && info.singleCount > 0) {
            infoStr = wxString::Format("C:%d S:%d", info.compositeCount, info.singleCount);
        } else if (info.compositeCount > 0) {
            infoStr = wxString::Format("C:%d", info.compositeCount);
        } else if (info.singleCount > 0) {
            infoStr = wxString::Format("S:%d", info.singleCount);
        }
        m_doodadListCtrl->SetItem(itemIdx, 1, infoStr);
    }

    // Update pagination
    m_pageLabel->SetLabel(wxString::Format("Page %d/%d", m_currentPage + 1, m_totalPages));
    m_prevPageBtn->Enable(m_currentPage > 0);
    m_nextPageBtn->Enable(m_currentPage < m_totalPages - 1);
}

void DoodadEditorDialog::OnFilterChanged(wxCommandEvent& event) {
    if (m_isLoading) return;

    wxString filter = m_filterCtrl->GetValue().Lower();

    m_filteredDoodads.clear();

    for (const DoodadBrushInfo& info : m_allDoodads) {
        if (filter.IsEmpty() || info.name.Lower().Contains(filter)) {
            m_filteredDoodads.push_back(info);
        }
    }

    m_currentPage = 0;
    m_totalPages = (m_filteredDoodads.size() + DOODADS_PER_PAGE - 1) / DOODADS_PER_PAGE;
    if (m_totalPages == 0) m_totalPages = 1;

    UpdateDoodadList();
}

void DoodadEditorDialog::OnDoodadListSelected(wxListEvent& event) {
    int listIdx = event.GetIndex();
    int actualIdx = m_currentPage * DOODADS_PER_PAGE + listIdx;

    if (actualIdx >= 0 && actualIdx < (int)m_filteredDoodads.size()) {
        LoadDoodadBrush(m_filteredDoodads[actualIdx].name);
    }
}

void DoodadEditorDialog::OnPrevPage(wxCommandEvent& event) {
    if (m_currentPage > 0) {
        m_currentPage--;
        UpdateDoodadList();
    }
}

void DoodadEditorDialog::OnNextPage(wxCommandEvent& event) {
    if (m_currentPage < m_totalPages - 1) {
        m_currentPage++;
        UpdateDoodadList();
    }
}

void DoodadEditorDialog::OnCreateNew(wxCommandEvent& event) {
    ClearEditor();
    m_nameCtrl->SetValue("new_doodad");
    m_nameCtrl->SetFocus();
    m_nameCtrl->SelectAll();
}

void DoodadEditorDialog::LoadDoodadBrush(const wxString& brushName) {
    Brush* brush = g_brushes.getBrush(brushName.ToStdString());
    if (!brush || !brush->is<DoodadBrush>()) {
        return;
    }

    auto* doodad = brush->as<DoodadBrush>();
    if (!doodad) return;

    ClearEditor();

    // Load properties
    m_nameCtrl->SetValue(wxString(doodad->getName()));
    m_lookIdCtrl->SetValue(doodad->getLookID());
    m_draggableCheck->SetValue(doodad->canSmear());
    m_onBlockingCheck->SetValue(doodad->placeOnBlocking());
    m_onDuplicateCheck->SetValue(doodad->placeOnDuplicate());
    m_redoBordersCheck->SetValue(doodad->doNewBorders());
    m_oneSizeCheck->SetValue(doodad->oneSizeFitsAll());
    m_thicknessCtrl->SetValue(doodad->getThickness());
    m_thicknessCeilingCtrl->SetValue(doodad->getThicknessCeiling());

    // Load items from first variation (variation 0)
    const auto& alternatives = doodad->getItems().getAlternatives();
    if (!alternatives.empty()) {
        const auto& alt = alternatives[0];

        // Load all single items
        for (const auto& single : alt->single_items) {
            uint16_t itemId = single.item ? single.item->getID() : 0;
            if (itemId > 0) {
                m_singleItems.push_back(DoodadSingleItem(itemId, single.chance));
            }
        }

        // Load all composite items
        for (const auto& comp : alt->composite_items) {
            if (!comp.items.empty()) {
                DoodadComposite doodadComp;
                doodadComp.chance = comp.chance;

                for (const auto& tilePair : comp.items) {
                    const Position& pos = tilePair.first;
                    const DoodadItemVector& items = tilePair.second;
                    for (const auto& item : items) {
                        DoodadTileItem tileItem;
                        tileItem.x = pos.x;
                        tileItem.y = pos.y;
                        tileItem.z = pos.z;
                        tileItem.itemId = item->getID();
                        doodadComp.tiles.push_back(tileItem);
                    }
                }

                m_composites.push_back(doodadComp);
            }
        }
    }

    UpdateSingleItemsList();
    UpdateCompositesList();

    if (!m_composites.empty()) {
        m_compositesList->SetSelection(0);
        m_currentCompositeIndex = 0;
        // Existing composites may sit anywhere in relative space (and on other floors) —
        // point the grid window at them instead of showing an empty -5..+4 slice.
        FitGridViewToComposite();
        UpdateGridFromComposite();
    }

    UpdatePreview();
}

wxString DoodadEditorDialog::FindDoodadBrushNameByItemId(uint16_t itemId) const {
    if (itemId == 0) return wxString();

    const BrushMap& brushMap = g_brushes.getMap();
    for (const auto& pair : brushMap) {
        Brush* brush = pair.second.get();
        if (brush && brush->is<DoodadBrush>()) {
            auto* doodad = brush->as<DoodadBrush>();
            if (doodad && doodad->getItems().ownsItem(itemId)) {
                return wxString(doodad->getName());
            }
        }
    }
    return wxString();
}

bool DoodadEditorDialog::OpenItemInEditor(uint16_t itemId) {
    wxString name = FindDoodadBrushNameByItemId(itemId);
    if (name.IsEmpty()) {
        return false;
    }

    LoadDoodadBrush(name);
    if (m_findServerIdCtrl) m_findServerIdCtrl->SetValue(itemId);

    // Jump to the tab that actually holds the item: Single Items if it's a single
    // entry, otherwise the Composites tab.
    bool isSingle = false;
    for (const auto& single : m_singleItems) {
        if (single.itemId == itemId) {
            isSingle = true;
            break;
        }
    }
    if (m_notebook) {
        m_notebook->SetSelection(isSingle ? 0 : 1);
    }
    return true;
}

void DoodadEditorDialog::OnFindByServerId(wxCommandEvent& WXUNUSED(event)) {
    uint16_t wanted = static_cast<uint16_t>(m_findServerIdCtrl->GetValue());
    if (wanted == 0) {
        wxMessageBox("Enter a server (item) ID to search for.", "Find by Server ID", wxICON_INFORMATION);
        return;
    }

    if (!OpenItemInEditor(wanted)) {
        wxMessageBox(wxString::Format("No doodad brush uses item ID %u.", static_cast<unsigned>(wanted)),
            "Not Found", wxICON_WARNING);
    }
}

void DoodadEditorDialog::ClearAll() {
    m_allDoodads.clear();
    m_filteredDoodads.clear();
    ClearEditor();
}

void DoodadEditorDialog::ClearEditor() {
    m_nameCtrl->Clear();
    m_lookIdCtrl->SetValue(0);
    m_draggableCheck->SetValue(true);
    m_onBlockingCheck->SetValue(false);
    m_onDuplicateCheck->SetValue(false);
    m_redoBordersCheck->SetValue(false);
    m_oneSizeCheck->SetValue(false);
    m_saveAsAlternateCheck->SetValue(false);
    m_thicknessCtrl->SetValue(25);
    m_thicknessCeilingCtrl->SetValue(100);

    m_singleItems.clear();
    m_composites.clear();
    m_currentCompositeIndex = -1;

    UpdateSingleItemsList();
    UpdateCompositesList();
    m_gridPanel->SetView(-DOODAD_GRID_CENTER, -DOODAD_GRID_CENTER, 0);
    SyncGridViewControls();
    m_gridPanel->Clear();
    m_previewPanel->Clear();
    UpdateGridInfoLabel();
}

void DoodadEditorDialog::OnAddSingleItem(wxCommandEvent& event) {
    uint16_t itemId = m_singleItemIdCtrl->GetValue();
    int chance = m_singleItemChanceCtrl->GetValue();

    if (itemId == 0) {
        itemId = GetItemIDFromCurrentBrush();
    }

    if (itemId == 0) {
        wxMessageBox("Please enter a valid item ID or select a brush.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    DoodadSingleItem item(itemId, chance);
    m_singleItems.push_back(item);
    UpdateSingleItemsList();
}

void DoodadEditorDialog::AddSingleItemById(uint16_t itemId) {
    if (itemId == 0) return;

    int chance = m_singleItemChanceCtrl ? m_singleItemChanceCtrl->GetValue() : 10;
    m_singleItems.push_back(DoodadSingleItem(itemId, chance));
    UpdateSingleItemsList();
}

void DoodadEditorDialog::OnRemoveSingleItem(wxCommandEvent& event) {
    RemoveSingleItemAt(m_singleItemsList->GetSelectedIndex());
}

void DoodadEditorDialog::RemoveSingleItemAt(int index) {
    if (index >= 0 && index < (int)m_singleItems.size()) {
        m_singleItems.erase(m_singleItems.begin() + index);
        UpdateSingleItemsList();
    }
}

void DoodadEditorDialog::SelectSingleItemAt(int index) {
    if (index >= 0 && index < (int)m_singleItems.size()) {
        m_singleItemIdCtrl->SetValue(m_singleItems[index].itemId);
        m_singleItemChanceCtrl->SetValue(m_singleItems[index].chance);
    }
}

void DoodadEditorDialog::OnBrowseSingleItem(wxCommandEvent& event) {
    FindItemDialog dialog(this, "Select Item");
    if (dialog.ShowModal() == wxID_OK) {
        uint16_t itemId = dialog.getResultID();
        if (itemId > 0) {
            m_singleItemIdCtrl->SetValue(itemId);
        }
    }
}

void DoodadEditorDialog::OnNewComposite(wxCommandEvent& event) {
    if (m_currentCompositeIndex >= 0) {
        UpdateCompositeFromGrid();
    }

    DoodadComposite comp;
    comp.chance = 10;
    m_composites.push_back(comp);

    m_currentCompositeIndex = (int)m_composites.size() - 1;
    RefreshCompositeListLabels();
    m_compositeChanceCtrl->SetValue(comp.chance);

    // A brand new composite always starts on the default window (-5..+4 of layer 0).
    m_gridPanel->SetView(-DOODAD_GRID_CENTER, -DOODAD_GRID_CENTER, 0);
    SyncGridViewControls();
    m_gridPanel->Clear();
    UpdateGridInfoLabel();
    UpdatePreview();
}

void DoodadEditorDialog::OnRemoveComposite(wxCommandEvent& event) {
    int sel = m_compositesList->GetSelection();
    if (sel != wxNOT_FOUND && sel < (int)m_composites.size()) {
        // Removing a composite other than the one on the grid must not drop its edits.
        if (m_currentCompositeIndex >= 0 && m_currentCompositeIndex != sel) {
            UpdateCompositeFromGrid();
        }
        m_composites.erase(m_composites.begin() + sel);
        m_currentCompositeIndex = -1;

        m_gridPanel->Clear();
        m_previewPanel->Clear();

        if (!m_composites.empty()) {
            // Keep the selection next to what was just removed instead of jumping to the top.
            m_currentCompositeIndex = std::min(sel, (int)m_composites.size() - 1);
            m_compositeChanceCtrl->SetValue(m_composites[m_currentCompositeIndex].chance);
            FitGridViewToComposite();
            UpdateGridFromComposite();
        }
        RefreshCompositeListLabels();
        UpdateGridInfoLabel();
        UpdatePreview();
    }
}

void DoodadEditorDialog::OnCompositeSelected(wxCommandEvent& event) {
    if (m_currentCompositeIndex >= 0 && m_currentCompositeIndex < (int)m_composites.size()) {
        UpdateCompositeFromGrid();
    }

    int sel = m_compositesList->GetSelection();
    if (sel != wxNOT_FOUND && sel < (int)m_composites.size()) {
        m_currentCompositeIndex = sel;
        m_compositeChanceCtrl->SetValue(m_composites[sel].chance);
        // Composites may live anywhere in relative space — point the window at this one.
        FitGridViewToComposite();
        UpdateGridFromComposite();
        UpdatePreview();
    }
}

void DoodadEditorDialog::OnCompositeChanceChanged(wxSpinEvent& event) {
    if (m_currentCompositeIndex >= 0 && m_currentCompositeIndex < (int)m_composites.size()) {
        m_composites[m_currentCompositeIndex].chance = m_compositeChanceCtrl->GetValue();
        UpdateCompositesList();
        m_compositesList->SetSelection(m_currentCompositeIndex);
    }
}

void DoodadEditorDialog::OnClearGrid(wxCommandEvent& event) {
    // Clears the visible window only. UpdateCompositeFromGrid() merges that emptiness back
    // into the composite, leaving other layers and off-window tiles alone.
    m_gridPanel->Clear();
    UpdateCompositeFromGrid();
    RefreshCompositeListLabels();
    UpdateGridInfoLabel();
    UpdatePreview();
}

void DoodadEditorDialog::OnClearAllTiles(wxCommandEvent& event) {
    if (m_currentCompositeIndex < 0 || m_currentCompositeIndex >= (int)m_composites.size()) {
        return;
    }

    const size_t count = m_composites[m_currentCompositeIndex].tiles.size();
    if (count > 0) {
        if (wxMessageBox(wxString::Format("Remove all %zu item(s) of this composite, on every layer?", count),
                "Clear All", wxYES_NO | wxICON_QUESTION, this) != wxYES) {
            return;
        }
    }

    m_composites[m_currentCompositeIndex].tiles.clear();
    m_gridPanel->Clear();
    RefreshCompositeListLabels();
    UpdateGridInfoLabel();
    UpdatePreview();
}

bool DoodadEditorDialog::BuildCompositeTilesFromSelection(std::vector<DoodadTileItem>& outTiles,
                                                          int& outFloorCount,
                                                          int& outDroppedZ) {
    outTiles.clear();
    outFloorCount = 0;
    outDroppedZ = 0;

    // GetCurrentFloor() reads the current map tab, so make sure there is one.
    if (!g_gui.IsEditorOpen()) {
        wxMessageBox("No map open. Open a map and select some tiles first.", "Error", wxOK | wxICON_ERROR, this);
        return false;
    }

    Editor* editor = g_gui.GetCurrentEditor();
    if (!editor) {
        wxMessageBox("No editor available.", "Error", wxOK | wxICON_ERROR, this);
        return false;
    }

    const Selection& selection = editor->selection;
    if (selection.size() == 0) {
        wxMessageBox("No tiles selected. Select some tiles on the map first.", "Error", wxOK | wxICON_ERROR, this);
        return false;
    }

    Position minPos(INT_MAX, INT_MAX, INT_MAX);
    Position maxPos(INT_MIN, INT_MIN, INT_MIN);

    const auto& tiles = selection.getTiles();
    for (Tile* tile : tiles) {
        const Position& pos = tile->getPosition();
        minPos.x = std::min(minPos.x, pos.x);
        minPos.y = std::min(minPos.y, pos.y);
        maxPos.x = std::max(maxPos.x, pos.x);
        maxPos.y = std::max(maxPos.y, pos.y);
    }

    // X/Y are anchored on the selection (center by default, top-left on request), but Z is
    // anchored on the floor the user is standing on: tiles of that floor get z == 0 so they
    // land on the tile being painted and the remaining floors stack relative to it. Same
    // rule as Area Decoration's cluster import.
    const bool anchorTopLeft = m_fromSelAnchorChoice && m_fromSelAnchorChoice->GetSelection() == 1;
    const Position anchor(anchorTopLeft ? minPos.x : (minPos.x + maxPos.x) / 2,
                          anchorTopLeft ? minPos.y : (minPos.y + maxPos.y) / 2,
                          g_gui.GetCurrentFloor());

    const bool includeGround = m_fromSelIncludeGroundCheck && m_fromSelIncludeGroundCheck->GetValue();
    std::set<int> floors;

    outTiles.reserve(tiles.size());

    for (Tile* tile : tiles) {
        const Position& pos = tile->getPosition();
        const int relX = pos.x - anchor.x;
        const int relY = pos.y - anchor.y;
        const int relZ = pos.z - anchor.z;

        // The doodad loader rejects anything outside this range.
        if (relZ < DOODAD_MIN_Z || relZ > DOODAD_MAX_Z) {
            ++outDroppedZ;
            continue;
        }

        const size_t before = outTiles.size();

        // Bottom to top, matching the order the map itself stacks them. Meta items are
        // editor-only markers (they have no client sprite), so they never belong in a brush.
        if (includeGround && tile->ground && tile->ground->isSelected() &&
            tile->ground->getID() > 0 && !tile->ground->isMetaItem()) {
            outTiles.push_back(DoodadTileItem(relX, relY, relZ, tile->ground->getID()));
        }
        for (const auto& item : tile->items) {
            if (item && item->isSelected() && item->getID() > 0 && !item->isMetaItem()) {
                outTiles.push_back(DoodadTileItem(relX, relY, relZ, item->getID()));
            }
        }

        if (outTiles.size() > before) {
            floors.insert(relZ);
        }
    }

    outFloorCount = (int)floors.size();

    if (outTiles.empty()) {
        wxMessageBox("The selected tiles contain no selected items.\n\n"
                     "Tiles can be selected with only their creature or spawn selected, and "
                     "\"Include ground\" is off by default.",
                     "Error", wxOK | wxICON_ERROR, this);
        return false;
    }

    return true;
}

void DoodadEditorDialog::OnAddCompositeFromSelection(wxCommandEvent& event) {
    std::vector<DoodadTileItem> newTiles;
    int floorCount = 0;
    int droppedZ = 0;
    if (!BuildCompositeTilesFromSelection(newTiles, floorCount, droppedZ)) {
        return;
    }

    // A whole-map selection would produce a doodads.xml nobody wants — ask first.
    if (newTiles.size() > 2000) {
        if (wxMessageBox(wxString::Format("This selection produces %zu item entries. Continue?", newTiles.size()),
                "Large composite", wxYES_NO | wxICON_WARNING, this) != wxYES) {
            return;
        }
    }

    if (m_currentCompositeIndex >= 0) {
        UpdateCompositeFromGrid();
    }

    const bool replace = m_fromSelReplaceCheck && m_fromSelReplaceCheck->GetValue() &&
                         m_currentCompositeIndex >= 0 && m_currentCompositeIndex < (int)m_composites.size();
    if (replace) {
        m_composites[m_currentCompositeIndex].tiles = std::move(newTiles);
    } else {
        DoodadComposite comp;
        comp.chance = m_compositeChanceCtrl ? m_compositeChanceCtrl->GetValue() : 10;
        comp.tiles = std::move(newTiles);
        m_composites.push_back(std::move(comp));
        m_currentCompositeIndex = (int)m_composites.size() - 1;
    }

    FitGridViewToComposite();
    UpdateGridFromComposite();
    RefreshCompositeListLabels();
    m_compositeChanceCtrl->SetValue(m_composites[m_currentCompositeIndex].chance);
    UpdatePreview();

    const size_t total = m_composites[m_currentCompositeIndex].tiles.size();
    // Counted only now: the window just moved (Fit), so anything measured before would be
    // reported against a window the user never saw.
    const int offWindow = CountItemsOutsideGridWindow();

    wxString msg = wxString::Format("Composite %d filled with %zu item(s)", m_currentCompositeIndex + 1, total);
    if (floorCount > 1) {
        msg << wxString::Format(", %d floors (anchored on floor %d)", floorCount, g_gui.GetCurrentFloor());
    }
    if (offWindow > 0) {
        msg << wxString::Format(", %d item(s) outside the grid window (kept - use Layer / View X/Y to reach them)", offWindow);
    }
    if (droppedZ > 0) {
        msg << wxString::Format(", %d tile(s) dropped for being more than %d floors away", droppedZ, DOODAD_MAX_Z);
    }
    msg << ".";

    g_gui.SetStatusText(msg);
    if (droppedZ > 0) {
        wxMessageBox(msg, "Composite imported", wxOK | wxICON_INFORMATION, this);
    }
}

void DoodadEditorDialog::OnBrowseGridItem(wxCommandEvent& event) {
    FindItemDialog dialog(this, "Select Item");
    if (dialog.ShowModal() == wxID_OK) {
        uint16_t itemId = dialog.getResultID();
        if (itemId > 0) {
            m_gridItemIdCtrl->SetValue(itemId);

            int gridX, gridY;
            m_gridPanel->GetSelectedCell(gridX, gridY);
            if (gridX >= 0 && gridY >= 0) {
                // Picking an id replaces what the cell shows; it never grows the stack.
                ApplyItemToGridPosition(gridX, gridY, itemId, false);
            }
        }
    }
}

void DoodadEditorDialog::OnGridItemIdChanged(wxSpinEvent& event) {
    uint16_t itemId = m_gridItemIdCtrl->GetValue();
    if (itemId > 0) {
        int gridX, gridY;
        m_gridPanel->GetSelectedCell(gridX, gridY);
        if (gridX >= 0 && gridY >= 0) {
            // Every arrow click fires this handler — replace the top item instead of
            // stacking, or holding the arrow would bury the cell in items.
            ApplyItemToGridPosition(gridX, gridY, itemId, false);
        }
    }
}

void DoodadEditorDialog::EnsureCurrentComposite() {
    if (m_currentCompositeIndex >= 0 && m_currentCompositeIndex < (int)m_composites.size()) {
        return;
    }

    // Deliberately does NOT touch the grid window: the caller already resolved a cell
    // against the window on screen, so moving it here would send the item to another
    // layer/offset than the one the user clicked.
    DoodadComposite comp;
    comp.chance = m_compositeChanceCtrl ? m_compositeChanceCtrl->GetValue() : 10;
    m_composites.push_back(comp);
    m_currentCompositeIndex = (int)m_composites.size() - 1;
    RefreshCompositeListLabels();
}

void DoodadEditorDialog::ApplyItemToGridPosition(int gridX, int gridY, uint16_t itemId, bool allowStack) {
    EnsureCurrentComposite();

    const bool stackMode = m_gridStackCheck && m_gridStackCheck->GetValue();
    if (itemId == 0 || !stackMode) {
        m_gridPanel->SetItemAt(gridX, gridY, itemId);
    } else if (allowStack) {
        m_gridPanel->PushItemAt(gridX, gridY, itemId);
    } else {
        m_gridPanel->ReplaceTopItemAt(gridX, gridY, itemId);
    }
    m_gridPanel->Refresh();
    UpdateCompositeFromGrid();
    RefreshCompositeListLabels();
    UpdateGridInfoLabel();
    UpdatePreview();
}

void DoodadEditorDialog::RemoveTopItemFromGridPosition(int gridX, int gridY) {
    if (m_currentCompositeIndex < 0 || m_currentCompositeIndex >= (int)m_composites.size()) {
        return;
    }

    m_gridPanel->PopItemAt(gridX, gridY);
    m_gridPanel->Refresh();
    UpdateCompositeFromGrid();
    RefreshCompositeListLabels();
    UpdateGridInfoLabel();
    UpdatePreview();
}

void DoodadEditorDialog::UpdateCompositeFromGrid() {
    if (m_currentCompositeIndex < 0 || m_currentCompositeIndex >= (int)m_composites.size()) {
        return;
    }

    auto& tiles = m_composites[m_currentCompositeIndex].tiles;
    const int layer = m_gridPanel->GetLayer();
    const int originX = m_gridPanel->GetOriginX();
    const int originY = m_gridPanel->GetOriginY();

    // Only the region the grid window currently owns is rewritten. Tiles on other layers or
    // outside the window survive untouched — that is what lets a composite be bigger than
    // 10x10, span floors and stack items without the grid eating it on the next click.
    tiles.erase(std::remove_if(tiles.begin(), tiles.end(), [&](const DoodadTileItem& tile) {
        return tile.z == layer &&
               tile.x >= originX && tile.x < originX + DOODAD_GRID_SIZE &&
               tile.y >= originY && tile.y < originY + DOODAD_GRID_SIZE;
    }), tiles.end());

    const std::vector<DoodadTileItem> visible = m_gridPanel->GetAllItems();
    tiles.insert(tiles.end(), visible.begin(), visible.end());

    // Stable sort keeps the stacking order inside each (x, y, z) group, so the grouped XML
    // emitter can merge adjacent entries and a save/load round trip preserves the stack.
    std::stable_sort(tiles.begin(), tiles.end(), [](const DoodadTileItem& a, const DoodadTileItem& b) {
        if (a.z != b.z) return a.z < b.z;
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
}

void DoodadEditorDialog::UpdateGridFromComposite() {
    m_gridPanel->Clear();

    if (m_currentCompositeIndex < 0 || m_currentCompositeIndex >= (int)m_composites.size()) {
        UpdateGridInfoLabel();
        return;
    }

    m_gridPanel->SetItems(m_composites[m_currentCompositeIndex].tiles);
    m_gridPanel->Refresh();
    m_previewPanel->SetItems(m_composites[m_currentCompositeIndex].tiles);
    UpdateGridInfoLabel();
}

void DoodadEditorDialog::SyncGridViewControls() {
    // SetValue() does not fire wxEVT_SPINCTRL, so this never re-enters OnGridViewChanged.
    if (m_gridLayerCtrl) m_gridLayerCtrl->SetValue(m_gridPanel->GetLayer());
    if (m_gridOriginXCtrl) m_gridOriginXCtrl->SetValue(m_gridPanel->GetOriginX());
    if (m_gridOriginYCtrl) m_gridOriginYCtrl->SetValue(m_gridPanel->GetOriginY());
}

void DoodadEditorDialog::OnGridViewChanged(wxSpinEvent& event) {
    // Order matters: flush what the grid holds using the OLD window, only then move it.
    // Moving first would make the merge erase the wrong region of the composite.
    UpdateCompositeFromGrid();
    m_gridPanel->SetView(m_gridOriginXCtrl->GetValue(),
                         m_gridOriginYCtrl->GetValue(),
                         m_gridLayerCtrl->GetValue());
    UpdateGridFromComposite();
    RefreshCompositeListLabels();
    UpdatePreview();
}

void DoodadEditorDialog::OnGridFitView(wxCommandEvent& event) {
    UpdateCompositeFromGrid();
    FitGridViewToComposite();
    UpdateGridFromComposite();
    UpdatePreview();
}

void DoodadEditorDialog::FitGridViewToComposite() {
    if (m_currentCompositeIndex < 0 || m_currentCompositeIndex >= (int)m_composites.size() ||
        m_composites[m_currentCompositeIndex].tiles.empty()) {
        m_gridPanel->SetView(-DOODAD_GRID_CENTER, -DOODAD_GRID_CENTER, 0);
        SyncGridViewControls();
        return;
    }

    const auto& tiles = m_composites[m_currentCompositeIndex].tiles;

    // Show the layer holding the most items; ties go to the layer closest to 0.
    std::map<int, int> perLayer;
    for (const auto& tile : tiles) {
        perLayer[tile.z]++;
    }
    auto distanceFromZero = [](int z) { return z < 0 ? -z : z; };
    int layer = 0;
    int best = -1;
    for (const auto& entry : perLayer) {
        if (entry.second > best ||
            (entry.second == best && distanceFromZero(entry.first) < distanceFromZero(layer))) {
            layer = entry.first;
            best = entry.second;
        }
    }

    int minX = INT_MAX, maxX = INT_MIN, minY = INT_MAX, maxY = INT_MIN;
    for (const auto& tile : tiles) {
        if (tile.z != layer) continue;
        minX = std::min(minX, tile.x);
        maxX = std::max(maxX, tile.x);
        minY = std::min(minY, tile.y);
        maxY = std::max(maxY, tile.y);
    }

    // Center the window on the layer bounding box when it fits, anchor on its top-left
    // corner when it does not — the user pans from there with the View spins.
    int originX = (maxX - minX + 1 <= DOODAD_GRID_SIZE)
        ? minX - (DOODAD_GRID_SIZE - (maxX - minX + 1)) / 2
        : minX;
    int originY = (maxY - minY + 1 <= DOODAD_GRID_SIZE)
        ? minY - (DOODAD_GRID_SIZE - (maxY - minY + 1)) / 2
        : minY;

    m_gridPanel->SetView(originX, originY, layer);
    SyncGridViewControls();
}

int DoodadEditorDialog::CountItemsOutsideGridWindow() const {
    if (m_currentCompositeIndex < 0 || m_currentCompositeIndex >= (int)m_composites.size()) {
        return 0;
    }

    int count = 0;
    for (const auto& tile : m_composites[m_currentCompositeIndex].tiles) {
        if (tile.z != m_gridPanel->GetLayer() || !m_gridPanel->IsInWindow(tile.x, tile.y)) {
            ++count;
        }
    }
    return count;
}

void DoodadEditorDialog::UpdateGridInfoLabel() {
    if (!m_gridInfoLabel) return;

    if (m_currentCompositeIndex < 0 || m_currentCompositeIndex >= (int)m_composites.size()) {
        m_gridInfoLabel->SetLabel("");
        return;
    }

    const auto& tiles = m_composites[m_currentCompositeIndex].tiles;
    std::set<int> layers;
    for (const auto& tile : tiles) {
        layers.insert(tile.z);
    }
    const int offWindow = CountItemsOutsideGridWindow();

    wxString label = wxString::Format("Layer z=%d | %zu item(s)", m_gridPanel->GetLayer(), tiles.size());
    if (layers.size() > 1) {
        wxString layerList;
        for (int z : layers) {
            if (!layerList.IsEmpty()) layerList << ", ";
            layerList << z;
        }
        label << " | layers: " << layerList;
    }
    if (offWindow > 0) {
        label << wxString::Format(" | %d item(s) outside this window", offWindow);
    }
    m_gridInfoLabel->SetLabel(label);
}

uint16_t DoodadEditorDialog::GetCurrentItemId() const {
    return m_gridItemIdCtrl->GetValue();
}

void DoodadEditorDialog::UpdateSingleItemsList() {
    m_singleItemsList->SetItems(m_singleItems);
}

void DoodadEditorDialog::UpdateCompositesList() {
    m_compositesList->Clear();
    int index = 1;
    for (const auto& comp : m_composites) {
        wxString label = wxString::Format("Composite %d (C:%d T:%zu)",
            index++, comp.chance, comp.tiles.size());
        m_compositesList->Append(label);
    }
}

void DoodadEditorDialog::RefreshCompositeListLabels() {
    // Rebuilding the listbox drops its selection, which would leave the highlight out of
    // sync with m_currentCompositeIndex — restore it here so the item count on the label
    // can be refreshed after every edit.
    UpdateCompositesList();
    if (m_currentCompositeIndex >= 0 && m_currentCompositeIndex < (int)m_composites.size()) {
        m_compositesList->SetSelection(m_currentCompositeIndex);
    }
}

void DoodadEditorDialog::UpdatePreview() {
    if (m_currentCompositeIndex >= 0 && m_currentCompositeIndex < (int)m_composites.size()) {
        m_previewPanel->SetItems(m_composites[m_currentCompositeIndex].tiles);
    } else {
        m_previewPanel->Clear();
    }
    m_previewPanel->Refresh();
}

void DoodadEditorDialog::OnPageChanged(wxBookCtrlEvent& event) {
    // Ignore events from the outer BrushesEditorDialog notebook — only react to our own.
    if (event.GetEventObject() != m_notebook) {
        event.Skip();
        return;
    }
    m_activeTab = event.GetSelection();
    event.Skip();
}

wxString DoodadEditorDialog::GenerateXML() {
    std::ostringstream xml;

    wxString name = m_nameCtrl->GetValue();
    if (name.IsEmpty()) {
        name = "new_doodad";
    }

    xml << "<brush name=\"" << name.ToStdString() << "\" type=\"doodad\"";
    xml << " server_lookid=\"" << m_lookIdCtrl->GetValue() << "\"";

    if (m_draggableCheck->GetValue()) {
        xml << " draggable=\"true\"";
    }
    if (m_onBlockingCheck->GetValue()) {
        xml << " on_blocking=\"true\"";
    } else {
        xml << " on_blocking=\"false\"";
    }
    if (m_onDuplicateCheck->GetValue()) {
        xml << " on_duplicate=\"true\"";
    }
    if (m_redoBordersCheck->GetValue()) {
        xml << " redo_borders=\"true\"";
    }
    if (m_oneSizeCheck->GetValue()) {
        xml << " one_size=\"true\"";
    }

    int thickness = m_thicknessCtrl->GetValue();
    int ceiling = m_thicknessCeilingCtrl->GetValue();
    if (thickness > 0 || ceiling > 0) {
        xml << " thickness=\"" << thickness << "/" << ceiling << "\"";
    }

    xml << ">\n";

    const bool asAlternate = m_saveAsAlternateCheck && m_saveAsAlternateCheck->GetValue();
    const char* itemIndent = asAlternate ? "\t\t" : "\t";
    const char* compIndent = asAlternate ? "\t\t" : "\t";
    const char* tileIndent = asAlternate ? "\t\t\t" : "\t\t";

    if (asAlternate) {
        xml << "\t<alternate>\n";
    }

    for (const auto& item : m_singleItems) {
        xml << itemIndent << "<item id=\"" << item.itemId << "\" chance=\"" << item.chance << "\" />\n";
    }

    for (const auto& comp : m_composites) {
        if (comp.tiles.empty()) continue;

        xml << compIndent << "<composite chance=\"" << comp.chance << "\">\n";
        // Entries sharing a position are one stacked tile: emit a single <tile> holding
        // every <item>, bottom first. UpdateCompositeFromGrid() keeps them adjacent.
        for (size_t i = 0; i < comp.tiles.size(); ) {
            const DoodadTileItem& first = comp.tiles[i];
            xml << tileIndent << "<tile x=\"" << first.x << "\" y=\"" << first.y << "\"";
            if (first.z != 0) {
                xml << " z=\"" << first.z << "\"";
            }
            xml << ">";

            size_t j = i;
            while (j < comp.tiles.size() && comp.tiles[j].x == first.x &&
                   comp.tiles[j].y == first.y && comp.tiles[j].z == first.z) {
                xml << " <item id=\"" << comp.tiles[j].itemId << "\" />";
                ++j;
            }

            xml << " </tile>\n";
            i = j;
        }
        xml << compIndent << "</composite>\n";
    }

    if (asAlternate) {
        xml << "\t</alternate>\n";
    }

    xml << "</brush>\n";

    return wxString(xml.str());
}

bool DoodadEditorDialog::ValidateDoodad() {
    if (m_nameCtrl->GetValue().IsEmpty()) {
        wxMessageBox("Please enter a brush name.", "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }

    if (m_singleItems.empty() && m_composites.empty()) {
        wxMessageBox("Please add at least one item or composite.", "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }

    for (const auto& comp : m_composites) {
        if (comp.tiles.empty()) {
            wxMessageBox("One or more composites have no tiles defined.", "Validation Error", wxOK | wxICON_ERROR);
            return false;
        }
    }

    return true;
}

// Strips the " (Doodad)" / " (Doodad - new)" suffix from a combobox label, returning
// the bare tileset name. Returns the input unchanged if no recognized suffix is present.
static wxString StripDoodadSuffix(const wxString& label) {
    wxString trimmed = label;
    trimmed.Trim(true).Trim(false);
    size_t parenIdx = trimmed.rfind(" (Doodad");
    if (parenIdx != wxString::npos) {
        return wxString(trimmed.Mid(0, parenIdx)).Trim(true).Trim(false);
    }
    return trimmed;
}

void DoodadEditorDialog::LoadExistingTilesets() {
    if (!m_tilesetCombo) return;
    m_tilesetCombo->Clear();

    ClientVersion* version = g_version.getLoadedVersion();
    if (!version) return;

    wxFileName tilesetsFile(version->getDataPath().GetFullPath(), "tilesets.xml");
    if (!tilesetsFile.FileExists()) return;

    pugi::xml_document doc;
    if (!doc.load_file(tilesetsFile.GetFullPath().ToStdString().c_str())) return;

    pugi::xml_node materials = doc.child("materials");
    if (!materials) return;

    // A tileset name can appear in multiple <tileset> blocks (terrain block,
    // doodad block, etc.). Aggregate them and tag each with whether any block
    // already has a <doodad> section.
    std::map<std::string, bool> nameHasDoodad;
    std::vector<std::string> orderedNames;
    for (pugi::xml_node tilesetNode = materials.child("tileset"); tilesetNode; tilesetNode = tilesetNode.next_sibling("tileset")) {
        pugi::xml_attribute nameAttr = tilesetNode.attribute("name");
        if (!nameAttr) continue;
        std::string name = nameAttr.as_string();
        bool hasDoodad = static_cast<bool>(tilesetNode.child("doodad"));
        auto it = nameHasDoodad.find(name);
        if (it == nameHasDoodad.end()) {
            nameHasDoodad[name] = hasDoodad;
            orderedNames.push_back(name);
        } else if (hasDoodad) {
            it->second = true;
        }
    }

    for (const std::string& name : orderedNames) {
        wxString label = wxString(name);
        label += nameHasDoodad[name] ? " (Doodad)" : " (Doodad - new)";
        m_tilesetCombo->Append(label);
    }

    if (m_tilesetBrushList) m_tilesetBrushList->Clear();
}

void DoodadEditorDialog::RefreshTilesetBrushList() {
    if (!m_tilesetBrushList) return;
    m_tilesetBrushList->Clear();

    wxString tilesetName = StripDoodadSuffix(m_tilesetCombo->GetValue());
    if (tilesetName.IsEmpty()) return;

    ClientVersion* version = g_version.getLoadedVersion();
    if (!version) return;

    wxFileName tilesetsFile(version->getDataPath().GetFullPath(), "tilesets.xml");
    if (!tilesetsFile.FileExists()) return;

    pugi::xml_document doc;
    if (!doc.load_file(tilesetsFile.GetFullPath().ToStdString().c_str())) return;
    pugi::xml_node materials = doc.child("materials");
    if (!materials) return;

    for (pugi::xml_node tilesetNode = materials.child("tileset"); tilesetNode; tilesetNode = tilesetNode.next_sibling("tileset")) {
        pugi::xml_attribute nameAttr = tilesetNode.attribute("name");
        if (!nameAttr || wxString(nameAttr.as_string()) != tilesetName) continue;
        pugi::xml_node doodad = tilesetNode.child("doodad");
        if (!doodad) continue;
        for (pugi::xml_node brushNode = doodad.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
            pugi::xml_attribute bn = brushNode.attribute("name");
            if (bn) m_tilesetBrushList->Append(wxString(bn.as_string()));
        }
    }
}

void DoodadEditorDialog::OnTilesetSelectionChanged(wxCommandEvent& WXUNUSED(event)) {
    RefreshTilesetBrushList();
}

void DoodadEditorDialog::OnAddToTileset(wxCommandEvent& WXUNUSED(event)) {
    wxString brushName = m_nameCtrl->GetValue().Trim(true).Trim(false);
    if (brushName.IsEmpty()) {
        wxMessageBox("Please enter a name for the doodad brush first.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    wxString tilesetName = StripDoodadSuffix(m_tilesetCombo->GetValue());
    if (tilesetName.IsEmpty()) {
        wxMessageBox("Please select or type a tileset name.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    int insertMode = m_tilesetInsertPosition ? m_tilesetInsertPosition->GetSelection() : 2;
    wxString afterBrushName;
    if (insertMode == 1) { // After selected
        int sel = m_tilesetBrushList ? m_tilesetBrushList->GetSelection() : wxNOT_FOUND;
        if (sel == wxNOT_FOUND) {
            wxMessageBox("'After selected' was chosen but no brush is selected in the list.\n"
                "Pick a reference brush, or switch to 'At start' / 'At end'.",
                "Error", wxOK | wxICON_ERROR);
            return;
        }
        afterBrushName = m_tilesetBrushList->GetString(sel);
    }

    ClientVersion* version = g_version.getLoadedVersion();
    if (!version) {
        wxMessageBox("No client version loaded.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    wxFileName tilesetsFile(version->getDataPath().GetFullPath(), "tilesets.xml");
    if (!tilesetsFile.FileExists()) {
        wxMessageBox("Cannot find tilesets.xml in the data directory.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    const std::string filePath = tilesetsFile.GetFullPath().ToStdString();

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filePath.c_str());
    if (!result) {
        wxMessageBox(wxString::Format("Failed to parse tilesets.xml:\n%s", result.description()),
            "Error", wxOK | wxICON_ERROR);
        return;
    }

    pugi::xml_node materials = doc.child("materials");
    if (!materials) {
        wxMessageBox("Invalid tilesets.xml: missing 'materials' node.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    // Prefer a tileset block that already has <doodad>; otherwise fall back to
    // the first block with the matching name (so we can append a <doodad> to it).
    pugi::xml_node targetTileset;
    pugi::xml_node fallbackTileset;
    for (pugi::xml_node tilesetNode = materials.child("tileset"); tilesetNode; tilesetNode = tilesetNode.next_sibling("tileset")) {
        pugi::xml_attribute nameAttr = tilesetNode.attribute("name");
        if (!nameAttr || wxString(nameAttr.as_string()) != tilesetName) continue;
        if (!fallbackTileset) fallbackTileset = tilesetNode;
        if (tilesetNode.child("doodad")) {
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

    // Find or create the <doodad> child (doodad brushes live under doodad category)
    pugi::xml_node doodad = targetTileset.child("doodad");
    if (!doodad) {
        doodad = targetTileset.append_child("doodad");
    }

    // Check for duplicates
    for (pugi::xml_node brushNode = doodad.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
        pugi::xml_attribute nameAttr = brushNode.attribute("name");
        if (nameAttr && wxString(nameAttr.as_string()) == brushName) {
            wxMessageBox("Brush '" + brushName + "' is already in tileset '" + tilesetName + "'.",
                "Already Exists", wxOK | wxICON_INFORMATION);
            return;
        }
    }

    // Insert at the chosen position.
    pugi::xml_node newBrush;
    if (insertMode == 0) { // At start
        newBrush = doodad.prepend_child("brush");
    } else if (insertMode == 1) { // After selected
        pugi::xml_node anchor;
        for (pugi::xml_node brushNode = doodad.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
            pugi::xml_attribute nameAttr = brushNode.attribute("name");
            if (nameAttr && wxString(nameAttr.as_string()) == afterBrushName) {
                anchor = brushNode;
                break;
            }
        }
        if (anchor) {
            newBrush = doodad.insert_child_after("brush", anchor);
        } else {
            newBrush = doodad.append_child("brush");
        }
    } else { // At end (default)
        newBrush = doodad.append_child("brush");
    }
    newBrush.append_attribute("name").set_value(brushName.ToStdString().c_str());

    if (!doc.save_file(filePath.c_str())) {
        wxMessageBox("Failed to save tilesets.xml.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    wxMessageBox("Brush '" + brushName + "' added to tileset '" + tilesetName + "'.\n"
        "Restart the editor (or reload the client) to see it in the palette.",
        "Success", wxOK | wxICON_INFORMATION);

    LoadExistingTilesets();
    m_tilesetCombo->SetValue(tilesetName + " (Doodad)");
    RefreshTilesetBrushList();
}

void DoodadEditorDialog::SaveDoodad() {
    if (m_currentCompositeIndex >= 0) {
        UpdateCompositeFromGrid();
    }

    if (!ValidateDoodad()) {
        return;
    }

    wxString xml = GenerateXML();

    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(xml));
        wxTheClipboard->Close();
        wxMessageBox("XML copied to clipboard!\n\nPaste it into your doodads.xml file.",
            "Success", wxOK | wxICON_INFORMATION);
    } else {
        wxMessageBox("Failed to copy to clipboard.\n\nXML:\n" + xml,
            "Error", wxOK | wxICON_ERROR);
    }
}

void DoodadEditorDialog::OnSave(wxCommandEvent& event) {
    SaveDoodad();
}

void DoodadEditorDialog::OnSaveToFile(wxCommandEvent& event) {
    if (m_currentCompositeIndex >= 0) {
        UpdateCompositeFromGrid();
    }

    if (!ValidateDoodad()) {
        return;
    }

    ClientVersion* version = g_version.getLoadedVersion();
    if (!version) {
        wxMessageBox("No client version loaded.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    wxFileName doodadsFile(version->getDataPath().GetFullPath(), "doodads.xml");
    const std::string filePath = doodadsFile.GetFullPath().ToStdString();

    if (!doodadsFile.FileExists()) {
        wxMessageBox(wxString::Format("doodads.xml not found at:\n%s", doodadsFile.GetFullPath()),
            "Error", wxOK | wxICON_ERROR);
        return;
    }

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filePath.c_str());
    if (!result) {
        wxMessageBox(wxString::Format("Failed to parse doodads.xml:\n%s", result.description()),
            "Error", wxOK | wxICON_ERROR);
        return;
    }

    pugi::xml_node root = doc.child("materials");
    if (!root) {
        wxMessageBox("Invalid doodads.xml: missing <materials> root.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    pugi::xml_document fragment;
    const std::string xmlStr = GenerateXML().ToStdString();
    if (!fragment.load(xmlStr.c_str())) {
        wxMessageBox("Failed to parse generated XML.", "Error", wxOK | wxICON_ERROR);
        return;
    }
    pugi::xml_node newBrush = fragment.child("brush");
    if (!newBrush) {
        wxMessageBox("Generated XML has no <brush> node.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    const std::string brushName = m_nameCtrl->GetValue().IsEmpty()
        ? std::string("new_doodad")
        : m_nameCtrl->GetValue().ToStdString();

    pugi::xml_node existing;
    for (pugi::xml_node b = root.child("brush"); b; b = b.next_sibling("brush")) {
        if (brushName == b.attribute("name").as_string()) {
            existing = b;
            break;
        }
    }

    if (existing) {
        // The editor only ever loads the first <alternate> block, so saving over a brush
        // that has several would silently drop the extra variations. Say so out loud.
        int alternateCount = 0;
        for (pugi::xml_node alt = existing.child("alternate"); alt; alt = alt.next_sibling("alternate")) {
            ++alternateCount;
        }

        wxString question = wxString::Format(
            "A brush named \"%s\" already exists in doodads.xml.\n\nReplace it?", brushName);
        if (alternateCount > 1) {
            question << wxString::Format(
                "\n\nWARNING: it has %d <alternate> variations and the editor only loaded the first one. "
                "Replacing it keeps just that one and discards the other %d.",
                alternateCount, alternateCount - 1);
        }

        int answer = wxMessageBox(question, "Brush exists", wxYES_NO | wxICON_QUESTION);
        if (answer != wxYES) {
            return;
        }
        root.insert_copy_before(newBrush, existing);
        root.remove_child(existing);
    } else {
        root.append_copy(newBrush);
    }

    if (!doc.save_file(filePath.c_str(), "\t")) {
        wxMessageBox("Failed to write doodads.xml.", "Error", wxOK | wxICON_ERROR);
        return;
    }

    wxMessageBox(wxString::Format("Brush \"%s\" saved to doodads.xml.\n\nReload the client version to see the changes in-editor.", brushName),
        "Success", wxOK | wxICON_INFORMATION);
}

// ============================================================================
// DoodadGridPanel Implementation
// ============================================================================

DoodadGridPanel::DoodadGridPanel(wxWindow* parent, wxWindowID id) :
    wxPanel(parent, id, wxDefaultPosition,
        wxSize(DOODAD_GRID_SIZE * DOODAD_CELL_SIZE + 1, DOODAD_GRID_SIZE * DOODAD_CELL_SIZE + 1),
        wxBORDER_SIMPLE),
    m_originX(-DOODAD_GRID_CENTER),
    m_originY(-DOODAD_GRID_CENTER),
    m_layer(0),
    m_selectedX(DOODAD_GRID_CENTER),
    m_selectedY(DOODAD_GRID_CENTER) {

    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(DOODAD_GRID_SIZE * DOODAD_CELL_SIZE + 1, DOODAD_GRID_SIZE * DOODAD_CELL_SIZE + 1));
}

DoodadGridPanel::~DoodadGridPanel() {
}

void DoodadGridPanel::SetItemAt(int gridX, int gridY, uint16_t itemId) {
    if (gridX < 0 || gridX >= DOODAD_GRID_SIZE || gridY < 0 || gridY >= DOODAD_GRID_SIZE) {
        return;
    }

    m_grid[gridX][gridY].clear();
    if (itemId > 0) {
        m_grid[gridX][gridY].push_back(itemId);
    }
}

void DoodadGridPanel::PushItemAt(int gridX, int gridY, uint16_t itemId) {
    if (gridX < 0 || gridX >= DOODAD_GRID_SIZE || gridY < 0 || gridY >= DOODAD_GRID_SIZE || itemId == 0) {
        return;
    }

    m_grid[gridX][gridY].push_back(itemId);
}

void DoodadGridPanel::ReplaceTopItemAt(int gridX, int gridY, uint16_t itemId) {
    if (gridX < 0 || gridX >= DOODAD_GRID_SIZE || gridY < 0 || gridY >= DOODAD_GRID_SIZE || itemId == 0) {
        return;
    }

    if (m_grid[gridX][gridY].empty()) {
        m_grid[gridX][gridY].push_back(itemId);
    } else {
        m_grid[gridX][gridY].back() = itemId;
    }
}

void DoodadGridPanel::PopItemAt(int gridX, int gridY) {
    if (gridX < 0 || gridX >= DOODAD_GRID_SIZE || gridY < 0 || gridY >= DOODAD_GRID_SIZE) {
        return;
    }

    if (!m_grid[gridX][gridY].empty()) {
        m_grid[gridX][gridY].pop_back();
    }
}

uint16_t DoodadGridPanel::GetItemAt(int gridX, int gridY) const {
    if (gridX >= 0 && gridX < DOODAD_GRID_SIZE && gridY >= 0 && gridY < DOODAD_GRID_SIZE &&
        !m_grid[gridX][gridY].empty()) {
        return m_grid[gridX][gridY].back();
    }
    return 0;
}

const std::vector<uint16_t>& DoodadGridPanel::GetStackAt(int gridX, int gridY) const {
    static const std::vector<uint16_t> empty;
    if (gridX >= 0 && gridX < DOODAD_GRID_SIZE && gridY >= 0 && gridY < DOODAD_GRID_SIZE) {
        return m_grid[gridX][gridY];
    }
    return empty;
}

void DoodadGridPanel::SetView(int originX, int originY, int layer) {
    // Clamped to the same range as the View spin controls, so GetOriginX() and the control
    // can never disagree and make the window jump on the next adjustment.
    m_originX = std::max(-DOODAD_MAX_ORIGIN, std::min(DOODAD_MAX_ORIGIN, originX));
    m_originY = std::max(-DOODAD_MAX_ORIGIN, std::min(DOODAD_MAX_ORIGIN, originY));
    m_layer = std::max(DOODAD_MIN_Z, std::min(DOODAD_MAX_Z, layer));
}

void DoodadGridPanel::Clear() {
    for (int x = 0; x < DOODAD_GRID_SIZE; x++) {
        for (int y = 0; y < DOODAD_GRID_SIZE; y++) {
            m_grid[x][y].clear();
        }
    }
    Refresh();
}

void DoodadGridPanel::SetSelectedCell(int gridX, int gridY) {
    if (gridX >= 0 && gridX < DOODAD_GRID_SIZE && gridY >= 0 && gridY < DOODAD_GRID_SIZE) {
        m_selectedX = gridX;
        m_selectedY = gridY;
        Refresh();
    }
}

void DoodadGridPanel::GetSelectedCell(int& gridX, int& gridY) const {
    gridX = m_selectedX;
    gridY = m_selectedY;
}

std::vector<DoodadTileItem> DoodadGridPanel::GetAllItems() const {
    std::vector<DoodadTileItem> items;
    for (int x = 0; x < DOODAD_GRID_SIZE; x++) {
        for (int y = 0; y < DOODAD_GRID_SIZE; y++) {
            // One entry per stack slot, bottom first — the caller merges them back into the
            // composite as a single stacked tile.
            for (uint16_t itemId : m_grid[x][y]) {
                items.push_back(DoodadTileItem(GridToRelativeX(x), GridToRelativeY(y), m_layer, itemId));
            }
        }
    }
    return items;
}

void DoodadGridPanel::SetItems(const std::vector<DoodadTileItem>& items) {
    Clear();
    for (const auto& item : items) {
        if (item.z != m_layer || item.itemId == 0) {
            continue;
        }
        int gridX = RelativeToGridX(item.x);
        int gridY = RelativeToGridY(item.y);
        if (gridX >= 0 && gridX < DOODAD_GRID_SIZE && gridY >= 0 && gridY < DOODAD_GRID_SIZE) {
            m_grid[gridX][gridY].push_back(item.itemId);
        }
    }
    Refresh();
}

void DoodadGridPanel::GetCellFromCoordinates(int px, int py, int& gridX, int& gridY) const {
    gridX = px / DOODAD_CELL_SIZE;
    gridY = py / DOODAD_CELL_SIZE;

    if (gridX < 0) gridX = 0;
    if (gridX >= DOODAD_GRID_SIZE) gridX = DOODAD_GRID_SIZE - 1;
    if (gridY < 0) gridY = 0;
    if (gridY >= DOODAD_GRID_SIZE) gridY = DOODAD_GRID_SIZE - 1;
}

// Info about a propagated cell from a multi-tile sprite
struct PropagationInfo {
    uint16_t srcItemId; // Source item that propagates into this cell
    int cx;             // Which tile-column of the sprite (0 = anchor column)
    int cy;             // Which tile-row of the sprite (0 = anchor row)
    uint8_t sprW;       // Total sprite width in tiles
    uint8_t sprH;       // Total sprite height in tiles
};

// Helper: draw a specific tile-portion of a multi-tile sprite into a cell
// cx,cy = which tile of the sprite (0,0 = bottom-right anchor)
// sprW,sprH = total sprite size in tiles
// The full-size bitmap is (max(sprW,sprH)*32) x (max(sprW,sprH)*32)
// Each tile's source region: x = (sprW-1-cx)*32, y = (sprH-1-cy)*32
static void DrawSpriteTile(wxDC& dc, GameSprite* gs, int destX, int destY, int destSize,
                           int cx, int cy) {
    wxBitmap bmp = SpriteIconGenerator::Generate(gs, SPRITE_SIZE_32x32, false);
    if (!bmp.IsOk()) return;

    int bmpW = bmp.GetWidth();
    int bmpH = bmp.GetHeight();

    // Source region for this tile: sprite tiles are laid out with
    // (width-1, height-1) at top-left of the bitmap and (0,0) at bottom-right
    int srcX = (gs->width - 1 - cx) * 32;
    int srcY = (gs->height - 1 - cy) * 32;

    // Clamp to bitmap bounds
    if (srcX < 0 || srcY < 0 || srcX >= bmpW || srcY >= bmpH) return;

    wxMemoryDC memDC(bmp);
    dc.StretchBlit(destX, destY, destSize, destSize, &memDC, srcX, srcY, 32, 32, wxCOPY, true);
}

void DoodadGridPanel::OnPaint(wxPaintEvent& event) {
    wxBufferedPaintDC dc(this);

    dc.SetBackground(wxBrush(wxColour(40, 40, 50)));
    dc.Clear();

    // Build propagation map for multi-tile sprites
    // propagation[x][y] stores info about which sprite piece occupies that cell
    PropagationInfo propagation[DOODAD_GRID_SIZE][DOODAD_GRID_SIZE] = {};

    for (int x = 0; x < DOODAD_GRID_SIZE; x++) {
        for (int y = 0; y < DOODAD_GRID_SIZE; y++) {
            uint16_t itemId = GetItemAt(x, y);
            if (itemId == 0) continue;

            auto itemDef = g_item_definitions.get(static_cast<ServerItemId>(itemId));
            if (itemDef.serverId() == 0) continue;

            GameSprite* gs = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(itemDef.clientId()));
            if (!gs || (gs->width <= 1 && gs->height <= 1)) continue;

            // Sprite extends to the left (-cx) and up (-cy) from the anchor
            for (int cx = 0; cx < gs->width; cx++) {
                for (int cy = 0; cy < gs->height; cy++) {
                    if (cx == 0 && cy == 0) continue; // Skip anchor
                    int gx = x - cx;
                    int gy = y - cy;
                    if (gx >= 0 && gx < DOODAD_GRID_SIZE && gy >= 0 && gy < DOODAD_GRID_SIZE) {
                        propagation[gx][gy] = { itemId, cx, cy, gs->width, gs->height };
                    }
                }
            }
        }
    }

    // Draw grid cells
    for (int x = 0; x < DOODAD_GRID_SIZE; x++) {
        for (int y = 0; y < DOODAD_GRID_SIZE; y++) {
            int px = x * DOODAD_CELL_SIZE;
            int py = y * DOODAD_CELL_SIZE;

            // (0,0) is the anchor tile of the composite. It only shows when the window is
            // panned over it, which is not always the case on big composites.
            const bool isAnchorCell = (GridToRelativeX(x) == 0 && GridToRelativeY(y) == 0);

            if (x == m_selectedX && y == m_selectedY) {
                dc.SetBrush(wxBrush(wxColour(80, 80, 120)));
            } else if (isAnchorCell) {
                dc.SetBrush(wxBrush(wxColour(50, 80, 50)));
            } else {
                dc.SetBrush(wxBrush(wxColour(50, 50, 60)));
            }

            dc.SetPen(wxPen(wxColour(80, 80, 90)));
            dc.DrawRectangle(px, py, DOODAD_CELL_SIZE, DOODAD_CELL_SIZE);

            if (isAnchorCell) {
                dc.SetPen(wxPen(wxColour(0, 200, 0), 2));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(px + 1, py + 1, DOODAD_CELL_SIZE - 2, DOODAD_CELL_SIZE - 2);
            }

            uint16_t itemId = GetItemAt(x, y);
            if (itemId > 0) {
                // This is a real item cell — draw the anchor tile (cx=0, cy=0) of the sprite
                auto itemDef = g_item_definitions.get(static_cast<ServerItemId>(itemId));
                if (itemDef.serverId() != 0) {
                    GameSprite* gs = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(itemDef.clientId()));
                    if (gs && (gs->width > 1 || gs->height > 1)) {
                        // Multi-tile: draw only the anchor portion (bottom-right piece)
                        DrawSpriteTile(dc, gs, px + 3, py + 3, 32, 0, 0);
                    } else if (gs) {
                        // Single-tile: draw normally
                        gs->DrawTo(&dc, SPRITE_SIZE_32x32, px + 3, py + 3, 32, 32);
                    } else {
                        dc.SetTextForeground(wxColour(200, 200, 200));
                        dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
                        dc.DrawText(wxString::Format("%d", itemId), px + 2, py + 2);
                    }
                } else {
                    dc.SetTextForeground(wxColour(200, 200, 200));
                    dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
                    dc.DrawText(wxString::Format("%d", itemId), px + 2, py + 2);
                }
            } else if (propagation[x][y].srcItemId > 0) {
                // Propagated cell: draw the corresponding sprite piece + red overlay
                const auto& prop = propagation[x][y];
                auto propDef = g_item_definitions.get(static_cast<ServerItemId>(prop.srcItemId));
                if (propDef.serverId() != 0) {
                    GameSprite* gs = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(propDef.clientId()));
                    if (gs) {
                        DrawSpriteTile(dc, gs, px + 3, py + 3, 32, prop.cx, prop.cy);
                    }
                }

                // Red border overlay to indicate propagation
                dc.SetPen(wxPen(wxColour(200, 50, 50), 2));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(px + 1, py + 1, DOODAD_CELL_SIZE - 2, DOODAD_CELL_SIZE - 2);
            }

            // Stacked cells only show their top sprite — the badge says how deep the stack is.
            const size_t stackSize = GetStackAt(x, y).size();
            if (stackSize > 1) {
                wxString badge = wxString::Format("x%zu", stackSize);
                dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
                wxSize badgeSize = dc.GetTextExtent(badge);
                int bx = px + DOODAD_CELL_SIZE - badgeSize.GetWidth() - 3;
                int by = py + DOODAD_CELL_SIZE - badgeSize.GetHeight() - 2;
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(wxBrush(wxColour(20, 20, 30)));
                dc.DrawRectangle(bx - 1, by - 1, badgeSize.GetWidth() + 2, badgeSize.GetHeight() + 2);
                dc.SetTextForeground(wxColour(255, 200, 0));
                dc.DrawText(badge, bx, by);
            }

            int relX = GridToRelativeX(x);
            int relY = GridToRelativeY(y);
            if (y == 0) {
                dc.SetTextForeground(wxColour(150, 150, 150));
                dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
                dc.DrawText(wxString::Format("%d", relX), px + DOODAD_CELL_SIZE / 2 - 4, py + 2);
            }
            if (x == 0) {
                dc.SetTextForeground(wxColour(150, 150, 150));
                dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
                dc.DrawText(wxString::Format("%d", relY), px + 2, py + DOODAD_CELL_SIZE / 2 - 4);
            }
        }
    }

    if (m_selectedX >= 0 && m_selectedY >= 0) {
        int px = m_selectedX * DOODAD_CELL_SIZE;
        int py = m_selectedY * DOODAD_CELL_SIZE;
        dc.SetPen(wxPen(wxColour(255, 200, 0), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(px, py, DOODAD_CELL_SIZE, DOODAD_CELL_SIZE);
    }
}

void DoodadGridPanel::OnMouseDown(wxMouseEvent& event) {
    int gridX, gridY;
    GetCellFromCoordinates(event.GetX(), event.GetY(), gridX, gridY);
    SetSelectedCell(gridX, gridY);

    wxWindow* parent = GetParent();
    while (parent && !dynamic_cast<DoodadEditorDialog*>(parent)) {
        parent = parent->GetParent();
    }

    DoodadEditorDialog* dialog = dynamic_cast<DoodadEditorDialog*>(parent);
    if (dialog) {
        uint16_t currentItem = GetItemAt(gridX, gridY);
        dialog->m_gridItemIdCtrl->SetValue(currentItem);
    }

    event.Skip();
}

void DoodadGridPanel::OnMouseRightUp(wxMouseEvent& event) {
    int gridX, gridY;
    GetCellFromCoordinates(event.GetX(), event.GetY(), gridX, gridY);
    SetSelectedCell(gridX, gridY);

    wxWindow* parent = GetParent();
    while (parent && !dynamic_cast<DoodadEditorDialog*>(parent)) {
        parent = parent->GetParent();
    }

    DoodadEditorDialog* dialog = dynamic_cast<DoodadEditorDialog*>(parent);
    if (dialog) {
        dialog->RemoveTopItemFromGridPosition(gridX, gridY);
        dialog->m_gridItemIdCtrl->SetValue(GetItemAt(gridX, gridY));
    }

    event.Skip();
}

void DoodadGridPanel::OnMouseClick(wxMouseEvent& event) {
    int gridX, gridY;
    GetCellFromCoordinates(event.GetX(), event.GetY(), gridX, gridY);

    uint16_t itemId = GetItemIDFromCurrentBrush();

    if (itemId > 0) {
        wxWindow* parent = GetParent();
        while (parent && !dynamic_cast<DoodadEditorDialog*>(parent)) {
            parent = parent->GetParent();
        }

        DoodadEditorDialog* dialog = dynamic_cast<DoodadEditorDialog*>(parent);
        if (dialog) {
            dialog->ApplyItemToGridPosition(gridX, gridY, itemId);
        }
    }

    event.Skip();
}

// ============================================================================
// DoodadPreviewPanel Implementation
// ============================================================================

DoodadPreviewPanel::DoodadPreviewPanel(wxWindow* parent, wxWindowID id) :
    wxPanel(parent, id, wxDefaultPosition, wxSize(200, 200), wxBORDER_SIMPLE) {

    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(200, 200));
}

DoodadPreviewPanel::~DoodadPreviewPanel() {
}

void DoodadPreviewPanel::SetItems(const std::vector<DoodadTileItem>& items) {
    m_items = items;
    Refresh();
}

void DoodadPreviewPanel::Clear() {
    m_items.clear();
    Refresh();
}

void DoodadPreviewPanel::OnPaint(wxPaintEvent& event) {
    wxBufferedPaintDC dc(this);

    wxSize size = GetClientSize();
    int centerX = size.GetWidth() / 2;
    int centerY = size.GetHeight() / 2;

    dc.SetBackground(wxBrush(wxColour(30, 30, 40)));
    dc.Clear();

    // Shrink the cells until the whole composite fits — imported composites are routinely
    // much larger than the ±3 tiles this panel used to assume.
    int extent = 3;
    for (const auto& item : m_items) {
        extent = std::max(extent, std::max(std::max(item.x, -item.x), std::max(item.y, -item.y)));
    }

    // Below ~6px a cell shows nothing useful anyway, so that is the floor. Past it the
    // preview stops shrinking and shows the middle of the composite instead.
    const int available = std::max(1, std::min(size.GetWidth(), size.GetHeight()));
    const int cellSize = std::max(6, std::min(32, available / (2 * extent + 1)));

    // Never paint more cells than fit on screen: a composite imported from a large map
    // selection would otherwise ask for tens of thousands of rectangles on every repaint.
    const int drawExtent = std::min(extent, std::max(1, available / (2 * cellSize)));

    dc.SetPen(wxPen(wxColour(50, 50, 60)));
    for (int x = -drawExtent; x <= drawExtent; x++) {
        for (int y = -drawExtent; y <= drawExtent; y++) {
            int px = centerX + x * cellSize - cellSize / 2;
            int py = centerY + y * cellSize - cellSize / 2;
            dc.DrawRectangle(px, py, cellSize, cellSize);
        }
    }

    dc.SetPen(wxPen(wxColour(0, 200, 0), 2));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(centerX - cellSize / 2, centerY - cellSize / 2, cellSize, cellSize);

    // Build propagation map: (relX, relY) -> PropagationInfo
    std::map<std::pair<int,int>, PropagationInfo> propagation;

    for (const auto& item : m_items) {
        auto itemDef = g_item_definitions.get(static_cast<ServerItemId>(item.itemId));
        if (itemDef.serverId() == 0) continue;

        GameSprite* gs = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(itemDef.clientId()));
        if (!gs || (gs->width <= 1 && gs->height <= 1)) continue;

        for (int cx = 0; cx < gs->width; cx++) {
            for (int cy = 0; cy < gs->height; cy++) {
                if (cx == 0 && cy == 0) continue;
                int propX = item.x - cx;
                int propY = item.y - cy;
                propagation[{propX, propY}] = { item.itemId, cx, cy, gs->width, gs->height };
            }
        }
    }

    // Draw real items (anchor tiles). Lower floors (higher z) go down first so the floors
    // above them end up on top, and stacked items keep their bottom-to-top order.
    std::vector<DoodadTileItem> ordered = m_items;
    std::stable_sort(ordered.begin(), ordered.end(),
        [](const DoodadTileItem& a, const DoodadTileItem& b) { return a.z > b.z; });

    for (const auto& item : ordered) {
        int px = centerX + item.x * cellSize - cellSize / 2;
        int py = centerY + item.y * cellSize - cellSize / 2;

        // Sprite generation is expensive — skip anything scrolled off the panel.
        if (px + cellSize < 0 || py + cellSize < 0 || px > size.GetWidth() || py > size.GetHeight()) {
            continue;
        }

        auto itemDef = g_item_definitions.get(static_cast<ServerItemId>(item.itemId));
        if (itemDef.serverId() != 0) {
            GameSprite* gs = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(itemDef.clientId()));
            if (gs && (gs->width > 1 || gs->height > 1)) {
                DrawSpriteTile(dc, gs, px, py, cellSize, 0, 0);
            } else if (gs) {
                gs->DrawTo(&dc, SPRITE_SIZE_32x32, px, py, cellSize, cellSize);
            } else {
                dc.SetBrush(wxBrush(wxColour(100, 100, 150)));
                dc.SetPen(wxPen(wxColour(150, 150, 200)));
                dc.DrawRectangle(px + 2, py + 2, cellSize - 4, cellSize - 4);

                dc.SetTextForeground(wxColour(255, 255, 255));
                dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
                dc.DrawText(wxString::Format("%d", item.itemId), px + 4, py + 4);
            }
        } else {
            dc.SetBrush(wxBrush(wxColour(100, 100, 150)));
            dc.SetPen(wxPen(wxColour(150, 150, 200)));
            dc.DrawRectangle(px + 2, py + 2, cellSize - 4, cellSize - 4);

            dc.SetTextForeground(wxColour(255, 255, 255));
            dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
            dc.DrawText(wxString::Format("%d", item.itemId), px + 4, py + 4);
        }
    }

    // Positions taken by a real item, gathered once — scanning m_items per propagated cell
    // is quadratic and imported composites make that bite.
    std::set<std::pair<int, int>> occupied;
    for (const auto& item : m_items) {
        occupied.insert({ item.x, item.y });
    }

    // Draw propagation cells (sprite pieces from multi-tile neighbors)
    for (const auto& [pos, prop] : propagation) {
        if (occupied.count(pos) > 0) continue;

        int px = centerX + pos.first * cellSize - cellSize / 2;
        int py = centerY + pos.second * cellSize - cellSize / 2;
        if (px + cellSize < 0 || py + cellSize < 0 || px > size.GetWidth() || py > size.GetHeight()) {
            continue;
        }

        auto propDef = g_item_definitions.get(static_cast<ServerItemId>(prop.srcItemId));
        if (propDef.serverId() != 0) {
            GameSprite* gs = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(propDef.clientId()));
            if (gs) {
                DrawSpriteTile(dc, gs, px, py, cellSize, prop.cx, prop.cy);
            }
        }

        // Red border overlay
        dc.SetPen(wxPen(wxColour(200, 50, 50), 2));
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(px + 1, py + 1, cellSize - 2, cellSize - 2);
    }

    dc.SetTextForeground(wxColour(100, 100, 100));
    dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    dc.DrawText("(0,0)", centerX - 12, centerY + cellSize / 2 + 2);
}

// ============================================================================
// DoodadSingleItemsPanel Implementation
// ============================================================================

BEGIN_EVENT_TABLE(DoodadSingleItemsPanel, wxPanel)
    EVT_PAINT(DoodadSingleItemsPanel::OnPaint)
    EVT_LEFT_UP(DoodadSingleItemsPanel::OnMouseClick)
    EVT_SIZE(DoodadSingleItemsPanel::OnSize)
END_EVENT_TABLE()

DoodadSingleItemsPanel::DoodadSingleItemsPanel(wxWindow* parent, wxWindowID id) :
    wxPanel(parent, id, wxDefaultPosition, wxSize(-1, 3 * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN), wxBORDER_NONE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, 3 * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN));
}

void DoodadSingleItemsPanel::SetItems(const std::vector<DoodadSingleItem>& items) {
    m_items = items;
    if (m_selectedIndex >= static_cast<int>(m_items.size())) {
        m_selectedIndex = -1;
    }
    RecalcLayout();
    Refresh();
}

void DoodadSingleItemsPanel::Clear() {
    m_items.clear();
    m_cells.clear();
    m_selectedIndex = -1;
    Refresh();
}

void DoodadSingleItemsPanel::SetSelectedIndex(int idx) {
    if (idx < -1 || idx >= static_cast<int>(m_items.size())) return;
    m_selectedIndex = idx;
    Refresh();
}

void DoodadSingleItemsPanel::AddItemFromDrop(uint16_t itemId) {
    wxWindow* parent = GetParent();
    while (parent && !dynamic_cast<DoodadEditorDialog*>(parent)) {
        parent = parent->GetParent();
    }
    DoodadEditorDialog* dialog = dynamic_cast<DoodadEditorDialog*>(parent);
    if (dialog) {
        dialog->AddSingleItemById(itemId);
    }
}

void DoodadSingleItemsPanel::RecalcLayout() {
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

void DoodadSingleItemsPanel::OnSize(wxSizeEvent& event) {
    RecalcLayout();
    Refresh();
    event.Skip();
}

void DoodadSingleItemsPanel::OnPaint(wxPaintEvent& event) {
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

    int totalChance = 0;
    for (const auto& item : m_items) totalChance += item.chance;
    if (totalChance <= 0) totalChance = 1;

    for (const auto& cell : m_cells) {
        const auto& item = m_items[cell.index];

        bool selected = (cell.index == m_selectedIndex);
        if (selected) {
            dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
            dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Selected)));
        } else {
            dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
            dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
        }
        dc.DrawRoundedRectangle(cell.bounds, 3);

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

        dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
        int pct = static_cast<int>((static_cast<double>(item.chance) / totalChance) * 100.0 + 0.5);
        wxString chanceLabel = wxString::Format("%d (%d%%)", item.chance, pct);
        wxSize ts = dc.GetTextExtent(chanceLabel);
        dc.DrawText(chanceLabel,
            cell.bounds.x + (CELL_SIZE - ts.GetWidth()) / 2,
            cell.bounds.y + CELL_SIZE - ts.GetHeight() - 2);

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

void DoodadSingleItemsPanel::OnMouseClick(wxMouseEvent& event) {
    int mx = event.GetX();
    int my = event.GetY();

    wxWindow* parent = GetParent();
    while (parent && !dynamic_cast<DoodadEditorDialog*>(parent)) {
        parent = parent->GetParent();
    }
    DoodadEditorDialog* dialog = dynamic_cast<DoodadEditorDialog*>(parent);

    for (const auto& cell : m_cells) {
        if (cell.closeBtn.Contains(mx, my)) {
            if (dialog) {
                dialog->RemoveSingleItemAt(cell.index);
            }
            return;
        }

        if (cell.bounds.Contains(mx, my)) {
            m_selectedIndex = cell.index;
            if (dialog) {
                dialog->SelectSingleItemAt(cell.index);
            }
            Refresh();
            return;
        }
    }

    m_selectedIndex = -1;
    Refresh();
}
