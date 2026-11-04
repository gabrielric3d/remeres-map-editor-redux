//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/dialogs/map_profiles_dialog.h"

#include "map/map.h"
#include "ui/gui.h"
#include "util/image_manager.h"

#include <wx/checklst.h>
#include <wx/filename.h>
#include <wx/filepicker.h>
#include <wx/spinctrl.h>

// ============================================================================
// Map Profiles Dialog
// A profile = an ordered list of .otbm files (hand-made or read from a server maps.xml)
// opened together as one new map.

MapProfilesDialog::MapProfilesDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Map Profiles", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	profiles(MapProfiles::Load()) {
	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	wxStaticText* intro = newd wxStaticText(this, wxID_ANY,
		"Opens several maps at once. \"Open as layers\" shows them all together in one tab while every map\n"
		"stays its own file: pick the layer to edit in the Map Layers panel, and Save writes that map only.\n"
		"\"Open merged copy\" imports them into one new unsaved map instead (for a look or an export).\n"
		"Point a profile at the server maps.xml to get the same maps the server loads at boot.");
	sizer->Add(intro, 0, wxALL, 12);

	wxSizer* body_sizer = newd wxBoxSizer(wxHORIZONTAL);

	// ---- Profile list ----
	wxSizer* list_sizer = newd wxBoxSizer(wxVERTICAL);
	profile_list = newd wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(180, 300));
	list_sizer->Add(profile_list, 1, wxEXPAND | wxBOTTOM, 6);
	wxSizer* list_buttons = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* add_profile_button = newd wxButton(this, wxID_ANY, "Add");
	add_profile_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_PLUS, wxSize(16, 16)));
	list_buttons->Add(add_profile_button, 1, wxRIGHT, 4);
	remove_profile_button = newd wxButton(this, wxID_ANY, "Remove");
	remove_profile_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_TRASH_CAN, wxSize(16, 16)));
	list_buttons->Add(remove_profile_button, 1);
	list_sizer->Add(list_buttons, 0, wxEXPAND);
	body_sizer->Add(list_sizer, 0, wxEXPAND | wxRIGHT, 12);

	// ---- Selected profile ----
	wxStaticBoxSizer* profile_box = newd wxStaticBoxSizer(wxVERTICAL, this, "Profile");
	wxWindow* box = profile_box->GetStaticBox();

	wxFlexGridSizer* fields = newd wxFlexGridSizer(3, 6, 6);
	fields->AddGrowableCol(1);
	fields->Add(newd wxStaticText(box, wxID_ANY, "Name:"), 0, wxALIGN_CENTER_VERTICAL);
	name_text = newd wxTextCtrl(box, wxID_ANY);
	fields->Add(name_text, 1, wxEXPAND);
	fields->AddSpacer(0);
	fields->Add(newd wxStaticText(box, wxID_ANY, "Server maps.xml:"), 0, wxALIGN_CENTER_VERTICAL);
	maps_xml_picker = newd wxFilePickerCtrl(box, wxID_ANY, "", "Select the server maps.xml (data/world/maps.xml)",
		"maps.xml|maps.xml|XML files (*.xml)|*.xml|All files (*.*)|*.*", wxDefaultPosition, wxSize(420, -1),
		wxFLP_OPEN | wxFLP_FILE_MUST_EXIST | wxFLP_USE_TEXTCTRL);
	maps_xml_picker->SetToolTip("Optional. When set, the map list is read from this file every time the profile is opened.\n"
		"Leave it empty to keep a list of your own.");
	fields->Add(maps_xml_picker, 1, wxEXPAND);
	read_xml_button = newd wxButton(box, wxID_ANY, "Read now");
	read_xml_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_ROTATE, wxSize(16, 16)));
	read_xml_button->SetToolTip("Read the maps.xml again now to see the current list.");
	fields->Add(read_xml_button, 0);
	profile_box->Add(fields, 0, wxEXPAND | wxALL, 6);

	linked_hint = newd wxStaticText(box, wxID_ANY,
		"The list comes from maps.xml: uncheck the maps you don't want (that is remembered).\n"
		"Clear the maps.xml field to edit the list by hand.");
	profile_box->Add(linked_hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

	profile_box->Add(newd wxStaticText(box, wxID_ANY, "Maps (checked ones are loaded, top to bottom):"), 0, wxLEFT | wxRIGHT, 6);
	wxSizer* maps_sizer = newd wxBoxSizer(wxHORIZONTAL);
	map_list = newd wxCheckListBox(box, wxID_ANY, wxDefaultPosition, wxSize(-1, 220));
	maps_sizer->Add(map_list, 1, wxEXPAND | wxRIGHT, 6);
	wxSizer* map_buttons = newd wxBoxSizer(wxVERTICAL);
	add_maps_button = newd wxButton(box, wxID_ANY, "Add maps...");
	add_maps_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_FILE_CIRCLE_PLUS, wxSize(16, 16)));
	map_buttons->Add(add_maps_button, 0, wxEXPAND | wxBOTTOM, 4);
	remove_map_button = newd wxButton(box, wxID_ANY, "Remove");
	remove_map_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_TRASH_CAN, wxSize(16, 16)));
	map_buttons->Add(remove_map_button, 0, wxEXPAND | wxBOTTOM, 4);
	map_up_button = newd wxButton(box, wxID_ANY, "Up");
	map_up_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_ARROW_UP, wxSize(16, 16)));
	map_buttons->Add(map_up_button, 0, wxEXPAND | wxBOTTOM, 4);
	map_down_button = newd wxButton(box, wxID_ANY, "Down");
	map_down_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_ARROW_DOWN, wxSize(16, 16)));
	map_buttons->Add(map_down_button, 0, wxEXPAND | wxBOTTOM, 12);
	check_all_button = newd wxButton(box, wxID_ANY, "Check all");
	check_all_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_CHECK, wxSize(16, 16)));
	map_buttons->Add(check_all_button, 0, wxEXPAND | wxBOTTOM, 4);
	check_none_button = newd wxButton(box, wxID_ANY, "Uncheck all");
	check_none_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_XMARK, wxSize(16, 16)));
	map_buttons->Add(check_none_button, 0, wxEXPAND);
	maps_sizer->Add(map_buttons, 0);
	profile_box->Add(maps_sizer, 1, wxEXPAND | wxALL, 6);

	// ---- Selected map ----
	wxSizer* map_fields = newd wxBoxSizer(wxHORIZONTAL);
	map_fields->Add(newd wxStaticText(box, wxID_ANY, "Offset X:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	offset_x_spin = newd wxSpinCtrl(box, wxID_ANY, "0", wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, -65535, 65535, 0);
	map_fields->Add(offset_x_spin, 0, wxRIGHT, 12);
	map_fields->Add(newd wxStaticText(box, wxID_ANY, "Offset Y:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	offset_y_spin = newd wxSpinCtrl(box, wxID_ANY, "0", wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, -65535, 65535, 0);
	map_fields->Add(offset_y_spin, 0, wxRIGHT, 12);
	houses_check = newd wxCheckBox(box, wxID_ANY, "Load houses and towns");
	houses_check->SetToolTip("Same as load-house in maps.xml: off leaves the map's houses and towns out.");
	map_fields->Add(houses_check, 0, wxALIGN_CENTER_VERTICAL);
	profile_box->Add(map_fields, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

	body_sizer->Add(profile_box, 1, wxEXPAND);
	sizer->Add(body_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);

	// ---- Open/OK/Cancel ----
	wxSizer* button_sizer = newd wxBoxSizer(wxHORIZONTAL);
	open_button = newd wxButton(this, wxID_ANY, "Open as layers");
	open_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_LAYER_GROUP, wxSize(16, 16)));
	open_button->SetToolTip("Save the profiles and open every checked map as a layer: all drawn together in one tab,\n"
		"one file per map. Edits and Save go to the layer picked in the Map Layers panel.");
	button_sizer->Add(open_button, wxSizerFlags(1).Center().Border(wxRIGHT, 4));
	open_merged_button = newd wxButton(this, wxID_ANY, "Open merged copy");
	open_merged_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_FOLDER_OPEN, wxSize(16, 16)));
	open_merged_button->SetToolTip("Save the profiles and import every checked map into one new unsaved map.\n"
		"Nothing is written back to the source maps.");
	button_sizer->Add(open_merged_button, wxSizerFlags(1).Center().Border(wxRIGHT, 12));
	wxButton* ok_button = newd wxButton(this, wxID_OK, "OK");
	ok_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_CHECK, wxSize(16, 16)));
	button_sizer->Add(ok_button, wxSizerFlags(1).Center());
	wxButton* cancel_button = newd wxButton(this, wxID_CANCEL, "Cancel");
	cancel_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_XMARK, wxSize(16, 16)));
	button_sizer->Add(cancel_button, wxSizerFlags(1).Center());
	sizer->Add(button_sizer, 0, wxALL | wxCENTER, 12);

	SetSizerAndFit(sizer);
	Centre(wxBOTH);

	for (const MapProfile& profile : profiles) {
		profile_list->Append(ProfileLabel(profile));
	}
	if (!profiles.empty()) {
		current = 0;
		profile_list->SetSelection(0);
	}
	ShowCurrent();

	profile_list->Bind(wxEVT_LISTBOX, &MapProfilesDialog::OnSelectProfile, this);
	profile_list->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { OnOpenProfile(false); });
	add_profile_button->Bind(wxEVT_BUTTON, &MapProfilesDialog::OnAddProfile, this);
	remove_profile_button->Bind(wxEVT_BUTTON, &MapProfilesDialog::OnRemoveProfile, this);
	name_text->Bind(wxEVT_TEXT, &MapProfilesDialog::OnNameChanged, this);
	maps_xml_picker->Bind(wxEVT_FILEPICKER_CHANGED, &MapProfilesDialog::OnMapsXmlChanged, this);
	read_xml_button->Bind(wxEVT_BUTTON, &MapProfilesDialog::OnReadMapsXml, this);
	map_list->Bind(wxEVT_LISTBOX, &MapProfilesDialog::OnSelectMap, this);
	map_list->Bind(wxEVT_CHECKLISTBOX, &MapProfilesDialog::OnToggleMap, this);
	add_maps_button->Bind(wxEVT_BUTTON, &MapProfilesDialog::OnAddMaps, this);
	remove_map_button->Bind(wxEVT_BUTTON, &MapProfilesDialog::OnRemoveMap, this);
	map_up_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnMoveMap(-1); });
	map_down_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnMoveMap(1); });
	check_all_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnCheckAll(true); });
	check_none_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnCheckAll(false); });
	offset_x_spin->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { OnMapFieldChanged(); });
	offset_x_spin->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { OnMapFieldChanged(); });
	offset_y_spin->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { OnMapFieldChanged(); });
	offset_y_spin->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { OnMapFieldChanged(); });
	houses_check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { OnMapFieldChanged(); });
	open_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnOpenProfile(false); });
	open_merged_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnOpenProfile(true); });
	ok_button->Bind(wxEVT_BUTTON, &MapProfilesDialog::OnClickOK, this);

	wxIcon icon;
	icon.CopyFromBitmap(IMAGE_MANAGER.GetBitmap(ICON_LAYER_GROUP, wxSize(32, 32)));
	SetIcon(icon);
}

bool MapProfilesDialog::IsLinked() const {
	return current != wxNOT_FOUND && !profiles[current].maps_xml.empty();
}

wxString MapProfilesDialog::ProfileLabel(const MapProfile& profile) const {
	wxString label = profile.name.empty() ? wxString("(unnamed)") : profile.name;
	size_t enabled = 0;
	for (const MapProfileEntry& entry : profile.maps) {
		enabled += entry.enabled ? 1 : 0;
	}
	label += wxString::Format(" (%zu/%zu)", enabled, profile.maps.size());
	return label;
}

wxString MapProfilesDialog::MapLabel(const MapProfileEntry& entry) const {
	wxString label = entry.name + "  -  " + entry.file;
	if (entry.offset_x != 0 || entry.offset_y != 0) {
		label << "  [offset " << entry.offset_x << ", " << entry.offset_y << "]";
	}
	if (entry.load_houses) {
		label << "  [houses]";
	}
	if (!wxFileName::FileExists(entry.file)) {
		label << "  (NOT FOUND)";
	}
	return label;
}

void MapProfilesDialog::ShowCurrent() {
	updating_controls = true;
	if (current == wxNOT_FOUND) {
		name_text->ChangeValue("");
		maps_xml_picker->SetPath("");
	} else {
		name_text->ChangeValue(profiles[current].name);
		maps_xml_picker->SetPath(profiles[current].maps_xml);
	}
	updating_controls = false;
	FillMapList();
}

void MapProfilesDialog::FillMapList() {
	updating_controls = true;
	const int selection = map_list->GetSelection();
	map_list->Clear();
	if (current != wxNOT_FOUND) {
		const MapProfile& profile = profiles[current];
		for (size_t i = 0; i < profile.maps.size(); ++i) {
			map_list->Append(MapLabel(profile.maps[i]));
			map_list->Check(static_cast<unsigned int>(i), profile.maps[i].enabled);
		}
		if (selection != wxNOT_FOUND && selection < static_cast<int>(profile.maps.size())) {
			map_list->SetSelection(selection);
		}
		profile_list->SetString(current, ProfileLabel(profile));
	}
	updating_controls = false;
	ShowSelectedMap();
}

void MapProfilesDialog::ShowSelectedMap() {
	updating_controls = true;
	const int selection = map_list->GetSelection();
	if (current != wxNOT_FOUND && selection != wxNOT_FOUND) {
		const MapProfileEntry& entry = profiles[current].maps[selection];
		offset_x_spin->SetValue(entry.offset_x);
		offset_y_spin->SetValue(entry.offset_y);
		houses_check->SetValue(entry.load_houses);
	} else {
		offset_x_spin->SetValue(0);
		offset_y_spin->SetValue(0);
		houses_check->SetValue(false);
	}
	updating_controls = false;
	UpdateEnabledState();
}

void MapProfilesDialog::UpdateEnabledState() {
	const bool has_profile = current != wxNOT_FOUND;
	const bool linked = IsLinked();
	const bool editable = has_profile && !linked;
	const int selection = map_list->GetSelection();
	const int count = static_cast<int>(map_list->GetCount());
	const bool has_map = has_profile && selection != wxNOT_FOUND;

	remove_profile_button->Enable(has_profile);
	name_text->Enable(has_profile);
	maps_xml_picker->Enable(has_profile);
	read_xml_button->Enable(linked);
	linked_hint->Show(linked);
	map_list->Enable(has_profile);
	add_maps_button->Enable(editable);
	remove_map_button->Enable(editable && has_map);
	map_up_button->Enable(editable && has_map && selection > 0);
	map_down_button->Enable(editable && has_map && selection + 1 < count);
	check_all_button->Enable(has_profile && count > 0);
	check_none_button->Enable(has_profile && count > 0);
	offset_x_spin->Enable(editable && has_map);
	offset_y_spin->Enable(editable && has_map);
	houses_check->Enable(editable && has_map);
	open_button->Enable(has_profile && count > 0);
	open_merged_button->Enable(has_profile && count > 0);
	Layout();
}

bool MapProfilesDialog::SyncCurrent(bool report_errors) {
	if (!IsLinked()) {
		return false;
	}
	wxString error;
	if (!MapProfiles::SyncFromServer(profiles[current], error)) {
		if (report_errors) {
			wxMessageBox(error, "Map Profiles", wxOK | wxICON_WARNING, this);
		}
		return false;
	}
	FillMapList();
	return true;
}

void MapProfilesDialog::OnSelectProfile(wxCommandEvent&) {
	current = profile_list->GetSelection();
	map_list->SetSelection(wxNOT_FOUND);
	ShowCurrent();
}

void MapProfilesDialog::OnAddProfile(wxCommandEvent&) {
	MapProfile profile;
	profile.name = wxString::Format("Profile %zu", profiles.size() + 1);
	// Start from the server of the map being edited, the usual reason to create a profile.
	if (g_gui.IsEditorOpen() && g_gui.GetCurrentMap().hasFile()) {
		profile.maps_xml = MapProfiles::FindServerMapsXml(wxstr(g_gui.GetCurrentMap().getFilename()));
	}
	profiles.push_back(profile);
	profile_list->Append(ProfileLabel(profile));

	current = static_cast<int>(profiles.size()) - 1;
	profile_list->SetSelection(current);
	map_list->SetSelection(wxNOT_FOUND);
	ShowCurrent();
	SyncCurrent(true);
	name_text->SetFocus();
	name_text->SelectAll();
}

void MapProfilesDialog::OnRemoveProfile(wxCommandEvent&) {
	if (current == wxNOT_FOUND) {
		return;
	}
	profiles.erase(profiles.begin() + current);
	profile_list->Delete(current);
	if (profiles.empty()) {
		current = wxNOT_FOUND;
	} else {
		current = std::min(current, static_cast<int>(profiles.size()) - 1);
		profile_list->SetSelection(current);
	}
	map_list->SetSelection(wxNOT_FOUND);
	ShowCurrent();
}

void MapProfilesDialog::OnNameChanged(wxCommandEvent&) {
	if (updating_controls || current == wxNOT_FOUND) {
		return;
	}
	profiles[current].name = name_text->GetValue().Trim().Trim(false);
	profile_list->SetString(current, ProfileLabel(profiles[current]));
}

void MapProfilesDialog::OnMapsXmlChanged(wxFileDirPickerEvent&) {
	if (updating_controls || current == wxNOT_FOUND) {
		return;
	}
	const wxString path = maps_xml_picker->GetPath().Trim().Trim(false);
	profiles[current].maps_xml = path;
	// Read it as soon as it points at a real file; typing a path half-way stays quiet.
	if (!path.empty() && wxFileName::FileExists(path)) {
		SyncCurrent(true);
	}
	UpdateEnabledState();
}

void MapProfilesDialog::OnReadMapsXml(wxCommandEvent&) {
	SyncCurrent(true);
}

void MapProfilesDialog::OnSelectMap(wxCommandEvent&) {
	ShowSelectedMap();
}

void MapProfilesDialog::OnToggleMap(wxCommandEvent& event) {
	if (updating_controls || current == wxNOT_FOUND) {
		return;
	}
	const int index = event.GetInt();
	if (index >= 0 && index < static_cast<int>(profiles[current].maps.size())) {
		profiles[current].maps[index].enabled = map_list->IsChecked(index);
		profile_list->SetString(current, ProfileLabel(profiles[current]));
	}
}

void MapProfilesDialog::OnCheckAll(bool checked) {
	if (current == wxNOT_FOUND) {
		return;
	}
	for (MapProfileEntry& entry : profiles[current].maps) {
		entry.enabled = checked;
	}
	FillMapList();
}

void MapProfilesDialog::OnAddMaps(wxCommandEvent&) {
	if (current == wxNOT_FOUND || IsLinked()) {
		return;
	}
	wxString start;
	if (!profiles[current].maps.empty()) {
		start = wxFileName(profiles[current].maps.back().file).GetPath();
	}
	wxFileDialog dlg(this, "Select maps", start, "", "OTBM maps (*.otbm)|*.otbm|All files (*.*)|*.*", wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}
	wxArrayString paths;
	dlg.GetPaths(paths);
	for (const wxString& path : paths) {
		MapProfileEntry entry;
		entry.file = path;
		entry.name = wxFileName(path).GetName();
		// The first map of a list carries the houses, like a server maps.xml without load-house.
		entry.load_houses = profiles[current].maps.empty();
		profiles[current].maps.push_back(entry);
	}
	FillMapList();
	map_list->SetSelection(map_list->GetCount() - 1);
	ShowSelectedMap();
}

void MapProfilesDialog::OnRemoveMap(wxCommandEvent&) {
	const int selection = map_list->GetSelection();
	if (current == wxNOT_FOUND || IsLinked() || selection == wxNOT_FOUND) {
		return;
	}
	std::vector<MapProfileEntry>& maps = profiles[current].maps;
	maps.erase(maps.begin() + selection);
	FillMapList();
	if (!maps.empty()) {
		map_list->SetSelection(std::min(selection, static_cast<int>(maps.size()) - 1));
	}
	ShowSelectedMap();
}

void MapProfilesDialog::OnMoveMap(int direction) {
	const int selection = map_list->GetSelection();
	if (current == wxNOT_FOUND || IsLinked() || selection == wxNOT_FOUND) {
		return;
	}
	std::vector<MapProfileEntry>& maps = profiles[current].maps;
	const int target = selection + direction;
	if (target < 0 || target >= static_cast<int>(maps.size())) {
		return;
	}
	std::swap(maps[selection], maps[target]);
	FillMapList();
	map_list->SetSelection(target);
	ShowSelectedMap();
}

void MapProfilesDialog::OnMapFieldChanged() {
	const int selection = map_list->GetSelection();
	if (updating_controls || current == wxNOT_FOUND || IsLinked() || selection == wxNOT_FOUND) {
		return;
	}
	MapProfileEntry& entry = profiles[current].maps[selection];
	entry.offset_x = offset_x_spin->GetValue();
	entry.offset_y = offset_y_spin->GetValue();
	entry.load_houses = houses_check->GetValue();
	// SetString re-inserts the item on MSW, so restore its check mark and selection.
	updating_controls = true;
	map_list->SetString(selection, MapLabel(entry));
	map_list->Check(selection, entry.enabled);
	map_list->SetSelection(selection);
	updating_controls = false;
}

void MapProfilesDialog::StoreAndSave() {
	for (size_t i = 0; i < profiles.size(); ++i) {
		if (profiles[i].name.empty()) {
			profiles[i].name = wxString::Format("Profile %zu", i + 1);
		}
	}
	MapProfiles::Save(profiles);
}

void MapProfilesDialog::OnOpenProfile(bool merged) {
	if (current == wxNOT_FOUND || profiles[current].maps.empty()) {
		return;
	}
	StoreAndSave();
	profile_to_open = current;
	open_merged = merged;
	EndModal(wxID_OPEN);
}

void MapProfilesDialog::ShowAndOpen(wxWindow* parent, const MapLoadOptions& load_options) {
	int index = wxNOT_FOUND;
	bool merged = false;
	{
		MapProfilesDialog dialog(parent);
		if (dialog.ShowModal() != wxID_OPEN) {
			return;
		}
		index = dialog.GetProfileToOpen();
		merged = dialog.OpenMerged();
	}

	std::vector<MapProfile> profiles = MapProfiles::Load();
	if (index < 0 || index >= static_cast<int>(profiles.size())) {
		return;
	}
	if (merged) {
		MapProfiles::OpenMerged(profiles[index], load_options);
	} else {
		MapProfiles::OpenAsLayers(profiles[index], load_options);
	}
	// A profile linked to a server maps.xml was re-read while opening; keep the fresh list.
	MapProfiles::Save(profiles);
}

void MapProfilesDialog::OnClickOK(wxCommandEvent&) {
	StoreAndSave();
	EndModal(wxID_OK);
}
