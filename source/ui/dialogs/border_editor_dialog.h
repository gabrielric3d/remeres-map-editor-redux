//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Border Editor Dialog - Visual editor for auto borders
//////////////////////////////////////////////////////////////////////

#ifndef RME_BORDER_EDITOR_DIALOG_H_
#define RME_BORDER_EDITOR_DIALOG_H_

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/spinctrl.h>
#include <wx/panel.h>
#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/choice.h>
#include <wx/notebook.h>
#include <wx/listbox.h>
#include <wx/radiobox.h>
#include <wx/dnd.h>
#include <vector>
#include <map>

class BorderGridPanel;
class BorderPreviewPanel;
class BorderEditorDialog;
class EdgeItemsPanel;
class GroundItemsPanel;
class BorderNorthPreview;
class GroundBordersPanel;

// Drop target for the border grid - accepts item drags from palette
class BorderGridDropTarget : public wxTextDropTarget {
public:
	explicit BorderGridDropTarget(BorderGridPanel* grid);
	bool OnDropText(wxCoord x, wxCoord y, const wxString& data) override;
private:
	BorderGridPanel* m_grid;
};

// Drop target for the ground items panel - accepts item drags from palette
class GroundItemsDropTarget : public wxTextDropTarget {
public:
	explicit GroundItemsDropTarget(GroundItemsPanel* panel);
	bool OnDropText(wxCoord x, wxCoord y, const wxString& data) override;
private:
	GroundItemsPanel* m_panel;
};

enum BorderEdgePosition {
	EDGE_NONE = -1,
	EDGE_N,
	EDGE_E,
	EDGE_S,
	EDGE_W,
	EDGE_CNW,
	EDGE_CNE,
	EDGE_CSE,
	EDGE_CSW,
	EDGE_DNW,
	EDGE_DNE,
	EDGE_DSE,
	EDGE_DSW,
	EDGE_COUNT
};

BorderEdgePosition edgeStringToPosition(const std::string& edgeStr);
std::string edgePositionToString(BorderEdgePosition pos);

// A single item variant for a border edge direction
struct BorderEdgeItem {
	uint16_t itemId = 0;
	int chance = 100;

	BorderEdgeItem() = default;
	BorderEdgeItem(uint16_t id, int c) : itemId(id), chance(c) { }
};

// Legacy struct kept for preview compatibility
struct BorderItem {
	BorderEdgePosition position;
	uint16_t itemId;

	BorderItem() : position(EDGE_NONE), itemId(0) { }
	BorderItem(BorderEdgePosition pos, uint16_t id) : position(pos), itemId(id) { }
};

struct GroundItem {
	uint16_t itemId;
	int chance;

	GroundItem() : itemId(0), chance(10) { }
	GroundItem(uint16_t id, int c) : itemId(id), chance(c) { }
};

// A reference to a border used by a ground brush: <border align="..." to="..." id="..."/>
struct GroundBorderRef {
	std::string align;   // "outer" or "inner"
	std::string to;      // "" (unset/any), "none", or a brush name
	int borderId;
	bool enabled;        // false => serialized with enabled="false" and ignored by loader

	GroundBorderRef() : align("outer"), to(""), borderId(0), enabled(true) { }
	GroundBorderRef(const std::string& a, const std::string& t, int id) : align(a), to(t), borderId(id), enabled(true) { }
};

// Visual panel that displays edge items as sprite cells with X button
class EdgeItemsPanel : public wxPanel {
public:
	EdgeItemsPanel(wxWindow* parent, wxWindowID id = wxID_ANY);

	void SetItems(const std::vector<BorderEdgeItem>& items);
	void Clear();
	int GetSelectedIndex() const { return m_selectedIndex; }

	void OnPaint(wxPaintEvent& event);
	void OnMouseClick(wxMouseEvent& event);

private:
	struct CellRect {
		wxRect bounds;
		wxRect closeBtn;
		int index;
	};

	std::vector<BorderEdgeItem> m_items;
	std::vector<CellRect> m_cells;
	int m_selectedIndex = -1;

	static constexpr int CELL_SIZE = 56;
	static constexpr int CELL_MARGIN = 4;
	static constexpr int CLOSE_BTN_SIZE = 14;

	void RecalcLayout();

	DECLARE_EVENT_TABLE()
};

// Visual panel for ground items: wrapping grid of sprite cells with chance and X button
class GroundItemsPanel : public wxPanel {
public:
	GroundItemsPanel(wxWindow* parent, wxWindowID id = wxID_ANY);

	void SetItems(const std::vector<GroundItem>& items);
	void Clear();
	int GetSelectedIndex() const { return m_selectedIndex; }

	// Called by drop target when an item is dropped on the panel
	void AddItemFromDrop(uint16_t itemId);

	void OnPaint(wxPaintEvent& event);
	void OnMouseClick(wxMouseEvent& event);
	void OnSize(wxSizeEvent& event);

private:
	struct CellRect {
		wxRect bounds;
		wxRect closeBtn;
		int index;
	};

	std::vector<GroundItem> m_items;
	std::vector<CellRect> m_cells;
	int m_selectedIndex = -1;

	static constexpr int CELL_SIZE = 56;
	static constexpr int CELL_MARGIN = 4;
	static constexpr int CLOSE_BTN_SIZE = 14;

	void RecalcLayout();

	DECLARE_EVENT_TABLE()
};

// Visual panel for ground border references: wrapping grid of cells with sprite + label + X
class GroundBordersPanel : public wxPanel {
public:
	GroundBordersPanel(wxWindow* parent, wxWindowID id = wxID_ANY);

	void SetItems(const std::vector<GroundBorderRef>& items);
	const std::vector<GroundBorderRef>& GetItems() const { return m_items; }
	void Clear();
	int GetSelectedIndex() const { return m_selectedIndex; }
	void SetSelectedIndex(int idx);

	void OnPaint(wxPaintEvent& event);
	void OnMouseClick(wxMouseEvent& event);
	void OnSize(wxSizeEvent& event);

private:
	struct CellRect {
		wxRect bounds;
		wxRect closeBtn;
		wxRect enableBtn;
		int index;
	};

	std::vector<GroundBorderRef> m_items;
	std::vector<uint16_t> m_northItemIds;
	std::vector<CellRect> m_cells;
	int m_selectedIndex = -1;

	static constexpr int CELL_SIZE = 64;
	static constexpr int CELL_MARGIN = 4;
	static constexpr int CLOSE_BTN_SIZE = 14;

	void RecalcLayout();
	void RecalcNorthSprites();

	DECLARE_EVENT_TABLE()
};

// Small single-cell preview that shows the "north" edge sprite of a border id
class BorderNorthPreview : public wxPanel {
public:
	BorderNorthPreview(wxWindow* parent, wxWindowID id = wxID_ANY);

	void SetItemId(uint16_t itemId);
	void Clear();

	void OnPaint(wxPaintEvent& event);

private:
	uint16_t m_itemId = 0;

	DECLARE_EVENT_TABLE()
};

// Embedded panel containing the Border + Ground sub-editor.
// Hosted inside BrushesEditorDialog as one of its tabs.
class BorderEditorDialog : public wxPanel {
public:
	BorderEditorDialog(wxWindow* parent);
	~BorderEditorDialog();

	void OnPositionSelected(wxCommandEvent& event);
	void OnAddItem(wxCommandEvent& event);
	void OnRemoveBorderItem(int index);
	void OnUpdateChance(wxCommandEvent& event);
	void OnClear(wxCommandEvent& event);
	void OnSave(wxCommandEvent& event);
	void OnBrowse(wxCommandEvent& event);
	void OnLoadBorder(wxCommandEvent& event);
	void OnPageChanged(wxBookCtrlEvent& event);
	void OnAddGroundItem(wxCommandEvent& event);
	void OnRemoveGroundItem(int index);
	void OnUpdateGroundChance(wxCommandEvent& event);
	void OnLoadGroundBrush(wxCommandEvent& event);
	void OnGroundBrowse(wxCommandEvent& event);
	void OnFindBorderByItemId(wxCommandEvent& event);
	void OnFindGroundByItemId(wxCommandEvent& event);
	void AddGroundItemById(uint16_t itemId);
	void OnAddGroundBorder(wxCommandEvent& event);
	void OnRemoveGroundBorder(wxCommandEvent& event);
	void OnMoveGroundBorderUp(wxCommandEvent& event);
	void OnMoveGroundBorderDown(wxCommandEvent& event);
	void RefreshGroundBordersList(int selectAt = -1);
	void OnGroundBorderIdChanged(wxCommandEvent& event);
	uint16_t LookupNorthItemId(int borderId);
	void OnAddGroundToTileset(wxCommandEvent& event);
	void OnTilesetSelectionChanged(wxCommandEvent& event);
	void LoadExistingTilesets();
	void RefreshTilesetBrushList();
	void ApplyItemToPosition(BorderEdgePosition pos, uint16_t itemId);
	void UpdatePreview();
	void UpdateEdgeItemsList();
	wxString GetVersionDataDirectory();

protected:
	void CreateGUIControls();
	void LoadExistingBorders();
	void LoadExistingGroundBrushes();
	void SaveBorder();
	void SaveGroundBrush();
	bool ValidateBorder();
	bool ValidateGroundBrush();
	void ClearItems();
	void ClearGroundItems();
	void UpdateGroundItemsList();

public:
	// Common
	wxTextCtrl* m_nameCtrl;
	wxSpinCtrl* m_idCtrl;
	wxNotebook* m_notebook;

	// Border Tab
	wxPanel* m_borderPanel;
	wxComboBox* m_existingBordersCombo;
	wxSpinCtrl* m_findBorderItemIdCtrl;
	wxCheckBox* m_isOptionalCheck;
	wxCheckBox* m_isGroundCheck;
	wxSpinCtrl* m_groupCtrl;
	wxSpinCtrl* m_itemIdCtrl;
	wxSpinCtrl* m_itemChanceCtrl;
	EdgeItemsPanel* m_edgeItemsPanel;
	wxStaticText* m_edgeItemsLabel;

	// Ground Tab
	wxPanel* m_groundPanel;
	wxComboBox* m_existingGroundBrushesCombo;
	wxSpinCtrl* m_findGroundItemIdCtrl;
	wxSpinCtrl* m_serverLookIdCtrl;
	wxSpinCtrl* m_zOrderCtrl;
	wxSpinCtrl* m_groundItemIdCtrl;
	wxSpinCtrl* m_groundItemChanceCtrl;
	GroundItemsPanel* m_groundItemsPanel;
	wxButton* m_addGroundItemButton;
	wxButton* m_updateGroundItemButton;

	// Shared action bar (panel-level, below Common Properties)
	wxButton* m_clearButton = nullptr;
	wxButton* m_saveButton = nullptr;

	// Border references for the Ground brush
	GroundBordersPanel* m_groundBordersList;
	wxChoice* m_groundBorderAlignCtrl;
	wxComboBox* m_groundBorderIdCtrl;
	BorderNorthPreview* m_groundBorderPreview;
	wxComboBox* m_groundBorderToCtrl;
	wxButton* m_addGroundBorderButton;
	wxButton* m_removeGroundBorderButton;

	// Tileset assignment
	wxComboBox* m_tilesetCombo;
	wxListBox* m_tilesetBrushList;
	wxRadioBox* m_tilesetInsertPosition;
	wxButton* m_addToTilesetButton;

	// Border items: multiple items per direction with chance
	std::map<BorderEdgePosition, std::vector<BorderEdgeItem>> m_borderItems;

	// Ground items
	std::vector<GroundItem> m_groundItems;

	// Ground border references
	std::vector<GroundBorderRef> m_groundBorders;

	// Border grid
	BorderGridPanel* m_gridPanel;

	// Border preview panel
	BorderPreviewPanel* m_previewPanel;

private:
	int m_nextBorderId;
	int m_activeTab;
	bool m_groundTabLoaded = false;

	DECLARE_EVENT_TABLE()
};

// Grid panel to visually show border item positions
class BorderGridPanel : public wxPanel {
public:
	BorderGridPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~BorderGridPanel();

	void SetItemId(BorderEdgePosition pos, uint16_t itemId);
	void SetItemCount(BorderEdgePosition pos, int count);
	uint16_t GetItemId(BorderEdgePosition pos) const;
	void Clear();
	void LoadSampleBorder();
	void SetPreviewItems(const std::vector<BorderItem>& items);

	void SetSelectedPosition(BorderEdgePosition pos);
	BorderEdgePosition GetSelectedPosition() const { return m_selectedPosition; }

	BorderEdgePosition GetPositionFromCoordinates(int x, int y) const;

	void OnPaint(wxPaintEvent& event);
	void OnMouseClick(wxMouseEvent& event);
	void OnMouseDown(wxMouseEvent& event);
	void OnRightClick(wxMouseEvent& event);

private:
	std::map<BorderEdgePosition, uint16_t> m_items;
	std::map<BorderEdgePosition, int> m_itemCounts;
	std::map<BorderEdgePosition, uint16_t> m_sampleItems;
	std::vector<BorderItem> m_previewItems;
	BorderEdgePosition m_selectedPosition;

	DECLARE_EVENT_TABLE()
};

// Panel to preview how the border would look
class BorderPreviewPanel : public wxPanel {
public:
	BorderPreviewPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~BorderPreviewPanel();

	void SetBorderItems(const std::vector<BorderItem>& items);
	void Clear();

	void OnPaint(wxPaintEvent& event);

private:
	std::vector<BorderItem> m_borderItems;

	DECLARE_EVENT_TABLE()
};

#endif // RME_BORDER_EDITOR_DIALOG_H_
