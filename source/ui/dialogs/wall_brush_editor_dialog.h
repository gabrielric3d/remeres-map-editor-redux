//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Wall Brush Editor Dialog - Visual editor for wall brushes across all material
// files (walls.xml, doodads.xml, ... — anything included by materials.xml)
//////////////////////////////////////////////////////////////////////

#ifndef RME_UI_DIALOGS_WALL_BRUSH_EDITOR_DIALOG_H_
#define RME_UI_DIALOGS_WALL_BRUSH_EDITOR_DIALOG_H_

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/spinctrl.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/choice.h>
#include <wx/listbox.h>
#include <wx/radiobox.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Reused for the small single-sprite "look" preview next to the Load combo.
#include "ui/dialogs/border_editor_dialog.h"

class WallSegmentItemsPanel;
class WallDoorsPanel;
class WallSegmentGridPanel;

// The wall segment kinds we let users author. These map 1:1 to the
// <wall type="..."> strings inside walls.xml, and to the 16 entries of the engine
// alignment table (WallBrush::full_border_types, indexed by a N/W/E/S connection
// bitmask). The only alignment left out is "untouchable", which is not a shape:
// it survives a load/save round trip through m_preservedWallNodes instead.
//
// The first four values keep their historical order/index - the classifier and any
// stored UI state line up with them.
enum WallSegmentType {
	WALL_SEG_HORIZONTAL = 0, // W+E    -
	WALL_SEG_VERTICAL,       // N+S    |
	WALL_SEG_CORNER,         // N+W    _|   (a.k.a. "northwest diagonal")
	WALL_SEG_POLE,           // none   .
	WALL_SEG_NE_DIAGONAL,    // N+E    |_
	WALL_SEG_SW_DIAGONAL,    // W+S    -,
	WALL_SEG_SE_DIAGONAL,    // E+S    ,-
	WALL_SEG_NORTH_T,        // W+E+S  T
	WALL_SEG_SOUTH_T,        // N+W+E  _|_
	WALL_SEG_EAST_T,         // N+W+S  -|
	WALL_SEG_WEST_T,         // N+E+S  |-
	WALL_SEG_INTERSECTION,   // all    +
	WALL_SEG_NORTH_END,      // S      end pointing north
	WALL_SEG_SOUTH_END,      // N      end pointing south
	WALL_SEG_EAST_END,       // W      end pointing east
	WALL_SEG_WEST_END,       // E      end pointing west
	WALL_SEG_COUNT
};

const char* wallSegmentToString(WallSegmentType seg);
WallSegmentType wallSegmentFromString(const std::string& str); // returns WALL_SEG_COUNT if unknown
// Human-facing label for the grid tooltip / "items for ..." caption.
const char* wallSegmentLabel(WallSegmentType seg);
// N/W/E/S connection bitmask of a segment, used to draw its schematic in the grid.
// Bits match the engine: N=1, W=2, E=4, S=8.
int wallSegmentConnections(WallSegmentType seg);

// A single item variant for a wall segment (id + chance weight).
struct WallItemEntry {
	uint16_t itemId = 0;
	int chance = 100;

	WallItemEntry() = default;
	WallItemEntry(uint16_t id, int c) : itemId(id), chance(c) { }
};

// A door/window piece attached to a wall segment.
// `type` is stored verbatim (e.g. "normal", "locked", "window", "any door") so that
// loading a wall that uses an exotic type and re-saving it never loses the value.
struct WallDoorEntry {
	uint16_t itemId = 0;
	std::string type = "normal";
	bool open = false;

	WallDoorEntry() = default;
	WallDoorEntry(uint16_t id, const std::string& t, bool o) : itemId(id), type(t), open(o) { }
};

// Embedded panel containing the Wall sub-editor.
// Hosted inside BrushesEditorDialog as the "Walls" tab.
class WallBrushEditorDialog : public wxPanel {
public:
	WallBrushEditorDialog(wxWindow* parent);
	~WallBrushEditorDialog() override;

	// Event handlers
	void OnAddItem(wxCommandEvent& event);
	void OnUpdateChance(wxCommandEvent& event);
	void OnBrowseItem(wxCommandEvent& event);
	void OnAddDoor(wxCommandEvent& event);
	void OnBrowseDoor(wxCommandEvent& event);
	void OnScanWall(wxCommandEvent& event);
	void OnClear(wxCommandEvent& event);
	void OnSave(wxCommandEvent& event);
	void OnLoadWall(wxCommandEvent& event);
	void OnLoadTextChanged(wxCommandEvent& event);
	void OnFindByItemId(wxCommandEvent& event);
	void OnTilesetSelectionChanged(wxCommandEvent& event);
	void OnAddToTileset(wxCommandEvent& event);

	// Called by the segment grid when a cell is clicked.
	void SelectSegment(WallSegmentType seg);

	// Called by the item/door cell panels when their X button is clicked.
	void RemoveItemAt(int index);
	void RemoveDoorAt(int index);
	void SelectItemChance(int index); // mirror selected item's chance into the spin

	// Drop entry points: add an item/door dragged from the palette onto the cell
	// panels straight into the segment being edited.
	void AddItemById(uint16_t itemId);
	void AddDoorById(uint16_t itemId);

	// Entry point for the map context menu ("Open in Brushes Editor"): finds the
	// wall brush that uses the given item (an item or a door piece) and loads it,
	// selecting the segment that contains it. Returns false if no wall brush uses it.
	bool OpenItemInEditor(uint16_t itemId);

	WallSegmentType CurrentSegment() const;

private:
	void CreateGUIControls();
	void LoadExistingWalls();
	void LoadExistingTilesets();
	void RefreshTilesetBrushList();
	bool LoadWallByName(const wxString& name);
	bool FindWallByItemId(uint16_t itemId, wxString& outName, WallSegmentType& outSeg);
	void SaveWall();
	bool Validate();
	void ClearAll();
	void RefreshSegmentPanels(); // repaint item + door panels for the current segment
	wxString GetVersionDataDirectory();
	uint16_t LookupWallLookId(const wxString& name) const;

	// All material XML files that may hold wall brushes: every <include> in
	// materials.xml (walls.xml, doodads.xml, grounds.xml, ...), with walls.xml
	// guaranteed present as the default save target. Wall brushes are not confined
	// to walls.xml — e.g. "lava stream" is a type="wall" brush inside doodads.xml.
	wxArrayString GetWallMaterialFiles();

public:
	// Per-segment authored data.
	std::vector<WallItemEntry> m_items[WALL_SEG_COUNT];
	std::vector<WallDoorEntry> m_doors[WALL_SEG_COUNT];

	// UI — common
	wxTextCtrl* m_nameCtrl = nullptr;
	wxSpinCtrl* m_serverLookIdCtrl = nullptr;
	wxCheckBox* m_draggableCheck = nullptr;
	wxCheckBox* m_onBlockingCheck = nullptr;
	wxTextCtrl* m_thicknessCtrl = nullptr;

	wxComboBox* m_existingWallsCombo = nullptr;
	BorderNorthPreview* m_loadPreview = nullptr;
	wxSpinCtrl* m_findItemIdCtrl = nullptr;

	// Segment selector + item editor
	WallSegmentGridPanel* m_segmentGrid = nullptr;
	wxStaticText* m_segmentCaption = nullptr; // "Items for <segment>:"
	WallSegmentItemsPanel* m_itemsPanel = nullptr;
	wxSpinCtrl* m_itemIdCtrl = nullptr;
	wxSpinCtrl* m_itemChanceCtrl = nullptr;

	// Door editor
	wxStaticText* m_doorsCaption = nullptr; // "Doors for <segment>:"
	WallDoorsPanel* m_doorsPanel = nullptr;
	wxSpinCtrl* m_doorIdCtrl = nullptr;
	wxChoice* m_doorTypeCtrl = nullptr;
	wxCheckBox* m_doorOpenCheck = nullptr;

	// Tileset assignment
	wxComboBox* m_tilesetCombo = nullptr;
	wxListBox* m_tilesetBrushList = nullptr;
	wxRadioBox* m_tilesetInsertPosition = nullptr;
	wxButton* m_addToTilesetButton = nullptr;

	// Action bar
	wxButton* m_scanButton = nullptr;
	wxButton* m_clearButton = nullptr;
	wxButton* m_saveButton = nullptr;

private:
	// Lowercased wall brush name -> server_lookid, for the Load combo preview.
	std::map<wxString, uint16_t> m_wallLookIds;
	// Lowercased wall brush name -> full path of the material file it lives in, so
	// Save writes back to the right file (e.g. doodads.xml) instead of duplicating
	// the brush into walls.xml.
	std::map<wxString, wxString> m_wallSourceFiles;

	// The editor models the 16 connection shapes, but not "untouchable" (nor any
	// segment type a future data set might introduce). To avoid destroying those,
	// we capture the parts we don't model on load and re-emit them verbatim on save:
	//   - m_preservedWallNodes: raw XML of <wall> segments with unmodeled types.
	//   - m_preservedBrushAttrs: brush-level attributes we don't manage (e.g. activated).
	std::vector<std::string> m_preservedWallNodes;
	std::vector<std::pair<std::string, std::string>> m_preservedBrushAttrs;
	// The brush name the preserved data above was captured for. Only re-emitted when
	// saving under the same name, so renaming to a new brush doesn't inherit it.
	wxString m_preservedForName;

	DECLARE_EVENT_TABLE()
};

// Clickable map of the 16 wall segments, laid out the way they connect on the map:
// the 3x3 block on the left is the junction cross (corners, T pieces, intersection),
// the right column holds the straights and the pole, and the bottom row the four ends.
//
//   ,-  T  -,  -        <- SE diagonal, north T, SW diagonal, horizontal
//   |- +  -|  |         <- west T, intersection, east T, vertical
//   |_ _|_ _|  .        <- NE diagonal, south T, corner (NW), pole
//   ^  v  <  >          <- south/north/east/west end
//
// A cell with items draws the first one's sprite plus an "xN" badge; an empty cell
// draws the schematic of the shape, so the grid doubles as a checklist of what the
// brush still misses.
class WallSegmentGridPanel : public wxPanel {
public:
	WallSegmentGridPanel(wxWindow* parent, wxWindowID id = wxID_ANY);

	void SetSelected(WallSegmentType seg);
	WallSegmentType GetSelected() const { return m_selected; }

	// Cell contents: sprite of the first item and how many items the segment holds.
	void SetSegmentPreview(WallSegmentType seg, uint16_t itemId, int itemCount, int doorCount);
	void ClearPreviews();

	// Segment under the given client coordinates, or WALL_SEG_COUNT when outside.
	WallSegmentType HitTest(int x, int y) const;

	void OnPaint(wxPaintEvent& event);
	void OnMouseClick(wxMouseEvent& event);
	void OnMotion(wxMouseEvent& event);
	void OnLeave(wxMouseEvent& event);

private:
	struct CellInfo {
		uint16_t itemId = 0;
		int itemCount = 0;
		int doorCount = 0;
	};

	wxRect CellRectFor(WallSegmentType seg) const;
	void DrawSchematic(wxDC& dc, const wxRect& cell, WallSegmentType seg, const wxColour& colour) const;

	WallSegmentType m_selected = WALL_SEG_HORIZONTAL;
	WallSegmentType m_hovered = WALL_SEG_COUNT;
	CellInfo m_cells[WALL_SEG_COUNT];

	static constexpr int GRID_COLS = 4;
	static constexpr int GRID_ROWS = 4;
	static constexpr int CELL_SIZE = 48;
	static constexpr int CELL_MARGIN = 3;

	DECLARE_EVENT_TABLE()
};

// Wrapping grid of sprite cells for the items of the selected wall segment.
// Each cell shows the item sprite, its chance, and an X remove button.
class WallSegmentItemsPanel : public wxPanel {
public:
	WallSegmentItemsPanel(wxWindow* parent, wxWindowID id = wxID_ANY);

	void SetItems(const std::vector<WallItemEntry>& items);
	void Clear();
	int GetSelectedIndex() const { return m_selectedIndex; }

	void OnPaint(wxPaintEvent& event);
	void OnMouseClick(wxMouseEvent& event);
	void OnSize(wxSizeEvent& event);

private:
	struct CellRect {
		wxRect bounds;
		wxRect closeBtn;
		int index;
	};

	std::vector<WallItemEntry> m_items;
	std::vector<CellRect> m_cells;
	int m_selectedIndex = -1;

	static constexpr int CELL_SIZE = 56;
	static constexpr int CELL_MARGIN = 4;
	static constexpr int CLOSE_BTN_SIZE = 14;

	void RecalcLayout();

	DECLARE_EVENT_TABLE()
};

// Wrapping grid of sprite cells for the doors/windows of the selected wall segment.
// Each cell shows the door sprite, its type, an open/closed badge, and an X button.
class WallDoorsPanel : public wxPanel {
public:
	WallDoorsPanel(wxWindow* parent, wxWindowID id = wxID_ANY);

	void SetItems(const std::vector<WallDoorEntry>& items);
	void Clear();
	int GetSelectedIndex() const { return m_selectedIndex; }

	void OnPaint(wxPaintEvent& event);
	void OnMouseClick(wxMouseEvent& event);
	void OnSize(wxSizeEvent& event);

private:
	struct CellRect {
		wxRect bounds;
		wxRect closeBtn;
		int index;
	};

	std::vector<WallDoorEntry> m_items;
	std::vector<CellRect> m_cells;
	int m_selectedIndex = -1;

	static constexpr int CELL_W = 66;
	static constexpr int CELL_H = 80;
	static constexpr int CELL_MARGIN = 4;
	static constexpr int CLOSE_BTN_SIZE = 14;

	void RecalcLayout();

	DECLARE_EVENT_TABLE()
};

#endif // RME_UI_DIALOGS_WALL_BRUSH_EDITOR_DIALOG_H_
