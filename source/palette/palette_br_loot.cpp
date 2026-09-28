//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// ============================================================================
// Battle Royale loot palettes (zones + items placed by hand)

#include "app/main.h"

#include "palette/palette_br_loot.h"

#include "app/settings.h"
#include "brushes/br_loot/br_loot_item_brush.h"
#include "brushes/br_loot/br_loot_zone_brush.h"
#include "brushes/managers/brush_manager.h"
#include "editor/editor.h"
#include "game/br_loot_catalog.h"
#include "game/br_loot_zones.h"
#include "item_definitions/core/item_definition_store.h"
#include "map/map.h"
#include "map/tile.h"
#include "map/tile_operations.h"
#include "rendering/core/graphics.h"
#include "ui/gui.h"
#include "ui/main_frame.h"
#include "ui/main_menubar.h"

#include <wx/dcmemory.h>
#include <wx/imaglist.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>

#include <algorithm>

namespace {
	constexpr int ICON_SIZE = 32;
	// The tiers the tooling seeds (rules.json). Offered even without the catalog, so
	// zones can be painted before the generator ever ran.
	constexpr int DEFAULT_TIERS = 3;

	wxString tierText(int tier) {
		const std::string label = g_br_loot_catalog.tierLabel(tier);
		if (label.empty()) {
			return wxString::Format("Tier %d", tier);
		}
		return wxString::Format("%d - ", tier) + wxstr(label);
	}

	wxString zoneText(const BRLootZone& zone) {
		// "3-231" is how the HUB names a zone: tier, then the id that never changes.
		return wxString::Format("%d-%u   ", zone.tier, static_cast<unsigned>(zone.id)) + tierText(zone.tier);
	}

	wxString placedText(const BRLootItem& item) {
		wxString text = wxstr(item.name);
		if (item.max > item.min) {
			text << wxString::Format("  x%d-%d", item.min, item.max);
		} else if (item.min > 1) {
			text << wxString::Format("  x%d", item.min);
		}
		if (item.chance < 100) {
			text << wxString::Format("  %d%%", item.chance);
		}
		text << wxString::Format("   (%d, %d, %d)", item.pos.x, item.pos.y, item.pos.z);
		return text;
	}

	wxBitmap makeItemBitmap(uint16_t server_id) {
		wxBitmap bitmap(ICON_SIZE, ICON_SIZE, 32);
		wxMemoryDC dc(bitmap);
		dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX)));
		dc.Clear();

		Sprite* sprite = nullptr;
		if (server_id != 0) {
			if (const auto definition = g_item_definitions.get(server_id)) {
				sprite = g_gui.gfx.getSprite(definition.clientId());
			}
		}
		if (sprite) {
			sprite->DrawTo(&dc, SPRITE_SIZE_32x32, 0, 0, ICON_SIZE, ICON_SIZE);
		} else {
			// Not in this client's items: a hollow box, so the row still lines up.
			dc.SetPen(*wxGREY_PEN);
			dc.SetBrush(*wxTRANSPARENT_BRUSH);
			dc.DrawRectangle(6, 6, ICON_SIZE - 12, ICON_SIZE - 12);
		}
		dc.SelectObject(wxNullBitmap);
		return bitmap;
	}

	void syncViewMenu() {
		if (g_gui.root) {
			if (MainMenuBar* menu = g_gui.root->GetMainMenuBar()) {
				menu->LoadValues();
			}
		}
	}
} // namespace

// ============================================================================
// Zones

BRLootZonePalettePanel::BRLootZonePalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id),
	map(nullptr),
	catalog_version(0) {
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* sidesizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Loot Zones");
	wxWindow* box = sidesizer->GetStaticBox();

	wxSizer* tiersizer = newd wxBoxSizer(wxHORIZONTAL);
	tiersizer->Add(newd wxStaticText(box, wxID_ANY, "Tier:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	tier_choice = newd wxChoice(box, PALETTE_BRLOOT_ZONE_TIER);
	tier_choice->SetToolTip("Tier of the zones created with New, and the tier Set tier gives to the selected zone.");
	tiersizer->Add(tier_choice, 1, wxEXPAND);
	sidesizer->Add(tiersizer, 0, wxEXPAND | wxBOTTOM, 4);

	zone_list = newd wxListBox(box, PALETTE_BRLOOT_ZONE_LIST, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	sidesizer->Add(zone_list, 1, wxEXPAND);

	wxSizer* buttons = newd wxBoxSizer(wxHORIZONTAL);
	new_button = newd wxButton(box, PALETTE_BRLOOT_ZONE_NEW, "New", wxDefaultPosition, wxSize(40, -1));
	new_button->SetToolTip("Create a zone with the tier above and start painting it.");
	buttons->Add(new_button, 1, wxEXPAND);
	tier_button = newd wxButton(box, PALETTE_BRLOOT_ZONE_SET_TIER, "Set tier", wxDefaultPosition, wxSize(56, -1));
	tier_button->SetToolTip("Give the selected zone the tier above.");
	buttons->Add(tier_button, 1, wxEXPAND);
	delete_button = newd wxButton(box, PALETTE_BRLOOT_ZONE_DELETE, "Delete", wxDefaultPosition, wxSize(50, -1));
	delete_button->SetToolTip("Delete the selected zone and take its tiles out of it (Ctrl+Z brings both back).");
	buttons->Add(delete_button, 1, wxEXPAND);
	goto_button = newd wxButton(box, PALETTE_BRLOOT_ZONE_GOTO, "Go to", wxDefaultPosition, wxSize(44, -1));
	goto_button->SetToolTip("Center the view on the selected zone.");
	buttons->Add(goto_button, 1, wxEXPAND);
	sidesizer->Add(buttons, 0, wxEXPAND | wxTOP, 2);

	show_toggle = newd wxCheckBox(box, PALETTE_BRLOOT_TOGGLE_SHOW, "Show loot zones on map");
	show_toggle->SetToolTip("Tint every zone with the color of its tier, outline it and write its id. Same as View > Show loot zones (BR).");
	show_toggle->SetValue(g_settings.getBoolean(Config::SHOW_BR_LOOT_ZONES));
	sidesizer->Add(show_toggle, 0, wxEXPAND | wxTOP, 4);

	solid_toggle = newd wxCheckBox(box, PALETTE_BRLOOT_TOGGLE_SOLID, "Paint zones solid (hides terrain)");
	solid_toggle->SetToolTip("Fill each zone with a solid block of its tier color, to check what is covered. The selection and the brush preview underneath stop showing.");
	solid_toggle->SetValue(g_settings.getBoolean(Config::BR_LOOT_ZONE_SOLID_FILL));
	sidesizer->Add(solid_toggle, 0, wxEXPAND | wxTOP, 2);

	topsizer->Add(sidesizer, 1, wxEXPAND);

	summary = newd wxStaticText(this, wxID_ANY, wxEmptyString);
	topsizer->Add(summary, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);

	auto* hint = newd wxStaticText(this, wxID_ANY,
		"Pick a zone in the list, or on the map: Ctrl+Alt+click a tile (the Smart Brush picker), "
		"right-click it > Select Loot Zone, or -- with no zone picked -- just click it. Then paint to add "
		"tiles; Ctrl+click takes them out, and painting over another zone moves those tiles to this one. "
		"Colors: tier 1 green, 2 blue, 3 orange. "
		"Saved to <map>-brloot.json with the map; a zone with no painted tile is not written.");
	hint->SetForegroundColour(wxColour(128, 128, 128));
	hint->Wrap(220);
	topsizer->Add(hint, 0, wxEXPAND | wxALL, 4);

	SetSizerAndFit(topsizer);

	Bind(wxEVT_LISTBOX, &BRLootZonePalettePanel::OnClickZone, this, PALETTE_BRLOOT_ZONE_LIST);
	Bind(wxEVT_LISTBOX_DCLICK, &BRLootZonePalettePanel::OnDoubleClickZone, this, PALETTE_BRLOOT_ZONE_LIST);
	Bind(wxEVT_BUTTON, &BRLootZonePalettePanel::OnClickNew, this, PALETTE_BRLOOT_ZONE_NEW);
	Bind(wxEVT_BUTTON, &BRLootZonePalettePanel::OnClickSetTier, this, PALETTE_BRLOOT_ZONE_SET_TIER);
	Bind(wxEVT_BUTTON, &BRLootZonePalettePanel::OnClickDelete, this, PALETTE_BRLOOT_ZONE_DELETE);
	Bind(wxEVT_BUTTON, &BRLootZonePalettePanel::OnClickGoto, this, PALETTE_BRLOOT_ZONE_GOTO);
	Bind(wxEVT_CHECKBOX, &BRLootZonePalettePanel::OnToggleShow, this, PALETTE_BRLOOT_TOGGLE_SHOW);
	Bind(wxEVT_CHECKBOX, &BRLootZonePalettePanel::OnToggleSolid, this, PALETTE_BRLOOT_TOGGLE_SOLID);

	UpdateTierChoices();
}

wxString BRLootZonePalettePanel::GetName() const {
	return "BR Loot Zones";
}

PaletteType BRLootZonePalettePanel::GetType() const {
	return TILESET_BR_LOOT_ZONE;
}

void BRLootZonePalettePanel::SelectFirstBrush() {
	//
}

int BRLootZonePalettePanel::GetSelectedBrushSize() const {
	return 0;
}

Brush* BRLootZonePalettePanel::GetSelectedBrush() const {
	g_brush_manager.br_loot_zone_brush->setZone(GetSelectedZoneId());
	return g_brush_manager.br_loot_zone_brush;
}

bool BRLootZonePalettePanel::SelectBrush(const Brush* whatbrush) {
	return whatbrush == g_brush_manager.br_loot_zone_brush;
}

void BRLootZonePalettePanel::SetMap(Map* m) {
	map = m;
	Enable(m != nullptr);
	UpdateList(GetSelectedZoneId());
}

void BRLootZonePalettePanel::OnSwitchIn() {
	PalettePanel::OnSwitchIn();
	// The View menu can flip these behind our back.
	show_toggle->SetValue(g_settings.getBoolean(Config::SHOW_BR_LOOT_ZONES));
	solid_toggle->SetValue(g_settings.getBoolean(Config::BR_LOOT_ZONE_SOLID_FILL));
	g_br_loot_catalog.refresh(true);
	OnUpdate();
}

void BRLootZonePalettePanel::OnUpdate() {
	g_br_loot_catalog.refresh();
	if (catalog_version != g_br_loot_catalog.getVersion()) {
		UpdateTierChoices();
		row_texts.clear(); // the tier names in the rows may have changed
	}
	UpdateList(GetSelectedZoneId());
}

Editor* BRLootZonePalettePanel::GetEditor() const {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || !map || &editor->map != map) {
		return nullptr;
	}
	return editor;
}

uint32_t BRLootZonePalettePanel::GetSelectedZoneId() const {
	const int sel = zone_list->GetSelection();
	if (sel == wxNOT_FOUND || sel < 0 || static_cast<size_t>(sel) >= row_ids.size()) {
		return 0;
	}
	return row_ids[sel];
}

int BRLootZonePalettePanel::GetChosenTier() const {
	const int sel = tier_choice->GetSelection();
	if (sel == wxNOT_FOUND || sel < 0 || static_cast<size_t>(sel) >= tier_values.size()) {
		return 1;
	}
	return tier_values[sel];
}

void BRLootZonePalettePanel::ChooseTier(int tier) {
	for (size_t i = 0; i < tier_values.size(); ++i) {
		if (tier_values[i] == tier) {
			tier_choice->SetSelection(static_cast<int>(i));
			return;
		}
	}
}

void BRLootZonePalettePanel::UpdateTierChoices() {
	const int chosen = tier_values.empty() ? 1 : GetChosenTier();
	catalog_version = g_br_loot_catalog.getVersion();

	// The catalog's tiers, the three the tooling seeds, and any tier a zone of this
	// map already uses -- a zone must never have a tier the list cannot show.
	std::vector<int> tiers;
	for (const auto& tier : g_br_loot_catalog.getTiers()) {
		tiers.push_back(tier.tier);
	}
	for (int tier = 1; tier <= DEFAULT_TIERS; ++tier) {
		tiers.push_back(tier);
	}
	if (map) {
		for (const BRLootZone* zone : map->br_loot_zones.getOrdered()) {
			tiers.push_back(zone->tier);
		}
	}
	std::sort(tiers.begin(), tiers.end());
	tiers.erase(std::unique(tiers.begin(), tiers.end()), tiers.end());

	tier_values = tiers;
	tier_choice->Clear();
	for (int tier : tier_values) {
		tier_choice->Append(tierText(tier));
	}
	ChooseTier(chosen);
	if (tier_choice->GetSelection() == wxNOT_FOUND && !tier_values.empty()) {
		tier_choice->SetSelection(0);
	}
}

void BRLootZonePalettePanel::UpdateList(uint32_t select_id) {
	std::vector<uint32_t> ids;
	std::vector<wxString> texts;
	bool unknown_tier = false;
	if (map) {
		for (const BRLootZone* zone : map->br_loot_zones.getOrdered()) {
			ids.push_back(zone->id);
			texts.push_back(zoneText(*zone));
			if (std::find(tier_values.begin(), tier_values.end(), zone->tier) == tier_values.end()) {
				unknown_tier = true;
			}
		}
	}
	if (unknown_tier) {
		UpdateTierChoices();
	}

	// Rebuilding a list of hundreds of rows on every refresh would reset its scroll
	// each time a zone is painted; only rebuild when a row actually changed.
	if (texts != row_texts) {
		zone_list->Freeze();
		zone_list->Clear();
		for (const wxString& text : texts) {
			zone_list->Append(text);
		}
		zone_list->Thaw();
		row_ids = std::move(ids);
		row_texts = std::move(texts);
	}

	int select_row = wxNOT_FOUND;
	for (size_t i = 0; i < row_ids.size(); ++i) {
		if (row_ids[i] == select_id) {
			select_row = static_cast<int>(i);
			break;
		}
	}
	if (select_row != wxNOT_FOUND) {
		if (zone_list->GetSelection() != select_row) {
			zone_list->SetSelection(select_row);
			// A zone picked on the map can be any of hundreds of rows.
			zone_list->EnsureVisible(select_row);
		}
	} else if (zone_list->GetSelection() != wxNOT_FOUND) {
		zone_list->SetSelection(wxNOT_FOUND);
	}

	// A zone that left the table (Delete, or undoing New) leaves the brush too: the
	// hover turns red instead of promising a stroke the draw would refuse.
	BRLootZoneBrush* brush = g_brush_manager.br_loot_zone_brush;
	if (map && brush && brush->getZone() != 0 && !map->br_loot_zones.getZone(brush->getZone())) {
		brush->setZone(0);
	}
	UpdateSummary();
}

void BRLootZonePalettePanel::UpdateSummary() {
	if (!map) {
		summary_text.clear();
		summary->SetLabel(wxEmptyString);
		return;
	}
	wxString text = wxString::Format("%u zone(s), %u item(s) placed by hand",
		static_cast<unsigned>(map->br_loot_zones.getOrdered().size()),
		static_cast<unsigned>(map->br_loot_zones.getItems().size()));
	const std::string& report = map->br_loot_zones.getLastReport();
	if (!report.empty()) {
		text << "\n" << wxstr(report);
	}
	if (summary_text != text) {
		summary_text = text;
		summary->SetLabel(text);
		summary->Wrap(220);
		Layout();
	}
}

bool BRLootZonePalettePanel::PickZone(uint32_t zone_id) {
	const BRLootZone* zone = map ? map->br_loot_zones.getZone(zone_id) : nullptr;
	if (!zone) {
		return false;
	}
	const int tier = zone->tier;
	UpdateList(zone_id);
	if (GetSelectedZoneId() != zone_id) {
		return false;
	}
	ChooseTier(tier);
	// The pick may have just turned the overlay on (BrushSelector::SelectBRLootZone).
	show_toggle->SetValue(g_settings.getBoolean(Config::SHOW_BR_LOOT_ZONES));
	g_brush_manager.br_loot_zone_brush->setZone(zone_id);
	return true;
}

void BRLootZonePalettePanel::OnClickZone(wxCommandEvent& WXUNUSED(event)) {
	const uint32_t id = GetSelectedZoneId();
	if (map) {
		if (const BRLootZone* zone = map->br_loot_zones.getZone(id)) {
			ChooseTier(zone->tier);
		}
	}
	// Configure the brush, then the no-arg SelectBrush(): it asks this palette for
	// the brush, which is what sets the zone (the two-arg form would not).
	g_brush_manager.br_loot_zone_brush->setZone(id);
	g_gui.SelectBrush();
}

void BRLootZonePalettePanel::OnDoubleClickZone(wxCommandEvent& WXUNUSED(event)) {
	wxCommandEvent dummy;
	OnClickGoto(dummy);
}

void BRLootZonePalettePanel::OnClickNew(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = GetEditor();
	if (!editor) {
		return;
	}
	BRLootZonesState state = map->br_loot_zones.snapshot();
	BRLootZone zone;
	zone.id = map->br_loot_zones.allocateId();
	zone.tier = GetChosenTier();
	const uint32_t id = zone.id;
	state.zones.emplace(id, std::move(zone));
	editor->ApplyBRLootState(state);

	UpdateList(id);
	g_brush_manager.br_loot_zone_brush->setZone(id);
	g_gui.SelectBrush();
}

void BRLootZonePalettePanel::OnClickSetTier(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = GetEditor();
	const uint32_t id = GetSelectedZoneId();
	if (!editor || id == 0) {
		return;
	}
	BRLootZonesState state = map->br_loot_zones.snapshot();
	const auto it = state.zones.find(id);
	const int tier = GetChosenTier();
	if (it == state.zones.end() || it->second.tier == tier) {
		return;
	}
	it->second.tier = tier;
	editor->ApplyBRLootState(state);
	UpdateList(id);
}

void BRLootZonePalettePanel::OnClickDelete(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = GetEditor();
	const uint32_t id = GetSelectedZoneId();
	if (!editor || id == 0) {
		return;
	}
	const BRLootZone* zone = map->br_loot_zones.getZone(id);
	if (!zone) {
		return;
	}

	// The paint goes with the zone, in the SAME undo step. A zone removed from the
	// table but still painted would come back as an unknown id at the next save.
	std::vector<std::unique_ptr<Tile>> tiles;
	for (MapIterator it = map->begin(); it != map->end(); ++it) {
		Tile* tile = (*it).get();
		if (tile && tile->getBRLootZoneId() == id) {
			std::unique_ptr<Tile> copy = TileOperations::deepCopy(tile, *map);
			copy->setBRLootZoneId(0);
			tiles.push_back(std::move(copy));
		}
	}

	wxString question;
	question << "Delete loot zone " << wxString::Format("%d-%u", zone->tier, static_cast<unsigned>(id)) << "?\n\n"
			 << static_cast<unsigned long>(tiles.size()) << " painted tile(s) leave the zone with it.\n"
			 << "Ctrl+Z brings the zone and its tiles back.";
	if (wxMessageBox(question, "Delete Loot Zone", wxYES_NO | wxICON_QUESTION | wxCENTER, this) != wxYES) {
		return;
	}

	BRLootZonesState state = map->br_loot_zones.snapshot();
	state.zones.erase(id);
	editor->ApplyBRLootState(state, std::move(tiles), ACTION_CHANGE_PROPERTIES);
	UpdateList(0);
}

void BRLootZonePalettePanel::OnClickGoto(wxCommandEvent& WXUNUSED(event)) {
	const uint32_t id = GetSelectedZoneId();
	if (!map || id == 0) {
		return;
	}
	// A fresh box: the incremental one only grows, and erased tiles would pull the
	// view to where the zone used to be.
	map->br_loot_zones.recalculateBounds(id);
	const BRLootZone* zone = map->br_loot_zones.getZone(id);
	if (!zone || zone->bounds_per_floor.empty()) {
		g_gui.SetStatusText("This zone has no painted tile yet.");
		return;
	}

	int floor = g_gui.GetCurrentFloor();
	const ZoneBounds* bounds = zone->boundsForFloor(floor);
	if (!bounds) {
		// Not on this floor: the floor where it has the widest box.
		int best_area = -1;
		for (const auto& entry : zone->bounds_per_floor) {
			if (!entry.second.seeded) {
				continue;
			}
			const int area = (entry.second.max_x - entry.second.min_x + 1) * (entry.second.max_y - entry.second.min_y + 1);
			if (area > best_area) {
				best_area = area;
				floor = entry.first;
				bounds = &entry.second;
			}
		}
	}
	if (!bounds) {
		return;
	}
	g_gui.SetScreenCenterPosition(Position((bounds->min_x + bounds->max_x) / 2, (bounds->min_y + bounds->max_y) / 2, floor));
	g_gui.RefreshView();
}

void BRLootZonePalettePanel::OnToggleShow(wxCommandEvent& WXUNUSED(event)) {
	g_settings.setInteger(Config::SHOW_BR_LOOT_ZONES, show_toggle->GetValue() ? 1 : 0);
	syncViewMenu();
	g_gui.RefreshView();
}

void BRLootZonePalettePanel::OnToggleSolid(wxCommandEvent& WXUNUSED(event)) {
	g_settings.setInteger(Config::BR_LOOT_ZONE_SOLID_FILL, solid_toggle->GetValue() ? 1 : 0);
	syncViewMenu();
	g_gui.RefreshView();
}

// ============================================================================
// Items placed by hand

BRLootItemPalettePanel::BRLootItemPalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id),
	map(nullptr),
	catalog_version(0) {
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* itemsizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Loot Items");
	wxWindow* itembox = itemsizer->GetStaticBox();

	item_list = newd wxListCtrl(itembox, PALETTE_BRLOOT_ITEM_LIST, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
	item_list->InsertColumn(0, wxEmptyString, wxLIST_FORMAT_LEFT, 190);
	itemsizer->Add(item_list, 3, wxEXPAND);

	catalog_note = newd wxStaticText(itembox, wxID_ANY,
		"No loot catalog for this client. Run python scripts/make_loot_zones.py in the battle royale workspace.");
	catalog_note->SetForegroundColour(wxColour(200, 80, 60));
	catalog_note->Wrap(200);
	itemsizer->Add(catalog_note, 0, wxEXPAND | wxTOP, 2);

	auto* amounts = newd wxFlexGridSizer(3, 2, 2, 4);
	amounts->AddGrowableCol(1, 1);
	amounts->Add(newd wxStaticText(itembox, wxID_ANY, "Min:"), 0, wxALIGN_CENTER_VERTICAL);
	min_spin = newd wxSpinCtrl(itembox, PALETTE_BRLOOT_ITEM_MIN, wxEmptyString, wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 1, 100, 1);
	amounts->Add(min_spin, 1, wxEXPAND);
	amounts->Add(newd wxStaticText(itembox, wxID_ANY, "Max:"), 0, wxALIGN_CENTER_VERTICAL);
	max_spin = newd wxSpinCtrl(itembox, PALETTE_BRLOOT_ITEM_MAX, wxEmptyString, wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 1, 100, 1);
	amounts->Add(max_spin, 1, wxEXPAND);
	amounts->Add(newd wxStaticText(itembox, wxID_ANY, "Chance %:"), 0, wxALIGN_CENTER_VERTICAL);
	chance_spin = newd wxSpinCtrl(itembox, PALETTE_BRLOOT_ITEM_CHANCE, wxEmptyString, wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 1, 100, 100);
	amounts->Add(chance_spin, 1, wxEXPAND);
	itemsizer->Add(amounts, 0, wxEXPAND | wxTOP, 4);

	topsizer->Add(itemsizer, 3, wxEXPAND);

	wxStaticBoxSizer* placedsizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Placed On This Map");
	wxWindow* placedbox = placedsizer->GetStaticBox();
	placed_list = newd wxListBox(placedbox, PALETTE_BRLOOT_PLACED_LIST, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxLB_SINGLE);
	placedsizer->Add(placed_list, 1, wxEXPAND);
	wxSizer* buttons = newd wxBoxSizer(wxHORIZONTAL);
	goto_button = newd wxButton(placedbox, PALETTE_BRLOOT_PLACED_GOTO, "Go to", wxDefaultPosition, wxSize(50, -1));
	buttons->Add(goto_button, 1, wxEXPAND);
	remove_button = newd wxButton(placedbox, PALETTE_BRLOOT_PLACED_REMOVE, "Remove", wxDefaultPosition, wxSize(60, -1));
	buttons->Add(remove_button, 1, wxEXPAND);
	placedsizer->Add(buttons, 0, wxEXPAND | wxTOP, 2);
	topsizer->Add(placedsizer, 2, wxEXPAND | wxTOP, 4);

	auto* hint = newd wxStaticText(this, wxID_ANY,
		"Pick an item and click a tile to place it; clicking again replaces it, Ctrl+click takes it off. "
		"Ctrl+Alt+click a placed item (or right-click > Select Loot Item) loads it into the brush. "
		"The server rolls it every match. Saved to <map>-brloot.json with the map.");
	hint->SetForegroundColour(wxColour(128, 128, 128));
	hint->Wrap(220);
	topsizer->Add(hint, 0, wxEXPAND | wxALL, 4);

	SetSizerAndFit(topsizer);

	Bind(wxEVT_LIST_ITEM_SELECTED, &BRLootItemPalettePanel::OnSelectItem, this, PALETTE_BRLOOT_ITEM_LIST);
	Bind(wxEVT_SPINCTRL, &BRLootItemPalettePanel::OnChangeAmount, this, PALETTE_BRLOOT_ITEM_MIN);
	Bind(wxEVT_SPINCTRL, &BRLootItemPalettePanel::OnChangeAmount, this, PALETTE_BRLOOT_ITEM_MAX);
	Bind(wxEVT_SPINCTRL, &BRLootItemPalettePanel::OnChangeAmount, this, PALETTE_BRLOOT_ITEM_CHANCE);
	Bind(wxEVT_LISTBOX, &BRLootItemPalettePanel::OnClickPlaced, this, PALETTE_BRLOOT_PLACED_LIST);
	Bind(wxEVT_LISTBOX_DCLICK, &BRLootItemPalettePanel::OnDoubleClickPlaced, this, PALETTE_BRLOOT_PLACED_LIST);
	Bind(wxEVT_BUTTON, &BRLootItemPalettePanel::OnClickGoto, this, PALETTE_BRLOOT_PLACED_GOTO);
	Bind(wxEVT_BUTTON, &BRLootItemPalettePanel::OnClickRemove, this, PALETTE_BRLOOT_PLACED_REMOVE);
}

wxString BRLootItemPalettePanel::GetName() const {
	return "BR Loot Items";
}

PaletteType BRLootItemPalettePanel::GetType() const {
	return TILESET_BR_LOOT_ITEM;
}

void BRLootItemPalettePanel::SelectFirstBrush() {
	//
}

int BRLootItemPalettePanel::GetSelectedBrushSize() const {
	return 0;
}

Brush* BRLootItemPalettePanel::GetSelectedBrush() const {
	ApplyToBrush();
	return g_brush_manager.br_loot_item_brush;
}

bool BRLootItemPalettePanel::SelectBrush(const Brush* whatbrush) {
	return whatbrush == g_brush_manager.br_loot_item_brush;
}

void BRLootItemPalettePanel::SetMap(Map* m) {
	map = m;
	Enable(m != nullptr);
	UpdatePlaced();
}

void BRLootItemPalettePanel::OnSwitchIn() {
	PalettePanel::OnSwitchIn();
	g_br_loot_catalog.refresh(true);
	OnUpdate();
}

void BRLootItemPalettePanel::OnUpdate() {
	g_br_loot_catalog.refresh();
	if (catalog_version != g_br_loot_catalog.getVersion() || (item_names.empty() && g_br_loot_catalog.isLoaded())) {
		UpdateCatalog();
	}
	UpdatePlaced();
}

Editor* BRLootItemPalettePanel::GetEditor() const {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || !map || &editor->map != map) {
		return nullptr;
	}
	return editor;
}

void BRLootItemPalettePanel::UpdateCatalog() {
	catalog_version = g_br_loot_catalog.getVersion();

	std::string selected;
	const long sel = item_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel >= 0 && static_cast<size_t>(sel) < item_names.size()) {
		selected = item_names[sel];
	}

	item_list->Freeze();
	item_list->DeleteAllItems();
	item_names.clear();

	auto* images = newd wxImageList(ICON_SIZE, ICON_SIZE, true);
	for (const auto& entry : g_br_loot_catalog.getEntries()) {
		const int image = images->Add(makeItemBitmap(entry.server_id));
		const long row = item_list->InsertItem(static_cast<long>(item_names.size()), wxstr(entry.name), image);
		item_list->SetItemData(row, static_cast<long>(item_names.size()));
		item_names.push_back(entry.name);
	}
	// The list owns the images from here on (and frees the previous set).
	item_list->AssignImageList(images, wxIMAGE_LIST_SMALL);
	item_list->Thaw();

	catalog_note->Show(!g_br_loot_catalog.isLoaded());
	Layout();

	if (!selected.empty()) {
		SelectItemByName(selected);
	}
}

bool BRLootItemPalettePanel::SelectItemByName(const std::string& name) {
	for (size_t i = 0; i < item_names.size(); ++i) {
		if (item_names[i] == name) {
			const long row = static_cast<long>(i);
			item_list->SetItemState(row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			item_list->EnsureVisible(row);
			return true;
		}
	}
	return false;
}

void BRLootItemPalettePanel::UpdatePlaced() {
	std::vector<Position> positions;
	std::vector<wxString> texts;
	if (map) {
		std::vector<const BRLootItem*> items;
		for (const BRLootItem& item : map->br_loot_zones.getItems()) {
			items.push_back(&item);
		}
		std::sort(items.begin(), items.end(), [](const BRLootItem* a, const BRLootItem* b) {
			if (a->name != b->name) {
				return a->name < b->name;
			}
			return a->pos < b->pos;
		});
		for (const BRLootItem* item : items) {
			positions.push_back(item->pos);
			texts.push_back(placedText(*item));
		}
	}

	Position selected;
	const int row = GetSelectedPlacedRow();
	const bool had_selection = row != wxNOT_FOUND;
	if (had_selection) {
		selected = placed_positions[row];
	}

	if (texts != placed_texts) {
		placed_list->Freeze();
		placed_list->Clear();
		for (const wxString& text : texts) {
			placed_list->Append(text);
		}
		placed_list->Thaw();
		placed_positions = std::move(positions);
		placed_texts = std::move(texts);
		if (had_selection) {
			for (size_t i = 0; i < placed_positions.size(); ++i) {
				if (placed_positions[i] == selected) {
					placed_list->SetSelection(static_cast<int>(i));
					break;
				}
			}
		}
	}
}

int BRLootItemPalettePanel::GetSelectedPlacedRow() const {
	const int sel = placed_list->GetSelection();
	if (sel == wxNOT_FOUND || sel < 0 || static_cast<size_t>(sel) >= placed_positions.size()) {
		return wxNOT_FOUND;
	}
	return sel;
}

void BRLootItemPalettePanel::ApplyToBrush() const {
	BRLootItemBrush* brush = g_brush_manager.br_loot_item_brush;
	const long sel = item_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel >= 0 && static_cast<size_t>(sel) < item_names.size()) {
		const std::string& name = item_names[sel];
		brush->setItem(name, g_br_loot_catalog.serverIdOf(name));
	} else {
		brush->setItem(std::string(), 0);
	}
	const int min_count = min_spin->GetValue();
	const int max_count = std::max(min_count, max_spin->GetValue());
	brush->setAmounts(min_count, max_count, chance_spin->GetValue());
}

void BRLootItemPalettePanel::OnSelectItem(wxListEvent& WXUNUSED(event)) {
	ApplyToBrush();
	g_gui.SelectBrush();
}

void BRLootItemPalettePanel::OnChangeAmount(wxSpinEvent& event) {
	// Max never below min: moving one drags the other along.
	if (event.GetId() == PALETTE_BRLOOT_ITEM_MIN && max_spin->GetValue() < min_spin->GetValue()) {
		max_spin->SetValue(min_spin->GetValue());
	} else if (event.GetId() == PALETTE_BRLOOT_ITEM_MAX && min_spin->GetValue() > max_spin->GetValue()) {
		min_spin->SetValue(max_spin->GetValue());
	}
	ApplyToBrush();
}

void BRLootItemPalettePanel::OnClickPlaced(wxCommandEvent& WXUNUSED(event)) {
	if (LoadPlaced(GetSelectedPlacedRow())) {
		g_gui.SelectBrush();
	}
}

bool BRLootItemPalettePanel::LoadPlaced(int row) {
	// Load that item into the brush: clicking its tile again rewrites it with
	// whatever is changed here -- that is how a placed item is edited.
	if (!map || row == wxNOT_FOUND) {
		return false;
	}
	const BRLootItem* found = map->br_loot_zones.itemAt(placed_positions[row]);
	if (!found) {
		return false;
	}
	// A copy: selecting the row below fires the list's own selection handler.
	const BRLootItem item = *found;
	if (!SelectItemByName(item.name)) {
		// Not in this client's catalog: no item picked, or the next click on that tile
		// would rewrite it with whatever item the list had selected before.
		for (long sel = item_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED); sel != -1;
			 sel = item_list->GetNextItem(sel, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) {
			item_list->SetItemState(sel, 0, wxLIST_STATE_SELECTED);
		}
	}
	min_spin->SetValue(item.min);
	max_spin->SetValue(item.max);
	chance_spin->SetValue(item.chance);
	ApplyToBrush();
	return true;
}

bool BRLootItemPalettePanel::PickPlaced(const Position& pos) {
	if (!map || !map->br_loot_zones.itemAt(pos)) {
		return false;
	}
	UpdatePlaced();
	for (size_t i = 0; i < placed_positions.size(); ++i) {
		if (placed_positions[i] == pos) {
			const int row = static_cast<int>(i);
			placed_list->SetSelection(row);
			placed_list->EnsureVisible(row);
			return LoadPlaced(row);
		}
	}
	return false;
}

void BRLootItemPalettePanel::OnDoubleClickPlaced(wxCommandEvent& WXUNUSED(event)) {
	wxCommandEvent dummy;
	OnClickGoto(dummy);
}

void BRLootItemPalettePanel::OnClickGoto(wxCommandEvent& WXUNUSED(event)) {
	const int row = GetSelectedPlacedRow();
	if (row == wxNOT_FOUND) {
		return;
	}
	g_gui.SetScreenCenterPosition(placed_positions[row]);
	g_gui.RefreshView();
}

void BRLootItemPalettePanel::OnClickRemove(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = GetEditor();
	const int row = GetSelectedPlacedRow();
	if (!editor || row == wxNOT_FOUND) {
		return;
	}
	const Position pos = placed_positions[row];
	BRLootZonesState state = map->br_loot_zones.snapshot();
	const auto it = std::find_if(state.items.begin(), state.items.end(), [&](const BRLootItem& item) {
		return item.pos == pos;
	});
	if (it == state.items.end()) {
		return;
	}
	state.items.erase(it);
	editor->ApplyBRLootState(state);
}
