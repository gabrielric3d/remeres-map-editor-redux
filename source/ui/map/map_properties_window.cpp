#include "ui/map/map_properties_window.h"

#include "editor/editor.h"
#include "map/map.h"
#include "editor/operations/map_version_changer.h"
#include "ui/gui.h"
#include "app/managers/version_manager.h"
#include "ui/dialog_util.h"
#include "ui/map_tab.h"
#include "util/image_manager.h"

#include <wx/dirdlg.h>

namespace {

const int kMapSizePresets[] = { 128, 256, 512, 1024, 2048, 4096, 8192 };
constexpr int kMapSizePresetCount = sizeof(kMapSizePresets) / sizeof(kMapSizePresets[0]);

int GetPresetSizeFromChoiceSelection(int selection) {
	if (selection <= 0 || selection > kMapSizePresetCount) {
		return -1;
	}
	return kMapSizePresets[selection - 1];
}

int GetChoiceSelectionFromDimensions(int width, int height) {
	if (width != height) {
		return 0; // Custom
	}
	for (int i = 0; i < kMapSizePresetCount; ++i) {
		if (width == kMapSizePresets[i]) {
			return i + 1;
		}
	}
	return 0; // Custom
}

std::string BuildMapBaseName(const std::string& map_name) {
	FileName map_file(wxstr(map_name));
	std::string base_name = nstr(map_file.GetName());
	if (base_name.empty()) {
		base_name = "map";
	}
	return base_name;
}

std::string BuildAutoHouseFilename(const std::string& map_name) {
	return BuildMapBaseName(map_name) + "-house.xml";
}

std::string BuildAutoSpawnFilename(const std::string& map_name) {
	return BuildMapBaseName(map_name) + "-spawn.xml";
}

std::string BuildAutoWaypointFilename(const std::string& map_name) {
	return BuildMapBaseName(map_name) + "-waypoint.xml";
}

std::string GetDefaultMapExtension() {
	return "otbm";
}

std::string BuildMapFilenameFromInput(const wxString& map_name_input, const std::string& fallback_extension) {
	wxString name = map_name_input;
	name.Trim(true);
	name.Trim(false);

	FileName map_name(name);
	std::string base_name = nstr(map_name.GetName());
	if (base_name.empty()) {
		base_name = nstr(name);
	}
	if (base_name.empty()) {
		base_name = "map";
	}

	std::string extension = nstr(map_name.GetExt());
	if (extension.empty()) {
		extension = fallback_extension;
	}
	if (extension.empty()) {
		return base_name;
	}
	return base_name + "." + extension;
}

} // anonymous namespace

MapPropertiesWindow::MapPropertiesWindow(wxWindow* parent, MapTab* view, Editor& editor, bool allow_create_from_selection) :
	wxDialog(parent, wxID_ANY, "Map Properties", wxDefaultPosition, FROM_DIP(parent, wxSize(300, 200)), wxRESIZE_BORDER | wxCAPTION),
	view(view),
	editor(editor),
	height_spin(nullptr),
	width_spin(nullptr),
	map_name_ctrl(nullptr),
	save_location_ctrl(nullptr),
	size_preset_choice(nullptr),
	version_choice(nullptr),
	protocol_choice(nullptr),
	sync_external_files_checkbox(nullptr),
	create_from_selection_checkbox(nullptr),
	remember_save_location_checkbox(nullptr),
	description_ctrl(nullptr),
	house_filename_ctrl(nullptr),
	spawn_filename_ctrl(nullptr),
	waypoint_filename_ctrl(nullptr),
	updating_dimensions(false) {
	// Setup data variables
	Map& map = editor.map;

	std::string initial_map_name = map.getName();
	if (initial_map_name.empty() && map.hasFile()) {
		FileName map_file(wxstr(map.getFilename()));
		initial_map_name = nstr(map_file.GetFullName());
	}
	const std::string initial_map_base_name = BuildMapBaseName(initial_map_name);

	std::string initial_save_location;
	const bool remember_save_location = g_settings.getBoolean(Config::MAP_PROPERTIES_REMEMBER_SAVE_LOCATION);
	if (map.hasFile()) {
		FileName map_file(wxstr(map.getFilename()));
		initial_save_location = nstr(map_file.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR));
	} else if (remember_save_location) {
		initial_save_location = g_settings.getString(Config::MAP_PROPERTIES_DEFAULT_SAVE_LOCATION);
	}

	default_house_filename = BuildAutoHouseFilename(initial_map_base_name);
	default_spawn_filename = BuildAutoSpawnFilename(initial_map_base_name);
	default_waypoint_filename = BuildAutoWaypointFilename(initial_map_base_name);

	wxSizer* topsizer = newd wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer* grid_sizer = newd wxFlexGridSizer(2, 10, 10);
	grid_sizer->AddGrowableCol(1);

	// Map Name
	grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "Map Name"));
	map_name_ctrl = newd wxTextCtrl(this, wxID_ANY, wxstr(initial_map_base_name));
	map_name_ctrl->SetToolTip("Name for the map file (without path)");
	grid_sizer->Add(map_name_ctrl, wxSizerFlags(1).Expand());

	// Save Location
	grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "Save Location"));
	{
		wxSizer* save_location_sizer = newd wxBoxSizer(wxHORIZONTAL);
		save_location_ctrl = newd wxTextCtrl(this, wxID_ANY, wxstr(initial_save_location));
		save_location_ctrl->SetToolTip("Directory where the map will be saved");
		save_location_sizer->Add(save_location_ctrl, wxSizerFlags(1).Expand());

		wxButton* browseBtn = newd wxButton(this, MAP_PROPERTIES_BROWSE_SAVE_LOCATION, "Browse...");
		browseBtn->SetToolTip("Browse for save directory");
		save_location_sizer->Add(browseBtn, wxSizerFlags(0).Border(wxLEFT, 6));
		browseBtn->Bind(wxEVT_BUTTON, &MapPropertiesWindow::OnBrowseSaveLocation, this);

		grid_sizer->Add(save_location_sizer, 1, wxEXPAND);
	}

	// Remember Save Location
	grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "Remember Save Location"));
	remember_save_location_checkbox = newd wxCheckBox(this, wxID_ANY, "Keep this directory");
	remember_save_location_checkbox->SetValue(remember_save_location);
	remember_save_location_checkbox->SetToolTip("Persist the save location for future new maps");
	grid_sizer->Add(remember_save_location_checkbox, wxSizerFlags(1).Expand());

	// Description
	grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "Map Description"));
	description_ctrl = newd wxTextCtrl(this, wxID_ANY, wxstr(map.getMapDescription()), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
	description_ctrl->SetToolTip("Enter a description for the map");
	grid_sizer->Add(description_ctrl, wxSizerFlags(1).Expand());

	// Map version
	grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "Map Version"));
	version_choice = newd wxChoice(this, MAP_PROPERTIES_VERSION);
	version_choice->SetToolTip("Select the OTBM version (Determines feature support)");
	version_choice->Append("OTServ 0.5.0");
	version_choice->Append("OTServ 0.6.0");
	version_choice->Append("OTServ 0.6.1");
	version_choice->Append("OTServ 0.7.0 (revscriptsys)");

	switch (map.getVersion().otbm) {
		case MAP_OTBM_1:
			version_choice->SetSelection(0);
			break;
		case MAP_OTBM_2:
			version_choice->SetSelection(1);
			break;
		case MAP_OTBM_3:
			version_choice->SetSelection(2);
			break;
		case MAP_OTBM_4:
			version_choice->SetSelection(3);
			break;
		default:
			version_choice->SetSelection(0);
	}

	grid_sizer->Add(version_choice, wxSizerFlags(1).Expand());

	// Client Version
	grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "Client Version"));
	protocol_choice = newd wxChoice(this, wxID_ANY);
	protocol_choice->SetToolTip("Select the client version (protocol)");
	grid_sizer->Add(protocol_choice, wxSizerFlags(1).Expand());

	// Size Preset
	grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "Map Size Preset"));
	size_preset_choice = newd wxChoice(this, wxID_ANY);
	size_preset_choice->SetToolTip("Select a preset map size or use Custom");
	size_preset_choice->Append("Custom");
	for (int i = 0; i < kMapSizePresetCount; ++i) {
		size_preset_choice->Append(wxString::Format("%d x %d", kMapSizePresets[i], kMapSizePresets[i]));
	}
	grid_sizer->Add(size_preset_choice, wxSizerFlags(1).Expand());

	// Dimensions
	grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "Map Dimensions"));
	{
		wxSizer* subsizer = newd wxBoxSizer(wxHORIZONTAL);
		subsizer->Add(
			width_spin = newd wxSpinCtrl(this, wxID_ANY, wxstr(i2s(map.getWidth())), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 256, MAP_MAX_WIDTH), wxSizerFlags(1).Expand()
		);
		width_spin->SetToolTip("Map width in tiles");
		subsizer->Add(
			height_spin = newd wxSpinCtrl(this, wxID_ANY, wxstr(i2s(map.getHeight())), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 256, MAP_MAX_HEIGHT), wxSizerFlags(1).Expand()
		);
		height_spin->SetToolTip("Map height in tiles");
		grid_sizer->Add(subsizer, 1, wxEXPAND);
	}

	// Bind dimension/preset events
	size_preset_choice->Bind(wxEVT_CHOICE, &MapPropertiesWindow::OnSizePresetChanged, this);
	map_name_ctrl->Bind(wxEVT_TEXT, &MapPropertiesWindow::OnMapNameChanged, this);
	width_spin->Bind(wxEVT_TEXT, &MapPropertiesWindow::OnDimensionsChanged, this);
	height_spin->Bind(wxEVT_TEXT, &MapPropertiesWindow::OnDimensionsChanged, this);
	width_spin->Bind(wxEVT_SPINCTRL, &MapPropertiesWindow::OnDimensionsChangedSpin, this);
	height_spin->Bind(wxEVT_SPINCTRL, &MapPropertiesWindow::OnDimensionsChangedSpin, this);

	// Create From Selection (conditional)
	if (allow_create_from_selection) {
		grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "Create From Selection"));
		create_from_selection_checkbox = newd wxCheckBox(this, wxID_ANY, "Copy current selection to (0, 0, 7)");
		create_from_selection_checkbox->SetToolTip("Paste the current selection into the new map at position (0, 0, 7)");
		grid_sizer->Add(create_from_selection_checkbox, wxSizerFlags(1).Expand());
	}

	// Auto External Files
	grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "Auto External Files"));
	sync_external_files_checkbox = newd wxCheckBox(this, MAP_PROPERTIES_SYNC_EXTERNAL_FILES, "Use map name");
	sync_external_files_checkbox->SetValue(
		map.getHouseFilename() == default_house_filename &&
		map.getSpawnFilename() == default_spawn_filename &&
		map.getWaypointFilename() == default_waypoint_filename
	);
	sync_external_files_checkbox->SetToolTip("Automatically name house/spawn/waypoint files based on map name");
	sync_external_files_checkbox->Bind(wxEVT_CHECKBOX, &MapPropertiesWindow::OnToggleSyncExternalFiles, this);
	grid_sizer->Add(sync_external_files_checkbox, wxSizerFlags(1).Expand());

	// External Housefile
	grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "External Housefile"));
	house_filename_ctrl = newd wxTextCtrl(this, wxID_ANY, wxstr(map.getHouseFilename()));
	house_filename_ctrl->SetToolTip("External house XML file (leave empty for internal)");
	grid_sizer->Add(house_filename_ctrl, 1, wxEXPAND);

	// External Spawnfile
	grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "External Spawnfile"));
	spawn_filename_ctrl = newd wxTextCtrl(this, wxID_ANY, wxstr(map.getSpawnFilename()));
	spawn_filename_ctrl->SetToolTip("External spawn XML file (leave empty for internal)");
	grid_sizer->Add(spawn_filename_ctrl, 1, wxEXPAND);

	// External Waypointfile (Redux-specific, not in BT)
	grid_sizer->Add(newd wxStaticText(this, wxID_ANY, "External Waypointfile"));
	waypoint_filename_ctrl = newd wxTextCtrl(this, wxID_ANY, wxstr(map.getWaypointFilename()));
	waypoint_filename_ctrl->SetToolTip("External waypoint XML file (leave empty for internal)");
	grid_sizer->Add(waypoint_filename_ctrl, 1, wxEXPAND);

	topsizer->Add(grid_sizer, wxSizerFlags(1).Expand().Border(wxALL, 20));

	// OK / Cancel buttons
	wxSizer* subsizer = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* okBtn = newd wxButton(this, wxID_OK, "OK");
	okBtn->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_CHECK));
	okBtn->SetToolTip("Confirm changes");
	subsizer->Add(okBtn, wxSizerFlags(1).Center());

	wxButton* cancelBtn = newd wxButton(this, wxID_CANCEL, "Cancel");
	cancelBtn->SetBitmap(IMAGE_MANAGER.GetBitmapBundle(ICON_XMARK));
	cancelBtn->SetToolTip("Discard changes");
	subsizer->Add(cancelBtn, wxSizerFlags(1).Center());
	topsizer->Add(subsizer, wxSizerFlags(0).Center().Border(wxLEFT | wxRIGHT | wxBOTTOM, 20));

	SetSizerAndFit(topsizer);
	Centre(wxBOTH);
	UpdateProtocolList();

	ClientVersion* current_version = ClientVersion::getBestMatch(map.getVersion().client);
	if (current_version) {
		protocol_choice->SetStringSelection(wxstr(current_version->getName()));
	}

	version_choice->Bind(wxEVT_CHOICE, &MapPropertiesWindow::OnChangeVersion, this);
	okBtn->Bind(wxEVT_BUTTON, &MapPropertiesWindow::OnClickOK, this);
	cancelBtn->Bind(wxEVT_BUTTON, &MapPropertiesWindow::OnClickCancel, this);

	SetIcons(IMAGE_MANAGER.GetIconBundle(ICON_GEAR));

	SyncSizePresetSelectionFromDimensions();
	UpdateExternalFilenameControls();
}

void MapPropertiesWindow::UpdateProtocolList() {
	wxString ver = version_choice->GetStringSelection();
	wxString client = protocol_choice->GetStringSelection();

	protocol_choice->Clear();

	ClientVersionList versions;
	if (g_settings.getInteger(Config::USE_OTBM_4_FOR_ALL_MAPS)) {
		versions = ClientVersion::getAllVisible();
	} else {
		MapVersionID map_version = MAP_OTBM_1;
		if (ver.Contains("0.5.0")) {
			map_version = MAP_OTBM_1;
		} else if (ver.Contains("0.6.0")) {
			map_version = MAP_OTBM_2;
		} else if (ver.Contains("0.6.1")) {
			map_version = MAP_OTBM_3;
		} else if (ver.Contains("0.7.0")) {
			map_version = MAP_OTBM_4;
		}
		versions = ClientVersion::getAllForOTBMVersion(map_version);
	}

	for (const auto& v : versions) {
		protocol_choice->Append(wxstr(v->getName()));
	}
	protocol_choice->SetSelection(0);
	protocol_choice->SetStringSelection(client);
}

void MapPropertiesWindow::OnChangeVersion(wxCommandEvent&) {
	UpdateProtocolList();
}

void MapPropertiesWindow::OnToggleSyncExternalFiles(wxCommandEvent&) {
	UpdateExternalFilenameControls();
}

void MapPropertiesWindow::OnMapNameChanged(wxCommandEvent& event) {
	UpdateAutoExternalFilenames();
	UpdateExternalFilenameControls();
	event.Skip();
}

void MapPropertiesWindow::OnBrowseSaveLocation(wxCommandEvent& WXUNUSED(event)) {
	wxString initial_path;
	if (save_location_ctrl) {
		initial_path = save_location_ctrl->GetValue();
	}

	wxDirDialog dialog(this, "Select save location", initial_path, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
	if (dialog.ShowModal() == wxID_OK && save_location_ctrl) {
		save_location_ctrl->ChangeValue(dialog.GetPath());
	}
}

void MapPropertiesWindow::OnSizePresetChanged(wxCommandEvent&) {
	if (!size_preset_choice || !width_spin || !height_spin) {
		return;
	}

	int preset_size = GetPresetSizeFromChoiceSelection(size_preset_choice->GetSelection());
	if (preset_size < 0) {
		return;
	}

	updating_dimensions = true;
	width_spin->SetValue(preset_size);
	height_spin->SetValue(preset_size);
	updating_dimensions = false;
}

void MapPropertiesWindow::OnDimensionsChanged(wxCommandEvent& event) {
	SyncSizePresetSelectionFromDimensions();
	event.Skip();
}

void MapPropertiesWindow::OnDimensionsChangedSpin(wxSpinEvent& event) {
	SyncSizePresetSelectionFromDimensions();
	event.Skip();
}

void MapPropertiesWindow::SyncSizePresetSelectionFromDimensions() {
	if (updating_dimensions || !size_preset_choice || !width_spin || !height_spin) {
		return;
	}

	int selection = GetChoiceSelectionFromDimensions(width_spin->GetValue(), height_spin->GetValue());
	size_preset_choice->SetSelection(selection);
}

void MapPropertiesWindow::UpdateExternalFilenameControls() {
	if (!sync_external_files_checkbox || !house_filename_ctrl || !spawn_filename_ctrl || !waypoint_filename_ctrl) {
		return;
	}

	const bool use_map_name = sync_external_files_checkbox->GetValue();
	if (use_map_name) {
		house_filename_ctrl->ChangeValue(wxstr(default_house_filename));
		spawn_filename_ctrl->ChangeValue(wxstr(default_spawn_filename));
		waypoint_filename_ctrl->ChangeValue(wxstr(default_waypoint_filename));
	}

	house_filename_ctrl->Enable(!use_map_name);
	spawn_filename_ctrl->Enable(!use_map_name);
	waypoint_filename_ctrl->Enable(!use_map_name);
}

void MapPropertiesWindow::UpdateAutoExternalFilenames() {
	std::string map_name = editor.map.getName();
	if (map_name_ctrl) {
		wxString map_name_value = map_name_ctrl->GetValue();
		map_name_value.Trim(true);
		map_name_value.Trim(false);
		if (!map_name_value.empty()) {
			map_name = nstr(map_name_value);
		}
	}

	default_house_filename = BuildAutoHouseFilename(map_name);
	default_spawn_filename = BuildAutoSpawnFilename(map_name);
	default_waypoint_filename = BuildAutoWaypointFilename(map_name);
}

void MapPropertiesWindow::OnClickOK(wxCommandEvent& WXUNUSED(event)) {
	Map& map = editor.map;

	MapVersion old_ver = map.getVersion();
	MapVersion new_ver;

	wxString ver = version_choice->GetStringSelection();

	ClientVersion* selected_version = ClientVersion::get(nstr(protocol_choice->GetStringSelection()));
	if (!selected_version) {
		DialogUtil::PopupDialog(this, "Error", "Invalid client version selected.", wxOK);
		return;
	}
	new_ver.client = selected_version->getProtocolID();
	if (ver.Contains("0.5.0")) {
		new_ver.otbm = MAP_OTBM_1;
	} else if (ver.Contains("0.6.0")) {
		new_ver.otbm = MAP_OTBM_2;
	} else if (ver.Contains("0.6.1")) {
		new_ver.otbm = MAP_OTBM_3;
	} else if (ver.Contains("0.7.0")) {
		new_ver.otbm = MAP_OTBM_4;
	}

	if (!MapVersionChanger::changeMapVersion(this, editor, new_ver)) {
		return;
	}

	// Update auto external filenames based on current map name input
	UpdateAutoExternalFilenames();

	// Determine map filename extension
	std::string map_name_fallback_extension = GetDefaultMapExtension();
	if (map.hasFile()) {
		FileName current_map_file(wxstr(map.getFilename()));
		const std::string current_extension = nstr(current_map_file.GetExt());
		if (!current_extension.empty()) {
			map_name_fallback_extension = current_extension;
		}
	}

	// Build map filename from name input
	wxString map_name_value = map_name_ctrl ? map_name_ctrl->GetValue() : wxString();
	map_name_value.Trim(true);
	map_name_value.Trim(false);
	if (map_name_value.empty()) {
		map_name_value = wxstr(BuildMapBaseName(map.getName()));
	}

	const std::string map_filename = BuildMapFilenameFromInput(map_name_value, map_name_fallback_extension);

	// Handle save location
	wxString save_location_value = save_location_ctrl ? save_location_ctrl->GetValue() : wxString();
	save_location_value.Trim(true);
	save_location_value.Trim(false);

	std::string save_location = nstr(save_location_value);
	if (save_location.empty() && map.hasFile()) {
		FileName current_map_file(wxstr(map.getFilename()));
		save_location = nstr(current_map_file.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR));
	}

	// Persist remember save location setting
	const bool do_remember = remember_save_location_checkbox && remember_save_location_checkbox->GetValue();
	g_settings.setInteger(Config::MAP_PROPERTIES_REMEMBER_SAVE_LOCATION, do_remember ? 1 : 0);
	if (do_remember && !save_location.empty()) {
		g_settings.setString(Config::MAP_PROPERTIES_DEFAULT_SAVE_LOCATION, save_location);
	} else if (!do_remember) {
		g_settings.setString(Config::MAP_PROPERTIES_DEFAULT_SAVE_LOCATION, "");
	}
	g_settings.save();

	// Set full file path if save location is provided
	if (!save_location.empty()) {
		FileName target_map_file;
		target_map_file.AssignDir(wxstr(save_location));
		target_map_file.SetFullName(wxstr(map_filename));
		map.setFilename(nstr(target_map_file.GetFullPath()));
		map.setName(nstr(target_map_file.GetFullName()));
	} else {
		map.setName(map_filename);
	}

	// Set map properties
	map.setMapDescription(nstr(description_ctrl->GetValue()));
	map.setHouseFilename(nstr(house_filename_ctrl->GetValue()));
	map.setSpawnFilename(nstr(spawn_filename_ctrl->GetValue()));
	map.setWaypointFilename(nstr(waypoint_filename_ctrl->GetValue()));

	// Only resize if we have to
	int new_map_width = width_spin->GetValue();
	int new_map_height = height_spin->GetValue();
	if (new_map_width != map.getWidth() || new_map_height != map.getHeight()) {
		map.setWidth(new_map_width);
		map.setHeight(new_map_height);
		if (view) {
			g_gui.FitViewToMap(view);
		}
	}
	g_gui.RefreshPalettes();

	EndModal(wxID_OK);
}

void MapPropertiesWindow::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	// Just close this window
	EndModal(wxID_CANCEL);
}

MapPropertiesWindow::~MapPropertiesWindow() = default;

bool MapPropertiesWindow::ShouldCreateFromSelection() const {
	return create_from_selection_checkbox && create_from_selection_checkbox->GetValue();
}
