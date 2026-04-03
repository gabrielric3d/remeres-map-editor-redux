#include "ui/tool_options_surface.h"

#include "app/settings.h"
#include "brushes/brush.h"
#include "brushes/managers/brush_manager.h"
#include "brushes/border/optional_border_brush.h"
#include "brushes/door/door_brush.h"
#include "brushes/flag/flag_brush.h"
#include "game/sprites.h"
#include "rendering/core/game_sprite.h"
#include "ui/gui.h"
#include "util/image_manager.h"

#include <algorithm>
#include <format>
#include <wx/tglbtn.h>

namespace {
	constexpr int BRUSH_ICON_SIZE = 32;
	constexpr int TOOL_COLUMNS = 6;
	constexpr int TOOL_BUTTON_SIZE = 34;
	constexpr int MODE_BUTTON_ICON_SIZE = 18;
	constexpr int MIN_AXIS_SIZE = 0;
	constexpr int MAX_AXIS_SIZE = 15;
	constexpr int MIN_THICKNESS = 1;
	constexpr int MAX_THICKNESS = 100;
	const wxColour MODE_ON_COLOUR(102, 187, 106);
	const wxColour MODE_OFF_COLOUR(255, 214, 102);

	[[nodiscard]] int effectiveAxisSpan(int slider_value, bool exact) {
		return exact ? std::max(1, slider_value) : slider_value * 2 + 1;
	}

	void ConfigureFlatModeButton(wxBitmapToggleButton* button, const wxBitmap& bitmap) {
		if (!button) {
			return;
		}

		button->SetBitmap(bitmap);
		button->SetBitmapCurrent(bitmap);
		button->SetBitmapFocus(bitmap);
		button->SetBitmapPressed(bitmap);
	}
}

ToolOptionsSurface::ToolOptionsSurface(wxWindow* parent) :
	wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL) {
	BuildUi();
	ReloadSettings();
}

void ToolOptionsSurface::BuildUi() {
	main_sizer = newd wxBoxSizer(wxVERTICAL);

	main_tools_sizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Main tools");
	main_tools_grid = newd wxGridSizer(0, TOOL_COLUMNS, FromDIP(4), FromDIP(4));
	main_tools_sizer->Add(main_tools_grid, 0, wxEXPAND | wxALL, FromDIP(4));
	main_sizer->Add(main_tools_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(4));

	size_sizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Size");

	const auto create_axis_row = [&](const wxString& label, wxSlider*& slider, wxStaticText*& value_label) {
		auto* row = newd wxBoxSizer(wxHORIZONTAL);
		const wxSize mode_button_size = FromDIP(wxSize(26, 26));
		if (label == "X") {
			exact_button = newd wxBitmapToggleButton(size_sizer->GetStaticBox(), wxID_ANY, wxBitmap(), wxDefaultPosition, mode_button_size, wxBU_EXACTFIT | wxBORDER_NONE);
			exact_button->SetToolTip("Exact size");
			row->Add(exact_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
		} else {
			aspect_button = newd wxBitmapToggleButton(size_sizer->GetStaticBox(), wxID_ANY, wxBitmap(), wxDefaultPosition, mode_button_size, wxBU_EXACTFIT | wxBORDER_NONE);
			aspect_button->SetToolTip("Keep X and Y connected");
			row->Add(aspect_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
		}
		row->Add(newd wxStaticText(size_sizer->GetStaticBox(), wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
		slider = newd wxSlider(size_sizer->GetStaticBox(), wxID_ANY, 0, MIN_AXIS_SIZE, MAX_AXIS_SIZE, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
		row->Add(slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
		value_label = newd wxStaticText(size_sizer->GetStaticBox(), wxID_ANY, "1");
		row->Add(value_label, 0, wxALIGN_CENTER_VERTICAL);
		size_sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
	};

	create_axis_row("X", size_x_slider, size_x_value);
	create_axis_row("Y", size_y_slider, size_y_value);

	main_sizer->Add(size_sizer, 0, wxEXPAND | wxALL, FromDIP(4));

	other_sizer = newd wxStaticBoxSizer(wxVERTICAL, this, "Other");
	auto* thickness_row = newd wxBoxSizer(wxHORIZONTAL);
	thickness_row->Add(newd wxStaticText(other_sizer->GetStaticBox(), wxID_ANY, "Thickness"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	thickness_slider = newd wxSlider(other_sizer->GetStaticBox(), wxID_ANY, MAX_THICKNESS, MIN_THICKNESS, MAX_THICKNESS, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
	thickness_row->Add(thickness_slider, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	thickness_value = newd wxStaticText(other_sizer->GetStaticBox(), wxID_ANY, "100%");
	thickness_row->Add(thickness_value, 0, wxALIGN_CENTER_VERTICAL);
	other_sizer->Add(thickness_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

	preview_border_checkbox = newd wxCheckBox(other_sizer->GetStaticBox(), wxID_ANY, "Preview Border");
	lock_doors_checkbox = newd wxCheckBox(other_sizer->GetStaticBox(), wxID_ANY, "Lock Doors (Shift)");
	other_sizer->Add(preview_border_checkbox, 0, wxEXPAND | wxALL, FromDIP(6));
	other_sizer->Add(lock_doors_checkbox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
	main_sizer->Add(other_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

	SetSizer(main_sizer);

	size_x_slider->Bind(wxEVT_SLIDER, &ToolOptionsSurface::OnSizeXChanged, this);
	size_y_slider->Bind(wxEVT_SLIDER, &ToolOptionsSurface::OnSizeYChanged, this);
	exact_button->Bind(wxEVT_TOGGLEBUTTON, &ToolOptionsSurface::OnExactToggled, this);
	aspect_button->Bind(wxEVT_TOGGLEBUTTON, &ToolOptionsSurface::OnAspectToggled, this);
	preview_border_checkbox->Bind(wxEVT_CHECKBOX, &ToolOptionsSurface::OnPreviewBorderToggled, this);
	lock_doors_checkbox->Bind(wxEVT_CHECKBOX, &ToolOptionsSurface::OnLockDoorsToggled, this);
	thickness_slider->Bind(wxEVT_SLIDER, &ToolOptionsSurface::OnThicknessChanged, this);
}

void ToolOptionsSurface::SetPaletteType(PaletteType type) {
	current_type = type;
	RebuildToolButtons();
	RefreshFromState();
}

void ToolOptionsSurface::SetActiveBrush(Brush* brush) {
	active_brush = brush;
	SyncToolSelection();
}

void ToolOptionsSurface::UpdateBrushSize(BrushShape shape, int size) {
	(void)shape;
	(void)size;
	RefreshFromState();
}

void ToolOptionsSurface::ReloadSettings() {
	active_brush = g_gui.GetCurrentBrush();
	RefreshFromState();
}

void ToolOptionsSurface::Clear() {
	current_type = TILESET_UNKNOWN;
	active_brush = nullptr;
	RebuildToolButtons();
	RefreshFromState();
}

void ToolOptionsSurface::RebuildToolButtons() {
	if (!main_tools_grid) {
		return;
	}

	for (auto& entry : tool_buttons) {
		if (entry.button) {
			main_tools_grid->Detach(entry.button);
			entry.button->Destroy();
		}
	}
	tool_buttons.clear();

	for (Brush* brush : GetToolsForCurrentPalette()) {
		auto* button = newd wxBitmapToggleButton(
			main_tools_sizer->GetStaticBox(),
			wxID_ANY,
			CreateBrushBitmap(brush),
			wxDefaultPosition,
			FromDIP(wxSize(TOOL_BUTTON_SIZE, TOOL_BUTTON_SIZE)),
			wxBU_EXACTFIT
		);
		button->SetMinSize(FromDIP(wxSize(TOOL_BUTTON_SIZE, TOOL_BUTTON_SIZE)));
		button->SetMaxSize(FromDIP(wxSize(TOOL_BUTTON_SIZE, TOOL_BUTTON_SIZE)));
		button->SetToolTip(wxstr(brush->getName()));
		button->Bind(wxEVT_TOGGLEBUTTON, &ToolOptionsSurface::OnToolButton, this);
		main_tools_grid->Add(button, 0, wxALIGN_CENTER);
		tool_buttons.push_back(ToolButtonEntry { .brush = brush, .button = button });
	}

	SyncToolSelection();
	UpdateSectionVisibility();
	Layout();
}

void ToolOptionsSurface::RefreshFromState() {
	SetMutatingUi(true);

	const auto state = g_gui.GetBrushSizeState();
	const int size_min = state.exact ? 1 : 0;
	size_x_slider->SetRange(size_min, MAX_AXIS_SIZE);
	size_y_slider->SetRange(size_min, MAX_AXIS_SIZE);
	size_x_slider->SetValue(state.size_x);
	size_y_slider->SetValue(state.size_y);
	exact_button->SetValue(state.exact);
	aspect_button->SetValue(state.aspect_locked);
	preview_border_checkbox->SetValue(g_settings.getInteger(Config::SHOW_AUTOBORDER_PREVIEW));
	lock_doors_checkbox->SetValue(g_settings.getInteger(Config::DRAW_LOCKED_DOOR));
	const int thickness_percent = g_brush_manager.UseCustomThickness() ? std::clamp(static_cast<int>(g_brush_manager.GetCustomThicknessMod() * 100.0f), MIN_THICKNESS, MAX_THICKNESS) : MAX_THICKNESS;
	thickness_slider->SetValue(thickness_percent);
	UpdateSizeLabels();
	UpdateModeButtons();
	thickness_value->SetLabel(std::format("{}%", thickness_slider->GetValue()));
	active_brush = g_gui.GetCurrentBrush();
	SyncToolSelection();
	SetMutatingUi(false);

	UpdateSectionVisibility();
	Layout();
}

void ToolOptionsSurface::UpdateSectionVisibility() {
	const bool show_tools = !tool_buttons.empty();
	const bool show_size = current_type != TILESET_UNKNOWN;
	const bool show_thickness = current_type == TILESET_COLLECTION || current_type == TILESET_DOODAD;
	const bool show_options = current_type == TILESET_TERRAIN || current_type == TILESET_COLLECTION;

	main_sizer->Show(main_tools_sizer, show_tools, true);
	main_sizer->Show(size_sizer, show_size, true);
	other_sizer->Show(thickness_slider->GetContainingSizer(), show_thickness, true);
	preview_border_checkbox->Show(show_options);
	lock_doors_checkbox->Show(show_options);
	main_sizer->Show(other_sizer, show_thickness || show_options, true);
}

void ToolOptionsSurface::UpdateSizeLabels() {
	const bool exact = g_gui.IsExactBrushSize();
	size_x_value->SetLabel(std::format("{}", effectiveAxisSpan(g_gui.GetBrushSizeX(), exact)));
	size_y_value->SetLabel(std::format("{}", effectiveAxisSpan(g_gui.GetBrushSizeY(), exact)));
}

void ToolOptionsSurface::UpdateModeButtons() {
	if (exact_button) {
		const bool exact = g_gui.IsExactBrushSize();
		const auto bitmap = CreateModeBitmap("svg/solid/bullseye.svg", exact ? MODE_ON_COLOUR : MODE_OFF_COLOUR);
		ConfigureFlatModeButton(exact_button, bitmap);
		exact_button->SetToolTip(exact ? "Exact size is on" : "Exact size is off");
	}

	if (aspect_button) {
		const bool connected = g_gui.IsBrushAspectRatioLocked();
		const auto bitmap = CreateModeBitmap(connected ? "svg/solid/link.svg" : "svg/solid/unlink.svg", connected ? MODE_ON_COLOUR : MODE_OFF_COLOUR);
		ConfigureFlatModeButton(aspect_button, bitmap);
		aspect_button->SetToolTip(connected ? "X and Y are connected" : "X and Y move independently");
	}
}

void ToolOptionsSurface::SyncToolSelection() {
	for (const auto& entry : tool_buttons) {
		if (entry.button) {
			entry.button->SetValue(active_brush == entry.brush);
		}
	}
}

void ToolOptionsSurface::SetMutatingUi(bool value) {
	mutating_ui = value;
}

bool ToolOptionsSurface::IsMutatingUi() const {
	return mutating_ui;
}

std::vector<Brush*> ToolOptionsSurface::GetToolsForCurrentPalette() const {
	std::vector<Brush*> brushes;
	const bool has_tools = current_type == TILESET_TERRAIN || current_type == TILESET_COLLECTION;
	if (!has_tools) {
		return brushes;
	}

	if (g_brush_manager.optional_brush) {
		brushes.push_back(g_brush_manager.optional_brush);
	}
	if (g_brush_manager.eraser) {
		brushes.push_back(g_brush_manager.eraser);
	}
	if (g_brush_manager.pz_brush) {
		brushes.push_back(g_brush_manager.pz_brush);
	}
	if (g_brush_manager.rook_brush) {
		brushes.push_back(g_brush_manager.rook_brush);
	}
	if (g_brush_manager.nolog_brush) {
		brushes.push_back(g_brush_manager.nolog_brush);
	}
	if (g_brush_manager.pvp_brush) {
		brushes.push_back(g_brush_manager.pvp_brush);
	}
	if (g_brush_manager.normal_door_brush) {
		brushes.push_back(g_brush_manager.normal_door_brush);
	}
	if (g_brush_manager.locked_door_brush) {
		brushes.push_back(g_brush_manager.locked_door_brush);
	}
	if (g_brush_manager.magic_door_brush) {
		brushes.push_back(g_brush_manager.magic_door_brush);
	}
	if (g_brush_manager.quest_door_brush) {
		brushes.push_back(g_brush_manager.quest_door_brush);
	}
	if (g_brush_manager.hatch_door_brush) {
		brushes.push_back(g_brush_manager.hatch_door_brush);
	}
	if (g_brush_manager.window_door_brush) {
		brushes.push_back(g_brush_manager.window_door_brush);
	}
	if (g_brush_manager.archway_door_brush) {
		brushes.push_back(g_brush_manager.archway_door_brush);
	}

	return brushes;
}

wxBitmap ToolOptionsSurface::CreateBrushBitmap(Brush* brush) const {
	if (!brush) {
		return wxBitmap(FromDIP(wxSize(BRUSH_ICON_SIZE, BRUSH_ICON_SIZE)));
	}

	Sprite* sprite = brush->getSprite();
	if (!sprite && brush->getLookID() != 0) {
		sprite = g_gui.gfx.getSprite(brush->getLookID());
	}

	if (sprite) {
		wxBitmap bitmap(FromDIP(wxSize(BRUSH_ICON_SIZE, BRUSH_ICON_SIZE)));
		wxMemoryDC dc(bitmap);
		dc.SetBackground(*wxWHITE_BRUSH);
		dc.Clear();
		const int x_offset = (bitmap.GetWidth() - BRUSH_ICON_SIZE) / 2;
		const int y_offset = (bitmap.GetHeight() - BRUSH_ICON_SIZE) / 2;
		sprite->DrawTo(&dc, SPRITE_SIZE_32x32, x_offset, y_offset, BRUSH_ICON_SIZE, BRUSH_ICON_SIZE);
		dc.SelectObject(wxNullBitmap);
		return bitmap;
	}

	wxBitmap bitmap(FromDIP(wxSize(BRUSH_ICON_SIZE, BRUSH_ICON_SIZE)));
	wxMemoryDC dc(bitmap);
	dc.SetBackground(*wxLIGHT_GREY_BRUSH);
	dc.Clear();
	dc.DrawLabel(wxstr(brush->getName()).Left(1), wxRect(wxPoint(0, 0), bitmap.GetSize()), wxALIGN_CENTER);
	dc.SelectObject(wxNullBitmap);
	return bitmap;
}

wxBitmap ToolOptionsSurface::CreateModeBitmap(std::string_view assetPath, const wxColour& tint) const {
	return IMAGE_MANAGER.GetBitmap(assetPath, FromDIP(wxSize(MODE_BUTTON_ICON_SIZE, MODE_BUTTON_ICON_SIZE)), tint);
}

void ToolOptionsSurface::OnToolButton(wxCommandEvent& event) {
	if (IsMutatingUi()) {
		return;
	}

	auto* button = dynamic_cast<wxBitmapToggleButton*>(event.GetEventObject());
	if (!button) {
		return;
	}

	for (const auto& entry : tool_buttons) {
		if (entry.button != button) {
			continue;
		}

		active_brush = entry.brush;
		g_gui.SelectBrush(entry.brush);
		g_gui.SetStatusText(std::format("Selected Tool: {}", entry.brush->getName()));
		break;
	}

	SyncToolSelection();
}

void ToolOptionsSurface::OnSizeXChanged(wxCommandEvent& event) {
	if (IsMutatingUi()) {
		return;
	}

	g_gui.SetBrushSizeX(event.GetInt());
	UpdateSizeLabels();
	g_gui.SetStatusText(std::format("Brush size X: {}", effectiveAxisSpan(g_gui.GetBrushSizeX(), g_gui.IsExactBrushSize())));
}

void ToolOptionsSurface::OnSizeYChanged(wxCommandEvent& event) {
	if (IsMutatingUi()) {
		return;
	}

	g_gui.SetBrushSizeY(event.GetInt());
	UpdateSizeLabels();
	g_gui.SetStatusText(std::format("Brush size Y: {}", effectiveAxisSpan(g_gui.GetBrushSizeY(), g_gui.IsExactBrushSize())));
}

void ToolOptionsSurface::OnExactToggled(wxCommandEvent& event) {
	if (IsMutatingUi()) {
		return;
	}

	g_gui.SetExactBrushSize(event.IsChecked());
	RefreshFromState();
}

void ToolOptionsSurface::OnAspectToggled(wxCommandEvent& event) {
	if (IsMutatingUi()) {
		return;
	}

	g_gui.SetBrushAspectRatioLocked(event.IsChecked());
	RefreshFromState();
}

void ToolOptionsSurface::OnPreviewBorderToggled(wxCommandEvent& event) {
	if (IsMutatingUi()) {
		return;
	}

	g_settings.setInteger(Config::SHOW_AUTOBORDER_PREVIEW, event.IsChecked());
	g_gui.RefreshView();
}

void ToolOptionsSurface::OnLockDoorsToggled(wxCommandEvent& event) {
	if (IsMutatingUi()) {
		return;
	}

	g_settings.setInteger(Config::DRAW_LOCKED_DOOR, event.IsChecked());
	g_brush_manager.SetDoorLocked(event.IsChecked());
}

void ToolOptionsSurface::OnThicknessChanged(wxCommandEvent& event) {
	if (IsMutatingUi()) {
		return;
	}

	thickness_value->SetLabel(std::format("{}%", event.GetInt()));
	g_brush_manager.SetBrushThickness(true, event.GetInt(), 100);
}
