//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"

#include "ui/dialogs/brushes_editor_dialog.h"
#include "ui/dialogs/border_editor_dialog.h"
#include "ui/dialogs/doodad_editor_dialog.h"
#include "ui/theme.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>

BEGIN_EVENT_TABLE(BrushesEditorDialog, wxDialog)
	EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, BrushesEditorDialog::OnPageChanged)
	EVT_CLOSE(BrushesEditorDialog::OnCloseWindow)
END_EVENT_TABLE()

BrushesEditorDialog::BrushesEditorDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Brushes Editor", wxDefaultPosition, wxSize(1200, 850),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX),
	m_notebook(nullptr),
	m_bordersPanel(nullptr),
	m_doodadsPanel(nullptr),
	m_wallsPanel(nullptr),
	m_tilesetsPanel(nullptr) {

	SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	CreateGUIControls();

	// ESC closes the dialog. There's no wxID_CANCEL button to anchor the default escape
	// behavior to, so handle the key explicitly via CHAR_HOOK (catches the event before
	// any focused child swallows it).
	Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
		if (event.GetKeyCode() == WXK_ESCAPE) {
			Close();
			return;
		}
		event.Skip();
	});

	// Enforce a minimum size so the user never has to resize to see the Ground Items / Borders
	// panels — they collapse badly when the dialog is shorter than ~800px.
	SetMinSize(wxSize(1100, 780));

	CenterOnParent();
}

BrushesEditorDialog::~BrushesEditorDialog() = default;

void BrushesEditorDialog::CreateGUIControls() {
	wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

	m_notebook = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);

	// Bigger bold tab headers — matches the Border/Ground sub-editor's visual language.
	wxFont tabFont = m_notebook->GetFont();
	tabFont.SetPointSize(tabFont.GetPointSize() + 3);
	tabFont.SetWeight(wxFONTWEIGHT_BOLD);
	m_notebook->SetFont(tabFont);
	m_notebook->SetPadding(wxSize(16, 8));

	// Tab 1: Borders & Grounds (the existing border editor, now a wxPanel)
	m_bordersPanel = new BorderEditorDialog(m_notebook);
	m_notebook->AddPage(m_bordersPanel, "  Borders & Grounds  ");

	// Tab 2: Doodads
	m_doodadsPanel = new DoodadEditorDialog(m_notebook);
	m_notebook->AddPage(m_doodadsPanel, "  Doodads  ");

	// Tab 3: Walls (placeholder)
	m_wallsPanel = CreatePlaceholderPanel(
		"Walls Editor",
		"Walls editor is coming soon.\n\n"
		"This will let you author walls.xml entries — wall segments, junctions,\n"
		"doors, windows and pole pieces — from a visual grid.");
	m_notebook->AddPage(m_wallsPanel, "  Walls  ");

	// Tab 4: Tilesets (placeholder)
	m_tilesetsPanel = CreatePlaceholderPanel(
		"Tilesets Editor",
		"Tilesets editor is coming soon.\n\n"
		"This will let you create and organize tilesets, move brushes between\n"
		"categories and rename tileset groups — all editing tilesets.xml.");
	m_notebook->AddPage(m_tilesetsPanel, "  Tilesets  ");

	topSizer->Add(m_notebook, 1, wxEXPAND | wxALL, 5);

	// No footer button row: each embedded editor (Borders, Doodads) owns its own action bar
	// with Clear + Save. Use ESC or the window's X to close the dialog.

	SetSizer(topSizer);
	Layout();
}

wxPanel* BrushesEditorDialog::CreatePlaceholderPanel(const wxString& title, const wxString& message) {
	wxPanel* panel = new wxPanel(m_notebook);
	panel->SetBackgroundColour(Theme::Get(Theme::Role::Surface));

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->AddStretchSpacer(1);

	wxStaticText* titleText = new wxStaticText(panel, wxID_ANY, title);
	wxFont titleFont = titleText->GetFont();
	titleFont.SetPointSize(titleFont.GetPointSize() + 6);
	titleFont.SetWeight(wxFONTWEIGHT_BOLD);
	titleText->SetFont(titleFont);
	titleText->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	sizer->Add(titleText, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 12);

	wxStaticText* messageText = new wxStaticText(panel, wxID_ANY, message, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE);
	messageText->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	sizer->Add(messageText, 0, wxALIGN_CENTER_HORIZONTAL);

	sizer->AddStretchSpacer(1);

	panel->SetSizer(sizer);
	return panel;
}

void BrushesEditorDialog::OnPageChanged(wxBookCtrlEvent& event) {
	// Ignore nested notebook events (the Border/Ground sub-notebook inside the Borders tab).
	if (event.GetEventObject() != m_notebook) {
		event.Skip();
		return;
	}
	// The initial dialog size (1200x850) already fits every tab comfortably.
	event.Skip();
}

void BrushesEditorDialog::OnCloseWindow(wxCloseEvent& event) {
	// Destroy asynchronously so any in-flight timers (e.g. Doodad's load timer) can unwind cleanly.
	Destroy();
}
