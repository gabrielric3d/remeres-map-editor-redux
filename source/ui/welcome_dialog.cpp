#include "ui/welcome_dialog.h"

#include <algorithm>
#include <format>
#include <ranges>
#include <sstream>

#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/statline.h>

#include "app/client_version.h"
#include "app/definitions.h"
#include "app/main.h"
#include "app/preferences.h"
#include "app/settings.h"
#include "ui/theme.h"
#include "ui/welcome/map_details_dialog.h"
#include "ui/welcome/startup_button.h"
#include "ui/welcome/startup_card.h"
#include "ui/welcome/startup_formatters.h"
#include "ui/welcome/startup_list_box.h"
#include "util/image_manager.h"

wxDEFINE_EVENT(WELCOME_DIALOG_ACTION, wxCommandEvent);

namespace {
constexpr int ID_RECENT_LIST = wxID_HIGHEST + 101;
constexpr int ID_CLIENT_LIST = wxID_HIGHEST + 102;
constexpr int ID_FORCE_LOAD = wxID_HIGHEST + 103;
constexpr int ID_LOAD_BUTTON = wxID_HIGHEST + 104;
constexpr int ID_FAVORITE_LIST = wxID_HIGHEST + 105;
constexpr int ID_INFO_BUTTON = wxID_HIGHEST + 106;

bool PathsEqual(const wxString& lhs, const wxString& rhs) {
#ifdef __WINDOWS__
	return lhs.CmpNoCase(rhs) == 0;
#else
	return lhs == rhs;
#endif
}

wxString BuildHeaderTitle(const wxString& title_text, const wxString& version_text) {
	if (version_text.empty()) {
		return title_text;
	}

	wxString compact_version = version_text;
	if (compact_version.StartsWith("Version ")) {
		compact_version = compact_version.AfterFirst(' ');
	}

	return compact_version.empty() ? title_text : title_text + " " + compact_version;
}
}

WelcomeDialog::WelcomeDialog(const wxString& title_text, const wxString& version_text, const wxSize& size, const wxBitmap& rme_logo, const std::vector<wxString>& recent_files) :
	wxDialog(nullptr, wxID_ANY, title_text, wxDefaultPosition, size, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	SetBackgroundColour(Theme::Get(Theme::Role::PanelBackground));
	SetForegroundColour(Theme::Get(Theme::Role::Text));
	SetMinSize(FROM_DIP(this, wxSize(1180, 720)));

	const auto favorite_paths = LoadFavoritesFromSettings();
	const auto is_favorite_path = [&favorite_paths](const wxString& path) {
		return std::ranges::any_of(favorite_paths, [&path](const wxString& fav) {
			return PathsEqual(fav, path);
		});
	};

	for (const auto& favorite_path : favorite_paths) {
		m_favorite_maps.push_back({ favorite_path, FormatModifiedLabel(favorite_path), false, true });
	}

	for (const auto& recent_file : recent_files) {
		if (is_favorite_path(recent_file)) {
			continue;
		}
		m_recent_maps.push_back({ recent_file, FormatModifiedLabel(recent_file), false, false });
	}

	for (ClientVersion* client : ClientVersion::getConfiguredVisible()) {
		m_configured_clients.push_back({
			client,
			wxstr(client->getName()),
			client->getClientPath().GetFullPath(),
		});
	}

	BuildInterface(title_text, version_text, rme_logo);
	Bind(wxEVT_SIZE, &WelcomeDialog::OnSize, this);
	m_info_pulse_timer.SetOwner(this);
	Bind(wxEVT_TIMER, &WelcomeDialog::OnInfoPulseTick, this);

	if (!m_configured_clients.empty()) {
		const ClientVersion* latest = ClientVersion::getLatestVersion();
		const auto latest_it = std::ranges::find_if(m_configured_clients, [latest](const auto& entry) {
			return entry.client == latest;
		});
		const int default_client_index = latest_it != m_configured_clients.end() ? static_cast<int>(std::distance(m_configured_clients.begin(), latest_it)) : 0;
		SetSelectedClientIndex(default_client_index, false);
	}

	if (!m_favorite_maps.empty()) {
		SetSelectedFavoriteIndex(0);
	} else if (!m_recent_maps.empty()) {
		SetSelectedMapIndex(0);
	} else {
		RefreshFooterState();
	}

	Centre();
}

std::optional<StartupLoadRequest> WelcomeDialog::ConsumePendingLoadRequest() {
	auto request = m_pending_load_request;
	m_pending_load_request.reset();
	return request;
}

void WelcomeDialog::RefreshConfiguredClients() {
	const auto selected_client_id = GetSelectedClient() != nullptr ? GetSelectedClient()->getID() : ClientVersionID {};

	m_configured_clients.clear();
	for (ClientVersion* client : ClientVersion::getConfiguredVisible()) {
		m_configured_clients.push_back({
			client,
			wxstr(client->getName()),
			client->getClientPath().GetFullPath(),
		});
	}

	RefreshClientList();

	if (!selected_client_id.empty()) {
		const auto selected_it = std::ranges::find_if(m_configured_clients, [&](const auto& entry) {
			return entry.client != nullptr && entry.client->getID() == selected_client_id;
		});
		if (selected_it != m_configured_clients.end()) {
			SetSelectedClientIndex(static_cast<int>(std::distance(m_configured_clients.begin(), selected_it)), m_has_manual_client_selection);
			return;
		}
	}

	if (!m_configured_clients.empty()) {
		const ClientVersion* latest = ClientVersion::getLatestVersion();
		const auto latest_it = std::ranges::find_if(m_configured_clients, [latest](const auto& entry) {
			return entry.client == latest;
		});
		const int fallback_index = latest_it != m_configured_clients.end() ? static_cast<int>(std::distance(m_configured_clients.begin(), latest_it)) : 0;
		SetSelectedClientIndex(fallback_index, false);
	} else {
		SetSelectedClientIndex(wxNOT_FOUND, false);
	}
}

void WelcomeDialog::BuildInterface(const wxString& title_text, const wxString& version_text, const wxBitmap& rme_logo) {
	auto* root_sizer = new wxBoxSizer(wxVERTICAL);

	auto* header_panel = new wxPanel(this, wxID_ANY);
	header_panel->SetBackgroundColour(Theme::Get(Theme::Role::RaisedSurface));
	auto* header_sizer = new wxBoxSizer(wxHORIZONTAL);

	auto* logo = new wxStaticBitmap(header_panel, wxID_ANY, rme_logo);
	header_sizer->Add(logo, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(14));

	auto* title_sizer = new wxBoxSizer(wxVERTICAL);
	auto* title_label = new wxStaticText(header_panel, wxID_ANY, BuildHeaderTitle(title_text, version_text));
	title_label->SetFont(Theme::GetFont(15, true));
	title_label->SetForegroundColour(Theme::Get(Theme::Role::Text));
	title_label->SetBackgroundColour(header_panel->GetBackgroundColour());
	title_sizer->Add(title_label, 0, wxBOTTOM, FromDIP(2));

	auto* subtitle_label = new wxStaticText(header_panel, wxID_ANY, "Welcome back. Pick a map, confirm the client, and load when you are ready.");
	subtitle_label->SetFont(Theme::GetFont(9, false));
	subtitle_label->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	subtitle_label->SetBackgroundColour(header_panel->GetBackgroundColour());
	title_sizer->Add(subtitle_label, 0);

	header_sizer->Add(title_sizer, 1, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(16));

	auto* preferences_button = new StartupButton(header_panel, wxID_PREFERENCES, "Preferences", StartupButtonVariant::Secondary);
	preferences_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_GEAR, FromDIP(wxSize(18, 18))));
	preferences_button->Bind(wxEVT_BUTTON, &WelcomeDialog::OnPreferences, this);
	header_sizer->Add(preferences_button, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(14));

	header_panel->SetSizer(header_sizer);
	root_sizer->Add(header_panel, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, FromDIP(10));

	auto* content_panel = new wxPanel(this, wxID_ANY);
	content_panel->SetBackgroundColour(Theme::Get(Theme::Role::PanelBackground));
	auto* content_sizer = new wxBoxSizer(wxHORIZONTAL);

	auto* actions_card = new StartupCardPanel(content_panel, "Quick Actions");
	actions_card->SetMinSize(wxSize(FromDIP(170), -1));
	auto* new_map_button = new StartupButton(actions_card, wxID_NEW, "New Map", StartupButtonVariant::Primary);
	new_map_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_NEW, FromDIP(wxSize(18, 18))));
	new_map_button->Bind(wxEVT_BUTTON, &WelcomeDialog::OnNewMap, this);
	actions_card->GetBodySizer()->Add(new_map_button, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

	auto* browse_map_button = new StartupButton(actions_card, wxID_OPEN, "Browse Map", StartupButtonVariant::Secondary);
	browse_map_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_OPEN, FromDIP(wxSize(18, 18))));
	browse_map_button->Bind(wxEVT_BUTTON, &WelcomeDialog::OnBrowseMap, this);
	actions_card->GetBodySizer()->Add(browse_map_button, 0, wxEXPAND);
	content_sizer->Add(actions_card, 0, wxEXPAND | wxALL, FromDIP(10));

	auto* favorite_card = new StartupCardPanel(content_panel, "Favorite Maps");
	m_favorite_list = new StartupListBox(favorite_card, ID_FAVORITE_LIST);
	m_favorite_list->Bind(wxEVT_LISTBOX, &WelcomeDialog::OnFavoriteMapSelected, this);
	m_favorite_list->Bind(wxEVT_LISTBOX_DCLICK, &WelcomeDialog::OnFavoriteMapActivated, this);
	m_favorite_list->Bind(STARTUP_FAVORITE_TOGGLED, &WelcomeDialog::OnFavoriteFavoriteToggled, this);
	favorite_card->GetBodySizer()->Add(m_favorite_list, 1, wxEXPAND);
	content_sizer->Add(favorite_card, 30, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, FromDIP(10));

	auto* recent_card = new StartupCardPanel(content_panel, "Recent Maps");
	m_recent_list = new StartupListBox(recent_card, ID_RECENT_LIST);
	m_recent_list->Bind(wxEVT_LISTBOX, &WelcomeDialog::OnRecentMapSelected, this);
	m_recent_list->Bind(wxEVT_LISTBOX_DCLICK, &WelcomeDialog::OnRecentMapActivated, this);
	m_recent_list->Bind(STARTUP_FAVORITE_TOGGLED, &WelcomeDialog::OnRecentFavoriteToggled, this);
	recent_card->GetBodySizer()->Add(m_recent_list, 1, wxEXPAND);
	content_sizer->Add(recent_card, 30, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, FromDIP(10));

	auto* available_clients_card = new StartupCardPanel(content_panel, "Available Clients");
	m_client_list = new StartupListBox(available_clients_card, ID_CLIENT_LIST);
	m_client_list->Bind(wxEVT_LISTBOX, &WelcomeDialog::OnClientSelected, this);
	available_clients_card->GetBodySizer()->Add(m_client_list, 1, wxEXPAND);
	content_sizer->Add(available_clients_card, 20, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, FromDIP(10));

	content_panel->SetSizer(content_sizer);
	root_sizer->Add(content_panel, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));

	auto* footer_panel = new wxPanel(this, wxID_ANY);
	footer_panel->SetBackgroundColour(Theme::Get(Theme::Role::FooterSurface));
	auto* footer_root_sizer = new wxBoxSizer(wxVERTICAL);

	auto* footer_actions = new wxBoxSizer(wxHORIZONTAL);
	auto* footer_left = new wxBoxSizer(wxHORIZONTAL);
	auto* exit_button = new StartupButton(footer_panel, wxID_EXIT, "Exit", StartupButtonVariant::Secondary);
	exit_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_POWER_OFF, FromDIP(wxSize(18, 18))));
	exit_button->Bind(wxEVT_BUTTON, &WelcomeDialog::OnExit, this);
	footer_left->Add(exit_button, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));
	footer_actions->Add(footer_left, 1, wxALIGN_CENTER_VERTICAL);

	m_status_card = new StartupCardPanel(footer_panel, "", true);
	m_status_card->SetMinSize(wxSize(FromDIP(420), -1));
	m_status_text = new wxStaticText(m_status_card, wxID_ANY, "");
	m_status_text->SetFont(Theme::GetFont(9, false));
	m_status_text->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	m_status_text->SetBackgroundColour(m_status_card->GetBackgroundColour());
	m_status_card->GetBodySizer()->Add(m_status_text, 1, wxEXPAND);
	footer_actions->Add(m_status_card, 2, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(10));

	auto* footer_right = new wxBoxSizer(wxHORIZONTAL);
	footer_right->AddStretchSpacer();

	m_info_button = new StartupButton(footer_panel, ID_INFO_BUTTON, "Info", StartupButtonVariant::Secondary);
	m_info_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_CIRCLE_INFO, FromDIP(wxSize(18, 18))));
	m_info_button->Bind(wxEVT_BUTTON, &WelcomeDialog::OnInfoRequested, this);
	m_info_button->Enable(false);
	footer_right->Add(m_info_button, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

	m_force_load_checkbox = new wxCheckBox(footer_panel, ID_FORCE_LOAD, "Force Load");
	m_force_load_checkbox->SetForegroundColour(Theme::Get(Theme::Role::Text));
	m_force_load_checkbox->SetBackgroundColour(footer_panel->GetBackgroundColour());
	m_force_load_checkbox->Bind(wxEVT_CHECKBOX, &WelcomeDialog::OnForceLoadChanged, this);
	footer_right->Add(m_force_load_checkbox, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

	m_load_button = new StartupButton(footer_panel, ID_LOAD_BUTTON, "Load Map", StartupButtonVariant::Primary);
	m_load_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_OPEN, FromDIP(wxSize(18, 18))));
	m_load_button->Bind(wxEVT_BUTTON, &WelcomeDialog::OnLoadRequested, this);
	footer_right->Add(m_load_button, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));
	footer_actions->Add(footer_right, 1, wxEXPAND);

	footer_root_sizer->Add(footer_actions, 0, wxEXPAND);
	footer_panel->SetSizer(footer_root_sizer);
	root_sizer->Add(footer_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));

	SetSizer(root_sizer);

	RefreshRecentMapList();
	RefreshFavoriteMapList();
	RefreshClientList();
}

void WelcomeDialog::OnPreferences(wxCommandEvent& WXUNUSED(event)) {
	PreferencesWindow preferences_window(this, true);
	preferences_window.ShowModal();
	RefreshConfiguredClients();
}

void WelcomeDialog::OnNewMap(wxCommandEvent& WXUNUSED(event)) {
	auto* new_event = new wxCommandEvent(WELCOME_DIALOG_ACTION, wxID_NEW);
	QueueEvent(new_event);
}

void WelcomeDialog::OnBrowseMap(wxCommandEvent& WXUNUSED(event)) {
	wxFileDialog file_dialog(this, "Open map file", "", "", MAP_LOAD_FILE_WILDCARD, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (file_dialog.ShowModal() == wxID_OK) {
		AddBrowsedMap(file_dialog.GetPath());
	}
}

void WelcomeDialog::OnExit(wxCommandEvent& WXUNUSED(event)) {
	Close(true);
}

void WelcomeDialog::OnLoadRequested(wxCommandEvent& WXUNUSED(event)) {
	AttemptLoad(true);
}

void WelcomeDialog::OnInfoRequested(wxCommandEvent& WXUNUSED(event)) {
	const OTBMStartupPeekResult* map_info = GetSelectedMapInfo();
	if (!map_info) {
		return;
	}
	MapDetailsDialog dialog(this, GetSelectedMapPath(), map_info, GetSelectedClient());
	dialog.ShowModal();
}

void WelcomeDialog::OnInfoPulseTick(wxTimerEvent& WXUNUSED(event)) {
	if (!m_info_button || !m_info_button->IsEnabled()) {
		return;
	}
	m_info_pulse_phase = (m_info_pulse_phase + 1) % 2;
	m_info_button->SetVariant(m_info_pulse_phase == 0 ? StartupButtonVariant::Secondary : StartupButtonVariant::Primary);
}

void WelcomeDialog::OnRecentMapSelected(wxCommandEvent& WXUNUSED(event)) {
	SetSelectedMapIndex(m_recent_list->GetSelection());
}

void WelcomeDialog::OnRecentMapActivated(wxCommandEvent& WXUNUSED(event)) {
	AttemptLoad(true);
}

void WelcomeDialog::OnFavoriteMapSelected(wxCommandEvent& WXUNUSED(event)) {
	SetSelectedFavoriteIndex(m_favorite_list->GetSelection());
}

void WelcomeDialog::OnFavoriteMapActivated(wxCommandEvent& WXUNUSED(event)) {
	AttemptLoad(true);
}

void WelcomeDialog::OnRecentFavoriteToggled(wxCommandEvent& event) {
	const int index = event.GetInt();
	if (index < 0 || index >= static_cast<int>(m_recent_maps.size())) {
		return;
	}
	ToggleFavoriteByPath(m_recent_maps[index].path);
}

void WelcomeDialog::OnFavoriteFavoriteToggled(wxCommandEvent& event) {
	const int index = event.GetInt();
	if (index < 0 || index >= static_cast<int>(m_favorite_maps.size())) {
		return;
	}
	ToggleFavoriteByPath(m_favorite_maps[index].path);
}

void WelcomeDialog::OnClientSelected(wxCommandEvent& WXUNUSED(event)) {
	SetSelectedClientIndex(m_client_list->GetSelection(), true);
}

void WelcomeDialog::OnForceLoadChanged(wxCommandEvent& WXUNUSED(event)) {
	RefreshFooterState();
}

void WelcomeDialog::OnSize(wxSizeEvent& event) {
	UpdateStatusWrap();
	event.Skip();
}

void WelcomeDialog::RefreshRecentMapList() {
	std::vector<StartupListItem> items;
	items.reserve(m_recent_maps.size());
	for (const auto& map_entry : m_recent_maps) {
		const wxFileName filename(map_entry.path);
		items.push_back({
			filename.GetFullName(),
			filename.GetPath(),
			map_entry.modified_label,
			std::string(ICON_FILE),
			wxNullColour,
			true,
			map_entry.is_favorite,
		});
	}

	m_recent_list->SetItems(std::move(items));
	if (!m_selection_in_favorites && m_selected_map_index != wxNOT_FOUND && m_selected_map_index < static_cast<int>(m_recent_maps.size())) {
		m_recent_list->SetSelection(m_selected_map_index);
	} else {
		m_recent_list->SetSelection(wxNOT_FOUND);
	}
}

void WelcomeDialog::RefreshFavoriteMapList() {
	if (!m_favorite_list) {
		return;
	}
	std::vector<StartupListItem> items;
	items.reserve(m_favorite_maps.size());
	const wxColour favorite_colour(214, 170, 46);
	for (const auto& map_entry : m_favorite_maps) {
		const wxFileName filename(map_entry.path);
		items.push_back({
			filename.GetFullName(),
			filename.GetPath(),
			map_entry.modified_label,
			std::string(ICON_FILE),
			favorite_colour,
			true,
			true,
		});
	}

	m_favorite_list->SetItems(std::move(items));
	if (m_selection_in_favorites && m_selected_favorite_index != wxNOT_FOUND && m_selected_favorite_index < static_cast<int>(m_favorite_maps.size())) {
		m_favorite_list->SetSelection(m_selected_favorite_index);
	} else {
		m_favorite_list->SetSelection(wxNOT_FOUND);
	}
}

void WelcomeDialog::RefreshClientList() {
	std::vector<StartupListItem> items;
	items.reserve(m_configured_clients.size());
	for (const auto& client_entry : m_configured_clients) {
		items.push_back({
			client_entry.name,
			FormatStartupClientPath(client_entry.client_path),
			wxString(),
			std::string(ICON_HARD_DRIVE),
		});
	}

	m_client_list->SetItems(std::move(items));
	if (m_selected_client_index != wxNOT_FOUND && m_selected_client_index < static_cast<int>(m_configured_clients.size())) {
		m_client_list->SetSelection(m_selected_client_index);
	}
}

void WelcomeDialog::RefreshFooterState() {
	const StartupCompatibilityStatus status = GetCompatibilityStatus();
	m_status_text->SetLabel(BuildCompatibilityMessage());
	m_status_text->SetForegroundColour(StartupStatusColour(status));

	const bool mismatch = status == StartupCompatibilityStatus::ForceRequired || status == StartupCompatibilityStatus::Forced;
	if (!mismatch && m_force_load_checkbox->GetValue()) {
		m_force_load_checkbox->SetValue(false);
	}
	m_force_load_checkbox->Enable(mismatch);

	const bool can_load = status == StartupCompatibilityStatus::Compatible || status == StartupCompatibilityStatus::Forced;
	m_load_button->Enable(can_load);

	UpdateInfoButtonPulse();

	Layout();
	UpdateStatusWrap();
	Layout();
}

void WelcomeDialog::UpdateInfoButtonPulse() {
	if (!m_info_button) {
		return;
	}

	const bool has_selection = GetSelectedMapInfo() != nullptr;
	m_info_button->Enable(has_selection);

	if (has_selection) {
		if (!m_info_pulse_timer.IsRunning()) {
			m_info_pulse_phase = 0;
			m_info_pulse_timer.Start(500);
		}
	} else {
		if (m_info_pulse_timer.IsRunning()) {
			m_info_pulse_timer.Stop();
		}
		m_info_pulse_phase = 0;
		m_info_button->SetVariant(StartupButtonVariant::Secondary);
	}
}

wxString WelcomeDialog::GetSelectedMapPath() const {
	if (m_selection_in_favorites) {
		if (m_selected_favorite_index != wxNOT_FOUND && m_selected_favorite_index < static_cast<int>(m_favorite_maps.size())) {
			return m_favorite_maps[m_selected_favorite_index].path;
		}
	} else {
		if (m_selected_map_index != wxNOT_FOUND && m_selected_map_index < static_cast<int>(m_recent_maps.size())) {
			return m_recent_maps[m_selected_map_index].path;
		}
	}
	return wxEmptyString;
}

void WelcomeDialog::UpdateStatusWrap() {
	if (!m_status_text) {
		return;
	}

	const int wrap_width = m_status_card != nullptr ?
		std::max(m_status_card->GetClientSize().GetWidth() - FromDIP(20), FromDIP(220)) :
		std::max(GetClientSize().GetWidth() - FromDIP(320), FromDIP(240));
	m_status_text->Wrap(wrap_width);
}

void WelcomeDialog::SetSelectedMapIndex(int index) {
	if (index < 0 || index >= static_cast<int>(m_recent_maps.size())) {
		m_selected_map_index = wxNOT_FOUND;
		m_recent_list->SetSelection(wxNOT_FOUND);
	} else {
		m_selected_map_index = index;
		m_selection_in_favorites = false;
		m_selected_favorite_index = wxNOT_FOUND;
		if (m_favorite_list) {
			m_favorite_list->SetSelection(wxNOT_FOUND);
		}
		m_recent_list->SetSelection(index);
		EnsurePeeked(m_recent_maps[index].path);
	}

	AutoSelectMatchingClient();
	RefreshFooterState();
}

void WelcomeDialog::SetSelectedFavoriteIndex(int index) {
	if (index < 0 || index >= static_cast<int>(m_favorite_maps.size())) {
		m_selected_favorite_index = wxNOT_FOUND;
		if (m_favorite_list) {
			m_favorite_list->SetSelection(wxNOT_FOUND);
		}
	} else {
		m_selected_favorite_index = index;
		m_selection_in_favorites = true;
		m_selected_map_index = wxNOT_FOUND;
		if (m_recent_list) {
			m_recent_list->SetSelection(wxNOT_FOUND);
		}
		if (m_favorite_list) {
			m_favorite_list->SetSelection(index);
		}
		EnsurePeeked(m_favorite_maps[index].path);
	}

	AutoSelectMatchingClient();
	RefreshFooterState();
}

void WelcomeDialog::SetSelectedClientIndex(int index, bool manual_selection) {
	if (index < 0 || index >= static_cast<int>(m_configured_clients.size())) {
		m_selected_client_index = wxNOT_FOUND;
		m_client_list->SetSelection(wxNOT_FOUND);
	} else {
		m_selected_client_index = index;
		m_client_list->SetSelection(index);
	}

	m_has_manual_client_selection = manual_selection;
	RefreshFooterState();
}

void WelcomeDialog::AutoSelectMatchingClient() {
	if (m_has_manual_client_selection || m_selected_map_index == wxNOT_FOUND) {
		return;
	}

	const OTBMStartupPeekResult* info = GetSelectedMapInfo();
	if (!info || info->has_error) {
		return;
	}

	auto exact_match = std::ranges::find_if(m_configured_clients, [info](const auto& client_entry) {
		return client_entry.client->getOtbMajor() == info->items_major_version && client_entry.client->getOtbId() == info->items_minor_version;
	});
	if (exact_match == m_configured_clients.end()) {
		exact_match = std::ranges::find_if(m_configured_clients, [info](const auto& client_entry) {
			return client_entry.client->getOtbId() == info->items_minor_version;
		});
	}

	if (exact_match != m_configured_clients.end()) {
		const int new_index = static_cast<int>(std::distance(m_configured_clients.begin(), exact_match));
		if (new_index != m_selected_client_index) {
			m_selected_client_index = new_index;
			m_client_list->SetSelection(new_index);
		}
	} else if (m_selected_client_index == wxNOT_FOUND && !m_configured_clients.empty()) {
		m_selected_client_index = 0;
		m_client_list->SetSelection(0);
	}
}

bool WelcomeDialog::AttemptLoad(bool show_message) {
	const bool has_selection = m_selection_in_favorites
		? (m_selected_favorite_index != wxNOT_FOUND)
		: (m_selected_map_index != wxNOT_FOUND);
	if (!has_selection) {
		if (show_message) {
			wxMessageBox("Select a map before loading.", "Load Map", wxOK | wxICON_INFORMATION, this);
		}
		return false;
	}

	ClientVersion* selected_client = GetSelectedClient();
	if (!selected_client) {
		if (show_message) {
			wxMessageBox("Select a configured client before loading.", "Load Map", wxOK | wxICON_INFORMATION, this);
		}
		return false;
	}

	const OTBMStartupPeekResult* map_info = GetSelectedMapInfo();
	if (!map_info || map_info->has_error) {
		if (show_message) {
			const wxString error_message = (map_info != nullptr && !map_info->error_message.empty()) ? map_info->error_message : wxString("The selected map could not be read.");
			wxMessageBox(error_message, "Load Map", wxOK | wxICON_WARNING, this);
		}
		return false;
	}

	const StartupCompatibilityStatus status = GetCompatibilityStatus();
	if (status == StartupCompatibilityStatus::ForceRequired) {
		if (show_message) {
			wxMessageBox("The selected client does not match the map items version. Enable Force Load to continue.", "Client Mismatch", wxOK | wxICON_WARNING, this);
		}
		return false;
	}

	const wxString selected_path = m_selection_in_favorites
		? m_favorite_maps[m_selected_favorite_index].path
		: m_recent_maps[m_selected_map_index].path;

	m_pending_load_request = StartupLoadRequest {
		selected_path,
		MapLoadOptions {
			.selected_client_id = selected_client->getID(),
			.force_client_mismatch = status == StartupCompatibilityStatus::Forced,
		},
	};

	auto* open_event = new wxCommandEvent(WELCOME_DIALOG_ACTION, wxID_OPEN);
	open_event->SetString(m_pending_load_request->map_path);
	QueueEvent(open_event);
	return true;
}

bool WelcomeDialog::AddBrowsedMap(const wxString& path) {
	const auto favorite_it = std::ranges::find_if(m_favorite_maps, [&path](const auto& entry) {
		return PathsEqual(entry.path, path);
	});
	if (favorite_it != m_favorite_maps.end()) {
		SetSelectedFavoriteIndex(static_cast<int>(std::distance(m_favorite_maps.begin(), favorite_it)));
		return true;
	}

	const int existing_index = FindRecentMapIndex(path);
	if (existing_index != wxNOT_FOUND) {
		SetSelectedMapIndex(existing_index);
		return true;
	}

	std::erase_if(m_recent_maps, [](const auto& map_entry) {
		return map_entry.ephemeral;
	});

	m_recent_maps.insert(m_recent_maps.begin(), StartupRecentMapEntry {
		path,
		FormatModifiedLabel(path),
		true,
		false,
	});
	RefreshRecentMapList();
	SetSelectedMapIndex(0);
	return true;
}

void WelcomeDialog::EnsurePeeked(const wxString& path) {
	const std::string key = NormalizePathKey(path);
	if (m_peek_cache.contains(key)) {
		return;
	}

	OTBMStartupPeekResult peek_result;
	if (!IOMapOTBM::peekStartupInfo(FileName(path), peek_result) && !peek_result.has_error) {
		peek_result.has_error = true;
		peek_result.error_message = "Could not read the selected map.";
	}
	m_peek_cache.emplace(key, std::move(peek_result));
}

const OTBMStartupPeekResult* WelcomeDialog::GetSelectedMapInfo() const {
	wxString path;
	if (m_selection_in_favorites) {
		if (m_selected_favorite_index == wxNOT_FOUND || m_selected_favorite_index >= static_cast<int>(m_favorite_maps.size())) {
			return nullptr;
		}
		path = m_favorite_maps[m_selected_favorite_index].path;
	} else {
		if (m_selected_map_index == wxNOT_FOUND || m_selected_map_index >= static_cast<int>(m_recent_maps.size())) {
			return nullptr;
		}
		path = m_recent_maps[m_selected_map_index].path;
	}

	const auto key = NormalizePathKey(path);
	const auto it = m_peek_cache.find(key);
	return it != m_peek_cache.end() ? &it->second : nullptr;
}

ClientVersion* WelcomeDialog::GetSelectedClient() const {
	if (m_selected_client_index == wxNOT_FOUND || m_selected_client_index >= static_cast<int>(m_configured_clients.size())) {
		return nullptr;
	}
	return m_configured_clients[m_selected_client_index].client;
}

StartupCompatibilityStatus WelcomeDialog::GetCompatibilityStatus() const {
	const OTBMStartupPeekResult* map_info = GetSelectedMapInfo();
	ClientVersion* client = GetSelectedClient();

	if (!map_info || !client) {
		return StartupCompatibilityStatus::MissingSelection;
	}
	if (map_info->has_error) {
		return StartupCompatibilityStatus::MapError;
	}

	const bool matches = client->getOtbMajor() == map_info->items_major_version && client->getOtbId() == map_info->items_minor_version;
	if (matches) {
		return StartupCompatibilityStatus::Compatible;
	}
	return m_force_load_checkbox->GetValue() ? StartupCompatibilityStatus::Forced : StartupCompatibilityStatus::ForceRequired;
}

wxString WelcomeDialog::BuildCompatibilityMessage() const {
	const OTBMStartupPeekResult* map_info = GetSelectedMapInfo();
	ClientVersion* client = GetSelectedClient();

	if (m_recent_maps.empty() && m_favorite_maps.empty()) {
		return "No recent maps yet. Browse for a map or start a new one.";
	}
	if (!map_info) {
		return "Select a map to preview its header information.";
	}
	if (map_info->has_error) {
		return "The selected map could not be previewed. Loading is disabled until a valid OTBM is selected.";
	}
	if (!client) {
		return "Select a configured client to compare against the map header.";
	}

	switch (GetCompatibilityStatus()) {
		case StartupCompatibilityStatus::Compatible:
			return "Selected client matches the map items version. Load is ready.";
		case StartupCompatibilityStatus::Forced:
			return "Client mismatch is being ignored. The map will load with the selected client assets.";
		case StartupCompatibilityStatus::ForceRequired:
			return wxstr(std::format(
				"Map header expects items {}.{} while the selected client provides {}.{}. Enable Force Load to continue anyway.",
				map_info->items_major_version,
				map_info->items_minor_version,
				client->getOtbMajor(),
				client->getOtbId()
			));
		case StartupCompatibilityStatus::MapError:
			return "The selected map could not be previewed. Loading is disabled until a valid OTBM is selected.";
		case StartupCompatibilityStatus::MissingSelection:
		default:
			return "Select both a map and a configured client to continue.";
	}
}

int WelcomeDialog::FindRecentMapIndex(const wxString& path) const {
	const std::string normalized_path = NormalizePathKey(path);
	for (size_t index = 0; index < m_recent_maps.size(); ++index) {
		if (NormalizePathKey(m_recent_maps[index].path) == normalized_path) {
			return static_cast<int>(index);
		}
	}
	return wxNOT_FOUND;
}

wxString WelcomeDialog::FormatModifiedLabel(const wxString& path) {
	wxFileName filename(path);
	wxDateTime modified_time;
	return filename.GetTimes(nullptr, &modified_time, nullptr) ? modified_time.Format("%Y-%m-%d %H:%M") : wxString("Modified time unavailable");
}

std::string WelcomeDialog::NormalizePathKey(const wxString& path) {
	wxFileName filename(path);
	filename.Normalize(wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS | wxPATH_NORM_TILDE);
	return nstr(filename.GetFullPath());
}

std::vector<wxString> WelcomeDialog::LoadFavoritesFromSettings() const {
	std::vector<wxString> favorites;
	const std::string raw = g_settings.getString(Config::FAVORITE_FILES);
	std::istringstream stream(raw);
	std::string line;
	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (line.empty()) {
			continue;
		}
		wxString path = wxString::FromUTF8(line);
		if (std::ranges::any_of(favorites, [&path](const wxString& existing) { return PathsEqual(existing, path); })) {
			continue;
		}
		favorites.push_back(path);
	}
	return favorites;
}

void WelcomeDialog::SaveFavoritesToSettings() const {
	std::ostringstream stream;
	for (size_t i = 0; i < m_favorite_maps.size(); ++i) {
		if (i > 0) {
			stream << "\n";
		}
		stream << m_favorite_maps[i].path.ToStdString(wxConvUTF8);
	}
	g_settings.setString(Config::FAVORITE_FILES, stream.str());
	g_settings.save();
}

void WelcomeDialog::ToggleFavoriteByPath(const wxString& path) {
	const auto favorite_it = std::ranges::find_if(m_favorite_maps, [&path](const auto& entry) {
		return PathsEqual(entry.path, path);
	});

	if (favorite_it != m_favorite_maps.end()) {
		StartupRecentMapEntry moved = *favorite_it;
		moved.is_favorite = false;
		m_favorite_maps.erase(favorite_it);

		const auto existing_recent = std::ranges::find_if(m_recent_maps, [&path](const auto& entry) {
			return PathsEqual(entry.path, path);
		});
		if (existing_recent == m_recent_maps.end()) {
			m_recent_maps.insert(m_recent_maps.begin(), std::move(moved));
		}
	} else {
		const auto recent_it = std::ranges::find_if(m_recent_maps, [&path](const auto& entry) {
			return PathsEqual(entry.path, path);
		});
		StartupRecentMapEntry moved;
		if (recent_it != m_recent_maps.end()) {
			moved = *recent_it;
			m_recent_maps.erase(recent_it);
		} else {
			moved = { path, FormatModifiedLabel(path), false, true };
		}
		moved.is_favorite = true;
		m_favorite_maps.insert(m_favorite_maps.begin(), std::move(moved));
	}

	SaveFavoritesToSettings();

	m_selection_in_favorites = false;
	m_selected_map_index = wxNOT_FOUND;
	m_selected_favorite_index = wxNOT_FOUND;

	RefreshRecentMapList();
	RefreshFavoriteMapList();

	if (!m_favorite_maps.empty()) {
		SetSelectedFavoriteIndex(0);
	} else if (!m_recent_maps.empty()) {
		SetSelectedMapIndex(0);
	} else {
		RefreshFooterState();
	}
}
