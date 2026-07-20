//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_BRUSH_SWAP_DIALOG_H_
#define RME_BRUSH_SWAP_DIALOG_H_

#include "ui/replace_tool/rule_manager.h"
#include "ui/replace_tool/brush_mapping_service.h"

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <string>
#include <vector>
#include <functional>

class wxButton;
class wxImageList;
class wxStaticText;

// Picks two brushes and expands them into an explicit, ordered list of item
// pairs (ground/center first, then N/E/S/W, corners, diagonals). Accepting the
// dialog turns every pair into a plain item->item replacement rule, so the
// result is fully visible and editable on the rule cards afterwards.
//
// This is the expanded counterpart to the compact BRUSH slot mode: same role
// mapping, but materialised up front instead of resolved at execution time.
// Modeless, like the Advanced Replace tool that owns it: the user must stay
// free to consult the map and the palette while pairing. The generated rules
// arrive through the callback instead of a ShowModal() return value.
class BrushSwapDialog : public wxDialog {
public:
	using Callback = std::function<void(const std::vector<ReplacementRule>&)>;

	// Opens the dialog and calls back once, on OK. Never fires after `parent`
	// is destroyed: the dialog is a child window and dies with it.
	static void Open(wxWindow* parent, Callback onAccepted);

	// Public so wxWidgets' deferred destruction can delete it.
	~BrushSwapDialog() override = default;

private:
	explicit BrushSwapDialog(wxWindow* parent);

	void OnPickFrom(wxCommandEvent& event);
	void OnPickTo(wxCommandEvent& event);
	void OnOk(wxCommandEvent& event);

	void RebuildPairs();
	void UpdateSlotButtons();

	wxButton* m_fromButton = nullptr;
	wxButton* m_toButton = nullptr;
	wxListCtrl* m_list = nullptr;
	wxImageList* m_images = nullptr;
	wxStaticText* m_status = nullptr;

	// Either side can be a named brush or an AutoBorder id.
	BrushMappingService::Selection m_from;
	BrushMappingService::Selection m_to;

	std::vector<BrushMappingService::RolePair> m_pairs;
	std::vector<ReplacementRule> m_rules;
	Callback m_onAccepted;
};

#endif
