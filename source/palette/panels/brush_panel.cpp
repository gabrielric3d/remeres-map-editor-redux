#include "palette/panels/brush_panel.h"
#include "ui/gui.h"
#include "app/settings.h"
#include "palette/palette_window.h" // For PaletteWindow dynamic_casts
#include "palette/controls/virtual_brush_grid.h"
#include <spdlog/spdlog.h>
#include <wx/wrapsizer.h>
#include <algorithm>
#include <cctype>
#include <iterator>

namespace {
	// Case insensitive substring test that doesn't allocate, the filter runs
	// over every brush of every tileset while the user types.
	bool ContainsNoCase(const std::string& haystack, const std::string& needle_lower) {
		if (needle_lower.empty()) {
			return true;
		}
		const auto it = std::search(
			haystack.begin(), haystack.end(),
			needle_lower.begin(), needle_lower.end(),
			[](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == b;
			}
		);
		return it != haystack.end();
	}
}

// ============================================================================
// Brush Panel
// A container of brush buttons

BrushPanel::BrushPanel(wxWindow* parent) :
	wxPanel(parent, wxID_ANY),
	tileset(nullptr),
	brushbox(nullptr),
	loaded(false),
	needs_rebuild(false),
	list_type(BRUSHLIST_LISTBOX) {
	sizer = newd wxBoxSizer(wxVERTICAL);
	SetSizerAndFit(sizer);
}

BrushPanel::~BrushPanel() {
	////
}

void BrushPanel::AssignTileset(const TilesetCategory* _tileset) {
	if (_tileset != tileset) {
		tileset = _tileset;
		needs_rebuild = true;
		if (loaded) {
			DestroyBrushbox();
		}
	}
}

void BrushPanel::SetListType(BrushListType ltype) {
	if (list_type != ltype) {
		list_type = ltype;
		needs_rebuild = true;
		if (loaded) {
			DestroyBrushbox();
		}
	}
}

void BrushPanel::SetListType(wxString ltype) {
	if (ltype == "small icons") {
		SetListType(BRUSHLIST_SMALL_ICONS);
	} else if (ltype == "large icons") {
		SetListType(BRUSHLIST_LARGE_ICONS);
	} else if (ltype == "listbox") {
		SetListType(BRUSHLIST_LISTBOX);
	} else if (ltype == "textlistbox") {
		SetListType(BRUSHLIST_TEXT_LISTBOX);
	}
}

void BrushPanel::DestroyBrushbox() {
	sizer->Clear(true);
	loaded = false;
	brushbox = nullptr;
}

void BrushPanel::InvalidateContents() {
	// Mark for rebuild on next LoadContents - the grid will be recreated
	needs_rebuild = true;
	DestroyBrushbox();
}

void BrushPanel::LoadContents() {
	if (loaded && !needs_rebuild) {
		return;
	}

	// Destroy existing grid if we need to rebuild
	if (needs_rebuild && brushbox) {
		DestroyBrushbox();
	}
	needs_rebuild = false;

	ASSERT(tileset != nullptr);

	switch (list_type) {
		case BRUSHLIST_LARGE_ICONS:
			brushbox = newd VirtualBrushGrid(this, tileset, RENDER_SIZE_32x32);
			break;
		case BRUSHLIST_SMALL_ICONS:
			brushbox = newd VirtualBrushGrid(this, tileset, RENDER_SIZE_16x16);
			break;
		case BRUSHLIST_LISTBOX:
		case BRUSHLIST_TEXT_LISTBOX: {
			auto vbg = newd VirtualBrushGrid(this, tileset, RENDER_SIZE_32x32);
			vbg->SetDisplayMode(VirtualBrushGrid::DisplayMode::List);
			brushbox = vbg;
			break;
		}
		default:
			break;
	}

	if (!brushbox) {
		return;
	}

	loaded = true;
	sizer->Add(brushbox->GetSelfWindow(), 1, wxEXPAND);
	Layout();
	ApplyDisplayOptions();
	brushbox->SelectFirstBrush();
}

void BrushPanel::ApplyDisplayOptions() {
	if (!brushbox) {
		return;
	}
	brushbox->SetShowLabels(show_labels);
	brushbox->SetSort(sort_key, sort_dir);
	brushbox->SetReorderMode(reorder_mode);

	auto* vbg = dynamic_cast<VirtualBrushGrid*>(brushbox);
	if (vbg) {
		if (filter_text.empty()) {
			vbg->ClearFilter();
		} else {
			vbg->SetFilter(filter_text);
		}
	}
}

void BrushPanel::SelectFirstBrush() {
	if (loaded) {
		ASSERT(brushbox != nullptr);
		brushbox->SelectFirstBrush();
	}
}

Brush* BrushPanel::GetSelectedBrush() const {
	if (loaded) {
		ASSERT(brushbox != nullptr);
		return brushbox->GetSelectedBrush();
	}

	if (tileset && tileset->size() > 0) {
		return tileset->brushlist[0];
	}
	return nullptr;
}

bool BrushPanel::SelectBrush(const Brush* whatbrush) {
	if (loaded) {
		ASSERT(brushbox != nullptr);
		return brushbox->SelectBrush(whatbrush);
	}

	for (const auto* brush : tileset->brushlist) {
		if (brush == whatbrush) {
			LoadContents();
			return brushbox->SelectBrush(whatbrush);
		}
	}
	return false;
}

bool BrushPanel::SelectBrushByOffset(int offset) {
	if (loaded && brushbox) {
		return brushbox->SelectBrushByOffset(offset);
	}
	return false;
}

void BrushPanel::SetFilter(const std::string& filter) {
	// Remember it even when the grid isn't built yet, LoadContents reapplies it.
	filter_text = filter;

	if (!loaded || !brushbox) {
		return;
	}
	auto* vbg = dynamic_cast<VirtualBrushGrid*>(brushbox);
	if (vbg) {
		if (filter.empty()) {
			vbg->ClearFilter();
		} else {
			vbg->SetFilter(filter);
		}
	}
}

void BrushPanel::ClearFilter() {
	SetFilter("");
}

bool BrushPanel::HasVisibleBrushes() const {
	if (!tileset) {
		return false;
	}
	if (filter_text.empty()) {
		return tileset->size() > 0;
	}

	const std::string needle = as_lower_str(filter_text);
	for (const Brush* brush : tileset->brushlist) {
		if (brush && ContainsNoCase(brush->getName(), needle)) {
			return true;
		}
	}
	return false;
}

void BrushPanel::SetSort(TilesetSortKey key, TilesetSortDirection dir) {
	sort_key = key;
	sort_dir = dir;
	if (loaded && brushbox) {
		brushbox->SetSort(key, dir);
	}
}

void BrushPanel::SetShowLabels(bool show) {
	show_labels = show;
	if (loaded && brushbox) {
		brushbox->SetShowLabels(show);
	}
}

void BrushPanel::SetReorderMode(bool enabled) {
	reorder_mode = enabled;
	if (loaded && brushbox) {
		brushbox->SetReorderMode(enabled);
	}
}

void BrushPanel::ResetCustomOrder() {
	if (loaded && brushbox) {
		brushbox->ResetCustomOrder();
	}
}

void BrushPanel::OnSwitchIn() {
	LoadContents();
	if (brushbox) {
		brushbox->OnSwitchIn();
	}
}

void BrushPanel::OnSwitchOut() {
	if (brushbox) {
		brushbox->OnSwitchOut();
	}
}
