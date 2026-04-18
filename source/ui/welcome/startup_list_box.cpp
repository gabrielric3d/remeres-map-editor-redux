#include "ui/welcome/startup_list_box.h"

#include "util/image_manager.h"
#include "ui/theme.h"

wxDEFINE_EVENT(STARTUP_FAVORITE_TOGGLED, wxCommandEvent);

namespace {
constexpr int kStarSize = 16;
constexpr int kStarRightPadding = 12;
}

StartupListBox::StartupListBox(wxWindow* parent, wxWindowID id) :
	wxVListBox(parent, id, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE | wxBORDER_NONE) {
	SetBackgroundColour(Theme::Get(Theme::Role::PanelBackground));
	SetForegroundColour(Theme::Get(Theme::Role::Text));

	Bind(wxEVT_LEFT_DOWN, &StartupListBox::OnLeftDown, this);
	Bind(wxEVT_MOTION, &StartupListBox::OnMouseMotion, this);
	Bind(wxEVT_LEAVE_WINDOW, &StartupListBox::OnMouseLeave, this);
}

void StartupListBox::SetItems(std::vector<StartupListItem> items) {
	m_items = std::move(items);
	SetItemCount(m_items.size());
	m_hovered_star_index = wxNOT_FOUND;
	RefreshAll();
}

const StartupListItem* StartupListBox::GetItem(size_t index) const {
	if (index >= m_items.size()) {
		return nullptr;
	}
	return &m_items[index];
}

void StartupListBox::OnDrawBackground(wxDC& dc, const wxRect& rect, size_t index) const {
	const bool selected = IsSelected(index);
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(selected ? Theme::Get(Theme::Role::SelectionFill) : Theme::Get(Theme::Role::PanelBackground)));
	dc.DrawRoundedRectangle(rect.Deflate(FromDIP(2), FromDIP(2)), FromDIP(8));

	if (selected) {
		wxRect accent_rect = rect.Deflate(FromDIP(4), FromDIP(6));
		accent_rect.SetWidth(FromDIP(4));
		dc.SetBrush(wxBrush(Theme::Get(Theme::Role::PrimaryButton)));
		dc.DrawRoundedRectangle(accent_rect, FromDIP(2));
	}
}

void StartupListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t index) const {
	if (index >= m_items.size()) {
		return;
	}

	const auto& item = m_items[index];
	const bool selected = IsSelected(index);
	const int icon_size = FromDIP(16);
	const int left_padding = FromDIP(14);
	const int top_padding = FromDIP(10);
	const int text_x = rect.x + left_padding + icon_size + FromDIP(10);
	const int star_reserve = item.show_star ? FromDIP(kStarSize + kStarRightPadding + 6) : FromDIP(12);
	const int available_width = rect.width - (text_x - rect.x) - star_reserve;
	const bool has_tertiary = !item.tertiary_text.IsEmpty();

	const wxBitmap icon = GetIconBitmap(item.icon_art_id);
	if (icon.IsOk()) {
		dc.DrawBitmap(icon, rect.x + left_padding, rect.y + top_padding, true);
	}

	dc.SetFont(Theme::GetFont(10, true));
	const wxColour primary_colour = selected ? Theme::Get(Theme::Role::TextOnAccent) : (item.accent_colour.IsOk() ? item.accent_colour : *wxWHITE);
	dc.SetTextForeground(primary_colour);
	const wxString primary_text = wxControl::Ellipsize(item.primary_text, dc, wxELLIPSIZE_MIDDLE, available_width);
	dc.DrawText(primary_text, text_x, rect.y + FromDIP(6));

	const wxColour subtle_colour = selected ? Theme::Get(Theme::Role::TextOnAccent) : Theme::Get(Theme::Role::TextSubtle);
	dc.SetFont(Theme::GetFont(8, false));
	dc.SetTextForeground(subtle_colour);
	const wxString secondary_text = wxControl::Ellipsize(item.secondary_text, dc, wxELLIPSIZE_MIDDLE, available_width);
	dc.DrawText(secondary_text, text_x, rect.y + FromDIP(28));

	if (has_tertiary) {
		const wxString tertiary_text = wxControl::Ellipsize(item.tertiary_text, dc, wxELLIPSIZE_END, available_width);
		dc.DrawText(tertiary_text, text_x, rect.y + FromDIP(46));
	}

	if (item.show_star) {
		const std::string art = item.is_favorite ? std::string(ICON_STAR_SOLID) : std::string(ICON_STAR);
		const wxBitmap star = GetIconBitmap(art);
		if (star.IsOk()) {
			const wxRect star_rect = GetStarRect(rect);
			dc.DrawBitmap(star, star_rect.x, star_rect.y, true);
		}
	}
}

wxCoord StartupListBox::OnMeasureItem(size_t index) const {
	if (index < m_items.size() && !m_items[index].tertiary_text.IsEmpty()) {
		return FromDIP(68);
	}
	return FromDIP(50);
}

wxBitmap StartupListBox::GetIconBitmap(const std::string& art_id) const {
	if (const auto cached = m_icon_cache.find(art_id); cached != m_icon_cache.end()) {
		return cached->second;
	}

	wxBitmap bitmap = IMAGE_MANAGER.GetBitmap(art_id, wxSize(16, 16));
	m_icon_cache.emplace(art_id, bitmap);
	return bitmap;
}

wxRect StartupListBox::GetStarRect(const wxRect& item_rect) const {
	const int star_size = FromDIP(kStarSize);
	const int right_padding = FromDIP(kStarRightPadding);
	const int x = item_rect.GetRight() - right_padding - star_size;
	const int y = item_rect.y + (item_rect.height - star_size) / 2;
	return wxRect(x, y, star_size, star_size);
}

int StartupListBox::HitTestStar(const wxPoint& position) const {
	const int item = VirtualHitTest(position.y);
	if (item == wxNOT_FOUND || item >= static_cast<int>(m_items.size())) {
		return wxNOT_FOUND;
	}
	if (!m_items[item].show_star) {
		return wxNOT_FOUND;
	}

	const int star_size = FromDIP(kStarSize);
	const int right_padding = FromDIP(kStarRightPadding);
	const int hit_left = GetClientSize().GetWidth() - right_padding - star_size - FromDIP(4);
	const int hit_right = GetClientSize().GetWidth() - right_padding + FromDIP(4);
	return (position.x >= hit_left && position.x <= hit_right) ? item : wxNOT_FOUND;
}

void StartupListBox::OnLeftDown(wxMouseEvent& event) {
	const int star_index = HitTestStar(event.GetPosition());
	if (star_index != wxNOT_FOUND) {
		const auto& item = m_items[star_index];
		wxCommandEvent toggle_event(STARTUP_FAVORITE_TOGGLED, GetId());
		toggle_event.SetEventObject(this);
		toggle_event.SetInt(star_index);
		toggle_event.SetString(item.primary_text);
		GetEventHandler()->ProcessEvent(toggle_event);
		return;
	}
	event.Skip();
}

void StartupListBox::OnMouseMotion(wxMouseEvent& event) {
	const int star_index = HitTestStar(event.GetPosition());
	if (star_index != m_hovered_star_index) {
		m_hovered_star_index = star_index;
		SetCursor(star_index != wxNOT_FOUND ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
	}
	event.Skip();
}

void StartupListBox::OnMouseLeave(wxMouseEvent& event) {
	if (m_hovered_star_index != wxNOT_FOUND) {
		m_hovered_star_index = wxNOT_FOUND;
		SetCursor(wxNullCursor);
	}
	event.Skip();
}
