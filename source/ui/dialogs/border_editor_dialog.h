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
#include <wx/notebook.h>
#include <wx/listbox.h>
#include <wx/dnd.h>
#include <vector>
#include <map>

class BorderGridPanel;
class BorderPreviewPanel;
class BorderEditorDialog;

// Drop target for the border grid - accepts item drags from palette
class BorderGridDropTarget : public wxTextDropTarget {
public:
	explicit BorderGridDropTarget(BorderGridPanel* grid);
	bool OnDropText(wxCoord x, wxCoord y, const wxString& data) override;
private:
	BorderGridPanel* m_grid;
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

class BorderEditorDialog : public wxDialog {
public:
	BorderEditorDialog(wxWindow* parent, const wxString& title);
	~BorderEditorDialog();

	void OnPositionSelected(wxCommandEvent& event);
	void OnAddItem(wxCommandEvent& event);
	void OnClear(wxCommandEvent& event);
	void OnSave(wxCommandEvent& event);
	void OnClose(wxCommandEvent& event);
	void OnBrowse(wxCommandEvent& event);
	void OnLoadBorder(wxCommandEvent& event);
	void OnPageChanged(wxBookCtrlEvent& event);
	void OnAddGroundItem(wxCommandEvent& event);
	void OnRemoveGroundItem(wxCommandEvent& event);
	void OnLoadGroundBrush(wxCommandEvent& event);
	void OnGroundBrowse(wxCommandEvent& event);
	void ApplyItemToPosition(BorderEdgePosition pos, uint16_t itemId);
	void UpdatePreview();
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
	wxCheckBox* m_isOptionalCheck;
	wxCheckBox* m_isGroundCheck;
	wxSpinCtrl* m_groupCtrl;
	wxSpinCtrl* m_itemIdCtrl;

	// Ground Tab
	wxPanel* m_groundPanel;
	wxComboBox* m_existingGroundBrushesCombo;
	wxSpinCtrl* m_serverLookIdCtrl;
	wxSpinCtrl* m_zOrderCtrl;
	wxSpinCtrl* m_groundItemIdCtrl;
	wxSpinCtrl* m_groundItemChanceCtrl;
	wxListBox* m_groundItemsList;
	wxButton* m_addGroundItemButton;
	wxButton* m_removeGroundItemButton;

	// Border items
	std::vector<BorderItem> m_borderItems;

	// Ground items
	std::vector<GroundItem> m_groundItems;

	// Border grid
	BorderGridPanel* m_gridPanel;

	// Border preview panel
	BorderPreviewPanel* m_previewPanel;

private:
	int m_nextBorderId;
	int m_activeTab;

	DECLARE_EVENT_TABLE()
};

// Grid panel to visually show border item positions
class BorderGridPanel : public wxPanel {
public:
	BorderGridPanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~BorderGridPanel();

	void SetItemId(BorderEdgePosition pos, uint16_t itemId);
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
