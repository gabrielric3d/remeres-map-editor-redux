#include "app/main.h"
#include "app/settings.h"
#include "palette/controls/virtual_brush_grid.h"
#include "palette/tileset_order.h"
#include "ui/gui.h"
#include "ui/gui_ids.h"
#include "rendering/core/graphics.h"
#include "brushes/raw/raw_brush.h"
#include "item_definitions/core/item_definition_store.h"

#include <glad/glad.h>

#include <nanovg.h>
#include <nanovg_gl.h>

#include "util/nvg_utils.h"
#include "ui/theme.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <deque>
#include <unordered_map>
#include <wx/clipbrd.h>
#include <wx/dnd.h>
#include <wx/dataobj.h>
#include <format>
#include "ui/map_tab.h"
#include "ui/map_window.h"
#include "ui/replace_tool/replace_tool_window.h"

namespace {
	static constexpr float GROW_FACTOR = 2.0f;
	static constexpr float SHADOW_ALPHA_BASE = 20.0f;
	static constexpr float SHADOW_ALPHA_FACTOR = 64.0f;
	static constexpr float SHADOW_BLUR_BASE = 6.0f;
	static constexpr float SHADOW_BLUR_FACTOR = 4.0f;
	static constexpr int TIMER_INTERVAL = 16;
	static constexpr float INTER_THRESHOLD = 0.01f;
	static constexpr float INTER_FACTOR = 0.2f;

	static constexpr int ANIM_TIMER_ID = 9900;
	static constexpr int PRELOAD_TIMER_ID = 9901;

	// Minimum cell width when names are drawn under the icons.
	static constexpr int LABEL_MIN_CELL_WIDTH = 64;
	static constexpr float LABEL_FONT_SIZE = 9.0f;

	// Grid cells widen when names are shown so the label has room to breathe.
	int CellWidthFor(int itemSize, bool showLabels) {
		return showLabels ? std::max(itemSize, LABEL_MIN_CELL_WIDTH) : itemSize;
	}

	// Server id used when sorting by ID, falls back to the look id.
	uint32_t GetBrushSortID(const Brush* brush) {
		if (!brush) {
			return 0;
		}
		if (brush->is<RAWBrush>()) {
			const RAWBrush* raw = brush->as<RAWBrush>();
			if (raw) {
				return raw->getItemID();
			}
		}
		const int lookId = brush->getLookID();
		return lookId > 0 ? static_cast<uint32_t>(lookId) : 0;
	}
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
	m_animTimer(this, ANIM_TIMER_ID),
	m_preloadTimer(this, PRELOAD_TIMER_ID) {

	if (icon_size == RENDER_SIZE_16x16) {
		item_size = 18;
	} else {
		item_size = g_settings.getInteger(Config::PALETTE_GRID_ICON_SIZE);
		if (item_size < 32) item_size = 32;
		if (item_size > 64) item_size = 64;
	}

	Bind(wxEVT_LEFT_DOWN, &VirtualBrushGrid::OnMouseDown, this);
	Bind(wxEVT_LEFT_UP, &VirtualBrushGrid::OnMouseUp, this);
	Bind(wxEVT_MOUSE_CAPTURE_LOST, &VirtualBrushGrid::OnCaptureLost, this);
	Bind(wxEVT_RIGHT_DOWN, &VirtualBrushGrid::OnRightClick, this);
	Bind(wxEVT_MOTION, &VirtualBrushGrid::OnMotion, this);
	Bind(wxEVT_SIZE, &VirtualBrushGrid::OnSize, this);
	Bind(wxEVT_TIMER, &VirtualBrushGrid::OnTimer, this, ANIM_TIMER_ID);
	Bind(wxEVT_TIMER, &VirtualBrushGrid::OnPreloadTimer, this, PRELOAD_TIMER_ID);
	Bind(wxEVT_MENU, &VirtualBrushGrid::OnCopyServerID, this, PALETTE_POPUP_MENU_COPY_SERVER_ID);
	Bind(wxEVT_MENU, &VirtualBrushGrid::OnCopyClientID, this, PALETTE_POPUP_MENU_COPY_CLIENT_ID);
	Bind(wxEVT_MENU, &VirtualBrushGrid::OnApplyReplaceOriginal, this, PALETTE_POPUP_MENU_APPLY_REPLACE_ORIGINAL);
	Bind(wxEVT_MENU, &VirtualBrushGrid::OnApplyReplaceReplacement, this, PALETTE_POPUP_MENU_APPLY_REPLACE_REPLACEMENT);
	Bind(wxEVT_MENU, &VirtualBrushGrid::OnMoveBrushMenu, this, PALETTE_POPUP_MENU_MOVE_BACKWARD, PALETTE_POPUP_MENU_RESET_ORDER);

	LoadStoredOrder();
	RebuildDisplayList();

	UpdateLayout();
	StartPreloading();
}

VirtualBrushGrid::~VirtualBrushGrid() {
	m_preloadTimer.Stop();
}

void VirtualBrushGrid::SetDisplayMode(DisplayMode mode) {
	if (display_mode != mode) {
		display_mode = mode;
		UpdateLayout();
		Refresh();
	}
}

// ============================================================================
// Display list: user order -> filter -> sort

size_t VirtualBrushGrid::GetEffectiveBrushCount() const {
	if (use_display_list) {
		return display_indices.size();
	}
	return tileset->size();
}

Brush* VirtualBrushGrid::GetEffectiveBrush(size_t index) const {
	if (use_display_list) {
		if (index >= display_indices.size()) {
			return nullptr;
		}
		const size_t real = display_indices[index];
		if (real < tileset->brushlist.size()) {
			return tileset->brushlist[real];
		}
		return nullptr;
	}
	if (index < tileset->size()) {
		return tileset->brushlist[index];
	}
	return nullptr;
}

void VirtualBrushGrid::LoadStoredOrder() {
	base_indices.clear();

	const std::vector<std::string>* order = g_tileset_order.GetOrder(tileset->tileset.name, tileset->getType());
	if (!order) {
		return;
	}

	const size_t count = tileset->brushlist.size();

	// A tileset may hold the same brush twice, so keep a queue per key.
	std::unordered_map<std::string, std::deque<size_t>> pending;
	for (size_t i = 0; i < count; ++i) {
		pending[TilesetOrderStore::MakeBrushKey(tileset->brushlist[i])].push_back(i);
	}

	std::vector<bool> used(count, false);
	base_indices.reserve(count);

	for (const std::string& key : *order) {
		auto it = pending.find(key);
		if (it == pending.end() || it->second.empty()) {
			continue; // The brush is no longer part of this tileset
		}
		const size_t index = it->second.front();
		it->second.pop_front();
		base_indices.push_back(index);
		used[index] = true;
	}

	// Brushes added after the order was saved keep their original place at the end.
	for (size_t i = 0; i < count; ++i) {
		if (!used[i]) {
			base_indices.push_back(i);
		}
	}
}

void VirtualBrushGrid::RebuildDisplayList() {
	const size_t count = tileset->brushlist.size();
	const bool has_base = !base_indices.empty();
	const bool has_sort = (sort_key != TilesetSortKey::None);

	filter_active = !current_filter.empty();

	if (!has_base && !has_sort && !filter_active) {
		display_indices.clear();
		use_display_list = false;
		return;
	}

	display_indices.clear();
	display_indices.reserve(count);

	if (has_base) {
		for (const size_t index : base_indices) {
			if (index < count) {
				display_indices.push_back(index);
			}
		}
	} else {
		for (size_t i = 0; i < count; ++i) {
			display_indices.push_back(i);
		}
	}

	if (filter_active) {
		std::erase_if(display_indices, [this](size_t index) {
			const Brush* brush = tileset->brushlist[index];
			return !brush || as_lower_str(brush->getName()).find(current_filter) == std::string::npos;
		});
	}

	if (has_sort) {
		const bool ascending = (sort_dir == TilesetSortDirection::Ascending);
		std::stable_sort(display_indices.begin(), display_indices.end(), [this, ascending](size_t lhs, size_t rhs) {
			const Brush* a = tileset->brushlist[lhs];
			const Brush* b = tileset->brushlist[rhs];
			if (!a || !b) {
				return a != nullptr;
			}

			if (sort_key == TilesetSortKey::ID) {
				const uint32_t idA = GetBrushSortID(a);
				const uint32_t idB = GetBrushSortID(b);
				if (idA != idB) {
					return ascending ? (idA < idB) : (idA > idB);
				}
			} else {
				const int cmp = wxStricmp(wxstr(a->getName()), wxstr(b->getName()));
				if (cmp != 0) {
					return ascending ? (cmp < 0) : (cmp > 0);
				}
			}
			return lhs < rhs;
		});
	}

	use_display_list = true;
}

void VirtualBrushGrid::SetFilter(const std::string& filter) {
	current_filter = as_lower_str(filter);
	RebuildDisplayList();
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
	RebuildDisplayList();
	selected_index = -1;
	hover_index = -1;
	if (GetEffectiveBrushCount() > 0) {
		selected_index = 0;
	}
	UpdateLayout();
	SetScrollPosition(0);
	Refresh();
}

// ============================================================================
// Display options and manual ordering

void VirtualBrushGrid::SetSort(TilesetSortKey key, TilesetSortDirection dir) {
	if (sort_key == key && sort_dir == dir) {
		return;
	}

	Brush* previous = GetSelectedBrush();

	sort_key = key;
	sort_dir = dir;
	RebuildDisplayList();

	// Keep the selection on the same brush across a reorder of the view.
	selected_index = -1;
	if (previous) {
		const size_t count = GetEffectiveBrushCount();
		for (size_t i = 0; i < count; ++i) {
			if (GetEffectiveBrush(i) == previous) {
				selected_index = static_cast<int>(i);
				break;
			}
		}
	}

	UpdateLayout();
	Refresh();
}

void VirtualBrushGrid::SetShowLabels(bool show) {
	if (show_labels == show) {
		return;
	}
	show_labels = show;
	UpdateLayout();
	Refresh();
}

void VirtualBrushGrid::SetReorderMode(bool enabled) {
	if (reorder_mode == enabled) {
		return;
	}
	reorder_mode = enabled;
	reorder_source = -1;
	reorder_target = -1;
	if (!enabled && HasCapture()) {
		ReleaseMouse();
	}
	Refresh();
}

void VirtualBrushGrid::ResetCustomOrder() {
	if (base_indices.empty()) {
		return;
	}

	Brush* previous = GetSelectedBrush();

	g_tileset_order.ClearOrder(tileset->tileset.name, tileset->getType());
	base_indices.clear();
	RebuildDisplayList();

	selected_index = -1;
	if (previous) {
		const size_t count = GetEffectiveBrushCount();
		for (size_t i = 0; i < count; ++i) {
			if (GetEffectiveBrush(i) == previous) {
				selected_index = static_cast<int>(i);
				break;
			}
		}
	}

	UpdateLayout();
	Refresh();
}

bool VirtualBrushGrid::CanReorder() const {
	// The view has to map 1:1 onto the stored order for a move to make sense.
	return !filter_active && sort_key == TilesetSortKey::None;
}

void VirtualBrushGrid::MoveBrush(int from, int insertBefore) {
	if (!CanReorder()) {
		return;
	}

	const int count = static_cast<int>(GetEffectiveBrushCount());
	if (count <= 1 || from < 0 || from >= count) {
		return;
	}

	insertBefore = std::clamp(insertBefore, 0, count);
	if (insertBefore == from || insertBefore == from + 1) {
		return; // Dropped back where it already was
	}

	// Materialize the current order, move one entry, and store it back.
	std::vector<size_t> order;
	if (use_display_list) {
		order = display_indices;
	} else {
		order.reserve(static_cast<size_t>(count));
		for (int i = 0; i < count; ++i) {
			order.push_back(static_cast<size_t>(i));
		}
	}

	const size_t moved = order[static_cast<size_t>(from)];
	order.erase(order.begin() + from);

	const int target = (insertBefore > from) ? insertBefore - 1 : insertBefore;
	order.insert(order.begin() + target, moved);

	base_indices = std::move(order);
	RebuildDisplayList();

	if (selected_index == from) {
		selected_index = target;
	} else if (selected_index >= 0) {
		// Shift the selection along with the items the moved brush passed over.
		if (from < selected_index && target >= selected_index) {
			--selected_index;
		} else if (from > selected_index && target <= selected_index) {
			++selected_index;
		}
	}

	PersistOrder();
	UpdateLayout();
	Refresh();
}

void VirtualBrushGrid::PersistOrder() {
	std::vector<Brush*> brushes;
	const size_t count = GetEffectiveBrushCount();
	brushes.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		Brush* brush = GetEffectiveBrush(i);
		if (brush) {
			brushes.push_back(brush);
		}
	}
	g_tileset_order.SetOrder(tileset->tileset.name, tileset->getType(), brushes);
}

// ============================================================================
// Layout

int VirtualBrushGrid::GetCellHeight() const {
	if (display_mode == DisplayMode::List) {
		return std::max(LIST_ROW_HEIGHT, item_size + 4);
	}
	return show_labels ? (item_size + LABEL_HEIGHT) : item_size;
}

int VirtualBrushGrid::GetRowStride() const {
	if (display_mode == DisplayMode::List) {
		return GetCellHeight();
	}
	return GetCellHeight() + padding;
}

void VirtualBrushGrid::UpdateLayout() {
	int width = GetClientSize().x;
	if (width <= 0) {
		width = 200; // Default
	}

	const int count = static_cast<int>(GetEffectiveBrushCount());

	if (display_mode == DisplayMode::List) {
		columns = 1;
		const int contentHeight = count * GetRowStride() + padding;
		UpdateScrollbar(contentHeight);
	} else {
		const int cellWidth = CellWidthFor(item_size, show_labels);
		columns = std::max(1, (width - padding) / (cellWidth + padding));
		const int rows = (count + columns - 1) / columns;
		const int contentHeight = rows * GetRowStride() + padding;
		UpdateScrollbar(contentHeight);
	}
}

wxSize VirtualBrushGrid::DoGetBestClientSize() const {
	return FromDIP(wxSize(200, 300));
}

wxRect VirtualBrushGrid::GetItemRect(int index) const {
	if (display_mode == DisplayMode::List) {
		const int rowHeight = GetRowStride();
		const int width = GetClientSize().x - 2 * padding;
		return wxRect(padding, padding + index * rowHeight, width, rowHeight);
	}

	const int row = index / columns;
	const int col = index % columns;
	const int cellWidth = CellWidthFor(item_size, show_labels);

	return wxRect(
		padding + col * (cellWidth + padding),
		padding + row * GetRowStride(),
		cellWidth,
		GetCellHeight()
	);
}

int VirtualBrushGrid::HitTest(int x, int y) const {
	const int scrollPos = GetScrollPosition();
	const int realY = y + scrollPos;
	const int realX = x;

	const int count = static_cast<int>(GetEffectiveBrushCount());

	if (display_mode == DisplayMode::List) {
		const int rowHeight = GetRowStride();
		const int row = (realY - padding) / rowHeight;

		if (row < 0 || row >= count) {
			return -1;
		}

		if (realX >= padding && realX <= GetClientSize().x - padding) {
			return row;
		}
		return -1;
	}

	const int cellWidth = CellWidthFor(item_size, show_labels);
	const int col = (realX - padding) / (cellWidth + padding);
	const int row = (realY - padding) / GetRowStride();

	if (col < 0 || col >= columns || row < 0) {
		return -1;
	}

	const int index = row * columns + col;
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

int VirtualBrushGrid::InsertHitTest(int x, int y) const {
	const int count = static_cast<int>(GetEffectiveBrushCount());
	if (count == 0) {
		return 0;
	}

	const int scrollPos = GetScrollPosition();
	const int realY = y + scrollPos;

	if (display_mode == DisplayMode::List) {
		const int rowHeight = GetRowStride();
		int row = (realY - padding) / rowHeight;
		row = std::clamp(row, 0, count - 1);
		const int rowTop = padding + row * rowHeight;
		return (realY - rowTop > rowHeight / 2) ? row + 1 : row;
	}

	const int cellWidth = CellWidthFor(item_size, show_labels);
	const int rowCount = (count + columns - 1) / columns;
	int row = (realY - padding) / GetRowStride();
	row = std::clamp(row, 0, std::max(0, rowCount - 1));

	int col = (x - padding) / (cellWidth + padding);
	col = std::clamp(col, 0, columns - 1);

	int index = row * columns + col;
	if (index >= count) {
		return count;
	}

	const int cellLeft = padding + col * (cellWidth + padding);
	if (x - cellLeft > cellWidth / 2) {
		++index;
	}
	return std::clamp(index, 0, count);
}

// ============================================================================
// Painting

void VirtualBrushGrid::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
	// Calculate visible range
	const int scrollPos = GetScrollPosition();
	const int rowHeight = GetRowStride();
	const int startRow = scrollPos / rowHeight;
	const int endRow = (scrollPos + height + rowHeight - 1) / rowHeight + 1;

	const int startIdx = startRow * columns;
	const int endIdx = std::min(static_cast<int>(GetEffectiveBrushCount()), endRow * columns);

	// Draw visible items
	for (int i = startIdx; i < endIdx; ++i) {
		DrawBrushItem(vg, i, GetItemRect(i));
	}

	DrawInsertMarker(vg);
}

const std::string& VirtualBrushGrid::GetUtf8Name(const Brush* brush) const {
	auto it = m_utf8NameCache.find(brush);
	if (it == m_utf8NameCache.end()) {
		it = m_utf8NameCache.emplace(brush, std::string(wxstr(brush->getName()).ToUTF8())).first;
	}
	return it->second;
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

	// The card currently being dragged to a new position
	if (i == reorder_source) {
		nvgBeginPath(vg);
		nvgRoundedRect(vg, x, y, w, h, 4.0f);
		nvgFillColor(vg, nvgRGBA(255, 255, 255, 40));
		nvgFill(vg);
	}

	// Draw brush sprite
	Brush* brush = GetEffectiveBrush(static_cast<size_t>(i));
	if (!brush) {
		return;
	}

	Sprite* spr = brush->getSprite();
	if (!spr) {
		spr = g_gui.gfx.getSprite(brush->getLookID());
	}

	const bool gridLabels = (display_mode == DisplayMode::Grid && show_labels);

	if (spr) {
		const int tex = GetOrCreateSpriteTexture(vg, spr);
		if (tex > 0) {
			const int iconSize = item_size - 2 * ICON_OFFSET;
			int iconX;
			int iconY;
			if (display_mode == DisplayMode::List) {
				iconX = rect.x + ICON_OFFSET;
				iconY = rect.y + (rect.height - iconSize) / 2;
			} else {
				iconX = rect.x + (rect.width - iconSize) / 2;
				const int iconArea = gridLabels ? item_size : rect.height;
				iconY = rect.y + (iconArea - iconSize) / 2;
			}

			NVGpaint imgPaint = nvgImagePattern(vg, static_cast<float>(iconX), static_cast<float>(iconY), static_cast<float>(iconSize), static_cast<float>(iconSize), 0.0f, tex, 1.0f);

			nvgBeginPath(vg);
			nvgRoundedRect(vg, static_cast<float>(iconX), static_cast<float>(iconY), static_cast<float>(iconSize), static_cast<float>(iconSize), 3.0f);
			nvgFillPaint(vg, imgPaint);
			nvgFill(vg);
		}
	}

	if (display_mode == DisplayMode::List) {
		nvgFontSize(vg, 14.0f);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::Text)));
		nvgText(vg, rect.x + item_size + 8.0f, rect.y + rect.height / 2.0f, GetUtf8Name(brush).c_str(), nullptr);
	} else if (gridLabels) {
		const float labelTop = static_cast<float>(rect.y + item_size);
		nvgSave(vg);
		nvgScissor(vg, static_cast<float>(rect.x), labelTop, static_cast<float>(rect.width), static_cast<float>(LABEL_HEIGHT));
		nvgFontSize(vg, LABEL_FONT_SIZE);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		nvgTextLineHeight(vg, 1.15f);
		nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::Text)));
		nvgTextBox(vg, static_cast<float>(rect.x + 2), labelTop + 1.0f, static_cast<float>(rect.width - 4), GetUtf8Name(brush).c_str(), nullptr);
		nvgRestore(vg);
	}
}

void VirtualBrushGrid::DrawInsertMarker(NVGcontext* vg) {
	if (reorder_target < 0) {
		return;
	}

	const int count = static_cast<int>(GetEffectiveBrushCount());
	if (count == 0) {
		return;
	}

	const int index = std::clamp(reorder_target, 0, count);
	const bool afterLast = (index >= count);
	const wxRect rect = GetItemRect(afterLast ? count - 1 : index);

	nvgBeginPath(vg);
	if (display_mode == DisplayMode::List) {
		const float markerY = afterLast ? static_cast<float>(rect.y + rect.height) : static_cast<float>(rect.y);
		nvgRect(vg, static_cast<float>(rect.x), markerY - 1.0f, static_cast<float>(rect.width), 2.0f);
	} else {
		const float markerX = afterLast ? static_cast<float>(rect.x + rect.width) : static_cast<float>(rect.x);
		nvgRect(vg, markerX - 1.0f, static_cast<float>(rect.y), 2.0f, static_cast<float>(rect.height));
	}
	nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::Accent)));
	nvgFill(vg);
}

// ============================================================================
// Input

void VirtualBrushGrid::OnMouseDown(wxMouseEvent& event) {
	const int index = HitTest(event.GetX(), event.GetY());
	if (index == -1) {
		return;
	}

	// Store drag start position
	m_dragStartPos = event.GetPosition();
	m_isDragging = false;

	if (reorder_mode && CanReorder()) {
		reorder_source = index;
		reorder_target = -1;
		if (!HasCapture()) {
			CaptureMouse();
		}
	}

	if (index != selected_index) {
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

			// Auto-assign to replace tool slots if enabled
			MapTab* tab = g_gui.GetCurrentMapTab();
			if (tab) {
				ReplaceToolWindow* rtw = tab->GetReplaceToolWindow();
				if (rtw && rtw->IsAutoAssignEnabled()) {
					uint16_t serverId = 0;
					if (brush->is<RAWBrush>()) {
						serverId = brush->as<RAWBrush>()->getItemID();
					} else {
						int lookId = brush->getLookID();
						if (lookId > 0) {
							serverId = static_cast<uint16_t>(lookId);
						}
					}
					if (serverId > 0) {
						rtw->AutoAssignItem(serverId);
					}
				}
			}
		}
		Refresh();
	}
}

void VirtualBrushGrid::OnMouseUp(wxMouseEvent& event) {
	if (HasCapture()) {
		ReleaseMouse();
	}

	if (reorder_source >= 0) {
		const int from = reorder_source;
		const int target = reorder_target;
		reorder_source = -1;
		reorder_target = -1;
		if (target >= 0) {
			MoveBrush(from, target);
		}
		Refresh();
	}

	event.Skip();
}

void VirtualBrushGrid::OnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event)) {
	reorder_source = -1;
	reorder_target = -1;
	Refresh();
}

void VirtualBrushGrid::OnRightClick(wxMouseEvent& event) {
	// A popup menu must never run while the mouse is captured by a drag.
	if (HasCapture()) {
		ReleaseMouse();
	}
	reorder_source = -1;
	reorder_target = -1;

	const int index = HitTest(event.GetX(), event.GetY());
	if (index == -1) {
		return;
	}

	Brush* brush = GetEffectiveBrush(static_cast<size_t>(index));
	if (!brush) {
		return;
	}

	// Select the item under right-click
	if (index != selected_index) {
		selected_index = index;
		g_gui.SelectBrush(brush, tileset->getType());
		Refresh();
	}

	wxMenu menu;

	// Get the item server ID for this brush
	uint16_t serverId = 0;
	if (brush->is<RAWBrush>()) {
		RAWBrush* raw = brush->as<RAWBrush>();
		if (raw) {
			serverId = raw->getItemID();
		}
	} else {
		int lookId = brush->getLookID();
		if (lookId > 0) {
			serverId = static_cast<uint16_t>(lookId);
		}
	}

	if (serverId > 0) {
		auto def = g_item_definitions.get(serverId);
		int clientId = def ? def.clientId() : 0;

		menu.Append(PALETTE_POPUP_MENU_COPY_SERVER_ID, wxString::Format("Copy Server ID (%d)", serverId));
		menu.Append(PALETTE_POPUP_MENU_COPY_CLIENT_ID, wxString::Format("Copy Client ID (%d)", clientId));
		menu.AppendSeparator();
		menu.Append(PALETTE_POPUP_MENU_APPLY_REPLACE_ORIGINAL, "Apply to Replace Box 1 (Original)");
		menu.Append(PALETTE_POPUP_MENU_APPLY_REPLACE_REPLACEMENT, "Apply to Replace Box 2 (Replacement)");
	}

	// Manual ordering, only while the view matches the stored order
	const int count = static_cast<int>(GetEffectiveBrushCount());
	if (CanReorder() && count > 1) {
		if (menu.GetMenuItemCount() > 0) {
			menu.AppendSeparator();
		}
		menu.Append(PALETTE_POPUP_MENU_MOVE_BACKWARD, "Move Backward")->Enable(index > 0);
		menu.Append(PALETTE_POPUP_MENU_MOVE_FORWARD, "Move Forward")->Enable(index < count - 1);
		menu.Append(PALETTE_POPUP_MENU_MOVE_TO_START, "Move to Start")->Enable(index > 0);
		menu.Append(PALETTE_POPUP_MENU_MOVE_TO_END, "Move to End")->Enable(index < count - 1);
		menu.Append(PALETTE_POPUP_MENU_RESET_ORDER, "Reset Tileset Order")->Enable(!base_indices.empty());
	}

	if (menu.GetMenuItemCount() > 0) {
		PopupMenu(&menu, event.GetPosition());
	}
}

void VirtualBrushGrid::OnMoveBrushMenu(wxCommandEvent& event) {
	if (event.GetId() == PALETTE_POPUP_MENU_RESET_ORDER) {
		ResetCustomOrder();
		return;
	}

	if (!CanReorder() || selected_index < 0) {
		return;
	}

	const int count = static_cast<int>(GetEffectiveBrushCount());
	switch (event.GetId()) {
		case PALETTE_POPUP_MENU_MOVE_BACKWARD:
			MoveBrush(selected_index, selected_index - 1);
			break;
		case PALETTE_POPUP_MENU_MOVE_FORWARD:
			MoveBrush(selected_index, selected_index + 2);
			break;
		case PALETTE_POPUP_MENU_MOVE_TO_START:
			MoveBrush(selected_index, 0);
			break;
		case PALETTE_POPUP_MENU_MOVE_TO_END:
			MoveBrush(selected_index, count);
			break;
		default:
			break;
	}
}

void VirtualBrushGrid::OnCopyServerID(wxCommandEvent& WXUNUSED(event)) {
	Brush* brush = GetSelectedBrush();
	if (!brush) {
		return;
	}

	int serverId = 0;
	if (brush->is<RAWBrush>()) {
		serverId = brush->as<RAWBrush>()->getItemID();
	} else {
		serverId = brush->getLookID();
	}

	if (serverId > 0 && wxTheClipboard->Open()) {
		wxTheClipboard->SetData(new wxTextDataObject(std::to_string(serverId)));
		wxTheClipboard->Close();
	}
}

void VirtualBrushGrid::OnCopyClientID(wxCommandEvent& WXUNUSED(event)) {
	Brush* brush = GetSelectedBrush();
	if (!brush) {
		return;
	}

	uint16_t serverId = 0;
	if (brush->is<RAWBrush>()) {
		serverId = brush->as<RAWBrush>()->getItemID();
	} else {
		serverId = static_cast<uint16_t>(brush->getLookID());
	}

	if (serverId > 0) {
		auto def = g_item_definitions.get(serverId);
		if (def && wxTheClipboard->Open()) {
			wxTheClipboard->SetData(new wxTextDataObject(std::to_string(def.clientId())));
			wxTheClipboard->Close();
		}
	}
}

void VirtualBrushGrid::OnApplyReplaceOriginal(wxCommandEvent& WXUNUSED(event)) {
	Brush* brush = GetSelectedBrush();
	if (!brush) {
		return;
	}

	uint16_t serverId = 0;
	if (brush->is<RAWBrush>()) {
		serverId = brush->as<RAWBrush>()->getItemID();
	} else {
		serverId = static_cast<uint16_t>(brush->getLookID());
	}

	if (serverId > 0) {
		MapTab* tab = g_gui.GetCurrentMapTab();
		if (tab) {
			tab->ApplyItemToReplaceOriginal(serverId);
		}
	}
}

void VirtualBrushGrid::OnApplyReplaceReplacement(wxCommandEvent& WXUNUSED(event)) {
	Brush* brush = GetSelectedBrush();
	if (!brush) {
		return;
	}

	uint16_t serverId = 0;
	if (brush->is<RAWBrush>()) {
		serverId = brush->as<RAWBrush>()->getItemID();
	} else {
		serverId = static_cast<uint16_t>(brush->getLookID());
	}

	if (serverId > 0) {
		MapTab* tab = g_gui.GetCurrentMapTab();
		if (tab) {
			tab->ApplyItemToReplaceReplacement(serverId);
		}
	}
}

void VirtualBrushGrid::OnMotion(wxMouseEvent& event) {
	// Reordering takes over the drag gesture inside the grid
	if (reorder_mode && reorder_source >= 0 && event.LeftIsDown()) {
		const int target = InsertHitTest(event.GetX(), event.GetY());
		if (target != reorder_target) {
			reorder_target = target;
			Refresh();
		}
		return;
	}

	// Drag & Drop initiation
	if (event.Dragging() && event.LeftIsDown() && selected_index >= 0) {
		if (!m_isDragging) {
			wxPoint diff = event.GetPosition() - m_dragStartPos;
			if (std::abs(diff.x) > 3 || std::abs(diff.y) > 3) {
				m_isDragging = true;

				Brush* brush = GetEffectiveBrush(static_cast<size_t>(selected_index));
				if (brush) {
					uint16_t serverId = 0;
					if (brush->is<RAWBrush>()) {
						serverId = brush->as<RAWBrush>()->getItemID();
					} else {
						int lookId = brush->getLookID();
						if (lookId > 0) {
							serverId = static_cast<uint16_t>(lookId);
						}
					}

					if (serverId > 0) {
						wxTextDataObject data(wxString::Format("RME_ITEM:%u", serverId));
						wxDropSource dragSource(this);
						dragSource.SetData(data);
						dragSource.DoDragDrop(wxDrag_AllowMove);
					}
				}
			}
		}
		return;
	}

	m_isDragging = false;
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

bool VirtualBrushGrid::SelectBrushByOffset(int offset) {
	int count = static_cast<int>(GetEffectiveBrushCount());
	if (count == 0) {
		return false;
	}

	int new_index = selected_index + offset;
	if (new_index < 0 || new_index >= count) {
		return false;
	}

	selected_index = new_index;

	// Ensure visible (same logic as SelectBrush)
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

// ============================================================================
// Progressive texture preloading

void VirtualBrushGrid::StartPreloading() {
	m_preloadIndex = 0;
	m_preloadComplete = false;
	m_preloadTimer.Start(PRELOAD_TIMER_INTERVAL);
}

void VirtualBrushGrid::OnSwitchIn() {
	// Resume preloading the remaining textures when this page becomes visible again.
	if (!m_preloadComplete && !m_preloadTimer.IsRunning()) {
		m_preloadTimer.Start(PRELOAD_TIMER_INTERVAL);
	}
}

void VirtualBrushGrid::OnSwitchOut() {
	// Stop preloading while hidden so background grids don't compete for the UI thread.
	m_preloadTimer.Stop();
}

void VirtualBrushGrid::OnPreloadTimer(wxTimerEvent&) {
	if (m_preloadComplete) {
		m_preloadTimer.Stop();
		return;
	}

	NVGcontext* vg = GetNVGContext();
	if (!vg) {
		// GL not initialized yet, try again next tick
		return;
	}

	// Ensure GL context is current for texture creation
	if (!MakeContextCurrent()) {
		return;
	}

	size_t count = GetEffectiveBrushCount();
	size_t loaded = 0;

	while (m_preloadIndex < count && loaded < PRELOAD_BATCH_SIZE) {
		Brush* brush = GetEffectiveBrush(m_preloadIndex);
		if (brush) {
			Sprite* spr = brush->getSprite();
			if (!spr) {
				spr = g_gui.gfx.getSprite(brush->getLookID());
			}
			if (spr) {
				// This will create and cache the texture if not already cached
				GetOrCreateSpriteTexture(vg, spr);
				++loaded;
			}
		}
		++m_preloadIndex;
	}

	if (m_preloadIndex >= count) {
		m_preloadComplete = true;
		m_preloadTimer.Stop();
	}

	// Refresh to show newly loaded textures
	if (loaded > 0) {
		Refresh();
	}
}
