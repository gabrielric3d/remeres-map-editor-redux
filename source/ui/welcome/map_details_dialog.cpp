#include "ui/welcome/map_details_dialog.h"

#include "app/client_version.h"
#include "ui/theme.h"
#include "ui/welcome/startup_card.h"
#include "ui/welcome/startup_formatters.h"
#include "ui/welcome/startup_info_panel.h"

MapDetailsDialog::MapDetailsDialog(wxWindow* parent, const wxString& map_path, const OTBMStartupPeekResult* map_info, ClientVersion* client) :
	wxDialog(parent, wxID_ANY, "Map & Client Details", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	SetBackgroundColour(Theme::Get(Theme::Role::PanelBackground));
	SetForegroundColour(Theme::Get(Theme::Role::Text));
	SetMinSize(FromDIP(wxSize(720, 520)));

	auto* root_sizer = new wxBoxSizer(wxVERTICAL);

	if (!map_path.empty()) {
		auto* header = new wxStaticText(this, wxID_ANY, map_path);
		header->SetFont(Theme::GetFont(10, true));
		header->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
		header->SetBackgroundColour(GetBackgroundColour());
		root_sizer->Add(header, 0, wxALL, FromDIP(12));
	}

	auto* content_panel = new wxPanel(this, wxID_ANY);
	content_panel->SetBackgroundColour(Theme::Get(Theme::Role::PanelBackground));
	auto* content_sizer = new wxBoxSizer(wxHORIZONTAL);

	auto* map_card = new StartupCardPanel(content_panel, "Selected Map Info");
	auto* map_info_panel = new StartupInfoPanel(map_card);
	map_info_panel->SetFields(BuildStartupMapFields(map_info));
	map_card->GetBodySizer()->Add(map_info_panel, 1, wxEXPAND);
	content_sizer->Add(map_card, 1, wxEXPAND | wxALL, FromDIP(8));

	auto* client_card = new StartupCardPanel(content_panel, "Client Information");
	auto* client_info_panel = new StartupInfoPanel(client_card);
	client_info_panel->SetFields(BuildStartupClientFields(client));
	client_card->GetBodySizer()->Add(client_info_panel, 1, wxEXPAND);
	content_sizer->Add(client_card, 1, wxEXPAND | wxALL, FromDIP(8));

	content_panel->SetSizer(content_sizer);
	root_sizer->Add(content_panel, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));

	auto* footer_sizer = new wxBoxSizer(wxHORIZONTAL);
	footer_sizer->AddStretchSpacer();
	auto* close_button = new wxButton(this, wxID_CLOSE, "Close");
	close_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); });
	footer_sizer->Add(close_button, 0, wxALL, FromDIP(12));
	root_sizer->Add(footer_sizer, 0, wxEXPAND);

	SetSizer(root_sizer);
	SetSize(FromDIP(wxSize(820, 560)));
	Centre();

	Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
		if (event.GetKeyCode() == WXK_ESCAPE) {
			EndModal(wxID_CLOSE);
		} else {
			event.Skip();
		}
	});
}
