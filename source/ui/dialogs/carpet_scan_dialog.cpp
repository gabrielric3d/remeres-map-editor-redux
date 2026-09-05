//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Carpet Scan Dialog - classify candidate items into carpet alignment slots
// (kNN over the carpet brushes already loaded in g_brushes, every candidate
// measured against the rest of the scanned batch).
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/dialogs/carpet_scan_dialog.h"
#include "brushes/carpet/carpet_classifier.h"
#include "editor/editor.h"
#include "editor/selection.h"
#include "map/tile.h"
#include "game/item.h"
#include "item_definitions/core/item_definition_store.h"
#include "ui/gui.h"
#include "ui/theme.h"
#include "util/common.h"
#include "util/nvg_utils.h"

#include <wx/sizer.h>
#include <wx/msgdlg.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>

#include <algorithm>
#include <set>

namespace {

enum {
	ID_CARPET_SCAN_ADD = wxID_HIGHEST + 5400,
	ID_CARPET_SCAN_FROM_SELECTION,
	ID_CARPET_SCAN_CLEAR,
	ID_CARPET_SCAN_AUTO_DETECT,
	ID_CARPET_SCAN_ALIGN_CHOICE,
	ID_CARPET_SCAN_APPLY,
	ID_CARPET_SCAN_CHECK_ALIGN_CHOICE,
	ID_CARPET_SCAN_CHECK_ALIGN,
	ID_CARPET_SCAN_SHOW_ALL,
};

constexpr int MAX_IDS_PER_ADD = 2000;
constexpr long MIN_ITEM_ID = 100;
constexpr long MAX_ITEM_ID = 65535;

// Across 13 slots the smallest winning share for K=5 is 2 votes (~40%), and a
// fully split vote gives 20% each. Below this a row stays Pending so the user reviews
// it instead of it silently landing in a slot.
constexpr float MIN_AUTO_CONFIDENCE = 30.0f;

// Index of the "(excluded)" entry in the override choice: after "(auto)" + the 13 slots.
constexpr int EXCLUDED_CHOICE_INDEX = 1 + static_cast<int>(CarpetClassifier::ALIGN_COUNT);

// Effective score used for the filtered view sort. Manual overrides count as 101.
float RowScore(const CarpetScanRow& row) {
	return row.manual ? 101.0f : row.result.confidence;
}

// Slot codes ("n", "cnw", "center") are short enough for a 96px grid cell; the long
// label shows in the tooltip, the review line and the override dropdown.
wxString ShortAlignName(const std::string& align) {
	return wxstr(align);
}

// "cnw - Northwest corner" for the dropdowns.
wxString AlignDisplayName(const std::string& align) {
	const BorderType slot = carpetAlignFromString(align);
	if (slot == BORDER_NONE) {
		return wxstr(align);
	}
	return wxString::Format("%s - %s", wxstr(align), carpetAlignLabel(slot));
}

wxString StateName(const CarpetScanRow& row) {
	switch (row.state) {
		case CarpetScanRow::State::Pending:
			return row.scanned ? "pending" : "not scanned";
		case CarpetScanRow::State::Assigned:
			return "assigned";
		case CarpetScanRow::State::Excluded:
			return "excluded";
		case CarpetScanRow::State::AlreadyInCarpet:
			return wxString::Format("already in carpet '%s'", wxstr(row.result.existingCarpetName));
		case CarpetScanRow::State::Rejected:
			return row.result.status == CarpetScanResult::Status::TooLarge
				? "rejected (sprite larger than 1x1)"
				: "rejected (no sprite)";
	}
	return "";
}

// Second text line inside a grid cell.
wxString RowLine2(const CarpetScanRow& row) {
	switch (row.state) {
		case CarpetScanRow::State::Rejected:
			return row.result.status == CarpetScanResult::Status::TooLarge ? "too large" : "no sprite";
		case CarpetScanRow::State::Excluded:
			return "excluded";
		case CarpetScanRow::State::AlreadyInCarpet:
			return "in a carpet";
		default:
			break;
	}
	if (!row.scanned) {
		return "not scanned";
	}
	const std::string align = row.effectiveAlign();
	if (align.empty()) {
		return "no match";
	}
	wxString text = wxString::Format("%s %.0f%%", ShortAlignName(align), row.result.confidence);
	if (row.manual) {
		text += "*";
	}
	return text;
}

// 2px status border color, mirroring the Border Scan styling.
wxColour StateBorderColour(const CarpetScanRow& row) {
	switch (row.state) {
		case CarpetScanRow::State::Assigned:
			return (!row.manual && row.result.confidence < 55.0f)
				? Theme::Get(Theme::Role::Warning)
				: Theme::Get(Theme::Role::Success);
		case CarpetScanRow::State::Pending:
			return Theme::Get(Theme::Role::Border);
		case CarpetScanRow::State::Excluded:
		case CarpetScanRow::State::AlreadyInCarpet:
			return Theme::Get(Theme::Role::TextSubtle);
		case CarpetScanRow::State::Rejected:
			return Theme::Get(Theme::Role::Error);
	}
	return Theme::Get(Theme::Role::Border);
}

} // namespace

// ============================================================================
// CarpetScanRow
// ============================================================================

std::string CarpetScanRow::effectiveAlign() const {
	return manual ? manualAlign : result.align;
}

// ============================================================================
// CarpetScanGrid
// ============================================================================

CarpetScanGrid::CarpetScanGrid(wxWindow* parent, std::vector<CarpetScanRow>* rows) :
	VirtualItemGrid(parent, wxID_ANY),
	m_rows(rows) {
	m_itemSize = 96;
	m_padding = 6;

	const wxColour bg = Theme::Get(Theme::Role::Background);
	m_bgRed = bg.Red() / 255.0f;
	m_bgGreen = bg.Green() / 255.0f;
	m_bgBlue = bg.Blue() / 255.0f;
}

size_t CarpetScanGrid::GetItemCount() const {
	if (!m_rows) {
		return 0;
	}
	return m_view.empty() ? m_rows->size() : m_view.size();
}

int CarpetScanGrid::RowIndexFor(int gridIndex) const {
	if (!m_rows || gridIndex < 0) {
		return -1;
	}
	if (m_view.empty()) {
		return gridIndex < static_cast<int>(m_rows->size()) ? gridIndex : -1;
	}
	if (gridIndex >= static_cast<int>(m_view.size())) {
		return -1;
	}
	return m_view[gridIndex];
}

uint16_t CarpetScanGrid::GetItem(size_t index) const {
	const int rowIndex = RowIndexFor(static_cast<int>(index));
	return rowIndex >= 0 ? (*m_rows)[rowIndex].result.itemId : 0;
}

void CarpetScanGrid::SetView(std::vector<int> view) {
	m_view = std::move(view);
	RefreshGrid();
}

void CarpetScanGrid::OnItemSelected(int index) {
	if (m_onSelected) {
		m_onSelected(RowIndexFor(index));
	}
}

wxString CarpetScanGrid::GetItemName(size_t index) const {
	const int rowIndex = RowIndexFor(static_cast<int>(index));
	if (rowIndex < 0) {
		return "";
	}
	const CarpetScanRow& row = (*m_rows)[rowIndex];

	wxString tip = wxString::Format("Item %u", static_cast<unsigned int>(row.result.itemId));
	const auto definition = g_item_definitions.get(row.result.itemId);
	if (definition) {
		const wxString name = wxstr(definition.name());
		if (!name.IsEmpty()) {
			tip += " - " + name;
		}
	}
	if (row.scanned && !row.result.align.empty()) {
		tip += wxString::Format("\nSuggested: %s (%.0f%%)", wxstr(row.result.align), row.result.confidence);
		if (!row.result.secondAlign.empty()) {
			tip += wxString::Format(", 2nd: %s (%.0f%%)", wxstr(row.result.secondAlign), row.result.secondConfidence);
		}
	}
	if (row.result.status == CarpetScanResult::Status::AlreadyInCarpet) {
		tip += wxString::Format("\nAlready used by carpet brush '%s'", wxstr(row.result.existingCarpetName));
		if (!row.result.existingCarpetAlign.empty()) {
			tip += wxString::Format(" as %s", wxstr(row.result.existingCarpetAlign));
		}
	}
	if (row.manual) {
		tip += wxString::Format("\nManual override: %s", wxstr(row.manualAlign));
	}
	if (row.excluded) {
		tip += "\nExcluded";
	}
	tip += "\nStatus: " + StateName(row);
	return tip;
}

void CarpetScanGrid::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
	const size_t count = GetItemCount();
	if (count == 0) {
		nvgFontSize(vg, 13.0f);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle)));
		nvgText(vg, width / 2.0f, GetScrollPosition() + height / 2.0f,
				"Add candidates and click Auto-Detect", nullptr);
		return;
	}

	// Visible range (same math as the VirtualItemGrid base).
	const int rowHeight = m_itemSize + m_padding;
	const int scrollPos = GetScrollPosition();
	const int startRow = scrollPos / rowHeight;
	const int endRow = (scrollPos + height + rowHeight - 1) / rowHeight;
	const int startIdx = startRow * m_columns;
	const int endIdx = std::min(static_cast<int>(count), (endRow + 1) * m_columns);

	for (int i = startIdx; i < endIdx; ++i) {
		if (i < 0) {
			continue;
		}
		const int rowIndex = RowIndexFor(i);
		if (rowIndex < 0) {
			continue;
		}
		const CarpetScanRow& row = (*m_rows)[rowIndex];

		const wxRect r = GetItemRect(i);
		const float x = static_cast<float>(r.x);
		const float y = static_cast<float>(r.y);
		const float w = static_cast<float>(r.width);
		const float h = static_cast<float>(r.height);

		const bool isSelected = (i == m_selectedIndex);
		const bool isHovered = (i == m_hoverIndex);

		// Card background
		nvgBeginPath(vg);
		nvgRoundedRect(vg, x, y, w, h, 4.0f);
		if (isSelected) {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::Selected)));
		} else if (isHovered) {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBaseHover)));
		} else {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBase)));
		}
		nvgFill(vg);

		// 2px status border
		const wxColour borderCol = StateBorderColour(row);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, 4.0f);
		nvgStrokeColor(vg, NvgUtils::ToNvColor(borderCol));
		nvgStrokeWidth(vg, 2.0f);
		nvgStroke(vg);

		// Sprite (top-centered). Carpets are 32x32, but keep the aspect-ratio math so
		// an odd-sized sprite still draws sensibly.
		const int tex = GetOrCreateItemTexture(vg, row.result.itemId);
		if (tex > 0) {
			int texW = 0;
			int texH = 0;
			nvgImageSize(vg, tex, &texW, &texH);
			if (texW <= 0 || texH <= 0) {
				texW = 32;
				texH = 32;
			}
			const float maxSide = 40.0f;
			const float scale = std::min(maxSide / static_cast<float>(texW), maxSide / static_cast<float>(texH));
			const float drawW = texW * scale;
			const float drawH = texH * scale;
			const float dx = x + (w - drawW) / 2.0f;
			const float dy = y + 4.0f + (maxSide - drawH) / 2.0f;
			NVGpaint imgPaint = nvgImagePattern(vg, dx, dy, drawW, drawH, 0.0f, tex, 1.0f);
			nvgBeginPath(vg);
			nvgRect(vg, dx, dy, drawW, drawH);
			nvgFillPaint(vg, imgPaint);
			nvgFill(vg);
		}

		// Line 1: item id
		const std::string idText = std::to_string(row.result.itemId);
		nvgFontSize(vg, 12.0f);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		if (isSelected) {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextOnAccent)));
		} else {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::Text)));
		}
		nvgText(vg, x + w / 2.0f, y + 50.0f, idText.c_str(), nullptr);

		// Line 2: align/confidence or status
		const wxString line2 = RowLine2(row);
		nvgFontSize(vg, 11.0f);
		if (isSelected) {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextOnAccent)));
		} else if (row.state == CarpetScanRow::State::Pending) {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle)));
		} else {
			nvgFillColor(vg, NvgUtils::ToNvColor(borderCol));
		}
		nvgText(vg, x + w / 2.0f, y + 66.0f, line2.mb_str(), nullptr);
	}
}

// ============================================================================
// CarpetScanDialog
// ============================================================================

CarpetScanDialog::CarpetScanDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Carpet Scan", wxDefaultPosition, wxDefaultSize,
			 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {

	SetBackgroundColour(Theme::Get(Theme::Role::Surface));

	wxBoxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	// --- 1. Candidates row ---
	wxBoxSizer* candidatesRow = newd wxBoxSizer(wxHORIZONTAL);
	candidatesRow->Add(newd wxStaticText(this, wxID_ANY, "Candidates:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	m_candidateInput = newd wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_candidateInput->SetHint("e.g. 6477-6488, 57619");
	m_candidateInput->SetToolTip("Comma-separated item IDs and ranges (A-B)");
	candidatesRow->Add(m_candidateInput, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	wxButton* addButton = newd wxButton(this, ID_CARPET_SCAN_ADD, "Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	addButton->SetToolTip("Add the typed IDs/ranges to the candidate list");
	candidatesRow->Add(addButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	m_fromSelectionButton = newd wxButton(this, ID_CARPET_SCAN_FROM_SELECTION, "From Map Selection", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_fromSelectionButton->SetToolTip("Collect unique item IDs from the current map selection");
	candidatesRow->Add(m_fromSelectionButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	wxButton* clearButton = newd wxButton(this, ID_CARPET_SCAN_CLEAR, "Clear", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	clearButton->SetToolTip("Remove all candidates");
	candidatesRow->Add(clearButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	m_countLabel = newd wxStaticText(this, wxID_ANY, "0 candidates");
	m_countLabel->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	candidatesRow->Add(m_countLabel, 0, wxALIGN_CENTER_VERTICAL);

	sizer->Add(candidatesRow, 0, wxEXPAND | wxALL, 8);

	// --- 2. Detect row + "Check alignment" filter ---
	wxBoxSizer* detectRow = newd wxBoxSizer(wxHORIZONTAL);

	wxButton* detectButton = newd wxButton(this, ID_CARPET_SCAN_AUTO_DETECT, "Auto-Detect");
	detectButton->SetToolTip("Classify all candidates against each other (scan one carpet family at a time; Shift+click also shows a validation report)");
	detectRow->Add(detectButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	m_statusLabel = newd wxStaticText(this, wxID_ANY, "Add candidate item IDs, then click Auto-Detect.",
									  wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	m_statusLabel->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	detectRow->Add(m_statusLabel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	detectRow->Add(newd wxStaticText(this, wxID_ANY, "Check alignment:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	wxArrayString alignNames;
	for (const std::string& align : CarpetClassifier::ALIGN_NAMES) {
		alignNames.Add(AlignDisplayName(align));
	}
	m_checkAlignChoice = newd wxChoice(this, ID_CARPET_SCAN_CHECK_ALIGN_CHOICE, wxDefaultPosition, wxDefaultSize, alignNames);
	m_checkAlignChoice->SetSelection(0);
	m_checkAlignChoice->SetToolTip("Filter the grid to one slot, sorted by confidence");
	detectRow->Add(m_checkAlignChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	wxButton* checkButton = newd wxButton(this, ID_CARPET_SCAN_CHECK_ALIGN, "Check", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	checkButton->SetToolTip("Show only rows classified as the chosen align (best confidence first)");
	detectRow->Add(checkButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	wxButton* showAllButton = newd wxButton(this, ID_CARPET_SCAN_SHOW_ALL, "Show All", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	showAllButton->SetToolTip("Remove the slot filter");
	detectRow->Add(showAllButton, 0, wxALIGN_CENTER_VERTICAL);

	sizer->Add(detectRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	// --- 3. Results grid ---
	m_grid = newd CarpetScanGrid(this, &m_rows);
	m_grid->SetSelectionCallback([this](int rowIndex) { OnRowSelected(rowIndex); });
	sizer->Add(m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

	// --- 4. Selected-row line ---
	wxBoxSizer* reviewRow = newd wxBoxSizer(wxHORIZONTAL);

	m_rowInfoLabel = newd wxStaticText(this, wxID_ANY, "Select a result to review it.",
									   wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	m_rowInfoLabel->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	reviewRow->Add(m_rowInfoLabel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	reviewRow->Add(newd wxStaticText(this, wxID_ANY, "Alignment:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	wxArrayString overrideEntries;
	overrideEntries.Add("(auto)");
	for (const std::string& align : CarpetClassifier::ALIGN_NAMES) {
		overrideEntries.Add(AlignDisplayName(align));
	}
	overrideEntries.Add("(excluded)");
	m_alignChoice = newd wxChoice(this, ID_CARPET_SCAN_ALIGN_CHOICE, wxDefaultPosition, wxDefaultSize, overrideEntries);
	m_alignChoice->SetSelection(0);
	m_alignChoice->Enable(false);
	m_alignChoice->SetToolTip("Override the detected align, or exclude the item from Apply");
	reviewRow->Add(m_alignChoice, 0, wxALIGN_CENTER_VERTICAL);

	sizer->Add(reviewRow, 0, wxEXPAND | wxALL, 8);

	// --- 5. Bottom buttons ---
	wxBoxSizer* buttonRow = newd wxBoxSizer(wxHORIZONTAL);
	buttonRow->AddStretchSpacer(1);

	wxButton* applyButton = newd wxButton(this, ID_CARPET_SCAN_APPLY, "Apply to Carpet");
	applyButton->SetToolTip("Add the assigned items to their alignments in the Carpet editor");
	applyButton->SetDefault();
	buttonRow->Add(applyButton, 0, wxRIGHT, 6);

	wxButton* cancelButton = newd wxButton(this, wxID_CANCEL, "Cancel");
	buttonRow->Add(cancelButton, 0);

	sizer->Add(buttonRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	SetSizer(sizer);
	SetMinClientSize(FromDIP(wxSize(640, 560)));
	SetClientSize(FromDIP(wxSize(720, 600)));
	Centre(wxBOTH);

	// --- Bindings (prefer Bind over event tables) ---
	addButton->Bind(wxEVT_BUTTON, &CarpetScanDialog::OnAddCandidates, this);
	m_candidateInput->Bind(wxEVT_TEXT_ENTER, &CarpetScanDialog::OnAddCandidates, this);
	m_fromSelectionButton->Bind(wxEVT_BUTTON, &CarpetScanDialog::OnFromMapSelection, this);
	clearButton->Bind(wxEVT_BUTTON, &CarpetScanDialog::OnClearCandidates, this);
	detectButton->Bind(wxEVT_BUTTON, &CarpetScanDialog::OnAutoDetect, this);
	m_alignChoice->Bind(wxEVT_CHOICE, &CarpetScanDialog::OnAlignOverride, this);
	checkButton->Bind(wxEVT_BUTTON, &CarpetScanDialog::OnCheckAlign, this);
	showAllButton->Bind(wxEVT_BUTTON, &CarpetScanDialog::OnShowAll, this);
	applyButton->Bind(wxEVT_BUTTON, &CarpetScanDialog::OnApply, this);

	// --- Guards ---
	if (g_gui.GetCurrentEditor() == nullptr) {
		m_fromSelectionButton->Enable(false);
		m_fromSelectionButton->SetToolTip("Open a map to collect items from the selection");
	}
}

void CarpetScanDialog::SetStatusMessage(const wxString& text, bool isError) {
	m_statusLabel->SetLabel(text);
	m_statusLabel->SetForegroundColour(
		Theme::Get(isError ? Theme::Role::Error : Theme::Role::TextSubtle));
	m_statusLabel->Refresh();
	Layout();
}

bool CarpetScanDialog::ParseCandidateText(const wxString& text, std::vector<uint16_t>& outIds, wxString& error) const {
	outIds.clear();

	wxStringTokenizer tokenizer(text, ",");
	while (tokenizer.HasMoreTokens()) {
		wxString token = tokenizer.GetNextToken();
		token.Trim(true).Trim(false);
		if (token.IsEmpty()) {
			continue;
		}

		long first = 0;
		long last = 0;
		const int dash = token.Find('-');
		if (dash == wxNOT_FOUND) {
			if (!token.ToLong(&first) || first < MIN_ITEM_ID || first > MAX_ITEM_ID) {
				error = wxString::Format("Invalid token '%s' - expected an item ID in [%ld, %ld].",
										 token, MIN_ITEM_ID, MAX_ITEM_ID);
				return false;
			}
			last = first;
		} else {
			wxString left = token.Left(dash);
			wxString right = token.Mid(dash + 1);
			left.Trim(true).Trim(false);
			right.Trim(true).Trim(false);
			if (!left.ToLong(&first) || !right.ToLong(&last)
				|| first < MIN_ITEM_ID || first > MAX_ITEM_ID
				|| last < MIN_ITEM_ID || last > MAX_ITEM_ID) {
				error = wxString::Format("Invalid range '%s' - expected A-B with both in [%ld, %ld].",
										 token, MIN_ITEM_ID, MAX_ITEM_ID);
				return false;
			}
			if (first > last) {
				error = wxString::Format("Invalid range '%s' - start is greater than end.", token);
				return false;
			}
		}

		for (long value = first; value <= last; ++value) {
			outIds.push_back(static_cast<uint16_t>(value));
			if (outIds.size() > static_cast<size_t>(MAX_IDS_PER_ADD)) {
				error = wxString::Format("Too many IDs in one Add (max %d).", MAX_IDS_PER_ADD);
				return false;
			}
		}
	}

	if (outIds.empty()) {
		error = "No valid item IDs found.";
		return false;
	}
	return true;
}

size_t CarpetScanDialog::AddCandidateIds(const std::vector<uint16_t>& ids) {
	std::set<uint16_t> existing;
	for (const CarpetScanRow& row : m_rows) {
		existing.insert(row.result.itemId);
	}

	size_t added = 0;
	for (uint16_t id : ids) {
		if (!existing.insert(id).second) {
			continue; // silently skip duplicates (within the batch and against existing rows)
		}
		CarpetScanRow row;
		row.result.itemId = id;
		m_rows.push_back(row);
		++added;
	}
	return added;
}

void CarpetScanDialog::OnAddCandidates(wxCommandEvent& WXUNUSED(event)) {
	wxString text = m_candidateInput->GetValue();
	text.Trim(true).Trim(false);
	if (text.IsEmpty()) {
		SetStatusMessage("Type item IDs or ranges first (e.g. 6477-6488, 57619).", true);
		return;
	}

	std::vector<uint16_t> ids;
	wxString error;
	if (!ParseCandidateText(text, ids, error)) {
		wxMessageBox(error, "Invalid candidates", wxOK | wxICON_ERROR, this);
		return;
	}

	const size_t added = AddCandidateIds(ids);
	m_candidateInput->Clear();
	RecomputeStates();
	RefreshGridAndCounts(static_cast<int>(added));
}

void CarpetScanDialog::OnFromMapSelection(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		wxMessageBox("Open a map to collect items from the selection.", "Carpet Scan", wxOK | wxICON_INFORMATION, this);
		return;
	}

	const std::vector<Tile*>& tiles = editor->selection.getTiles();
	if (tiles.empty()) {
		wxMessageBox("The map selection is empty.", "Carpet Scan", wxOK | wxICON_INFORMATION, this);
		return;
	}

	std::set<uint16_t> unique;
	for (const Tile* tile : tiles) {
		if (!tile) {
			continue;
		}
		if (tile->ground) {
			unique.insert(tile->ground->getID());
		}
		for (const auto& item : tile->items) {
			if (item) {
				unique.insert(item->getID());
			}
		}
	}

	std::vector<uint16_t> ids;
	ids.reserve(unique.size());
	for (uint16_t id : unique) {
		if (id >= MIN_ITEM_ID) {
			ids.push_back(id);
		}
	}
	if (ids.empty()) {
		wxMessageBox("No items found in the selection.", "Carpet Scan", wxOK | wxICON_INFORMATION, this);
		return;
	}

	const size_t added = AddCandidateIds(ids);
	RecomputeStates();
	RefreshGridAndCounts(static_cast<int>(added));
}

void CarpetScanDialog::OnClearCandidates(wxCommandEvent& WXUNUSED(event)) {
	m_rows.clear();
	RecomputeStates();
	RefreshGridAndCounts();
	SetStatusMessage("Add candidate item IDs, then click Auto-Detect.");
}

void CarpetScanDialog::OnAutoDetect(wxCommandEvent& WXUNUSED(event)) {
	if (m_rows.empty()) {
		SetStatusMessage("Add candidate item IDs first.", true);
		return;
	}

	const bool showValidation = wxGetKeyState(WXK_SHIFT);

	CarpetClassifier& classifier = CarpetClassifier::Get();
	wxString validationReport;
	{
		wxBusyCursor busy;
		if (!classifier.ensureTrained()) {
			wxMessageBox("No training data: no carpet brushes are loaded.", "Carpet Scan", wxOK | wxICON_WARNING, this);
			return;
		}

		std::vector<uint16_t> ids;
		ids.reserve(m_rows.size());
		for (const CarpetScanRow& row : m_rows) {
			ids.push_back(row.result.itemId);
		}

		const std::vector<CarpetScanResult> results = classifier.classify(ids);
		for (size_t i = 0; i < m_rows.size() && i < results.size(); ++i) {
			m_rows[i].result = results[i];
			m_rows[i].scanned = true;
			// Re-detect clears manual overrides.
			m_rows[i].manual = false;
			m_rows[i].manualAlign.clear();
			m_rows[i].excluded = false;
		}

		if (showValidation) {
			validationReport = wxstr(classifier.validateLeaveOneOut());
		}
	}

	size_t rejected = 0;
	for (const CarpetScanRow& row : m_rows) {
		if (row.result.status == CarpetScanResult::Status::TooLarge
			|| row.result.status == CarpetScanResult::Status::NoSprite) {
			++rejected;
		}
	}

	SetStatusMessage(wxString::Format("Trained on %lu samples from %lu carpet brushes (%lu rejected).",
									  static_cast<unsigned long>(classifier.sampleCount()),
									  static_cast<unsigned long>(classifier.groupCount()),
									  static_cast<unsigned long>(rejected)));

	RecomputeStates();
	RefreshGridAndCounts();

	if (showValidation && !validationReport.IsEmpty()) {
		wxMessageBox(validationReport, "Leave-one-out validation", wxOK | wxICON_INFORMATION, this);
	}
}

void CarpetScanDialog::RecomputeStates() {
	// A carpet slot holds several item variants, so there is no per-slot winner:
	// every accepted row is assigned to its align.
	for (CarpetScanRow& row : m_rows) {
		if (row.scanned
			&& (row.result.status == CarpetScanResult::Status::TooLarge
				|| row.result.status == CarpetScanResult::Status::NoSprite)) {
			row.state = CarpetScanRow::State::Rejected;
			continue;
		}
		if (row.excluded) {
			row.state = CarpetScanRow::State::Excluded;
			continue;
		}
		if (!row.scanned) {
			row.state = CarpetScanRow::State::Pending;
			continue;
		}
		if (row.result.status == CarpetScanResult::Status::AlreadyInCarpet && !row.manual) {
			// Informative, not a hard rejection - excluded from auto-assignment unless
			// the user force-includes it via a manual align override.
			row.state = CarpetScanRow::State::AlreadyInCarpet;
			continue;
		}
		if (!row.manual && row.result.confidence < MIN_AUTO_CONFIDENCE) {
			row.state = CarpetScanRow::State::Pending;
			continue;
		}
		if (row.effectiveAlign().empty()) {
			row.state = CarpetScanRow::State::Pending;
			continue;
		}
		row.state = CarpetScanRow::State::Assigned;
	}
}

void CarpetScanDialog::RefreshGridAndCounts(int newlyAdded) {
	// Remember the selected ROW (not grid index) across the view rebuild.
	const int selectedRow = m_grid->RowIndexFor(m_grid->GetSelection());

	// Rebuild the filtered view from the current rows.
	std::vector<int> view;
	if (!m_filterAlign.empty()) {
		for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
			if (m_rows[i].effectiveAlign() == m_filterAlign) {
				view.push_back(i);
			}
		}
		std::stable_sort(view.begin(), view.end(), [this](int a, int b) {
			return RowScore(m_rows[a]) > RowScore(m_rows[b]);
		});
	}

	int gridIndex = -1;
	if (selectedRow >= 0 && selectedRow < static_cast<int>(m_rows.size())) {
		if (m_filterAlign.empty()) {
			gridIndex = selectedRow;
		} else {
			const auto it = std::find(view.begin(), view.end(), selectedRow);
			if (it != view.end()) {
				gridIndex = static_cast<int>(it - view.begin());
			}
		}
	}

	m_grid->SetView(std::move(view));
	m_grid->SetSelection(gridIndex);

	// Counts label.
	size_t assigned = 0;
	for (const CarpetScanRow& row : m_rows) {
		if (row.state == CarpetScanRow::State::Assigned) {
			++assigned;
		}
	}
	wxString label = wxString::Format("%lu candidates", static_cast<unsigned long>(m_rows.size()));
	if (newlyAdded >= 0) {
		label += wxString::Format(" (+%d new)", newlyAdded);
	}
	if (assigned > 0) {
		label += wxString::Format(" - %lu assigned", static_cast<unsigned long>(assigned));
	}
	m_countLabel->SetLabel(label);

	Layout();
	UpdateSelectedRowUI();
}

void CarpetScanDialog::OnRowSelected(int WXUNUSED(rowIndex)) {
	UpdateSelectedRowUI();
}

void CarpetScanDialog::UpdateSelectedRowUI() {
	const int rowIndex = m_grid->RowIndexFor(m_grid->GetSelection());
	if (rowIndex < 0 || rowIndex >= static_cast<int>(m_rows.size())) {
		m_rowInfoLabel->SetLabel("Select a result to review it.");
		m_updatingChoice = true;
		m_alignChoice->SetSelection(0);
		m_updatingChoice = false;
		m_alignChoice->Enable(false);
		Layout();
		return;
	}

	const CarpetScanRow& row = m_rows[rowIndex];
	m_alignChoice->Enable(true);

	wxString info = wxString::Format("item %u", static_cast<unsigned int>(row.result.itemId));
	if (row.scanned && !row.result.align.empty()) {
		info += wxString::Format(" - suggested %s (%.0f%%)", wxstr(row.result.align), row.result.confidence);
		if (!row.result.secondAlign.empty()) {
			info += wxString::Format(", 2nd %s (%.0f%%)", wxstr(row.result.secondAlign), row.result.secondConfidence);
		}
	}
	if (row.manual) {
		info += wxString::Format(" - manual %s", wxstr(row.manualAlign));
	}
	info += " - " + StateName(row);
	m_rowInfoLabel->SetLabel(info);

	// Programmatic selection must not fire OnAlignOverride.
	m_updatingChoice = true;
	if (row.excluded) {
		m_alignChoice->SetSelection(EXCLUDED_CHOICE_INDEX);
	} else if (row.manual) {
		int manualIdx = 0;
		for (size_t s = 0; s < CarpetClassifier::ALIGN_COUNT; ++s) {
			if (CarpetClassifier::ALIGN_NAMES[s] == row.manualAlign) {
				manualIdx = static_cast<int>(s) + 1;
				break;
			}
		}
		m_alignChoice->SetSelection(manualIdx);
	} else {
		m_alignChoice->SetSelection(0); // (auto)
	}
	m_updatingChoice = false;

	Layout();
}

void CarpetScanDialog::OnAlignOverride(wxCommandEvent& WXUNUSED(event)) {
	if (m_updatingChoice) {
		return;
	}

	const int rowIndex = m_grid->RowIndexFor(m_grid->GetSelection());
	if (rowIndex < 0 || rowIndex >= static_cast<int>(m_rows.size())) {
		return;
	}
	CarpetScanRow& row = m_rows[rowIndex];

	const int sel = m_alignChoice->GetSelection();
	if (sel <= 0) {
		// "(auto)"
		row.manual = false;
		row.manualAlign.clear();
		row.excluded = false;
	} else if (sel == EXCLUDED_CHOICE_INDEX) {
		row.excluded = true;
		row.manual = false;
		row.manualAlign.clear();
	} else {
		// Alignment name - also force-includes AlreadyInCarpet rows.
		row.manual = true;
		row.manualAlign = CarpetClassifier::ALIGN_NAMES[sel - 1];
		row.excluded = false;
	}

	RecomputeStates();
	RefreshGridAndCounts();
}

void CarpetScanDialog::OnCheckAlign(wxCommandEvent& WXUNUSED(event)) {
	const int sel = m_checkAlignChoice->GetSelection();
	if (sel < 0 || sel >= static_cast<int>(CarpetClassifier::ALIGN_COUNT)) {
		return;
	}
	m_filterAlign = CarpetClassifier::ALIGN_NAMES[sel];
	RefreshGridAndCounts();
	SetStatusMessage(wxString::Format("Showing only '%s' rows, best confidence first.", wxstr(m_filterAlign)));
}

void CarpetScanDialog::OnShowAll(wxCommandEvent& WXUNUSED(event)) {
	if (m_filterAlign.empty()) {
		return;
	}
	m_filterAlign.clear();
	RefreshGridAndCounts();
	SetStatusMessage("Showing all candidates.");
}

void CarpetScanDialog::OnApply(wxCommandEvent& WXUNUSED(event)) {
	m_assignments.clear();
	for (const CarpetScanRow& row : m_rows) {
		if (row.state != CarpetScanRow::State::Assigned) {
			continue;
		}
		// The single align-name <-> BorderType conversion point: always through
		// the canonical align-name string, never a numeric cast.
		const BorderType align = carpetAlignFromString(row.effectiveAlign());
		if (align == BORDER_NONE) {
			continue;
		}
		m_assignments[align].push_back(row.result.itemId);
	}

	if (m_assignments.empty()) {
		wxMessageBox("Nothing to apply - no align has an assigned item.", "Carpet Scan", wxOK | wxICON_INFORMATION, this);
		return;
	}

	EndModal(wxID_OK);
}
