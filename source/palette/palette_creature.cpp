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
#include "brushes/spawn/spawn_brush.h"
#include "game/materials.h"

// ============================================================================
// Creature palette

CreaturePalettePanel::CreaturePalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id) {
	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	choicebook = newd wxChoicebook(this, wxID_ANY);
	topsizer->Add(choicebook, 1, wxEXPAND);

	SetSizerAndFit(topsizer);

	Bind(wxEVT_CHOICEBOOK_PAGE_CHANGING, &CreaturePalettePanel::OnSwitchingPage, this);
	Bind(wxEVT_CHOICEBOOK_PAGE_CHANGED, &CreaturePalettePanel::OnPageChanged, this);

	OnUpdate();
}

PaletteType CreaturePalettePanel::GetType() const {
	return TILESET_CREATURE;
}

void CreaturePalettePanel::SelectFirstBrush() {
	if (choicebook->GetPageCount() == 0) {
		return;
	}

	if (auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage())) {
		panel->SelectFirstBrush();
	}
}

Brush* CreaturePalettePanel::GetSelectedBrush() const {
	return GetSelectedCreatureBrush();
}

Brush* CreaturePalettePanel::GetSelectedCreatureBrush() const {
	if (choicebook->GetPageCount() == 0) {
		return nullptr;
	}

	auto* panel = dynamic_cast<BrushPanel*>(choicebook->GetCurrentPage());
	if (!panel) {
		return nullptr;
	}

	Brush* brush = panel->GetSelectedBrush();
	return brush && brush->is<CreatureBrush>() ? brush : nullptr;
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
				return true;
			}
		}
	} else if (whatbrush->is<SpawnBrush>()) {
		return true;
	}
	return false;
}

int CreaturePalettePanel::GetSelectedBrushSize() const {
	return g_settings.getInteger(Config::CURRENT_SPAWN_RADIUS);
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
	(void)shape;
	(void)size;
}

void CreaturePalettePanel::OnSwitchIn() {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SetSpawnTime(g_settings.getInteger(Config::DEFAULT_SPAWNTIME));
	g_gui.SetBrushSize(g_settings.getInteger(Config::CURRENT_SPAWN_RADIUS));
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
		// BrushPanel doesn't easily expose selection by index,
		// but since this is usually used for "SelectFirstBrush" (index 0),
		// we can just select the first brush if bp is valid.
		bp->SelectFirstBrush();
	}
}

void CreaturePalettePanel::SelectCreature(std::string name) {
	// Better approach: use g_creatures to find the brush
	// and then call the existing SelectBrush(Brush*)
	if (CreatureType* ct = g_creatures[name]) {
		if (ct->brush) {
			SelectBrush(ct->brush);
		}
	}
}

void CreaturePalettePanel::OnSwitchingPage(wxChoicebookEvent& event) {
	// Do nothing
}

void CreaturePalettePanel::OnPageChanged(wxChoicebookEvent& event) {
	g_gui.ActivatePalette(GetParentPalette());
	g_gui.SelectBrush();
}
