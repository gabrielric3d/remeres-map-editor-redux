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

#include "main.h"

#include "palette_brushlist.h"
#include "gui.h"
#include "brush.h"
#include "raw_brush.h"
#include "map_window.h"
#include "map_tab.h"
#include "border_editor_window.h"
#include "doodad_editor_window.h"
#include <wx/dnd.h>
#include <wx/menu.h>

namespace {

constexpr int kMinListIconSize = 16;
constexpr int kMaxListIconSize = 128;
const wxColour kPaletteListBackgroundColour(0x0C, 0x14, 0x2A);
const wxColour kPaletteListSelectionColour(0x16, 0x24, 0x43);
const wxColour kPaletteListTextColour(0xE0, 0xE6, 0xFF);
constexpr int kCreateBorderButtonId = wxID_HIGHEST + 4100;
constexpr int kCreateDoodadButtonId = wxID_HIGHEST + 4101;

int GetConfiguredListIconSize()
{
	int size = g_settings.getInteger(Config::PALETTE_LIST_ICON_SIZE);
	if(size < kMinListIconSize) {
		return kMinListIconSize;
	}
	if(size > kMaxListIconSize) {
		return kMaxListIconSize;
	}
	return size;
}

}

// ============================================================================
// Brush Palette Panel
// A common class for terrain/doodad/item/raw palette

BEGIN_EVENT_TABLE(BrushPalettePanel, PalettePanel)
	EVT_CHOICEBOOK_PAGE_CHANGING(wxID_ANY, BrushPalettePanel::OnSwitchingPage)
	EVT_CHOICEBOOK_PAGE_CHANGED(wxID_ANY, BrushPalettePanel::OnPageChanged)
	EVT_BUTTON(kCreateBorderButtonId, BrushPalettePanel::OnClickCreateBorder)
	EVT_BUTTON(kCreateDoodadButtonId, BrushPalettePanel::OnClickCreateDoodad)
END_EVENT_TABLE()

BrushPalettePanel::BrushPalettePanel(wxWindow* parent, const TilesetContainer& tilesets, TilesetCategoryType category, wxWindowID id) :
	PalettePanel(parent, id),
	palette_type(category),
	choicebook(nullptr),
	size_panel(nullptr)
{
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	// Create the tileset panel
	wxSizer* ts_sizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Tileset");
	wxChoicebook* tmp_choicebook = newd wxChoicebook(this, wxID_ANY, wxDefaultPosition, wxSize(180,250));
	ts_sizer->Add(tmp_choicebook, 1, wxEXPAND);
	topsizer->Add(ts_sizer, 1, wxEXPAND);

	if(palette_type == TILESET_TERRAIN) {
		wxButton* createBorderButton = newd wxButton(this, kCreateBorderButtonId, "Create Border");
		createBorderButton->SetToolTip("Open the Border Editor to create or edit auto-borders");
		topsizer->Add(createBorderButton, 0, wxEXPAND | wxALL, 5);
	}

	if(palette_type == TILESET_DOODAD) {
		wxButton* createDoodadButton = newd wxButton(this, kCreateDoodadButtonId, "Create Doodad");
		createDoodadButton->SetToolTip("Open the Doodad Editor to create or edit doodad brushes");
		topsizer->Add(createDoodadButton, 0, wxEXPAND | wxALL, 5);
	}

	for(TilesetContainer::const_iterator iter = tilesets.begin(); iter != tilesets.end(); ++iter) {
		const TilesetCategory* tcg = iter->second->getCategory(category);
		if(tcg && tcg->size() > 0) {
			BrushPanel* panel = newd BrushPanel(tmp_choicebook);
			panel->AssignTileset(tcg);
			tmp_choicebook->AddPage(panel, wxstr(iter->second->name));
		}
	}

	SetSizerAndFit(topsizer);

	choicebook = tmp_choicebook;
}

BrushPalettePanel::~BrushPalettePanel()
{
	////
}

void BrushPalettePanel::InvalidateContents()
{
	for(size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		panel->InvalidateContents();
	}
	PalettePanel::InvalidateContents();
}

void BrushPalettePanel::LoadCurrentContents()
{
	wxWindow* page = choicebook->GetCurrentPage();
	BrushPanel* panel = dynamic_cast<BrushPanel*>(page);
	if(panel) {
		panel->OnSwitchIn();
	}
	PalettePanel::LoadCurrentContents();
}

void BrushPalettePanel::LoadAllContents()
{
	for(size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		panel->LoadContents();
	}
	PalettePanel::LoadAllContents();
}

PaletteType BrushPalettePanel::GetType() const
{
	return palette_type;
}

void BrushPalettePanel::SetListType(BrushListType ltype)
{
	if(!choicebook) return;
	for(size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		panel->SetListType(ltype);
	}
}

void BrushPalettePanel::SetListType(wxString ltype)
{
	if(!choicebook) return;
	for(size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		panel->SetListType(ltype);
	}
}

Brush* BrushPalettePanel::GetSelectedBrush() const
{
	if(!choicebook) return nullptr;
	wxWindow* page = choicebook->GetCurrentPage();
	BrushPanel* panel = dynamic_cast<BrushPanel*>(page);
	Brush* res = nullptr;
	if(panel) {
		for(ToolBarList::const_iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
			res = (*iter)->GetSelectedBrush();
			if(res) return res;
		}
		res = panel->GetSelectedBrush();
	}
	return res;
}

void BrushPalettePanel::SelectFirstBrush()
{
	if(!choicebook) return;
	wxWindow* page = choicebook->GetCurrentPage();
	BrushPanel* panel = dynamic_cast<BrushPanel*>(page);
	panel->SelectFirstBrush();
}

bool BrushPalettePanel::SelectBrush(const Brush* whatbrush)
{
	if(!choicebook) {
		return false;
	}

	BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	if(!panel) {
		return false;
	}

	for(PalettePanel* toolBar : tool_bars) {
		if(toolBar->SelectBrush(whatbrush)) {
			panel->SelectBrush(nullptr);
			return true;
		}
	}

	if(panel->SelectBrush(whatbrush)) {
		for(PalettePanel* toolBar : tool_bars) {
			toolBar->SelectBrush(nullptr);
		}
		return true;
	}

	for(size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		if((int)iz == choicebook->GetSelection()) {
			continue;
		}

		panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if(panel && panel->SelectBrush(whatbrush)) {
			choicebook->ChangeSelection(iz);
			for(PalettePanel* toolBar : tool_bars) {
				toolBar->SelectBrush(nullptr);
			}
			return true;
		}
	}
	return false;
}

void BrushPalettePanel::OnSwitchingPage(wxChoicebookEvent& event)
{
	event.Skip();
	if(!choicebook) {
		return;
	}
	BrushPanel* old_panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	if(old_panel) {
		old_panel->OnSwitchOut();
		for(ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
			Brush* tmp = (*iter)->GetSelectedBrush();
			if(tmp) {
				remembered_brushes[old_panel] = tmp;
			}
		}
	}

	wxWindow* page = choicebook->GetPage(event.GetSelection());
	BrushPanel* panel = dynamic_cast<BrushPanel*>(page);
	if(panel) {
		panel->OnSwitchIn();
		for(ToolBarList::iterator iter = tool_bars.begin(); iter != tool_bars.end(); ++iter) {
			(*iter)->SelectBrush(remembered_brushes[panel]);
		}
	}
}

void BrushPalettePanel::OnPageChanged(wxChoicebookEvent& event)
{
	if(!choicebook) {
		return;
	}
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void BrushPalettePanel::OnClickCreateBorder(wxCommandEvent& WXUNUSED(event))
{
	BorderEditorDialog* dialog = new BorderEditorDialog(g_gui.root, "Auto Border Editor");
	dialog->Show();
	g_gui.RefreshView();
}

void BrushPalettePanel::OnClickCreateDoodad(wxCommandEvent& WXUNUSED(event))
{
	DoodadEditorDialog* dialog = new DoodadEditorDialog(g_gui.root, "Doodad Brush Editor");
	dialog->Show();
	g_gui.RefreshView();
}

void BrushPalettePanel::OnSwitchIn() {
	LoadCurrentContents();
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushSizeInternal(last_brush_size);
	OnUpdateBrushSize(g_gui.GetBrushShape(), last_brush_size);
}

// ============================================================================
// Brush Panel
// A container of brush buttons

BEGIN_EVENT_TABLE(BrushPanel, wxPanel)
	// Listbox style
	EVT_LISTBOX(wxID_ANY, BrushPanel::OnClickListBoxRow)
	EVT_BUTTON(wxID_ANY, BrushPanel::OnToggleViewMode)
END_EVENT_TABLE()

BrushPanel::BrushPanel(wxWindow *parent) :
	wxPanel(parent, wxID_ANY),
	tileset(nullptr),
	brushbox(nullptr),
	loaded(false),
	list_type(BRUSHLIST_LISTBOX),
	toggle_view_button(nullptr),
	zoom_in_button(nullptr),
	zoom_out_button(nullptr),
	control_sizer(nullptr)
{
	sizer = newd wxBoxSizer(wxVERTICAL);

	// Add control buttons at the top
	control_sizer = newd wxBoxSizer(wxHORIZONTAL);

	toggle_view_button = newd wxButton(this, wxID_ANY, "Grid View", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	toggle_view_button->SetToolTip("Toggle between list and grid view");
	control_sizer->Add(toggle_view_button, 0, wxALL, 2);

	zoom_in_button = newd wxButton(this, wxID_ANY, "+", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	zoom_in_button->SetToolTip("Zoom in");
	zoom_in_button->Enable(false);
	control_sizer->Add(zoom_in_button, 0, wxALL, 2);

	zoom_out_button = newd wxButton(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	zoom_out_button->SetToolTip("Zoom out");
	zoom_out_button->Enable(false);
	control_sizer->Add(zoom_out_button, 0, wxALL, 2);

	sizer->Add(control_sizer, 0, wxEXPAND | wxALL, 2);

	SetSizerAndFit(sizer);
}

BrushPanel::~BrushPanel()
{
	////
}

void BrushPanel::AssignTileset(const TilesetCategory* _tileset)
{
	if(_tileset != tileset) {
		InvalidateContents();
		tileset = _tileset;
	}
}

void BrushPanel::SetListType(BrushListType ltype)
{
	if(list_type != ltype) {
		InvalidateContents();
		list_type = ltype;
	}
}

void BrushPanel::SetListType(wxString ltype)
{
	if(ltype == "small icons") {
		SetListType(BRUSHLIST_SMALL_ICONS);
	} else if(ltype == "large icons") {
		SetListType(BRUSHLIST_LARGE_ICONS);
	} else if(ltype == "listbox") {
		SetListType(BRUSHLIST_LISTBOX);
	} else if(ltype == "textlistbox") {
		SetListType(BRUSHLIST_TEXT_LISTBOX);
	} else if(ltype == "seamless" || ltype == "grid") {
		SetListType(BRUSHLIST_SEAMLESS_GRID);
	}
}

void BrushPanel::CleanupBrushbox(BrushBoxInterface* box)
{
	if(!box) return;

	// Special cleanup for SeamlessGridPanel
	SeamlessGridPanel* gridPanel = dynamic_cast<SeamlessGridPanel*>(box);
	if(gridPanel) {
		// Clear sprite cache
		gridPanel->ClearSpriteCache();
		// Make sure any loading timer is stopped
		wxTimer* timer = gridPanel->GetLoadingTimer();
		if(timer && timer->IsRunning()) {
			timer->Stop();
		}
	}

	// Remove from sizer and destroy
	sizer->Detach(box->GetSelfWindow());
	box->GetSelfWindow()->Destroy();
}

void BrushPanel::InvalidateContents()
{
	// Properly clean up the existing brushbox if it exists
	if(brushbox) {
		CleanupBrushbox(brushbox);
		brushbox = nullptr;
	}
	loaded = false;
}

void BrushPanel::LoadContents()
{
	if(loaded) {
		return;
	}
	loaded = true;
	ASSERT(tileset != nullptr);
	switch (list_type) {
		case BRUSHLIST_LARGE_ICONS:
			brushbox = newd BrushIconBox(this, tileset, RENDER_SIZE_32x32);
			break;
		case BRUSHLIST_SMALL_ICONS:
			brushbox = newd BrushIconBox(this, tileset, RENDER_SIZE_16x16);
			break;
		case BRUSHLIST_LISTBOX:
			brushbox = newd BrushListBox(this, tileset);
			break;
		case BRUSHLIST_SEAMLESS_GRID:
			brushbox = newd SeamlessGridPanel(this, tileset);
			break;
		default:
			break;
	}
	ASSERT(brushbox != nullptr);
	sizer->Add(brushbox->GetSelfWindow(), 1, wxEXPAND);

	// Layout and fit
	sizer->Layout();
	Layout();
	Fit();

	// For SeamlessGridPanel, use CallAfter to ensure the size is correct
	SeamlessGridPanel* gridPanel = dynamic_cast<SeamlessGridPanel*>(brushbox);
	if(gridPanel) {
		// Use CallAfter to delay RecalculateGrid until after layout is fully applied
		CallAfter([gridPanel]() {
			if(gridPanel) {
				gridPanel->RecalculateGrid();
				gridPanel->Refresh();
				gridPanel->Update();
			}
		});
	}

	brushbox->SelectFirstBrush();
}

void BrushPanel::SelectFirstBrush()
{
	if(loaded) {
		ASSERT(brushbox != nullptr);
		brushbox->SelectFirstBrush();
	}
}

Brush* BrushPanel::GetSelectedBrush() const
{
	if(loaded) {
		ASSERT(brushbox != nullptr);
		return brushbox->GetSelectedBrush();
	}

	if(tileset && tileset->size() > 0) {
		return tileset->brushlist[0];
	}
	return nullptr;
}

bool BrushPanel::SelectBrush(const Brush* whatbrush)
{
	if(loaded) {
		//std::cout << loaded << std::endl;
		//std::cout << brushbox << std::endl;
		ASSERT(brushbox != nullptr);
		return brushbox->SelectBrush(whatbrush);
	}

	for(BrushVector::const_iterator iter = tileset->brushlist.begin(); iter != tileset->brushlist.end(); ++iter) {
		if(*iter == whatbrush) {
			LoadContents();
			return brushbox->SelectBrush(whatbrush);
		}
	}
	return false;
}

void BrushPanel::OnSwitchIn()
{
	LoadContents();
}

void BrushPanel::OnSwitchOut()
{
	////
}

void BrushPanel::OnToggleViewMode(wxCommandEvent& event)
{
	if(event.GetEventObject() == toggle_view_button) {
		// Properly cleanup current brushbox first
		if(brushbox) {
			CleanupBrushbox(brushbox);
			brushbox = nullptr;
		}
		loaded = false;

		// Toggle between list and grid view
		if(list_type == BRUSHLIST_SEAMLESS_GRID) {
			list_type = BRUSHLIST_LISTBOX;
			toggle_view_button->SetLabel("Grid View");
			zoom_in_button->Enable(false);
			zoom_out_button->Enable(false);
		} else {
			list_type = BRUSHLIST_SEAMLESS_GRID;
			toggle_view_button->SetLabel("List View");
			zoom_in_button->Enable(true);
			zoom_out_button->Enable(true);
		}

		// Load new contents
		LoadContents();

		// Force proper layout update
		sizer->Layout();
		Layout();

		// Get parent to relayout as well
		wxWindow* parent = GetParent();
		if(parent) {
			parent->Layout();
			parent->Refresh();
		}

		Refresh();
		Update();
	} else if(event.GetEventObject() == zoom_in_button && list_type == BRUSHLIST_SEAMLESS_GRID) {
		OnZoomIn(event);
	} else if(event.GetEventObject() == zoom_out_button && list_type == BRUSHLIST_SEAMLESS_GRID) {
		OnZoomOut(event);
	}
}

void BrushPanel::OnZoomIn(wxCommandEvent& event)
{
	if(list_type == BRUSHLIST_SEAMLESS_GRID && brushbox) {
		SeamlessGridPanel* grid = dynamic_cast<SeamlessGridPanel*>(brushbox);
		if(grid) {
			grid->IncrementZoom();
		}
	}
}

void BrushPanel::OnZoomOut(wxCommandEvent& event)
{
	if(list_type == BRUSHLIST_SEAMLESS_GRID && brushbox) {
		SeamlessGridPanel* grid = dynamic_cast<SeamlessGridPanel*>(brushbox);
		if(grid) {
			grid->DecrementZoom();
		}
	}
}

void BrushPanel::OnClickListBoxRow(wxCommandEvent& event)
{
	ASSERT(tileset->getType() >= TILESET_UNKNOWN && tileset->getType() <= TILESET_CAMERA_PATH);
	// We just notify the GUI of the action, it will take care of everything else
	ASSERT(brushbox);
	size_t n = event.GetSelection();


	wxWindow* w = this;
	while((w = w->GetParent()) && dynamic_cast<PaletteWindow*>(w) == nullptr);

	if(w)
		g_gui.ActivatePalette(static_cast<PaletteWindow*>(w));

	g_gui.SelectBrush(tileset->brushlist[n], tileset->getType());
}

// ============================================================================
// BrushIconBox

BEGIN_EVENT_TABLE(BrushIconBox, wxScrolledWindow)
	// Listbox style
	EVT_TOGGLEBUTTON(wxID_ANY, BrushIconBox::OnClickBrushButton)
	EVT_LEFT_DOWN(BrushIconBox::OnMouseDown)
	EVT_MOTION(BrushIconBox::OnMouseMotion)
	EVT_RIGHT_DOWN(BrushIconBox::OnRightClick)
	EVT_MENU(PALETTE_POPUP_MENU_APPLY_REPLACE_BOX1, BrushIconBox::OnApplyReplaceBox1)
	EVT_MENU(PALETTE_POPUP_MENU_APPLY_REPLACE_BOX2, BrushIconBox::OnApplyReplaceBox2)
	EVT_MENU(PALETTE_POPUP_MENU_APPLY_BRUSH_REPLACE_BOX1, BrushIconBox::OnApplyBrushReplaceBox1)
	EVT_MENU(PALETTE_POPUP_MENU_APPLY_BRUSH_REPLACE_BOX2, BrushIconBox::OnApplyBrushReplaceBox2)
	EVT_MENU(PALETTE_POPUP_MENU_COPY_SERVER_ID, BrushIconBox::OnCopyServerID)
	EVT_MENU(PALETTE_POPUP_MENU_COPY_CLIENT_ID, BrushIconBox::OnCopyClientID)
END_EVENT_TABLE()

BrushIconBox::BrushIconBox(wxWindow *parent, const TilesetCategory *_tileset, RenderSize rsz) :
	wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL),
	BrushBoxInterface(_tileset),
	icon_size(rsz),
	dragging(false)
{
	ASSERT(tileset->getType() >= TILESET_UNKNOWN && tileset->getType() <= TILESET_CAMERA_PATH);
	int width;
	if(icon_size == RENDER_SIZE_32x32) {
		width = std::max(g_settings.getInteger(Config::PALETTE_COL_COUNT) / 2 + 1, 1);
	} else {
		width = std::max(g_settings.getInteger(Config::PALETTE_COL_COUNT) + 1, 1);
	}

	// Create buttons
	wxSizer* stacksizer = newd wxBoxSizer(wxVERTICAL);
	wxSizer* rowsizer = nullptr;
	int item_counter = 0;
	for(BrushVector::const_iterator iter = tileset->brushlist.begin(); iter != tileset->brushlist.end(); ++iter) {
		ASSERT(*iter);
		++item_counter;

		if(!rowsizer) {
			rowsizer = newd wxBoxSizer(wxHORIZONTAL);
		}

		BrushButton* bb = newd BrushButton(this, *iter, rsz);
		rowsizer->Add(bb);
		brush_buttons.push_back(bb);

		if(item_counter % width == 0) { // newd row
			stacksizer->Add(rowsizer);
			rowsizer = nullptr;
		}
	}
	if(rowsizer) {
		stacksizer->Add(rowsizer);
	}

	SetScrollbars(20,20, 8, item_counter/width, 0, 0);
	SetSizer(stacksizer);
}

BrushIconBox::~BrushIconBox()
{
	////
}

void BrushIconBox::SelectFirstBrush()
{
	if(tileset && tileset->size() > 0) {
		DeselectAll();
		brush_buttons[0]->SetValue(true);
		EnsureVisible((size_t)0);
	}
}

Brush* BrushIconBox::GetSelectedBrush() const
{
	if(!tileset) {
		return nullptr;
	}

	for(std::vector<BrushButton*>::const_iterator it = brush_buttons.begin(); it != brush_buttons.end(); ++it) {
		if((*it)->GetValue()) {
			return (*it)->brush;
		}
	}
	return nullptr;
}

bool BrushIconBox::SelectBrush(const Brush* whatbrush)
{
	DeselectAll();
	for(std::vector<BrushButton*>::iterator it = brush_buttons.begin(); it != brush_buttons.end(); ++it) {
		if((*it)->brush == whatbrush) {
			(*it)->SetValue(true);
			EnsureVisible(*it);
			return true;
		}
	}
	return false;
}

void BrushIconBox::DeselectAll()
{
	for(std::vector<BrushButton*>::iterator it = brush_buttons.begin(); it != brush_buttons.end(); ++it) {
		(*it)->SetValue(false);
	}
}

void BrushIconBox::EnsureVisible(BrushButton* btn)
{
	int windowSizeX, windowSizeY;
	GetVirtualSize(&windowSizeX, &windowSizeY);

	int scrollUnitX;
	int scrollUnitY;
	GetScrollPixelsPerUnit(&scrollUnitX, &scrollUnitY);

	wxRect rect = btn->GetRect();
	int y;
	CalcUnscrolledPosition(0, rect.y, nullptr, &y);

	int maxScrollPos = windowSizeY / scrollUnitY;
	int scrollPosY = std::min(maxScrollPos, (y / scrollUnitY));

	int startScrollPosY;
	GetViewStart(nullptr, &startScrollPosY);

	int clientSizeX, clientSizeY;
	GetClientSize(&clientSizeX, &clientSizeY);
	int endScrollPosY = startScrollPosY + clientSizeY / scrollUnitY;

	if(scrollPosY < startScrollPosY || scrollPosY > endScrollPosY){
		//only scroll if the button isnt visible
		Scroll(-1, scrollPosY);
	}
}

void BrushIconBox::EnsureVisible(size_t n)
{
	EnsureVisible(brush_buttons[n]);
}

void BrushIconBox::OnClickBrushButton(wxCommandEvent& event)
{
	wxObject* obj = event.GetEventObject();
	BrushButton* btn = dynamic_cast<BrushButton*>(obj);
	if(btn) {
		wxWindow* w = this;
		while((w = w->GetParent()) && dynamic_cast<PaletteWindow*>(w) == nullptr);
		if(w)
			g_gui.ActivatePalette(static_cast<PaletteWindow*>(w));
		g_gui.SelectBrush(btn->brush, tileset->getType());
	}
}

void BrushIconBox::OnMouseDown(wxMouseEvent& event)
{
	drag_start_pos = event.GetPosition();
	dragging = false;
	event.Skip();
}

void BrushIconBox::OnMouseMotion(wxMouseEvent& event)
{
	if(event.Dragging() && event.LeftIsDown()) {
		if(!dragging) {
			int dx = abs(event.GetPosition().x - drag_start_pos.x);
			int dy = abs(event.GetPosition().y - drag_start_pos.y);
			if(dx > 3 || dy > 3) {
				dragging = true;

				Brush* brush = GetSelectedBrush();
				if(brush && brush->isRaw()) {
					RAWBrush* raw = brush->asRaw();
					if(raw) {
						uint16_t itemId = raw->getItemID();
						wxString data = wxString::Format("ITEM_ID:%d", itemId);
						wxTextDataObject dragData(data);
						wxDropSource dragSource(this);
						dragSource.SetData(dragData);
						dragSource.DoDragDrop(wxDrag_CopyOnly);
					}
				}
			}
		}
	} else {
		dragging = false;
	}
	event.Skip();
}

void BrushIconBox::OnRightClick(wxMouseEvent& event)
{
	Brush* brush = GetSelectedBrush();
	if(!brush) return;

	wxMenu menu;

	if(brush->isRaw()) {
		RAWBrush* raw = brush->asRaw();
		if(raw) {
			menu.Append(PALETTE_POPUP_MENU_COPY_SERVER_ID, wxString::Format("Copy Server ID (%d)", raw->getItemID()));
			menu.Append(PALETTE_POPUP_MENU_COPY_CLIENT_ID, wxString::Format("Copy Client ID (%d)", raw->getLookID()));
			menu.AppendSeparator();
			menu.Append(PALETTE_POPUP_MENU_APPLY_REPLACE_BOX1, "Apply to Replace Box 1 (Original)");
			menu.Append(PALETTE_POPUP_MENU_APPLY_REPLACE_BOX2, "Apply to Replace Box 2 (Replacement)");
			PopupMenu(&menu, event.GetPosition());
		}
	} else if(brush->isGround() || brush->isCarpet()) {
		menu.Append(PALETTE_POPUP_MENU_APPLY_BRUSH_REPLACE_BOX1, "Apply to Replace Box 1 (Original)");
		menu.Append(PALETTE_POPUP_MENU_APPLY_BRUSH_REPLACE_BOX2, "Apply to Replace Box 2 (Replacement)");
		PopupMenu(&menu, event.GetPosition());
	}
}

void BrushIconBox::OnApplyReplaceBox1(wxCommandEvent& WXUNUSED(event))
{
	Brush* brush = GetSelectedBrush();
	if(brush && brush->isRaw()) {
		RAWBrush* raw = brush->asRaw();
		if(raw) {
			uint16_t itemId = raw->getItemID();
			MapTab* tab = g_gui.GetCurrentMapTab();
			if(tab) {
				MapWindow* window = dynamic_cast<MapWindow*>(tab);
				if(window) {
					window->ApplyItemToReplaceBoxOriginal(itemId);
				}
			}
		}
	}
}

void BrushIconBox::OnApplyReplaceBox2(wxCommandEvent& WXUNUSED(event))
{
	Brush* brush = GetSelectedBrush();
	if(brush && brush->isRaw()) {
		RAWBrush* raw = brush->asRaw();
		if(raw) {
			uint16_t itemId = raw->getItemID();
			MapTab* tab = g_gui.GetCurrentMapTab();
			if(tab) {
				MapWindow* window = dynamic_cast<MapWindow*>(tab);
				if(window) {
					window->ApplyItemToReplaceBoxReplacement(itemId);
				}
			}
		}
	}
}

void BrushIconBox::OnApplyBrushReplaceBox1(wxCommandEvent& WXUNUSED(event))
{
	Brush* brush = GetSelectedBrush();
	if(brush && (brush->isGround() || brush->isCarpet())) {
		MapTab* tab = g_gui.GetCurrentMapTab();
		if(tab) {
			MapWindow* window = dynamic_cast<MapWindow*>(tab);
			if(window) {
				window->ApplyBrushToReplaceBoxOriginal(brush);
			}
		}
	}
}

void BrushIconBox::OnApplyBrushReplaceBox2(wxCommandEvent& WXUNUSED(event))
{
	Brush* brush = GetSelectedBrush();
	if(brush && (brush->isGround() || brush->isCarpet())) {
		MapTab* tab = g_gui.GetCurrentMapTab();
		if(tab) {
			MapWindow* window = dynamic_cast<MapWindow*>(tab);
			if(window) {
				window->ApplyBrushToReplaceBoxReplacement(brush);
			}
		}
	}
}

void BrushIconBox::OnCopyServerID(wxCommandEvent& WXUNUSED(event))
{
	Brush* brush = GetSelectedBrush();
	if(brush && brush->isRaw()) {
		RAWBrush* raw = brush->asRaw();
		if(raw) {
			if(wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(std::to_string(raw->getItemID())));
				wxTheClipboard->Close();
			}
		}
	}
}

void BrushIconBox::OnCopyClientID(wxCommandEvent& WXUNUSED(event))
{
	Brush* brush = GetSelectedBrush();
	if(brush && brush->isRaw()) {
		RAWBrush* raw = brush->asRaw();
		if(raw) {
			if(wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(std::to_string(raw->getLookID())));
				wxTheClipboard->Close();
			}
		}
	}
}

// ============================================================================
// BrushListBox

BEGIN_EVENT_TABLE(BrushListBox, wxVListBox)
	EVT_KEY_DOWN(BrushListBox::OnKey)
	EVT_LEFT_DOWN(BrushListBox::OnMouseDown)
	EVT_MOTION(BrushListBox::OnMouseMotion)
	EVT_RIGHT_DOWN(BrushListBox::OnRightClick)
	EVT_MENU(PALETTE_POPUP_MENU_APPLY_REPLACE_BOX1, BrushListBox::OnApplyReplaceBox1)
	EVT_MENU(PALETTE_POPUP_MENU_APPLY_REPLACE_BOX2, BrushListBox::OnApplyReplaceBox2)
	EVT_MENU(PALETTE_POPUP_MENU_APPLY_BRUSH_REPLACE_BOX1, BrushListBox::OnApplyBrushReplaceBox1)
	EVT_MENU(PALETTE_POPUP_MENU_APPLY_BRUSH_REPLACE_BOX2, BrushListBox::OnApplyBrushReplaceBox2)
	EVT_MENU(PALETTE_POPUP_MENU_COPY_SERVER_ID, BrushListBox::OnCopyServerID)
	EVT_MENU(PALETTE_POPUP_MENU_COPY_CLIENT_ID, BrushListBox::OnCopyClientID)
END_EVENT_TABLE()

BrushListBox::BrushListBox(wxWindow* parent, const TilesetCategory* tileset) :
	wxVListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE),
	BrushBoxInterface(tileset),
	icon_pixel_size(GetConfiguredListIconSize()),
	dragging(false)
{
	SetBackgroundColour(kPaletteListBackgroundColour);
	SetOwnForegroundColour(kPaletteListTextColour);
	SetItemCount(tileset->size());
}

BrushListBox::~BrushListBox()
{
	////
}

void BrushListBox::SelectFirstBrush()
{
	SetSelection(0);
	wxWindow::ScrollLines(-1);
}

Brush* BrushListBox::GetSelectedBrush() const
{
	if(!tileset) {
		return nullptr;
	}

	int n = GetSelection();
	if(n != wxNOT_FOUND) {
		return tileset->brushlist[n];
	} else if(tileset->size() > 0) {
		return tileset->brushlist[0];
	}
	return nullptr;
}

bool BrushListBox::SelectBrush(const Brush* whatbrush)
{
	for(size_t n = 0; n < tileset->size(); ++n) {
		if(tileset->brushlist[n] == whatbrush) {
			SetSelection(n);
			return true;
		}
	}
	return false;
}

void BrushListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t n) const
{
	ASSERT(n < tileset->size());
	dc.SetPen(*wxTRANSPARENT_PEN);
	if(IsSelected(n)) {
		dc.SetBrush(wxBrush(kPaletteListSelectionColour));
	} else {
		dc.SetBrush(wxBrush(kPaletteListBackgroundColour));
	}
	dc.DrawRectangle(rect);

	const int padding = 4;
	const int icon_x = rect.GetX() + padding;
	const int icon_y = rect.GetY() + std::max(0, (rect.GetHeight() - icon_pixel_size) / 2);

	Brush* brush = tileset->brushlist[n];
	int look_id = brush->getLookID();

	// Check for technical tiles (invisible walkable/blocking tiles) by name
	std::string brush_name = brush->getName();
	bool is_stairs = (brush_name.find("459") != std::string::npos && brush_name.find("stairs") != std::string::npos);
	bool is_walkable = (brush_name.find("460") != std::string::npos || brush_name.find("invisible walkable") != std::string::npos || brush_name.find("transparent walkable") != std::string::npos);
	bool is_not_walkable = (brush_name.find("1548") != std::string::npos || brush_name.find("transparent not walkable") != std::string::npos || brush_name.find("transparency tile") != std::string::npos);

	if (is_stairs || is_walkable || is_not_walkable) {
		// Draw colored square for technical tiles
		wxColor techColor;
		if (is_stairs) {
			techColor = wxColor(200, 200, 0); // Yellow for stairs
		} else if (is_walkable) {
			techColor = wxColor(200, 0, 0); // Red for transparent walkable
		} else if (is_not_walkable) {
			techColor = wxColor(0, 200, 200); // Cyan for transparent not walkable
		}
		dc.SetBrush(wxBrush(techColor));
		dc.SetPen(wxPen(techColor, 1));
		dc.DrawRectangle(icon_x + 2, icon_y + 2, icon_pixel_size - 4, icon_pixel_size - 4);
	} else {
		Sprite* spr = g_gui.gfx.getSprite(look_id);
		if(spr) {
			spr->DrawTo(&dc, SPRITE_SIZE_32x32, icon_x, icon_y, icon_pixel_size, icon_pixel_size);
		}
	}

	if(IsSelected(n)) {
		dc.SetTextForeground(wxColor(0xFF, 0xFF, 0xFF));
	} else {
		dc.SetTextForeground(kPaletteListTextColour);
	}
	const int text_x = icon_x + icon_pixel_size + padding;
	const int char_height = dc.GetCharHeight();
	const int text_y = rect.GetY() + std::max(0, (rect.GetHeight() - char_height) / 2);
	dc.DrawText(wxstr(brush->getName()), text_x, text_y);
}

wxCoord BrushListBox::OnMeasureItem(size_t n) const
{
	const int padding = 4;
	return icon_pixel_size + padding * 2;
}

void BrushListBox::OnKey(wxKeyEvent& event)
{
	switch(event.GetKeyCode()) {
		case WXK_UP:
		case WXK_DOWN:
		case WXK_LEFT:
		case WXK_RIGHT:
			if(g_settings.getInteger(Config::LISTBOX_EATS_ALL_EVENTS)) {
		case WXK_PAGEUP:
		case WXK_PAGEDOWN:
		case WXK_HOME:
		case WXK_END:
			event.Skip(true);
			} else {
			[[fallthrough]];
		default:
			if(g_gui.GetCurrentTab() != nullptr) {
				g_gui.GetCurrentMapTab()->GetEventHandler()->AddPendingEvent(event);
			}
		}
	}
}

void BrushListBox::OnMouseDown(wxMouseEvent& event)
{
	drag_start_pos = event.GetPosition();
	dragging = false;
	event.Skip();
}

void BrushListBox::OnMouseMotion(wxMouseEvent& event)
{
	if(event.Dragging() && event.LeftIsDown()) {
		if(!dragging) {
			int dx = abs(event.GetPosition().x - drag_start_pos.x);
			int dy = abs(event.GetPosition().y - drag_start_pos.y);
			if(dx > 3 || dy > 3) {
				dragging = true;

				int n = GetSelection();
				if(n != wxNOT_FOUND && tileset && (size_t)n < tileset->size()) {
					Brush* brush = tileset->brushlist[n];
					if(brush && brush->isRaw()) {
						RAWBrush* raw = brush->asRaw();
						if(raw) {
							uint16_t itemId = raw->getItemID();
							wxString data = wxString::Format("ITEM_ID:%d", itemId);
							wxTextDataObject dragData(data);
							wxDropSource dragSource(this);
							dragSource.SetData(dragData);
							dragSource.DoDragDrop(wxDrag_CopyOnly);
						}
					}
				}
			}
		}
	} else {
		dragging = false;
	}
	event.Skip();
}

void BrushListBox::OnRightClick(wxMouseEvent& event)
{
	int n = GetSelection();
	if(n != wxNOT_FOUND && tileset && (size_t)n < tileset->size()) {
		Brush* brush = tileset->brushlist[n];
		if(!brush) return;

		wxMenu menu;

		if(brush->isRaw()) {
			RAWBrush* raw = brush->asRaw();
			if(raw) {
				menu.Append(PALETTE_POPUP_MENU_COPY_SERVER_ID, wxString::Format("Copy Server ID (%d)", raw->getItemID()));
				menu.Append(PALETTE_POPUP_MENU_COPY_CLIENT_ID, wxString::Format("Copy Client ID (%d)", raw->getLookID()));
				menu.AppendSeparator();
				menu.Append(PALETTE_POPUP_MENU_APPLY_REPLACE_BOX1, "Apply to Replace Box 1 (Original)");
				menu.Append(PALETTE_POPUP_MENU_APPLY_REPLACE_BOX2, "Apply to Replace Box 2 (Replacement)");
				PopupMenu(&menu, event.GetPosition());
			}
		} else if(brush->isGround() || brush->isCarpet()) {
			menu.Append(PALETTE_POPUP_MENU_APPLY_BRUSH_REPLACE_BOX1, "Apply to Replace Box 1 (Original)");
			menu.Append(PALETTE_POPUP_MENU_APPLY_BRUSH_REPLACE_BOX2, "Apply to Replace Box 2 (Replacement)");
			PopupMenu(&menu, event.GetPosition());
		}
	}
}

void BrushListBox::OnApplyReplaceBox1(wxCommandEvent& WXUNUSED(event))
{
	int n = GetSelection();
	if(n != wxNOT_FOUND && tileset && (size_t)n < tileset->size()) {
		Brush* brush = tileset->brushlist[n];
		if(brush && brush->isRaw()) {
			RAWBrush* raw = brush->asRaw();
			if(raw) {
				uint16_t itemId = raw->getItemID();
				MapTab* tab = g_gui.GetCurrentMapTab();
				if(tab) {
					MapWindow* window = dynamic_cast<MapWindow*>(tab);
					if(window) {
						window->ApplyItemToReplaceBoxOriginal(itemId);
					}
				}
			}
		}
	}
}

void BrushListBox::OnApplyReplaceBox2(wxCommandEvent& WXUNUSED(event))
{
	int n = GetSelection();
	if(n != wxNOT_FOUND && tileset && (size_t)n < tileset->size()) {
		Brush* brush = tileset->brushlist[n];
		if(brush && brush->isRaw()) {
			RAWBrush* raw = brush->asRaw();
			if(raw) {
				uint16_t itemId = raw->getItemID();
				MapTab* tab = g_gui.GetCurrentMapTab();
				if(tab) {
					MapWindow* window = dynamic_cast<MapWindow*>(tab);
					if(window) {
						window->ApplyItemToReplaceBoxReplacement(itemId);
					}
				}
			}
		}
	}
}

void BrushListBox::OnApplyBrushReplaceBox1(wxCommandEvent& WXUNUSED(event))
{
	int n = GetSelection();
	if(n != wxNOT_FOUND && tileset && (size_t)n < tileset->size()) {
		Brush* brush = tileset->brushlist[n];
		if(brush && (brush->isGround() || brush->isCarpet())) {
			MapTab* tab = g_gui.GetCurrentMapTab();
			if(tab) {
				MapWindow* window = dynamic_cast<MapWindow*>(tab);
				if(window) {
					window->ApplyBrushToReplaceBoxOriginal(brush);
				}
			}
		}
	}
}

void BrushListBox::OnApplyBrushReplaceBox2(wxCommandEvent& WXUNUSED(event))
{
	int n = GetSelection();
	if(n != wxNOT_FOUND && tileset && (size_t)n < tileset->size()) {
		Brush* brush = tileset->brushlist[n];
		if(brush && (brush->isGround() || brush->isCarpet())) {
			MapTab* tab = g_gui.GetCurrentMapTab();
			if(tab) {
				MapWindow* window = dynamic_cast<MapWindow*>(tab);
				if(window) {
					window->ApplyBrushToReplaceBoxReplacement(brush);
				}
			}
		}
	}
}

void BrushListBox::OnCopyServerID(wxCommandEvent& WXUNUSED(event))
{
	int n = GetSelection();
	if(n != wxNOT_FOUND && tileset && (size_t)n < tileset->size()) {
		Brush* brush = tileset->brushlist[n];
		if(brush && brush->isRaw()) {
			RAWBrush* raw = brush->asRaw();
			if(raw) {
				if(wxTheClipboard->Open()) {
					wxTheClipboard->SetData(new wxTextDataObject(std::to_string(raw->getItemID())));
					wxTheClipboard->Close();
				}
			}
		}
	}
}

void BrushListBox::OnCopyClientID(wxCommandEvent& WXUNUSED(event))
{
	int n = GetSelection();
	if(n != wxNOT_FOUND && tileset && (size_t)n < tileset->size()) {
		Brush* brush = tileset->brushlist[n];
		if(brush && brush->isRaw()) {
			RAWBrush* raw = brush->asRaw();
			if(raw) {
				if(wxTheClipboard->Open()) {
					wxTheClipboard->SetData(new wxTextDataObject(std::to_string(raw->getLookID())));
					wxTheClipboard->Close();
				}
			}
		}
	}
}

// ============================================================================
// SeamlessGridPanel
// A direct rendering class for dense sprite grid with zero margins and pagination

BEGIN_EVENT_TABLE(SeamlessGridPanel, wxScrolledWindow)
EVT_LEFT_DOWN(SeamlessGridPanel::OnMouseClick)
EVT_MOTION(SeamlessGridPanel::OnMouseMove)
EVT_PAINT(SeamlessGridPanel::OnPaint)
EVT_SIZE(SeamlessGridPanel::OnSize)
EVT_SCROLLWIN(SeamlessGridPanel::OnScroll)
EVT_TIMER(wxID_ANY, SeamlessGridPanel::OnTimer)
EVT_KEY_DOWN(SeamlessGridPanel::OnKeyDown)
END_EVENT_TABLE()

SeamlessGridPanel::SeamlessGridPanel(wxWindow* parent, const TilesetCategory* _tileset) :
	wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxWANTS_CHARS),
	BrushBoxInterface(_tileset),
	columns(1),
	sprite_size(32),
	zoom_level(1),
	selected_index(-1),
	hover_index(-1),
	buffer(nullptr),
	show_item_ids(false),
	first_visible_row(0),
	last_visible_row(0),
	visible_rows_margin(30),
	total_rows(0),
	need_full_redraw(true),
	use_progressive_loading(true),
	is_large_tileset(false),
	loading_step(0),
	max_loading_steps(5),
	loading_timer(nullptr),
	chunk_size(g_settings.getInteger(Config::GRID_CHUNK_SIZE)),
	current_chunk(0),
	total_chunks(1),
	navigation_panel(nullptr) {

	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetBackgroundColour(wxColour(64, 64, 64));
	SetWindowStyle(GetWindowStyle() | wxWANTS_CHARS);

	is_large_tileset = tileset && tileset->size() > LARGE_TILESET_THRESHOLD;

	if (tileset && tileset->size() > 10000) {
		total_chunks = (tileset->size() + chunk_size - 1) / chunk_size;
		CreateNavigationPanel(parent);
	}

	visible_rows_margin = g_settings.getInteger(Config::GRID_VISIBLE_ROWS_MARGIN);

	if (is_large_tileset && use_progressive_loading) {
		loading_timer = new wxTimer(this);
		max_loading_steps = 10;
	}

	sprite_cache.clear();
	SetScrollRate(sprite_size, sprite_size);
	RecalculateGrid();
	SelectFirstBrush();

	if (is_large_tileset && use_progressive_loading) {
		StartProgressiveLoading();
	}

	// Ensure the panel is shown and has minimum size
	SetMinSize(wxSize(100, 100));
	Show();
}

SeamlessGridPanel::~SeamlessGridPanel() {
	if (buffer) {
		delete buffer;
		buffer = nullptr;
	}

	if (loading_timer) {
		loading_timer->Stop();
		delete loading_timer;
		loading_timer = nullptr;
	}

	ClearSpriteCache();
}

void SeamlessGridPanel::StartProgressiveLoading() {
	if (!loading_timer) return;

	loading_step = 0;
	visible_rows_margin = 3;
	need_full_redraw = true;

	if(tileset->size() < 200) {
		loading_step = max_loading_steps;
		visible_rows_margin = 30;
		if(loading_timer->IsRunning()) {
			loading_timer->Stop();
		}
		UpdateViewableItems();
		Refresh();
		return;
	}

	int zoom_factor = zoom_level * zoom_level;
	int itemsToShowInitially = std::min(100 / zoom_factor, static_cast<int>(tileset->size()));
	itemsToShowInitially = std::max(20, itemsToShowInitially);

	int itemsPerStep = (tileset->size() - itemsToShowInitially) / max_loading_steps;
	itemsPerStep = std::max(30, itemsPerStep / zoom_factor);

	if (itemsPerStep < 50) {
		max_loading_steps = std::max(3, static_cast<int>(tileset->size() / (50 / zoom_factor)));
	}

	int interval = 200 + (zoom_level - 1) * 50;
	loading_timer->Start(interval);
	Refresh();
}

void SeamlessGridPanel::OnTimer(wxTimerEvent& event) {
	loading_step++;
	visible_rows_margin = std::min(3 + loading_step * 5, 30);
	UpdateViewableItems();
	Refresh();

	if (loading_step >= max_loading_steps || static_cast<int>(tileset->size()) <= 1000) {
		loading_timer->Stop();
		loading_step = max_loading_steps;
		visible_rows_margin = 30;
		need_full_redraw = true;
		Refresh();
	}
}

void SeamlessGridPanel::UpdateViewableItems() {
	if(!tileset || tileset->size() == 0 || total_rows == 0) return;

	int xStart, yStart;
	GetViewStart(&xStart, &yStart);
	int ppuX, ppuY;
	GetScrollPixelsPerUnit(&ppuX, &ppuY);

	if(ppuY > 0) {
		yStart *= ppuY;
	} else {
		yStart = 0;
	}

	int width, height;
	GetClientSize(&width, &height);

	// If no valid height, use a reasonable default
	if(height <= 0) {
		height = 250; // Default palette height
	}

	// If sprite_size is 0, use default
	int effective_sprite_size = sprite_size > 0 ? sprite_size : 32;

	int new_first_row = std::max(0, (yStart / effective_sprite_size) - visible_rows_margin);
	int new_last_row = ((yStart + height) / effective_sprite_size) + visible_rows_margin;

	// Ensure we don't exceed the total rows
	if(new_last_row >= total_rows) {
		new_last_row = total_rows - 1;
	}

	// Ensure we show at least something
	if(new_last_row < 0) {
		new_last_row = total_rows > 0 ? total_rows - 1 : 0;
	}

	if (new_first_row != first_visible_row || new_last_row != last_visible_row) {
		first_visible_row = new_first_row;
		last_visible_row = new_last_row;
		Refresh();
	}
}

void SeamlessGridPanel::ManageSpriteCache() {
	const size_t MAX_CACHE_SIZE = 2000;
	if (sprite_cache.size() > MAX_CACHE_SIZE) {
		sprite_cache.clear();
	}
}

void SeamlessGridPanel::ClearSpriteCache() {
	sprite_cache.clear();
}

void SeamlessGridPanel::DrawItemsToPanel(wxDC& dc) {
	if(!tileset || tileset->size() == 0) return;

	if (need_full_redraw) {
		ManageSpriteCache();
	}

	int width, height;
	GetClientSize(&width, &height);

	// Draw loading progress for large datasets
	if (loading_step < max_loading_steps && tileset->size() > 1000) {
		int progressWidth = width - 40;
		int progressHeight = 20;
		int progressX = 20;
		int progressY = 20;

		dc.SetBrush(wxBrush(wxColor(200, 200, 200)));
		dc.SetPen(wxPen(wxColor(100, 100, 100)));
		dc.DrawRectangle(progressX, progressY, progressWidth, progressHeight);

		float progress = static_cast<float>(loading_step + 1) / max_loading_steps;
		dc.SetBrush(wxBrush(wxColor(0, 150, 0)));
		dc.SetPen(wxPen(wxColor(0, 100, 0)));
		dc.DrawRectangle(progressX, progressY, progressWidth * progress, progressHeight);

		wxString zoomInfo = zoom_level > 1 ? wxString::Format(" (Zoom %dx)", zoom_level) : wxString("");
		wxString loadingMsg = wxString::Format("Loading %zu items%s... (%d%%)",
			tileset->size(), zoomInfo.c_str(), static_cast<int>((loading_step + 1) * 100 / max_loading_steps));

		wxSize textSize = dc.GetTextExtent(loadingMsg);
		dc.SetTextForeground(wxColor(0, 0, 0));
		dc.DrawText(loadingMsg, (width - textSize.GetWidth()) / 2, progressY + progressHeight + 5);

		int maxItemsToDraw = tileset->size() * progress;

		for(int row = first_visible_row; row <= last_visible_row; ++row) {
			for(int col = 0; col < columns; ++col) {
				int index = row * columns + col;

				if (tileset->size() > 10000) {
					index = current_chunk * chunk_size + index;
				}

				if(index >= static_cast<int>(tileset->size()) || index >= maxItemsToDraw) break;

				int x = col * sprite_size;
				int y = row * sprite_size;

				if (y < progressY + progressHeight + 40) continue;

				DrawSpriteAt(dc, x, y, index);
			}
		}
	} else {
		if (tileset->size() > 10000) {
			size_t chunk_start = current_chunk * chunk_size;
			size_t items_in_chunk = std::min(static_cast<size_t>(chunk_size), tileset->size() - chunk_start);

			for(int row = first_visible_row; row <= last_visible_row; ++row) {
				for(int col = 0; col < columns; ++col) {
					int local_index = row * columns + col;
					if(local_index >= static_cast<int>(items_in_chunk)) break;

					int global_index = chunk_start + local_index;
					int x = col * sprite_size;
					int y = row * sprite_size;

					DrawSpriteAt(dc, x, y, global_index);
				}
			}
		} else {
			for(int row = first_visible_row; row <= last_visible_row; ++row) {
				for(int col = 0; col < columns; ++col) {
					int index = row * columns + col;
					if(index >= static_cast<int>(tileset->size())) break;

					int x = col * sprite_size;
					int y = row * sprite_size;

					DrawSpriteAt(dc, x, y, index);
				}
			}
		}
	}

	need_full_redraw = false;
}

void SeamlessGridPanel::DrawSpriteAt(wxDC& dc, int x, int y, int index) {
	if (index < 0 || index >= static_cast<int>(tileset->size())) return;

	Brush* brush = tileset->brushlist[index];
	if (!brush) return;

	if (index == selected_index) {
		wxColor selectColor(120, 120, 200, 180);
		dc.SetBrush(wxBrush(selectColor));
		dc.SetPen(wxPen(wxColor(80, 80, 160), 2));
		dc.DrawRectangle(x, y, sprite_size, sprite_size);
	} else if (index == hover_index) {
		wxColor hoverColor(200, 200, 255, 120);
		dc.SetBrush(wxBrush(hoverColor));
		dc.SetPen(wxPen(wxColor(150, 150, 230, 180), 1));
		dc.DrawRectangle(x, y, sprite_size, sprite_size);
	}

	bool need_to_create_sprite = true;

	if (sprite_cache.count(index) > 0 && sprite_cache[index].is_valid) {
		if (sprite_cache[index].zoom_level == zoom_level) {
			dc.DrawBitmap(sprite_cache[index].bitmap, x, y, true);
			need_to_create_sprite = false;
		}
	}

	if (need_to_create_sprite) {
		int look_id = brush->getLookID();
		Sprite* sprite = g_gui.gfx.getSprite(look_id);

		// Check for technical tiles (invisible walkable/blocking tiles) by name
		std::string brush_name = brush->getName();
		bool is_stairs = (brush_name.find("459") != std::string::npos && brush_name.find("stairs") != std::string::npos);
		bool is_walkable = (brush_name.find("460") != std::string::npos || brush_name.find("invisible walkable") != std::string::npos || brush_name.find("transparent walkable") != std::string::npos);
		bool is_not_walkable = (brush_name.find("1548") != std::string::npos || brush_name.find("transparent not walkable") != std::string::npos || brush_name.find("transparency tile") != std::string::npos);

		if (is_stairs || is_walkable || is_not_walkable) {
			// Draw colored square for technical tiles
			wxColor techColor;
			if (is_stairs) {
				techColor = wxColor(200, 200, 0); // Yellow for stairs
			} else if (is_walkable) {
				techColor = wxColor(200, 0, 0); // Red for transparent walkable
			} else if (is_not_walkable) {
				techColor = wxColor(0, 200, 200); // Cyan for transparent not walkable
			}

			dc.SetBrush(wxBrush(techColor));
			dc.SetPen(wxPen(techColor.IsOk() ? techColor : *wxBLACK, 1));
			dc.DrawRectangle(x + 2, y + 2, sprite_size - 4, sprite_size - 4);
		} else if (sprite) {
			if (zoom_level == 1) {
				wxBitmap bmp(32, 32);
				wxMemoryDC memDC(bmp);
				memDC.SetBackground(*wxTRANSPARENT_BRUSH);
				memDC.Clear();
				sprite->DrawTo(&memDC, SPRITE_SIZE_32x32, 0, 0, 32, 32);
				memDC.SelectObject(wxNullBitmap);

				CachedSprite cached;
				cached.bitmap = bmp;
				cached.zoom_level = zoom_level;
				cached.is_valid = true;
				sprite_cache[index] = cached;

				dc.DrawBitmap(bmp, x, y, true);
			} else {
				wxBitmap temp_bmp(32, 32);
				wxMemoryDC temp_dc(temp_bmp);
				temp_dc.SetBackground(*wxTRANSPARENT_BRUSH);
				temp_dc.Clear();
				sprite->DrawTo(&temp_dc, SPRITE_SIZE_32x32, 0, 0, 32, 32);
				temp_dc.SelectObject(wxNullBitmap);

				wxImage img = temp_bmp.ConvertToImage();
				img.SetMaskColour(255, 0, 255);
				img = img.Rescale(sprite_size, sprite_size, wxIMAGE_QUALITY_HIGH);
				wxBitmap scaled(img);

				CachedSprite cached;
				cached.bitmap = scaled;
				cached.zoom_level = zoom_level;
				cached.is_valid = true;
				sprite_cache[index] = cached;

				dc.DrawBitmap(scaled, x, y, true);
			}
		}
	}

	if (show_item_ids && brush->isRaw()) {
		RAWBrush* raw = static_cast<RAWBrush*>(brush);
		wxFont font = dc.GetFont();
		font.SetPointSize(std::max(8, 8 + (zoom_level - 1) * 2));
		dc.SetFont(font);

		wxString idText = wxString::Format("%d", raw->getItemID());
		wxSize textSize = dc.GetTextExtent(idText);
		int textHeight = std::max(14, 14 + (zoom_level - 1) * 4);

		wxColor bgColor(0, 0, 0, 140);
		dc.SetBrush(wxBrush(bgColor));
		dc.SetPen(wxPen(wxColor(0, 0, 0, 0)));
		dc.DrawRectangle(x, y + sprite_size - textHeight, textSize.GetWidth() + 4, textHeight);

		dc.SetTextForeground(wxColor(255, 255, 255));
		dc.DrawText(idText, x + 2, y + sprite_size - textHeight);
	}
}

void SeamlessGridPanel::OnPaint(wxPaintEvent& event) {
	wxAutoBufferedPaintDC dc(this);
	DoPrepareDC(dc);
	dc.SetBackground(wxBrush(GetBackgroundColour()));
	dc.Clear();
	DrawItemsToPanel(dc);
}

void SeamlessGridPanel::OnSize(wxSizeEvent& event) {
	RecalculateGrid();
	Refresh();
	event.Skip();
}

void SeamlessGridPanel::RecalculateGrid() {
	if(!tileset || tileset->size() == 0) return;

	int width, height;
	GetClientSize(&width, &height);

	// If no valid size yet, try to get from parent
	if(width <= 0) {
		wxWindow* parent = GetParent();
		if(parent) {
			parent->GetClientSize(&width, &height);
		}
	}

	// Ensure we have a valid width - use sensible minimum
	if(width < sprite_size) {
		width = 180; // Reasonable minimum width for palette
	}

	// Calculate columns based on available width
	columns = std::max(1, width / sprite_size);

	if (tileset->size() > 10000) {
		size_t chunk_start = current_chunk * chunk_size;
		size_t items_in_chunk = std::min(static_cast<size_t>(chunk_size), tileset->size() - chunk_start);
		total_rows = (items_in_chunk + columns - 1) / columns;
		int virtual_height = total_rows * sprite_size;

		if (current_chunk < total_chunks - 1) {
			virtual_height += 40;
		}

		SetVirtualSize(width, virtual_height);
	} else {
		total_rows = (tileset->size() + columns - 1) / columns;
		int virtual_height = total_rows * sprite_size;
		SetVirtualSize(width, virtual_height);
	}

	// Ensure visible rows are calculated correctly
	first_visible_row = 0;
	last_visible_row = std::min(total_rows - 1, (height / sprite_size) + visible_rows_margin);
	if(last_visible_row < 0) last_visible_row = total_rows - 1;

	UpdateViewableItems();

	if (buffer) {
		delete buffer;
		buffer = nullptr;
	}

	ManageSpriteCache();
	need_full_redraw = true;
}

void SeamlessGridPanel::OnScroll(wxScrollWinEvent& event) {
	UpdateViewableItems();

	if (loading_step < max_loading_steps && tileset->size() > 1000) {
		visible_rows_margin = 3;
		UpdateViewableItems();
		Refresh();
		StartProgressiveLoading();
	} else {
		visible_rows_margin = 30;
		UpdateViewableItems();
		Refresh();
	}

	event.Skip();
}

int SeamlessGridPanel::GetSpriteIndexAt(int x, int y) const {
	int logX, logY;
	CalcUnscrolledPosition(x, y, &logX, &logY);
	int col = logX / sprite_size;
	int row = logY / sprite_size;
	int index = row * columns + col;

	if (index >= 0 && index < static_cast<int>(tileset->size()) && col >= 0 && col < columns) {
		return index;
	}

	return -1;
}

void SeamlessGridPanel::OnMouseClick(wxMouseEvent& event) {
	int xPos, yPos;
	CalcUnscrolledPosition(event.GetX(), event.GetY(), &xPos, &yPos);
	int col = xPos / sprite_size;
	int row = yPos / sprite_size;

	if (col >= 0 && col < columns && row >= 0) {
		int index = row * columns + col;

		if (tileset->size() > 10000) {
			size_t chunk_start = current_chunk * chunk_size;
			size_t items_in_chunk = std::min(static_cast<size_t>(chunk_size), tileset->size() - chunk_start);
			if (index >= static_cast<int>(items_in_chunk)) return;
			index = chunk_start + index;
		}

		if (index >= 0 && index < static_cast<int>(tileset->size())) {
			selected_index = index;
			Refresh();

			wxWindow* w = this;
			while((w = w->GetParent()) && dynamic_cast<PaletteWindow*>(w) == nullptr);
			if(w) {
				g_gui.ActivatePalette(static_cast<PaletteWindow*>(w));
			}

			g_gui.SelectBrush(tileset->brushlist[index], tileset->getType());
		}
	}

	event.Skip();
}

void SeamlessGridPanel::OnMouseMove(wxMouseEvent& event) {
	int old_hover = hover_index;
	hover_index = GetSpriteIndexAt(event.GetX(), event.GetY());

	if (hover_index != old_hover) {
		Refresh();
	}

	event.Skip();
}

void SeamlessGridPanel::OnKeyDown(wxKeyEvent& event) {
	int keycode = event.GetKeyCode();
	int new_index = selected_index;

	switch(keycode) {
		case WXK_LEFT:
			if(selected_index > 0) new_index = selected_index - 1;
			break;
		case WXK_RIGHT:
			if(selected_index < static_cast<int>(tileset->size()) - 1) new_index = selected_index + 1;
			break;
		case WXK_UP:
			if(selected_index >= columns) new_index = selected_index - columns;
			break;
		case WXK_DOWN:
			if(selected_index + columns < static_cast<int>(tileset->size())) new_index = selected_index + columns;
			break;
		default:
			event.Skip();
			return;
	}

	if(new_index != selected_index) {
		SelectIndex(new_index);
	}

	event.Skip();
}

void SeamlessGridPanel::SelectFirstBrush() {
	if(tileset && tileset->size() > 0) {
		selected_index = 0;
		Refresh();
	}
}

Brush* SeamlessGridPanel::GetSelectedBrush() const {
	if(selected_index >= 0 && selected_index < static_cast<int>(tileset->size())) {
		return tileset->brushlist[selected_index];
	}
	if(tileset && tileset->size() > 0) {
		return tileset->brushlist[0];
	}
	return nullptr;
}

bool SeamlessGridPanel::SelectBrush(const Brush* whatbrush) {
	if(!whatbrush || !tileset) return false;

	for(size_t i = 0; i < tileset->size(); ++i) {
		if(tileset->brushlist[i] == whatbrush) {
			SelectIndex(i);
			return true;
		}
	}
	return false;
}

void SeamlessGridPanel::SelectIndex(int index) {
	if(index < 0 || index >= static_cast<int>(tileset->size())) return;

	selected_index = index;

	if (tileset->size() > 10000) {
		int target_chunk = index / chunk_size;
		if (target_chunk != current_chunk) {
			current_chunk = target_chunk;
			ClearSpriteCache();
			RecalculateGrid();
			UpdateNavigationPanel();
		}
	}

	int row = (index % chunk_size) / columns;
	int scroll_y = row * sprite_size;
	int ppuX, ppuY;
	GetScrollPixelsPerUnit(&ppuX, &ppuY);
	if(ppuY > 0) {
		Scroll(-1, scroll_y / ppuY);
	}

	Refresh();
}

int SeamlessGridPanel::IncrementZoom() {
	if(zoom_level < 4) {
		SetZoomLevel(zoom_level + 1);
	}
	return zoom_level;
}

int SeamlessGridPanel::DecrementZoom() {
	if(zoom_level > 1) {
		SetZoomLevel(zoom_level - 1);
	}
	return zoom_level;
}

void SeamlessGridPanel::SetZoomLevel(int level) {
	if(level < 1) level = 1;
	if(level > 4) level = 4;

	if(level != zoom_level) {
		zoom_level = level;
		sprite_size = 32 * zoom_level;
		ClearSpriteCache();
		SetScrollRate(sprite_size, sprite_size);
		RecalculateGrid();
		Refresh();
	}
}

void SeamlessGridPanel::UpdateGridSize() {
	sprite_size = 32 * zoom_level;
	SetScrollRate(sprite_size, sprite_size);
	RecalculateGrid();
}

void SeamlessGridPanel::CreateNavigationPanel(wxWindow* parent) {
	// Implementation would go here if needed for large tilesets
	// For now, keeping it simple
}

void SeamlessGridPanel::UpdateNavigationPanel() {
	// Implementation would go here if needed for large tilesets
}

void SeamlessGridPanel::OnNavigationButtonClicked(wxCommandEvent& event) {
	// Implementation would go here if needed for large tilesets
}
