//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Wall Scan Dialog - classify candidate items into wall segments by
// sprite shape (kNN over the wall brushes already loaded in g_brushes).
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/dialogs/wall_scan_dialog.h"
#include "brushes/wall/wall_classifier.h"
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
	ID_WALL_SCAN_ADD = wxID_HIGHEST + 5300,
	ID_WALL_SCAN_FROM_SELECTION,
	ID_WALL_SCAN_CLEAR,
	ID_WALL_SCAN_AUTO_DETECT,
	ID_WALL_SCAN_SEGMENT_CHOICE,
	ID_WALL_SCAN_APPLY,
	ID_WALL_SCAN_CHECK_SEGMENT_CHOICE,
	ID_WALL_SCAN_CHECK_SEGMENT,
	ID_WALL_SCAN_SHOW_ALL,
};

constexpr int MAX_IDS_PER_ADD = 2000;
constexpr long MIN_ITEM_ID = 100;
constexpr long MAX_ITEM_ID = 65535;

// Across 16 segments the smallest winning share for K=5 is 2 votes (~40%), and a
// fully split vote gives 20% each. Below this a row stays Pending so the user reviews
// it instead of it silently landing in a segment.
constexpr float MIN_AUTO_CONFIDENCE = 30.0f;

// Index of the "(excluded)" entry in the override choice: after "(auto)" + 4 segments.
constexpr int EXCLUDED_CHOICE_INDEX = 1 + static_cast<int>(WallClassifier::SEGMENT_COUNT);

// Effective score used for the filtered view sort. Manual overrides count as 101.
float RowScore(const WallScanRow& row) {
	return row.manual ? 101.0f : row.result.confidence;
}

// Canonical names like "southeast diagonal" do not fit a 96px grid cell; the full name
// still shows in the tooltip, the review line and the override dropdown.
wxString ShortSegmentName(const std::string& segment) {
	if (segment == "horizontal") return "horiz";
	if (segment == "vertical") return "vert";
	if (segment == "corner") return "cnr NW";
	if (segment == "northeast diagonal") return "cnr NE";
	if (segment == "southwest diagonal") return "cnr SW";
	if (segment == "southeast diagonal") return "cnr SE";
	if (segment == "north T") return "T-N";
	if (segment == "south T") return "T-S";
	if (segment == "east T") return "T-E";
	if (segment == "west T") return "T-W";
	if (segment == "intersection") return "cross";
	if (segment == "north end") return "end-N";
	if (segment == "south end") return "end-S";
	if (segment == "east end") return "end-E";
	if (segment == "west end") return "end-W";
	return wxstr(segment);
}

wxString StateName(const WallScanRow& row) {
	switch (row.state) {
		case WallScanRow::State::Pending:
			return row.scanned ? "pending" : "not scanned";
		case WallScanRow::State::Assigned:
			return "assigned";
		case WallScanRow::State::Excluded:
			return "excluded";
		case WallScanRow::State::AlreadyInWall:
			return wxString::Format("already in wall '%s'", wxstr(row.result.existingWallName));
		case WallScanRow::State::Rejected:
			return row.result.status == WallScanResult::Status::TooLarge
				? "rejected (sprite larger than 2x2)"
				: "rejected (no sprite)";
	}
	return "";
}

// Second text line inside a grid cell.
wxString RowLine2(const WallScanRow& row) {
	switch (row.state) {
		case WallScanRow::State::Rejected:
			return row.result.status == WallScanResult::Status::TooLarge ? "too large" : "no sprite";
		case WallScanRow::State::Excluded:
			return "excluded";
		case WallScanRow::State::AlreadyInWall:
			return "in a wall";
		default:
			break;
	}
	if (!row.scanned) {
		return "not scanned";
	}
	const std::string segment = row.effectiveSegment();
	if (segment.empty()) {
		return "no match";
	}
	wxString text = wxString::Format("%s %.0f%%", ShortSegmentName(segment), row.result.confidence);
	if (row.manual) {
		text += "*";
	}
	return text;
}

// 2px status border color, mirroring the Border Scan styling.
wxColour StateBorderColour(const WallScanRow& row) {
	switch (row.state) {
		case WallScanRow::State::Assigned:
			return (!row.manual && row.result.confidence < 55.0f)
				? Theme::Get(Theme::Role::Warning)
				: Theme::Get(Theme::Role::Success);
		case WallScanRow::State::Pending:
			return Theme::Get(Theme::Role::Border);
		case WallScanRow::State::Excluded:
		case WallScanRow::State::AlreadyInWall:
			return Theme::Get(Theme::Role::TextSubtle);
		case WallScanRow::State::Rejected:
			return Theme::Get(Theme::Role::Error);
	}
	return Theme::Get(Theme::Role::Border);
}

} // namespace

// ============================================================================
// WallScanRow
// ============================================================================

std::string WallScanRow::effectiveSegment() const {
	return manual ? manualSegment : result.segment;
}

// ============================================================================
// WallScanGrid
// ============================================================================

WallScanGrid::WallScanGrid(wxWindow* parent, std::vector<WallScanRow>* rows) :
	VirtualItemGrid(parent, wxID_ANY),
	m_rows(rows) {
	m_itemSize = 96;
	m_padding = 6;

	const wxColour bg = Theme::Get(Theme::Role::Background);
	m_bgRed = bg.Red() / 255.0f;
	m_bgGreen = bg.Green() / 255.0f;
	m_bgBlue = bg.Blue() / 255.0f;
}

size_t WallScanGrid::GetItemCount() const {
	if (!m_rows) {
		return 0;
	}
	return m_view.empty() ? m_rows->size() : m_view.size();
}

int WallScanGrid::RowIndexFor(int gridIndex) const {
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

uint16_t WallScanGrid::GetItem(size_t index) const {
	const int rowIndex = RowIndexFor(static_cast<int>(index));
	return rowIndex >= 0 ? (*m_rows)[rowIndex].result.itemId : 0;
}

void WallScanGrid::SetView(std::vector<int> view) {
	m_view = std::move(view);
	RefreshGrid();
}

void WallScanGrid::OnItemSelected(int index) {
	if (m_onSelected) {
		m_onSelected(RowIndexFor(index));
	}
}

wxString WallScanGrid::GetItemName(size_t index) const {
	const int rowIndex = RowIndexFor(static_cast<int>(index));
	if (rowIndex < 0) {
		return "";
	}
	const WallScanRow& row = (*m_rows)[rowIndex];

	wxString tip = wxString::Format("Item %u", static_cast<unsigned int>(row.result.itemId));
	const auto definition = g_item_definitions.get(row.result.itemId);
	if (definition) {
		const wxString name = wxstr(definition.name());
		if (!name.IsEmpty()) {
			tip += " - " + name;
		}
	}
	if (row.scanned && !row.result.segment.empty()) {
		tip += wxString::Format("\nSuggested: %s (%.0f%%)", wxstr(row.result.segment), row.result.confidence);
		if (!row.result.secondSegment.empty()) {
			tip += wxString::Format(", 2nd: %s (%.0f%%)", wxstr(row.result.secondSegment), row.result.secondConfidence);
		}
	}
	if (row.result.status == WallScanResult::Status::AlreadyInWall) {
		tip += wxString::Format("\nAlready used by wall brush '%s'", wxstr(row.result.existingWallName));
		if (!row.result.existingWallSegment.empty()) {
			tip += wxString::Format(" as %s", wxstr(row.result.existingWallSegment));
		}
	}
	if (row.manual) {
		tip += wxString::Format("\nManual override: %s", wxstr(row.manualSegment));
	}
	if (row.excluded) {
		tip += "\nExcluded";
	}
	tip += "\nStatus: " + StateName(row);
	return tip;
}

void WallScanGrid::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
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
		const WallScanRow& row = (*m_rows)[rowIndex];

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

		// Sprite (top-centered). Wall sprites are often 32x64, so the drawn box keeps
		// the sprite aspect ratio instead of squashing it into a square.
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

		// Line 2: segment/confidence or status
		const wxString line2 = RowLine2(row);
		nvgFontSize(vg, 11.0f);
		if (isSelected) {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextOnAccent)));
		} else if (row.state == WallScanRow::State::Pending) {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle)));
		} else {
			nvgFillColor(vg, NvgUtils::ToNvColor(borderCol));
		}
		nvgText(vg, x + w / 2.0f, y + 66.0f, line2.mb_str(), nullptr);
	}
}

// ============================================================================
// WallScanDialog
// ============================================================================

WallScanDialog::WallScanDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Wall Scan", wxDefaultPosition, wxDefaultSize,
			 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {

	SetBackgroundColour(Theme::Get(Theme::Role::Surface));

	wxBoxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	// --- 1. Candidates row ---
	wxBoxSizer* candidatesRow = newd wxBoxSizer(wxHORIZONTAL);
	candidatesRow->Add(newd wxStaticText(this, wxID_ANY, "Candidates:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	m_candidateInput = newd wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	m_candidateInput->SetHint("e.g. 55550-55561, 60000");
	m_candidateInput->SetToolTip("Comma-separated item IDs and ranges (A-B)");
	candidatesRow->Add(m_candidateInput, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	wxButton* addButton = newd wxButton(this, ID_WALL_SCAN_ADD, "Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	addButton->SetToolTip("Add the typed IDs/ranges to the candidate list");
	candidatesRow->Add(addButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	m_fromSelectionButton = newd wxButton(this, ID_WALL_SCAN_FROM_SELECTION, "From Map Selection", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_fromSelectionButton->SetToolTip("Collect unique item IDs from the current map selection");
	candidatesRow->Add(m_fromSelectionButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	wxButton* clearButton = newd wxButton(this, ID_WALL_SCAN_CLEAR, "Clear", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	clearButton->SetToolTip("Remove all candidates");
	candidatesRow->Add(clearButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	m_countLabel = newd wxStaticText(this, wxID_ANY, "0 candidates");
	m_countLabel->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	candidatesRow->Add(m_countLabel, 0, wxALIGN_CENTER_VERTICAL);

	sizer->Add(candidatesRow, 0, wxEXPAND | wxALL, 8);

	// --- 2. Detect row + "Check segment" filter ---
	wxBoxSizer* detectRow = newd wxBoxSizer(wxHORIZONTAL);

	wxButton* detectButton = newd wxButton(this, ID_WALL_SCAN_AUTO_DETECT, "Auto-Detect");
	detectButton->SetToolTip("Classify all candidates by sprite shape (Shift+click also shows a validation report)");
	detectRow->Add(detectButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	m_statusLabel = newd wxStaticText(this, wxID_ANY, "Add candidate item IDs, then click Auto-Detect.",
									  wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	m_statusLabel->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	detectRow->Add(m_statusLabel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	detectRow->Add(newd wxStaticText(this, wxID_ANY, "Check segment:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	wxArrayString segmentNames;
	for (const std::string& segment : WallClassifier::SEGMENT_NAMES) {
		segmentNames.Add(wxstr(segment));
	}
	m_checkSegmentChoice = newd wxChoice(this, ID_WALL_SCAN_CHECK_SEGMENT_CHOICE, wxDefaultPosition, wxDefaultSize, segmentNames);
	m_checkSegmentChoice->SetSelection(0);
	m_checkSegmentChoice->SetToolTip("Filter the grid to one segment, sorted by confidence");
	detectRow->Add(m_checkSegmentChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	wxButton* checkButton = newd wxButton(this, ID_WALL_SCAN_CHECK_SEGMENT, "Check", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	checkButton->SetToolTip("Show only rows classified as the chosen segment (best confidence first)");
	detectRow->Add(checkButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	wxButton* showAllButton = newd wxButton(this, ID_WALL_SCAN_SHOW_ALL, "Show All", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	showAllButton->SetToolTip("Remove the segment filter");
	detectRow->Add(showAllButton, 0, wxALIGN_CENTER_VERTICAL);

	sizer->Add(detectRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	// --- 3. Results grid ---
	m_grid = newd WallScanGrid(this, &m_rows);
	m_grid->SetSelectionCallback([this](int rowIndex) { OnRowSelected(rowIndex); });
	sizer->Add(m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

	// --- 4. Selected-row line ---
	wxBoxSizer* reviewRow = newd wxBoxSizer(wxHORIZONTAL);

	m_rowInfoLabel = newd wxStaticText(this, wxID_ANY, "Select a result to review it.",
									   wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	m_rowInfoLabel->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	reviewRow->Add(m_rowInfoLabel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	reviewRow->Add(newd wxStaticText(this, wxID_ANY, "Segment:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	wxArrayString overrideEntries;
	overrideEntries.Add("(auto)");
	for (const std::string& segment : WallClassifier::SEGMENT_NAMES) {
		overrideEntries.Add(wxstr(segment));
	}
	overrideEntries.Add("(excluded)");
	m_segmentChoice = newd wxChoice(this, ID_WALL_SCAN_SEGMENT_CHOICE, wxDefaultPosition, wxDefaultSize, overrideEntries);
	m_segmentChoice->SetSelection(0);
	m_segmentChoice->Enable(false);
	m_segmentChoice->SetToolTip("Override the detected segment, or exclude the item from Apply");
	reviewRow->Add(m_segmentChoice, 0, wxALIGN_CENTER_VERTICAL);

	sizer->Add(reviewRow, 0, wxEXPAND | wxALL, 8);

	// --- 5. Bottom buttons ---
	wxBoxSizer* buttonRow = newd wxBoxSizer(wxHORIZONTAL);
	buttonRow->AddStretchSpacer(1);

	wxButton* applyButton = newd wxButton(this, ID_WALL_SCAN_APPLY, "Apply to Wall");
	applyButton->SetToolTip("Add the assigned items to their segments in the Wall editor");
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
	addButton->Bind(wxEVT_BUTTON, &WallScanDialog::OnAddCandidates, this);
	m_candidateInput->Bind(wxEVT_TEXT_ENTER, &WallScanDialog::OnAddCandidates, this);
	m_fromSelectionButton->Bind(wxEVT_BUTTON, &WallScanDialog::OnFromMapSelection, this);
	clearButton->Bind(wxEVT_BUTTON, &WallScanDialog::OnClearCandidates, this);
	detectButton->Bind(wxEVT_BUTTON, &WallScanDialog::OnAutoDetect, this);
	m_segmentChoice->Bind(wxEVT_CHOICE, &WallScanDialog::OnSegmentOverride, this);
	checkButton->Bind(wxEVT_BUTTON, &WallScanDialog::OnCheckSegment, this);
	showAllButton->Bind(wxEVT_BUTTON, &WallScanDialog::OnShowAll, this);
	applyButton->Bind(wxEVT_BUTTON, &WallScanDialog::OnApply, this);

	// --- Guards ---
	if (g_gui.GetCurrentEditor() == nullptr) {
		m_fromSelectionButton->Enable(false);
		m_fromSelectionButton->SetToolTip("Open a map to collect items from the selection");
	}
}

void WallScanDialog::SetStatusMessage(const wxString& text, bool isError) {
	m_statusLabel->SetLabel(text);
	m_statusLabel->SetForegroundColour(
		Theme::Get(isError ? Theme::Role::Error : Theme::Role::TextSubtle));
	m_statusLabel->Refresh();
	Layout();
}

bool WallScanDialog::ParseCandidateText(const wxString& text, std::vector<uint16_t>& outIds, wxString& error) const {
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

size_t WallScanDialog::AddCandidateIds(const std::vector<uint16_t>& ids) {
	std::set<uint16_t> existing;
	for (const WallScanRow& row : m_rows) {
		existing.insert(row.result.itemId);
	}

	size_t added = 0;
	for (uint16_t id : ids) {
		if (!existing.insert(id).second) {
			continue; // silently skip duplicates (within the batch and against existing rows)
		}
		WallScanRow row;
		row.result.itemId = id;
		m_rows.push_back(row);
		++added;
	}
	return added;
}

void WallScanDialog::OnAddCandidates(wxCommandEvent& WXUNUSED(event)) {
	wxString text = m_candidateInput->GetValue();
	text.Trim(true).Trim(false);
	if (text.IsEmpty()) {
		SetStatusMessage("Type item IDs or ranges first (e.g. 55550-55561, 60000).", true);
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

void WallScanDialog::OnFromMapSelection(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		wxMessageBox("Open a map to collect items from the selection.", "Wall Scan", wxOK | wxICON_INFORMATION, this);
		return;
	}

	const std::vector<Tile*>& tiles = editor->selection.getTiles();
	if (tiles.empty()) {
		wxMessageBox("The map selection is empty.", "Wall Scan", wxOK | wxICON_INFORMATION, this);
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
		wxMessageBox("No items found in the selection.", "Wall Scan", wxOK | wxICON_INFORMATION, this);
		return;
	}

	const size_t added = AddCandidateIds(ids);
	RecomputeStates();
	RefreshGridAndCounts(static_cast<int>(added));
}

void WallScanDialog::OnClearCandidates(wxCommandEvent& WXUNUSED(event)) {
	m_rows.clear();
	RecomputeStates();
	RefreshGridAndCounts();
	SetStatusMessage("Add candidate item IDs, then click Auto-Detect.");
}

void WallScanDialog::OnAutoDetect(wxCommandEvent& WXUNUSED(event)) {
	if (m_rows.empty()) {
		SetStatusMessage("Add candidate item IDs first.", true);
		return;
	}

	const bool showValidation = wxGetKeyState(WXK_SHIFT);

	WallClassifier& classifier = WallClassifier::Get();
	wxString validationReport;
	{
		wxBusyCursor busy;
		if (!classifier.ensureTrained()) {
			wxMessageBox("No training data: no wall brushes are loaded.", "Wall Scan", wxOK | wxICON_WARNING, this);
			return;
		}

		std::vector<uint16_t> ids;
		ids.reserve(m_rows.size());
		for (const WallScanRow& row : m_rows) {
			ids.push_back(row.result.itemId);
		}

		const std::vector<WallScanResult> results = classifier.classify(ids);
		for (size_t i = 0; i < m_rows.size() && i < results.size(); ++i) {
			m_rows[i].result = results[i];
			m_rows[i].scanned = true;
			// Re-detect clears manual overrides.
			m_rows[i].manual = false;
			m_rows[i].manualSegment.clear();
			m_rows[i].excluded = false;
		}

		if (showValidation) {
			validationReport = wxstr(classifier.validateLeaveOneOut());
		}
	}

	size_t rejected = 0;
	for (const WallScanRow& row : m_rows) {
		if (row.result.status == WallScanResult::Status::TooLarge
			|| row.result.status == WallScanResult::Status::NoSprite) {
			++rejected;
		}
	}

	SetStatusMessage(wxString::Format("Trained on %lu samples from %lu wall brushes (%lu rejected).",
									  static_cast<unsigned long>(classifier.sampleCount()),
									  static_cast<unsigned long>(classifier.groupCount()),
									  static_cast<unsigned long>(rejected)));

	RecomputeStates();
	RefreshGridAndCounts();

	if (showValidation && !validationReport.IsEmpty()) {
		wxMessageBox(validationReport, "Leave-one-out validation", wxOK | wxICON_INFORMATION, this);
	}
}

void WallScanDialog::RecomputeStates() {
	// A wall segment holds several item variants, so there is no per-segment winner:
	// every accepted row is assigned to its segment.
	for (WallScanRow& row : m_rows) {
		if (row.scanned
			&& (row.result.status == WallScanResult::Status::TooLarge
				|| row.result.status == WallScanResult::Status::NoSprite)) {
			row.state = WallScanRow::State::Rejected;
			continue;
		}
		if (row.excluded) {
			row.state = WallScanRow::State::Excluded;
			continue;
		}
		if (!row.scanned) {
			row.state = WallScanRow::State::Pending;
			continue;
		}
		if (row.result.status == WallScanResult::Status::AlreadyInWall && !row.manual) {
			// Informative, not a hard rejection - excluded from auto-assignment unless
			// the user force-includes it via a manual segment override.
			row.state = WallScanRow::State::AlreadyInWall;
			continue;
		}
		if (!row.manual && row.result.confidence < MIN_AUTO_CONFIDENCE) {
			row.state = WallScanRow::State::Pending;
			continue;
		}
		if (row.effectiveSegment().empty()) {
			row.state = WallScanRow::State::Pending;
			continue;
		}
		row.state = WallScanRow::State::Assigned;
	}
}

void WallScanDialog::RefreshGridAndCounts(int newlyAdded) {
	// Remember the selected ROW (not grid index) across the view rebuild.
	const int selectedRow = m_grid->RowIndexFor(m_grid->GetSelection());

	// Rebuild the filtered view from the current rows.
	std::vector<int> view;
	if (!m_filterSegment.empty()) {
		for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
			if (m_rows[i].effectiveSegment() == m_filterSegment) {
				view.push_back(i);
			}
		}
		std::stable_sort(view.begin(), view.end(), [this](int a, int b) {
			return RowScore(m_rows[a]) > RowScore(m_rows[b]);
		});
	}

	int gridIndex = -1;
	if (selectedRow >= 0 && selectedRow < static_cast<int>(m_rows.size())) {
		if (m_filterSegment.empty()) {
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
	for (const WallScanRow& row : m_rows) {
		if (row.state == WallScanRow::State::Assigned) {
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

void WallScanDialog::OnRowSelected(int WXUNUSED(rowIndex)) {
	UpdateSelectedRowUI();
}

void WallScanDialog::UpdateSelectedRowUI() {
	const int rowIndex = m_grid->RowIndexFor(m_grid->GetSelection());
	if (rowIndex < 0 || rowIndex >= static_cast<int>(m_rows.size())) {
		m_rowInfoLabel->SetLabel("Select a result to review it.");
		m_updatingChoice = true;
		m_segmentChoice->SetSelection(0);
		m_updatingChoice = false;
		m_segmentChoice->Enable(false);
		Layout();
		return;
	}

	const WallScanRow& row = m_rows[rowIndex];
	m_segmentChoice->Enable(true);

	wxString info = wxString::Format("item %u", static_cast<unsigned int>(row.result.itemId));
	if (row.scanned && !row.result.segment.empty()) {
		info += wxString::Format(" - suggested %s (%.0f%%)", wxstr(row.result.segment), row.result.confidence);
		if (!row.result.secondSegment.empty()) {
			info += wxString::Format(", 2nd %s (%.0f%%)", wxstr(row.result.secondSegment), row.result.secondConfidence);
		}
	}
	if (row.manual) {
		info += wxString::Format(" - manual %s", wxstr(row.manualSegment));
	}
	info += " - " + StateName(row);
	m_rowInfoLabel->SetLabel(info);

	// Programmatic selection must not fire OnSegmentOverride.
	m_updatingChoice = true;
	if (row.excluded) {
		m_segmentChoice->SetSelection(EXCLUDED_CHOICE_INDEX);
	} else if (row.manual) {
		int manualIdx = 0;
		for (size_t s = 0; s < WallClassifier::SEGMENT_COUNT; ++s) {
			if (WallClassifier::SEGMENT_NAMES[s] == row.manualSegment) {
				manualIdx = static_cast<int>(s) + 1;
				break;
			}
		}
		m_segmentChoice->SetSelection(manualIdx);
	} else {
		m_segmentChoice->SetSelection(0); // (auto)
	}
	m_updatingChoice = false;

	Layout();
}

void WallScanDialog::OnSegmentOverride(wxCommandEvent& WXUNUSED(event)) {
	if (m_updatingChoice) {
		return;
	}

	const int rowIndex = m_grid->RowIndexFor(m_grid->GetSelection());
	if (rowIndex < 0 || rowIndex >= static_cast<int>(m_rows.size())) {
		return;
	}
	WallScanRow& row = m_rows[rowIndex];

	const int sel = m_segmentChoice->GetSelection();
	if (sel <= 0) {
		// "(auto)"
		row.manual = false;
		row.manualSegment.clear();
		row.excluded = false;
	} else if (sel == EXCLUDED_CHOICE_INDEX) {
		row.excluded = true;
		row.manual = false;
		row.manualSegment.clear();
	} else {
		// Segment name - also force-includes AlreadyInWall rows.
		row.manual = true;
		row.manualSegment = WallClassifier::SEGMENT_NAMES[sel - 1];
		row.excluded = false;
	}

	RecomputeStates();
	RefreshGridAndCounts();
}

void WallScanDialog::OnCheckSegment(wxCommandEvent& WXUNUSED(event)) {
	const int sel = m_checkSegmentChoice->GetSelection();
	if (sel < 0 || sel >= static_cast<int>(WallClassifier::SEGMENT_COUNT)) {
		return;
	}
	m_filterSegment = WallClassifier::SEGMENT_NAMES[sel];
	RefreshGridAndCounts();
	SetStatusMessage(wxString::Format("Showing only '%s' rows, best confidence first.", wxstr(m_filterSegment)));
}

void WallScanDialog::OnShowAll(wxCommandEvent& WXUNUSED(event)) {
	if (m_filterSegment.empty()) {
		return;
	}
	m_filterSegment.clear();
	RefreshGridAndCounts();
	SetStatusMessage("Showing all candidates.");
}

void WallScanDialog::OnApply(wxCommandEvent& WXUNUSED(event)) {
	m_assignments.clear();
	for (const WallScanRow& row : m_rows) {
		if (row.state != WallScanRow::State::Assigned) {
			continue;
		}
		// The single segment-name <-> WallSegmentType conversion point: always through
		// the canonical segment-name string, never a numeric cast.
		const WallSegmentType seg = wallSegmentFromString(row.effectiveSegment());
		if (seg == WALL_SEG_COUNT) {
			continue;
		}
		m_assignments[seg].push_back(row.result.itemId);
	}

	if (m_assignments.empty()) {
		wxMessageBox("Nothing to apply - no segment has an assigned item.", "Wall Scan", wxOK | wxICON_INFORMATION, this);
		return;
	}

	EndModal(wxID_OK);
}
