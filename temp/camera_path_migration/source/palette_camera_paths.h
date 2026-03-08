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

#ifndef RME_PALETTE_CAMERA_PATHS_H_
#define RME_PALETTE_CAMERA_PATHS_H_

#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/choice.h>

#include <vector>

#include "palette_common.h"
#include "camera_path.h"

class CameraPathPalettePanel : public PalettePanel {
public:
	CameraPathPalettePanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~CameraPathPalettePanel();

	wxString GetName() const;
	PaletteType GetType() const;

	Brush* GetSelectedBrush() const;
	int GetSelectedBrushSize() const;
	bool SelectBrush(const Brush* whatbrush);

	void OnUpdate();
	void OnSwitchIn();
	void OnSwitchOut();

	void SetMap(Map* map);

	// Getters for keyframe properties from UI fields
	double GetKeyframeDuration() const;
	double GetKeyframeSpeed() const;
	double GetKeyframeZoom() const;
	int GetKeyframeZ() const;
	int GetKeyframeEasing() const;

	// wxWidgets event handling
	void OnClickPath(wxListEvent& event);
	void OnBeginEditPathLabel(wxListEvent& event);
	void OnEditPathLabel(wxListEvent& event);
	void OnAddPath(wxCommandEvent& event);
	void OnRemovePath(wxCommandEvent& event);
	void OnToggleLoop(wxCommandEvent& event);
	void OnPlayPause(wxCommandEvent& event);

	void OnClickKeyframe(wxListEvent& event);
	void OnDeselectKeyframe(wxListEvent& event);
	void OnAddKeyframe(wxCommandEvent& event);
	void OnRemoveKeyframe(wxCommandEvent& event);
	void OnClearKeyframes(wxCommandEvent& event);
	void OnKeyframeUp(wxCommandEvent& event);
	void OnKeyframeDown(wxCommandEvent& event);
	void OnApplyKeyframeProps(wxCommandEvent& event);
	void OnToggleShowPaths(wxCommandEvent& event);
	void OnGetZoom(wxCommandEvent& event);

protected:
	void RefreshPathList();
	void RefreshKeyframeList();
	void UpdateKeyframeControls();
	CameraPath* GetActivePath() const;
	int GetSelectedKeyframeIndex() const;
	std::vector<int> GetSelectedKeyframeIndices() const;
	Position GetCursorPosition() const;

	Map* map;
	wxListCtrl* path_list;
	wxListCtrl* keyframe_list;
	wxButton* add_path_button;
	wxButton* remove_path_button;
	wxButton* play_pause_button;
	wxCheckBox* loop_checkbox;
	wxCheckBox* show_paths_checkbox;

	wxButton* add_keyframe_button;
	wxButton* remove_keyframe_button;
	wxButton* clear_keyframes_button;
	wxButton* keyframe_up_button;
	wxButton* keyframe_down_button;
	wxButton* apply_props_button;

	wxStaticText* pos_label;
	wxTextCtrl* duration_ctrl;
	wxTextCtrl* speed_ctrl;
	wxTextCtrl* zoom_ctrl;
	wxButton* get_zoom_button;
	wxTextCtrl* z_ctrl;
	wxChoice* easing_ctrl;

	DECLARE_EVENT_TABLE()
};

#endif
