#include "ui/find_item_window_views.h"

#include "brushes/creature/creature_brush.h"
#include "ui/gui.h"
#include "ui/theme.h"

#include <glad/glad.h>
#include <nanovg.h>

#include <algorithm>
#include <limits>
#include <string_view>

wxDEFINE_EVENT(EVT_ADVANCED_FINDER_RESULT_RIGHT_ACTIVATE, wxCommandEvent);

namespace {
	NVGcolor toColor(const wxColour& colour) {
		return nvgRGBA(colour.Red(), colour.Green(), colour.Blue(), colour.Alpha());
	}

	[[nodiscard]] std::string hoverSummary(const AdvancedFinderCatalogRow& row) {
		if (row.isCreature()) {
			return "SID: -   CID: -   " + row.label;
		}
		return "SID: " + std::to_string(row.server_id) + "   CID: " + std::to_string(row.client_id) + "   " + row.label;
	}
}

AdvancedFinderResultsView::AdvancedFinderResultsView(wxWindow* parent, wxWindowID id) :
	NanoVGCanvas(parent, id, wxVSCROLL | wxWANTS_CHARS) {
	SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	bindEvents();
}

void AdvancedFinderResultsView::bindEvents() {
	Bind(wxEVT_SIZE, &AdvancedFinderResultsView::onSize, this);
	Bind(wxEVT_LEFT_DOWN, &AdvancedFinderResultsView::onMouseDown, this);
	Bind(wxEVT_MOTION, &AdvancedFinderResultsView::onMouseMove, this);
	Bind(wxEVT_LEAVE_WINDOW, &AdvancedFinderResultsView::onMouseLeave, this);
	Bind(wxEVT_MOUSEWHEEL, &AdvancedFinderResultsView::onMouseWheel, this);
	Bind(wxEVT_KEY_DOWN, &AdvancedFinderResultsView::onKeyDown, this);
	Bind(wxEVT_LEFT_DCLICK, &AdvancedFinderResultsView::onLeftDoubleClick, this);
	Bind(wxEVT_RIGHT_DCLICK, &AdvancedFinderResultsView::onRightDoubleClick, this);
	Bind(wxEVT_CONTEXT_MENU, &AdvancedFinderResultsView::onContextMenu, this);
}

void AdvancedFinderResultsView::SetMode(AdvancedFinderResultViewMode mode) {
	if (mode_ == mode) {
		return;
	}

	mode_ = mode;
	SetScrollStep(mode_ == AdvancedFinderResultViewMode::Grid ? std::max(1, gridCardHeight() + gridGap()) : std::max(1, listItemHeight()));
	updateLayoutMetrics(cached_width_ > 0 ? cached_width_ : GetClientSize().x);
	EnsureSelectionVisible();
	refreshNow();
}

void AdvancedFinderResultsView::SetRows(std::vector<const AdvancedFinderCatalogRow*> rows, const AdvancedFinderSelectionKey& preferred_selection) {
	rows_ = std::move(rows);
	empty_state_ = rows_.empty() ? EmptyState::Prompt : EmptyState::Rows;
	primary_message_.clear();
	secondary_message_.clear();

	if (rows_.empty()) {
		selected_index_ = -1;
		updateLayoutMetrics(cached_width_ > 0 ? cached_width_ : GetClientSize().x);
		refreshNow();
		return;
	}

	selectDefaultRow(preferred_selection);
	updateLayoutMetrics(cached_width_ > 0 ? cached_width_ : GetClientSize().x);
	EnsureSelectionVisible();
	refreshNow();
}

void AdvancedFinderResultsView::SetPrompt(std::string primary, std::string secondary) {
	empty_state_ = EmptyState::Prompt;
	primary_message_ = std::move(primary);
	secondary_message_ = std::move(secondary);
	rows_.clear();
	selected_index_ = -1;
	updateLayoutMetrics(cached_width_ > 0 ? cached_width_ : GetClientSize().x);
	refreshNow();
}

void AdvancedFinderResultsView::SetNoMatches(std::string primary, std::string secondary) {
	empty_state_ = EmptyState::NoMatches;
	primary_message_ = std::move(primary);
	secondary_message_ = std::move(secondary);
	rows_.clear();
	selected_index_ = -1;
	updateLayoutMetrics(cached_width_ > 0 ? cached_width_ : GetClientSize().x);
	refreshNow();
}

const AdvancedFinderCatalogRow* AdvancedFinderResultsView::GetSelectedRow() const {
	if (empty_state_ != EmptyState::Rows || selected_index_ < 0 || selected_index_ >= static_cast<int>(rows_.size())) {
		return nullptr;
	}

	return rows_[selected_index_];
}

int AdvancedFinderResultsView::GetSelectionIndex() const {
	return selected_index_;
}

void AdvancedFinderResultsView::SetSelectionIndex(int index) {
	setSelectionInternal(index, false);
	EnsureSelectionVisible();
	refreshNow();
}

void AdvancedFinderResultsView::EnsureSelectionVisible() {
	if (selected_index_ < 0 || selected_index_ >= static_cast<int>(rows_.size())) {
		return;
	}

	const wxRect rect = itemRect(static_cast<size_t>(selected_index_));
	const int scroll_pos = GetScrollPosition();
	const int view_height = GetClientSize().y;
	const int top = rect.y;
	const int bottom = rect.y + rect.height;

	if (top < scroll_pos) {
		SetScrollPosition(top);
	} else if (bottom > scroll_pos + view_height) {
		SetScrollPosition(bottom - view_height);
	}
}

void AdvancedFinderResultsView::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
	updateLayoutMetrics(width);

	if (empty_state_ != EmptyState::Rows || rows_.empty()) {
		drawEmptyState(vg, width, height);
		return;
	}

	const int scroll_pos = GetScrollPosition();
	const wxPoint live_mouse = ScreenToClient(wxGetMousePosition());
	const bool mouse_inside = GetClientRect().Contains(live_mouse);
	const int live_hover_index = mouse_inside ? hitTest(live_mouse.x, live_mouse.y + scroll_pos) : -1;
	hover_position_ = mouse_inside ? live_mouse : wxDefaultPosition;
	hover_index_ = live_hover_index;

	if (mode_ == AdvancedFinderResultViewMode::List) {
		const int header_height = std::max(1, listHeaderHeight());
		const wxColour header_fill = Theme::Get(Theme::Role::RaisedSurface);
		const wxColour border = Theme::Get(Theme::Role::CardBorder);
		const wxColour text = Theme::Get(Theme::Role::TextSubtle);
		const float column_image = static_cast<float>(FromDIP(56));
		const float column_sid = static_cast<float>(FromDIP(110));
		const float column_cid = static_cast<float>(FromDIP(110));
		const float y_center = header_height * 0.5f;

		nvgBeginPath(vg);
		nvgRect(vg, 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(header_height));
		nvgFillColor(vg, toColor(header_fill));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgMoveTo(vg, 0.0f, static_cast<float>(header_height));
		nvgLineTo(vg, static_cast<float>(width), static_cast<float>(header_height));
		nvgStrokeWidth(vg, 1.0f);
		nvgStrokeColor(vg, toColor(border));
		nvgStroke(vg);

		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgFontSize(vg, 12.0f);
		nvgFillColor(vg, toColor(text));
		nvgText(vg, 14.0f, y_center, "Image", nullptr);
		nvgText(vg, column_image + 12.0f, y_center, "SID", nullptr);
		nvgText(vg, column_image + column_sid + 12.0f, y_center, "CID", nullptr);
		nvgText(vg, column_image + column_sid + column_cid + 12.0f, y_center, "Name", nullptr);

		const int item_height = std::max(1, listItemHeight());
		const int content_scroll = std::max(0, scroll_pos - header_height);
		int start_index = std::max(0, content_scroll / item_height);
		int end_index = std::min(static_cast<int>(rows_.size()), (content_scroll + height + item_height - 1) / item_height);

		for (int index = start_index; index < end_index; ++index) {
			const wxRect rect = itemRect(static_cast<size_t>(index));
			drawListRow(vg, rect, *rows_[index], index == selected_index_, index == live_hover_index);
		}
		return;
	}

	const int row_height = std::max(1, gridCardHeight() + gridGap());
	int first_row = std::max(0, (scroll_pos - padding_) / row_height);
	int last_row = std::min((static_cast<int>(rows_.size()) + columns_ - 1) / columns_, (scroll_pos + height - padding_ + row_height - 1) / row_height + 1);

	for (int row = first_row; row < last_row; ++row) {
		for (int column = 0; column < columns_; ++column) {
			const int index = row * columns_ + column;
			if (index < 0 || index >= static_cast<int>(rows_.size())) {
				continue;
			}

			const wxRect rect = itemRect(static_cast<size_t>(index));
			if (rect.y > scroll_pos + height || rect.y + rect.height < scroll_pos) {
				continue;
			}

			drawGridCard(vg, rect, *rows_[index], index == selected_index_, index == live_hover_index);
		}
	}

	if (mouse_inside && live_hover_index >= 0 && live_hover_index < static_cast<int>(rows_.size())) {
		drawGridHoverInfo(vg, width, height, scroll_pos);
	}
}

void AdvancedFinderResultsView::onSize(wxSizeEvent& event) {
	updateLayoutMetrics(GetClientSize().x);
	EnsureSelectionVisible();
	event.Skip();
}

void AdvancedFinderResultsView::onMouseDown(wxMouseEvent& event) {
	const int index = hitTest(event.GetX(), event.GetY() + GetScrollPosition());
	if (index >= 0) {
		setSelectionInternal(index, true);
		EnsureSelectionVisible();
	}
	SetFocus();
}

void AdvancedFinderResultsView::onMouseMove(wxMouseEvent& event) {
	hover_position_ = wxPoint(event.GetX(), event.GetY());
	const int index = hitTest(event.GetX(), event.GetY() + GetScrollPosition());
	if (index != hover_index_ || (mode_ == AdvancedFinderResultViewMode::Grid && hover_index_ >= 0)) {
		hover_index_ = index;
		Refresh();
	}
	event.Skip();
}

void AdvancedFinderResultsView::onMouseLeave(wxMouseEvent& WXUNUSED(event)) {
	hover_position_ = wxDefaultPosition;
	if (hover_index_ != -1) {
		hover_index_ = -1;
		Refresh();
	}
}

void AdvancedFinderResultsView::onMouseWheel(wxMouseEvent& event) {
	event.Skip();
	CallAfter([this] {
		const wxPoint point = ScreenToClient(wxGetMousePosition());
		if (!GetClientRect().Contains(point)) {
			hover_position_ = wxDefaultPosition;
			hover_index_ = -1;
			Refresh();
			return;
		}

		hover_position_ = point;
		hover_index_ = hitTest(point.x, point.y + GetScrollPosition());
		Refresh();
	});
}

void AdvancedFinderResultsView::onKeyDown(wxKeyEvent& event) {
	if (rows_.empty() || empty_state_ != EmptyState::Rows) {
		event.Skip();
		return;
	}

	int next = selected_index_;
	const int columns = std::max(1, columns_);
	const int page_step = std::max(1, visibleRows()) * (mode_ == AdvancedFinderResultViewMode::Grid ? columns : 1);

	switch (event.GetKeyCode()) {
		case WXK_UP:
			next = (mode_ == AdvancedFinderResultViewMode::Grid) ? selected_index_ - columns : selected_index_ - 1;
			break;
		case WXK_DOWN:
			next = (mode_ == AdvancedFinderResultViewMode::Grid) ? selected_index_ + columns : selected_index_ + 1;
			break;
		case WXK_LEFT:
			if (mode_ == AdvancedFinderResultViewMode::Grid) {
				next = selected_index_ - 1;
			}
			break;
		case WXK_RIGHT:
			if (mode_ == AdvancedFinderResultViewMode::Grid) {
				next = selected_index_ + 1;
			}
			break;
		case WXK_HOME:
			next = 0;
			break;
		case WXK_END:
			next = static_cast<int>(rows_.size()) - 1;
			break;
		case WXK_PAGEUP:
			next = selected_index_ - page_step;
			break;
		case WXK_PAGEDOWN:
			next = selected_index_ + page_step;
			break;
		case WXK_RETURN:
		case WXK_NUMPAD_ENTER:
			sendActivateEvent();
			return;
		default:
			event.Skip();
			return;
	}

	next = std::clamp(next, 0, static_cast<int>(rows_.size()) - 1);
	if (next != selected_index_) {
		setSelectionInternal(next, true);
		EnsureSelectionVisible();
		return;
	}

	event.Skip();
}

void AdvancedFinderResultsView::onLeftDoubleClick(wxMouseEvent& event) {
	const int index = hitTest(event.GetX(), event.GetY() + GetScrollPosition());
	if (index >= 0) {
		setSelectionInternal(index, true);
		sendActivateEvent();
	}
	SetFocus();
}

void AdvancedFinderResultsView::onRightDoubleClick(wxMouseEvent& event) {
	const int index = hitTest(event.GetX(), event.GetY() + GetScrollPosition());
	if (index >= 0) {
		setSelectionInternal(index, true);
		wxCommandEvent activate_event(EVT_ADVANCED_FINDER_RESULT_RIGHT_ACTIVATE, GetId());
		activate_event.SetEventObject(this);
		activate_event.SetInt(selected_index_);
		ProcessWindowEvent(activate_event);
	}
	event.StopPropagation();
	SetFocus();
}

void AdvancedFinderResultsView::onContextMenu(wxContextMenuEvent& event) {
	event.StopPropagation();
}

void AdvancedFinderResultsView::updateLayoutMetrics(int width) {
	cached_width_ = width;
	cached_height_ = GetClientSize().y;
	list_item_height_ = FromDIP(48);
	padding_ = FromDIP(8);
	gap_ = FromDIP(4);
	card_width_ = FromDIP(40);
	card_height_ = FromDIP(40);

	if (mode_ == AdvancedFinderResultViewMode::Grid) {
		columns_ = columnsForWidth(width);
	} else {
		columns_ = 1;
	}

	updateScrollbarForLayout();
}

void AdvancedFinderResultsView::updateScrollbarForLayout() {
	if (empty_state_ != EmptyState::Rows || rows_.empty()) {
		UpdateScrollbar(0);
		return;
	}

	int content_height = 0;
	if (mode_ == AdvancedFinderResultViewMode::List) {
		content_height = std::max(1, listHeaderHeight()) + static_cast<int>(rows_.size()) * std::max(1, list_item_height_);
	} else {
		const int row_count = (static_cast<int>(rows_.size()) + columns_ - 1) / columns_;
		content_height = padding_ * 2 + row_count * std::max(1, card_height_);
		if (row_count > 1) {
			content_height += (row_count - 1) * gap_;
		}
	}

	UpdateScrollbar(content_height);
	SetScrollStep(mode_ == AdvancedFinderResultViewMode::Grid ? std::max(1, card_height_ + gap_) : std::max(1, list_item_height_));
}

void AdvancedFinderResultsView::setSelectionInternal(int index, bool notify) {
	if (empty_state_ != EmptyState::Rows || rows_.empty()) {
		selected_index_ = -1;
		return;
	}

	if (index < 0 || index >= static_cast<int>(rows_.size())) {
		index = -1;
	}

	if (selected_index_ == index) {
		if (notify) {
			sendSelectionEvent();
		}
		return;
	}

	selected_index_ = index;
	Refresh();

	if (notify) {
		sendSelectionEvent();
	}
}

void AdvancedFinderResultsView::selectDefaultRow(const AdvancedFinderSelectionKey& preferred_selection) {
	selected_index_ = -1;
	for (size_t index = 0; index < rows_.size(); ++index) {
		if (AdvancedFinderSelectionMatches(*rows_[index], preferred_selection)) {
			selected_index_ = static_cast<int>(index);
			break;
		}
	}

	if (selected_index_ < 0 && !rows_.empty()) {
		selected_index_ = 0;
	}
}

void AdvancedFinderResultsView::refreshNow() {
	Refresh();
}

void AdvancedFinderResultsView::sendSelectionEvent() {
	wxCommandEvent event(wxEVT_LISTBOX, GetId());
	event.SetEventObject(this);
	event.SetInt(selected_index_);
	ProcessWindowEvent(event);
}

void AdvancedFinderResultsView::sendActivateEvent() {
	wxCommandEvent event(wxEVT_LISTBOX_DCLICK, GetId());
	event.SetEventObject(this);
	event.SetInt(selected_index_);
	ProcessWindowEvent(event);
}

int AdvancedFinderResultsView::hitTest(int x, int y) const {
	if (empty_state_ != EmptyState::Rows || rows_.empty()) {
		return -1;
	}

	if (mode_ == AdvancedFinderResultViewMode::List) {
		const int header_height = std::max(1, listHeaderHeight());
		if (y < header_height) {
			return -1;
		}
		const int item_height = std::max(1, list_item_height_);
		const int index = (y - header_height) / item_height;
		if (index >= 0 && index < static_cast<int>(rows_.size())) {
			return index;
		}
		return -1;
	}

	const int row_height = std::max(1, card_height_ + gap_);
	const int row = (y - padding_) / row_height;
	const int row_offset = (y - padding_) % row_height;
	if (row < 0 || row_offset < 0 || row_offset > card_height_) {
		return -1;
	}

	const int column_width = card_width_ + gap_;
	const int column = (x - padding_) / column_width;
	const int column_offset = (x - padding_) % column_width;
	if (column < 0 || column_offset < 0 || column_offset > card_width_) {
		return -1;
	}

	const int index = row * columns_ + column;
	if (index >= 0 && index < static_cast<int>(rows_.size())) {
		return index;
	}
	return -1;
}

wxRect AdvancedFinderResultsView::itemRect(size_t index) const {
	if (mode_ == AdvancedFinderResultViewMode::List) {
		const int item_height = std::max(1, list_item_height_);
		return wxRect(0, std::max(1, listHeaderHeight()) + static_cast<int>(index) * item_height, GetClientSize().x, item_height);
	}

	const int row = static_cast<int>(index) / std::max(1, columns_);
	const int column = static_cast<int>(index) % std::max(1, columns_);
	const int row_height = std::max(1, card_height_ + gap_);
	const int x = padding_ + column * (card_width_ + gap_);
	const int y = padding_ + row * row_height;
	return wxRect(x, y, card_width_, card_height_);
}

Sprite* AdvancedFinderResultsView::spriteForRow(const AdvancedFinderCatalogRow& row) const {
	if (row.isCreature() && row.creature_brush != nullptr) {
		return row.creature_brush->getSprite();
	}

	if (row.client_id != 0) {
		return g_gui.gfx.getSprite(row.client_id);
	}

	return nullptr;
}

int AdvancedFinderResultsView::columnsForWidth(int width) const {
	const int usable_width = std::max(1, width - padding_ * 2);
	const int card_space = card_width_ + gap_;
	return std::max(1, (usable_width + gap_) / std::max(1, card_space));
}

int AdvancedFinderResultsView::listHeaderHeight() const {
	return FromDIP(28);
}

int AdvancedFinderResultsView::listItemHeight() const {
	return FromDIP(48);
}

int AdvancedFinderResultsView::gridCardWidth() const {
	return FromDIP(40);
}

int AdvancedFinderResultsView::gridCardHeight() const {
	return FromDIP(40);
}

int AdvancedFinderResultsView::gridPadding() const {
	return FromDIP(8);
}

int AdvancedFinderResultsView::gridGap() const {
	return FromDIP(4);
}

int AdvancedFinderResultsView::visibleRows() const {
	const int row_height = (mode_ == AdvancedFinderResultViewMode::Grid) ? std::max(1, card_height_ + gap_) : std::max(1, list_item_height_);
	return std::max(1, GetClientSize().y / row_height);
}

void AdvancedFinderResultsView::drawEmptyState(NVGcontext* vg, int width, int height) const {
	const wxColour surface = Theme::Get(Theme::Role::Background);
	const wxColour title = Theme::Get(Theme::Role::TextSubtle);
	const wxColour body = Theme::Get(Theme::Role::TextSubtle);

	nvgBeginPath(vg);
	nvgRoundedRect(vg, 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);
	nvgFillColor(vg, toColor(surface));
	nvgFill(vg);

	nvgFontFace(vg, "sans");
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

	const char* primary = primary_message_.empty() ? "No matching items" : primary_message_.c_str();
	const char* secondary = secondary_message_.empty() ? "Try a different query or filters." : secondary_message_.c_str();

	nvgFontSize(vg, 15.0f);
	nvgFillColor(vg, toColor(title));
	nvgText(vg, 18.0f, height * 0.42f, primary, nullptr);

	nvgFontSize(vg, 12.0f);
	nvgFillColor(vg, toColor(body));
	nvgText(vg, 18.0f, height * 0.52f, secondary, nullptr);
}

void AdvancedFinderResultsView::drawSpriteBadge(NVGcontext* vg, const wxRect& rect, Sprite* sprite) const {
	if (sprite == nullptr) {
		return;
	}

	if (const int texture = const_cast<AdvancedFinderResultsView*>(this)->GetOrCreateSpriteTexture(vg, sprite); texture > 0) {
		const float radius = 8.0f;
		NVGpaint paint = nvgImagePattern(vg, rect.x, rect.y, rect.width, rect.height, 0.0f, texture, 1.0f);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, rect.x, rect.y, rect.width, rect.height, radius);
		nvgFillPaint(vg, paint);
		nvgFill(vg);
	}
}

void AdvancedFinderResultsView::drawListRow(NVGcontext* vg, const wxRect& rect, const AdvancedFinderCatalogRow& row, bool selected, bool hovered) const {
	const wxColour selected_fill = Theme::Get(Theme::Role::Accent);
	const wxColour hover_fill = Theme::Get(Theme::Role::CardBaseHover);
	const wxColour row_fill = Theme::Get(Theme::Role::CardBase);
	const wxColour border = Theme::Get(Theme::Role::CardBorder);
	const wxColour icon_fill = Theme::Get(Theme::Role::RaisedSurface);
	const wxColour icon_border = selected ? Theme::Get(Theme::Role::AccentHover) : border;
	const wxColour text = selected ? Theme::Get(Theme::Role::TextOnAccent) : Theme::Get(Theme::Role::Text);
	const wxColour fill_colour = selected ? selected_fill : (hovered ? hover_fill : row_fill);
	const float column_image = static_cast<float>(FromDIP(56));
	const float column_sid = static_cast<float>(FromDIP(110));
	const float column_cid = static_cast<float>(FromDIP(110));

	nvgBeginPath(vg);
	nvgRect(vg, rect.x, rect.y, rect.width, rect.height);
	nvgFillColor(vg, toColor(fill_colour));
	nvgFill(vg);
	nvgStrokeWidth(vg, 1.0f);
	nvgStrokeColor(vg, toColor(border));
	nvgStroke(vg);

	const int icon_well = FromDIP(36);
	const int icon_size = FromDIP(32);
	const wxRect icon_background(rect.x + 8, rect.y + 6, FromDIP(40), rect.height - 12);
	nvgBeginPath(vg);
	nvgRoundedRect(vg, icon_background.x, icon_background.y, icon_background.width, icon_background.height, 6.0f);
	nvgFillColor(vg, toColor(icon_fill));
	nvgFill(vg);
	nvgStrokeWidth(vg, 1.0f);
	nvgStrokeColor(vg, toColor(icon_border));
	nvgStroke(vg);

	const wxRect icon_rect(
		icon_background.x + (icon_background.width - icon_well) / 2,
		rect.y + (rect.height - icon_well) / 2,
		icon_well,
		icon_well
	);
	const wxRect sprite_rect(icon_rect.x + (icon_rect.width - icon_size) / 2, icon_rect.y + (icon_rect.height - icon_size) / 2, icon_size, icon_size);
	drawSpriteBadge(vg, sprite_rect, spriteForRow(row));

	nvgFontFace(vg, "sans");
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	nvgFontSize(vg, 13.0f);
	nvgFillColor(vg, toColor(text));

	const float text_y = static_cast<float>(rect.y + rect.height * 0.5f);
	nvgSave(vg);
	nvgScissor(vg, column_image + 6.0f, rect.y, rect.width - column_image - 12.0f, rect.height);
	nvgText(vg, column_image + 12.0f, text_y, std::to_string(row.server_id).c_str(), nullptr);
	nvgText(vg, column_image + column_sid + 12.0f, text_y, std::to_string(row.client_id).c_str(), nullptr);
	nvgText(vg, column_image + column_sid + column_cid + 12.0f, text_y, row.label.c_str(), nullptr);
	nvgRestore(vg);

	nvgBeginPath(vg);
	nvgMoveTo(vg, column_image, rect.y);
	nvgLineTo(vg, column_image, rect.y + rect.height);
	nvgMoveTo(vg, column_image + column_sid, rect.y);
	nvgLineTo(vg, column_image + column_sid, rect.y + rect.height);
	nvgMoveTo(vg, column_image + column_sid + column_cid, rect.y);
	nvgLineTo(vg, column_image + column_sid + column_cid, rect.y + rect.height);
	nvgStrokeWidth(vg, 1.0f);
	nvgStrokeColor(vg, toColor(border));
	nvgStroke(vg);
}

void AdvancedFinderResultsView::drawGridCard(NVGcontext* vg, const wxRect& rect, const AdvancedFinderCatalogRow& row, bool selected, bool hovered) const {
	const wxColour border = Theme::Get(Theme::Role::CardBorder);
	const wxColour hover_fill = Theme::Get(Theme::Role::CardBaseHover);
	const wxColour selected_fill = Theme::Get(Theme::Role::Accent);
	const wxColour surface = Theme::Get(Theme::Role::RaisedSurface);

	nvgBeginPath(vg);
	nvgRoundedRect(vg, rect.x, rect.y, rect.width, rect.height, 4.0f);
	nvgFillColor(vg, toColor(selected ? selected_fill : (hovered ? hover_fill : surface)));
	nvgFill(vg);
	nvgStrokeWidth(vg, 1.0f);
	nvgStrokeColor(vg, toColor(selected ? Theme::Get(Theme::Role::AccentHover) : border));
	nvgStroke(vg);

	const wxRect sprite_rect(
		rect.x + (rect.width - FromDIP(32)) / 2,
		rect.y + (rect.height - FromDIP(32)) / 2,
		FromDIP(32),
		FromDIP(32)
	);
	drawSpriteBadge(vg, sprite_rect, spriteForRow(row));
}

void AdvancedFinderResultsView::drawGridHoverInfo(NVGcontext* vg, int width, int height, int scroll_pos) const {
	if (hover_index_ < 0 || hover_index_ >= static_cast<int>(rows_.size()) || hover_position_ == wxDefaultPosition) {
		return;
	}

	const auto& row = *rows_[hover_index_];
	const std::string summary = hoverSummary(row);
	const wxColour panel = Theme::Get(Theme::Role::RaisedSurface);
	const wxColour border = Theme::Get(Theme::Role::CardBorder);
	const wxColour text = Theme::Get(Theme::Role::Text);

	nvgFontFace(vg, "sans");
	nvgFontSize(vg, 12.0f);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

	float bounds[4] {};
	nvgTextBounds(vg, 0.0f, 0.0f, summary.c_str(), nullptr, bounds);
	const float panel_width = std::min(static_cast<float>(width - FromDIP(24)), (bounds[2] - bounds[0]) + static_cast<float>(FromDIP(18)));
	const float panel_height = static_cast<float>(FromDIP(28));
	float panel_x = static_cast<float>(hover_position_.x + FromDIP(14));
	float panel_y = static_cast<float>(hover_position_.y + scroll_pos + FromDIP(18));
	panel_x = std::clamp(panel_x, static_cast<float>(FromDIP(8)), static_cast<float>(width) - panel_width - static_cast<float>(FromDIP(8)));
	panel_y = std::clamp(
		panel_y,
		static_cast<float>(scroll_pos + FromDIP(8)),
		static_cast<float>(scroll_pos + height) - panel_height - static_cast<float>(FromDIP(8))
	);

	nvgBeginPath(vg);
	nvgRoundedRect(vg, panel_x, panel_y, panel_width, panel_height, 6.0f);
	nvgFillColor(vg, toColor(panel));
	nvgFill(vg);
	nvgStrokeWidth(vg, 1.0f);
	nvgStrokeColor(vg, toColor(border));
	nvgStroke(vg);

	nvgSave(vg);
	nvgScissor(vg, panel_x + FromDIP(9), panel_y, panel_width - FromDIP(18), panel_height);
	nvgFillColor(vg, toColor(text));
	nvgText(vg, panel_x + FromDIP(9), panel_y + panel_height * 0.5f, summary.c_str(), nullptr);
	nvgRestore(vg);
}
