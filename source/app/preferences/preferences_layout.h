#ifndef RME_PREFERENCES_LAYOUT_H_
#define RME_PREFERENCES_LAYOUT_H_

#include <vector>

#include <wx/checkbox.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "ui/theme.h"

class PreferencesSectionPanel : public wxPanel {
public:
	PreferencesSectionPanel(wxWindow* parent, const wxString& title, const wxString& description = wxString());

	wxBoxSizer* GetBodySizer() const {
		return m_body_sizer;
	}

	// reserved_width: room taken on the same row by the control beside the label.
	void RegisterWrappedLabel(wxStaticText* label, int reserved_width = 0);

private:
	struct WrappedLabel {
		wxStaticText* label;
		wxString text; // unwrapped text: Wrap() bakes line breaks into the label
		wxString wrapped_text; // label text after the last wrap, to notice SetLabel() from outside
		int reserved_width;
	};

	void OnPaint(wxPaintEvent& event);
	void OnParentSize(wxSizeEvent& event);
	// Width the section gets from its parent, which does not depend on the section's
	// own contents (a wrap width taken from the section itself only ever grows).
	int GetAvailableWidth() const;
	void WrapLabel(WrappedLabel& wrapped, int section_width);
	void UpdateWrapping();
	void RefreshScrolledParent();

	wxStaticText* m_description_label = nullptr;
	wxBoxSizer* m_body_sizer = nullptr;
	std::vector<WrappedLabel> m_wrapped_labels;
	int m_last_section_width = -1;
	bool m_scroll_refresh_pending = false;
};

namespace PreferencesLayout {
wxStaticText* CreateBodyText(wxWindow* parent, const wxString& text, bool bold = false);
wxStaticText* AddNotice(PreferencesSectionPanel* section, const wxString& text, Theme::Role role = Theme::Role::TextSubtle);
wxCheckBox* AddCheckBoxRow(
	PreferencesSectionPanel* section,
	const wxString& title,
	const wxString& description,
	bool value
);
void AddControlRow(
	PreferencesSectionPanel* section,
	const wxString& title,
	const wxString& description,
	wxWindow* control,
	bool expand_control = false
);
wxStaticText* AddValuePreviewRow(
	PreferencesSectionPanel* section,
	const wxString& title,
	const wxString& description,
	const wxString& value
);
}

#endif
