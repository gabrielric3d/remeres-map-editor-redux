//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Wall Scan Dialog - classify candidate items into wall segments by
// sprite shape (kNN over the wall brushes already loaded in g_brushes).
//
// The dialog only produces segment assignments; applying them to the
// Wall Brush Editor and saving stays in WallBrushEditorDialog.
//
// Unlike Border Scan, a wall segment legitimately holds several item
// variants, so every accepted row is assigned - there is no
// "duplicate" state and a segment can collect many items.
//////////////////////////////////////////////////////////////////////

#ifndef RME_WALL_SCAN_DIALOG_H_
#define RME_WALL_SCAN_DIALOG_H_

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/stattext.h>

#include "util/virtual_item_grid.h"
#include "brushes/wall/wall_classifier.h"
#include "ui/dialogs/wall_brush_editor_dialog.h" // WallSegmentType + wallSegmentFromString

#include <functional>
#include <map>
#include <string>
#include <vector>

// One scan candidate: classifier result + user review state.
struct WallScanRow {
	WallScanResult result;
	bool scanned = false; // false until Auto-Detect classified this row
	bool manual = false;
	std::string manualSegment;
	bool excluded = false;

	enum class State { Pending, Assigned, Excluded, Rejected, AlreadyInWall };
	State state = State::Pending;

	std::string effectiveSegment() const; // manualSegment when manual, else result.segment
};

// NanoVG results grid: one status-colored cell per candidate row
// (sprite + item id + segment/confidence line).
class WallScanGrid : public VirtualItemGrid {
public:
	WallScanGrid(wxWindow* parent, std::vector<WallScanRow>* rows);

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
	std::vector<WallScanRow>* m_rows;
	std::vector<int> m_view; // empty = identity
	std::function<void(int)> m_onSelected;
};

class WallScanDialog : public wxDialog {
public:
	explicit WallScanDialog(wxWindow* parent);

	// Valid after ShowModal() returns wxID_OK. A segment may carry several items;
	// they come back in the order they were added as candidates.
	const std::map<WallSegmentType, std::vector<uint16_t>>& GetSegmentAssignments() const {
		return m_assignments;
	}

private:
	void OnAddCandidates(wxCommandEvent& event);
	void OnFromMapSelection(wxCommandEvent& event);
	void OnClearCandidates(wxCommandEvent& event);
	void OnAutoDetect(wxCommandEvent& event);
	void OnSegmentOverride(wxCommandEvent& event);
	void OnApply(wxCommandEvent& event);
	void OnCheckSegment(wxCommandEvent& event);
	void OnShowAll(wxCommandEvent& event);
	void OnRowSelected(int rowIndex);

	size_t AddCandidateIds(const std::vector<uint16_t>& ids);
	bool ParseCandidateText(const wxString& text, std::vector<uint16_t>& outIds, wxString& error) const;
	void RecomputeStates();
	void RefreshGridAndCounts(int newlyAdded = -1);
	void UpdateSelectedRowUI();
	void SetStatusMessage(const wxString& text, bool isError = false);

	std::vector<WallScanRow> m_rows;
	std::map<WallSegmentType, std::vector<uint16_t>> m_assignments;

	// Controls
	wxTextCtrl* m_candidateInput = nullptr;
	wxButton* m_fromSelectionButton = nullptr;
	wxStaticText* m_countLabel = nullptr;
	wxStaticText* m_statusLabel = nullptr;
	wxChoice* m_checkSegmentChoice = nullptr;
	WallScanGrid* m_grid = nullptr;
	wxStaticText* m_rowInfoLabel = nullptr;
	wxChoice* m_segmentChoice = nullptr;

	std::string m_filterSegment; // active "Check segment" filter; empty = show all
	bool m_updatingChoice = false; // re-entrancy guard for programmatic SetSelection
};

#endif // RME_WALL_SCAN_DIALOG_H_
