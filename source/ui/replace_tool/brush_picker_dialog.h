//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_BRUSH_PICKER_DIALOG_H_
#define RME_BRUSH_PICKER_DIALOG_H_

#include "ui/replace_tool/brush_mapping_service.h"

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <functional>

class wxTextCtrl;
class wxChoice;
class wxImageList;
class Brush;

// Modal, filterable chooser for the Advanced Replace tool.
//
// Lists the families BrushMappingService can swap by role (Ground / Wall /
// Carpet) and, when borders are allowed, the AutoBorders from borders.xml.
// Borders carry no name - only a numeric id - so the search box matches ids as
// well as names, and a type filter narrows the list down to one family.
// The dialog is *modeless*: the Advanced Replace tool itself is modeless, and
// blocking the app would stop the user from consulting the map or the palette
// mid-pick. Because there is no ShowModal() to return a value, the result
// arrives through a callback, and the dialog owns itself (Destroy on close).
// Always open one through the static helpers below - never on the stack.
class BrushPickerDialog : public wxDialog {
public:
	using Callback = std::function<void(const BrushMappingService::Selection&)>;

	// Brushes only. Used by the rule cards, where a slot can persist a brush
	// name but has no field for a border id.
	// familyFilter: when non-null, only brushes compatible with it are listed.
	// The callback fires only on OK, and never after `parent` is destroyed
	// (the dialog is a child window, so it dies with it).
	static void PickBrush(wxWindow* parent, const std::string& initialSelection, const Brush* familyFilter, Callback onPicked);

	// Brushes and borders. Used by the Swap Brush dialog, which expands the
	// selection into plain item rules and so needs no persistence for it.
	static void PickAny(wxWindow* parent, const BrushMappingService::Selection& initial, const BrushMappingService::Selection& familyFilter, Callback onPicked);

private:
	BrushPickerDialog(wxWindow* parent, const std::string& initialSelection, const Brush* familyFilter);
	BrushPickerDialog(wxWindow* parent, const BrushMappingService::Selection& initial, const BrushMappingService::Selection& familyFilter);

	void Accept(); // fire the callback, then self-destruct

	void Build(); // shared by both constructors
	void RebuildList();
	// Builds the "which selection owns this server id" index. Walking every
	// brush is not free, so it is done once, lazily, the first time the Server
	// ID field is actually used.
	void EnsureItemIndex();
	void OnSearch(wxCommandEvent& event);
	void OnTypeChanged(wxCommandEvent& event);
	void OnActivated(wxListEvent& event);
	void OnSelectionChanged(wxListEvent& event);

	struct Entry {
		BrushMappingService::Selection selection;
		std::string label; // brush name, or "Border <id>"
		uint16_t previewId = 0;
		std::string family;
		const Brush* brush = nullptr; // null for borders
	};

	// Families offered by the type filter, in dropdown order.
	enum class TypeFilter { All,
							Ground,
							Wall,
							Carpet,
							Border };
	TypeFilter CurrentTypeFilter() const;

	wxTextCtrl* m_search = nullptr;
	wxTextCtrl* m_serverId = nullptr;
	wxChoice* m_type = nullptr;
	wxListCtrl* m_list = nullptr;
	wxImageList* m_images = nullptr;

	// server item id -> the brushes / borders containing it
	std::unordered_map<uint16_t, std::vector<std::string>> m_itemToBrushes;
	std::unordered_map<uint16_t, std::vector<uint32_t>> m_itemToBorders;
	bool m_itemIndexBuilt = false;

	const Brush* m_familyFilter = nullptr;
	bool m_allowBorders = false;
	bool m_borderOnly = false; // family filter resolved to a border

	BrushMappingService::Selection m_selection;
	std::vector<Entry> m_entries;
	Callback m_onPicked;
};

#endif
