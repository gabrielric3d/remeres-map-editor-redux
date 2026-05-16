#include "ui/map/towns_window.h"

#include "editor/editor.h"
#include "map/map.h"
#include "game/town.h"
#include "ui/positionctrl.h"
#include "ui/gui.h"
#include "ui/dialog_util.h"
#include "util/image_manager.h"

#include <algorithm>
#include <iterator>

EditTownsDialog::EditTownsDialog(wxWindow* parent, Editor& editor) :
	wxDialog(parent, wxID_ANY, "Towns", wxDefaultPosition, FROM_DIP(parent, wxSize(280, 330))),
	editor(editor) {
	Map& map = editor.map;

	// Create topsizer
	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);
	wxSizer* tmpsizer;
	wxStaticBoxSizer* staticSizer;

	for (const auto& [id, town] : map.towns) {
		town_list.push_back(std::make_unique<Town>(*town));
		if (max_town_id < town->getID()) {
			max_town_id = town->getID();
		}
	}

	// Town list
	town_listbox = newd wxListCtrl(this, EDIT_TOWNS_LISTBOX, wxDefaultPosition, FROM_DIP(this, wxSize(240, 100)), wxLC_REPORT | wxLC_SINGLE_SEL);
	town_listbox->InsertColumn(0, "Town", wxLIST_FORMAT_LEFT, FROM_DIP(this, 165));
	town_listbox->InsertColumn(1, "ID", wxLIST_FORMAT_LEFT, FROM_DIP(this, 55));
	sizer->Add(town_listbox, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);

	tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
	auto addBtn = newd wxButton(this, EDIT_TOWNS_ADD, "Add");
	addBtn->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_PLUS));
	addBtn->SetToolTip("Add a new town");
	tmpsizer->Add(addBtn, 0, wxTOP, 5);
	remove_button = newd wxButton(this, EDIT_TOWNS_REMOVE, "Remove");
	remove_button->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_MINUS));
	remove_button->SetToolTip("Remove selected town");
	tmpsizer->Add(remove_button, 0, wxRIGHT | wxTOP, 5);
	sizer->Add(tmpsizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

	// House options
	staticSizer = newd wxStaticBoxSizer(wxHORIZONTAL, this, "Name / ID");
	name_field = newd wxTextCtrl(staticSizer->GetStaticBox(), wxID_ANY, "", wxDefaultPosition, FROM_DIP(this, wxSize(190, 20)), 0, wxTextValidator(wxFILTER_ASCII, &town_name));
	name_field->SetToolTip("Town name");
	staticSizer->Add(name_field, 2, wxEXPAND | wxLEFT | wxBOTTOM, 5);

	id_field = newd wxTextCtrl(staticSizer->GetStaticBox(), wxID_ANY, "", wxDefaultPosition, FROM_DIP(this, wxSize(40, 20)), 0, wxTextValidator(wxFILTER_NUMERIC, &town_id));
	id_field->SetToolTip("Town ID (any houses associated with this town will be updated automatically)");
	staticSizer->Add(id_field, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	sizer->Add(staticSizer, 0, wxEXPAND | wxALL, 10);

	// Temple position
	temple_position = newd PositionCtrl(this, "Temple Position", 0, 0, 0, map.getWidth(), map.getHeight());
	select_position_button = newd wxButton(temple_position->GetStaticBox(), EDIT_TOWNS_SELECT_TEMPLE, "Go To");
	select_position_button->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_LOCATION_ARROW));
	select_position_button->SetToolTip("Jump to temple position");
	temple_position->Add(select_position_button, 0, wxLEFT | wxRIGHT | wxBOTTOM, 5);
	sizer->Add(temple_position, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

	// OK/Cancel buttons
	tmpsizer = newd wxBoxSizer(wxHORIZONTAL);
	auto okBtn = newd wxButton(this, wxID_OK, "OK");
	okBtn->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_CHECK));
	okBtn->SetToolTip("Save changes");
	tmpsizer->Add(okBtn, wxSizerFlags(1).Center());
	auto cancelBtn = newd wxButton(this, wxID_CANCEL, "Cancel");
	cancelBtn->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_XMARK));
	cancelBtn->SetToolTip("Cancel");
	tmpsizer->Add(cancelBtn, wxSizerFlags(1).Center());
	sizer->Add(tmpsizer, 0, wxCENTER | wxALL, 10);

	SetSizerAndFit(sizer);
	Centre(wxBOTH);
	BuildListBox(true);

	town_listbox->Bind(wxEVT_LIST_ITEM_SELECTED, &EditTownsDialog::OnListBoxChange, this);
	addBtn->Bind(wxEVT_BUTTON, &EditTownsDialog::OnClickAdd, this);
	remove_button->Bind(wxEVT_BUTTON, &EditTownsDialog::OnClickRemove, this);
	select_position_button->Bind(wxEVT_BUTTON, &EditTownsDialog::OnClickSelectTemplePosition, this);
	okBtn->Bind(wxEVT_BUTTON, &EditTownsDialog::OnClickOK, this);
	cancelBtn->Bind(wxEVT_BUTTON, &EditTownsDialog::OnClickCancel, this);

	SetIcons(IMAGE_MANAGER.GetIconBundle(ICON_CITY));
}

EditTownsDialog::~EditTownsDialog() = default;

bool EditTownsDialog::ApplyEdits(uint32_t old_town_id, bool* out_name_changed) {
	if (out_name_changed) {
		*out_name_changed = false;
	}

	Town* old_town = nullptr;
	for (auto& town : town_list) {
		if (old_town_id == town->getID()) {
			old_town = town.get();
			break;
		}
	}
	if (!old_town) {
		return true;
	}

	// Parse and validate the (possibly edited) ID
	unsigned long new_id_ul = 0;
	if (!id_field->GetValue().ToULong(&new_id_ul) || new_id_ul == 0) {
		DialogUtil::PopupDialog(this, "Error", "Town ID must be a positive number.", wxOK);
		id_field->SetValue(wxString::Format("%lu", static_cast<unsigned long>(old_town_id)));
		return false;
	}
	uint32_t new_town_id = static_cast<uint32_t>(new_id_ul);

	if (new_town_id != old_town_id) {
		for (const auto& town : town_list) {
			if (town.get() != old_town && town->getID() == new_town_id) {
				DialogUtil::PopupDialog(this, "Error", "Another town already uses that ID.", wxOK);
				id_field->SetValue(wxString::Format("%lu", static_cast<unsigned long>(old_town_id)));
				return false;
			}
		}

		// Propagate ID change to all houses pointing at the old ID
		for (const auto& [id, house] : editor.map.houses) {
			if (house->townid == old_town_id) {
				house->townid = new_town_id;
			}
		}

		old_town->setID(new_town_id);
		if (max_town_id < new_town_id) {
			max_town_id = new_town_id;
		}
	}

	editor.map.getOrCreateTile(old_town->getTemplePosition())->getLocation()->decreaseTownCount();
	Position templePos = temple_position->GetPosition();
	editor.map.getOrCreateTile(templePos)->getLocation()->increaseTownCount();
	old_town->setTemplePosition(templePos);

	wxString new_name = name_field->GetValue();
	wxString old_name = wxstr(old_town->getName());
	old_town->setName(nstr(new_name));
	if (out_name_changed) {
		*out_name_changed = (new_name != old_name);
	}

	return true;
}

void EditTownsDialog::BuildListBox(bool doselect) {
	long tmplong = 0;
	max_town_id = 0;
	uint32_t selection_before = 0;

	if (doselect && id_field->GetValue().ToLong(&tmplong)) {
		uint32_t old_town_id = tmplong;

		for (const auto& town : town_list) {
			if (old_town_id == town->getID()) {
				selection_before = town->getID();
				break;
			}
		}
	}

	town_listbox->DeleteAllItems();
	long row = 0;
	for (const auto& town : town_list) {
		town_listbox->InsertItem(row, wxstr(town->getName()));
		town_listbox->SetItem(row, 1, wxString::Format("%u", town->getID()));
		if (max_town_id < town->getID()) {
			max_town_id = town->getID();
		}
		++row;
	}
	remove_button->Enable(town_listbox->GetItemCount() != 0);
	select_position_button->Enable(false);

	if (doselect) {
		if (selection_before) {
			int i = 0;
			for (const auto& town : town_list) {
				if (selection_before == town->getID()) {
					SelectListItem(i);
					return;
				}
				++i;
			}
		}
		UpdateSelection(0);
	}
}

void EditTownsDialog::UpdateSelection(int new_selection) {
	// Save edits to the previously selected town. We track the previous selection
	// in `prev_selection` because by the time wxEVT_LISTBOX fires, the listbox's
	// own selection has already moved to `new_selection`.
	if (!town_list.empty() && prev_selection != wxNOT_FOUND
		&& static_cast<size_t>(prev_selection) < town_list.size()) {
		uint32_t old_town_id = town_list[prev_selection]->getID();
		bool name_changed = false;
		if (!ApplyEdits(old_town_id, &name_changed)) {
			// Validation failed; revert the listbox to the previous selection
			SelectListItem(prev_selection);
			return;
		}
		if (name_changed) {
			BuildListBox(false);
		}
	}

	// Clear fields
	town_name.Clear();
	town_id.Clear();

	if (town_list.size() > size_t(new_selection)) {
		name_field->Enable(true);
		id_field->Enable(true);
		temple_position->Enable(true);
		select_position_button->Enable(true);

		// Change the values to reflect the newd selection
		Town* town = town_list[new_selection].get();
		ASSERT(town);

		town_name << wxstr(town->getName());
		name_field->SetValue(town_name);
		town_id << long(town->getID());
		id_field->SetValue(town_id);
		temple_position->SetPosition(town->getTemplePosition());
		SelectListItem(new_selection);
		prev_selection = new_selection;
	} else {
		name_field->Enable(false);
		id_field->Enable(false);
		temple_position->Enable(false);
		select_position_button->Enable(false);
		prev_selection = wxNOT_FOUND;
	}
	Refresh();
}

void EditTownsDialog::OnListBoxChange(wxListEvent& event) {
	if (suppress_list_select) {
		return;
	}
	UpdateSelection(event.GetIndex());
}

void EditTownsDialog::SelectListItem(long index) {
	if (index < 0 || index >= town_listbox->GetItemCount()) {
		return;
	}
	// SetItemState fires wxEVT_LIST_ITEM_SELECTED; suppress it so this behaves
	// like the old wxListBox::SetSelection (silent, no re-entrant UpdateSelection).
	suppress_list_select = true;
	town_listbox->SetItemState(index, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
		wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	town_listbox->EnsureVisible(index);
	suppress_list_select = false;
}

void EditTownsDialog::OnClickSelectTemplePosition(wxCommandEvent& WXUNUSED(event)) {
	Position templepos = temple_position->GetPosition();
	g_gui.SetScreenCenterPosition(templepos);
}

void EditTownsDialog::OnClickAdd(wxCommandEvent& WXUNUSED(event)) {
	// Apply any pending edits to the currently selected town first, so the user
	// doesn't lose unsaved changes (name/id/temple) and we can bail on validation errors.
	if (prev_selection != wxNOT_FOUND && static_cast<size_t>(prev_selection) < town_list.size()) {
		uint32_t old_town_id = town_list[prev_selection]->getID();
		if (!ApplyEdits(old_town_id, nullptr)) {
			return;
		}
	}

	auto new_town = std::make_unique<Town>(++max_town_id);
	new_town->setName("Unnamed Town");
	new_town->setTemplePosition(Position(0, 0, 0));
	town_list.push_back(std::move(new_town));

	editor.map.getOrCreateTile(Position(0, 0, 0))->getLocation()->increaseTownCount();

	prev_selection = wxNOT_FOUND;
	BuildListBox(false);
	UpdateSelection(town_list.size() - 1);
	SelectListItem(static_cast<long>(town_list.size()) - 1);
	id_field->SetFocus();
	id_field->SelectAll();
}

void EditTownsDialog::OnClickRemove(wxCommandEvent& WXUNUSED(event)) {
	int selection_index = prev_selection;
	if (selection_index == wxNOT_FOUND || static_cast<size_t>(selection_index) >= town_list.size()) {
		return;
	}

	Town* town = town_list[selection_index].get();

	Map& map = editor.map;
	for (const auto& [id, house] : map.houses) {
		if (house->townid == town->getID()) {
			DialogUtil::PopupDialog(this, "Error", "You cannot delete a town which still has houses associated with it.", wxOK);
			return;
		}
	}

	// remove town flag from tile
	editor.map.getOrCreateTile(town->getTemplePosition())->getLocation()->decreaseTownCount();

	// remove town object
	town_list.erase(town_list.begin() + selection_index);
	// The town that was selected is gone; clear prev_selection so UpdateSelection
	// skips its "save old values" pass.
	prev_selection = wxNOT_FOUND;
	BuildListBox(false);
	UpdateSelection(selection_index - 1);
}

void EditTownsDialog::OnClickOK(wxCommandEvent& WXUNUSED(event)) {
	if (Validate() && TransferDataFromWindow()) {
		// Save edits to the currently selected town.
		if (!town_list.empty() && prev_selection != wxNOT_FOUND
			&& static_cast<size_t>(prev_selection) < town_list.size()) {
			uint32_t old_town_id = town_list[prev_selection]->getID();
			bool name_changed = false;
			if (!ApplyEdits(old_town_id, &name_changed)) {
				return;
			}
			if (name_changed) {
				BuildListBox(true);
			}
		}

		Towns& towns = editor.map.towns;

		// Verify the newd information
		for (const auto& town : town_list) {
			if (town->getName() == "") {
				DialogUtil::PopupDialog(this, "Error", "You can't have a town with an empty name.", wxOK);
				return;
			}
			if (!town->getTemplePosition().isValid() || town->getTemplePosition().x > editor.map.getWidth() || town->getTemplePosition().y > editor.map.getHeight()) {
				wxString msg;
				msg << "The town " << wxstr(town->getName()) << " has an invalid temple position.";
				DialogUtil::PopupDialog(this, "Error", msg, wxOK);
				return;
			}
		}

		// Clear old towns
		towns.clear();

		// Build the newd town map
		for (auto& town : town_list) {
			// Movement to towns.addTown() leaves moved-from unique_ptrs in town_list.
			// This is safe as town_list.clear() is called immediately after.
			towns.addTown(std::move(town));
		}
		town_list.clear();
		editor.map.doChange();

		EndModal(1);
		g_gui.RefreshPalettes();
	}
}

void EditTownsDialog::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	// Just close this window
	EndModal(0);
}
