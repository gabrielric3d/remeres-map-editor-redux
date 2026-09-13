#include "palette/panels/brush_palette_panel.h"
#include "ui/gui.h"
#include "ui/gui_ids.h"
#include "ui/add_tileset_window.h"
#include "ui/add_item_window.h"
#include "game/materials.h"
#include "palette/palette_window.h"
#include "palette/managers/palette_manager.h"
#include "util/image_manager.h"
#include "ui/theme.h"
#include <wx/menu.h>
#include <spdlog/spdlog.h>

namespace {
	// Sorting is stored as a single integer so it survives across sessions.
	int SortModeToInt(TilesetSortKey key, TilesetSortDirection dir) {
		if (key == TilesetSortKey::None) {
			return 0;
		}
		const bool ascending = (dir == TilesetSortDirection::Ascending);
		if (key == TilesetSortKey::Name) {
			return ascending ? 1 : 2;
		}
		return ascending ? 3 : 4;
	}

	void IntToSortMode(int mode, TilesetSortKey& key, TilesetSortDirection& dir) {
		switch (mode) {
			case 1:
				key = TilesetSortKey::Name;
				dir = TilesetSortDirection::Ascending;
				break;
			case 2:
				key = TilesetSortKey::Name;
				dir = TilesetSortDirection::Descending;
				break;
			case 3:
				key = TilesetSortKey::ID;
				dir = TilesetSortDirection::Ascending;
				break;
			case 4:
				key = TilesetSortKey::ID;
				dir = TilesetSortDirection::Descending;
				break;
			default:
				key = TilesetSortKey::None;
				dir = TilesetSortDirection::Ascending;
				break;
		}
	}
}

// ============================================================================
// Brush Palette Panel
// A common class for terrain/doodad/item/raw palette

BrushPalettePanel::BrushPalettePanel(wxWindow* parent, const TilesetContainer& tilesets, TilesetCategoryType category, wxWindowID id) :
	PalettePanel(parent, id),
	palette_type(category),
	choicebook(nullptr) {
	Bind(wxEVT_BUTTON, &BrushPalettePanel::OnClickAddItemToTileset, this, wxID_ADD);
	Bind(wxEVT_BUTTON, &BrushPalettePanel::OnClickAddTileset, this, wxID_NEW);
	Bind(wxEVT_CHOICEBOOK_PAGE_CHANGING, &BrushPalettePanel::OnSwitchingPage, this);
	Bind(wxEVT_CHOICEBOOK_PAGE_CHANGED, &BrushPalettePanel::OnPageChanged, this);

	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	// Create the tileset panel
	wxSizer* ts_sizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Tileset");
	wxWindow* ts_parent = static_cast<wxStaticBoxSizer*>(ts_sizer)->GetStaticBox();
	CreateTilesetToolbar(ts_sizer, ts_parent);
	wxChoicebook* tmp_choicebook = newd wxChoicebook(ts_parent, wxID_ANY, wxDefaultPosition, wxSize(180, 250));
	ts_sizer->Add(tmp_choicebook, 1, wxEXPAND);
	topsizer->Add(ts_sizer, 1, wxEXPAND);

	// Display options toolbar (icon background + slot size)
	{
		wxSizer* display_sizer = newd wxBoxSizer(wxHORIZONTAL);

		// Icon background color
		icon_bg_choice = newd wxChoice(this, PALETTE_ICON_BG_CHOICE);
		icon_bg_choice->Append("Black");
		icon_bg_choice->Append("Gray");
		icon_bg_choice->Append("White");
		int bgVal = g_settings.getInteger(Config::ICON_BACKGROUND);
		if (bgVal == 255) {
			icon_bg_choice->SetSelection(2);
		} else if (bgVal == 88) {
			icon_bg_choice->SetSelection(1);
		} else {
			icon_bg_choice->SetSelection(0);
		}
		icon_bg_choice->SetToolTip("Icon background color");
		display_sizer->Add(icon_bg_choice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

		// Slot size
		slot_size_choice = newd wxChoice(this, PALETTE_SLOT_SIZE_CHOICE);
		slot_size_choice->Append("32x32");
		slot_size_choice->Append("36x36");
		slot_size_choice->Append("42x42");
		slot_size_choice->Append("48x48");
		slot_size_choice->Append("52x52");
		slot_size_choice->Append("64x64");

		int slotSize = g_settings.getInteger(Config::PALETTE_GRID_ICON_SIZE);
		int selIdx = 1; // default 36
		if (slotSize <= 32) selIdx = 0;
		else if (slotSize <= 36) selIdx = 1;
		else if (slotSize <= 42) selIdx = 2;
		else if (slotSize <= 48) selIdx = 3;
		else if (slotSize <= 52) selIdx = 4;
		else selIdx = 5;
		slot_size_choice->SetSelection(selIdx);
		slot_size_choice->SetToolTip("Grid slot size");
		display_sizer->Add(slot_size_choice, 0, wxALIGN_CENTER_VERTICAL);

		topsizer->Add(display_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);

		Bind(wxEVT_CHOICE, &BrushPalettePanel::OnIconBackgroundChanged, this, PALETTE_ICON_BG_CHOICE);
		Bind(wxEVT_CHOICE, &BrushPalettePanel::OnSlotSizeChanged, this, PALETTE_SLOT_SIZE_CHOICE);
	}

	// Border and Doodad editors moved to Tools > Brushes Editor (menubar).

if (g_settings.getBoolean(Config::SHOW_TILESET_EDITOR)) {
		wxSizer* tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
		wxButton* buttonAddTileset = newd wxButton(this, wxID_NEW, "Add new Tileset");
		buttonAddTileset->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_PLUS, wxSize(16, 16)));
		buttonAddTileset->SetToolTip("Create a new custom tileset");
		tmpsizer->Add(buttonAddTileset, wxSizerFlags(0).Center());

		wxButton* buttonAddItemToTileset = newd wxButton(this, wxID_ADD, "Add new Item");
		buttonAddItemToTileset->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_PLUS, wxSize(16, 16)));
		buttonAddItemToTileset->SetToolTip("Add a new item to the current tileset");
		tmpsizer->Add(buttonAddItemToTileset, wxSizerFlags(0).Center());

		topsizer->Add(tmpsizer, 0, wxCENTER, 10);
	}

	for (const auto& tileset : GetSortedTilesets(tilesets)) {
		const TilesetCategory* tcg = tileset->getCategory(category);
		if (tcg && tcg->size() > 0) {
			BrushPanel* panel = newd BrushPanel(tmp_choicebook);
			panel->AssignTileset(tcg);
			tmp_choicebook->AddPage(panel, wxstr(tileset->name));
		}
	}

	SetSizerAndFit(topsizer);

	choicebook = tmp_choicebook;
	ApplyDisplayOptionsToPages();
}

BrushPalettePanel::~BrushPalettePanel() {
	////
}

void BrushPalettePanel::InvalidateContents() {
	brush_index_built = false;
	brush_page_index.clear();
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		panel->InvalidateContents();
	}
	PalettePanel::InvalidateContents();
}

void BrushPalettePanel::LoadCurrentContents() {
	if (!choicebook) {
		return;
	}
	wxWindow* page = choicebook->GetCurrentPage();
	BrushPanel* panel = dynamic_cast<BrushPanel*>(page);
	if (panel) {
		panel->OnSwitchIn();
	}
	PalettePanel::LoadCurrentContents();
}

void BrushPalettePanel::LoadAllContents() {
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		panel->LoadContents();
	}
	PalettePanel::LoadAllContents();
}

PaletteType BrushPalettePanel::GetType() const {
	return palette_type;
}

void BrushPalettePanel::SetListType(BrushListType ltype) {
	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		panel->SetListType(ltype);
	}
}

void BrushPalettePanel::SetListType(wxString ltype) {
	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		panel->SetListType(ltype);
	}
}

Brush* BrushPalettePanel::GetSelectedBrush() const {
	if (!choicebook) {
		return nullptr;
	}
	wxWindow* page = choicebook->GetCurrentPage();
	BrushPanel* panel = dynamic_cast<BrushPanel*>(page);
	Brush* res = nullptr;
	if (panel) {
		for (const auto& toolBar : tool_bars) {
			res = toolBar->GetSelectedBrush();
			if (res) {
				return res;
			}
		}
		res = panel->GetSelectedBrush();
	}
	return res;
}

void BrushPalettePanel::SelectFirstBrush() {
	if (!choicebook) {
		return;
	}
	wxWindow* page = choicebook->GetCurrentPage();
	if (!page) {
		return;
	}
	BrushPanel* panel = dynamic_cast<BrushPanel*>(page);
	if (panel) {
		panel->SelectFirstBrush();
	}
}

void BrushPalettePanel::EnsureBrushIndex() {
	if (brush_index_built || !choicebook) {
		return;
	}
	brush_index_built = true;
	brush_page_index.clear();
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (!panel || !panel->GetTileset()) {
			continue;
		}
		for (const auto* brush : panel->GetTileset()->brushlist) {
			brush_page_index[brush] = iz;
		}
	}
}

bool BrushPalettePanel::SelectBrushByOffset(int offset) {
	if (!choicebook) {
		return false;
	}
	BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	if (!panel) {
		return false;
	}
	return panel->SelectBrushByOffset(offset);
}

bool BrushPalettePanel::SelectBrush(const Brush* whatbrush) {
	if (!choicebook) {
		return false;
	}

	BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	if (!panel) {
		return false;
	}

	for (PalettePanel* toolBar : tool_bars) {
		if (toolBar->SelectBrush(whatbrush)) {
			panel->SelectBrush(nullptr);
			return true;
		}
	}

	// Try current page first
	if (panel->SelectBrush(whatbrush)) {
		for (PalettePanel* toolBar : tool_bars) {
			toolBar->SelectBrush(nullptr);
		}
		return true;
	}

	// Use index for O(1) page lookup instead of scanning all pages
	EnsureBrushIndex();
	auto indexIt = brush_page_index.find(whatbrush);
	if (indexIt != brush_page_index.end()) {
		size_t pageIdx = indexIt->second;
		if ((int)pageIdx != choicebook->GetSelection()) {
			panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(pageIdx));
			if (panel && panel->SelectBrush(whatbrush)) {
				choicebook->ChangeSelection(pageIdx);
				for (PalettePanel* toolBar : tool_bars) {
					toolBar->SelectBrush(nullptr);
				}
				return true;
			}
		}
	}
	return false;
}

void BrushPalettePanel::OnSwitchingPage(wxChoicebookEvent& event) {
	event.Skip();
}

void BrushPalettePanel::OnPageChanged(wxChoicebookEvent& event) {
	if (!choicebook) {
		return;
	}

	BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());

	// Pause preloading on tileset pages that are no longer visible.
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* other = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (other && other != panel) {
			other->OnSwitchOut();
		}
	}

	Brush* new_brush = nullptr;

	if (panel) {
		panel->OnSwitchIn();
		new_brush = panel->GetSelectedBrush();
	}

	g_gui.ActivatePalette(GetParentPalette());
	if (new_brush) {
		g_gui.SelectBrush(new_brush, palette_type);
	} else {
		g_gui.SelectBrush();
	}
	Layout();
}

void BrushPalettePanel::OnSwitchIn() {
	g_palettes.ActivatePalette(GetParentPalette());
	g_gui.RestoreBrushSizeState(last_brush_size_state);

	// Sync display option controls with current settings
	if (icon_bg_choice) {
		int bgVal = g_settings.getInteger(Config::ICON_BACKGROUND);
		int sel = (bgVal == 255) ? 2 : (bgVal == 88) ? 1 : 0;
		if (icon_bg_choice->GetSelection() != sel) {
			icon_bg_choice->SetSelection(sel);
		}
	}
	if (slot_size_choice) {
		static constexpr int sizeValues[] = { 32, 36, 42, 48, 52, 64 };
		int slotSize = g_settings.getInteger(Config::PALETTE_GRID_ICON_SIZE);
		int selIdx = 1;
		for (int i = 0; i < 6; ++i) {
			if (slotSize <= sizeValues[i]) { selIdx = i; break; }
			if (i == 5) selIdx = 5;
		}
		if (slot_size_choice->GetSelection() != selIdx) {
			slot_size_choice->SetSelection(selIdx);
		}
	}

	// Sorting and labels are shared by every palette, another one may have
	// changed them while this page was hidden.
	{
		TilesetSortKey key = sort_key;
		TilesetSortDirection dir = sort_dir;
		IntToSortMode(g_settings.getInteger(Config::PALETTE_TILESET_SORT_MODE), key, dir);
		if (key != sort_key || dir != sort_dir) {
			SetSort(key, dir);
		}

		const bool labels = g_settings.getBoolean(Config::PALETTE_TILESET_SHOW_LABELS);
		if (labels != show_labels) {
			SetShowLabels(labels);
		}
	}

	LoadCurrentContents();
}

void BrushPalettePanel::OnSwitchOut() {
	// Stop preloading on the visible tileset grid so it doesn't keep competing
	// for the UI thread while another palette page is shown.
	if (choicebook) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
		if (panel) {
			panel->OnSwitchOut();
		}
	}
	PalettePanel::OnSwitchOut();
}

void BrushPalettePanel::OnClickAddTileset(wxCommandEvent& WXUNUSED(event)) {
	if (!choicebook) {
		return;
	}

	wxDialog* w = newd AddTilesetWindow(g_gui.root, palette_type);
	int ret = w->ShowModal();
	w->Destroy();

	if (ret != 0) {
		g_gui.DestroyPalettes();
		g_gui.NewPalette();
	}
}

void BrushPalettePanel::OnClickAddItemToTileset(wxCommandEvent& WXUNUSED(event)) {
	if (!choicebook) {
		return;
	}
	int selection = choicebook->GetSelection();
	if (selection == wxNOT_FOUND) {
		return;
	}
	std::string tilesetName = choicebook->GetPageText(selection).ToStdString();

	auto _it = g_materials.tilesets.find(tilesetName);
	if (_it != g_materials.tilesets.end()) {
		wxDialog* w = newd AddItemWindow(g_gui.root, palette_type, _it->second);
		int ret = w->ShowModal();
		w->Destroy();

		if (ret != 0) {
			g_gui.RebuildPalettes();
		}
	}
}

void BrushPalettePanel::OnIconBackgroundChanged(wxCommandEvent&) {
	static constexpr int bgValues[] = { 0, 88, 255 };
	int sel = icon_bg_choice->GetSelection();
	if (sel < 0 || sel > 2) return;

	int newBg = bgValues[sel];
	if (g_settings.getInteger(Config::ICON_BACKGROUND) == newBg) return;

	g_gui.gfx.cleanSoftwareSprites();
	g_settings.setInteger(Config::ICON_BACKGROUND, newBg);

	for (auto* palette : g_palettes.GetPalettes()) {
		palette->InvalidateContents();
	}
}

void BrushPalettePanel::OnSlotSizeChanged(wxCommandEvent&) {
	static constexpr int sizeValues[] = { 32, 36, 42, 48, 52, 64 };
	int sel = slot_size_choice->GetSelection();
	if (sel < 0 || sel > 5) return;

	int newSize = sizeValues[sel];
	if (g_settings.getInteger(Config::PALETTE_GRID_ICON_SIZE) == newSize) return;

	g_settings.setInteger(Config::PALETTE_GRID_ICON_SIZE, newSize);

	for (auto* palette : g_palettes.GetPalettes()) {
		palette->InvalidateContents();
	}
}


// ============================================================================
// Tileset toolbar: search, sorting, labels and manual reordering


void BrushPalettePanel::CreateTilesetToolbar(wxSizer* ts_sizer, wxWindow* ts_parent) {
	IntToSortMode(g_settings.getInteger(Config::PALETTE_TILESET_SORT_MODE), sort_key, sort_dir);
	show_labels = g_settings.getBoolean(Config::PALETTE_TILESET_SHOW_LABELS);

	wxSizer* tools_sizer = newd wxBoxSizer(wxHORIZONTAL);

	tileset_search = newd wxTextCtrl(ts_parent, PALETTE_TILESET_SEARCH, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	tileset_search->SetHint("Search...");
	tileset_search->SetToolTip("Filter the brushes of every tileset by name");
	tileset_search->SetMinSize(FromDIP(wxSize(60, -1)));
	tools_sizer->Add(tileset_search, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);

	const wxSize iconSize = FromDIP(wxSize(16, 16));
	const wxColour iconColor = Theme::Get(Theme::Role::Text);
	const long toolbarStyle = (wxAUI_TB_DEFAULT_STYLE | wxAUI_TB_PLAIN_BACKGROUND) & ~wxAUI_TB_GRIPPER;

	tileset_toolbar = newd wxAuiToolBar(ts_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, toolbarStyle);
	tileset_toolbar->SetToolBitmapSize(iconSize);
	tileset_toolbar->SetMargins(1, 1, 1, 1);
	tileset_toolbar->SetToolBorderPadding(1);

	tileset_toolbar->AddTool(PALETTE_TILESET_SORT_AZ, wxEmptyString, IMAGE_MANAGER.GetBitmap(ICON_ARROW_DOWN_A_Z, iconSize, iconColor), "Sort ascending: A to Z, or lowest server id first");
	tileset_toolbar->AddTool(PALETTE_TILESET_SORT_ZA, wxEmptyString, IMAGE_MANAGER.GetBitmap(ICON_ARROW_DOWN_Z_A, iconSize, iconColor), "Sort descending: Z to A, or highest server id first");
	tileset_toolbar->AddTool(PALETTE_TILESET_SORT_OPTIONS, wxEmptyString, IMAGE_MANAGER.GetBitmap(ICON_LIST, iconSize, iconColor), "Sorting options");
	tileset_toolbar->AddTool(PALETTE_TILESET_TOGGLE_LABELS, wxEmptyString, IMAGE_MANAGER.GetBitmap(ICON_TAG, iconSize, iconColor), "Show the brush names under the icons", wxITEM_CHECK);
	tileset_toolbar->AddTool(PALETTE_TILESET_REORDER, wxEmptyString, IMAGE_MANAGER.GetBitmap(ICON_ARROWS_UP_DOWN_LEFT_RIGHT, iconSize, iconColor), "Reorder the tileset by dragging its brushes", wxITEM_CHECK);
	tileset_toolbar->Realize();

	tools_sizer->Add(tileset_toolbar, 0, wxALIGN_CENTER_VERTICAL);
	ts_sizer->Add(tools_sizer, 0, wxEXPAND | wxBOTTOM, 2);

	tileset_toolbar->Bind(wxEVT_TOOL, &BrushPalettePanel::OnTilesetToolClick, this);
	tileset_search->Bind(wxEVT_TEXT, &BrushPalettePanel::OnSearchText, this);
	tileset_search->Bind(wxEVT_CHAR_HOOK, &BrushPalettePanel::OnSearchCharHook, this);
	Bind(wxEVT_MENU, &BrushPalettePanel::OnSortOptionMenu, this, PALETTE_TILESET_SORT_BY_NAME, PALETTE_TILESET_RESET_ORDER);

	UpdateToolbarState();
}

void BrushPalettePanel::UpdateToolbarState() {
	if (!tileset_toolbar) {
		return;
	}
	tileset_toolbar->ToggleTool(PALETTE_TILESET_TOGGLE_LABELS, show_labels);
	tileset_toolbar->ToggleTool(PALETTE_TILESET_REORDER, reorder_mode);
	tileset_toolbar->Refresh();
}

void BrushPalettePanel::ApplyDisplayOptionsToPages() {
	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (!panel) {
			continue;
		}
		panel->SetShowLabels(show_labels);
		panel->SetSort(sort_key, sort_dir);
		panel->SetReorderMode(reorder_mode);
	}
}

void BrushPalettePanel::SetSort(TilesetSortKey key, TilesetSortDirection dir) {
	sort_key = key;
	sort_dir = dir;
	g_settings.setInteger(Config::PALETTE_TILESET_SORT_MODE, SortModeToInt(key, dir));

	// A sorted view no longer matches the stored order, so stop reordering.
	if (reorder_mode && key != TilesetSortKey::None) {
		reorder_mode = false;
		UpdateToolbarState();
	}

	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			panel->SetSort(key, dir);
			panel->SetReorderMode(reorder_mode);
		}
	}
}

void BrushPalettePanel::SetShowLabels(bool show) {
	show_labels = show;
	g_settings.setInteger(Config::PALETTE_TILESET_SHOW_LABELS, show ? 1 : 0);
	UpdateToolbarState();

	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			panel->SetShowLabels(show);
		}
	}
}

void BrushPalettePanel::SetReorderMode(bool enabled) {
	reorder_mode = enabled;

	// Dragging can only reorder while the grid shows the plain tileset order.
	if (enabled) {
		if (sort_key != TilesetSortKey::None) {
			SetSort(TilesetSortKey::None, TilesetSortDirection::Ascending);
			reorder_mode = true; // SetSort turned it off, we are enabling it on purpose
		}
		if (tileset_search && !tileset_search->GetValue().IsEmpty()) {
			tileset_search->ChangeValue(wxEmptyString);
			ApplyFilter("");
		}
	}

	UpdateToolbarState();

	if (!choicebook) {
		return;
	}
	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			panel->SetReorderMode(reorder_mode);
		}
	}
}

void BrushPalettePanel::ApplyFilter(const std::string& filter) {
	if (!choicebook) {
		return;
	}

	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel) {
			panel->SetFilter(filter);
		}
	}

	if (filter.empty()) {
		return;
	}

	// Jump to the first tileset that still has a match, so typing drills down.
	BrushPanel* current = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	if (current && current->HasVisibleBrushes()) {
		return;
	}

	for (size_t iz = 0; iz < choicebook->GetPageCount(); ++iz) {
		BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetPage(iz));
		if (panel && panel->HasVisibleBrushes()) {
			// ChangeSelection doesn't fire the page events, so hand the pages
			// over by hand and keep the hidden grid from preloading textures.
			if (current && current != panel) {
				current->OnSwitchOut();
			}
			choicebook->ChangeSelection(iz);
			panel->OnSwitchIn();
			break;
		}
	}
}

void BrushPalettePanel::OnTilesetToolClick(wxCommandEvent& event) {
	switch (event.GetId()) {
		case PALETTE_TILESET_SORT_AZ:
			SetSort(sort_key == TilesetSortKey::ID ? TilesetSortKey::ID : TilesetSortKey::Name, TilesetSortDirection::Ascending);
			break;
		case PALETTE_TILESET_SORT_ZA:
			SetSort(sort_key == TilesetSortKey::ID ? TilesetSortKey::ID : TilesetSortKey::Name, TilesetSortDirection::Descending);
			break;
		case PALETTE_TILESET_TOGGLE_LABELS:
			SetShowLabels(event.IsChecked());
			break;
		case PALETTE_TILESET_REORDER:
			SetReorderMode(event.IsChecked());
			break;
		case PALETTE_TILESET_SORT_OPTIONS: {
			wxMenu menu;
			menu.AppendRadioItem(PALETTE_TILESET_SORT_OFF, "Tileset order")->Check(sort_key == TilesetSortKey::None);
			menu.AppendRadioItem(PALETTE_TILESET_SORT_BY_NAME, "Sort by name")->Check(sort_key == TilesetSortKey::Name);
			menu.AppendRadioItem(PALETTE_TILESET_SORT_BY_ID, "Sort by server id")->Check(sort_key == TilesetSortKey::ID);
			menu.AppendSeparator();
			menu.Append(PALETTE_TILESET_RESET_ORDER, "Reset order of this tileset");
			PopupMenu(&menu);
			break;
		}
		default:
			break;
	}
}

void BrushPalettePanel::OnSortOptionMenu(wxCommandEvent& event) {
	switch (event.GetId()) {
		case PALETTE_TILESET_SORT_OFF:
			SetSort(TilesetSortKey::None, sort_dir);
			break;
		case PALETTE_TILESET_SORT_BY_NAME:
			SetSort(TilesetSortKey::Name, sort_dir);
			break;
		case PALETTE_TILESET_SORT_BY_ID:
			SetSort(TilesetSortKey::ID, sort_dir);
			break;
		case PALETTE_TILESET_RESET_ORDER: {
			if (!choicebook) {
				break;
			}
			BrushPanel* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
			if (panel) {
				panel->ResetCustomOrder();
			}
			break;
		}
		default:
			break;
	}
}

void BrushPalettePanel::OnSearchText(wxCommandEvent& event) {
	if (tileset_search) {
		ApplyFilter(nstr(tileset_search->GetValue()));
	}
	event.Skip();
}

void BrushPalettePanel::OnSearchCharHook(wxKeyEvent& event) {
	const int keycode = event.GetKeyCode();

	if (keycode == WXK_ESCAPE) {
		if (tileset_search && !tileset_search->GetValue().IsEmpty()) {
			tileset_search->ChangeValue(wxEmptyString);
			ApplyFilter("");
			return;
		}
		event.Skip();
		return;
	}

	// Navigation and editing keys behave normally
	if (keycode == WXK_RETURN || keycode == WXK_NUMPAD_ENTER || keycode == WXK_TAB ||
		keycode == WXK_UP || keycode == WXK_DOWN || keycode == WXK_LEFT || keycode == WXK_RIGHT ||
		keycode == WXK_HOME || keycode == WXK_END || keycode == WXK_DELETE || keycode == WXK_BACK ||
		keycode == WXK_PAGEUP || keycode == WXK_PAGEDOWN) {
		event.Skip();
		return;
	}

	if (event.ControlDown()) {
		event.Skip();
		return;
	}

	// Printable characters are written by hand so the editor hotkeys cannot
	// swallow them while a filter is being typed.
	if (keycode >= WXK_SPACE && keycode <= 255) {
		const wxChar ch = event.GetUnicodeKey();
		if (ch != WXK_NONE && tileset_search) {
			tileset_search->WriteText(wxString(ch));
		}
		return;
	}

	event.Skip();
}
