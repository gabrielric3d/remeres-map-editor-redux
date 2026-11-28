//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/claude_panel.h"

#include "ai/assistant_live.h"
#include "ai/claude_code_backend.h"
#include "ai/external_mcp.h"
#include "app/settings.h"
#include "rendering/io/map_recorder.h"
#include "editor/hotkey_manager.h"
#include "lua/lua_script_manager.h"
#include "ui/gui.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <iterator>
#include <wx/base64.h>
#include <wx/choice.h>
#include <wx/clipbrd.h>
#include <wx/datetime.h>
#include <wx/dnd.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/mstream.h>
#include <wx/textdlg.h>
#include <wx/bmpbuttn.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/treectrl.h>
#include <wx/valtext.h>
#include <wx/wrapsizer.h>

namespace {
	struct ModelOption {
		const char* label;
		const char* id;
	};
	const ModelOption MODELS[] = {
		{ "Claude Opus 5.5", "claude-opus-5-5" },
		{ "Claude Opus 5", "claude-opus-5" },
		{ "Claude Sonnet 5", "claude-sonnet-5" },
		{ "Claude Fable 5.1", "claude-fable-5-1" },
	};
	const char* EFFORTS[] = { "low", "medium", "high", "xhigh", "max" };
	struct BackendOption {
		const char* label;
		const char* id;
	};
	const BackendOption BACKENDS[] = {
		{ "Subscription (Claude Code)", "claude_code" },
		{ "API key (pay per token)", "api" },
	};
	struct SpeedOption {
		const char* label;
		int tiles_per_second;
		int max_ms; // a bigger edit speeds up to end within this
	};
	const SpeedOption SPEEDS[] = {
		{ "Slow", 12, 25000 },
		{ "Normal", 30, 12000 },
		{ "Fast", 100, 5000 },
	};
	struct RecordSpeedOption {
		const char* label;
		int speed; // times faster than real time
	};
	const RecordSpeedOption RECORD_SPEEDS[] = {
		{ "1x", 1 },
		{ "2x", 2 },
		{ "4x", 4 },
		{ "8x", 8 },
	};

	wxString formatDuration(double seconds) {
		const int total = static_cast<int>(seconds + 0.5);
		return wxString::Format("%d:%02d", total / 60, total % 60);
	}

	// The message box. Typing must never reach the editor's single-key menu
	// accelerators (A = automagic, D = doodad...), and the keys must go through
	// Windows' normal text path so dead keys and AltGr compose (á ã â ç ? / ...).
	// Inserting the letters by hand (the old way) dropped every accent.
	class ChatInput : public wxTextCtrl {
	public:
		using wxTextCtrl::wxTextCtrl;
#ifdef __WXMSW__
		bool MSWShouldPreProcessMessage(WXMSG* msg) override {
			if (msg->message == WM_KEYDOWN) {
				const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
				const bool alt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
				const bool function_key = msg->wParam == VK_ESCAPE || (msg->wParam >= VK_F1 && msg->wParam <= VK_F24);
				// Plain keys and AltGr (= Ctrl+Alt) are text: no accelerators.
				if (!function_key && ctrl == alt) {
					return false;
				}
			}
			return wxTextCtrl::MSWShouldPreProcessMessage(msg);
		}
#endif
	};

	// Image files dropped on the chat are attached to the next message.
	class ImageDropTarget : public wxFileDropTarget {
	public:
		explicit ImageDropTarget(ClaudePanel* panel) :
			panel(panel) { }
		bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& files) override {
			return panel->AttachFiles(files) > 0;
		}

	private:
		ClaudePanel* panel;
	};

	wxString formatTokens(long long n) {
		if (n >= 1'000'000) {
			return wxString::Format("%.1fM", n / 1'000'000.0);
		}
		if (n >= 1'000) {
			return wxString::Format("%.1fk", n / 1'000.0);
		}
		return wxString::Format("%lld", n);
	}
}

ClaudePanel::ClaudePanel(wxWindow* parent) :
	wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL) {
	BuildUi();
	LoadKnowledge();
	UpdateAttachments();
	CreateBackend();
	UpdateStatus();
	if (!ExternalMcp::applySettings()) {
		AppendLine("MCP server: " + wxString::FromUTF8(ExternalMcp::lastError()), Theme::Get(Theme::Role::Error));
	}
	UpdateMcpStatus();
}

ClaudePanel::~ClaudePanel() {
	MapRecorder::clearStatusCallback();
	if (mcp_port) {
		mcp_port->Unbind(wxEVT_CHAR_HOOK, &ClaudePanel::OnMcpPortKey, this);
		mcp_port->Unbind(wxEVT_KILL_FOCUS, &ClaudePanel::OnMcpPortKillFocus, this);
	}
	if (backend) {
		backend->setListener(nullptr);
		backend->cancel();
	}
}

void ClaudePanel::CreateBackend() {
	if (backend) {
		backend->setListener(nullptr);
		backend->reset();
		backend.reset();
		api_agent = nullptr;
	}
	const int bi = std::clamp(backend_choice->GetSelection(), 0, static_cast<int>(std::size(BACKENDS)) - 1);
	const std::string id = BACKENDS[bi].id;
	g_settings.setString(Config::CLAUDE_BACKEND, id);
	if (id == "api") {
		auto api = std::make_unique<ClaudeAgent>();
		api_agent = api.get();
		backend = std::move(api);
	} else {
		backend = std::make_unique<ClaudeCodeBackend>();
	}
	backend->setListener(this);
	api_key_button->Show(api_agent != nullptr);
	LoadApiKey();
	ApplySettingsToAgent();
	rate_limits.clear();
	UpdatePlanUsage();
	Layout();
}

void ClaudePanel::BuildUi() {
	SetBackgroundColour(Theme::Get(Theme::Role::Surface));
	auto* sizer = new wxBoxSizer(wxVERTICAL);
	const wxColour subtle = Theme::Get(Theme::Role::TextSubtle);
	wxFont small_font = GetFont();
	small_font.SetFractionalPointSize(small_font.GetFractionalPointSize() * 0.9);
	const int gap = FromDIP(4);

	// ---- header: model, effort and the settings toggle -------------------------
	// Everything else lives in the collapsible settings block, so the chat keeps
	// the height and nothing here forces the pane wider than it is.
	auto* top = new wxBoxSizer(wxHORIZONTAL);
	wxArrayString model_labels;
	for (const auto& m : MODELS) {
		model_labels.Add(m.label);
	}
	model_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, model_labels);
	model_choice->SetMinSize(FromDIP(wxSize(70, -1)));
	const std::string saved_model = g_settings.getString(Config::CLAUDE_MODEL);
	int model_index = 0;
	for (size_t i = 0; i < std::size(MODELS); ++i) {
		if (saved_model == MODELS[i].id) {
			model_index = static_cast<int>(i);
		}
	}
	model_choice->SetSelection(model_index);
	model_choice->SetToolTip("Model. Opus 5.5 is the newest Opus and cheaper than Opus 5; Sonnet 5 is cheaper still, Fable 5.1 is the strongest and most expensive.");
	top->Add(model_choice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);

	wxArrayString effort_labels;
	for (const char* e : EFFORTS) {
		effort_labels.Add(e);
	}
	effort_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, effort_labels);
	effort_choice->SetMinSize(FromDIP(wxSize(56, -1)));
	const std::string saved_effort = g_settings.getString(Config::CLAUDE_EFFORT);
	int effort_index = 2;
	for (size_t i = 0; i < std::size(EFFORTS); ++i) {
		if (saved_effort == EFFORTS[i]) {
			effort_index = static_cast<int>(i);
		}
	}
	effort_choice->SetSelection(effort_index);
	effort_choice->SetToolTip("Effort: how hard the model thinks. high is a good default; xhigh/max for big builds.");
	top->Add(effort_choice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);

	settings_button = new wxButton(this, wxID_ANY, wxString::FromUTF8("Settings \xE2\x96\xBE"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	settings_button->SetToolTip("Backend (subscription / API key), live view, MCP server and time-lapse recording");
	knowledge_button = new wxButton(this, wxID_ANY, wxString::FromUTF8("Knowledge \xE2\x96\xBE"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	knowledge_button->SetToolTip("The guides Claude works from (borders, clusters, structure, the Lua API...) and the rules inside each one. A check mark = Claude opened it in this conversation.");
	top->Add(knowledge_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
	top->Add(settings_button, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(top, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

	// ---- status: state + tokens, then the subscription windows ------------------
	// Ellipsized and not auto-sized: a long line never widens the pane.
	status = new wxStaticText(this, wxID_ANY, "Ready", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
	status->SetFont(small_font);
	status->SetForegroundColour(subtle);
	sizer->Add(status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
	plan_usage = new wxStaticText(this, wxID_ANY, "Plan", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
	plan_usage->SetFont(small_font);
	plan_usage->SetForegroundColour(subtle);
	plan_usage->SetToolTip("Subscription usage windows reported by Claude Code (same as /usage): the 5-hour session window and the weekly window, with the time they reset.");
	sizer->Add(plan_usage, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));

	// ---- settings (collapsed by default); every row wraps to the pane width -----
	settings_box = new wxBoxSizer(wxVERTICAL);
	const auto add_label = [&](wxSizer* row, const wxString& text) {
		auto* label = new wxStaticText(this, wxID_ANY, text);
		label->SetForegroundColour(Theme::Get(Theme::Role::Text));
		label->SetFont(GetFont().Bold());
		row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(6));
	};

	auto* engine = new wxWrapSizer(wxHORIZONTAL);
	add_label(engine, "Engine");
	wxArrayString backend_labels;
	for (const auto& b : BACKENDS) {
		backend_labels.Add(b.label);
	}
	backend_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, backend_labels);
	const std::string saved_backend = g_settings.getString(Config::CLAUDE_BACKEND);
	int backend_index = 0;
	for (size_t i = 0; i < std::size(BACKENDS); ++i) {
		if (saved_backend == BACKENDS[i].id) {
			backend_index = static_cast<int>(i);
		}
	}
	backend_choice->SetSelection(backend_index);
	backend_choice->SetToolTip("Subscription uses the claude program installed on this PC and your Claude plan (Pro/Max). API key calls the Anthropic API directly and is billed per token.");
	engine->Add(backend_choice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, gap);
	api_key_button = new wxButton(this, wxID_ANY, "API key", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	engine->Add(api_key_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, gap);
	show_thinking = new wxCheckBox(this, wxID_ANY, "Show thinking");
	show_thinking->SetValue(g_settings.getBoolean(Config::CLAUDE_SHOW_THINKING));
	show_thinking->SetForegroundColour(subtle);
	engine->Add(show_thinking, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, gap);
	settings_box->Add(engine, 0, wxEXPAND);

	// Live view: how Claude's work shows up on the map, whoever drives it.
	auto* live = new wxWrapSizer(wxHORIZONTAL);
	add_label(live, "Live");
	live_animate = new wxCheckBox(this, wxID_ANY, "Animate");
	live_animate->SetValue(g_settings.getBoolean(Config::CLAUDE_LIVE_ANIMATE));
	live_animate->SetToolTip("Lay paint down a few tiles per frame so you can watch Claude work. Each paint is still one undo step.");
	live_animate->SetForegroundColour(subtle);
	live->Add(live_animate, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, gap);
	wxArrayString speed_labels;
	for (const auto& s : SPEEDS) {
		speed_labels.Add(s.label);
	}
	live_speed = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, speed_labels);
	const int saved_rate = g_settings.getInteger(Config::CLAUDE_LIVE_RATE);
	int speed_index = 1;
	for (size_t i = 0; i < std::size(SPEEDS); ++i) {
		if (std::abs(SPEEDS[i].tiles_per_second - saved_rate) < std::abs(SPEEDS[speed_index].tiles_per_second - saved_rate)) {
			speed_index = static_cast<int>(i);
		}
	}
	live_speed->SetSelection(speed_index);
	live_speed->Enable(live_animate->GetValue());
	live_speed->SetToolTip("How fast Claude's edits appear: Slow 12, Normal 30, Fast 100 tiles per second (big edits speed up to end within 25 / 12 / 5 seconds).");
	live->Add(live_speed, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(8));
	live_follow = new wxCheckBox(this, wxID_ANY, "Follow");
	live_follow->SetValue(g_settings.getBoolean(Config::CLAUDE_LIVE_FOLLOW));
	live_follow->SetToolTip("Glide the camera to where Claude is working.");
	live_follow->SetForegroundColour(subtle);
	live->Add(live_follow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(8));
	live_cursor = new wxCheckBox(this, wxID_ANY, "Cursor");
	live_cursor->SetValue(g_settings.getBoolean(Config::CLAUDE_LIVE_CURSOR));
	live_cursor->SetToolTip("Show a \"Claude\" cursor and the area it is reading or changing.");
	live_cursor->SetForegroundColour(subtle);
	live->Add(live_cursor, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, gap);
	settings_box->Add(live, 0, wxEXPAND);

	// The editor tools for programs outside the editor.
	auto* mcp = new wxWrapSizer(wxHORIZONTAL);
	add_label(mcp, "MCP");
	mcp_external = new wxCheckBox(this, wxID_ANY, "Server on port");
	mcp_external->SetValue(g_settings.getBoolean(Config::CLAUDE_MCP_EXTERNAL));
	mcp_external->SetToolTip("Let other programs on this PC (Claude Code in a terminal or IDE, Claude Desktop) read and paint this map while you watch. Localhost only; clients must send the token that 'Copy setup' includes.");
	mcp_external->SetForegroundColour(subtle);
	mcp->Add(mcp_external, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, gap);
	mcp_port = new wxTextCtrl(this, wxID_ANY, wxString::Format("%d", g_settings.getInteger(Config::CLAUDE_MCP_PORT)), wxDefaultPosition, FromDIP(wxSize(56, -1)), wxTE_PROCESS_ENTER, wxTextValidator(wxFILTER_DIGITS));
	mcp_port->SetToolTip("Port on 127.0.0.1 (1024-65535). Changing it means registering the server again in the clients.");
	mcp->Add(mcp_port, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, gap);
	mcp_copy = new wxButton(this, wxID_ANY, "Copy setup", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	mcp_copy->SetToolTip("Copy the command that registers the editor in Claude Code (claude mcp add ...). Run it once in a terminal.");
	mcp->Add(mcp_copy, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(6));
	mcp_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
	mcp->Add(mcp_status, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, gap);
	settings_box->Add(mcp, 0, wxEXPAND);

	// Time-lapse of the map view.
	auto* record = new wxWrapSizer(wxHORIZONTAL);
	add_label(record, "Video");
	record_button = new wxButton(this, wxID_ANY, wxString::FromUTF8("\xE2\x97\x8F REC"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	record_button->SetToolTip("Record a time-lapse of the map view (.mp4): the camera, the mouse, Claude's cursor and every change. Only moments when something moves are recorded, so idle time drops out.");
	record->Add(record_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, gap);
	wxArrayString record_speed_labels;
	for (const auto& s : RECORD_SPEEDS) {
		record_speed_labels.Add(s.label);
	}
	record_speed = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, record_speed_labels);
	const int saved_record_speed = g_settings.getInteger(Config::RECORD_SPEED);
	int record_speed_index = 2;
	for (size_t i = 0; i < std::size(RECORD_SPEEDS); ++i) {
		if (RECORD_SPEEDS[i].speed == saved_record_speed) {
			record_speed_index = static_cast<int>(i);
		}
	}
	record_speed->SetSelection(record_speed_index);
	record_speed->SetToolTip("How many times faster than real time the video plays (counting only the moments something moved). Takes effect on the next recording.");
	record->Add(record_speed, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, gap);
	record_folder_button = new wxButton(this, wxID_ANY, "Folder", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	record_folder_button->SetToolTip("Open the folder where the videos are saved (the screenshot folder)");
	record->Add(record_folder_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(6));
	record_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
	record_status->SetForegroundColour(subtle);
	record->Add(record_status, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, gap);
	settings_box->Add(record, 0, wxEXPAND);

	sizer->Add(settings_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

	BuildKnowledgeBlock(sizer);

	// ---- transcript -------------------------------------------------------------
	transcript = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_AUTO_URL | wxTE_BESTWRAP | wxBORDER_NONE);
	transcript->SetBackgroundColour(Theme::Get(Theme::Role::Background));
	transcript->SetForegroundColour(Theme::Get(Theme::Role::Text));
	transcript->SetMinSize(FromDIP(wxSize(120, 80)));
	sizer->Add(transcript, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

	// ---- images waiting to go with the next message (thumbnails) ----------------
	attachment_row = new wxBoxSizer(wxVERTICAL);
	auto* attachment_head = new wxBoxSizer(wxHORIZONTAL);
	attachment_label = new wxStaticText(this, wxID_ANY, "images", wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
	attachment_label->SetFont(small_font);
	attachment_label->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	attachment_head->Add(attachment_label, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
	remove_attachments_button = new wxButton(this, wxID_ANY, "Remove all", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	remove_attachments_button->SetToolTip("Remove the attached images");
	attachment_head->Add(remove_attachments_button, 0, wxALIGN_CENTER_VERTICAL);
	attachment_row->Add(attachment_head, 0, wxEXPAND | wxBOTTOM, gap);
	attachment_strip = new wxWrapSizer(wxHORIZONTAL);
	attachment_row->Add(attachment_strip, 0, wxEXPAND);
	sizer->Add(attachment_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

	// ---- message box + buttons ----------------------------------------------------
	input = new ChatInput(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB);
	input->SetBackgroundColour(Theme::Get(Theme::Role::RaisedSurface));
	input->SetForegroundColour(Theme::Get(Theme::Role::Text));
	input->SetToolTip("Enter sends, Shift+Enter adds a line. You can write while Claude works.\nImages: the Image button, Ctrl+V, or drop files on the panel.");
	sizer->Add(input, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

	auto* buttons = new wxBoxSizer(wxHORIZONTAL);
	attach_button = new wxButton(this, wxID_ANY, "Image...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	attach_button->SetToolTip("Attach images to the next message (you can also paste with Ctrl+V or drop files on the panel)");
	buttons->Add(attach_button, 0, wxRIGHT, gap);
	clear_button = new wxButton(this, wxID_ANY, "New chat", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	clear_button->SetToolTip("Forget the conversation and start over (the map is not touched)");
	buttons->Add(clear_button, 0, wxRIGHT, gap);
	buttons->AddStretchSpacer();
	stop_button = new wxButton(this, wxID_ANY, "Stop", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	stop_button->Enable(false);
	buttons->Add(stop_button, 0, wxRIGHT, gap);
	send_button = new wxButton(this, wxID_ANY, "Send", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	send_button->SetDefault();
	buttons->Add(send_button, 0);
	sizer->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(6));

	SetSizer(sizer);
	ShowSettings(false);
	ShowKnowledge(false);
	FitInputHeight();

	settings_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		ShowSettings(!GetSizer()->IsShown(settings_box));
	});
	knowledge_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		const bool show = !GetSizer()->IsShown(knowledge_box);
		if (show) {
			LoadKnowledge(); // the guides may have been edited since
		}
		ShowKnowledge(show);
	});
	input->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
		FitInputHeight();
		event.Skip();
	});
	Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
		event.Skip();
		CallAfter([this]() {
			FitInputHeight();
		});
	});

	send_button->Bind(wxEVT_BUTTON, &ClaudePanel::OnSend, this);
	attach_button->Bind(wxEVT_BUTTON, &ClaudePanel::OnAttach, this);
	remove_attachments_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		attachments.clear();
		attachment_thumbs.clear();
		UpdateAttachments();
	});
	// Image files dropped on the panel or the message box get attached.
	SetDropTarget(new ImageDropTarget(this));
	input->SetDropTarget(new ImageDropTarget(this));
	transcript->SetDropTarget(new ImageDropTarget(this));
	stop_button->Bind(wxEVT_BUTTON, &ClaudePanel::OnStop, this);
	clear_button->Bind(wxEVT_BUTTON, &ClaudePanel::OnClear, this);
	api_key_button->Bind(wxEVT_BUTTON, &ClaudePanel::OnApiKey, this);
	backend_choice->Bind(wxEVT_CHOICE, &ClaudePanel::OnBackendChanged, this);
	model_choice->Bind(wxEVT_CHOICE, &ClaudePanel::OnModelChanged, this);
	effort_choice->Bind(wxEVT_CHOICE, &ClaudePanel::OnEffortChanged, this);
	show_thinking->Bind(wxEVT_CHECKBOX, &ClaudePanel::OnShowThinkingChanged, this);
	live_animate->Bind(wxEVT_CHECKBOX, &ClaudePanel::OnLiveChanged, this);
	live_speed->Bind(wxEVT_CHOICE, &ClaudePanel::OnLiveChanged, this);
	live_follow->Bind(wxEVT_CHECKBOX, &ClaudePanel::OnLiveChanged, this);
	live_cursor->Bind(wxEVT_CHECKBOX, &ClaudePanel::OnLiveChanged, this);
	mcp_external->Bind(wxEVT_CHECKBOX, &ClaudePanel::OnMcpToggled, this);
	mcp_copy->Bind(wxEVT_BUTTON, &ClaudePanel::OnMcpCopy, this);
	record_button->Bind(wxEVT_BUTTON, &ClaudePanel::OnRecord, this);
	record_speed->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
		const int i = std::clamp(record_speed->GetSelection(), 0, static_cast<int>(std::size(RECORD_SPEEDS)) - 1);
		g_settings.setInteger(Config::RECORD_SPEED, RECORD_SPEEDS[i].speed);
	});
	record_folder_button->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
		wxString dir = wxString::FromUTF8(g_settings.getString(Config::SCREENSHOT_DIRECTORY));
		if (!dir.empty()) {
			wxLaunchDefaultApplication(dir);
		}
	});
	// Member handlers (not lambdas) so the destructor can unbind them: a focus
	// event during teardown must not reach a half-destroyed panel.
	mcp_port->Bind(wxEVT_CHAR_HOOK, &ClaudePanel::OnMcpPortKey, this);
	mcp_port->Bind(wxEVT_KILL_FOCUS, &ClaudePanel::OnMcpPortKillFocus, this);
	// CHAR_HOOK runs before the frame's accelerator table sees the key; KEY_DOWN is
	// kept as a fallback for builds/platforms where the hook is not delivered.
	input->Bind(wxEVT_CHAR_HOOK, &ClaudePanel::OnInputKeyDown, this);
	input->Bind(wxEVT_KEY_DOWN, &ClaudePanel::OnInputKeyDown, this);
	// Make sure a click anywhere on the message box (or the panel background)
	// really lands the caret there, even if another window is fighting for focus.
	const auto focus_input = [this](wxMouseEvent& event) {
		if (input && wxWindow::FindFocus() != input) {
			input->SetFocus();
		}
		event.Skip();
	};
	input->Bind(wxEVT_LEFT_DOWN, focus_input);
	input->Bind(wxEVT_LEFT_UP, focus_input);
	Bind(wxEVT_LEFT_DOWN, focus_input);

	// Single-letter menu accelerators (A = automagic, D = doodad palette, ...) are
	// translated before any text control sees the key, so typing would trigger
	// them instead. Same treatment the palette search fields use.
	const auto suspend_hotkeys = [](wxFocusEvent& event) {
		spdlog::debug("ClaudePanel: text control focused, hotkeys off");
		g_hotkeys.DisableHotkeys();
		event.Skip();
	};
	const auto restore_hotkeys = [](wxFocusEvent& event) {
		spdlog::debug("ClaudePanel: text control lost focus, hotkeys on");
		g_hotkeys.EnableHotkeys();
		event.Skip();
	};
	input->Bind(wxEVT_SET_FOCUS, suspend_hotkeys);
	input->Bind(wxEVT_KILL_FOCUS, restore_hotkeys);
	transcript->Bind(wxEVT_SET_FOCUS, suspend_hotkeys);
	transcript->Bind(wxEVT_KILL_FOCUS, restore_hotkeys);
	mcp_port->Bind(wxEVT_SET_FOCUS, suspend_hotkeys);
	mcp_port->Bind(wxEVT_KILL_FOCUS, restore_hotkeys);

	AppendLine("Claude Assistant", Theme::Get(Theme::Role::Accent), true);
	AppendLine("Describe what you want on the map. Claude can read the map, paint with the editor's brushes, run Lua scripts and look at screenshots. Every change is a normal Undo.", Theme::Get(Theme::Role::TextSubtle));
	AppendLine("Enter sends, Shift+Enter adds a line. Attach images with Image..., Ctrl+V or by dropping files here.", Theme::Get(Theme::Role::TextSubtle));
	AppendLine("", Theme::Get(Theme::Role::Text));
}

void ClaudePanel::FocusInput() {
	if (input) {
		input->SetFocus();
	}
}

void ClaudePanel::ShowSettings(bool show) {
	if (!settings_box) {
		return;
	}
	GetSizer()->Show(settings_box, show, true);
	settings_button->SetLabel(wxString::FromUTF8(show ? "Settings \xE2\x96\xB4" : "Settings \xE2\x96\xBE"));
	Layout();
}

void ClaudePanel::FitInputHeight() {
	if (!input || IsBeingDeleted()) {
		return;
	}
	// GetNumberOfLines counts wrapped lines too, so a long paragraph grows the box.
	const int lines = std::clamp(input->GetNumberOfLines(), INPUT_MIN_LINES, INPUT_MAX_LINES);
	if (lines == input_lines) {
		return;
	}
	input_lines = lines;
	input->SetMinSize(wxSize(-1, lines * input->GetCharHeight() + FromDIP(10)));
	Layout();
}

// ---- knowledge ---------------------------------------------------------------

namespace {
	class KnowledgeItem : public wxTreeItemData {
	public:
		KnowledgeItem(int file, int section) :
			file(file), section(section) { }
		int file; // index in ClaudePanel::knowledge, -1 for a group node
		int section; // -1 = the file itself
	};

	wxString normalizedPath(const wxString& path, const wxString& base) {
		wxFileName fn(path);
		fn.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_LONG, base);
		wxString full = fn.GetFullPath();
#ifdef __WXMSW__
		full.MakeLower();
#endif
		return full;
	}
}

void ClaudePanel::BuildKnowledgeBlock(wxSizer* parent_sizer) {
	knowledge_box = new wxBoxSizer(wxVERTICAL);
	knowledge_tree = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 170)), wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_FULL_ROW_HIGHLIGHT | wxTR_NO_LINES | wxBORDER_NONE);
	knowledge_tree->SetBackgroundColour(Theme::Get(Theme::Role::Background));
	knowledge_tree->SetForegroundColour(Theme::Get(Theme::Role::Text));
	knowledge_box->Add(knowledge_tree, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

	knowledge_preview = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(-1, 140)), wxTE_MULTILINE | wxTE_READONLY | wxTE_BESTWRAP | wxBORDER_NONE);
	knowledge_preview->SetBackgroundColour(Theme::Get(Theme::Role::RaisedSurface));
	knowledge_preview->SetForegroundColour(Theme::Get(Theme::Role::Text));
	knowledge_box->Add(knowledge_preview, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

	auto* row = new wxBoxSizer(wxHORIZONTAL);
	auto* open_button = new wxButton(this, wxID_ANY, "Open file", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	open_button->SetToolTip("Open the selected guide in its editor (double-click does the same). Edits take effect on the next message / New chat.");
	row->Add(open_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	auto* folder_button = new wxButton(this, wxID_ANY, "Folder", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	folder_button->SetToolTip("Open the folder with the guides (scripts/claude_style). A new .md there shows up here.");
	row->Add(folder_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	auto* legend = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("\xE2\x9C\x93 = Claude read it in this chat"), wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END | wxST_NO_AUTORESIZE);
	legend->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	row->Add(legend, 1, wxALIGN_CENTER_VERTICAL);
	knowledge_box->Add(row, 0, wxEXPAND);
	parent_sizer->Add(knowledge_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

	const auto selected_file = [this]() -> int {
		const wxTreeItemId item = knowledge_tree->GetSelection();
		if (!item.IsOk()) {
			return -1;
		}
		const auto* data = static_cast<KnowledgeItem*>(knowledge_tree->GetItemData(item));
		return data ? data->file : -1;
	};
	const auto open_file = [this](int file) {
		if (file >= 0 && file < static_cast<int>(knowledge.size())) {
			wxLaunchDefaultApplication(knowledge[file].path);
		}
	};
	knowledge_tree->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent& event) {
		OnKnowledgeSelected();
		event.Skip();
	});
	knowledge_tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this, open_file](wxTreeEvent& event) {
		const auto* data = static_cast<KnowledgeItem*>(knowledge_tree->GetItemData(event.GetItem()));
		if (data && data->file >= 0) {
			open_file(data->file);
		} else {
			event.Skip(); // group node: expand/collapse
		}
	});
	open_button->Bind(wxEVT_BUTTON, [selected_file, open_file](wxCommandEvent&) {
		open_file(selected_file());
	});
	folder_button->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
		wxFileName dir(wxString::FromUTF8(g_luaScripts.getScriptsDirectory()), wxEmptyString);
		dir.AppendDir("claude_style");
		wxLaunchDefaultApplication(dir.DirExists() ? dir.GetPath() : wxString::FromUTF8(g_luaScripts.getScriptsDirectory()));
	});
}

void ClaudePanel::LoadKnowledge() {
	const wxString scripts = wxString::FromUTF8(g_luaScripts.getScriptsDirectory());
	const wxString sep = wxFileName::GetPathSeparator();

	// Keep the check marks of the guides Claude already read.
	std::vector<wxString> read_paths;
	for (const KnowledgeFile& file : knowledge) {
		if (file.read) {
			read_paths.push_back(normalizedPath(file.path, scripts));
		}
	}
	knowledge.clear();

	const auto add = [&](const wxString& path, const wxString& title_override, bool analysis) {
		wxFile f;
		if (!wxFileName::FileExists(path) || !f.Open(path)) {
			return;
		}
		wxString text;
		f.ReadAll(&text, wxConvUTF8);
		KnowledgeFile file;
		file.path = path;
		file.analysis = analysis;
		file.title = wxFileName(path).GetName();
		KnowledgeSection* current = nullptr;
		bool title_seen = false;
		for (wxString line : wxSplit(text, '\n', 0)) {
			line.Trim(true);
			if (!title_seen && line.StartsWith("# ")) {
				file.title = line.Mid(2).Trim(false);
				title_seen = true;
				continue;
			}
			if (line.StartsWith("## ")) {
				file.sections.push_back({ line.Mid(3).Trim(false), wxEmptyString });
				current = &file.sections.back();
				continue;
			}
			wxString& target = current ? current->body : file.intro;
			if (!target.empty() || !line.empty()) {
				target += line + "\n";
			}
		}
		if (!title_override.empty()) {
			file.title = title_override;
		}
		const wxString key = normalizedPath(path, scripts);
		file.read = std::find(read_paths.begin(), read_paths.end(), key) != read_paths.end();
		knowledge.push_back(std::move(file));
	};
	const auto add_dir = [&](const wxString& dir, bool analysis) {
		if (!wxDir::Exists(dir)) {
			return;
		}
		wxArrayString files;
		wxDir::GetAllFiles(dir, &files, "*.md", wxDIR_FILES);
		files.Sort();
		for (const wxString& path : files) {
			add(path, wxEmptyString, analysis);
		}
	};

	add(scripts + sep + "claude_style.md", "Guia principal (claude_style.md)", false);
	add_dir(scripts + sep + "claude_style", false);
	add(wxString::FromUTF8(ClaudeAgent::apiReferencePath()), "Lua API reference (README.md)", false);
	add_dir(scripts + sep + "claude_style" + sep + "analise", true);
	UpdateKnowledgeLabels();
}

void ClaudePanel::UpdateKnowledgeLabels() {
	if (!knowledge_tree) {
		return;
	}
	// Rebuilt whole: a handful of files, a few dozen sections.
	wxTreeItemId keep_selection;
	int selected_file = -2;
	int selected_section = -2;
	if (const wxTreeItemId sel = knowledge_tree->GetSelection(); sel.IsOk()) {
		if (const auto* data = static_cast<KnowledgeItem*>(knowledge_tree->GetItemData(sel))) {
			selected_file = data->file;
			selected_section = data->section;
		}
	}
	knowledge_tree->Freeze();
	knowledge_tree->DeleteAllItems();
	const wxTreeItemId root = knowledge_tree->AddRoot("knowledge");
	const wxString check = wxString::FromUTF8("\xE2\x9C\x93 ");
	wxTreeItemId analysis_group;
	int analysis_count = 0;
	for (size_t i = 0; i < knowledge.size(); ++i) {
		const KnowledgeFile& file = knowledge[i];
		const wxString label = (file.read ? check : wxString()) + file.title;
		wxTreeItemId item;
		if (file.analysis) {
			if (!analysis_group.IsOk()) {
				analysis_group = knowledge_tree->AppendItem(root, "Image analyses", -1, -1, new KnowledgeItem(-1, -1));
			}
			item = knowledge_tree->AppendItem(analysis_group, label, -1, -1, new KnowledgeItem(static_cast<int>(i), -1));
			++analysis_count;
		} else {
			item = knowledge_tree->AppendItem(root, label, -1, -1, new KnowledgeItem(static_cast<int>(i), -1));
			for (size_t s = 0; s < file.sections.size(); ++s) {
				const wxTreeItemId child = knowledge_tree->AppendItem(item, file.sections[s].title, -1, -1, new KnowledgeItem(static_cast<int>(i), static_cast<int>(s)));
				if (selected_file == static_cast<int>(i) && selected_section == static_cast<int>(s)) {
					keep_selection = child;
				}
			}
		}
		if (file.read) {
			knowledge_tree->SetItemBold(item, true);
		}
		if (selected_file == static_cast<int>(i) && selected_section == -1) {
			keep_selection = item;
		}
	}
	if (analysis_group.IsOk()) {
		knowledge_tree->SetItemText(analysis_group, wxString::Format("Image analyses (%d files)", analysis_count));
	}
	knowledge_tree->Thaw();
	if (keep_selection.IsOk()) {
		knowledge_tree->SelectItem(keep_selection);
		knowledge_tree->EnsureVisible(keep_selection);
	} else if (knowledge.empty()) {
		knowledge_preview->ChangeValue("No guides found. Put .md files in scripts/claude_style (and scripts/claude_style.md) and open Knowledge again.");
	} else {
		knowledge_preview->ChangeValue("Pick a guide or a rule to read it here.");
	}
}

void ClaudePanel::OnKnowledgeSelected() {
	const wxTreeItemId item = knowledge_tree->GetSelection();
	const auto* data = item.IsOk() ? static_cast<KnowledgeItem*>(knowledge_tree->GetItemData(item)) : nullptr;
	if (!data || data->file < 0 || data->file >= static_cast<int>(knowledge.size())) {
		knowledge_preview->ChangeValue("The notes Claude can search for examples, one file per batch of screenshots. Expand to pick one.");
		return;
	}
	const KnowledgeFile& file = knowledge[data->file];
	wxString text;
	if (data->section >= 0 && data->section < static_cast<int>(file.sections.size())) {
		const KnowledgeSection& section = file.sections[data->section];
		text = section.title + "\n\n" + section.body;
	} else {
		text = file.title + "\n" + file.path + "\n\n" + file.intro;
		if (!file.sections.empty()) {
			text += "\nRules in this guide:\n";
			for (const KnowledgeSection& section : file.sections) {
				text += wxString::FromUTF8("  \xE2\x80\xA2 ") + section.title + "\n";
			}
		}
	}
	constexpr size_t MAX_PREVIEW = 30000;
	if (text.length() > MAX_PREVIEW) {
		text = text.Left(MAX_PREVIEW) + "\n...";
	}
	knowledge_preview->ChangeValue(text);
	knowledge_preview->ShowPosition(0);
}

void ClaudePanel::ShowKnowledge(bool show) {
	if (!knowledge_box) {
		return;
	}
	GetSizer()->Show(knowledge_box, show, true);
	knowledge_button->SetLabel(wxString::FromUTF8(show ? "Knowledge \xE2\x96\xB4" : "Knowledge \xE2\x96\xBE"));
	Layout();
}

void ClaudePanel::MarkKnowledgeRead(const wxString& path) {
	if (path.empty()) {
		return;
	}
	const wxString scripts = wxString::FromUTF8(g_luaScripts.getScriptsDirectory());
	if (knowledge.empty()) {
		LoadKnowledge();
	}
	const wxString key = normalizedPath(path, scripts);
	for (KnowledgeFile& file : knowledge) {
		if (!file.read && normalizedPath(file.path, scripts) == key) {
			file.read = true;
			UpdateKnowledgeLabels();
			return;
		}
	}
}

void ClaudePanel::LoadApiKey() {
	std::string key = g_settings.getString(Config::CLAUDE_API_KEY);
	if (key.empty()) {
		if (const char* env = std::getenv("ANTHROPIC_API_KEY")) {
			key = env;
		}
	}
	if (api_agent) {
		api_agent->setApiKey(key);
	}
	api_key_button->SetLabel(key.empty() ? "API key..." : "API key (set)");
	api_key_button->SetToolTip(key.empty() ? "No key set. Click to paste your Anthropic API key." : "Key configured. Click to replace it.");
}

void ClaudePanel::ApplySettingsToAgent() {
	const int mi = std::clamp(model_choice->GetSelection(), 0, static_cast<int>(std::size(MODELS)) - 1);
	const int ei = std::clamp(effort_choice->GetSelection(), 0, static_cast<int>(std::size(EFFORTS)) - 1);
	if (backend) {
		backend->setModel(MODELS[mi].id);
		backend->setEffort(EFFORTS[ei]);
	}
	g_settings.setString(Config::CLAUDE_MODEL, MODELS[mi].id);
	g_settings.setString(Config::CLAUDE_EFFORT, EFFORTS[ei]);
}

void ClaudePanel::AppendStyled(const wxString& text, const wxColour& colour, bool bold, bool italic) {
	if (!transcript) {
		return;
	}
	wxFont font = transcript->GetFont();
	if (bold) {
		font.MakeBold();
	}
	if (italic) {
		font.MakeItalic();
	}
	transcript->SetDefaultStyle(wxTextAttr(colour, wxNullColour, font));
	transcript->AppendText(text);
	transcript->SetDefaultStyle(wxTextAttr(Theme::Get(Theme::Role::Text), wxNullColour, transcript->GetFont()));
	transcript->ShowPosition(transcript->GetLastPosition());
}

void ClaudePanel::AppendLine(const wxString& text, const wxColour& colour, bool bold) {
	AppendStyled(text + "\n", colour, bold, false);
}

void ClaudePanel::EnsureAssistantHeader() {
	if (!assistant_header_written) {
		AppendLine("Claude", Theme::Get(Theme::Role::Accent), true);
		assistant_header_written = true;
	}
}

void ClaudePanel::UpdateStatus() {
	if (!backend) {
		return;
	}
	const AssistantUsage& u = backend->getUsage();
	const wxString dot = wxString::FromUTF8("\xE2\x97\x8F ");
	wxString text;
	wxColour colour = Theme::Get(Theme::Role::TextSubtle);
	if (MapRecorder* recorder = MapRecorder::active()) {
		text = dot + "REC " + formatDuration(recorder->realSeconds()) + "  ";
		colour = Theme::Get(Theme::Role::Error);
	}
	if (busy) {
		text += dot + "Working";
		if (!MapRecorder::active()) {
			colour = Theme::Get(Theme::Role::Warning);
		}
	} else {
		text += dot + "Ready";
	}
	const wxString sep = wxString::FromUTF8(" \xC2\xB7 ");
	text += sep + wxString::FromUTF8(backend->name());
	if (u.requests > 0) {
		text += sep + wxString::Format("%d req", u.requests);
		text += sep + "in " + formatTokens(u.input_tokens) + " (+" + formatTokens(u.cache_read_tokens) + " cached)";
		text += sep + "out " + formatTokens(u.output_tokens);
		if (api_agent) {
			text += sep + wxString::Format("$%.3f", backend->estimateCost());
		}
	}
	status->SetLabel(text);
	status->SetForegroundColour(colour);
	wxString tip = wxString::Format("%s: %d request(s), input %s (+%s cached), output %s, $%.3f", wxString::FromUTF8(backend->name()), u.requests, formatTokens(u.input_tokens), formatTokens(u.cache_read_tokens), formatTokens(u.output_tokens), backend->estimateCost());
	if (!api_agent) {
		tip += " (subscription: the cost is only a reference)";
	}
	status->SetToolTip(tip);
	status->Refresh();
}

void ClaudePanel::SetBusy(bool value) {
	busy = value;
	// Send stays available: a message written mid-turn reaches Claude at its
	// next step (or right after the current answer).
	send_button->SetToolTip(value ? "Send now: Claude reads it at its next step, or right after the current answer." : "");
	stop_button->Enable(value);
	clear_button->Enable(!value);
	model_choice->Enable(!value);
	backend_choice->Enable(!value);
	UpdateStatus();
}

void ClaudePanel::SendCurrentInput() {
	wxString text = input->GetValue();
	text.Trim(true).Trim(false);
	if (text.IsEmpty() && attachments.empty()) {
		return;
	}
	if (!g_gui.IsEditorOpen()) {
		AppendLine("Open a map first.", Theme::Get(Theme::Role::Error));
		return;
	}
	input->Clear();
	FitInputHeight();
	AppendLine(busy ? "You (while Claude works)" : "You", Theme::Get(Theme::Role::Success), true);
	if (!text.IsEmpty()) {
		AppendLine(text, Theme::Get(Theme::Role::Text));
	}
	for (const AssistantImage& image : attachments) {
		AppendLine(wxString::Format("[image: %s, %dx%d]", wxString::FromUTF8(image.label), image.width, image.height), Theme::Get(Theme::Role::TextSubtle));
	}
	AppendLine("", Theme::Get(Theme::Role::Text));
	assistant_header_written = false;
	thinking_header_written = false;
	std::vector<AssistantImage> images = std::move(attachments);
	attachments.clear();
	attachment_thumbs.clear();
	UpdateAttachments();
	if (!busy) {
		SetBusy(true);
	}
	backend->send(std::string(text.ToUTF8()), images);
}

void ClaudePanel::onWorkResumed() {
	assistant_header_written = false;
	thinking_header_written = false;
	SetBusy(true);
}

// ---- image attachments -----------------------------------------------------

bool ClaudePanel::AttachImage(const wxImage& source, const wxString& label) {
	if (!source.IsOk()) {
		return false;
	}
	if (attachments.size() >= MAX_ATTACHMENTS) {
		AppendLine(wxString::Format("At most %d images per message.", static_cast<int>(MAX_ATTACHMENTS)), Theme::Get(Theme::Role::Warning));
		return false;
	}
	// Bigger than this the API scales the image down anyway (and bills the pixels).
	wxImage image = source.Copy();
	const int long_edge = std::max(image.GetWidth(), image.GetHeight());
	if (long_edge > MAX_IMAGE_EDGE) {
		const double scale = static_cast<double>(MAX_IMAGE_EDGE) / long_edge;
		image = image.Scale(std::max(1, static_cast<int>(std::lround(image.GetWidth() * scale))), std::max(1, static_cast<int>(std::lround(image.GetHeight() * scale))), wxIMAGE_QUALITY_HIGH);
	}

	const auto encode = [](const wxImage& img, wxBitmapType type, std::string& out) {
		wxMemoryOutputStream stream;
		if (!img.SaveFile(stream, type)) {
			return false;
		}
		const size_t length = static_cast<size_t>(stream.GetLength());
		std::vector<char> bytes(length);
		stream.CopyTo(bytes.data(), length);
		out = std::string(wxBase64Encode(bytes.data(), length).ToUTF8());
		return true;
	};

	// PNG keeps map screenshots and sprites sharp; a photo that comes out too
	// big goes as JPEG instead.
	AssistantImage attached;
	attached.media_type = "image/png";
	if (!encode(image, wxBITMAP_TYPE_PNG, attached.data_base64)) {
		AppendLine("Could not encode the image " + label + ".", Theme::Get(Theme::Role::Error));
		return false;
	}
	if (attached.data_base64.size() > MAX_PNG_BASE64) {
		wxImage flat = image.Copy();
		flat.SetOption(wxIMAGE_OPTION_QUALITY, 88);
		std::string jpeg;
		if (encode(flat, wxBITMAP_TYPE_JPEG, jpeg)) {
			attached.media_type = "image/jpeg";
			attached.data_base64 = std::move(jpeg);
		}
	}
	attached.label = std::string(label.ToUTF8());
	attached.width = image.GetWidth();
	attached.height = image.GetHeight();
	attachments.push_back(std::move(attached));

	// Preview: fit in a THUMB_EDGE square; pixel art stays crisp when enlarged.
	const int edge = FromDIP(THUMB_EDGE);
	const double fit = std::min(static_cast<double>(edge) / image.GetWidth(), static_cast<double>(edge) / image.GetHeight());
	const int tw = std::max(1, static_cast<int>(std::lround(image.GetWidth() * fit)));
	const int th = std::max(1, static_cast<int>(std::lround(image.GetHeight() * fit)));
	attachment_thumbs.emplace_back(image.Scale(tw, th, fit >= 1.0 ? wxIMAGE_QUALITY_NEAREST : wxIMAGE_QUALITY_HIGH));
	UpdateAttachments();
	return true;
}

void ClaudePanel::RemoveAttachment(size_t index) {
	if (index < attachments.size()) {
		attachments.erase(attachments.begin() + static_cast<std::ptrdiff_t>(index));
	}
	if (index < attachment_thumbs.size()) {
		attachment_thumbs.erase(attachment_thumbs.begin() + static_cast<std::ptrdiff_t>(index));
	}
	UpdateAttachments();
}

int ClaudePanel::AttachFiles(const wxArrayString& paths) {
	int added = 0;
	for (const wxString& path : paths) {
		wxImage image;
		{
			wxLogNull quiet; // a file that is not an image would pop up a wx error box
			image.LoadFile(path);
		}
		const wxString name = wxFileName(path).GetFullName();
		if (!image.IsOk()) {
			AppendLine("Not an image the editor can read: " + name, Theme::Get(Theme::Role::Error));
			continue;
		}
		if (AttachImage(image, name)) {
			++added;
		}
	}
	return added;
}

bool ClaudePanel::PasteImageFromClipboard() {
	wxClipboardLocker locker;
	if (!locker) {
		return false;
	}
	// Text on the clipboard means a normal paste, even if a picture came along.
	if (wxTheClipboard->IsSupported(wxDF_UNICODETEXT) || wxTheClipboard->IsSupported(wxDF_TEXT)) {
		return false;
	}
	if (wxTheClipboard->IsSupported(wxDF_BITMAP)) {
		wxBitmapDataObject data;
		if (wxTheClipboard->GetData(data) && data.GetBitmap().IsOk()) {
			return AttachImage(data.GetBitmap().ConvertToImage(), "clipboard");
		}
	}
	if (wxTheClipboard->IsSupported(wxDF_FILENAME)) {
		wxFileDataObject files;
		if (wxTheClipboard->GetData(files)) {
			return AttachFiles(files.GetFilenames()) > 0;
		}
	}
	return false;
}

void ClaudePanel::UpdateAttachments() {
	if (!attachment_row) {
		return;
	}
	// Rebuilt from scratch each time; the buttons are tiny and few (<= MAX_ATTACHMENTS).
	attachment_strip->Clear(true);
	if (attachments.empty()) {
		attachment_label->SetLabel(wxEmptyString);
		GetSizer()->Show(attachment_row, false, true);
		Layout();
		return;
	}
	attachment_label->SetLabel(wxString::Format("%d image(s) go with the next message. Click one to remove it.", static_cast<int>(attachments.size())));
	for (size_t i = 0; i < attachments.size() && i < attachment_thumbs.size(); ++i) {
		const AssistantImage& image = attachments[i];
		auto* thumb = new wxBitmapButton(this, wxID_ANY, attachment_thumbs[i], wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
		thumb->SetToolTip(wxString::Format("%s (%dx%d)\nClick to remove", wxString::FromUTF8(image.label), image.width, image.height));
		thumb->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) {
			// Not from inside the button's own handler: it gets destroyed.
			CallAfter([this, i]() {
				RemoveAttachment(i);
			});
		});
		attachment_strip->Add(thumb, 0, wxRIGHT | wxBOTTOM, FromDIP(4));
	}
	GetSizer()->Show(attachment_row, true, true);
	Layout();
}

void ClaudePanel::OnAttach(wxCommandEvent&) {
	wxFileDialog dialog(this, "Attach images", wxEmptyString, wxEmptyString, "Images (*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp)|*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp|All files (*.*)|*.*", wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	wxArrayString paths;
	dialog.GetPaths(paths);
	AttachFiles(paths);
	FocusInput();
}

// ---- listener --------------------------------------------------------------

void ClaudePanel::onAssistantText(const std::string& delta) {
	EnsureAssistantHeader();
	thinking_header_written = false;
	AppendStyled(wxString::FromUTF8(delta), Theme::Get(Theme::Role::Text));
}

void ClaudePanel::onThinking(const std::string& delta) {
	if (!show_thinking || !show_thinking->GetValue()) {
		return;
	}
	EnsureAssistantHeader();
	if (!thinking_header_written) {
		AppendStyled("\n[thinking] ", Theme::Get(Theme::Role::TextSubtle), false, true);
		thinking_header_written = true;
	}
	AppendStyled(wxString::FromUTF8(delta), Theme::Get(Theme::Role::TextSubtle), false, true);
}

void ClaudePanel::onToolCall(const std::string& name, const nlohmann::json& input) {
	if (name == "Read" && input.is_object() && input.contains("file_path") && input["file_path"].is_string()) {
		MarkKnowledgeRead(wxString::FromUTF8(input["file_path"].get<std::string>()));
	}
	EnsureAssistantHeader();
	thinking_header_written = false;
	std::string args;
	if (name == "run_lua") {
		args = input.value("description", "script");
		const std::string code = input.value("code", "");
		args += " (" + std::to_string(std::count(code.begin(), code.end(), '\n') + 1) + " lines)";
	} else {
		args = input.dump();
		if (args.size() > 160) {
			args = args.substr(0, 157) + "...";
		}
	}
	AppendStyled("\n> " + wxString::FromUTF8(name) + " " + wxString::FromUTF8(args) + "\n", Theme::Get(Theme::Role::Warning));
	// Repaint before a long tool runs. No event processing here: a Stop click in
	// the middle of the tool loop would corrupt the conversation history.
	transcript->Update();
	status->Update();
}

void ClaudePanel::onToolResult(const std::string& name, const ClaudeToolResult& result) {
	(void)name;
	const wxColour colour = result.is_error ? Theme::Get(Theme::Role::Error) : Theme::Get(Theme::Role::TextSubtle);
	AppendStyled("    -> " + wxString::FromUTF8(result.summary) + "\n", colour);
}

void ClaudePanel::onTurnEnd(const std::string& stop_reason) {
	if (stop_reason == "cancelled") {
		AppendLine("\n(stopped)", Theme::Get(Theme::Role::TextSubtle));
	} else if (stop_reason == "refusal") {
		AppendLine("\n(the model declined this request)", Theme::Get(Theme::Role::Warning));
	} else if (stop_reason == "max_tokens") {
		AppendLine("\n(response cut at the token limit; say 'continue' to go on)", Theme::Get(Theme::Role::Warning));
	}
	AppendLine("", Theme::Get(Theme::Role::Text));
	SetBusy(false);
	g_gui.RefreshView();
}

void ClaudePanel::onError(const std::string& message) {
	AppendLine("\nError: " + wxString::FromUTF8(message), Theme::Get(Theme::Role::Error));
	AppendLine("", Theme::Get(Theme::Role::Text));
	SetBusy(false);
}

void ClaudePanel::onUsage(const AssistantUsage&) {
	UpdateStatus();
}

void ClaudePanel::onRateLimit(const AssistantRateLimit& limit) {
	rate_limits[limit.type] = limit;
	UpdatePlanUsage();
}

void ClaudePanel::UpdatePlanUsage() {
	if (!plan_usage) {
		return;
	}
	if (!api_agent && rate_limits.empty()) {
		plan_usage->SetLabel("Plan: usage shows after the first reply");
		plan_usage->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
		return;
	}
	if (api_agent) {
		plan_usage->SetLabel(wxEmptyString);
		return;
	}

	// Friendlier names for the window types; unknown ones show their raw name.
	const auto label = [](const std::string& type) -> wxString {
		if (type == "five_hour") {
			return "Session (5h)";
		}
		if (type == "seven_day") {
			return "Week";
		}
		if (type == "seven_day_opus") {
			return "Week (Opus)";
		}
		if (type == "seven_day_sonnet") {
			return "Week (Sonnet)";
		}
		if (type == "seven_day_overage_included") {
			return "Week (incl. extra usage)";
		}
		if (type == "overage") {
			return "Extra usage";
		}
		return wxString::FromUTF8(type);
	};
	const auto resetText = [](long long unix_seconds) -> wxString {
		if (unix_seconds <= 0) {
			return wxEmptyString;
		}
		wxDateTime when(static_cast<time_t>(unix_seconds));
		const wxDateTime now = wxDateTime::Now();
		if (when.IsSameDate(now)) {
			return when.Format("%H:%M");
		}
		return when.Format("%a %H:%M");
	};

	wxString text = "Plan: ";
	bool warning = false;
	bool first = true;
	for (const auto& [type, limit] : rate_limits) {
		if (!first) {
			text += wxString::FromUTF8(" \xC2\xB7 ");
		}
		first = false;
		const int percent = static_cast<int>(limit.utilization * 100.0 + 0.5);
		text += label(type) + wxString::Format(" %d%%", percent);
		const wxString reset = resetText(limit.resets_at);
		if (!reset.IsEmpty()) {
			text += " (resets " + reset + ")";
		}
		if (limit.status == "rejected") {
			text += " LIMIT REACHED";
			warning = true;
		} else if (limit.status == "allowed_warning" || percent >= 80) {
			warning = true;
		}
		if (limit.using_overage) {
			text += " [extra usage]";
		}
	}
	plan_usage->SetLabel(text);
	plan_usage->SetForegroundColour(warning ? Theme::Get(Theme::Role::Warning) : Theme::Get(Theme::Role::TextSubtle));
	plan_usage->SetToolTip(text);
}

// ---- events ----------------------------------------------------------------

void ClaudePanel::OnSend(wxCommandEvent&) {
	SendCurrentInput();
}

void ClaudePanel::OnStop(wxCommandEvent&) {
	// Backend first: once it has cancelled, a paint that stops below cannot
	// start the conversation's next tool call.
	backend->cancel();
	AssistantLive::get().stopAll("stopped by the user");
}

void ClaudePanel::OnClear(wxCommandEvent&) {
	backend->reset();
	transcript->Clear();
	for (KnowledgeFile& file : knowledge) {
		file.read = false;
	}
	UpdateKnowledgeLabels();
	assistant_header_written = false;
	AppendLine("Conversation cleared. The style guide and API reference are reloaded on the next message.", Theme::Get(Theme::Role::TextSubtle));
	AppendLine("", Theme::Get(Theme::Role::Text));
	UpdateStatus();
}

void ClaudePanel::OnApiKey(wxCommandEvent&) {
	wxPasswordEntryDialog dialog(this, "Paste your Anthropic API key (sk-ant-...). It is stored in the editor settings on this computer.", "Claude API key");
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}
	wxString key = dialog.GetValue();
	key.Trim(true).Trim(false);
	g_settings.setString(Config::CLAUDE_API_KEY, std::string(key.mb_str()));
	g_settings.save();
	LoadApiKey();
}

void ClaudePanel::OnBackendChanged(wxCommandEvent&) {
	if (busy) {
		return;
	}
	CreateBackend();
	AppendLine("Backend: " + wxString::FromUTF8(backend->name()) + ". The conversation was reset.", Theme::Get(Theme::Role::TextSubtle));
	AppendLine("", Theme::Get(Theme::Role::Text));
	UpdateStatus();
}

void ClaudePanel::OnModelChanged(wxCommandEvent&) {
	ApplySettingsToAgent();
	UpdateStatus();
}

void ClaudePanel::OnEffortChanged(wxCommandEvent&) {
	ApplySettingsToAgent();
}

void ClaudePanel::OnShowThinkingChanged(wxCommandEvent&) {
	g_settings.setInteger(Config::CLAUDE_SHOW_THINKING, show_thinking->GetValue() ? 1 : 0);
}

void ClaudePanel::OnLiveChanged(wxCommandEvent&) {
	g_settings.setInteger(Config::CLAUDE_LIVE_ANIMATE, live_animate->GetValue() ? 1 : 0);
	g_settings.setInteger(Config::CLAUDE_LIVE_FOLLOW, live_follow->GetValue() ? 1 : 0);
	g_settings.setInteger(Config::CLAUDE_LIVE_CURSOR, live_cursor->GetValue() ? 1 : 0);
	const int si = std::clamp(live_speed->GetSelection(), 0, static_cast<int>(std::size(SPEEDS)) - 1);
	g_settings.setInteger(Config::CLAUDE_LIVE_RATE, SPEEDS[si].tiles_per_second);
	g_settings.setInteger(Config::CLAUDE_LIVE_MAX_MS, SPEEDS[si].max_ms);
	live_speed->Enable(live_animate->GetValue());
	g_gui.RefreshView();
}

void ClaudePanel::OnRecord(wxCommandEvent&) {
	MapRecorder& recorder = MapRecorder::get();
	if (recorder.isRecording()) {
		recorder.stop();
		return;
	}
	recorder.setStatusCallback([this]() {
		UpdateRecordStatus();
	});
	std::string error;
	if (!recorder.start(error)) {
		AppendLine("Recording: " + wxString::FromUTF8(error), Theme::Get(Theme::Role::Error));
		return;
	}
	AppendLine("Recording the map view to " + wxString::FromUTF8(recorder.outputPath()) + ". Press Stop to finish the video.", Theme::Get(Theme::Role::TextSubtle));
}

void ClaudePanel::UpdateRecordStatus() {
	if (MapRecorder* recorder = MapRecorder::active()) {
		was_recording = true;
		record_button->SetLabel(wxString::FromUTF8("\xE2\x96\xA0 Stop"));
		record_status->SetLabel(wxString::Format("REC %s  (video %s)", formatDuration(recorder->realSeconds()), formatDuration(recorder->videoSeconds())));
		record_status->SetForegroundColour(Theme::Get(Theme::Role::Error));
		UpdateStatus();
		Layout();
		return;
	}
	record_button->SetLabel(wxString::FromUTF8("\xE2\x97\x8F REC"));
	record_status->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	UpdateStatus();
	if (was_recording) {
		// Just stopped: say where the video went (or why there is none).
		was_recording = false;
		const MapRecorder& recorder = MapRecorder::get();
		const std::string error = recorder.lastError();
		if (recorder.framesWritten() == 0) {
			record_status->SetLabel("nothing recorded");
			if (error.empty()) {
				AppendLine("Recording stopped: nothing moved, so no video was saved.", Theme::Get(Theme::Role::TextSubtle));
			} else {
				AppendLine("Recording failed: " + wxString::FromUTF8(error), Theme::Get(Theme::Role::Error));
			}
		} else {
			record_status->SetLabel("saved, " + formatDuration(recorder.videoSeconds()));
			AppendLine(wxString::Format("Time-lapse saved (%s of work, %s of video): ", formatDuration(recorder.realSeconds()), formatDuration(recorder.videoSeconds())) + wxString::FromUTF8(recorder.outputPath()), Theme::Get(Theme::Role::Success));
			if (!error.empty()) {
				AppendLine("It stopped early: " + wxString::FromUTF8(error), Theme::Get(Theme::Role::Warning));
			}
		}
	}
	Layout();
}

void ClaudePanel::OnMcpToggled(wxCommandEvent&) {
	CommitMcpPort();
	g_settings.setInteger(Config::CLAUDE_MCP_EXTERNAL, mcp_external->GetValue() ? 1 : 0);
	const bool ok = ExternalMcp::applySettings();
	g_settings.save(); // the token is generated the first time
	UpdateMcpStatus();
	if (ExternalMcp::isRunning()) {
		AppendLine("MCP server on at " + wxString::FromUTF8(ExternalMcp::url()) + ". Use 'Copy setup' to register it in Claude Code.", Theme::Get(Theme::Role::TextSubtle));
	} else if (!ok) {
		AppendLine("MCP server: " + wxString::FromUTF8(ExternalMcp::lastError()), Theme::Get(Theme::Role::Error));
	} else {
		AppendLine("MCP server off.", Theme::Get(Theme::Role::TextSubtle));
	}
}

void ClaudePanel::OnMcpPortKey(wxKeyEvent& event) {
	const int key = event.GetKeyCode();
	if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
		CommitMcpPort();
		return;
	}
	// Digits by hand: the single-key accelerators would eat them.
	if (key >= '0' && key <= '9' && !event.ShiftDown() && !event.ControlDown() && !event.AltDown()) {
		mcp_port->WriteText(wxString(static_cast<wxChar>(key)));
		return;
	}
	event.Skip();
}

void ClaudePanel::OnMcpPortKillFocus(wxFocusEvent& event) {
	CommitMcpPort();
	event.Skip();
}

void ClaudePanel::CommitMcpPort() {
	if (!mcp_port || !mcp_external || IsBeingDeleted()) {
		return;
	}
	const int current = g_settings.getInteger(Config::CLAUDE_MCP_PORT);
	long value = 0;
	if (!mcp_port->GetValue().ToLong(&value) || value < 1024 || value > 65535) {
		mcp_port->ChangeValue(wxString::Format("%d", current));
		return;
	}
	if (value == current) {
		return;
	}
	g_settings.setInteger(Config::CLAUDE_MCP_PORT, static_cast<int>(value));
	if (mcp_external->GetValue()) {
		if (ExternalMcp::applySettings()) {
			AppendLine("MCP server moved to " + wxString::FromUTF8(ExternalMcp::url()) + ". Register it again in the clients ('Copy setup').", Theme::Get(Theme::Role::TextSubtle));
		} else {
			AppendLine("MCP server: " + wxString::FromUTF8(ExternalMcp::lastError()), Theme::Get(Theme::Role::Error));
		}
	}
	UpdateMcpStatus();
}

void ClaudePanel::OnMcpCopy(wxCommandEvent&) {
	CommitMcpPort();
	const std::string command = ExternalMcp::claudeAddCommand();
	g_settings.save(); // keep the token that is about to be pasted somewhere
	if (wxTheClipboard->Open()) {
		wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(command)));
		wxTheClipboard->Close();
	}
	AppendLine("Copied. Run it once in a terminal and Claude Code gets the editor tools (as mcp__rme__*) while this server is on:", Theme::Get(Theme::Role::TextSubtle));
	AppendLine(wxString::FromUTF8(command), Theme::Get(Theme::Role::Text));
	AppendLine("Other MCP clients: URL " + wxString::FromUTF8(ExternalMcp::url()) + " with the header \"Authorization: Bearer <token above>\".", Theme::Get(Theme::Role::TextSubtle));
	if (!ExternalMcp::isRunning()) {
		AppendLine("The server is off: tick 'MCP server' first.", Theme::Get(Theme::Role::Warning));
	}
	AppendLine("", Theme::Get(Theme::Role::Text));
}

void ClaudePanel::UpdateMcpStatus() {
	if (!mcp_status) {
		return;
	}
	if (ExternalMcp::isRunning()) {
		mcp_status->SetLabel("on");
		mcp_status->SetForegroundColour(Theme::Get(Theme::Role::Success));
		mcp_status->SetToolTip(wxString::FromUTF8(ExternalMcp::url()));
	} else if (mcp_external->GetValue()) {
		mcp_status->SetLabel("failed");
		mcp_status->SetForegroundColour(Theme::Get(Theme::Role::Error));
		mcp_status->SetToolTip(wxString::FromUTF8(ExternalMcp::lastError()));
	} else {
		mcp_status->SetLabel("off");
		mcp_status->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
		mcp_status->SetToolTip(wxEmptyString);
	}
	Layout();
}

void ClaudePanel::OnInputKeyDown(wxKeyEvent& event) {
	const int key = event.GetKeyCode();
	if (event.GetEventType() == wxEVT_CHAR_HOOK && event.ControlDown() && !event.AltDown() && key == 'V' && PasteImageFromClipboard()) {
		return; // an image (or image files) was on the clipboard: attached, nothing to paste as text
	}
	if ((key == WXK_RETURN || key == WXK_NUMPAD_ENTER) && !event.ShiftDown() && !event.ControlDown()) {
		if (event.GetEventType() == wxEVT_CHAR_HOOK) {
			SendCurrentInput();
		}
		return; // swallow both the hook and the key down so no newline is inserted
	}
#ifdef __WXMSW__
	if (event.GetEventType() == wxEVT_CHAR_HOOK && event.ControlDown() == event.AltDown() && key != WXK_ESCAPE && !(key >= WXK_F1 && key <= WXK_F24)) {
		// Typing (plain keys, AltGr): handled here so no parent CHAR_HOOK turns it
		// into an editor hotkey, but the key still goes the normal Windows way
		// (ChatInput keeps the accelerators out), so accents and AltGr compose.
		event.DoAllowNextEvent();
		return;
	}
#else
	if (event.GetEventType() == wxEVT_CHAR_HOOK && !event.ControlDown() && !event.AltDown()) {
		// Letters, digits and space collide with the single-letter menu accelerators
		// (A = automagic, S = selection mode, D = doodad palette...). Same trick as
		// the palette search fields: consume the key here, before the accelerator
		// table runs, and insert the character ourselves. Everything else (symbols,
		// navigation, backspace) is left to the control.
		const bool letter = key >= 'A' && key <= 'Z';
		const bool digit = key >= '0' && key <= '9';
		if (letter || (digit && !event.ShiftDown()) || key == WXK_SPACE) {
			wxChar ch = static_cast<wxChar>(key);
			if (letter) {
				const bool upper = event.ShiftDown() != wxGetKeyState(WXK_CAPITAL);
				ch = upper ? static_cast<wxChar>(key) : static_cast<wxChar>(key - 'A' + 'a');
			}
			input->WriteText(wxString(ch));
			return; // consumed: no accelerator, no second insertion
		}
		event.DoAllowNextEvent();
	}
#endif
	event.Skip();
}
