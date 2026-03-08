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

#include "main.h"

#include "recent_brushes_window.h"

#include <algorithm>
#include <array>
#include <wx/statline.h>
#include <wx/wrapsizer.h>
#include <wx/scrolwin.h>
#include <wx/button.h>

#include "brush.h"
#include "gui.h"
#include "sprites.h"
#include <wx/dcbuffer.h>

namespace {

constexpr size_t kMaxButtonsPerCategory = 32;

wxSize GetButtonPixelSize(RenderSize size)
{
	switch(size) {
	case RENDER_SIZE_16x16: return wxSize(20, 20);
	case RENDER_SIZE_32x32: return wxSize(36, 36);
	case RENDER_SIZE_64x64: return wxSize(68, 68);
	default: return wxSize(36, 36);
	}
}

const std::array<TilesetCategoryType, 8> kCategoryOrder = {
	TILESET_TERRAIN,
	TILESET_RAW,
	TILESET_DOODAD,
	TILESET_ITEM,
	TILESET_CREATURE,
	TILESET_HOUSE,
	TILESET_WAYPOINT,
	TILESET_CAMERA_PATH,
};

} // namespace

class RecentBrushButton : public ItemButton
{
public:
	RecentBrushButton(wxWindow* parent, const Brush* brush, TilesetCategoryType type) :
		ItemButton(parent, RENDER_SIZE_32x32, brush ? brush->getLookID() : 0, wxID_ANY),
		brush_(brush),
		category_(type),
		highlighted_(false)
	{
		if(brush_) {
			SetToolTip(wxstr(brush_->getName()));
		}
		const wxSize fixed_size = GetButtonPixelSize(size);
		SetMinSize(fixed_size);
		SetMaxSize(fixed_size);
		SetInitialSize(fixed_size);
		Bind(wxEVT_BUTTON, &RecentBrushButton::OnPressed, this);
		Bind(wxEVT_PAINT, &RecentBrushButton::OnPaint, this);
	}

	void SetHighlighted(bool active)
	{
		if(highlighted_ == active)
			return;

		highlighted_ = active;
		Refresh();
	}

	const Brush* GetBrush() const { return brush_; }

private:
	void OnPressed(wxCommandEvent& WXUNUSED(event))
	{
		if(brush_) {
			g_gui.ActivateBrush(brush_);
		}
	}

	void OnPaint(wxPaintEvent& event);

	const Brush* brush_;
	TilesetCategoryType category_;
	bool highlighted_;
};

void RecentBrushButton::OnPaint(wxPaintEvent& event)
{
	wxBufferedPaintDC pdc(this);

	if(g_gui.gfx.isUnloaded()) {
		return;
	}

	static std::unique_ptr<wxPen> highlight_pen;
	static std::unique_ptr<wxPen> dark_highlight_pen;
	static std::unique_ptr<wxPen> light_shadow_pen;
	static std::unique_ptr<wxPen> shadow_pen;

	if(highlight_pen.get() == nullptr)      highlight_pen.reset(newd wxPen(wxColor(0xFF,0xFF,0xFF), 1, wxSOLID));
	if(dark_highlight_pen.get() == nullptr) dark_highlight_pen.reset(newd wxPen(wxColor(0xD4,0xD0,0xC8), 1, wxSOLID));
	if(light_shadow_pen.get() == nullptr)   light_shadow_pen.reset(newd wxPen(wxColor(0x80,0x80,0x80), 1, wxSOLID));
	if(shadow_pen.get() == nullptr)         shadow_pen.reset(newd wxPen(wxColor(0x40,0x40,0x40), 1, wxSOLID));

	const wxSize size_px = GetButtonPixelSize(size);
	pdc.SetPen(*wxTRANSPARENT_PEN);
	pdc.SetBrush(wxBrush(GetParent()->GetBackgroundColour()));
	pdc.DrawRectangle(0, 0, size_px.GetWidth(), size_px.GetHeight());

	pdc.SetPen(*highlight_pen);
	pdc.DrawLine(0, 0, size_px.GetWidth() - 1, 0);
	pdc.DrawLine(0, 1, 0, size_px.GetHeight() - 1);
	pdc.SetPen(*dark_highlight_pen);
	pdc.DrawLine(1, 1, size_px.GetWidth() - 2, 1);
	pdc.DrawLine(1, 2, 1, size_px.GetHeight() - 2);
	pdc.SetPen(*light_shadow_pen);
	pdc.DrawLine(size_px.GetWidth() - 2, 1, size_px.GetWidth() - 2, size_px.GetHeight() - 2);
	pdc.DrawLine(1, size_px.GetHeight() - 2, size_px.GetWidth() - 1, size_px.GetHeight() - 2);
	pdc.SetPen(*shadow_pen);
	pdc.DrawLine(size_px.GetWidth() - 1, 0, size_px.GetWidth() - 1, size_px.GetHeight() - 1);
	pdc.DrawLine(0, size_px.GetHeight() - 1, size_px.GetWidth(), size_px.GetHeight() - 1);

	if(sprite) {
		if(size == RENDER_SIZE_16x16) {
			sprite->DrawTo(&pdc, SPRITE_SIZE_16x16, 2, 2);
		} else if(size == RENDER_SIZE_32x32) {
			sprite->DrawTo(&pdc, SPRITE_SIZE_32x32, 2, 2);
		}
	}

	if(!highlighted_) {
		return;
	}

	pdc.SetPen(wxPen(wxColour(200, 0, 0), 3));
	pdc.SetBrush(*wxTRANSPARENT_BRUSH);
	pdc.DrawRectangle(2, 2, size_px.GetWidth() - 4, size_px.GetHeight() - 4);
}

RecentBrushesWindow::RecentBrushesWindow(wxWindow* parent) :
	wxPanel(parent, wxID_ANY),
	selected_brush(nullptr)
{
	auto* root_sizer = newd wxBoxSizer(wxVERTICAL);

	auto* title = newd wxStaticText(this, wxID_ANY, "Recent Brushes");
	wxFont font = title->GetFont();
	font.MakeBold();
	title->SetFont(font);
	root_sizer->Add(title, 0, wxALL, 5);

	scroll_window = newd wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	scroll_window->SetScrollRate(5, 5);
	scroll_sizer = newd wxBoxSizer(wxVERTICAL);
	scroll_window->SetSizer(scroll_sizer);
	root_sizer->Add(scroll_window, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);

	clear_button = newd wxButton(this, wxID_ANY, "Clear All");
	clear_button->Bind(wxEVT_BUTTON, &RecentBrushesWindow::OnClearAll, this);
	root_sizer->Add(clear_button, 0, wxALIGN_CENTER | wxALL, 5);

	SetSizerAndFit(root_sizer);

	BuildInterface();
}

void RecentBrushesWindow::BuildInterface()
{
	for(TilesetCategoryType type : kCategoryOrder) {
		auto* container = newd wxPanel(scroll_window, wxID_ANY);
		auto* container_sizer = newd wxBoxSizer(wxVERTICAL);
		container->SetSizer(container_sizer);

		auto* label = newd wxStaticText(container, wxID_ANY, GetCategoryLabel(type));
		wxFont font = label->GetFont();
		font.MakeBold();
		label->SetFont(font);
		container_sizer->Add(label, 0, wxTOP | wxLEFT | wxRIGHT, 3);

		auto* line = newd wxStaticLine(container);
		container_sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 3);

		auto* brush_sizer = newd wxWrapSizer(wxHORIZONTAL);
		container_sizer->Add(brush_sizer, 0, wxEXPAND | wxALL, 2);

		scroll_sizer->Add(container, 0, wxEXPAND | wxBOTTOM, 6);

		CategoryWidgets widgets;
		widgets.container = container;
		widgets.label = label;
		widgets.brush_sizer = brush_sizer;
		categories[type] = widgets;

		container->Hide();
	}
}

void RecentBrushesWindow::UpdateBrushes(const RecentBrushMap& brushes)
{
	for(auto& [type, widgets] : categories) {
		for(RecentBrushButton* button : widgets.buttons) {
			widgets.brush_sizer->Detach(button);
			button->Destroy();
		}
		widgets.buttons.clear();

		auto it = brushes.find(type);
		if(it == brushes.end() || it->second.empty()) {
			widgets.container->Hide();
			continue;
		}

		size_t count = 0;
		for(const Brush* brush : it->second) {
			if(!brush) {
				continue;
			}
			if(count >= kMaxButtonsPerCategory) {
				break;
			}

			auto* button = newd RecentBrushButton(widgets.container, brush, type);
			widgets.brush_sizer->Add(button, 0, wxALL | wxFIXED_MINSIZE, 2);
			widgets.buttons.push_back(button);
			++count;
		}

		if(!widgets.buttons.empty()) {
			widgets.container->Show();
		} else {
			widgets.container->Hide();
		}
	}

	HideEmptyCategories();
	scroll_window->FitInside();
	Layout();

	SetSelectedBrush(selected_brush);
}

void RecentBrushesWindow::SetSelectedBrush(const Brush* brush)
{
	selected_brush = brush;
	for(auto& entry : categories) {
		for(RecentBrushButton* button : entry.second.buttons) {
			const bool highlighted = selected_brush && button->GetBrush() == selected_brush;
			button->SetHighlighted(highlighted);
		}
	}
}

void RecentBrushesWindow::HideEmptyCategories()
{
	bool any_visible = false;
	for(auto& entry : categories) {
		if(entry.second.container->IsShown()) {
			any_visible = true;
			break;
		}
	}

	clear_button->Enable(any_visible);
}

wxString RecentBrushesWindow::GetCategoryLabel(TilesetCategoryType type) const
{
	switch(type) {
	case TILESET_TERRAIN: return "Terrain";
	case TILESET_DOODAD: return "Doodad";
	case TILESET_ITEM: return "Item";
	case TILESET_RAW: return "RAW";
	case TILESET_CREATURE: return "Creatures";
	case TILESET_HOUSE: return "House";
	case TILESET_WAYPOINT: return "Waypoint";
	case TILESET_CAMERA_PATH: return "Camera Path";
	default: return "Other";
	}
}

void RecentBrushesWindow::OnClearAll(wxCommandEvent& WXUNUSED(event))
{
	g_gui.ClearRecentBrushes();
}
