//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Wall Brush Editor Dialog - Visual editor for wall brushes (walls.xml)
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
#include <vector>

// Reused for the small single-sprite "look" preview next to the Load combo.
#include "ui/dialogs/border_editor_dialog.h"

class WallSegmentItemsPanel;
class WallDoorsPanel;

// The wall segment kinds we let users author. These map 1:1 to the
// <wall type="..."> strings inside walls.xml. walls.xml only ever uses these
// four segment types, so there is no data loss on a load/save round trip.
enum WallSegmentType {
	WALL_SEG_HORIZONTAL = 0,
	WALL_SEG_VERTICAL,
	WALL_SEG_CORNER,
	WALL_SEG_POLE,
	WALL_SEG_COUNT
};

const char* wallSegmentToString(WallSegmentType seg);
WallSegmentType wallSegmentFromString(const std::string& str); // returns WALL_SEG_COUNT if unknown

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
	void OnSegmentSelected(wxCommandEvent& event);
	void OnAddItem(wxCommandEvent& event);
	void OnUpdateChance(wxCommandEvent& event);
	void OnBrowseItem(wxCommandEvent& event);
	void OnAddDoor(wxCommandEvent& event);
	void OnBrowseDoor(wxCommandEvent& event);
	void OnClear(wxCommandEvent& event);
	void OnSave(wxCommandEvent& event);
	void OnLoadWall(wxCommandEvent& event);
	void OnLoadTextChanged(wxCommandEvent& event);
	void OnFindByItemId(wxCommandEvent& event);
	void OnTilesetSelectionChanged(wxCommandEvent& event);
	void OnAddToTileset(wxCommandEvent& event);

	// Called by the item/door cell panels when their X button is clicked.
	void RemoveItemAt(int index);
	void RemoveDoorAt(int index);
	void SelectItemChance(int index); // mirror selected item's chance into the spin

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
	wxRadioBox* m_segmentRadio = nullptr;
	WallSegmentItemsPanel* m_itemsPanel = nullptr;
	wxSpinCtrl* m_itemIdCtrl = nullptr;
	wxSpinCtrl* m_itemChanceCtrl = nullptr;

	// Door editor
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
	wxButton* m_clearButton = nullptr;
	wxButton* m_saveButton = nullptr;

private:
	// Lowercased wall brush name -> server_lookid, for the Load combo preview.
	std::map<wxString, uint16_t> m_wallLookIds;

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
