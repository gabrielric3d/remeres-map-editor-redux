#ifndef RME_UI_MENUBAR_VIEW_SETTINGS_HANDLER_H_
#define RME_UI_MENUBAR_VIEW_SETTINGS_HANDLER_H_

#include <wx/wx.h>

class MainMenuBar;

class ViewSettingsHandler {
public:
	ViewSettingsHandler(MainMenuBar* menuBar);

	void LoadValues();
	void OnChangeViewSettings(wxCommandEvent& event);
	void OnToolbars(wxCommandEvent& event);
	void OnToggleAutomagic(wxCommandEvent& event);
	void OnToggleCarpetFillBorders(wxCommandEvent& event);
	void OnToggleFillSwapBorders(wxCommandEvent& event);
	void OnToggleDisableCarpetInteraction(wxCommandEvent& event);
	void OnToggleDeleteRemovesZones(wxCommandEvent& event);
	void OnSelectionTypeChange(wxCommandEvent& event);
	void OnSelectionLassoToggle(wxCommandEvent& event);
	void OnSelectionMagicWandToggle(wxCommandEvent& event);
	// Single entry point for the magic wand toggle (menu, radial wheel, Tool Options
	// button): stores the setting, syncs the menu check and the Tool Options button,
	// and toasts the new state.
	static void SetMagicWandEnabled(bool enabled);
	void OnToggleShowLights(wxCommandEvent& event);
	void OnToggleScreenShader(wxCommandEvent& event);
	void OnReloadForcedLightData(wxCommandEvent& event);

private:
	MainMenuBar* menuBar;
};

#endif
