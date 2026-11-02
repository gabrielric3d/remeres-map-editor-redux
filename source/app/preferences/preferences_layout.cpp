#include "app/preferences/preferences_layout.h"

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/scrolwin.h>

namespace {
constexpr int kSectionPaddingDip = 14;
constexpr int kRowControlGapDip = 12;
// Used until the parent has a size, so the first layout is not squeezed.
constexpr int kDefaultWrapWidthDip = 220;
constexpr int kMinWrapWidthDip = 120;

wxStaticText* CreateTitleLabel(wxWindow* parent, const wxString& text) {
	auto* label = new wxStaticText(parent, wxID_ANY, text);
	label->SetFont(Theme::GetFont(9, true));
	label->SetForegroundColour(Theme::Get(Theme::Role::Text));
	label->SetBackgroundColour(parent->GetBackgroundColour());
	return label;
}

wxStaticText* CreateDescriptionLabel(wxWindow* parent, const wxString& text) {
	auto* label = new wxStaticText(parent, wxID_ANY, text);
	label->SetFont(Theme::GetFont(8, false));
	label->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	label->SetBackgroundColour(parent->GetBackgroundColour());
	return label;
}

// Space the control takes beside the row's text column.
int RowReservedWidth(PreferencesSectionPanel* section, wxWindow* control) {
	return control->GetBestSize().GetWidth() + wxWindow::FromDIP(kRowControlGapDip, section);
}
}

PreferencesSectionPanel::PreferencesSectionPanel(wxWindow* parent, const wxString& title, const wxString& description) :
	wxPanel(parent, wxID_ANY) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetBackgroundColour(Theme::Get(Theme::Role::RaisedSurface));
	SetForegroundColour(Theme::Get(Theme::Role::Text));

	auto* root_sizer = new wxBoxSizer(wxVERTICAL);
	const int padding = wxWindow::FromDIP(kSectionPaddingDip, this);

	root_sizer->AddSpacer(wxWindow::FromDIP(12, this));

	auto* title_label = new wxStaticText(this, wxID_ANY, title);
	title_label->SetFont(Theme::GetFont(10, true));
	title_label->SetForegroundColour(Theme::Get(Theme::Role::Text));
	title_label->SetBackgroundColour(GetBackgroundColour());
	root_sizer->Add(title_label, 0, wxLEFT | wxRIGHT, padding);

	if (!description.empty()) {
		root_sizer->AddSpacer(wxWindow::FromDIP(4, this));
		m_description_label = CreateDescriptionLabel(this, description);
		RegisterWrappedLabel(m_description_label);
		root_sizer->Add(m_description_label, 0, wxLEFT | wxRIGHT, padding);
	}

	auto* separator = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, wxWindow::FromDIP(1, this)));
	separator->SetBackgroundColour(Theme::Get(Theme::Role::Border));
	root_sizer->Add(separator, 0, wxEXPAND | wxALL, padding);

	m_body_sizer = new wxBoxSizer(wxVERTICAL);
	root_sizer->Add(m_body_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, padding);

	SetSizer(root_sizer);
	Bind(wxEVT_PAINT, &PreferencesSectionPanel::OnPaint, this);
	// Own size: catches the first layout, when the section gets its real margin.
	Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
		UpdateWrapping();
		event.Skip();
	});
	// Parent size: the dialog was resized, so the text has to rewrap (also narrower).
	if (parent) {
		parent->Bind(wxEVT_SIZE, &PreferencesSectionPanel::OnParentSize, this);
	}
}

void PreferencesSectionPanel::RegisterWrappedLabel(wxStaticText* label, int reserved_width) {
	if (!label) {
		return;
	}

	m_wrapped_labels.push_back({ label, label->GetLabel(), label->GetLabel(), reserved_width });
	WrapLabel(m_wrapped_labels.back(), GetAvailableWidth());
}

int PreferencesSectionPanel::GetAvailableWidth() const {
	const wxWindow* parent = GetParent();
	if (!parent) {
		return -1;
	}
	const int parent_width = parent->GetClientSize().GetWidth();
	if (parent_width <= 1) {
		return -1;
	}
	// The pages give sections the same margin on both sides.
	const int margin = std::max(GetPosition().x, 0);
	return parent_width - 2 * margin;
}

void PreferencesSectionPanel::WrapLabel(WrappedLabel& wrapped, int section_width) {
	// The page changed the text after the last wrap (summary labels do): that is the new original.
	if (wrapped.label->GetLabel() != wrapped.wrapped_text) {
		wrapped.text = wrapped.label->GetLabel();
	}

	int wrap_width = wxWindow::FromDIP(kDefaultWrapWidthDip, this);
	if (section_width > 0) {
		// Section padding on both sides, the control beside the text and the rounded border.
		wrap_width = section_width - 2 * wxWindow::FromDIP(kSectionPaddingDip, this) - wrapped.reserved_width - wxWindow::FromDIP(4, this);
	}
	wrap_width = std::max(wrap_width, wxWindow::FromDIP(kMinWrapWidthDip, this));

	// Wrap() inserts line breaks into the label, so start again from the original text.
	wrapped.label->SetLabel(wrapped.text);
	wrapped.label->Wrap(wrap_width);
	wrapped.wrapped_text = wrapped.label->GetLabel();
}

void PreferencesSectionPanel::OnParentSize(wxSizeEvent& event) {
	UpdateWrapping();
	event.Skip();
}

void PreferencesSectionPanel::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);
	if (auto* parent = GetParent()) {
		dc.SetBackground(wxBrush(parent->GetBackgroundColour()));
	} else {
		dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	}
	dc.Clear();

	const wxRect rect = GetClientRect().Deflate(1);
	dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
	dc.SetBrush(wxBrush(Theme::Get(Theme::Role::RaisedSurface)));
	dc.DrawRoundedRectangle(rect, wxWindow::FromDIP(10, this));
}

void PreferencesSectionPanel::UpdateWrapping() {
	const int section_width = GetAvailableWidth();
	if (section_width == m_last_section_width) {
		return;
	}

	m_last_section_width = section_width;
	for (auto& wrapped : m_wrapped_labels) {
		WrapLabel(wrapped, section_width);
	}
	InvalidateBestSize();
	Layout();
	if (auto* containing_sizer = GetContainingSizer()) {
		containing_sizer->Layout();
	}

	if (!m_scroll_refresh_pending) {
		m_scroll_refresh_pending = true;
		CallAfter(&PreferencesSectionPanel::RefreshScrolledParent);
	}
}

void PreferencesSectionPanel::RefreshScrolledParent() {
	m_scroll_refresh_pending = false;
	for (auto* window = GetParent(); window; window = window->GetParent()) {
		if (auto* scrolled_window = dynamic_cast<wxScrolledWindow*>(window)) {
			scrolled_window->FitInside();
			break;
		}
	}
}

wxStaticText* PreferencesLayout::CreateBodyText(wxWindow* parent, const wxString& text, bool bold) {
	auto* label = new wxStaticText(parent, wxID_ANY, text);
	label->SetFont(Theme::GetFont(9, bold));
	label->SetForegroundColour(Theme::Get(Theme::Role::Text));
	label->SetBackgroundColour(parent->GetBackgroundColour());
	if (auto* section = dynamic_cast<PreferencesSectionPanel*>(parent)) {
		section->RegisterWrappedLabel(label);
	}
	return label;
}

wxStaticText* PreferencesLayout::AddNotice(PreferencesSectionPanel* section, const wxString& text, Theme::Role role) {
	auto* notice = new wxStaticText(section, wxID_ANY, text);
	notice->SetFont(Theme::GetFont(8, false));
	notice->SetForegroundColour(Theme::Get(role));
	notice->SetBackgroundColour(section->GetBackgroundColour());
	section->RegisterWrappedLabel(notice);
	section->GetBodySizer()->Add(notice, 0, wxEXPAND | wxBOTTOM, wxWindow::FromDIP(10, section));
	return notice;
}

wxCheckBox* PreferencesLayout::AddCheckBoxRow(
	PreferencesSectionPanel* section,
	const wxString& title,
	const wxString& description,
	bool value
) {
	auto* row_sizer = new wxBoxSizer(wxHORIZONTAL);
	auto* text_sizer = new wxBoxSizer(wxVERTICAL);

	auto* description_label = CreateDescriptionLabel(section, description);
	text_sizer->Add(CreateTitleLabel(section, title), 0, wxBOTTOM, wxWindow::FromDIP(2, section));
	text_sizer->Add(description_label, 0, wxEXPAND);
	row_sizer->Add(text_sizer, 1, wxEXPAND | wxRIGHT, wxWindow::FromDIP(kRowControlGapDip, section));

	auto* checkbox = new wxCheckBox(section, wxID_ANY, wxEmptyString);
	checkbox->SetValue(value);
	row_sizer->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL | wxTOP, wxWindow::FromDIP(2, section));
	section->RegisterWrappedLabel(description_label, RowReservedWidth(section, checkbox));

	section->GetBodySizer()->Add(row_sizer, 0, wxEXPAND | wxBOTTOM, wxWindow::FromDIP(12, section));
	return checkbox;
}

void PreferencesLayout::AddControlRow(
	PreferencesSectionPanel* section,
	const wxString& title,
	const wxString& description,
	wxWindow* control,
	bool expand_control
) {
	auto* row_sizer = new wxBoxSizer(wxHORIZONTAL);
	auto* text_sizer = new wxBoxSizer(wxVERTICAL);

	auto* description_label = CreateDescriptionLabel(section, description);
	text_sizer->Add(CreateTitleLabel(section, title), 0, wxBOTTOM, wxWindow::FromDIP(2, section));
	text_sizer->Add(description_label, 0, wxEXPAND);
	row_sizer->Add(text_sizer, 1, wxEXPAND | wxRIGHT, wxWindow::FromDIP(kRowControlGapDip, section));
	section->RegisterWrappedLabel(description_label, RowReservedWidth(section, control));

	int flags = wxTOP;
	if (expand_control) {
		flags |= wxEXPAND;
	} else {
		flags |= wxALIGN_CENTER_VERTICAL;
	}
	row_sizer->Add(control, expand_control ? 1 : 0, flags, wxWindow::FromDIP(2, section));

	section->GetBodySizer()->Add(row_sizer, 0, wxEXPAND | wxBOTTOM, wxWindow::FromDIP(12, section));
}

wxStaticText* PreferencesLayout::AddValuePreviewRow(
	PreferencesSectionPanel* section,
	const wxString& title,
	const wxString& description,
	const wxString& value
) {
	auto* value_label = CreateBodyText(section, value, true);
	value_label->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	AddControlRow(section, title, description, value_label, false);
	return value_label;
}
