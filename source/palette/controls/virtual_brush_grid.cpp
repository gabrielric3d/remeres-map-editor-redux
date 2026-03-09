#include "app/main.h"
#include "palette/controls/virtual_brush_grid.h"
#include "ui/gui.h"
#include "rendering/core/graphics.h"

#include <glad/glad.h>

#include <nanovg.h>
#include <nanovg_gl.h>

#include "util/nvg_utils.h"
#include "ui/theme.h"

#include <spdlog/spdlog.h>
#include <algorithm>

namespace {
	static constexpr float GROW_FACTOR = 2.0f;
	static constexpr float SHADOW_ALPHA_BASE = 20.0f;
	static constexpr float SHADOW_ALPHA_FACTOR = 64.0f;
	static constexpr float SHADOW_BLUR_BASE = 6.0f;
	static constexpr float SHADOW_BLUR_FACTOR = 4.0f;
	static constexpr int TIMER_INTERVAL = 16;
	static constexpr float INTER_THRESHOLD = 0.01f;
	static constexpr float INTER_FACTOR = 0.2f;
}

VirtualBrushGrid::VirtualBrushGrid(wxWindow* parent, const TilesetCategory* _tileset, RenderSize rsz) :
	NanoVGCanvas(parent, wxID_ANY, wxVSCROLL | wxWANTS_CHARS),
	BrushBoxInterface(_tileset),
	icon_size(rsz),
	selected_index(-1),
	hover_index(-1),
	columns(1),
	item_size(0),
	padding(4),
	m_animTimer(this) {

	if (icon_size == RENDER_SIZE_16x16) {
		item_size = 18;
	} else {
		item_size = GRID_ITEM_SIZE_BASE + 2; // + borders
	}

	Bind(wxEVT_LEFT_DOWN, &VirtualBrushGrid::OnMouseDown, this);
	Bind(wxEVT_MOTION, &VirtualBrushGrid::OnMotion, this);
	Bind(wxEVT_SIZE, &VirtualBrushGrid::OnSize, this);
	Bind(wxEVT_TIMER, &VirtualBrushGrid::OnTimer, this);

	UpdateLayout();
}

VirtualBrushGrid::~VirtualBrushGrid() = default;

void VirtualBrushGrid::SetDisplayMode(DisplayMode mode) {
	if (display_mode != mode) {
		display_mode = mode;
		UpdateLayout();
		Refresh();
	}
}

// ============================================================================
// Filter support

size_t VirtualBrushGrid::GetEffectiveBrushCount() const {
	if (filter_active) {
		return filtered_indices.size();
	}
	return tileset->size();
}

Brush* VirtualBrushGrid::GetEffectiveBrush(size_t index) const {
	if (filter_active) {
		if (index < filtered_indices.size()) {
			return tileset->brushlist[filtered_indices[index]];
		}
		return nullptr;
	}
	if (index < tileset->size()) {
		return tileset->brushlist[index];
	}
	return nullptr;
}

void VirtualBrushGrid::SetFilter(const std::string& filter) {
	current_filter = as_lower_str(filter);
	RebuildFilteredList();
	selected_index = -1;
	hover_index = -1;
	if (GetEffectiveBrushCount() > 0) {
		selected_index = 0;
	}
	UpdateLayout();
	SetScrollPosition(0);
	Refresh();
}

void VirtualBrushGrid::ClearFilter() {
	if (!filter_active && current_filter.empty()) {
		return;
	}
	current_filter.clear();
	filtered_indices.clear();
	filter_active = false;
	selected_index = -1;
	hover_index = -1;
	if (GetEffectiveBrushCount() > 0) {
		selected_index = 0;
	}
	UpdateLayout();
	SetScrollPosition(0);
	Refresh();
}

void VirtualBrushGrid::RebuildFilteredList() {
	filtered_indices.clear();
	if (current_filter.empty()) {
		filter_active = false;
		return;
	}

	filter_active = true;
	for (size_t i = 0; i < tileset->size(); ++i) {
		Brush* brush = tileset->brushlist[i];
		if (brush && as_lower_str(brush->getName()).find(current_filter) != std::string::npos) {
			filtered_indices.push_back(i);
		}
	}
}

// ============================================================================

void VirtualBrushGrid::UpdateLayout() {
	int width = GetClientSize().x;
	if (width <= 0) {
		width = 200; // Default
	}

	int count = static_cast<int>(GetEffectiveBrushCount());

	if (display_mode == DisplayMode::List) {
		columns = 1;
		int contentHeight = count * LIST_ROW_HEIGHT + padding;
		UpdateScrollbar(contentHeight);
	} else {
		columns = std::max(1, (width - padding) / (item_size + padding));
		int rows = (count + columns - 1) / columns;
		int contentHeight = rows * (item_size + padding) + padding;
		UpdateScrollbar(contentHeight);
	}
}

wxSize VirtualBrushGrid::DoGetBestClientSize() const {
	return FromDIP(wxSize(200, 300));
}

void VirtualBrushGrid::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
	// Calculate visible range
	int scrollPos = GetScrollPosition();
	int rowHeight = (display_mode == DisplayMode::List) ? LIST_ROW_HEIGHT : (item_size + padding);
	int startRow = scrollPos / rowHeight;
	int endRow = (scrollPos + height + rowHeight - 1) / rowHeight + 1;

	int startIdx = startRow * columns;
	int endIdx = std::min(static_cast<int>(GetEffectiveBrushCount()), endRow * columns);

	// Draw visible items
	for (int i = startIdx; i < endIdx; ++i) {
		DrawBrushItem(vg, i, GetItemRect(i));
	}
}

void VirtualBrushGrid::DrawBrushItem(NVGcontext* vg, int i, const wxRect& rect) {
	float x = static_cast<float>(rect.x);
	float y = static_cast<float>(rect.y);
	float w = static_cast<float>(rect.width);
	float h = static_cast<float>(rect.height);

	// Animation scaling
	if (i == hover_index) {
		float grow = GROW_FACTOR * hover_anim;
		x -= grow;
		y -= grow;
		w += grow * 2.0f;
		h += grow * 2.0f;
	}

	// Shadow / Glow
	if (i == selected_index) {
		// Glow for selected
		NVGpaint shadowPaint = nvgBoxGradient(vg, x, y, w, h, 4.0f, 10.0f, nvgRGBA(100, 150, 255, 128), nvgRGBA(0, 0, 0, 0));
		nvgBeginPath(vg);
		nvgRect(vg, x - 10, y - 10, w + 20, h + 20);
		nvgRoundedRect(vg, x, y, w, h, 4.0f);
		nvgPathWinding(vg, NVG_HOLE);
		nvgFillPaint(vg, shadowPaint);
		nvgFill(vg);
	} else if (i == hover_index) {
		// Animated shadow for hover
		float shadowAlpha = SHADOW_ALPHA_FACTOR * hover_anim + SHADOW_ALPHA_BASE;
		float shadowBlur = SHADOW_BLUR_BASE + SHADOW_BLUR_FACTOR * hover_anim;
		NVGpaint shadowPaint = nvgBoxGradient(vg, x, y + 2, w, h, 4.0f, shadowBlur, nvgRGBA(0, 0, 0, static_cast<int>(shadowAlpha)), nvgRGBA(0, 0, 0, 0));
		nvgBeginPath(vg);
		nvgRect(vg, x - 10, y - 10, w + 20, h + 20);
		nvgRoundedRect(vg, x, y, w, h, 4.0f);
		nvgPathWinding(vg, NVG_HOLE);
		nvgFillPaint(vg, shadowPaint);
		nvgFill(vg);
	}

	// Card background
	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, h, 4.0f);

	if (i == selected_index) {
		NVGcolor selCol = NvgUtils::ToNvColor(Theme::Get(Theme::Role::Accent));
		selCol.a = 1.0f; // Force opaque for background
		nvgFillColor(vg, selCol);
	} else if (i == hover_index) {
		nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBaseHover)));
	} else {
		// Normal - theme card base
		nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBase)));
	}
	nvgFill(vg);

	// Selection border
	if (i == selected_index) {
		nvgBeginPath(vg);
		nvgRoundedRect(vg, x + 0.5f, y + 0.5f, w - 1.0f, h - 1.0f, 4.0f);
		nvgStrokeColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::Accent)));
		nvgStrokeWidth(vg, 2.0f);
		nvgStroke(vg);
	}

	// Draw brush sprite
	Brush* brush = GetEffectiveBrush(static_cast<size_t>(i));
	if (brush) {
		Sprite* spr = brush->getSprite();
		if (!spr) {
			spr = g_gui.gfx.getSprite(brush->getLookID());
		}

		if (!spr) {
			return; // Safety check
		}

		int tex = GetOrCreateSpriteTexture(vg, spr);
		if (tex > 0) {
			int iconSize = (display_mode == DisplayMode::List) ? GRID_ITEM_SIZE_BASE : (item_size - 2 * ICON_OFFSET);
			int iconX = rect.x + ICON_OFFSET;
			int iconY = rect.y + ICON_OFFSET;

			NVGpaint imgPaint = nvgImagePattern(vg, static_cast<float>(iconX), static_cast<float>(iconY), static_cast<float>(iconSize), static_cast<float>(iconSize), 0.0f, tex, 1.0f);

			nvgBeginPath(vg);
			nvgRoundedRect(vg, static_cast<float>(iconX), static_cast<float>(iconY), static_cast<float>(iconSize), static_cast<float>(iconSize), 3.0f);
			nvgFillPaint(vg, imgPaint);
			nvgFill(vg);
		}

		if (display_mode == DisplayMode::List) {
			nvgFontSize(vg, 14.0f);
			nvgFontFace(vg, "sans");
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::Text)));

			auto it = m_utf8NameCache.find(brush);
			if (it == m_utf8NameCache.end()) {
				m_utf8NameCache[brush] = std::string(wxstr(brush->getName()).ToUTF8());
				it = m_utf8NameCache.find(brush);
			}
			nvgText(vg, rect.x + 40, rect.y + rect.height / 2.0f, it->second.c_str(), nullptr);
		}
	}
}

wxRect VirtualBrushGrid::GetItemRect(int index) const {
	if (display_mode == DisplayMode::List) {
		int width = GetClientSize().x - 2 * padding;
		return wxRect(padding, padding + index * LIST_ROW_HEIGHT, width, LIST_ROW_HEIGHT);
	} else {
		int row = index / columns;
		int col = index % columns;

		return wxRect(
			padding + col * (item_size + padding),
			padding + row * (item_size + padding),
			item_size,
			item_size
		);
	}
}

int VirtualBrushGrid::HitTest(int x, int y) const {
	int scrollPos = GetScrollPosition();
	int realY = y + scrollPos;
	int realX = x;

	int count = static_cast<int>(GetEffectiveBrushCount());

	if (display_mode == DisplayMode::List) {
		int row = (realY - padding) / LIST_ROW_HEIGHT;

		if (row < 0 || row >= count) {
			return -1;
		}

		int index = row;
		if (realX >= padding && realX <= GetClientSize().x - padding) {
			return index;
		}
		return -1;
	} else {
		int col = (realX - padding) / (item_size + padding);
		int row = (realY - padding) / (item_size + padding);

		if (col < 0 || col >= columns || row < 0) {
			return -1;
		}

		int index = row * columns + col;
		if (index >= 0 && index < count) {
			wxRect rect = GetItemRect(index);
			// Adjust rect to scroll position for contains check
			rect.y -= scrollPos;
			if (rect.Contains(x, y)) {
				return index;
			}
		}
		return -1;
	}
}

void VirtualBrushGrid::OnMouseDown(wxMouseEvent& event) {
	int index = HitTest(event.GetX(), event.GetY());
	if (index != -1 && index != selected_index) {
		selected_index = index;

		// Notify GUI - find PaletteWindow parent
		wxWindow* w = GetParent();
		while (w) {
			PaletteWindow* pw = dynamic_cast<PaletteWindow*>(w);
			if (pw) {
				g_gui.ActivatePalette(pw);
				break;
			}
			w = w->GetParent();
		}

		Brush* brush = GetEffectiveBrush(static_cast<size_t>(selected_index));
		if (brush) {
			g_gui.SelectBrush(brush, tileset->getType());
		}
		Refresh();
	}
}

void VirtualBrushGrid::OnMotion(wxMouseEvent& event) {
	int index = HitTest(event.GetX(), event.GetY());

	if (index != hover_index) {
		hover_index = index;
		if (index != -1) {
			hover_anim = 0.0f; // Reset animation for new target
		}
		if (!m_animTimer.IsRunning()) {
			m_animTimer.Start(TIMER_INTERVAL);
		}
		Refresh();
	} else if (hover_index != -1 && !m_animTimer.IsRunning()) {
		m_animTimer.Start(TIMER_INTERVAL);
	}

	// Tooltip
	if (index != -1) {
		Brush* brush = GetEffectiveBrush(static_cast<size_t>(index));
		if (brush) {
			wxString tip = wxstr(brush->getName());
			if (GetToolTipText() != tip) {
				SetToolTip(tip);
			}
		}
	} else {
		UnsetToolTip();
	}

	event.Skip();
}

void VirtualBrushGrid::OnTimer(wxTimerEvent& event) {
	float target = (hover_index != -1) ? 1.0f : 0.0f;
	if (std::abs(hover_anim - target) > INTER_THRESHOLD) {
		hover_anim += (target - hover_anim) * INTER_FACTOR;
		Refresh();
	} else {
		hover_anim = target;
		if (hover_index == -1) {
			m_animTimer.Stop();
		}
	}
}

void VirtualBrushGrid::OnSize(wxSizeEvent& event) {
	UpdateLayout();
	Refresh();
	event.Skip();
}

void VirtualBrushGrid::SelectFirstBrush() {
	if (GetEffectiveBrushCount() > 0) {
		selected_index = 0;
		Refresh();
	}
}

Brush* VirtualBrushGrid::GetSelectedBrush() const {
	if (selected_index >= 0 && selected_index < static_cast<int>(GetEffectiveBrushCount())) {
		return GetEffectiveBrush(static_cast<size_t>(selected_index));
	}
	return nullptr;
}

bool VirtualBrushGrid::SelectBrush(const Brush* brush) {
	size_t count = GetEffectiveBrushCount();
	for (size_t i = 0; i < count; ++i) {
		if (GetEffectiveBrush(i) == brush) {
			selected_index = static_cast<int>(i);

			// Ensure visible
			wxRect rect = GetItemRect(selected_index);
			int scrollPos = GetScrollPosition();
			int clientHeight = GetClientSize().y;

			if (rect.y < scrollPos) {
				SetScrollPosition(rect.y - padding);
			} else if (rect.y + rect.height > scrollPos + clientHeight) {
				SetScrollPosition(rect.y + rect.height - clientHeight + padding);
			}

			Refresh();
			return true;
		}
	}
	selected_index = -1;
	Refresh();
	return false;
}
