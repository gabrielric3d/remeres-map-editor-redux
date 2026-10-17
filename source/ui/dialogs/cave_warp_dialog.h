#ifndef RME_CAVE_WARP_DIALOG_H_
#define RME_CAVE_WARP_DIALOG_H_

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/tglbtn.h>

#include "editor/cave_warp.h"

#include <utility>
#include <vector>

// Tools > Cave Warp: presets learned from finished caves, the brushes that paint
// the cave shape on the map, and a test cave drawn with the preset to check what
// it learned.
class CaveWarpDialog : public wxDialog {
public:
	explicit CaveWarpDialog(wxWindow* parent);
	~CaveWarpDialog() override;

private:
	class PreviewPanel;

	void CreateControls();
	void RefreshPresetChoice();
	void RefreshGrounds();
	void RefreshFromTool();
	void ShowPreview(bool withSelection);

	void OnPresetChanged(wxCommandEvent& event);
	void OnNewPreset(wxCommandEvent& event);
	void OnDeletePreset(wxCommandEvent& event);
	void OnLearn(wxCommandEvent& event);
	void OnTestSelection(wxCommandEvent& event);
	void OnRemoveSample(wxCommandEvent& event);
	void OnNewTestCave(wxCommandEvent& event);
	void OnSetClass(uint8_t cls);
	void OnToolButton(CaveWarp::Tool tool);
	void OnStop(wxCommandEvent& event);
	void OnOptionChanged(wxCommandEvent& event);
	void OnClose(wxCloseEvent& event);

	wxChoice* m_presetChoice = nullptr;
	wxStaticText* m_info = nullptr;
	wxListCtrl* m_groundList = nullptr;
	wxImageList* m_groundImages = nullptr;
	std::vector<std::pair<CaveWarp::Tool, wxToggleButton*>> m_toolButtons;
	wxChoice* m_shapeChoice = nullptr;
	wxChoice* m_sizeChoice = nullptr;
	wxSpinCtrl* m_marginSpin = nullptr;
	wxCheckBox* m_fixCheck = nullptr;
	wxCheckBox* m_protectCheck = nullptr;
	wxStaticText* m_status = nullptr;
	wxStaticText* m_previewLabel = nullptr;
	PreviewPanel* m_preview = nullptr;
};

#endif
