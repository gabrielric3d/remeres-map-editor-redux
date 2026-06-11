//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Border Scan Dialog - classify candidate items into border edges by
// sprite shape (kNN over the borders already loaded in g_brushes).
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/dialogs/border_scan_dialog.h"
#include "brushes/ground/border_classifier.h"
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
	ID_SCAN_ADD = wxID_HIGHEST + 5200,
	ID_SCAN_FROM_SELECTION,
	ID_SCAN_CLEAR,
	ID_SCAN_AUTO_DETECT,
	ID_SCAN_EDGE_CHOICE,
	ID_SCAN_APPLY,
	ID_SCAN_CHECK_EDGE_CHOICE,
	ID_SCAN_CHECK_EDGE,
	ID_SCAN_SHOW_ALL,
};

constexpr int MAX_IDS_PER_ADD = 2000;
constexpr long MIN_ITEM_ID = 100;
constexpr long MAX_ITEM_ID = 65535;

// Index of the "(excluded)" entry in the override choice: after "(auto)" + 12 edges.
constexpr int EXCLUDED_CHOICE_INDEX = 1 + static_cast<int>(BorderClassifier::EDGE_COUNT);

// Effective score used for conflict resolution and the filtered view sort.
// Manual overrides count as confidence 101.
float RowScore(const BorderScanRow& row) {
	return row.manual ? 101.0f : row.result.confidence;
}

wxString StateName(const BorderScanRow& row) {
	switch (row.state) {
		case BorderScanRow::State::Pending:
			return row.scanned ? "pending" : "not scanned";
		case BorderScanRow::State::Assigned:
			return "assigned";
		case BorderScanRow::State::Duplicate:
			return "duplicate";
		case BorderScanRow::State::Excluded:
			return "excluded";
		case BorderScanRow::State::AlreadyInBorder:
			return wxString::Format("already in border %u", row.result.existingBorderId);
		case BorderScanRow::State::Rejected:
			return row.result.status == BorderScanResult::Status::MultiTile
				? "rejected (multi-tile)"
				: "rejected (no sprite)";
	}
	return "";
}

// Second text line inside a grid cell.
wxString RowLine2(const BorderScanRow& row) {
	switch (row.state) {
		case BorderScanRow::State::Rejected:
			return row.result.status == BorderScanResult::Status::MultiTile ? "multi-tile" : "no sprite";
		case BorderScanRow::State::Excluded:
			return "excluded";
		case BorderScanRow::State::AlreadyInBorder:
			return wxString::Format("in border %u", row.result.existingBorderId);
		case BorderScanRow::State::Duplicate:
			return "duplicate";
		default:
			break;
	}
	if (!row.scanned) {
		return "not scanned";
	}
	const std::string edge = row.effectiveEdge();
	if (edge.empty()) {
		return "no match";
	}
	wxString text = wxString::Format("%s %.0f%%", wxString(edge), row.result.confidence);
	if (row.manual) {
		text += "*";
	}
	return text;
}

// 2px status border color per Design Decision 7 styling.
wxColour StateBorderColour(const BorderScanRow& row) {
	switch (row.state) {
		case BorderScanRow::State::Assigned:
			return (!row.manual && row.result.confidence < 50.0f)
				? Theme::Get(Theme::Role::Warning)
				: Theme::Get(Theme::Role::Success);
		case BorderScanRow::State::Duplicate:
			return Theme::Get(Theme::Role::Warning);
		case BorderScanRow::State::Pending:
			return Theme::Get(Theme::Role::Border);
		case BorderScanRow::State::Excluded:
		case BorderScanRow::State::AlreadyInBorder:
			return Theme::Get(Theme::Role::TextSubtle);
		case BorderScanRow::State::Rejected:
			return Theme::Get(Theme::Role::Error);
	}
	return Theme::Get(Theme::Role::Border);
}

} // namespace

// ============================================================================
// BorderScanRow
// ============================================================================

std::string BorderScanRow::effectiveEdge() const {
	return manual ? manualEdge : result.edge;
}

// ============================================================================
// BorderScanGrid
// ============================================================================

BorderScanGrid::BorderScanGrid(wxWindow* parent, std::vector<BorderScanRow>* rows) :
	VirtualItemGrid(parent, wxID_ANY),
	m_rows(rows) {
	m_itemSize = 96;
	m_padding = 6;

	const wxColour bg = Theme::Get(Theme::Role::Background);
	m_bgRed = bg.Red() / 255.0f;
	m_bgGreen = bg.Green() / 255.0f;
	m_bgBlue = bg.Blue() / 255.0f;
}

size_t BorderScanGrid::GetItemCount() const {
	if (!m_rows) {
		return 0;
	}
	return m_view.empty() ? m_rows->size() : m_view.size();
}

int BorderScanGrid::RowIndexFor(int gridIndex) const {
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

uint16_t BorderScanGrid::GetItem(size_t index) const {
	const int rowIndex = RowIndexFor(static_cast<int>(index));
	return rowIndex >= 0 ? (*m_rows)[rowIndex].result.itemId : 0;
}

void BorderScanGrid::SetView(std::vector<int> view) {
	m_view = std::move(view);
	RefreshGrid();
}

void BorderScanGrid::OnItemSelected(int index) {
	if (m_onSelected) {
		m_onSelected(RowIndexFor(index));
	}
}

wxString BorderScanGrid::GetItemName(size_t index) const {
	const int rowIndex = RowIndexFor(static_cast<int>(index));
	if (rowIndex < 0) {
		return "";
	}
	const BorderScanRow& row = (*m_rows)[rowIndex];

	wxString tip = wxString::Format("Item %u", static_cast<unsigned int>(row.result.itemId));
	const auto definition = g_item_definitions.get(row.result.itemId);
	if (definition) {
		const wxString name = wxstr(definition.name());
		if (!name.IsEmpty()) {
			tip += " — " + name;
		}
	}
	if (row.scanned && !row.result.edge.empty()) {
		tip += wxString::Format("\nSuggested: %s (%.0f%%)", wxString(row.result.edge), row.result.confidence);
		if (!row.result.secondEdge.empty()) {
			tip += wxString::Format(", 2nd: %s (%.0f%%)", wxString(row.result.secondEdge), row.result.secondConfidence);
		}
	}
	if (row.result.status == BorderScanResult::Status::AlreadyInBorder) {
		tip += wxString::Format("\nAlready in border %u", row.result.existingBorderId);
	}
	if (row.manual) {
		tip += wxString::Format("\nManual override: %s", wxString(row.manualEdge));
	}
	if (row.excluded) {
		tip += "\nExcluded";
	}
	tip += "\nStatus: " + StateName(row);
	return tip;
}

void BorderScanGrid::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
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
		const BorderScanRow& row = (*m_rows)[rowIndex];

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

		// Sprite (32x32, top-centered)
		const int tex = GetOrCreateItemTexture(vg, row.result.itemId);
		if (tex > 0) {
			const float iconSize = 32.0f;
			const float dx = x + (w - iconSize) / 2.0f;
			const float dy = y + 8.0f;
			NVGpaint imgPaint = nvgImagePattern(vg, dx, dy, iconSize, iconSize, 0.0f, tex, 1.0f);
			nvgBeginPath(vg);
			nvgRect(vg, dx, dy, iconSize, iconSize);
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
		nvgText(vg, x + w / 2.0f, y + 46.0f, idText.c_str(), nullptr);

		// Line 2: edge/confidence or status
		const wxString line2 = RowLine2(row);
		nvgFontSize(vg, 11.0f);
		if (isSelected) {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextOnAccent)));
		} else if (row.state == BorderScanRow::State::Pending) {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle)));
		} else {
			nvgFillColor(vg, NvgUtils::ToNvColor(borderCol));
		}
		nvgText(vg, x + w / 2.0f, y + 62.0f, line2.mb_str(), nullptr);
	}
}

// ============================================================================
// BorderScanDialog
// ============================================================================

BorderScanDialog::BorderScanDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Border Scan", wxDefaultPosition, wxDefaultSize,
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

	wxButton* addButton = newd wxButton(this, ID_SCAN_ADD, "Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	addButton->SetToolTip("Add the typed IDs/ranges to the candidate list");
	candidatesRow->Add(addButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	m_fromSelectionButton = newd wxButton(this, ID_SCAN_FROM_SELECTION, "From Map Selection", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_fromSelectionButton->SetToolTip("Collect unique item IDs from the current map selection");
	candidatesRow->Add(m_fromSelectionButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	wxButton* clearButton = newd wxButton(this, ID_SCAN_CLEAR, "Clear", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	clearButton->SetToolTip("Remove all candidates");
	candidatesRow->Add(clearButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	m_countLabel = newd wxStaticText(this, wxID_ANY, "0 candidates");
	m_countLabel->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	candidatesRow->Add(m_countLabel, 0, wxALIGN_CENTER_VERTICAL);

	sizer->Add(candidatesRow, 0, wxEXPAND | wxALL, 8);

	// --- 2. Detect row + "Check Edge" filter ---
	wxBoxSizer* detectRow = newd wxBoxSizer(wxHORIZONTAL);

	wxButton* detectButton = newd wxButton(this, ID_SCAN_AUTO_DETECT, "Auto-Detect");
	detectButton->SetToolTip("Classify all candidates by sprite shape (Shift+click also shows a validation report)");
	detectRow->Add(detectButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	m_statusLabel = newd wxStaticText(this, wxID_ANY, "Add candidate item IDs, then click Auto-Detect.",
									  wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	m_statusLabel->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	detectRow->Add(m_statusLabel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	detectRow->Add(newd wxStaticText(this, wxID_ANY, "Check edge:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	wxArrayString edgeNames;
	for (const std::string& edge : BorderClassifier::EDGE_NAMES) {
		edgeNames.Add(wxString(edge));
	}
	m_checkEdgeChoice = newd wxChoice(this, ID_SCAN_CHECK_EDGE_CHOICE, wxDefaultPosition, wxDefaultSize, edgeNames);
	m_checkEdgeChoice->SetSelection(0);
	m_checkEdgeChoice->SetToolTip("Filter the grid to one edge, sorted by confidence");
	detectRow->Add(m_checkEdgeChoice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	wxButton* checkButton = newd wxButton(this, ID_SCAN_CHECK_EDGE, "Check", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	checkButton->SetToolTip("Show only rows classified as the chosen edge (best confidence first)");
	detectRow->Add(checkButton, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

	wxButton* showAllButton = newd wxButton(this, ID_SCAN_SHOW_ALL, "Show All", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	showAllButton->SetToolTip("Remove the edge filter");
	detectRow->Add(showAllButton, 0, wxALIGN_CENTER_VERTICAL);

	sizer->Add(detectRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	// --- 3. Results grid ---
	m_grid = newd BorderScanGrid(this, &m_rows);
	m_grid->SetSelectionCallback([this](int rowIndex) { OnRowSelected(rowIndex); });
	sizer->Add(m_grid, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

	// --- 4. Selected-row line ---
	wxBoxSizer* reviewRow = newd wxBoxSizer(wxHORIZONTAL);

	m_rowInfoLabel = newd wxStaticText(this, wxID_ANY, "Select a result to review it.",
									   wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	m_rowInfoLabel->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	reviewRow->Add(m_rowInfoLabel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	reviewRow->Add(newd wxStaticText(this, wxID_ANY, "Edge:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	wxArrayString overrideEntries;
	overrideEntries.Add("(auto)");
	for (const std::string& edge : BorderClassifier::EDGE_NAMES) {
		overrideEntries.Add(wxString(edge));
	}
	overrideEntries.Add("(excluded)");
	m_edgeChoice = newd wxChoice(this, ID_SCAN_EDGE_CHOICE, wxDefaultPosition, wxDefaultSize, overrideEntries);
	m_edgeChoice->SetSelection(0);
	m_edgeChoice->Enable(false);
	m_edgeChoice->SetToolTip("Override the detected edge, or exclude the item from Apply");
	reviewRow->Add(m_edgeChoice, 0, wxALIGN_CENTER_VERTICAL);

	sizer->Add(reviewRow, 0, wxEXPAND | wxALL, 8);

	// --- 5. Bottom buttons ---
	wxBoxSizer* buttonRow = newd wxBoxSizer(wxHORIZONTAL);
	buttonRow->AddStretchSpacer(1);

	wxButton* applyButton = newd wxButton(this, ID_SCAN_APPLY, "Apply to Border");
	applyButton->SetToolTip("Fill the Border Editor edges with the assigned items");
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
	addButton->Bind(wxEVT_BUTTON, &BorderScanDialog::OnAddCandidates, this);
	m_candidateInput->Bind(wxEVT_TEXT_ENTER, &BorderScanDialog::OnAddCandidates, this);
	m_fromSelectionButton->Bind(wxEVT_BUTTON, &BorderScanDialog::OnFromMapSelection, this);
	clearButton->Bind(wxEVT_BUTTON, &BorderScanDialog::OnClearCandidates, this);
	detectButton->Bind(wxEVT_BUTTON, &BorderScanDialog::OnAutoDetect, this);
	m_edgeChoice->Bind(wxEVT_CHOICE, &BorderScanDialog::OnEdgeOverride, this);
	checkButton->Bind(wxEVT_BUTTON, &BorderScanDialog::OnCheckEdge, this);
	showAllButton->Bind(wxEVT_BUTTON, &BorderScanDialog::OnShowAll, this);
	applyButton->Bind(wxEVT_BUTTON, &BorderScanDialog::OnApply, this);

	// --- Guards ---
	if (g_gui.GetCurrentEditor() == nullptr) {
		m_fromSelectionButton->Enable(false);
		m_fromSelectionButton->SetToolTip("Open a map to collect items from the selection");
	}
}

void BorderScanDialog::SetStatusMessage(const wxString& text, bool isError) {
	m_statusLabel->SetLabel(text);
	m_statusLabel->SetForegroundColour(
		Theme::Get(isError ? Theme::Role::Error : Theme::Role::TextSubtle));
	m_statusLabel->Refresh();
	Layout();
}

bool BorderScanDialog::ParseCandidateText(const wxString& text, std::vector<uint16_t>& outIds, wxString& error) const {
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
				error = wxString::Format("Invalid token '%s' — expected an item ID in [%ld, %ld].",
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
				error = wxString::Format("Invalid range '%s' — expected A-B with both in [%ld, %ld].",
										 token, MIN_ITEM_ID, MAX_ITEM_ID);
				return false;
			}
			if (first > last) {
				error = wxString::Format("Invalid range '%s' — start is greater than end.", token);
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

size_t BorderScanDialog::AddCandidateIds(const std::vector<uint16_t>& ids) {
	std::set<uint16_t> existing;
	for (const BorderScanRow& row : m_rows) {
		existing.insert(row.result.itemId);
	}

	size_t added = 0;
	for (uint16_t id : ids) {
		if (!existing.insert(id).second) {
			continue; // silently skip duplicates (within the batch and against existing rows)
		}
		BorderScanRow row;
		row.result.itemId = id;
		m_rows.push_back(row);
		++added;
	}
	return added;
}

void BorderScanDialog::OnAddCandidates(wxCommandEvent& WXUNUSED(event)) {
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

void BorderScanDialog::OnFromMapSelection(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		wxMessageBox("Open a map to collect items from the selection.", "Border Scan", wxOK | wxICON_INFORMATION, this);
		return;
	}

	const std::vector<Tile*>& tiles = editor->selection.getTiles();
	if (tiles.empty()) {
		wxMessageBox("The map selection is empty.", "Border Scan", wxOK | wxICON_INFORMATION, this);
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
		wxMessageBox("No items found in the selection.", "Border Scan", wxOK | wxICON_INFORMATION, this);
		return;
	}

	const size_t added = AddCandidateIds(ids);
	RecomputeStates();
	RefreshGridAndCounts(static_cast<int>(added));
}

void BorderScanDialog::OnClearCandidates(wxCommandEvent& WXUNUSED(event)) {
	m_rows.clear();
	RecomputeStates();
	RefreshGridAndCounts();
	SetStatusMessage("Add candidate item IDs, then click Auto-Detect.");
}

void BorderScanDialog::OnAutoDetect(wxCommandEvent& WXUNUSED(event)) {
	if (m_rows.empty()) {
		SetStatusMessage("Add candidate item IDs first.", true);
		return;
	}

	const bool showValidation = wxGetKeyState(WXK_SHIFT);

	BorderClassifier& classifier = BorderClassifier::Get();
	wxString validationReport;
	{
		wxBusyCursor busy;
		if (!classifier.ensureTrained()) {
			wxMessageBox("No training data: no borders are loaded.", "Border Scan", wxOK | wxICON_WARNING, this);
			return;
		}

		std::vector<uint16_t> ids;
		ids.reserve(m_rows.size());
		for (const BorderScanRow& row : m_rows) {
			ids.push_back(row.result.itemId);
		}

		const std::vector<BorderScanResult> results = classifier.classify(ids);
		for (size_t i = 0; i < m_rows.size() && i < results.size(); ++i) {
			m_rows[i].result = results[i];
			m_rows[i].scanned = true;
			// Re-detect clears manual overrides.
			m_rows[i].manual = false;
			m_rows[i].manualEdge.clear();
			m_rows[i].excluded = false;
		}

		if (showValidation) {
			validationReport = wxString(classifier.validateLeaveOneOut());
		}
	}

	size_t rejected = 0;
	for (const BorderScanRow& row : m_rows) {
		if (row.result.status == BorderScanResult::Status::MultiTile
			|| row.result.status == BorderScanResult::Status::NoSprite) {
			++rejected;
		}
	}

	SetStatusMessage(wxString::Format("Trained on %lu samples from %lu border groups (%lu rejected).",
									  static_cast<unsigned long>(classifier.sampleCount()),
									  static_cast<unsigned long>(classifier.groupCount()),
									  static_cast<unsigned long>(rejected)));

	RecomputeStates();
	RefreshGridAndCounts();

	if (showValidation && !validationReport.IsEmpty()) {
		wxMessageBox(validationReport, "Leave-one-out validation", wxOK | wxICON_INFORMATION, this);
	}
}

void BorderScanDialog::RecomputeStates() {
	// First pass: base state per row.
	for (BorderScanRow& row : m_rows) {
		if (row.scanned
			&& (row.result.status == BorderScanResult::Status::MultiTile
				|| row.result.status == BorderScanResult::Status::NoSprite)) {
			row.state = BorderScanRow::State::Rejected;
			continue;
		}
		if (row.excluded) {
			row.state = BorderScanRow::State::Excluded;
			continue;
		}
		if (!row.scanned) {
			row.state = BorderScanRow::State::Pending;
			continue;
		}
		if (row.result.status == BorderScanResult::Status::AlreadyInBorder && !row.manual) {
			// Informative, not a hard rejection — excluded from auto-assignment
			// unless the user force-includes it via a manual edge override.
			row.state = BorderScanRow::State::AlreadyInBorder;
			continue;
		}
		if (!row.manual && row.result.confidence < 25.0f) {
			row.state = BorderScanRow::State::Pending;
			continue;
		}
		if (row.effectiveEdge().empty()) {
			row.state = BorderScanRow::State::Pending;
			continue;
		}
		row.state = BorderScanRow::State::Assigned; // provisional; conflicts resolved below
	}

	// Second pass: per edge, the highest score stays Assigned, the rest become Duplicate.
	std::map<std::string, int> bestRowPerEdge;
	for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
		BorderScanRow& row = m_rows[i];
		if (row.state != BorderScanRow::State::Assigned) {
			continue;
		}
		const std::string edge = row.effectiveEdge();
		auto it = bestRowPerEdge.find(edge);
		if (it == bestRowPerEdge.end()) {
			bestRowPerEdge[edge] = i;
			continue;
		}
		if (RowScore(row) > RowScore(m_rows[it->second])) {
			m_rows[it->second].state = BorderScanRow::State::Duplicate;
			it->second = i;
		} else {
			row.state = BorderScanRow::State::Duplicate;
		}
	}
}

void BorderScanDialog::RefreshGridAndCounts(int newlyAdded) {
	// Remember the selected ROW (not grid index) across the view rebuild.
	const int selectedRow = m_grid->RowIndexFor(m_grid->GetSelection());

	// Rebuild the filtered view from the current rows.
	std::vector<int> view;
	if (!m_filterEdge.empty()) {
		for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
			if (m_rows[i].effectiveEdge() == m_filterEdge) {
				view.push_back(i);
			}
		}
		std::stable_sort(view.begin(), view.end(), [this](int a, int b) {
			return RowScore(m_rows[a]) > RowScore(m_rows[b]);
		});
	}

	int gridIndex = -1;
	if (selectedRow >= 0 && selectedRow < static_cast<int>(m_rows.size())) {
		if (m_filterEdge.empty()) {
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
	for (const BorderScanRow& row : m_rows) {
		if (row.state == BorderScanRow::State::Assigned) {
			++assigned;
		}
	}
	wxString label = wxString::Format("%lu candidates", static_cast<unsigned long>(m_rows.size()));
	if (newlyAdded >= 0) {
		label += wxString::Format(" (+%d new)", newlyAdded);
	}
	if (assigned > 0) {
		label += wxString::Format(" — %lu assigned", static_cast<unsigned long>(assigned));
	}
	m_countLabel->SetLabel(label);

	Layout();
	UpdateSelectedRowUI();
}

void BorderScanDialog::OnRowSelected(int WXUNUSED(rowIndex)) {
	UpdateSelectedRowUI();
}

void BorderScanDialog::UpdateSelectedRowUI() {
	const int rowIndex = m_grid->RowIndexFor(m_grid->GetSelection());
	if (rowIndex < 0 || rowIndex >= static_cast<int>(m_rows.size())) {
		m_rowInfoLabel->SetLabel("Select a result to review it.");
		m_updatingChoice = true;
		m_edgeChoice->SetSelection(0);
		m_updatingChoice = false;
		m_edgeChoice->Enable(false);
		Layout();
		return;
	}

	const BorderScanRow& row = m_rows[rowIndex];
	m_edgeChoice->Enable(true);

	wxString info = wxString::Format("item %u", static_cast<unsigned int>(row.result.itemId));
	if (row.scanned && !row.result.edge.empty()) {
		info += wxString::Format(" — suggested %s (%.0f%%)", wxString(row.result.edge), row.result.confidence);
		if (!row.result.secondEdge.empty()) {
			info += wxString::Format(", 2nd %s (%.0f%%)", wxString(row.result.secondEdge), row.result.secondConfidence);
		}
	}
	if (row.manual) {
		info += wxString::Format(" — manual %s", wxString(row.manualEdge));
	}
	info += " — " + StateName(row);
	m_rowInfoLabel->SetLabel(info);

	// Programmatic selection must not fire OnEdgeOverride.
	m_updatingChoice = true;
	if (row.excluded) {
		m_edgeChoice->SetSelection(EXCLUDED_CHOICE_INDEX);
	} else if (row.manual) {
		int manualIdx = 0;
		for (size_t e = 0; e < BorderClassifier::EDGE_COUNT; ++e) {
			if (BorderClassifier::EDGE_NAMES[e] == row.manualEdge) {
				manualIdx = static_cast<int>(e) + 1;
				break;
			}
		}
		m_edgeChoice->SetSelection(manualIdx);
	} else {
		m_edgeChoice->SetSelection(0); // (auto)
	}
	m_updatingChoice = false;

	Layout();
}

void BorderScanDialog::OnEdgeOverride(wxCommandEvent& WXUNUSED(event)) {
	if (m_updatingChoice) {
		return;
	}

	const int rowIndex = m_grid->RowIndexFor(m_grid->GetSelection());
	if (rowIndex < 0 || rowIndex >= static_cast<int>(m_rows.size())) {
		return;
	}
	BorderScanRow& row = m_rows[rowIndex];

	const int sel = m_edgeChoice->GetSelection();
	if (sel <= 0) {
		// "(auto)"
		row.manual = false;
		row.manualEdge.clear();
		row.excluded = false;
	} else if (sel == EXCLUDED_CHOICE_INDEX) {
		row.excluded = true;
		row.manual = false;
		row.manualEdge.clear();
	} else {
		// Edge name — also force-includes AlreadyInBorder rows.
		row.manual = true;
		row.manualEdge = BorderClassifier::EDGE_NAMES[sel - 1];
		row.excluded = false;
	}

	RecomputeStates();
	RefreshGridAndCounts();
}

void BorderScanDialog::OnCheckEdge(wxCommandEvent& WXUNUSED(event)) {
	const int sel = m_checkEdgeChoice->GetSelection();
	if (sel < 0 || sel >= static_cast<int>(BorderClassifier::EDGE_COUNT)) {
		return;
	}
	m_filterEdge = BorderClassifier::EDGE_NAMES[sel];
	RefreshGridAndCounts();
	SetStatusMessage(wxString::Format("Showing only '%s' rows, best confidence first.", wxString(m_filterEdge)));
}

void BorderScanDialog::OnShowAll(wxCommandEvent& WXUNUSED(event)) {
	if (m_filterEdge.empty()) {
		return;
	}
	m_filterEdge.clear();
	RefreshGridAndCounts();
	SetStatusMessage("Showing all candidates.");
}

void BorderScanDialog::OnApply(wxCommandEvent& WXUNUSED(event)) {
	m_assignments.clear();
	for (const BorderScanRow& row : m_rows) {
		if (row.state != BorderScanRow::State::Assigned) {
			continue;
		}
		// The single BorderType-name <-> BorderEdgePosition conversion point:
		// always through the canonical edge-name string, never a numeric cast.
		const BorderEdgePosition pos = edgeStringToPosition(row.effectiveEdge());
		if (pos == EDGE_NONE) {
			continue;
		}
		m_assignments[pos] = row.result.itemId;
	}

	if (m_assignments.empty()) {
		wxMessageBox("Nothing to apply — no edge has an assigned item.", "Border Scan", wxOK | wxICON_INFORMATION, this);
		return;
	}

	EndModal(wxID_OK);
}
