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
#include <wx/radiobox.h>
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
class DoodadSingleItemsPanel;

// Grid size constants.
// The grid is a *window* over the composite, not the composite itself: a composite may
// hold tiles at any offset (and on any floor), and the 10x10 grid shows one 10x10 slice
// of one floor at a time. DOODAD_GRID_CENTER is only the default window origin, the one
// that reproduces the historical -5..+4 view.
const int DOODAD_GRID_SIZE = 10;      // 10x10 visible window
const int DOODAD_GRID_CENTER = 5;     // Default origin: column 0 shows relative -5
const int DOODAD_CELL_SIZE = 38;      // Pixel size of each cell

// Z offsets accepted by the doodad loader.
const int DOODAD_MIN_Z = -7;
const int DOODAD_MAX_Z = 7;

// X/Y range accepted by the doodad loader. The grid window origin is clamped to it, and the
// View spin controls use the same range so panel and controls can never disagree.
const int DOODAD_MAX_ORIGIN = 0x7FFF;

// Pagination constants
const int DOODADS_PER_PAGE = 50;

// Represents a tile in the composite doodad.
// Several entries may share the same (x, y, z): they form a stack, bottom first.
struct DoodadTileItem {
    int x;           // Relative X offset from the composite anchor (any value)
    int y;           // Relative Y offset from the composite anchor (any value)
    int z;           // Z offset (-7 to +7, 0 = the floor the brush is painted on)
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

// Embedded panel containing the Doodad sub-editor.
// Hosted inside BrushesEditorDialog as one of its tabs.
class DoodadEditorDialog : public wxPanel {
public:
    DoodadEditorDialog(wxWindow* parent);
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
    void OnClearAllTiles(wxCommandEvent& event);
    void OnAddCompositeFromSelection(wxCommandEvent& event);
    void OnGridViewChanged(wxSpinEvent& event);
    void OnGridFitView(wxCommandEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnSaveToFile(wxCommandEvent& event);
    void OnBrowseGridItem(wxCommandEvent& event);
    void OnGridItemIdChanged(wxSpinEvent& event);
    void OnPageChanged(wxBookCtrlEvent& event);
    void OnLoadTimer(wxTimerEvent& event);
    void OnFilterChanged(wxCommandEvent& event);
    void OnDoodadListSelected(wxListEvent& event);
    void OnPrevPage(wxCommandEvent& event);
    void OnNextPage(wxCommandEvent& event);
    void OnCreateNew(wxCommandEvent& event);
    void OnAddToTileset(wxCommandEvent& event);
    void OnTilesetSelectionChanged(wxCommandEvent& event);
    void RefreshTilesetBrushList();
    void AddSingleItemById(uint16_t itemId);
    void OnFindByServerId(wxCommandEvent& event);

    // Loads the doodad brush that uses the given item id (as a single item or inside a
    // composite) and jumps to the relevant tab. Returns false if no doodad brush uses it.
    // Used by the "Open in Brushes Editor" flow and the "Find by Server ID" search box.
    bool OpenItemInEditor(uint16_t itemId);

    // Returns the name of the first doodad brush that owns `itemId`, or empty if none.
    wxString FindDoodadBrushNameByItemId(uint16_t itemId) const;

    // Public methods for grid panel to access.
    // allowStack is the difference between a painting gesture (the mouse, a drop) and
    // editing a value (the Item ID spin): only the former may grow a stack, otherwise every
    // click on the spin arrows would pile one more item onto the cell.
    void ApplyItemToGridPosition(int gridX, int gridY, uint16_t itemId, bool allowStack = true);
    void RemoveTopItemFromGridPosition(int gridX, int gridY);
    // Makes sure there is a composite to write into, without touching the grid window.
    void EnsureCurrentComposite();
    void UpdateCompositeFromGrid();
    void UpdateGridFromComposite();
    uint16_t GetCurrentItemId() const;
    void LoadDoodadBrush(const wxString& brushName);
    void RemoveSingleItemAt(int index);
    void SelectSingleItemAt(int index);

    // Turns the current map selection into composite tiles. X/Y are anchored on the
    // selection bounding box (center or top-left, per the Anchor choice), Z is anchored on
    // the floor the user is standing on — so tiles of that floor get z == 0 and land on the
    // tile being painted. Same rule as Area Decoration's "Add Cluster From Selection"
    // (area_decoration_dialog.cpp).
    // The out params are diagnostics for the summary message.
    // Returns false (after showing a message) when there is nothing usable.
    bool BuildCompositeTilesFromSelection(std::vector<DoodadTileItem>& outTiles,
                                          int& outFloorCount,
                                          int& outDroppedZ);

    // How many items of the current composite fall outside the grid window as it stands.
    int CountItemsOutsideGridWindow() const;

    // Moves the grid window so it shows the busiest part of the current composite.
    void FitGridViewToComposite();
    // Syncs the view spin controls with the grid panel without firing their events.
    void SyncGridViewControls();
    void UpdateGridInfoLabel();
    // Rebuilds the composites listbox and restores m_currentCompositeIndex as selection.
    void RefreshCompositeListLabels();

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
    void LoadExistingTilesets();

public:
    // UI Elements - public for access from other components
    // Left panel - Doodad list
    wxTextCtrl* m_filterCtrl;
    wxSpinCtrl* m_findServerIdCtrl;
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
    wxCheckBox* m_saveAsAlternateCheck;
    wxSpinCtrl* m_thicknessCtrl;
    wxSpinCtrl* m_thicknessCeilingCtrl;

    // Single Items Tab
    wxPanel* m_singlePanel;
    DoodadSingleItemsPanel* m_singleItemsList;
    wxSpinCtrl* m_singleItemIdCtrl;
    wxSpinCtrl* m_singleItemChanceCtrl;

    // Composites Tab
    wxPanel* m_compositePanel;
    wxListBox* m_compositesList;
    wxSpinCtrl* m_compositeChanceCtrl;
    wxSpinCtrl* m_gridItemIdCtrl;

    // Grid window controls (layer + origin of the visible 10x10 slice)
    wxSpinCtrl* m_gridLayerCtrl;
    wxSpinCtrl* m_gridOriginXCtrl;
    wxSpinCtrl* m_gridOriginYCtrl;
    wxStaticText* m_gridInfoLabel;
    wxCheckBox* m_gridStackCheck;

    // "From Selection" options
    wxCheckBox* m_fromSelIncludeGroundCheck;
    wxCheckBox* m_fromSelReplaceCheck;
    wxChoice* m_fromSelAnchorChoice;

    // Grid panel for composite editing
    DoodadGridPanel* m_gridPanel;

    // Preview panel
    DoodadPreviewPanel* m_previewPanel;

    // Tileset assignment
    wxComboBox* m_tilesetCombo;
    wxListBox* m_tilesetBrushList;
    wxRadioBox* m_tilesetInsertPosition;
    wxButton* m_addToTilesetButton;

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

// Custom panel for the 10x10 grid editor.
// It is a movable window over the composite: SetView() picks which floor (layer) and which
// 10x10 region of relative coordinates is on screen. Each cell holds a stack of items,
// index 0 at the bottom and back() on top — the same order the map stacks them.
class DoodadGridPanel : public wxPanel {
public:
    DoodadGridPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
    virtual ~DoodadGridPanel();

    void SetItemAt(int gridX, int gridY, uint16_t itemId);      // replaces the whole stack (0 clears it)
    void PushItemAt(int gridX, int gridY, uint16_t itemId);     // stacks on top
    void ReplaceTopItemAt(int gridX, int gridY, uint16_t itemId); // swaps the top item, keeps the stack
    void PopItemAt(int gridX, int gridY);                       // removes the top item
    uint16_t GetItemAt(int gridX, int gridY) const;          // top of the stack, 0 when empty
    const std::vector<uint16_t>& GetStackAt(int gridX, int gridY) const;
    void Clear();                                            // clears the visible window only

    void SetSelectedCell(int gridX, int gridY);
    void GetSelectedCell(int& gridX, int& gridY) const;

    // Window placement. originX/originY are the relative coordinates shown by grid cell 0.
    void SetView(int originX, int originY, int layer);
    int GetOriginX() const { return m_originX; }
    int GetOriginY() const { return m_originY; }
    int GetLayer() const { return m_layer; }

    // Items of the visible window only, stamped with the current layer as z.
    std::vector<DoodadTileItem> GetAllItems() const;
    // Keeps only the items on the current layer that fall inside the window.
    void SetItems(const std::vector<DoodadTileItem>& items);

    // Convert grid coordinates (0-9) to composite-relative coordinates and back
    int GridToRelativeX(int gridCoord) const { return gridCoord + m_originX; }
    int GridToRelativeY(int gridCoord) const { return gridCoord + m_originY; }
    int RelativeToGridX(int relCoord) const { return relCoord - m_originX; }
    int RelativeToGridY(int relCoord) const { return relCoord - m_originY; }
    bool IsInWindow(int relX, int relY) const {
        return relX >= m_originX && relX < m_originX + DOODAD_GRID_SIZE &&
               relY >= m_originY && relY < m_originY + DOODAD_GRID_SIZE;
    }

    void OnPaint(wxPaintEvent& event);
    void OnMouseClick(wxMouseEvent& event);
    void OnMouseDown(wxMouseEvent& event);
    void OnMouseRightUp(wxMouseEvent& event);

    // Public for drop target
    void GetCellFromCoordinates(int px, int py, int& gridX, int& gridY) const;

private:
    // Grid data: [gridX][gridY] -> item stack (bottom first, empty = no item)
    std::vector<uint16_t> m_grid[DOODAD_GRID_SIZE][DOODAD_GRID_SIZE];
    int m_originX;   // relative X shown by grid column 0
    int m_originY;   // relative Y shown by grid row 0
    int m_layer;     // z offset currently on screen
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

// Visual panel for Single Items (non-composite doodad entries).
// Wrapping grid of sprite cells with a chance label and X button — same style as GroundItemsPanel.
class DoodadSingleItemsPanel : public wxPanel {
public:
    DoodadSingleItemsPanel(wxWindow* parent, wxWindowID id = wxID_ANY);

    void SetItems(const std::vector<DoodadSingleItem>& items);
    void Clear();
    int GetSelectedIndex() const { return m_selectedIndex; }
    void SetSelectedIndex(int idx);

    void AddItemFromDrop(uint16_t itemId); // for drop target compatibility

    void OnPaint(wxPaintEvent& event);
    void OnMouseClick(wxMouseEvent& event);
    void OnSize(wxSizeEvent& event);

private:
    struct CellRect {
        wxRect bounds;
        wxRect closeBtn;
        int index;
    };

    std::vector<DoodadSingleItem> m_items;
    std::vector<CellRect> m_cells;
    int m_selectedIndex = -1;

    static constexpr int CELL_SIZE = 56;
    static constexpr int CELL_MARGIN = 4;
    static constexpr int CLOSE_BTN_SIZE = 14;

    void RecalcLayout();

    DECLARE_EVENT_TABLE()
};

#endif // RME_UI_DIALOGS_DOODAD_EDITOR_DIALOG_H_
