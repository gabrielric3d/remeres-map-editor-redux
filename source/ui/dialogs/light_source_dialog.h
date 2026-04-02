#ifndef RME_LIGHT_SOURCE_DIALOG_H_
#define RME_LIGHT_SOURCE_DIALOG_H_

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <vector>
#include "rendering/core/light_source_manager.h"

class wxButton;
class wxTextCtrl;
class wxSpinCtrl;
class wxColourPickerCtrl;

class LightSourceDialog : public wxDialog {
public:
	LightSourceDialog(wxWindow* parent);
	~LightSourceDialog();

private:
	void CreateControls();
	void PopulateList();
	void UpdateListItem(long index, const LightSourceEntry& entry);

	void OnAdd(wxCommandEvent& event);
	void OnRemove(wxCommandEvent& event);
	void OnEdit(wxCommandEvent& event);
	void OnListItemSelected(wxListEvent& event);
	void OnListItemDeselected(wxListEvent& event);
	void OnOK(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);

	wxListCtrl* list_ctrl = nullptr;
	wxButton* add_btn = nullptr;
	wxButton* remove_btn = nullptr;
	wxButton* edit_btn = nullptr;

	std::vector<LightSourceEntry> entries;
};

class LightSourceEditDialog : public wxDialog {
public:
	LightSourceEditDialog(wxWindow* parent, const LightSourceEntry& entry);

	LightSourceEntry GetEntry() const;

private:
	void OnOK(wxCommandEvent& event);

	wxSpinCtrl* client_id_ctrl = nullptr;
	wxTextCtrl* label_ctrl = nullptr;
	wxColourPickerCtrl* color_ctrl = nullptr;
};

#endif
