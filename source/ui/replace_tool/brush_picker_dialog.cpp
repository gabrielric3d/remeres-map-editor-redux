//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/replace_tool/brush_picker_dialog.h"

#include "ui/gui.h"
#include "ui/theme.h"
#include "rendering/core/graphics.h"
#include "brushes/brush.h"
#include "brushes/ground/ground_brush.h"
#include "brushes/ground/auto_border.h"
#include "brushes/wall/wall_brush.h"
#include "brushes/carpet/carpet_brush.h"
#include "item_definitions/core/item_definition_store.h"
#include "editor/hotkey_manager.h"

#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/listctrl.h>
#include <wx/imaglist.h>
#include <wx/dcmemory.h>

#include <algorithm>
#include <set>

namespace {

	const int PREVIEW_SIZE = 32;

	// Local copy of the item preview helper: the one in
	// dungeon_preset_editor_dialog.cpp is file-static.
	wxBitmap MakeItemBitmap(uint16_t itemId, int size) {
		wxBitmap bmp(size, size, 32);
		wxMemoryDC dc(bmp);
		dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
		dc.Clear();

		if (itemId > 0) {
			const auto itemDef = g_item_definitions.get(itemId);
			Sprite* spr = nullptr;
			if (itemDef) {
				spr = g_gui.gfx.getSprite(itemDef.clientId());
			}
			if (spr) {
				spr->DrawTo(&dc, SPRITE_SIZE_32x32, 0, 0, size, size);
			}
		}

		dc.SelectObject(wxNullBitmap);
		return bmp;
	}

	// Everything the numeric part of a search term can resolve to, computed once
	// per keystroke instead of per row.
	struct NumericMatch {
		bool active = false;
		uint32_t value = 0;
		const Brush* ownerBrush = nullptr; // brush owning that item id
		std::set<uint32_t> borderIds; // borders containing that item id
	};

	NumericMatch ResolveNumeric(const wxString& filter) {
		NumericMatch match;
		if (filter.empty()) {
			return match;
		}
		unsigned long parsed = 0;
		if (!filter.ToULong(&parsed) || parsed == 0) {
			return match;
		}
		match.active = true;
		match.value = (uint32_t)parsed;

		// An item id can identify a brush through the editor reverse index...
		if (parsed <= 0xFFFF) {
			const auto itemDef = g_item_definitions.get((uint16_t)parsed);
			if (itemDef) {
				match.ownerBrush = itemDef.editorData().brush;
			}
			// ...and a border through the border item index.
			for (const AutoBorder* border : g_brushes.findAutoBordersByBorderItem((uint16_t)parsed, BORDER_NONE)) {
				if (border) {
					match.borderIds.insert(border->id);
				}
			}
		}
		return match;
	}

} // namespace

void BrushPickerDialog::PickBrush(wxWindow* parent, const std::string& initialSelection, const Brush* familyFilter, Callback onPicked) {
	// Heap allocated and parented: modeless dialogs outlive the call, and being
	// a child window means it is destroyed with the parent instead of dangling.
	auto* dlg = new BrushPickerDialog(parent, initialSelection, familyFilter);
	dlg->m_onPicked = std::move(onPicked);
	dlg->Show();
	dlg->Raise();
}

void BrushPickerDialog::PickAny(wxWindow* parent, const BrushMappingService::Selection& initial, const BrushMappingService::Selection& familyFilter, Callback onPicked) {
	auto* dlg = new BrushPickerDialog(parent, initial, familyFilter);
	dlg->m_onPicked = std::move(onPicked);
	dlg->Show();
	dlg->Raise();
}

void BrushPickerDialog::Accept() {
	// Copy first: Destroy() is deferred, but the callback may reopen a picker.
	const BrushMappingService::Selection picked = m_selection;
	Callback callback = std::move(m_onPicked);
	m_onPicked = nullptr;
	Destroy();
	if (callback && !picked.empty()) {
		callback(picked);
	}
}

BrushPickerDialog::BrushPickerDialog(wxWindow* parent, const std::string& initialSelection, const Brush* familyFilter) :
	wxDialog(parent, wxID_ANY, "Select Brush", wxDefaultPosition, wxSize(460, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	m_familyFilter(familyFilter),
	m_allowBorders(false) {

	m_selection.brushName = initialSelection;
	Build();
}

BrushPickerDialog::BrushPickerDialog(wxWindow* parent, const BrushMappingService::Selection& initial, const BrushMappingService::Selection& familyFilter) :
	wxDialog(parent, wxID_ANY, "Select Brush", wxDefaultPosition, wxSize(460, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	m_allowBorders(true) {

	m_selection = initial;
	// A border source constrains the destination to borders, and vice versa.
	if (familyFilter.isBorder()) {
		m_borderOnly = true;
	} else if (!familyFilter.brushName.empty()) {
		m_familyFilter = BrushMappingService::FindBrush(familyFilter.brushName);
	}
	Build();
}

void BrushPickerDialog::Build() {
	SetBackgroundColour(Theme::Get(Theme::Role::Background));
	SetForegroundColour(Theme::Get(Theme::Role::Text));

	wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* filterRow = new wxBoxSizer(wxHORIZONTAL);

	m_search = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_search->SetHint("Search by name or id...");
	// Typing in the search field must not fire editor hotkeys.
	m_search->Bind(wxEVT_SET_FOCUS, [](wxFocusEvent& e) {
		g_hotkeys.DisableHotkeys();
		e.Skip();
	});
	m_search->Bind(wxEVT_KILL_FOCUS, [](wxFocusEvent& e) {
		g_hotkeys.EnableHotkeys();
		e.Skip();
	});
	m_search->Bind(wxEVT_TEXT, &BrushPickerDialog::OnSearch, this);
	filterRow->Add(m_search, 1, wxALIGN_CENTER_VERTICAL);

	// Separate field on purpose: in one box "58" would be ambiguous between a
	// border id and a server id. Here it always means the item id.
	m_serverId = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(110, -1), wxTE_PROCESS_ENTER);
	m_serverId->SetHint("Server ID");
	m_serverId->Bind(wxEVT_SET_FOCUS, [](wxFocusEvent& e) {
		g_hotkeys.DisableHotkeys();
		e.Skip();
	});
	m_serverId->Bind(wxEVT_KILL_FOCUS, [](wxFocusEvent& e) {
		g_hotkeys.EnableHotkeys();
		e.Skip();
	});
	m_serverId->Bind(wxEVT_TEXT, &BrushPickerDialog::OnSearch, this);
	filterRow->Add(m_serverId, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);

	m_type = new wxChoice(this, wxID_ANY);
	m_type->Append("All types");
	m_type->Append("Ground");
	m_type->Append("Wall");
	m_type->Append("Carpet");
	if (m_allowBorders) {
		m_type->Append("Border");
	}
	m_type->SetSelection(0);

	// A family filter already pins the family; leave the control visible but
	// inert so the reason the list is narrow stays obvious.
	if (m_borderOnly && m_allowBorders) {
		m_type->SetSelection((int)TypeFilter::Border);
		m_type->Disable();
	} else if (m_familyFilter) {
		const char* family = BrushMappingService::GetFamilyName(m_familyFilter);
		const int idx = m_type->FindString(family);
		if (idx != wxNOT_FOUND) {
			m_type->SetSelection(idx);
		}
		m_type->Disable();
	}
	m_type->Bind(wxEVT_CHOICE, &BrushPickerDialog::OnTypeChanged, this);
	filterRow->Add(m_type, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);

	root->Add(filterRow, 0, wxEXPAND | wxALL, 8);

	m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_list->SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	m_list->SetForegroundColour(Theme::Get(Theme::Role::Text));
	m_images = new wxImageList(PREVIEW_SIZE, PREVIEW_SIZE);
	m_list->AssignImageList(m_images, wxIMAGE_LIST_SMALL);
	m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, &BrushPickerDialog::OnActivated, this);
	m_list->Bind(wxEVT_LIST_ITEM_SELECTED, &BrushPickerDialog::OnSelectionChanged, this);
	root->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

	root->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 8);

	SetSizer(root);
	CenterOnParent();

	// Modeless: wxDialog's built-in OK/Cancel handling only ends a modal loop,
	// so closing is ours to do.
	Bind(
		wxEVT_BUTTON, [this](wxCommandEvent&) {
			Accept();
		},
		wxID_OK
	);
	Bind(
		wxEVT_BUTTON, [this](wxCommandEvent&) {
			Destroy();
		},
		wxID_CANCEL
	);
	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
		Destroy();
	});

	RebuildList();
}

void BrushPickerDialog::EnsureItemIndex() {
	if (m_itemIndexBuilt) {
		return;
	}
	m_itemIndexBuilt = true;

	std::set<std::string> seen;
	for (const auto& [name, brush] : g_brushes.getMap()) {
		Brush* b = brush.get();
		if (!b || (!b->is<GroundBrush>() && !b->is<WallBrush>() && !b->is<CarpetBrush>())) {
			continue;
		}
		if (!seen.insert(name).second) {
			continue;
		}
		BrushMappingService::Selection selection;
		selection.brushName = name;
		for (const uint16_t id : BrushMappingService::GetItemIds(selection)) {
			m_itemToBrushes[id].push_back(name);
		}
	}

	if (m_allowBorders) {
		for (const auto& [borderId, border] : g_brushes.getBorders()) {
			if (!border) {
				continue;
			}
			for (int dir = 1; dir <= 12; ++dir) {
				for (const auto& entry : border->tiles[dir]) {
					if (entry.id != 0) {
						m_itemToBorders[entry.id].push_back(borderId);
					}
				}
			}
		}
	}
}

BrushPickerDialog::TypeFilter BrushPickerDialog::CurrentTypeFilter() const {
	if (!m_type) {
		return TypeFilter::All;
	}
	return (TypeFilter)m_type->GetSelection();
}

void BrushPickerDialog::RebuildList() {
	if (!m_list || !m_images) {
		return;
	}

	const wxString rawFilter = m_search ? m_search->GetValue().Trim().Trim(false) : wxString();
	const wxString filter = rawFilter.Lower();
	const NumericMatch numeric = ResolveNumeric(rawFilter);
	const TypeFilter typeFilter = CurrentTypeFilter();

	// Server ID field: narrows to whatever owns that item id.
	const wxString rawServerId = m_serverId ? m_serverId->GetValue().Trim().Trim(false) : wxString();
	unsigned long parsedServerId = 0;
	const bool serverIdActive = !rawServerId.empty() && rawServerId.ToULong(&parsedServerId) && parsedServerId > 0 && parsedServerId <= 0xFFFF;
	std::set<std::string> serverIdBrushes;
	std::set<uint32_t> serverIdBorders;
	if (serverIdActive) {
		EnsureItemIndex();
		const uint16_t wanted = (uint16_t)parsedServerId;
		const auto brushHit = m_itemToBrushes.find(wanted);
		if (brushHit != m_itemToBrushes.end()) {
			serverIdBrushes.insert(brushHit->second.begin(), brushHit->second.end());
		}
		const auto borderHit = m_itemToBorders.find(wanted);
		if (borderHit != m_itemToBorders.end()) {
			serverIdBorders.insert(borderHit->second.begin(), borderHit->second.end());
		}
	}

	m_list->ClearAll();
	m_images->RemoveAll();
	m_list->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 250);
	m_list->InsertColumn(1, "Type", wxLIST_FORMAT_LEFT, 80);
	// Uniform meaning across rows: the server id of the representative item. The
	// border's own id is already spelled out in its "Border <id>" label.
	m_list->InsertColumn(2, "Server ID", wxLIST_FORMAT_LEFT, 80);

	m_entries.clear();

	// Matches a row against the search box: name substring, or the numeric term
	// resolved to this very brush/border.
	auto matches = [&](const Entry& entry) {
		// Both fields are ANDed: they narrow, they do not compete.
		if (serverIdActive) {
			const bool owns = entry.selection.isBorder() ? serverIdBorders.count(entry.selection.borderId) > 0 : serverIdBrushes.count(entry.selection.brushName) > 0;
			if (!owns) {
				return false;
			}
		}
		if (filter.empty()) {
			return true;
		}
		if (wxString(entry.label).Lower().Find(filter) != wxNOT_FOUND) {
			return true;
		}
		if (!numeric.active) {
			return false;
		}
		if (entry.selection.isBorder()) {
			// The border's own id, or a border holding that item id.
			return entry.selection.borderId == numeric.value || numeric.borderIds.count(entry.selection.borderId) > 0;
		}
		return entry.brush && entry.brush == numeric.ownerBrush;
	};

	const bool wantBrushes = (typeFilter != TypeFilter::Border);
	const bool wantBorders = m_allowBorders && (typeFilter == TypeFilter::All || typeFilter == TypeFilter::Border);

	if (wantBrushes) {
		// g_brushes.getMap() is a multimap: the same name can appear more than
		// once, so entries are deduplicated by name.
		std::set<std::string> seen;
		for (const auto& [name, brush] : g_brushes.getMap()) {
			Brush* b = brush.get();
			if (!b) {
				continue;
			}
			if (!b->is<GroundBrush>() && !b->is<WallBrush>() && !b->is<CarpetBrush>()) {
				continue; // Table/Doodad are out of scope for role mapping
			}
			if (typeFilter == TypeFilter::Ground && !b->is<GroundBrush>()) {
				continue;
			}
			if (typeFilter == TypeFilter::Wall && !b->is<WallBrush>()) {
				continue;
			}
			if (typeFilter == TypeFilter::Carpet && !b->is<CarpetBrush>()) {
				continue;
			}
			if (m_familyFilter && !BrushMappingService::AreCompatible(m_familyFilter, b)) {
				continue;
			}
			if (!seen.insert(name).second) {
				continue;
			}

			Entry entry;
			entry.selection.brushName = name;
			entry.label = name;
			entry.previewId = BrushMappingService::GetPreviewItemId(b);
			entry.family = BrushMappingService::GetFamilyName(b);
			entry.brush = b;
			if (matches(entry)) {
				m_entries.push_back(std::move(entry));
			}
		}
	}

	if (wantBorders) {
		for (const auto& [borderId, border] : g_brushes.getBorders()) {
			if (!border) {
				continue;
			}
			Entry entry;
			entry.selection.borderId = borderId;
			// Borders have no name in borders.xml, only an id.
			entry.label = "Border " + std::to_string(borderId);
			entry.previewId = BrushMappingService::GetPreviewItemId(entry.selection);
			entry.family = "Border";
			if (matches(entry)) {
				m_entries.push_back(std::move(entry));
			}
		}
	}

	std::sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
		// Brushes alphabetically first, then borders in numeric id order.
		const bool aBorder = a.selection.isBorder();
		const bool bBorder = b.selection.isBorder();
		if (aBorder != bBorder) {
			return !aBorder;
		}
		if (aBorder) {
			return a.selection.borderId < b.selection.borderId;
		}
		return a.label < b.label;
	});

	for (size_t i = 0; i < m_entries.size(); ++i) {
		const Entry& entry = m_entries[i];
		m_images->Add(MakeItemBitmap(entry.previewId, PREVIEW_SIZE));
		const long idx = m_list->InsertItem((long)i, entry.label, (int)i);
		m_list->SetItem(idx, 1, entry.family);
		m_list->SetItem(idx, 2, entry.previewId != 0 ? wxString::Format("%u", (unsigned)entry.previewId) : wxString("-"));

		const bool isCurrent = entry.selection.isBorder() ? (entry.selection.borderId == m_selection.borderId) : (!m_selection.isBorder() && entry.selection.brushName == m_selection.brushName);
		if (isCurrent) {
			m_list->SetItemState(idx, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			m_list->EnsureVisible(idx);
		}
	}
}

void BrushPickerDialog::OnSearch(wxCommandEvent& event) {
	RebuildList();
	event.Skip();
}

void BrushPickerDialog::OnTypeChanged(wxCommandEvent& event) {
	RebuildList();
	event.Skip();
}

void BrushPickerDialog::OnSelectionChanged(wxListEvent& event) {
	const long idx = event.GetIndex();
	if (idx >= 0 && idx < (long)m_entries.size()) {
		m_selection = m_entries[idx].selection;
	}
	event.Skip();
}

void BrushPickerDialog::OnActivated(wxListEvent& event) {
	const long idx = event.GetIndex();
	if (idx >= 0 && idx < (long)m_entries.size()) {
		m_selection = m_entries[idx].selection;
		Accept();
		return;
	}
	event.Skip();
}
