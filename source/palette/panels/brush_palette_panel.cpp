#include "palette/panels/brush_palette_panel.h"
#include "ui/gui.h"
#include "ui/gui_ids.h"
#include "ui/add_tileset_window.h"
#include "ui/add_item_window.h"
#include "ui/dialogs/border_editor_dialog.h"
#include "ui/dialogs/doodad_editor_dialog.h"
#include "game/materials.h"
#include "palette/palette_window.h"
#include "palette/managers/palette_manager.h"
#include "util/image_manager.h"
#include <spdlog/spdlog.h>

// ============================================================================
// Brush Palette Panel
// A common class for terrain/doodad/item/raw palette

BrushPalettePanel::BrushPalettePanel(wxWindow* parent, const TilesetContainer& tilesets, TilesetCategoryType category, wxWindowID id) :
	PalettePanel(parent, id),
	palette_type(category),
	choicebook(nullptr) {
	Bind(wxEVT_BUTTON, &BrushPalettePanel::OnClickAddItemToTileset, this, wxID_ADD);
	Bind(wxEVT_BUTTON, &BrushPalettePanel::OnClickAddTileset, this, wxID_NEW);
	Bind(wxEVT_BUTTON, &BrushPalettePanel::OnClickCreateBorder, this, PALETTE_TERRAIN_CREATE_BORDER);
	Bind(wxEVT_BUTTON, &BrushPalettePanel::OnClickEditDoodad, this, PALETTE_DOODAD_EDIT_DOODAD);
	Bind(wxEVT_CHOICEBOOK_PAGE_CHANGING, &BrushPalettePanel::OnSwitchingPage, this);
	Bind(wxEVT_CHOICEBOOK_PAGE_CHANGED, &BrushPalettePanel::OnPageChanged, this);

	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	// Create the tileset panel
	wxSizer* ts_sizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Tileset");
	wxChoicebook* tmp_choicebook = newd wxChoicebook(static_cast<wxStaticBoxSizer*>(ts_sizer)->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxSize(180, 250));
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

	if (category == TILESET_TERRAIN) {
		wxButton* createBorderButton = newd wxButton(this, PALETTE_TERRAIN_CREATE_BORDER, "Create Border");
		createBorderButton->SetToolTip("Open the Border Editor to create or edit auto-borders");
		topsizer->Add(createBorderButton, 0, wxEXPAND | wxALL, 5);
	}

	if (category == TILESET_DOODAD) {
		wxButton* editDoodadButton = newd wxButton(this, PALETTE_DOODAD_EDIT_DOODAD, "Doodad Editor");
		editDoodadButton->SetToolTip("Open the Doodad Brush Editor to view and create doodad brushes");
		topsizer->Add(editDoodadButton, 0, wxEXPAND | wxALL, 5);
	}

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

	LoadCurrentContents();
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

void BrushPalettePanel::OnClickCreateBorder(wxCommandEvent& WXUNUSED(event)) {
	BorderEditorDialog* dialog = newd BorderEditorDialog(g_gui.root, "Auto Border Editor");
	dialog->Show();
	g_gui.RefreshView();
}

void BrushPalettePanel::OnClickEditDoodad(wxCommandEvent& WXUNUSED(event)) {
	DoodadEditorDialog* dialog = newd DoodadEditorDialog(g_gui.root, "Doodad Brush Editor");
	dialog->Show();
}
