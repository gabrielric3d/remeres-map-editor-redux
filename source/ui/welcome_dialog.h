#ifndef WELCOME_DIALOG_H
#define WELCOME_DIALOG_H

#include <optional>
#include <unordered_map>
#include <vector>

#include <wx/timer.h>
#include <wx/wx.h>

#include "io/iomap_otbm.h"
#include "ui/welcome/startup_types.h"

class StartupButton;
class StartupCardPanel;
class StartupListBox;

wxDECLARE_EVENT(WELCOME_DIALOG_ACTION, wxCommandEvent);

class WelcomeDialog : public wxDialog {
public:
	WelcomeDialog(const wxString& title_text, const wxString& version_text, const wxSize& size, const wxBitmap& rme_logo, const std::vector<wxString>& recent_files);
	~WelcomeDialog() override = default;

	std::optional<StartupLoadRequest> ConsumePendingLoadRequest();
	void RefreshConfiguredClients();

private:
	void BuildInterface(const wxString& title_text, const wxString& version_text, const wxBitmap& rme_logo);

	void OnPreferences(wxCommandEvent& event);
	void OnNewMap(wxCommandEvent& event);
	void OnBrowseMap(wxCommandEvent& event);
	void OnExit(wxCommandEvent& event);
	void OnLoadRequested(wxCommandEvent& event);
	void OnInfoRequested(wxCommandEvent& event);
	void OnRecentMapSelected(wxCommandEvent& event);
	void OnRecentMapActivated(wxCommandEvent& event);
	void OnFavoriteMapSelected(wxCommandEvent& event);
	void OnFavoriteMapActivated(wxCommandEvent& event);
	void OnRecentFavoriteToggled(wxCommandEvent& event);
	void OnFavoriteFavoriteToggled(wxCommandEvent& event);
	void OnClientSelected(wxCommandEvent& event);
	void OnForceLoadChanged(wxCommandEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnInfoPulseTick(wxTimerEvent& event);

	void RefreshRecentMapList();
	void RefreshFavoriteMapList();
	void RefreshClientList();
	void RefreshFooterState();
	void UpdateStatusWrap();
	void UpdateInfoButtonPulse();
	[[nodiscard]] wxString GetSelectedMapPath() const;

	void SetSelectedMapIndex(int index);
	void SetSelectedFavoriteIndex(int index);
	void SetSelectedClientIndex(int index, bool manual_selection);
	void AutoSelectMatchingClient();

	bool AttemptLoad(bool show_message);
	bool AddBrowsedMap(const wxString& path);

	[[nodiscard]] std::vector<wxString> LoadFavoritesFromSettings() const;
	void SaveFavoritesToSettings() const;
	void ToggleFavoriteByPath(const wxString& path);

	void EnsurePeeked(const wxString& path);
	[[nodiscard]] const OTBMStartupPeekResult* GetSelectedMapInfo() const;
	[[nodiscard]] ClientVersion* GetSelectedClient() const;
	[[nodiscard]] StartupCompatibilityStatus GetCompatibilityStatus() const;
	[[nodiscard]] wxString BuildCompatibilityMessage() const;

	[[nodiscard]] int FindRecentMapIndex(const wxString& path) const;
	[[nodiscard]] static wxString FormatModifiedLabel(const wxString& path);
	[[nodiscard]] static std::string NormalizePathKey(const wxString& path);

	std::vector<StartupRecentMapEntry> m_recent_maps;
	std::vector<StartupRecentMapEntry> m_favorite_maps;
	std::vector<StartupConfiguredClientEntry> m_configured_clients;
	std::unordered_map<std::string, OTBMStartupPeekResult> m_peek_cache;
	std::optional<StartupLoadRequest> m_pending_load_request;

	StartupListBox* m_recent_list = nullptr;
	StartupListBox* m_favorite_list = nullptr;
	StartupListBox* m_client_list = nullptr;
	StartupButton* m_load_button = nullptr;
	StartupButton* m_info_button = nullptr;
	StartupCardPanel* m_status_card = nullptr;
	wxCheckBox* m_force_load_checkbox = nullptr;
	wxStaticText* m_status_text = nullptr;
	wxTimer m_info_pulse_timer;
	int m_info_pulse_phase = 0;
	int m_selected_map_index = wxNOT_FOUND;
	int m_selected_favorite_index = wxNOT_FOUND;
	int m_selected_client_index = wxNOT_FOUND;
	bool m_has_manual_client_selection = false;
	bool m_selection_in_favorites = false;
};

#endif
