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

#ifndef RME_INSTANCE_LAYOUT_DIALOG_H
#define RME_INSTANCE_LAYOUT_DIALOG_H

#include <vector>

#include <wx/spinctrl.h>
#include <wx/wx.h>

#include "map/position.h"

class PositionCtrl;

class InstanceLayoutDialog : public wxDialog
{
public:
	explicit InstanceLayoutDialog(wxWindow* parent);
	virtual ~InstanceLayoutDialog() = default;

	void UpdateEditor();

private:
	struct LayoutConfig {
		Position centerPos;
		Position fromPos;
		Position toPos;
		Position playerSpawnPos;
		Position bossSpawnPos;

		int leftCount = 0;
		int rightCount = 0;
		int northCount = 0;
		int southCount = 0;
		int spacingX = 0;
		int spacingY = 0;

		bool includeCenterInText = true;
		bool includeCenterInCopy = false;
	};

	struct InstancePlacement {
		int columnIndex = 0;
		int rowIndex = 0;
		bool isCenter = false;
		Position offset;
		Position centerPos;
		Position fromPos;
		Position toPos;
		Position playerSpawnPos;
		Position bossSpawnPos;
	};

	void CreateControls();
	void ApplySelectionBounds(bool showMessageWhenEmpty);
	void UpdateAreaSummary();
	void UpdateLayoutSummary();
	void RefreshBasePreview();
	bool BuildConfig(LayoutConfig& config, wxString& errorMessage) const;
	std::vector<InstancePlacement> BuildPlacements(const LayoutConfig& config) const;
	wxString BuildLuaText(const LayoutConfig& config, const std::vector<InstancePlacement>& placements) const;
	bool ApplyInstancesToMap(
		const LayoutConfig& config,
		const std::vector<InstancePlacement>& placements,
		wxString& errorMessage,
		int& instancesPlaced,
		int& tilesCopied
	);
	bool CopyTextToClipboard(const wxString& text) const;

	void OnPickArea(wxCommandEvent& event);
	void OnUseSelection(wxCommandEvent& event);
	void OnBasePositionTextChanged(wxCommandEvent& event);
	void OnLayoutSpinChanged(wxSpinEvent& event);
	void OnLayoutCheckChanged(wxCommandEvent& event);
	void OnGenerateText(wxCommandEvent& event);
	void OnApply(wxCommandEvent& event);
	void OnCloseButton(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);

private:
	PositionCtrl* m_centerPosCtrl = nullptr;
	PositionCtrl* m_fromPosCtrl = nullptr;
	PositionCtrl* m_toPosCtrl = nullptr;
	PositionCtrl* m_playerSpawnPosCtrl = nullptr;
	PositionCtrl* m_bossSpawnPosCtrl = nullptr;

	wxSpinCtrl* m_leftCountSpin = nullptr;
	wxSpinCtrl* m_rightCountSpin = nullptr;
	wxSpinCtrl* m_northCountSpin = nullptr;
	wxSpinCtrl* m_southCountSpin = nullptr;
	wxSpinCtrl* m_spacingXSpin = nullptr;
	wxSpinCtrl* m_spacingYSpin = nullptr;

	wxCheckBox* m_includeCenterInTextCheck = nullptr;
	wxCheckBox* m_includeCenterInCopyCheck = nullptr;
	wxCheckBox* m_copyTextToClipboardCheck = nullptr;

	wxStaticText* m_pickStatusText = nullptr;
	wxStaticText* m_areaSummaryText = nullptr;
	wxStaticText* m_layoutSummaryText = nullptr;
	wxPanel* m_basePreviewPanel = nullptr;
	wxTextCtrl* m_outputTextCtrl = nullptr;
	wxButton* m_applyButton = nullptr;

	wxDECLARE_EVENT_TABLE();
};

#endif // RME_INSTANCE_LAYOUT_DIALOG_H
