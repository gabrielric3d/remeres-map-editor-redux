//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "app/main.h"

#include "palette/palette_camera_paths.h"
#include "ui/gui.h"
#include "editor/editor.h"
#include "map/map.h"
#include "rendering/ui/map_display.h"
#include "brushes/managers/brush_manager.h"
#include "brushes/camera/camera_path_brush.h"
#include "app/settings.h"
#include "editor/hotkey_manager.h"
#include "ui/map_tab.h"

#include <algorithm>

CameraPathPalettePanel::CameraPathPalettePanel(wxWindow* parent, wxWindowID id) :
	PalettePanel(parent, id),
	map(nullptr) {
	wxSizer* root = newd wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* pathSizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Camera Paths");
	path_list = newd wxListCtrl(this, PALETTE_CAMERA_PATH_LISTBOX,
		wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_EDIT_LABELS | wxLC_NO_HEADER);
	path_list->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 160);
	path_list->InsertColumn(1, "Loop", wxLIST_FORMAT_LEFT, 40);
	pathSizer->Add(path_list, 1, wxEXPAND | wxBOTTOM, 4);

	wxSizer* pathButtons = newd wxBoxSizer(wxHORIZONTAL);
	add_path_button = newd wxButton(this, PALETTE_CAMERA_PATH_ADD, "Add");
	remove_path_button = newd wxButton(this, PALETTE_CAMERA_PATH_REMOVE, "Remove");
	pathButtons->Add(add_path_button, 1, wxEXPAND);
	pathButtons->Add(remove_path_button, 1, wxEXPAND | wxLEFT, 4);
	pathSizer->Add(pathButtons, 0, wxEXPAND);

	loop_checkbox = newd wxCheckBox(this, PALETTE_CAMERA_PATH_LOOP, "Loop");
	pathSizer->Add(loop_checkbox, 0, wxTOP, 4);

	play_pause_button = newd wxButton(this, PALETTE_CAMERA_PATH_PLAY, "Play/Pause");
	pathSizer->Add(play_pause_button, 0, wxEXPAND | wxTOP, 4);

	show_paths_checkbox = newd wxCheckBox(this, PALETTE_CAMERA_SHOW_PATHS, "Show Camera Paths");
	show_paths_checkbox->SetValue(g_settings.getBoolean(Config::SHOW_CAMERA_PATHS));
	pathSizer->Add(show_paths_checkbox, 0, wxTOP, 4);

	root->Add(pathSizer, 1, wxEXPAND | wxBOTTOM, 6);

	wxStaticBoxSizer* keySizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Keyframes");
	keyframe_list = newd wxListCtrl(this, PALETTE_CAMERA_KEYFRAME_LISTBOX,
		wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT);
	keyframe_list->InsertColumn(0, "#", wxLIST_FORMAT_LEFT, 30);
	keyframe_list->InsertColumn(1, "X", wxLIST_FORMAT_LEFT, 50);
	keyframe_list->InsertColumn(2, "Y", wxLIST_FORMAT_LEFT, 50);
	keyframe_list->InsertColumn(3, "Z", wxLIST_FORMAT_LEFT, 30);
	keyframe_list->InsertColumn(4, "Dur", wxLIST_FORMAT_LEFT, 50);
	keyframe_list->InsertColumn(5, "Speed", wxLIST_FORMAT_LEFT, 60);
	keyframe_list->InsertColumn(6, "Zoom", wxLIST_FORMAT_LEFT, 50);
	keySizer->Add(keyframe_list, 1, wxEXPAND | wxBOTTOM, 4);

	wxSizer* keyButtons = newd wxBoxSizer(wxHORIZONTAL);
	add_keyframe_button = newd wxButton(this, PALETTE_CAMERA_KEYFRAME_ADD, "Add");
	remove_keyframe_button = newd wxButton(this, PALETTE_CAMERA_KEYFRAME_REMOVE, "Remove");
	clear_keyframes_button = newd wxButton(this, PALETTE_CAMERA_KEYFRAME_CLEAR, "Clear");
	keyframe_up_button = newd wxButton(this, PALETTE_CAMERA_KEYFRAME_UP, "Up");
	keyframe_down_button = newd wxButton(this, PALETTE_CAMERA_KEYFRAME_DOWN, "Down");
	keyButtons->Add(add_keyframe_button, 1, wxEXPAND);
	keyButtons->Add(remove_keyframe_button, 1, wxEXPAND | wxLEFT, 4);
	keyButtons->Add(clear_keyframes_button, 1, wxEXPAND | wxLEFT, 4);
	keyButtons->Add(keyframe_up_button, 1, wxEXPAND | wxLEFT, 4);
	keyButtons->Add(keyframe_down_button, 1, wxEXPAND | wxLEFT, 4);
	keySizer->Add(keyButtons, 0, wxEXPAND | wxBOTTOM, 4);

	wxStaticBoxSizer* propsSizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Keyframe Properties");
	pos_label = newd wxStaticText(this, wxID_ANY, "Pos: - , -");
	propsSizer->Add(pos_label, 0, wxBOTTOM, 4);

	wxFlexGridSizer* grid = newd wxFlexGridSizer(2, 4, 6);
	grid->AddGrowableCol(1);

	grid->Add(newd wxStaticText(this, wxID_ANY, "Duration"), 0, wxALIGN_CENTER_VERTICAL);
	duration_ctrl = newd wxTextCtrl(this, wxID_ANY, "1.0");
	grid->Add(duration_ctrl, 1, wxEXPAND);

	grid->Add(newd wxStaticText(this, wxID_ANY, "Speed"), 0, wxALIGN_CENTER_VERTICAL);
	speed_ctrl = newd wxTextCtrl(this, wxID_ANY, "0.0");
	grid->Add(speed_ctrl, 1, wxEXPAND);

	grid->Add(newd wxStaticText(this, wxID_ANY, "Zoom"), 0, wxALIGN_CENTER_VERTICAL);
	wxBoxSizer* zoomSizer = newd wxBoxSizer(wxHORIZONTAL);
	zoom_ctrl = newd wxTextCtrl(this, wxID_ANY, "1.0");
	get_zoom_button = newd wxButton(this, PALETTE_CAMERA_GET_ZOOM, "Get", wxDefaultPosition, wxSize(40, -1));
	get_zoom_button->SetToolTip("Get current viewport zoom level");
	zoomSizer->Add(zoom_ctrl, 1, wxEXPAND);
	zoomSizer->Add(get_zoom_button, 0, wxLEFT, 4);
	grid->Add(zoomSizer, 1, wxEXPAND);

	grid->Add(newd wxStaticText(this, wxID_ANY, "Z"), 0, wxALIGN_CENTER_VERTICAL);
	z_ctrl = newd wxTextCtrl(this, wxID_ANY, "7");
	grid->Add(z_ctrl, 1, wxEXPAND);

	grid->Add(newd wxStaticText(this, wxID_ANY, "Easing"), 0, wxALIGN_CENTER_VERTICAL);
	wxArrayString easingChoices;
	easingChoices.Add("Linear");
	easingChoices.Add("Ease In/Out");
	easingChoices.Add("Ease In");
	easingChoices.Add("Ease Out");
	easingChoices.Add("Ease In/Out Cubic");
	easing_ctrl = newd wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, easingChoices);
	easing_ctrl->SetSelection(1);
	grid->Add(easing_ctrl, 1, wxEXPAND);

	propsSizer->Add(grid, 0, wxEXPAND | wxBOTTOM, 4);
	apply_props_button = newd wxButton(this, PALETTE_CAMERA_KEYFRAME_APPLY, "Apply to Selected");
	propsSizer->Add(apply_props_button, 0, wxEXPAND);

	keySizer->Add(propsSizer, 0, wxEXPAND);
	root->Add(keySizer, 2, wxEXPAND);

	SetSizerAndFit(root);

	// Bind events
	Bind(wxEVT_LIST_ITEM_SELECTED, &CameraPathPalettePanel::OnClickPath, this, PALETTE_CAMERA_PATH_LISTBOX);
	Bind(wxEVT_LIST_BEGIN_LABEL_EDIT, &CameraPathPalettePanel::OnBeginEditPathLabel, this, PALETTE_CAMERA_PATH_LISTBOX);
	Bind(wxEVT_LIST_END_LABEL_EDIT, &CameraPathPalettePanel::OnEditPathLabel, this, PALETTE_CAMERA_PATH_LISTBOX);
	Bind(wxEVT_BUTTON, &CameraPathPalettePanel::OnAddPath, this, PALETTE_CAMERA_PATH_ADD);
	Bind(wxEVT_BUTTON, &CameraPathPalettePanel::OnRemovePath, this, PALETTE_CAMERA_PATH_REMOVE);
	Bind(wxEVT_CHECKBOX, &CameraPathPalettePanel::OnToggleLoop, this, PALETTE_CAMERA_PATH_LOOP);
	Bind(wxEVT_BUTTON, &CameraPathPalettePanel::OnPlayPause, this, PALETTE_CAMERA_PATH_PLAY);

	Bind(wxEVT_LIST_ITEM_SELECTED, &CameraPathPalettePanel::OnClickKeyframe, this, PALETTE_CAMERA_KEYFRAME_LISTBOX);
	Bind(wxEVT_LIST_ITEM_DESELECTED, &CameraPathPalettePanel::OnDeselectKeyframe, this, PALETTE_CAMERA_KEYFRAME_LISTBOX);
	Bind(wxEVT_BUTTON, &CameraPathPalettePanel::OnAddKeyframe, this, PALETTE_CAMERA_KEYFRAME_ADD);
	Bind(wxEVT_BUTTON, &CameraPathPalettePanel::OnRemoveKeyframe, this, PALETTE_CAMERA_KEYFRAME_REMOVE);
	Bind(wxEVT_BUTTON, &CameraPathPalettePanel::OnClearKeyframes, this, PALETTE_CAMERA_KEYFRAME_CLEAR);
	Bind(wxEVT_BUTTON, &CameraPathPalettePanel::OnKeyframeUp, this, PALETTE_CAMERA_KEYFRAME_UP);
	Bind(wxEVT_BUTTON, &CameraPathPalettePanel::OnKeyframeDown, this, PALETTE_CAMERA_KEYFRAME_DOWN);
	Bind(wxEVT_BUTTON, &CameraPathPalettePanel::OnApplyKeyframeProps, this, PALETTE_CAMERA_KEYFRAME_APPLY);
	Bind(wxEVT_CHECKBOX, &CameraPathPalettePanel::OnToggleShowPaths, this, PALETTE_CAMERA_SHOW_PATHS);
	Bind(wxEVT_BUTTON, &CameraPathPalettePanel::OnGetZoom, this, PALETTE_CAMERA_GET_ZOOM);
}

CameraPathPalettePanel::~CameraPathPalettePanel() {
	////
}

wxString CameraPathPalettePanel::GetName() const {
	return "Camera Path";
}

PaletteType CameraPathPalettePanel::GetType() const {
	return TILESET_CAMERA_PATH;
}

Brush* CameraPathPalettePanel::GetSelectedBrush() const {
	return g_brush_manager.camera_path_brush;
}

int CameraPathPalettePanel::GetSelectedBrushSize() const {
	return 0;
}

bool CameraPathPalettePanel::SelectBrush(const Brush* whatbrush) {
	return whatbrush == g_brush_manager.camera_path_brush;
}

void CameraPathPalettePanel::OnUpdate() {
	RefreshPathList();
	RefreshKeyframeList();
	UpdateKeyframeControls();
}

void CameraPathPalettePanel::OnSwitchIn() {
	PalettePanel::OnSwitchIn();
	g_gui.ActivatePalette(GetParentPalette());
	show_paths_checkbox->SetValue(g_settings.getBoolean(Config::SHOW_CAMERA_PATHS));
}

void CameraPathPalettePanel::OnSwitchOut() {
	PalettePanel::OnSwitchOut();
}

void CameraPathPalettePanel::SetMap(Map* m) {
	map = m;
	OnUpdate();
}

double CameraPathPalettePanel::GetKeyframeDuration() const {
	double value = 1.0;
	duration_ctrl->GetValue().ToDouble(&value);
	return std::max(0.0, value);
}

double CameraPathPalettePanel::GetKeyframeSpeed() const {
	double value = 0.0;
	speed_ctrl->GetValue().ToDouble(&value);
	return std::max(0.0, value);
}

double CameraPathPalettePanel::GetKeyframeZoom() const {
	double value = 1.0;
	zoom_ctrl->GetValue().ToDouble(&value);
	return std::max(0.1, value);
}

int CameraPathPalettePanel::GetKeyframeZ() const {
	long value = 7;
	z_ctrl->GetValue().ToLong(&value);
	return static_cast<int>(std::max(0L, std::min<long>(MAP_MAX_LAYER, value)));
}

int CameraPathPalettePanel::GetKeyframeEasing() const {
	return easing_ctrl->GetSelection();
}

void CameraPathPalettePanel::RefreshPathList() {
	path_list->DeleteAllItems();

	if (!map) {
		path_list->Enable(false);
		add_path_button->Enable(false);
		remove_path_button->Enable(false);
		play_pause_button->Enable(false);
		loop_checkbox->Enable(false);
		return;
	}

	path_list->Enable(true);
	add_path_button->Enable(true);
	remove_path_button->Enable(true);
	play_pause_button->Enable(true);
	loop_checkbox->Enable(true);

	const std::vector<CameraPath>& paths = map->camera_paths.getPaths();
	for (size_t i = 0; i < paths.size(); ++i) {
		const CameraPath& path = paths[i];
		long idx = path_list->InsertItem(static_cast<long>(i), wxstr(path.name));
		path_list->SetItem(idx, 1, path.loop ? "Yes" : "No");
	}

	std::string activeName = map->camera_paths.getActivePathName();
	if (activeName.empty() && !paths.empty()) {
		activeName = paths.front().name;
		map->camera_paths.setActivePath(activeName);
	}
	for (long i = 0; i < path_list->GetItemCount(); ++i) {
		if (nstr(path_list->GetItemText(i)) == activeName) {
			path_list->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
			break;
		}
	}
}

void CameraPathPalettePanel::RefreshKeyframeList() {
	keyframe_list->DeleteAllItems();

	if (!map) {
		keyframe_list->Enable(false);
		add_keyframe_button->Enable(false);
		remove_keyframe_button->Enable(false);
		clear_keyframes_button->Enable(false);
		keyframe_up_button->Enable(false);
		keyframe_down_button->Enable(false);
		apply_props_button->Enable(false);
		return;
	}

	keyframe_list->Enable(true);
	add_keyframe_button->Enable(true);
	remove_keyframe_button->Enable(true);
	clear_keyframes_button->Enable(true);
	keyframe_up_button->Enable(true);
	keyframe_down_button->Enable(true);
	apply_props_button->Enable(true);

	CameraPath* path = GetActivePath();
	if (!path) {
		keyframe_list->Enable(false);
		remove_keyframe_button->Enable(false);
		clear_keyframes_button->Enable(false);
		keyframe_up_button->Enable(false);
		keyframe_down_button->Enable(false);
		apply_props_button->Enable(false);
		return;
	}

	for (size_t i = 0; i < path->keyframes.size(); ++i) {
		const CameraKeyframe& key = path->keyframes[i];
		long idx = keyframe_list->InsertItem(static_cast<long>(i), wxString::Format("%d", static_cast<int>(i + 1)));
		keyframe_list->SetItem(idx, 1, wxString::Format("%d", key.pos.x));
		keyframe_list->SetItem(idx, 2, wxString::Format("%d", key.pos.y));
		keyframe_list->SetItem(idx, 3, wxString::Format("%d", key.pos.z));
		keyframe_list->SetItem(idx, 4, wxString::Format("%.2f", key.duration));
		keyframe_list->SetItem(idx, 5, wxString::Format("%.2f", key.speed));
		keyframe_list->SetItem(idx, 6, wxString::Format("%.2f", key.zoom));
	}

	int activeIndex = map->camera_paths.getActiveKeyframe();
	if (activeIndex >= 0 && activeIndex < static_cast<int>(path->keyframes.size())) {
		keyframe_list->SetItemState(activeIndex, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	}

	loop_checkbox->SetValue(path->loop);
}

void CameraPathPalettePanel::UpdateKeyframeControls() {
	if (!map) {
		pos_label->SetLabel("Pos: - , -");
		return;
	}

	CameraPath* path = GetActivePath();
	if (!path) {
		pos_label->SetLabel("Pos: - , -");
		return;
	}

	std::vector<int> indices = GetSelectedKeyframeIndices();
	if (indices.empty()) {
		pos_label->SetLabel("Pos: - , -");
		return;
	}

	if (indices.size() > 1) {
		pos_label->SetLabel(wxString::Format("Selected: %zu keyframes", indices.size()));
		return;
	}

	int index = indices[0];
	if (index < 0 || index >= static_cast<int>(path->keyframes.size())) {
		pos_label->SetLabel("Pos: - , -");
		return;
	}

	const CameraKeyframe& key = path->keyframes[index];
	pos_label->SetLabel(wxString::Format("Pos: %d, %d", key.pos.x, key.pos.y));
	duration_ctrl->ChangeValue(wxString::Format("%.2f", key.duration));
	speed_ctrl->ChangeValue(wxString::Format("%.2f", key.speed));
	zoom_ctrl->ChangeValue(wxString::Format("%.2f", key.zoom));
	z_ctrl->ChangeValue(wxString::Format("%d", key.pos.z));
	easing_ctrl->SetSelection(static_cast<int>(key.easing));
}

CameraPath* CameraPathPalettePanel::GetActivePath() const {
	if (!map) {
		return nullptr;
	}
	return map->camera_paths.getActivePath();
}

int CameraPathPalettePanel::GetSelectedKeyframeIndex() const {
	long item = keyframe_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item != -1) {
		return static_cast<int>(item);
	}
	return map ? map->camera_paths.getActiveKeyframe() : -1;
}

std::vector<int> CameraPathPalettePanel::GetSelectedKeyframeIndices() const {
	std::vector<int> indices;
	long item = -1;
	while ((item = keyframe_list->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		indices.push_back(static_cast<int>(item));
	}
	return indices;
}

Position CameraPathPalettePanel::GetCursorPosition() const {
	MapTab* tab = g_gui.GetCurrentMapTab();
	if (!tab) {
		return Position();
	}
	MapCanvas* canvas = tab->GetCanvas();
	if (canvas) {
		Position pos = canvas->GetCursorPosition();
		if (pos.isValid()) {
			return pos;
		}
	}
	return tab->GetScreenCenterPosition();
}

void CameraPathPalettePanel::OnClickPath(wxListEvent& event) {
	if (!map) {
		return;
	}

	std::string name = nstr(event.GetText());
	map->camera_paths.setActivePath(name);
	map->camera_paths.setActiveKeyframe(-1);
	RefreshKeyframeList();
	UpdateKeyframeControls();
	g_gui.RefreshView();
}

void CameraPathPalettePanel::OnBeginEditPathLabel(wxListEvent& event) {
	g_hotkeys.DisableHotkeys();
}

void CameraPathPalettePanel::OnEditPathLabel(wxListEvent& event) {
	if (!map) {
		return;
	}

	if (event.IsEditCancelled()) {
		g_hotkeys.EnableHotkeys();
		return;
	}

	std::string newName = nstr(event.GetLabel());
	std::string oldName = nstr(path_list->GetItemText(event.GetIndex()));
	if (newName.empty()) {
		event.Veto();
		g_hotkeys.EnableHotkeys();
		return;
	}
	if (newName == oldName) {
		g_hotkeys.EnableHotkeys();
		return;
	}

	CameraPaths temp = map->camera_paths;
	CameraPath* path = temp.getPath(oldName);
	if (!path) {
		g_hotkeys.EnableHotkeys();
		return;
	}

	if (temp.getPath(newName)) {
		event.Veto();
		g_hotkeys.EnableHotkeys();
		return;
	}
	path->name = newName;
	temp.setActivePath(newName);

	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_DRAW);
	}

	g_hotkeys.EnableHotkeys();
}

void CameraPathPalettePanel::OnAddPath(wxCommandEvent&) {
	if (!map) {
		return;
	}

	CameraPaths temp = map->camera_paths;
	temp.addPath("Path");

	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_DRAW);
	}
}

void CameraPathPalettePanel::OnRemovePath(wxCommandEvent&) {
	if (!map) {
		return;
	}

	long item = path_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item == -1) {
		return;
	}

	std::string name = nstr(path_list->GetItemText(item));
	CameraPaths temp = map->camera_paths;
	if (!temp.removePath(name)) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_DRAW);
	}
}

void CameraPathPalettePanel::OnToggleLoop(wxCommandEvent&) {
	if (!map) {
		return;
	}

	CameraPaths temp = map->camera_paths;
	CameraPath* path = temp.getActivePath();
	if (!path) {
		return;
	}
	path->loop = loop_checkbox->GetValue();

	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_DRAW);
	}
}

void CameraPathPalettePanel::OnPlayPause(wxCommandEvent&) {
	g_gui.ToggleCameraPathPlayback();
}

void CameraPathPalettePanel::OnClickKeyframe(wxListEvent& event) {
	if (!map) {
		return;
	}

	int index = static_cast<int>(event.GetIndex());
	map->camera_paths.setActiveKeyframe(index);
	UpdateKeyframeControls();

	std::vector<int> indices = GetSelectedKeyframeIndices();
	if (indices.size() == 1) {
		if (CameraPath* path = GetActivePath()) {
			if (index >= 0 && index < static_cast<int>(path->keyframes.size())) {
				g_gui.SetScreenCenterPosition(path->keyframes[index].pos);
			}
		}
	}
	g_gui.RefreshView();
}

void CameraPathPalettePanel::OnDeselectKeyframe(wxListEvent&) {
	UpdateKeyframeControls();
	g_gui.RefreshView();
}

void CameraPathPalettePanel::OnAddKeyframe(wxCommandEvent&) {
	if (!map) {
		return;
	}

	CameraPaths temp = map->camera_paths;
	CameraPath* path = temp.getActivePath();
	if (!path) {
		path = temp.addPath("Path");
	}

	Position pos = GetCursorPosition();
	if (!pos.isValid()) {
		return;
	}

	double duration = 1.0;
	double speed = 0.0;
	double zoom = 1.0;
	long z = pos.z;

	duration_ctrl->GetValue().ToDouble(&duration);
	speed_ctrl->GetValue().ToDouble(&speed);
	zoom_ctrl->GetValue().ToDouble(&zoom);
	z_ctrl->GetValue().ToLong(&z);

	duration = std::max(0.0, duration);
	speed = std::max(0.0, speed);
	zoom = std::max(0.1, zoom);
	z = std::max(0L, std::min<long>(MAP_MAX_LAYER, z));

	CameraKeyframe key;
	key.pos = pos;
	key.pos.z = static_cast<int>(z);
	key.duration = duration;
	key.speed = speed;
	key.zoom = zoom;
	key.easing = static_cast<CameraEasing>(easing_ctrl->GetSelection());

	int insertIndex = static_cast<int>(path->keyframes.size());
	int activeIndex = temp.getActiveKeyframe();
	if (activeIndex >= 0 && activeIndex < static_cast<int>(path->keyframes.size())) {
		insertIndex = activeIndex + 1;
	}
	path->keyframes.insert(path->keyframes.begin() + insertIndex, key);
	temp.setActiveKeyframe(insertIndex);

	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_DRAW);
	}
}

void CameraPathPalettePanel::OnRemoveKeyframe(wxCommandEvent&) {
	if (!map) {
		return;
	}

	CameraPaths temp = map->camera_paths;
	CameraPath* path = temp.getActivePath();
	if (!path) {
		return;
	}

	int index = GetSelectedKeyframeIndex();
	if (index < 0 || index >= static_cast<int>(path->keyframes.size())) {
		return;
	}

	path->keyframes.erase(path->keyframes.begin() + index);
	if (path->keyframes.empty()) {
		temp.setActiveKeyframe(-1);
	} else {
		temp.setActiveKeyframe(0);
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_DRAW);
	}
}

void CameraPathPalettePanel::OnClearKeyframes(wxCommandEvent&) {
	if (!map) {
		return;
	}

	CameraPaths temp = map->camera_paths;
	CameraPath* path = temp.getActivePath();
	if (!path || path->keyframes.empty()) {
		return;
	}

	path->keyframes.clear();
	temp.setActiveKeyframe(-1);

	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_DRAW);
	}
}

void CameraPathPalettePanel::OnKeyframeUp(wxCommandEvent&) {
	if (!map) {
		return;
	}

	CameraPaths temp = map->camera_paths;
	CameraPath* path = temp.getActivePath();
	if (!path) {
		return;
	}

	int index = GetSelectedKeyframeIndex();
	if (index <= 0 || index >= static_cast<int>(path->keyframes.size())) {
		return;
	}

	std::swap(path->keyframes[index], path->keyframes[index - 1]);
	temp.setActiveKeyframe(index - 1);

	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_DRAW);
	}
}

void CameraPathPalettePanel::OnKeyframeDown(wxCommandEvent&) {
	if (!map) {
		return;
	}

	CameraPaths temp = map->camera_paths;
	CameraPath* path = temp.getActivePath();
	if (!path) {
		return;
	}

	int index = GetSelectedKeyframeIndex();
	if (index < 0 || index >= static_cast<int>(path->keyframes.size()) - 1) {
		return;
	}

	std::swap(path->keyframes[index], path->keyframes[index + 1]);
	temp.setActiveKeyframe(index + 1);

	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_DRAW);
	}
}

void CameraPathPalettePanel::OnApplyKeyframeProps(wxCommandEvent&) {
	if (!map) {
		return;
	}

	CameraPaths temp = map->camera_paths;
	CameraPath* path = temp.getActivePath();
	if (!path) {
		return;
	}

	std::vector<int> indices = GetSelectedKeyframeIndices();
	if (indices.empty()) {
		return;
	}

	double duration = 1.0;
	double speed = 0.0;
	double zoom = 1.0;
	long z = 7;

	duration_ctrl->GetValue().ToDouble(&duration);
	speed_ctrl->GetValue().ToDouble(&speed);
	zoom_ctrl->GetValue().ToDouble(&zoom);
	z_ctrl->GetValue().ToLong(&z);

	duration = std::max(0.0, duration);
	speed = std::max(0.0, speed);
	zoom = std::max(0.1, zoom);
	z = std::max(0L, std::min<long>(MAP_MAX_LAYER, z));

	CameraEasing easing = static_cast<CameraEasing>(easing_ctrl->GetSelection());

	for (int index : indices) {
		if (index >= 0 && index < static_cast<int>(path->keyframes.size())) {
			CameraKeyframe& key = path->keyframes[index];
			key.duration = duration;
			key.speed = speed;
			key.zoom = zoom;
			key.pos.z = static_cast<int>(z);
			key.easing = easing;
		}
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (editor) {
		editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_DRAW);
	}
}

void CameraPathPalettePanel::OnToggleShowPaths(wxCommandEvent&) {
	bool show = show_paths_checkbox->GetValue();
	g_settings.setInteger(Config::SHOW_CAMERA_PATHS, show ? 1 : 0);
	g_gui.RefreshView();
}

void CameraPathPalettePanel::OnGetZoom(wxCommandEvent&) {
	MapTab* tab = g_gui.GetCurrentMapTab();
	if (!tab) {
		return;
	}

	MapCanvas* canvas = tab->GetCanvas();
	if (!canvas) {
		return;
	}

	double zoom = canvas->GetZoom();
	zoom_ctrl->ChangeValue(wxString::Format("%.2f", zoom));
}
