//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Border Scan Dialog - classify candidate items into border edges by
// sprite shape (kNN over the borders already loaded in g_brushes).
//
// The dialog only produces edge assignments; applying them to the
// Border Editor's m_borderItems and saving stays in BorderEditorDialog.
//////////////////////////////////////////////////////////////////////

#ifndef RME_BORDER_SCAN_DIALOG_H_
#define RME_BORDER_SCAN_DIALOG_H_

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/stattext.h>

#include "util/virtual_item_grid.h"
#include "brushes/ground/border_classifier.h"
#include "ui/dialogs/border_editor_dialog.h" // BorderEdgePosition + edgeStringToPosition

#include <functional>
#include <map>
#include <string>
#include <vector>

// One scan candidate: classifier result + user review state.
struct BorderScanRow {
	BorderScanResult result;
	bool scanned = false; // false until Auto-Detect classified this row
	bool manual = false;
	std::string manualEdge;
	bool excluded = false;

	enum class State { Pending, Assigned, Duplicate, Excluded, Rejected, AlreadyInBorder };
	State state = State::Pending;

	std::string effectiveEdge() const; // manualEdge when manual, else result.edge
};

// NanoVG results grid: one status-colored cell per candidate row
// (sprite + item id + edge/confidence line).
class BorderScanGrid : public VirtualItemGrid {
public:
	BorderScanGrid(wxWindow* parent, std::vector<BorderScanRow>* rows);

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
	std::vector<BorderScanRow>* m_rows;
	std::vector<int> m_view; // empty = identity
	std::function<void(int)> m_onSelected;
};

class BorderScanDialog : public wxDialog {
public:
	explicit BorderScanDialog(wxWindow* parent);

	// Valid after ShowModal() returns wxID_OK.
	const std::map<BorderEdgePosition, uint16_t>& GetEdgeAssignments() const {
		return m_assignments;
	}

private:
	void OnAddCandidates(wxCommandEvent& event);
	void OnFromMapSelection(wxCommandEvent& event);
	void OnClearCandidates(wxCommandEvent& event);
	void OnAutoDetect(wxCommandEvent& event);
	void OnEdgeOverride(wxCommandEvent& event);
	void OnApply(wxCommandEvent& event);
	void OnCheckEdge(wxCommandEvent& event);
	void OnShowAll(wxCommandEvent& event);
	void OnRowSelected(int rowIndex);

	size_t AddCandidateIds(const std::vector<uint16_t>& ids);
	bool ParseCandidateText(const wxString& text, std::vector<uint16_t>& outIds, wxString& error) const;
	void RecomputeStates();
	void RefreshGridAndCounts(int newlyAdded = -1);
	void UpdateSelectedRowUI();
	void SetStatusMessage(const wxString& text, bool isError = false);

	std::vector<BorderScanRow> m_rows;
	std::map<BorderEdgePosition, uint16_t> m_assignments;

	// Controls
	wxTextCtrl* m_candidateInput = nullptr;
	wxButton* m_fromSelectionButton = nullptr;
	wxStaticText* m_countLabel = nullptr;
	wxStaticText* m_statusLabel = nullptr;
	wxChoice* m_checkEdgeChoice = nullptr;
	BorderScanGrid* m_grid = nullptr;
	wxStaticText* m_rowInfoLabel = nullptr;
	wxChoice* m_edgeChoice = nullptr;

	std::string m_filterEdge; // active "Check Edge" filter; empty = show all
	bool m_updatingChoice = false; // re-entrancy guard for programmatic SetSelection
};

#endif // RME_BORDER_SCAN_DIALOG_H_
