#ifndef RME_SCRIPT_MENU_HANDLER_H_
#define RME_SCRIPT_MENU_HANDLER_H_

#include "ui/gui_ids.h"
#include <wx/menu.h>
#include <wx/event.h>

class MainFrame;
class wxMenu;

class ScriptMenuHandler {
public:
	ScriptMenuHandler(MainFrame* frame);
	~ScriptMenuHandler();

	void OnScriptsOpenFolder(wxCommandEvent& event);
	void OnScriptsReload(wxCommandEvent& event);
	void OnScriptsManager(wxCommandEvent& event);
	void OnScriptExecute(wxCommandEvent& event);
	void OnShowOverlayToggle(wxCommandEvent& event);

	void LoadScriptsMenu(wxMenu* menu);
	void LoadShowMenu(wxMenu* menu);
	void UpdateShowMenu(wxMenu* menu);

private:
	MainFrame* m_frame;
	wxMenu* m_scriptsMenu = nullptr;
	wxMenu* m_showMenu = nullptr;
	size_t m_showMenuCount = 0;
	bool m_showMenuHasSeparator = false;
};

#endif
