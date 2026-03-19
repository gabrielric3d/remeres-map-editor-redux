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

#ifndef RME_UI_DIALOGS_DOODAD_EDITOR_DIALOG_H_
#define RME_UI_DIALOGS_DOODAD_EDITOR_DIALOG_H_

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/panel.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/choice.h>
#include <wx/notebook.h>
#include <wx/splitter.h>
#include <vector>
#include <map>

class DoodadGridPanel;
class DoodadPreviewPanel;
class DoodadListPanel;

// Grid size constants
const int DOODAD_GRID_SIZE = 10;      // 10x10 grid
const int DOODAD_GRID_CENTER = 5;     // Center position (0,0) is at index 5
const int DOODAD_CELL_SIZE = 38;      // Pixel size of each cell

// Pagination constants
const int DOODADS_PER_PAGE = 50;

// Represents a tile in the composite doodad
struct DoodadTileItem {
    int x;           // Relative X offset from center (-5 to +4)
    int y;           // Relative Y offset from center (-5 to +4)
    int z;           // Z offset (usually 0)
    uint16_t itemId;

    DoodadTileItem() : x(0), y(0), z(0), itemId(0) {}
    DoodadTileItem(int px, int py, int pz, uint16_t id) : x(px), y(py), z(pz), itemId(id) {}

    bool operator==(const DoodadTileItem& other) const {
        return x == other.x && y == other.y && z == other.z && itemId == other.itemId;
    }
};

// Represents a complete composite configuration
struct DoodadComposite {
    int chance;
    std::vector<DoodadTileItem> tiles;

    DoodadComposite() : chance(10) {}
};

// Represents a single item (non-composite)
struct DoodadSingleItem {
    uint16_t itemId;
    int chance;

    DoodadSingleItem() : itemId(0), chance(10) {}
    DoodadSingleItem(uint16_t id, int c) : itemId(id), chance(c) {}
};

// Info about a doodad brush for the list
struct DoodadBrushInfo {
    wxString name;
    int compositeCount;
    int singleCount;

    DoodadBrushInfo() : compositeCount(0), singleCount(0) {}
    DoodadBrushInfo(const wxString& n, int cc, int sc) : name(n), compositeCount(cc), singleCount(sc) {}
};

// Main dialog for doodad brush configuration
class DoodadEditorDialog : public wxDialog {
public:
    DoodadEditorDialog(wxWindow* parent, const wxString& title);
    virtual ~DoodadEditorDialog();

    // Event handlers
    void OnAddSingleItem(wxCommandEvent& event);
    void OnRemoveSingleItem(wxCommandEvent& event);
    void OnBrowseSingleItem(wxCommandEvent& event);
    void OnNewComposite(wxCommandEvent& event);
    void OnRemoveComposite(wxCommandEvent& event);
    void OnCompositeSelected(wxCommandEvent& event);
    void OnCompositeChanceChanged(wxSpinEvent& event);
    void OnClearGrid(wxCommandEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnClose(wxCommandEvent& event);
    void OnBrowseGridItem(wxCommandEvent& event);
    void OnGridItemIdChanged(wxSpinEvent& event);
    void OnPageChanged(wxBookCtrlEvent& event);
    void OnLoadTimer(wxTimerEvent& event);
    void OnFilterChanged(wxCommandEvent& event);
    void OnDoodadListSelected(wxListEvent& event);
    void OnPrevPage(wxCommandEvent& event);
    void OnNextPage(wxCommandEvent& event);
    void OnCreateNew(wxCommandEvent& event);

    // Public methods for grid panel to access
    void ApplyItemToGridPosition(int gridX, int gridY, uint16_t itemId);
    void UpdateCompositeFromGrid();
    void UpdateGridFromComposite();
    uint16_t GetCurrentItemId() const;
    void LoadDoodadBrush(const wxString& brushName);

protected:
    void CreateGUIControls();
    void LoadExistingDoodads();
    void UpdateDoodadList();
    void SaveDoodad();
    bool ValidateDoodad();
    void UpdatePreview();
    void UpdateSingleItemsList();
    void UpdateCompositesList();
    void ClearAll();
    void ClearEditor();
    wxString GenerateXML();

public:
    // UI Elements - public for access from other components
    // Left panel - Doodad list
    wxTextCtrl* m_filterCtrl;
    wxListCtrl* m_doodadListCtrl;
    wxStaticText* m_pageLabel;
    wxButton* m_prevPageBtn;
    wxButton* m_nextPageBtn;

    // Right panel - Editor
    wxTextCtrl* m_nameCtrl;
    wxSpinCtrl* m_lookIdCtrl;
    wxNotebook* m_notebook;

    // Properties
    wxCheckBox* m_draggableCheck;
    wxCheckBox* m_onBlockingCheck;
    wxCheckBox* m_onDuplicateCheck;
    wxCheckBox* m_redoBordersCheck;
    wxCheckBox* m_oneSizeCheck;
    wxSpinCtrl* m_thicknessCtrl;
    wxSpinCtrl* m_thicknessCeilingCtrl;

    // Single Items Tab
    wxPanel* m_singlePanel;
    wxListBox* m_singleItemsList;
    wxSpinCtrl* m_singleItemIdCtrl;
    wxSpinCtrl* m_singleItemChanceCtrl;

    // Composites Tab
    wxPanel* m_compositePanel;
    wxListBox* m_compositesList;
    wxSpinCtrl* m_compositeChanceCtrl;
    wxSpinCtrl* m_gridItemIdCtrl;

    // Grid panel for composite editing
    DoodadGridPanel* m_gridPanel;

    // Preview panel
    DoodadPreviewPanel* m_previewPanel;

    // Data
    std::vector<DoodadSingleItem> m_singleItems;
    std::vector<DoodadComposite> m_composites;
    int m_currentCompositeIndex;

    // Doodad list data
    std::vector<DoodadBrushInfo> m_allDoodads;
    std::vector<DoodadBrushInfo> m_filteredDoodads;
    int m_currentPage;
    int m_totalPages;

private:
    int m_activeTab;
    wxTimer* m_loadTimer;
    bool m_isLoading;

    DECLARE_EVENT_TABLE()
};

// Custom panel for the 10x10 grid editor
class DoodadGridPanel : public wxPanel {
public:
    DoodadGridPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
    virtual ~DoodadGridPanel();

    void SetItemAt(int gridX, int gridY, uint16_t itemId);
    uint16_t GetItemAt(int gridX, int gridY) const;
    void Clear();

    void SetSelectedCell(int gridX, int gridY);
    void GetSelectedCell(int& gridX, int& gridY) const;

    // Get all items in the grid
    std::vector<DoodadTileItem> GetAllItems() const;
    void SetItems(const std::vector<DoodadTileItem>& items);

    // Convert grid coordinates (0-9) to relative coordinates (-5 to +4)
    static int GridToRelative(int gridCoord) { return gridCoord - DOODAD_GRID_CENTER; }
    static int RelativeToGrid(int relCoord) { return relCoord + DOODAD_GRID_CENTER; }

    void OnPaint(wxPaintEvent& event);
    void OnMouseClick(wxMouseEvent& event);
    void OnMouseDown(wxMouseEvent& event);

    // Public for drop target
    void GetCellFromCoordinates(int px, int py, int& gridX, int& gridY) const;

private:
    // Grid data: [gridX][gridY] -> itemId (0 = empty)
    uint16_t m_grid[DOODAD_GRID_SIZE][DOODAD_GRID_SIZE];
    int m_selectedX;
    int m_selectedY;

    DECLARE_EVENT_TABLE()
};

// Preview panel showing the composite layout
class DoodadPreviewPanel : public wxPanel {
public:
    DoodadPreviewPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
    virtual ~DoodadPreviewPanel();

    void SetItems(const std::vector<DoodadTileItem>& items);
    void Clear();

    void OnPaint(wxPaintEvent& event);

private:
    std::vector<DoodadTileItem> m_items;

    DECLARE_EVENT_TABLE()
};

#endif // RME_UI_DIALOGS_DOODAD_EDITOR_DIALOG_H_
