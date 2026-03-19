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
#include "brushes/brush.h"
#include "brushes/doodad/doodad_brush.h"
#include "brushes/raw/raw_brush.h"
#include "item_definitions/core/item_definition_store.h"
#include "rendering/core/game_sprite.h"
#include "rendering/utilities/sprite_icon_generator.h"
#include <wx/sizer.h>
#include <wx/gbsizer.h>
#include <wx/statline.h>
#include <wx/dcbuffer.h>
#include <wx/dnd.h>
#include <wx/clipbrd.h>
#include <map>
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
    ID_COMPOSITE_CHANCE,
    ID_GRID_ITEM_ID,
    ID_BROWSE_GRID_ITEM,
    ID_LOAD_TIMER,
    ID_FILTER_TEXT,
    ID_DOODAD_LIST,
    ID_PREV_PAGE,
    ID_NEXT_PAGE,
    ID_CREATE_NEW
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
        if (!m_grid || !data.StartsWith("ITEM_ID:")) {
            return false;
        }

        wxString idStr = data.Mid(8);
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

// Event tables
BEGIN_EVENT_TABLE(DoodadEditorDialog, wxDialog)
    EVT_BUTTON(ID_ADD_SINGLE_ITEM, DoodadEditorDialog::OnAddSingleItem)
    EVT_BUTTON(ID_REMOVE_SINGLE_ITEM, DoodadEditorDialog::OnRemoveSingleItem)
    EVT_BUTTON(ID_BROWSE_SINGLE_ITEM, DoodadEditorDialog::OnBrowseSingleItem)
    EVT_BUTTON(ID_NEW_COMPOSITE, DoodadEditorDialog::OnNewComposite)
    EVT_BUTTON(ID_REMOVE_COMPOSITE, DoodadEditorDialog::OnRemoveComposite)
    EVT_BUTTON(ID_CLEAR_GRID, DoodadEditorDialog::OnClearGrid)
    EVT_BUTTON(ID_BROWSE_GRID_ITEM, DoodadEditorDialog::OnBrowseGridItem)
    EVT_BUTTON(wxID_SAVE, DoodadEditorDialog::OnSave)
    EVT_BUTTON(wxID_CLOSE, DoodadEditorDialog::OnClose)
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
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(DoodadGridPanel, wxPanel)
    EVT_PAINT(DoodadGridPanel::OnPaint)
    EVT_LEFT_UP(DoodadGridPanel::OnMouseClick)
    EVT_LEFT_DOWN(DoodadGridPanel::OnMouseDown)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(DoodadPreviewPanel, wxPanel)
    EVT_PAINT(DoodadPreviewPanel::OnPaint)
END_EVENT_TABLE()

// ============================================================================
// DoodadEditorDialog Implementation
// ============================================================================

DoodadEditorDialog::DoodadEditorDialog(wxWindow* parent, const wxString& title) :
    wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(1200, 800),
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX),
    m_currentCompositeIndex(-1),
    m_activeTab(0),
    m_loadTimer(nullptr),
    m_isLoading(true),
    m_currentPage(0),
    m_totalPages(0) {

    CreateGUIControls();
    CenterOnParent();

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
    row2Sizer->Add(m_oneSizeCheck, 0);

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

    m_singleItemsList = new wxListBox(m_singlePanel, ID_SINGLE_ITEM_LIST, wxDefaultPosition, wxSize(-1, 150), 0, nullptr, wxLB_SINGLE);
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

    compositeSizer->Add(compositeListSizer, 0, wxEXPAND | wxALL, 5);

    // Center: Grid Editor
    wxStaticBoxSizer* gridSizer = new wxStaticBoxSizer(wxVERTICAL, m_compositePanel, "Composite Grid (10x10)");

    m_gridPanel = new DoodadGridPanel(m_compositePanel);
    m_gridPanel->SetDropTarget(new DoodadGridDropTarget(m_gridPanel, this));
    gridSizer->Add(m_gridPanel, 0, wxALL | wxALIGN_CENTER, 5);

    wxStaticText* instructions = new wxStaticText(m_compositePanel, wxID_ANY,
        "Click cell to select, then use current brush or enter Item ID.\nCenter (green) = position (0,0)");
    instructions->SetForegroundColour(wxColour(100, 100, 200));
    gridSizer->Add(instructions, 0, wxALL, 5);

    wxBoxSizer* gridCtrlSizer = new wxBoxSizer(wxHORIZONTAL);
    gridCtrlSizer->Add(new wxStaticText(m_compositePanel, wxID_ANY, "Item ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_gridItemIdCtrl = new wxSpinCtrl(m_compositePanel, ID_GRID_ITEM_ID, "0", wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 0, 65535);
    gridCtrlSizer->Add(m_gridItemIdCtrl, 0, wxRIGHT, 5);
    gridCtrlSizer->Add(new wxButton(m_compositePanel, ID_BROWSE_GRID_ITEM, "Browse...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 5);
    gridCtrlSizer->Add(new wxButton(m_compositePanel, ID_CLEAR_GRID, "Clear Grid", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
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

    // === Bottom buttons ===
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->AddStretchSpacer();
    buttonSizer->Add(new wxButton(rightPanel, wxID_SAVE, "Save to Clipboard"), 0, wxRIGHT, 5);
    buttonSizer->Add(new wxButton(rightPanel, wxID_CLOSE, "Close"), 0);
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
        UpdateGridFromComposite();
    }

    UpdatePreview();
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
    m_thicknessCtrl->SetValue(25);
    m_thicknessCeilingCtrl->SetValue(100);

    m_singleItems.clear();
    m_composites.clear();
    m_currentCompositeIndex = -1;

    UpdateSingleItemsList();
    UpdateCompositesList();
    m_gridPanel->Clear();
    m_previewPanel->Clear();
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

void DoodadEditorDialog::OnRemoveSingleItem(wxCommandEvent& event) {
    int sel = m_singleItemsList->GetSelection();
    if (sel != wxNOT_FOUND && sel < (int)m_singleItems.size()) {
        m_singleItems.erase(m_singleItems.begin() + sel);
        UpdateSingleItemsList();
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

    UpdateCompositesList();
    m_compositesList->SetSelection(m_composites.size() - 1);
    m_currentCompositeIndex = m_composites.size() - 1;
    m_compositeChanceCtrl->SetValue(comp.chance);

    m_gridPanel->Clear();
    UpdatePreview();
}

void DoodadEditorDialog::OnRemoveComposite(wxCommandEvent& event) {
    int sel = m_compositesList->GetSelection();
    if (sel != wxNOT_FOUND && sel < (int)m_composites.size()) {
        m_composites.erase(m_composites.begin() + sel);
        m_currentCompositeIndex = -1;

        UpdateCompositesList();
        m_gridPanel->Clear();
        m_previewPanel->Clear();

        if (!m_composites.empty()) {
            m_compositesList->SetSelection(0);
            m_currentCompositeIndex = 0;
            UpdateGridFromComposite();
        }
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
    m_gridPanel->Clear();
    if (m_currentCompositeIndex >= 0 && m_currentCompositeIndex < (int)m_composites.size()) {
        m_composites[m_currentCompositeIndex].tiles.clear();
    }
    UpdatePreview();
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
                ApplyItemToGridPosition(gridX, gridY, itemId);
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
            ApplyItemToGridPosition(gridX, gridY, itemId);
        }
    }
}

void DoodadEditorDialog::ApplyItemToGridPosition(int gridX, int gridY, uint16_t itemId) {
    if (m_currentCompositeIndex < 0) {
        wxCommandEvent evt;
        OnNewComposite(evt);
    }

    m_gridPanel->SetItemAt(gridX, gridY, itemId);
    m_gridPanel->Refresh();
    UpdateCompositeFromGrid();
    UpdatePreview();
}

void DoodadEditorDialog::UpdateCompositeFromGrid() {
    if (m_currentCompositeIndex < 0 || m_currentCompositeIndex >= (int)m_composites.size()) {
        return;
    }

    m_composites[m_currentCompositeIndex].tiles = m_gridPanel->GetAllItems();
}

void DoodadEditorDialog::UpdateGridFromComposite() {
    m_gridPanel->Clear();

    if (m_currentCompositeIndex < 0 || m_currentCompositeIndex >= (int)m_composites.size()) {
        return;
    }

    m_gridPanel->SetItems(m_composites[m_currentCompositeIndex].tiles);
    m_gridPanel->Refresh();
    m_previewPanel->SetItems(m_composites[m_currentCompositeIndex].tiles);
}

uint16_t DoodadEditorDialog::GetCurrentItemId() const {
    return m_gridItemIdCtrl->GetValue();
}

void DoodadEditorDialog::UpdateSingleItemsList() {
    m_singleItemsList->Clear();
    for (const auto& item : m_singleItems) {
        wxString label = wxString::Format("ID: %d (Chance: %d)", item.itemId, item.chance);
        m_singleItemsList->Append(label);
    }
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

void DoodadEditorDialog::UpdatePreview() {
    if (m_currentCompositeIndex >= 0 && m_currentCompositeIndex < (int)m_composites.size()) {
        m_previewPanel->SetItems(m_composites[m_currentCompositeIndex].tiles);
    } else {
        m_previewPanel->Clear();
    }
    m_previewPanel->Refresh();
}

void DoodadEditorDialog::OnPageChanged(wxBookCtrlEvent& event) {
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

    for (const auto& item : m_singleItems) {
        xml << "\t<item id=\"" << item.itemId << "\" chance=\"" << item.chance << "\" />\n";
    }

    for (const auto& comp : m_composites) {
        if (comp.tiles.empty()) continue;

        xml << "\t<composite chance=\"" << comp.chance << "\">\n";
        for (const auto& tile : comp.tiles) {
            xml << "\t\t<tile x=\"" << tile.x << "\" y=\"" << tile.y << "\"";
            if (tile.z != 0) {
                xml << " z=\"" << tile.z << "\"";
            }
            xml << "> <item id=\"" << tile.itemId << "\" /> </tile>\n";
        }
        xml << "\t</composite>\n";
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

void DoodadEditorDialog::OnClose(wxCommandEvent& event) {
    EndModal(wxID_CLOSE);
}

// ============================================================================
// DoodadGridPanel Implementation
// ============================================================================

DoodadGridPanel::DoodadGridPanel(wxWindow* parent, wxWindowID id) :
    wxPanel(parent, id, wxDefaultPosition,
        wxSize(DOODAD_GRID_SIZE * DOODAD_CELL_SIZE + 1, DOODAD_GRID_SIZE * DOODAD_CELL_SIZE + 1),
        wxBORDER_SIMPLE),
    m_selectedX(DOODAD_GRID_CENTER),
    m_selectedY(DOODAD_GRID_CENTER) {

    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(DOODAD_GRID_SIZE * DOODAD_CELL_SIZE + 1, DOODAD_GRID_SIZE * DOODAD_CELL_SIZE + 1));

    for (int x = 0; x < DOODAD_GRID_SIZE; x++) {
        for (int y = 0; y < DOODAD_GRID_SIZE; y++) {
            m_grid[x][y] = 0;
        }
    }
}

DoodadGridPanel::~DoodadGridPanel() {
}

void DoodadGridPanel::SetItemAt(int gridX, int gridY, uint16_t itemId) {
    if (gridX >= 0 && gridX < DOODAD_GRID_SIZE && gridY >= 0 && gridY < DOODAD_GRID_SIZE) {
        m_grid[gridX][gridY] = itemId;
    }
}

uint16_t DoodadGridPanel::GetItemAt(int gridX, int gridY) const {
    if (gridX >= 0 && gridX < DOODAD_GRID_SIZE && gridY >= 0 && gridY < DOODAD_GRID_SIZE) {
        return m_grid[gridX][gridY];
    }
    return 0;
}

void DoodadGridPanel::Clear() {
    for (int x = 0; x < DOODAD_GRID_SIZE; x++) {
        for (int y = 0; y < DOODAD_GRID_SIZE; y++) {
            m_grid[x][y] = 0;
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
            if (m_grid[x][y] > 0) {
                DoodadTileItem item;
                item.x = GridToRelative(x);
                item.y = GridToRelative(y);
                item.z = 0;
                item.itemId = m_grid[x][y];
                items.push_back(item);
            }
        }
    }
    return items;
}

void DoodadGridPanel::SetItems(const std::vector<DoodadTileItem>& items) {
    Clear();
    for (const auto& item : items) {
        int gridX = RelativeToGrid(item.x);
        int gridY = RelativeToGrid(item.y);
        if (gridX >= 0 && gridX < DOODAD_GRID_SIZE && gridY >= 0 && gridY < DOODAD_GRID_SIZE) {
            m_grid[gridX][gridY] = item.itemId;
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
            uint16_t itemId = m_grid[x][y];
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

            if (x == m_selectedX && y == m_selectedY) {
                dc.SetBrush(wxBrush(wxColour(80, 80, 120)));
            } else if (x == DOODAD_GRID_CENTER && y == DOODAD_GRID_CENTER) {
                dc.SetBrush(wxBrush(wxColour(50, 80, 50)));
            } else {
                dc.SetBrush(wxBrush(wxColour(50, 50, 60)));
            }

            dc.SetPen(wxPen(wxColour(80, 80, 90)));
            dc.DrawRectangle(px, py, DOODAD_CELL_SIZE, DOODAD_CELL_SIZE);

            if (x == DOODAD_GRID_CENTER && y == DOODAD_GRID_CENTER) {
                dc.SetPen(wxPen(wxColour(0, 200, 0), 2));
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(px + 1, py + 1, DOODAD_CELL_SIZE - 2, DOODAD_CELL_SIZE - 2);
            }

            uint16_t itemId = m_grid[x][y];
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

            int relX = GridToRelative(x);
            int relY = GridToRelative(y);
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
    int cellSize = 32;

    dc.SetBackground(wxBrush(wxColour(30, 30, 40)));
    dc.Clear();

    dc.SetPen(wxPen(wxColour(50, 50, 60)));
    for (int x = -3; x <= 3; x++) {
        for (int y = -3; y <= 3; y++) {
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

    // Draw real items (anchor tiles)
    for (const auto& item : m_items) {
        int px = centerX + item.x * cellSize - cellSize / 2;
        int py = centerY + item.y * cellSize - cellSize / 2;

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

    // Draw propagation cells (sprite pieces from multi-tile neighbors)
    for (const auto& [pos, prop] : propagation) {
        // Skip if a real item already occupies this position
        bool hasRealItem = false;
        for (const auto& item : m_items) {
            if (item.x == pos.first && item.y == pos.second) {
                hasRealItem = true;
                break;
            }
        }
        if (hasRealItem) continue;

        int px = centerX + pos.first * cellSize - cellSize / 2;
        int py = centerY + pos.second * cellSize - cellSize / 2;

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
