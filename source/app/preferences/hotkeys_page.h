#ifndef RME_PREFERENCES_HOTKEYS_PAGE_H
#define RME_PREFERENCES_HOTKEYS_PAGE_H

#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>

#include "preferences_page.h"
#include "ui/main_menubar.h"

class HotkeysPage : public PreferencesPage {
public:
	explicit HotkeysPage(wxWindow* parent);
	void Apply() override;

private:
	void PopulateList();
	void UpdateSelection();
	int FindHotkeyConflict(const std::string& hotkeyText, MenuBar::ActionID excludeId) const;

	void OnSearchChanged(wxCommandEvent& event);
	void OnListSelected(wxListEvent& event);
	void OnSetHotkey(wxCommandEvent& event);
	void OnClearHotkey(wxCommandEvent& event);
	void OnResetSelected(wxCommandEvent& event);
	void OnResetAll(wxCommandEvent& event);
	void OnHotkeyKeyDown(wxKeyEvent& event);

	wxTextCtrl* m_searchCtrl = nullptr;
	wxListCtrl* m_listCtrl = nullptr;
	wxTextCtrl* m_hotkeyCtrl = nullptr;
	wxButton* m_setButton = nullptr;
	wxButton* m_clearButton = nullptr;
	wxButton* m_resetButton = nullptr;
	wxButton* m_resetAllButton = nullptr;
	wxStaticText* m_statusText = nullptr;

	std::vector<MenuHotkeyEntry> m_entries;
	bool m_capturing = false;
	std::string m_capturedHotkey;

	wxChoice* m_groundReplaceModifier = nullptr;
};

#endif
