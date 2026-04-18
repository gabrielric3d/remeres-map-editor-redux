//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Brushes Editor Dialog - unified hub for all brush-type editors
//////////////////////////////////////////////////////////////////////

#ifndef RME_UI_DIALOGS_BRUSHES_EDITOR_DIALOG_H_
#define RME_UI_DIALOGS_BRUSHES_EDITOR_DIALOG_H_

#include <wx/dialog.h>
#include <wx/notebook.h>

class BorderEditorDialog; // embedded panel
class DoodadEditorDialog; // embedded panel

class BrushesEditorDialog : public wxDialog {
public:
	BrushesEditorDialog(wxWindow* parent);
	~BrushesEditorDialog() override;

	void OnPageChanged(wxBookCtrlEvent& event);
	void OnCloseWindow(wxCloseEvent& event);

private:
	void CreateGUIControls();
	wxPanel* CreatePlaceholderPanel(const wxString& title, const wxString& message);

	wxNotebook* m_notebook;
	BorderEditorDialog* m_bordersPanel;
	DoodadEditorDialog* m_doodadsPanel;
	wxPanel* m_wallsPanel;
	wxPanel* m_tilesetsPanel;

	static constexpr int TAB_BORDERS_GROUNDS = 0;
	static constexpr int TAB_DOODADS = 1;
	static constexpr int TAB_WALLS = 2;
	static constexpr int TAB_TILESETS = 3;

	DECLARE_EVENT_TABLE()
};

#endif // RME_UI_DIALOGS_BRUSHES_EDITOR_DIALOG_H_
