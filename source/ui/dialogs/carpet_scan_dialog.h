//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Carpet Scan Dialog - classify candidate items into the 13 carpet alignment
// slots (kNN over the carpet brushes already loaded in g_brushes, comparing every
// candidate against the rest of the scanned batch - see carpet_classifier.h).
//
// The dialog only produces slot assignments; applying them to the carpet editor
// and saving stays in CarpetEditorPanel / DoodadEditorDialog.
//
// A slot legitimately holds several item variants, so every accepted row is
// assigned - there is no "duplicate" state and a slot can collect many items.
//////////////////////////////////////////////////////////////////////

#ifndef RME_CARPET_SCAN_DIALOG_H_
#define RME_CARPET_SCAN_DIALOG_H_

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/stattext.h>

#include "util/virtual_item_grid.h"
#include "brushes/carpet/carpet_classifier.h"
#include "ui/dialogs/carpet_editor_panel.h" // carpetAlignFromString + carpetAlignLabel

#include <functional>
#include <map>
#include <string>
#include <vector>

// One scan candidate: classifier result + user review state.
struct CarpetScanRow {
	CarpetScanResult result;
	bool scanned = false; // false until Auto-Detect classified this row
	bool manual = false;
	std::string manualAlign;
	bool excluded = false;

	enum class State { Pending, Assigned, Excluded, Rejected, AlreadyInCarpet };
	State state = State::Pending;

	std::string effectiveAlign() const; // manualAlign when manual, else result.align
};

// NanoVG results grid: one status-colored cell per candidate row
// (sprite + item id + align/confidence line).
class CarpetScanGrid : public VirtualItemGrid {
public:
	CarpetScanGrid(wxWindow* parent, std::vector<CarpetScanRow>* rows);

	size_t GetItemCount() const override;
	uint16_t GetItem(size_t index) const override;
	wxString GetItemName(size_t index) const override; // rich tooltip

	// Selection callback receives the ROW index (-1 when cleared), already
	// mapped through the active view.
	void SetSelectionCallback(std::function<void(int)> callback) {
		m_onSelected = std::move(callback);
	}

	// Filtered view: list of row indices to display. Empty = show all rows.
	void SetView(std::vector<int> view);
	int RowIndexFor(int gridIndex) const; // -1 when out of range

protected:
	void OnNanoVGPaint(NVGcontext* vg, int width, int height) override;
	void OnItemSelected(int index) override;

private:
	std::vector<CarpetScanRow>* m_rows;
	std::vector<int> m_view; // empty = identity
	std::function<void(int)> m_onSelected;
};

class CarpetScanDialog : public wxDialog {
public:
	explicit CarpetScanDialog(wxWindow* parent);

	// Valid after ShowModal() returns wxID_OK. A slot may carry several items;
	// they come back in the order they were added as candidates.
	const std::map<BorderType, std::vector<uint16_t>>& GetAlignAssignments() const {
		return m_assignments;
	}

private:
	void OnAddCandidates(wxCommandEvent& event);
	void OnFromMapSelection(wxCommandEvent& event);
	void OnClearCandidates(wxCommandEvent& event);
	void OnAutoDetect(wxCommandEvent& event);
	void OnAlignOverride(wxCommandEvent& event);
	void OnApply(wxCommandEvent& event);
	void OnCheckAlign(wxCommandEvent& event);
	void OnShowAll(wxCommandEvent& event);
	void OnRowSelected(int rowIndex);

	size_t AddCandidateIds(const std::vector<uint16_t>& ids);
	bool ParseCandidateText(const wxString& text, std::vector<uint16_t>& outIds, wxString& error) const;
	void RecomputeStates();
	void RefreshGridAndCounts(int newlyAdded = -1);
	void UpdateSelectedRowUI();
	void SetStatusMessage(const wxString& text, bool isError = false);

	std::vector<CarpetScanRow> m_rows;
	std::map<BorderType, std::vector<uint16_t>> m_assignments;

	// Controls
	wxTextCtrl* m_candidateInput = nullptr;
	wxButton* m_fromSelectionButton = nullptr;
	wxStaticText* m_countLabel = nullptr;
	wxStaticText* m_statusLabel = nullptr;
	wxChoice* m_checkAlignChoice = nullptr;
	CarpetScanGrid* m_grid = nullptr;
	wxStaticText* m_rowInfoLabel = nullptr;
	wxChoice* m_alignChoice = nullptr;

	std::string m_filterAlign; // active "Check alignment" filter; empty = show all
	bool m_updatingChoice = false; // re-entrancy guard for programmatic SetSelection
};

#endif // RME_CARPET_SCAN_DIALOG_H_
