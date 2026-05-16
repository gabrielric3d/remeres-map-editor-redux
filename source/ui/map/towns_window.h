#ifndef RME_UI_MAP_TOWNS_WINDOW_H_
#define RME_UI_MAP_TOWNS_WINDOW_H_

#include "app/main.h"
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <vector>
#include <memory>

class Editor;
class Town;
class PositionCtrl;

class EditTownsDialog : public wxDialog {
public:
	EditTownsDialog(wxWindow* parent, Editor& editor);
	virtual ~EditTownsDialog();

	void OnListBoxChange(wxListEvent&);
	void OnClickSelectTemplePosition(wxCommandEvent&);
	void OnClickAdd(wxCommandEvent&);
	void OnClickRemove(wxCommandEvent&);
	void OnClickOK(wxCommandEvent&);
	void OnClickCancel(wxCommandEvent&);

protected:
	void BuildListBox(bool doselect);
	void UpdateSelection(int new_selection);
	bool ApplyEdits(uint32_t old_town_id, bool* out_name_changed = nullptr);

	// Selects (and focuses/scrolls to) a row without triggering OnListBoxChange,
	// matching the old wxListBox::SetSelection behaviour the logic relies on.
	void SelectListItem(long index);

	Editor& editor;

	std::vector<std::unique_ptr<Town>> town_list;
	uint32_t max_town_id;
	int prev_selection = wxNOT_FOUND;

	wxListCtrl* town_listbox;
	bool suppress_list_select = false;
	wxString town_name, town_id;

	wxTextCtrl* name_field;
	wxTextCtrl* id_field;

	PositionCtrl* temple_position;
	wxButton* remove_button;
	wxButton* select_position_button;
};

#endif
