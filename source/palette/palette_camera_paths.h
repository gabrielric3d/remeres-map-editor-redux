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

#include "palette/palette_common.h"
#include "game/camera_paths.h"

#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/choice.h>

#include <vector>

class CameraPathPalettePanel : public PalettePanel {
public:
	CameraPathPalettePanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~CameraPathPalettePanel();

	wxString GetName() const override;
	PaletteType GetType() const override;

	Brush* GetSelectedBrush() const override;
	int GetSelectedBrushSize() const override;
	bool SelectBrush(const Brush* whatbrush) override;

	void OnUpdate() override;
	void OnSwitchIn() override;
	void OnSwitchOut() override;

	void SetMap(Map* map);

	double GetKeyframeDuration() const;
	double GetKeyframeSpeed() const;
	double GetKeyframeZoom() const;
	int GetKeyframeZ() const;
	int GetKeyframeEasing() const;

protected:
	void RefreshPathList();
	void RefreshKeyframeList();
	void UpdateKeyframeControls();
	CameraPath* GetActivePath() const;
	int GetSelectedKeyframeIndex() const;
	std::vector<int> GetSelectedKeyframeIndices() const;
	Position GetCursorPosition() const;

	// Event handlers
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
};

#endif
