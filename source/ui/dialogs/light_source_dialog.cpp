#include "ui/dialogs/light_source_dialog.h"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/clrpicker.h>
#include <wx/stattext.h>
#include <wx/msgdlg.h>

// --- LightSourceEditDialog ---

LightSourceEditDialog::LightSourceEditDialog(wxWindow* parent, const LightSourceEntry& entry) :
	wxDialog(parent, wxID_ANY, "Light Source", wxDefaultPosition, wxSize(300, 180)) {

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	wxFlexGridSizer* grid = new wxFlexGridSizer(2, 8, 8);
	grid->AddGrowableCol(1);

	grid->Add(new wxStaticText(this, wxID_ANY, "Client ID:"), 0, wxALIGN_CENTER_VERTICAL);
	client_id_ctrl = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 65535, entry.clientId);
	grid->Add(client_id_ctrl, 1, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Label:"), 0, wxALIGN_CENTER_VERTICAL);
	label_ctrl = new wxTextCtrl(this, wxID_ANY, entry.label);
	grid->Add(label_ctrl, 1, wxEXPAND);

	grid->Add(new wxStaticText(this, wxID_ANY, "Color:"), 0, wxALIGN_CENTER_VERTICAL);
	color_ctrl = new wxColourPickerCtrl(this, wxID_ANY, wxColour(entry.r, entry.g, entry.b));
	grid->Add(color_ctrl, 1, wxEXPAND);

	sizer->Add(grid, 1, wxALL | wxEXPAND, 10);

	wxSizer* btnSizer = CreateButtonSizer(wxOK | wxCANCEL);
	sizer->Add(btnSizer, 0, wxALL | wxALIGN_RIGHT, 10);

	SetSizerAndFit(sizer);
	CenterOnParent();

	Bind(wxEVT_BUTTON, &LightSourceEditDialog::OnOK, this, wxID_OK);
}

LightSourceEntry LightSourceEditDialog::GetEntry() const {
	LightSourceEntry entry;
	entry.clientId = static_cast<uint16_t>(client_id_ctrl->GetValue());
	entry.label = label_ctrl->GetValue().ToStdString();
	wxColour c = color_ctrl->GetColour();
	entry.r = c.Red();
	entry.g = c.Green();
	entry.b = c.Blue();
	return entry;
}

void LightSourceEditDialog::OnOK(wxCommandEvent& event) {
	if (client_id_ctrl->GetValue() <= 0) {
		wxMessageBox("Client ID must be greater than 0.", "Error", wxOK | wxICON_ERROR, this);
		return;
	}
	if (label_ctrl->GetValue().IsEmpty()) {
		wxMessageBox("Label cannot be empty.", "Error", wxOK | wxICON_ERROR, this);
		return;
	}
	EndModal(wxID_OK);
}

// --- LightSourceDialog ---

LightSourceDialog::LightSourceDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Light Sources", wxDefaultPosition, wxSize(500, 400),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {

	entries = LightSourceManager::instance().getEntries();
	CreateControls();
	PopulateList();
	CenterOnParent();
}

LightSourceDialog::~LightSourceDialog() = default;

void LightSourceDialog::CreateControls() {
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// List
	list_ctrl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	list_ctrl->InsertColumn(0, "Client ID", wxLIST_FORMAT_LEFT, 80);
	list_ctrl->InsertColumn(1, "Label", wxLIST_FORMAT_LEFT, 200);
	list_ctrl->InsertColumn(2, "Color", wxLIST_FORMAT_LEFT, 100);

	mainSizer->Add(list_ctrl, 1, wxALL | wxEXPAND, 8);

	// Buttons row
	wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
	add_btn = new wxButton(this, wxID_ANY, "Add");
	edit_btn = new wxButton(this, wxID_ANY, "Edit");
	remove_btn = new wxButton(this, wxID_ANY, "Remove");

	edit_btn->Enable(false);
	remove_btn->Enable(false);

	btnRow->Add(add_btn, 0, wxRIGHT, 4);
	btnRow->Add(edit_btn, 0, wxRIGHT, 4);
	btnRow->Add(remove_btn, 0);

	mainSizer->Add(btnRow, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

	// OK / Cancel
	wxSizer* okCancel = CreateButtonSizer(wxOK | wxCANCEL);
	mainSizer->Add(okCancel, 0, wxALL | wxALIGN_RIGHT, 8);

	SetSizer(mainSizer);

	// Binds
	add_btn->Bind(wxEVT_BUTTON, &LightSourceDialog::OnAdd, this);
	edit_btn->Bind(wxEVT_BUTTON, &LightSourceDialog::OnEdit, this);
	remove_btn->Bind(wxEVT_BUTTON, &LightSourceDialog::OnRemove, this);
	list_ctrl->Bind(wxEVT_LIST_ITEM_SELECTED, &LightSourceDialog::OnListItemSelected, this);
	list_ctrl->Bind(wxEVT_LIST_ITEM_DESELECTED, &LightSourceDialog::OnListItemDeselected, this);
	list_ctrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &LightSourceDialog::OnEdit, this);
	Bind(wxEVT_BUTTON, &LightSourceDialog::OnOK, this, wxID_OK);
	Bind(wxEVT_BUTTON, &LightSourceDialog::OnCancel, this, wxID_CANCEL);
}

void LightSourceDialog::PopulateList() {
	list_ctrl->DeleteAllItems();
	for (size_t i = 0; i < entries.size(); ++i) {
		long idx = list_ctrl->InsertItem(static_cast<long>(i), "");
		UpdateListItem(idx, entries[i]);
	}
}

void LightSourceDialog::UpdateListItem(long index, const LightSourceEntry& entry) {
	list_ctrl->SetItem(index, 0, std::to_string(entry.clientId));
	list_ctrl->SetItem(index, 1, entry.label);

	wxString colorStr = wxString::Format("#%02X%02X%02X", entry.r, entry.g, entry.b);
	list_ctrl->SetItem(index, 2, colorStr);
	list_ctrl->SetItemTextColour(index, wxColour(entry.r, entry.g, entry.b));
}

void LightSourceDialog::OnAdd(wxCommandEvent& event) {
	LightSourceEntry newEntry;
	newEntry.clientId = 1;
	newEntry.label = "New Light";
	newEntry.r = 255;
	newEntry.g = 200;
	newEntry.b = 0;

	LightSourceEditDialog dlg(this, newEntry);
	if (dlg.ShowModal() == wxID_OK) {
		entries.push_back(dlg.GetEntry());
		long idx = list_ctrl->InsertItem(list_ctrl->GetItemCount(), "");
		UpdateListItem(idx, entries.back());
	}
}

void LightSourceDialog::OnEdit(wxCommandEvent& event) {
	long sel = list_ctrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0 || sel >= static_cast<long>(entries.size())) return;

	LightSourceEditDialog dlg(this, entries[sel]);
	if (dlg.ShowModal() == wxID_OK) {
		entries[sel] = dlg.GetEntry();
		UpdateListItem(sel, entries[sel]);
	}
}

void LightSourceDialog::OnRemove(wxCommandEvent& event) {
	long sel = list_ctrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0 || sel >= static_cast<long>(entries.size())) return;

	entries.erase(entries.begin() + sel);
	list_ctrl->DeleteItem(sel);

	edit_btn->Enable(false);
	remove_btn->Enable(false);
}

void LightSourceDialog::OnListItemSelected(wxListEvent& event) {
	edit_btn->Enable(true);
	remove_btn->Enable(true);
}

void LightSourceDialog::OnListItemDeselected(wxListEvent& event) {
	edit_btn->Enable(false);
	remove_btn->Enable(false);
}

void LightSourceDialog::OnOK(wxCommandEvent& event) {
	LightSourceManager::instance().setEntries(entries);
	LightSourceManager::instance().save();
	EndModal(wxID_OK);
}

void LightSourceDialog::OnCancel(wxCommandEvent& event) {
	EndModal(wxID_CANCEL);
}
