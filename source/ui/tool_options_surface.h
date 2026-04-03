#ifndef RME_UI_TOOL_OPTIONS_SURFACE_H_
#define RME_UI_TOOL_OPTIONS_SURFACE_H_

#include "app/main.h"
#include "palette/palette_common.h"

#include <vector>
#include <wx/wx.h>

class Brush;
class wxBitmapToggleButton;
class wxSlider;
class wxGridSizer;

class ToolOptionsSurface : public wxPanel {
public:
	ToolOptionsSurface(wxWindow* parent);
	~ToolOptionsSurface() override = default;

	void SetPaletteType(PaletteType type);
	void SetActiveBrush(Brush* brush);
	void UpdateBrushSize(BrushShape shape, int size);
	void ReloadSettings();
	void Clear();

private:
	struct ToolButtonEntry {
		Brush* brush = nullptr;
		wxBitmapToggleButton* button = nullptr;
	};

	void BuildUi();
	void RebuildToolButtons();
	void RefreshFromState();
	void UpdateSectionVisibility();
	void UpdateSizeLabels();
	void UpdateModeButtons();
	void SyncToolSelection();
	void SetMutatingUi(bool value);
	[[nodiscard]] bool IsMutatingUi() const;
	[[nodiscard]] std::vector<Brush*> GetToolsForCurrentPalette() const;
	[[nodiscard]] wxBitmap CreateBrushBitmap(Brush* brush) const;
	[[nodiscard]] wxBitmap CreateModeBitmap(std::string_view assetPath, const wxColour& tint) const;

	void OnToolButton(wxCommandEvent& event);
	void OnSizeXChanged(wxCommandEvent& event);
	void OnSizeYChanged(wxCommandEvent& event);
	void OnExactToggled(wxCommandEvent& event);
	void OnAspectToggled(wxCommandEvent& event);
	void OnPreviewBorderToggled(wxCommandEvent& event);
	void OnLockDoorsToggled(wxCommandEvent& event);
	void OnThicknessChanged(wxCommandEvent& event);

private:
	PaletteType current_type = TILESET_UNKNOWN;
	Brush* active_brush = nullptr;
	bool mutating_ui = false;

	wxBoxSizer* main_sizer = nullptr;
	wxStaticBoxSizer* main_tools_sizer = nullptr;
	wxGridSizer* main_tools_grid = nullptr;
	wxStaticBoxSizer* size_sizer = nullptr;
	wxStaticBoxSizer* other_sizer = nullptr;

	wxSlider* size_x_slider = nullptr;
	wxSlider* size_y_slider = nullptr;
	wxStaticText* size_x_value = nullptr;
	wxStaticText* size_y_value = nullptr;
	wxBitmapToggleButton* exact_button = nullptr;
	wxBitmapToggleButton* aspect_button = nullptr;
	wxCheckBox* preview_border_checkbox = nullptr;
	wxCheckBox* lock_doors_checkbox = nullptr;
	wxSlider* thickness_slider = nullptr;
	wxStaticText* thickness_value = nullptr;

	std::vector<ToolButtonEntry> tool_buttons;
};

#endif
