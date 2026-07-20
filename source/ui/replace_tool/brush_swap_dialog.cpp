//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/replace_tool/brush_swap_dialog.h"
#include "ui/replace_tool/brush_picker_dialog.h"

#include "ui/gui.h"
#include "ui/theme.h"
#include "rendering/core/graphics.h"
#include "brushes/brush.h"
#include "item_definitions/core/item_definition_store.h"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/imaglist.h>
#include <wx/dcmemory.h>
#include <wx/msgdlg.h>
#include <wx/log.h>

#include <set>

namespace {

	const int SPRITE = 32;
	const int ARROW_W = 18;
	const int ROW_W = SPRITE + ARROW_W + SPRITE;

	void DrawSprite(wxMemoryDC& dc, uint16_t itemId, int x, int y) {
		if (itemId == 0) {
			return;
		}
		const auto itemDef = g_item_definitions.get(itemId);
		if (!itemDef) {
			return;
		}
		Sprite* spr = g_gui.gfx.getSprite(itemDef.clientId());
		if (spr) {
			spr->DrawTo(&dc, SPRITE_SIZE_32x32, x, y, SPRITE, SPRITE);
		}
	}

	// One row image holding both sides of the pair: [from] -> [to]. A single
	// composite bitmap keeps this a plain wxListCtrl, with no reliance on
	// subitem images.
	wxBitmap MakePairBitmap(uint16_t fromId, uint16_t toId) {
		wxBitmap bmp(ROW_W, SPRITE, 32);
		wxMemoryDC dc(bmp);
		dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
		dc.Clear();

		DrawSprite(dc, fromId, 0, 0);
		DrawSprite(dc, toId, SPRITE + ARROW_W, 0);

		// Arrow between the two sprites.
		const int midY = SPRITE / 2;
		const int ax = SPRITE + 3;
		const int bx = SPRITE + ARROW_W - 3;
		dc.SetPen(wxPen(Theme::Get(toId != 0 ? Theme::Role::TextSubtle : Theme::Role::Error), 1));
		dc.DrawLine(ax, midY, bx, midY);
		dc.DrawLine(bx - 4, midY - 4, bx, midY);
		dc.DrawLine(bx - 4, midY + 4, bx, midY);

		dc.SelectObject(wxNullBitmap);
		return bmp;
	}

} // namespace

BrushSwapDialog::BrushSwapDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Swap Brush", wxDefaultPosition, wxSize(520, 560), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {

	SetBackgroundColour(Theme::Get(Theme::Role::Background));
	SetForegroundColour(Theme::Get(Theme::Role::Text));

	wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

	wxStaticText* intro = new wxStaticText(this, wxID_ANY, "Pick the brush to replace and the brush to replace it with.\nEvery item is paired by role and added as its own rule.");
	intro->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	root->Add(intro, 0, wxEXPAND | wxALL, 10);

	// Two brush slots with an arrow between them.
	wxBoxSizer* slots = new wxBoxSizer(wxHORIZONTAL);
	m_fromButton = new wxButton(this, wxID_ANY, "Pick brush...", wxDefaultPosition, wxSize(200, 40));
	m_toButton = new wxButton(this, wxID_ANY, "Pick brush...", wxDefaultPosition, wxSize(200, 40));
	wxStaticText* arrow = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("\xE2\x86\x92"));
	arrow->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));

	slots->Add(m_fromButton, 1, wxALIGN_CENTER_VERTICAL);
	slots->Add(arrow, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 12);
	slots->Add(m_toButton, 1, wxALIGN_CENTER_VERTICAL);
	root->Add(slots, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

	m_fromButton->Bind(wxEVT_BUTTON, &BrushSwapDialog::OnPickFrom, this);
	m_toButton->Bind(wxEVT_BUTTON, &BrushSwapDialog::OnPickTo, this);

	m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_list->SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	m_list->SetForegroundColour(Theme::Get(Theme::Role::Text));
	m_images = new wxImageList(ROW_W, SPRITE);
	m_list->AssignImageList(m_images, wxIMAGE_LIST_SMALL);
	root->Add(m_list, 1, wxEXPAND | wxALL, 10);

	m_status = new wxStaticText(this, wxID_ANY, "");
	m_status->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	root->Add(m_status, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

	root->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);

	SetSizer(root);
	CenterOnParent();

	// Modeless: closing is ours to drive, wxDialog's default handlers only end
	// a modal loop.
	Bind(wxEVT_BUTTON, &BrushSwapDialog::OnOk, this, wxID_OK);
	Bind(
		wxEVT_BUTTON, [this](wxCommandEvent&) {
			Destroy();
		},
		wxID_CANCEL
	);
	Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
		Destroy();
	});

	RebuildPairs();
}

void BrushSwapDialog::Open(wxWindow* parent, Callback onAccepted) {
	auto* dlg = new BrushSwapDialog(parent);
	dlg->m_onAccepted = std::move(onAccepted);
	dlg->Show();
	dlg->Raise();
}

namespace {

	wxString SlotLabel(const BrushMappingService::Selection& selection) {
		if (selection.isBorder()) {
			return wxString::Format("Border %u", selection.borderId);
		}
		if (!selection.brushName.empty()) {
			return wxString(selection.brushName);
		}
		return "Pick brush...";
	}

} // namespace

void BrushSwapDialog::UpdateSlotButtons() {
	m_fromButton->SetLabel(SlotLabel(m_from));
	m_toButton->SetLabel(SlotLabel(m_to));
	// The destination only makes sense once a source defines the family.
	m_toButton->Enable(!m_from.empty());
}

void BrushSwapDialog::OnPickFrom(wxCommandEvent& event) {
	// `this` is safe to capture: the picker is parented to this dialog, so it
	// is destroyed with it and the callback cannot fire afterwards.
	BrushPickerDialog::PickAny(this, m_from, BrushMappingService::Selection(), [this](const BrushMappingService::Selection& picked) {
		m_from = picked;
		// Changing the source can invalidate the family: drop a destination that
		// no longer matches instead of showing an empty list.
		if (!m_to.empty() && !BrushMappingService::AreCompatible(m_from, m_to)) {
			m_to = BrushMappingService::Selection();
		}
		RebuildPairs();
	});
	event.Skip();
}

void BrushSwapDialog::OnPickTo(wxCommandEvent& event) {
	BrushPickerDialog::PickAny(this, m_to, m_from, [this](const BrushMappingService::Selection& picked) {
		m_to = picked;
		RebuildPairs();
	});
	event.Skip();
}

void BrushSwapDialog::RebuildPairs() {
	UpdateSlotButtons();

	m_pairs = BrushMappingService::BuildRolePairs(m_from, m_to);

	m_list->ClearAll();
	m_images->RemoveAll();
	m_list->InsertColumn(0, "", wxLIST_FORMAT_LEFT, ROW_W + 12);
	m_list->InsertColumn(1, "Role", wxLIST_FORMAT_LEFT, 180);
	m_list->InsertColumn(2, "From", wxLIST_FORMAT_LEFT, 70);
	m_list->InsertColumn(3, "To", wxLIST_FORMAT_LEFT, 70);

	int unmatched = 0;
	for (size_t i = 0; i < m_pairs.size(); ++i) {
		const auto& pair = m_pairs[i];
		m_images->Add(MakePairBitmap(pair.fromId, pair.toId));
		const long idx = m_list->InsertItem((long)i, "", (int)i);
		m_list->SetItem(idx, 1, pair.role);
		m_list->SetItem(idx, 2, wxString::Format("%u", pair.fromId));
		m_list->SetItem(idx, 3, pair.toId != 0 ? wxString::Format("%u", pair.toId) : wxString("-"));
		if (pair.toId == 0) {
			++unmatched;
		}
	}

	const int total = (int)m_pairs.size();
	if (m_from.empty()) {
		m_status->SetLabel("Pick a brush to see its items.");
	} else if (m_pairs.empty()) {
		m_status->SetLabel("This brush has no items that can be swapped by role.");
	} else if (m_to.empty()) {
		m_status->SetLabel(wxString::Format("%d items. Pick a destination brush to pair them.", total));
	} else if (unmatched > 0) {
		m_status->SetLabel(wxString::Format("%d items, %d without a match (skipped).", total, unmatched));
	} else {
		m_status->SetLabel(wxString::Format("%d items paired.", total));
	}
}

void BrushSwapDialog::OnOk(wxCommandEvent& event) {
	m_rules.clear();

	// The engine keys rules by fromId, so a duplicate source would silently
	// shadow the earlier one. Keep the first occurrence and drop the rest.
	std::set<uint16_t> seen;
	int duplicates = 0;
	for (const auto& pair : m_pairs) {
		if (pair.fromId == 0 || pair.toId == 0) {
			continue;
		}
		if (pair.fromId == pair.toId) {
			continue; // no-op rule
		}
		if (!seen.insert(pair.fromId).second) {
			++duplicates;
			continue;
		}
		ReplacementRule rule;
		rule.fromId = pair.fromId;
		ReplacementTarget target;
		target.id = pair.toId;
		target.probability = 100;
		rule.targets.push_back(target);
		m_rules.push_back(std::move(rule));
	}

	if (m_rules.empty()) {
		wxMessageBox("Pick two brushes first - there is nothing to add yet.", "Swap Brush", wxOK | wxICON_INFORMATION, this);
		return; // stay open
	}

	if (duplicates > 0) {
		wxLogWarning("Swap Brush: %d item(s) appear in more than one role; only the first rule was kept.", duplicates);
	}

	// Hand the rules over before self-destructing; Destroy() is deferred, but
	// m_rules would be gone by the time the callback ran otherwise.
	Callback callback = std::move(m_onAccepted);
	m_onAccepted = nullptr;
	const std::vector<ReplacementRule> rules = m_rules;
	Destroy();
	if (callback) {
		callback(rules);
	}
}
