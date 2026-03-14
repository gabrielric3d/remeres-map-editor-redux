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

#include "palette/palette_creature.h"
#include "palette/panels/brush_panel.h"
#include "brushes/creature/creature_brush.h"
#include "game/creatures.h"

#include "app/settings.h"
#include "brushes/brush.h"
#include "ui/gui.h"
#include "brushes/managers/brush_manager.h"
#include "brushes/spawn/spawn_brush.h"
#include "game/materials.h"
#include "util/image_manager.h"

#include <algorithm>
#include <sstream>
#include <wx/numdlg.h>

// ============================================================================
// Creature palette

CreaturePalettePanel::CreaturePalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id),
	handling_event(false),
	prefer_favorite_for_group_add(false) {
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	// Search filter
	creature_filter_text = newd wxTextCtrl(this, PALETTE_CREATURE_FILTER, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	creature_filter_text->SetHint("Search...");
	topsizer->Add(creature_filter_text, 0, wxEXPAND | wxALL, 4);

	// List view toggle
	creature_preview_checkbox = newd wxCheckBox(this, PALETTE_CREATURE_PREVIEW_TOGGLE, "List View");
	creature_preview_checkbox->SetValue(g_settings.getInteger(Config::PALETTE_CREATURE_STYLE) == BRUSHLIST_LISTBOX);
	topsizer->Add(creature_preview_checkbox, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);

	choicebook = newd wxChoicebook(this, wxID_ANY);
	topsizer->Add(choicebook, 1, wxEXPAND);

	// Favorites setup
	wxSizer* favorites_sizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Favorite Creatures");
	favorite_creature_list = newd wxListBox(this, PALETTE_CREATURE_FAVORITES_LIST, wxDefaultPosition, wxSize(-1, 95), 0, nullptr, wxLB_SINGLE);
	favorites_sizer->Add(favorite_creature_list, 1, wxEXPAND | wxBOTTOM, 5);

	wxSizer* favorites_buttons = newd wxBoxSizer(wxHORIZONTAL);
	favorite_creature_add_button = newd wxButton(this, PALETTE_CREATURE_FAVORITES_ADD, "Add Selected");
	favorite_creature_remove_button = newd wxButton(this, PALETTE_CREATURE_FAVORITES_REMOVE, "Remove");
	favorites_buttons->Add(favorite_creature_add_button, 1, wxRIGHT, 5);
	favorites_buttons->Add(favorite_creature_remove_button, 1);
	favorites_sizer->Add(favorites_buttons, 0, wxEXPAND);
	topsizer->Add(favorites_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	// Spawn group setup
	wxSizer* group_sizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Spawn Group");
	wxSizer* group_controls = newd wxBoxSizer(wxHORIZONTAL);
	group_controls->Add(newd wxStaticText(this, wxID_ANY, "Count"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	spawn_group_count_spin = newd wxSpinCtrl(this, PALETTE_CREATURE_GROUP_COUNT, i2ws(1), wxDefaultPosition, wxSize(50, 20), wxSP_ARROW_KEYS, 1, 100, 1);
	group_controls->Add(spawn_group_count_spin, 0, wxRIGHT, 5);
	spawn_group_add_button = newd wxButton(this, PALETTE_CREATURE_GROUP_ADD, "Add");
	group_controls->Add(spawn_group_add_button, 0);
	group_sizer->Add(group_controls, 0, wxEXPAND | wxBOTTOM, 5);

	spawn_group_list = newd wxListBox(this, PALETTE_CREATURE_GROUP_LIST, wxDefaultPosition, wxSize(-1, 90), 0, nullptr, wxLB_SINGLE);
	group_sizer->Add(spawn_group_list, 1, wxEXPAND | wxBOTTOM, 5);

	wxSizer* group_buttons = newd wxBoxSizer(wxHORIZONTAL);
	spawn_group_remove_button = newd wxButton(this, PALETTE_CREATURE_GROUP_REMOVE, "Remove");
	spawn_group_clear_button = newd wxButton(this, PALETTE_CREATURE_GROUP_CLEAR, "Clear");
	group_buttons->Add(spawn_group_remove_button, 0, wxRIGHT, 5);
	group_buttons->Add(spawn_group_clear_button, 0);
	group_sizer->Add(group_buttons, 0, wxEXPAND);

	topsizer->Add(group_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	// Footer for brushes and settings
	wxSizer* sidesizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Brushes");

	wxFlexGridSizer* grid = newd wxFlexGridSizer(3, 10, 10);
	grid->AddGrowableCol(1);

	grid->Add(newd wxStaticText(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), wxID_ANY, "Spawntime"));
	creature_spawntime_spin = newd wxSpinCtrl(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_CREATURE_SPAWN_TIME, i2ws(g_settings.getInteger(Config::DEFAULT_SPAWNTIME)), wxDefaultPosition, FROM_DIP(this, wxSize(64, 20)), wxSP_ARROW_KEYS, 0, 86400, g_settings.getInteger(Config::DEFAULT_SPAWNTIME));
	creature_spawntime_spin->SetToolTip("Spawn time (seconds)");
	grid->Add(creature_spawntime_spin, 0, wxEXPAND);
	creature_brush_button = newd wxToggleButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_CREATURE_BRUSH_BUTTON, "Place Creature");
	creature_brush_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_DRAGON, wxSize(16, 16)));
	creature_brush_button->SetToolTip("Place Creature");
	grid->Add(creature_brush_button, 0, wxEXPAND);

	grid->Add(newd wxStaticText(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), wxID_ANY, "Spawn size"));
	spawn_size_spin = newd wxSpinCtrl(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_CREATURE_SPAWN_SIZE, i2ws(5), wxDefaultPosition, FROM_DIP(this, wxSize(64, 20)), wxSP_ARROW_KEYS, 1, g_settings.getInteger(Config::MAX_SPAWN_RADIUS), g_settings.getInteger(Config::CURRENT_SPAWN_RADIUS));
	spawn_size_spin->SetToolTip("Spawn radius");
	grid->Add(spawn_size_spin, 0, wxEXPAND);
	spawn_brush_button = newd wxToggleButton(static_cast<wxStaticBoxSizer*>(sidesizer)->GetStaticBox(), PALETTE_SPAWN_BRUSH_BUTTON, "Place Spawn");
	spawn_brush_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_FIRE, wxSize(16, 16)));
	spawn_brush_button->SetToolTip("Place Spawn");
	grid->Add(spawn_brush_button, 0, wxEXPAND);

	sidesizer->Add(grid, 0, wxEXPAND);
	topsizer->Add(sidesizer, 0, wxEXPAND);

	SetSizerAndFit(topsizer);

	Bind(wxEVT_CHOICEBOOK_PAGE_CHANGING, &CreaturePalettePanel::OnSwitchingPage, this);
	Bind(wxEVT_CHOICEBOOK_PAGE_CHANGED, &CreaturePalettePanel::OnPageChanged, this);

	Bind(wxEVT_TOGGLEBUTTON, &CreaturePalettePanel::OnClickCreatureBrushButton, this, PALETTE_CREATURE_BRUSH_BUTTON);
	Bind(wxEVT_TOGGLEBUTTON, &CreaturePalettePanel::OnClickSpawnBrushButton, this, PALETTE_SPAWN_BRUSH_BUTTON);

	Bind(wxEVT_SPINCTRL, &CreaturePalettePanel::OnChangeSpawnTime, this, PALETTE_CREATURE_SPAWN_TIME);
	Bind(wxEVT_SPINCTRL, &CreaturePalettePanel::OnChangeSpawnSize, this, PALETTE_CREATURE_SPAWN_SIZE);

	// Search and preview events
	Bind(wxEVT_TEXT, &CreaturePalettePanel::OnFilterTextChange, this, PALETTE_CREATURE_FILTER);
	creature_filter_text->Bind(wxEVT_CHAR_HOOK, &CreaturePalettePanel::OnFilterCharHook, this);
	Bind(wxEVT_CHECKBOX, &CreaturePalettePanel::OnTogglePreview, this, PALETTE_CREATURE_PREVIEW_TOGGLE);

	// Favorite creatures events
	Bind(wxEVT_LISTBOX, &CreaturePalettePanel::OnFavoriteListChange, this, PALETTE_CREATURE_FAVORITES_LIST);
	Bind(wxEVT_LISTBOX_DCLICK, &CreaturePalettePanel::OnFavoriteListDoubleClick, this, PALETTE_CREATURE_FAVORITES_LIST);
	Bind(wxEVT_BUTTON, &CreaturePalettePanel::OnClickFavoriteAdd, this, PALETTE_CREATURE_FAVORITES_ADD);
	Bind(wxEVT_BUTTON, &CreaturePalettePanel::OnClickFavoriteRemove, this, PALETTE_CREATURE_FAVORITES_REMOVE);

	// Spawn group events
	Bind(wxEVT_LISTBOX, &CreaturePalettePanel::OnGroupListChange, this, PALETTE_CREATURE_GROUP_LIST);
	Bind(wxEVT_LISTBOX_DCLICK, &CreaturePalettePanel::OnGroupListDoubleClick, this, PALETTE_CREATURE_GROUP_LIST);
	Bind(wxEVT_BUTTON, &CreaturePalettePanel::OnClickGroupAdd, this, PALETTE_CREATURE_GROUP_ADD);
	Bind(wxEVT_BUTTON, &CreaturePalettePanel::OnClickGroupRemove, this, PALETTE_CREATURE_GROUP_REMOVE);
	Bind(wxEVT_BUTTON, &CreaturePalettePanel::OnClickGroupClear, this, PALETTE_CREATURE_GROUP_CLEAR);

	OnUpdate();

	// Restore spawn group from brush manager
	{
		const auto& group = g_brush_manager.GetSpawnCreatureGroup();
		spawn_group.clear();
		spawn_group.reserve(group.size());
		for (const auto& entry : group) {
			spawn_group.push_back({ entry.name, entry.count });
		}
	}
	UpdateSpawnGroupList();
	LoadFavoriteCreaturesFromSettings();
	UpdateFavoriteList();
}

PaletteType CreaturePalettePanel::GetType() const {
	return TILESET_CREATURE;
}

void CreaturePalettePanel::SelectFirstBrush() {
	SelectCreatureBrush();
}

Brush* CreaturePalettePanel::GetSelectedBrush() const {
	if (creature_brush_button->GetValue()) {
		if (choicebook->GetPageCount() == 0) {
			return nullptr;
		}
		BrushPanel* bp = reinterpret_cast<BrushPanel*>(choicebook->GetCurrentPage());
		Brush* brush = bp->GetSelectedBrush();
		if (brush && brush->is<CreatureBrush>()) {
			g_brush_manager.SetSpawnTime(creature_spawntime_spin->GetValue());
			return brush;
		}
	} else if (spawn_brush_button->GetValue()) {
		g_settings.setInteger(Config::CURRENT_SPAWN_RADIUS, spawn_size_spin->GetValue());
		g_settings.setInteger(Config::DEFAULT_SPAWNTIME, creature_spawntime_spin->GetValue());
		return g_brush_manager.spawn_brush;
	}
	return nullptr;
}

bool CreaturePalettePanel::SelectBrush(const Brush* whatbrush) {
	if (!whatbrush) {
		return false;
	}

	if (whatbrush->is<CreatureBrush>()) {
		for (size_t i = 0; i < choicebook->GetPageCount(); ++i) {
			BrushPanel* bp = reinterpret_cast<BrushPanel*>(choicebook->GetPage(i));
			if (bp->SelectBrush(whatbrush)) {
				if (choicebook->GetSelection() != i) {
					choicebook->SetSelection(i);
				}
				SelectCreatureBrush();
				return true;
			}
		}
	} else if (whatbrush->is<SpawnBrush>()) {
		SelectSpawnBrush();
		return true;
	}
	return false;
}

int CreaturePalettePanel::GetSelectedBrushSize() const {
	return spawn_size_spin->GetValue();
}

void CreaturePalettePanel::OnUpdate() {
	choicebook->DeleteAllPages();
	g_materials.createOtherTileset();

	const BrushListType ltype = (BrushListType)g_settings.getInteger(Config::PALETTE_CREATURE_STYLE);

	for (const auto& tileset : GetSortedTilesets(g_materials.tilesets)) {
		const TilesetCategory* tsc = tileset->getCategory(TILESET_CREATURE);
		if ((tsc && tsc->size() > 0) || tileset->name == "NPCs" || tileset->name == "Others") {
			BrushPanel* bp = newd BrushPanel(choicebook);
			bp->SetListType(ltype);
			bp->AssignTileset(tsc);
			bp->LoadContents();
			choicebook->AddPage(bp, wxstr(tileset->name));
		}
	}
	if (choicebook->GetPageCount() > 0) {
		choicebook->SetSelection(0);
	}
}

void CreaturePalettePanel::OnUpdateBrushSize(BrushShape shape, int size) {
	return spawn_size_spin->SetValue(size);
}

void CreaturePalettePanel::OnSwitchIn() {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetBrushSize(spawn_size_spin->GetValue());
}

void CreaturePalettePanel::SelectTileset(size_t index) {
	if (choicebook->GetPageCount() > index) {
		choicebook->SetSelection(index);
	}
}

void CreaturePalettePanel::OnRefreshTilesets() {
	OnUpdate();
}

void CreaturePalettePanel::SetListType(BrushListType ltype) {
	for (size_t i = 0; i < choicebook->GetPageCount(); ++i) {
		reinterpret_cast<BrushPanel*>(choicebook->GetPage(i))->SetListType(ltype);
	}
}

void CreaturePalettePanel::SetListType(wxString ltype) {
	if (ltype == "Icons") {
		SetListType(BRUSHLIST_LARGE_ICONS);
	} else if (ltype == "List") {
		SetListType(BRUSHLIST_LISTBOX);
	}
}

void CreaturePalettePanel::SelectCreature(size_t index) {
	if (choicebook->GetPageCount() > 0) {
		BrushPanel* bp = reinterpret_cast<BrushPanel*>(choicebook->GetPage(choicebook->GetSelection()));
		bp->SelectFirstBrush();
		SelectCreatureBrush();
	}
}

void CreaturePalettePanel::SelectCreature(std::string name) {
	if (CreatureType* ct = g_creatures[name]) {
		if (ct->brush) {
			SelectBrush(ct->brush);
		}
	}
}

void CreaturePalettePanel::SelectCreatureBrush() {
	if (choicebook->GetPageCount() > 0) {
		creature_brush_button->Enable(true);
		creature_brush_button->SetValue(true);
		spawn_brush_button->SetValue(false);
	} else {
		creature_brush_button->Enable(false);
		SelectSpawnBrush();
	}
}

void CreaturePalettePanel::SelectSpawnBrush() {
	creature_brush_button->SetValue(false);
	spawn_brush_button->SetValue(true);
}

void CreaturePalettePanel::OnSwitchingPage(wxChoicebookEvent& event) {
	// Do nothing
}

void CreaturePalettePanel::OnPageChanged(wxChoicebookEvent& event) {
	SelectCreatureBrush();
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnClickCreatureBrushButton(wxCommandEvent& event) {
	SelectCreatureBrush();
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnClickSpawnBrushButton(wxCommandEvent& event) {
	SelectSpawnBrush();
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnChangeSpawnTime(wxSpinEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetSpawnTime(event.GetPosition());
}

void CreaturePalettePanel::OnChangeSpawnSize(wxSpinEvent& event) {
	if (!handling_event) {
		handling_event = true;
		g_gui.ActivatePalette(GetParentPalette());
		g_gui.SetBrushSize(event.GetPosition());
		handling_event = false;
	}
}

// ============================================================================
// Search and Preview

void CreaturePalettePanel::OnFilterTextChange(wxCommandEvent& WXUNUSED(event)) {
	std::string filter = nstr(creature_filter_text->GetValue());
	ApplyFilterToAllPages(filter);
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnFilterCharHook(wxKeyEvent& event) {
	int keycode = event.GetKeyCode();

	// For special keys (navigation, escape, enter, tab, etc.), let them propagate normally
	if (keycode == WXK_ESCAPE || keycode == WXK_RETURN || keycode == WXK_NUMPAD_ENTER ||
		keycode == WXK_TAB || keycode == WXK_UP || keycode == WXK_DOWN ||
		keycode == WXK_LEFT || keycode == WXK_RIGHT || keycode == WXK_HOME ||
		keycode == WXK_END || keycode == WXK_DELETE || keycode == WXK_BACK ||
		keycode == WXK_PAGEUP || keycode == WXK_PAGEDOWN) {
		event.Skip();
		return;
	}

	// For Ctrl+key shortcuts (Ctrl+A, Ctrl+C, Ctrl+V, Ctrl+X, Ctrl+Z), let them propagate
	if (event.ControlDown()) {
		event.Skip();
		return;
	}

	// For all other keys (printable characters), consume the event to prevent
	// the PaletteWindow's wxChoicebook from changing tabs, then insert the
	// character into the text control manually
	if (keycode >= WXK_SPACE && keycode <= 255) {
		wxChar ch = event.GetUnicodeKey();
		if (ch != WXK_NONE) {
			creature_filter_text->WriteText(wxString(ch));
		}
		// Don't call event.Skip() - consume the event
		return;
	}

	event.Skip();
}

void CreaturePalettePanel::OnTogglePreview(wxCommandEvent& WXUNUSED(event)) {
	bool list_view = creature_preview_checkbox->GetValue();
	BrushListType ltype = list_view ? BRUSHLIST_LISTBOX : BRUSHLIST_LARGE_ICONS;
	g_settings.setInteger(Config::PALETTE_CREATURE_STYLE, static_cast<int>(ltype));
	for (size_t i = 0; i < choicebook->GetPageCount(); ++i) {
		BrushPanel* bp = reinterpret_cast<BrushPanel*>(choicebook->GetPage(i));
		bp->SetListType(ltype);
		bp->LoadContents();
		// Re-apply current filter after reload
		std::string filter = nstr(creature_filter_text->GetValue());
		if (!filter.empty()) {
			bp->SetFilter(filter);
		}
	}
}

void CreaturePalettePanel::ApplyFilterToAllPages(const std::string& filter) {
	for (size_t i = 0; i < choicebook->GetPageCount(); ++i) {
		BrushPanel* bp = reinterpret_cast<BrushPanel*>(choicebook->GetPage(i));
		bp->SetFilter(filter);
	}
}

// ============================================================================
// Favorite Creatures

void CreaturePalettePanel::OnFavoriteListChange(wxCommandEvent& WXUNUSED(event)) {
	const int selection = favorite_creature_list->GetSelection();
	prefer_favorite_for_group_add = (selection != wxNOT_FOUND);
	favorite_creature_remove_button->Enable(selection != wxNOT_FOUND);
}

void CreaturePalettePanel::OnFavoriteListDoubleClick(wxCommandEvent& event) {
	int selection = event.GetSelection();
	if (selection == wxNOT_FOUND) {
		selection = favorite_creature_list->GetSelection();
	}
	if (selection == wxNOT_FOUND) {
		return;
	}
	if (selection < 0 || selection >= static_cast<int>(favorite_creatures.size())) {
		return;
	}

	prefer_favorite_for_group_add = true;
	const std::string& name = favorite_creatures[selection];
	CreatureType* creature_type = g_creatures[name];
	if (creature_type && creature_type->brush) {
		SelectBrush(creature_type->brush);
	} else {
		SelectCreature(name);
	}

	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}

void CreaturePalettePanel::OnClickFavoriteAdd(wxCommandEvent& WXUNUSED(event)) {
	if (choicebook->GetPageCount() == 0) {
		return;
	}
	BrushPanel* bp = reinterpret_cast<BrushPanel*>(choicebook->GetCurrentPage());
	Brush* brush = bp->GetSelectedBrush();
	if (!brush || !brush->is<CreatureBrush>()) {
		return;
	}

	AddFavoriteCreature(brush->getName());
}

void CreaturePalettePanel::OnClickFavoriteRemove(wxCommandEvent& WXUNUSED(event)) {
	const int selection = favorite_creature_list->GetSelection();
	if (selection == wxNOT_FOUND) {
		return;
	}
	if (selection < 0 || selection >= static_cast<int>(favorite_creatures.size())) {
		return;
	}

	favorite_creatures.erase(favorite_creatures.begin() + selection);
	SaveFavoriteCreaturesToSettings();
	std::string preferred;
	if (!favorite_creatures.empty()) {
		const size_t next_index = std::min(static_cast<size_t>(selection), favorite_creatures.size() - 1);
		preferred = favorite_creatures[next_index];
	}
	UpdateFavoriteList(preferred);
}

void CreaturePalettePanel::LoadFavoriteCreaturesFromSettings() {
	favorite_creatures.clear();
	std::string raw = g_settings.getString(Config::CREATURE_FAVORITES);

	// Support both ';' (new format) and '\n' (legacy format) as separators
	std::string entry;
	for (size_t i = 0; i <= raw.size(); ++i) {
		char ch = (i < raw.size()) ? raw[i] : ';';
		if (ch == ';' || ch == '\n') {
			// Trim whitespace
			entry.erase(0, entry.find_first_not_of(" \t\r\n"));
			auto pos = entry.find_last_not_of(" \t\r\n");
			if (pos != std::string::npos) {
				entry.erase(pos + 1);
			} else {
				entry.clear();
			}
			if (!entry.empty()) {
				const std::string normalized = as_lower_str(entry);
				auto it = std::find_if(favorite_creatures.begin(), favorite_creatures.end(), [&](const std::string& existing) {
					return as_lower_str(existing) == normalized;
				});
				if (it == favorite_creatures.end()) {
					favorite_creatures.push_back(entry);
				}
			}
			entry.clear();
		} else {
			entry += ch;
		}
	}

	std::sort(favorite_creatures.begin(), favorite_creatures.end(), [](const std::string& lhs, const std::string& rhs) {
		return as_lower_str(lhs) < as_lower_str(rhs);
	});
}

void CreaturePalettePanel::SaveFavoriteCreaturesToSettings() const {
	std::ostringstream stream;
	for (size_t i = 0; i < favorite_creatures.size(); ++i) {
		if (i != 0) {
			stream << ';';
		}
		stream << favorite_creatures[i];
	}
	g_settings.setString(Config::CREATURE_FAVORITES, stream.str());
}

void CreaturePalettePanel::UpdateFavoriteList(const std::string& preferred_selection) {
	favorite_creature_list->Clear();
	int selection = wxNOT_FOUND;
	for (size_t i = 0; i < favorite_creatures.size(); ++i) {
		favorite_creature_list->Append(wxstr(favorite_creatures[i]));
		if (!preferred_selection.empty() && favorite_creatures[i] == preferred_selection) {
			selection = static_cast<int>(i);
		}
	}

	if (selection != wxNOT_FOUND) {
		favorite_creature_list->SetSelection(selection);
	}

	favorite_creature_remove_button->Enable(favorite_creature_list->GetSelection() != wxNOT_FOUND);
}

void CreaturePalettePanel::AddFavoriteCreature(const std::string& name) {
	if (name.empty()) {
		return;
	}

	const std::string normalized = as_lower_str(name);
	for (size_t i = 0; i < favorite_creatures.size(); ++i) {
		if (as_lower_str(favorite_creatures[i]) == normalized) {
			UpdateFavoriteList(favorite_creatures[i]);
			return;
		}
	}

	favorite_creatures.push_back(name);
	std::sort(favorite_creatures.begin(), favorite_creatures.end(), [](const std::string& lhs, const std::string& rhs) {
		return as_lower_str(lhs) < as_lower_str(rhs);
	});
	SaveFavoriteCreaturesToSettings();
	UpdateFavoriteList(name);
}

// ============================================================================
// Spawn Group

void CreaturePalettePanel::OnClickGroupAdd(wxCommandEvent& WXUNUSED(event)) {
	std::string name;
	if (prefer_favorite_for_group_add) {
		const int selection = favorite_creature_list->GetSelection();
		if (selection >= 0 && selection < static_cast<int>(favorite_creatures.size())) {
			name = favorite_creatures[selection];
		}
	}

	if (name.empty()) {
		if (choicebook->GetPageCount() == 0) {
			return;
		}
		BrushPanel* bp = reinterpret_cast<BrushPanel*>(choicebook->GetCurrentPage());
		Brush* brush = bp->GetSelectedBrush();
		if (!brush || !brush->is<CreatureBrush>()) {
			return;
		}
		name = brush->getName();
	}

	const int count = spawn_group_count_spin->GetValue();
	if (count <= 0) {
		return;
	}

	auto it = std::find_if(spawn_group.begin(), spawn_group.end(), [&](const SpawnGroupEntry& entry) {
		return entry.name == name;
	});
	if (it != spawn_group.end()) {
		it->count += count;
	} else {
		spawn_group.push_back({ name, count });
	}

	SyncSpawnGroupToGUI();
	UpdateSpawnGroupList();
}

void CreaturePalettePanel::OnClickGroupRemove(wxCommandEvent& WXUNUSED(event)) {
	const int selection = spawn_group_list->GetSelection();
	if (selection == wxNOT_FOUND) {
		return;
	}

	if (selection >= 0 && selection < static_cast<int>(spawn_group.size())) {
		spawn_group.erase(spawn_group.begin() + selection);
	}

	SyncSpawnGroupToGUI();
	UpdateSpawnGroupList();
}

void CreaturePalettePanel::OnClickGroupClear(wxCommandEvent& WXUNUSED(event)) {
	if (spawn_group.empty()) {
		return;
	}

	spawn_group.clear();
	SyncSpawnGroupToGUI();
	UpdateSpawnGroupList();
}

void CreaturePalettePanel::OnGroupListChange(wxCommandEvent& WXUNUSED(event)) {
	spawn_group_remove_button->Enable(spawn_group_list->GetSelection() != wxNOT_FOUND);
}

void CreaturePalettePanel::OnGroupListDoubleClick(wxCommandEvent& event) {
	int selection = event.GetSelection();
	if (selection == wxNOT_FOUND) {
		selection = spawn_group_list->GetSelection();
	}
	if (selection == wxNOT_FOUND) {
		return;
	}

	if (selection < 0 || selection >= static_cast<int>(spawn_group.size())) {
		return;
	}

	SpawnGroupEntry& entry = spawn_group[selection];
	wxString message;
	message << "Set creature count for \"" << wxstr(entry.name) << "\"";
	wxNumberEntryDialog dialog(
		this,
		message,
		"Count",
		"Edit Spawn Group Count",
		entry.count,
		1,
		1000000);

	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	entry.count = static_cast<int>(dialog.GetValue());
	SyncSpawnGroupToGUI();
	UpdateSpawnGroupList();
	spawn_group_list->SetSelection(selection);
	spawn_group_remove_button->Enable(true);
}

void CreaturePalettePanel::SyncSpawnGroupToGUI() {
	std::vector<SpawnCreatureEntry> group;
	group.reserve(spawn_group.size());
	for (const auto& entry : spawn_group) {
		group.push_back({ entry.name, entry.count });
	}
	g_brush_manager.SetSpawnCreatureGroup(group);
}

void CreaturePalettePanel::UpdateSpawnGroupList() {
	spawn_group_list->Clear();
	for (const auto& entry : spawn_group) {
		wxString label;
		label << wxstr(entry.name) << " x" << entry.count;
		spawn_group_list->Append(label);
	}

	spawn_group_remove_button->Enable(spawn_group_list->GetSelection() != wxNOT_FOUND);
	spawn_group_clear_button->Enable(!spawn_group.empty());
}
