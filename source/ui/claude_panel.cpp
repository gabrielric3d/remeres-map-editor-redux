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
#include <wx/valtext.h>

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
		{ "Claude Code (subscription)", "claude_code" },
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

	// Toolbar: backend, model, effort, key.
	auto* top = new wxBoxSizer(wxHORIZONTAL);
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
	backend_choice->SetToolTip("Claude Code uses the claude program installed on this PC and your Claude subscription (Pro/Max). API key calls the Anthropic API directly and is billed per token.");
	top->Add(backend_choice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));

	wxArrayString model_labels;
	for (const auto& m : MODELS) {
		model_labels.Add(m.label);
	}
	model_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, model_labels);
	const std::string saved_model = g_settings.getString(Config::CLAUDE_MODEL);
	int model_index = 0;
	for (size_t i = 0; i < std::size(MODELS); ++i) {
		if (saved_model == MODELS[i].id) {
			model_index = static_cast<int>(i);
		}
	}
	model_choice->SetSelection(model_index);
	model_choice->SetToolTip("Model. Opus 5.5 is the newest Opus and cheaper than Opus 5; Sonnet 5 is cheaper still, Fable 5.1 is the strongest and most expensive.");
	top->Add(model_choice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));

	wxArrayString effort_labels;
	for (const char* e : EFFORTS) {
		effort_labels.Add(e);
	}
	effort_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, effort_labels);
	const std::string saved_effort = g_settings.getString(Config::CLAUDE_EFFORT);
	int effort_index = 2;
	for (size_t i = 0; i < std::size(EFFORTS); ++i) {
		if (saved_effort == EFFORTS[i]) {
			effort_index = static_cast<int>(i);
		}
	}
	effort_choice->SetSelection(effort_index);
	effort_choice->SetToolTip("Effort: how hard the model thinks. high is a good default; xhigh/max for big builds.");
	top->Add(effort_choice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));

	api_key_button = new wxButton(this, wxID_ANY, "API key", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	top->Add(api_key_button, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(top, 0, wxEXPAND | wxALL, FromDIP(4));

	// Live view: how Claude's work shows up on the map, whoever drives it.
	const wxColour subtle = Theme::Get(Theme::Role::TextSubtle);
	auto* live = new wxBoxSizer(wxHORIZONTAL);
	live_animate = new wxCheckBox(this, wxID_ANY, "Animate");
	live_animate->SetValue(g_settings.getBoolean(Config::CLAUDE_LIVE_ANIMATE));
	live_animate->SetToolTip("Lay paint down a few tiles per frame so you can watch Claude work. Each paint is still one undo step.");
	live_animate->SetForegroundColour(subtle);
	live->Add(live_animate, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
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
	live->Add(live_speed, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	live_follow = new wxCheckBox(this, wxID_ANY, "Follow");
	live_follow->SetValue(g_settings.getBoolean(Config::CLAUDE_LIVE_FOLLOW));
	live_follow->SetToolTip("Glide the camera to where Claude is working.");
	live_follow->SetForegroundColour(subtle);
	live->Add(live_follow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
	live_cursor = new wxCheckBox(this, wxID_ANY, "Cursor");
	live_cursor->SetValue(g_settings.getBoolean(Config::CLAUDE_LIVE_CURSOR));
	live_cursor->SetToolTip("Show a \"Claude\" cursor and the area it is reading or changing.");
	live_cursor->SetForegroundColour(subtle);
	live->Add(live_cursor, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(live, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

	// The editor tools for programs outside the editor.
	auto* mcp = new wxBoxSizer(wxHORIZONTAL);
	mcp_external = new wxCheckBox(this, wxID_ANY, "MCP server, port");
	mcp_external->SetValue(g_settings.getBoolean(Config::CLAUDE_MCP_EXTERNAL));
	mcp_external->SetToolTip("Let other programs on this PC (Claude Code in a terminal or IDE, Claude Desktop) read and paint this map while you watch. Localhost only; clients must send the token that 'Copy setup' includes.");
	mcp_external->SetForegroundColour(subtle);
	mcp->Add(mcp_external, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	mcp_port = new wxTextCtrl(this, wxID_ANY, wxString::Format("%d", g_settings.getInteger(Config::CLAUDE_MCP_PORT)), wxDefaultPosition, FromDIP(wxSize(56, -1)), wxTE_PROCESS_ENTER, wxTextValidator(wxFILTER_DIGITS));
	mcp_port->SetToolTip("Port on 127.0.0.1 (1024-65535). Changing it means registering the server again in the clients.");
	mcp->Add(mcp_port, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	mcp_copy = new wxButton(this, wxID_ANY, "Copy setup", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	mcp_copy->SetToolTip("Copy the command that registers the editor in Claude Code (claude mcp add ...). Run it once in a terminal.");
	mcp->Add(mcp_copy, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	mcp_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
	mcp->Add(mcp_status, 1, wxALIGN_CENTER_VERTICAL);
	sizer->Add(mcp, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

	// Time-lapse of the map view.
	auto* record = new wxBoxSizer(wxHORIZONTAL);
	record_button = new wxButton(this, wxID_ANY, wxString::FromUTF8("\xE2\x97\x8F REC"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	record_button->SetToolTip("Record a time-lapse of the map view (.mp4): the camera, the mouse, Claude's cursor and every change. Only moments when something moves are recorded, so idle time drops out.");
	record->Add(record_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
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
	record->Add(record_speed, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	record_folder_button = new wxButton(this, wxID_ANY, "Folder", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	record_folder_button->SetToolTip("Open the folder where the videos are saved (the screenshot folder)");
	record->Add(record_folder_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
	record_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
	record_status->SetForegroundColour(subtle);
	record->Add(record_status, 1, wxALIGN_CENTER_VERTICAL);
	sizer->Add(record, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

	// Transcript.
	transcript = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_AUTO_URL | wxBORDER_NONE);
	transcript->SetBackgroundColour(Theme::Get(Theme::Role::Background));
	transcript->SetForegroundColour(Theme::Get(Theme::Role::Text));
	sizer->Add(transcript, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));

	// Status lines: tokens/cost of this conversation, then the subscription windows.
	status = new wxStaticText(this, wxID_ANY, wxEmptyString);
	status->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	sizer->Add(status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(4));
	plan_usage = new wxStaticText(this, wxID_ANY, wxEmptyString);
	plan_usage->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	plan_usage->SetToolTip("Subscription usage windows reported by Claude Code (same as /usage): the 5-hour session window and the weekly window, with the time they reset.");
	sizer->Add(plan_usage, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(4));

	// Input + buttons.
	input = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(-1, 72)), wxTE_MULTILINE | wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB);
	input->SetBackgroundColour(Theme::Get(Theme::Role::RaisedSurface));
	input->SetForegroundColour(Theme::Get(Theme::Role::Text));
	sizer->Add(input, 0, wxEXPAND | wxALL, FromDIP(4));
	auto* hint = new wxStaticText(this, wxID_ANY, "Enter sends, Shift+Enter adds a line. You can write while Claude works. Images: Image..., Ctrl+V or drop files here.");
	hint->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	hint->Wrap(FromDIP(360));
	sizer->Add(hint, 0, wxLEFT | wxRIGHT, FromDIP(6));

	// Images waiting to go with the next message.
	attachment_row = new wxBoxSizer(wxHORIZONTAL);
	attachment_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
	attachment_label->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	attachment_row->Add(attachment_label, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
	remove_attachments_button = new wxButton(this, wxID_ANY, "Remove", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	remove_attachments_button->SetToolTip("Remove the attached images");
	attachment_row->Add(remove_attachments_button, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(attachment_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(6));

	auto* buttons = new wxBoxSizer(wxHORIZONTAL);
	show_thinking = new wxCheckBox(this, wxID_ANY, "Show thinking");
	show_thinking->SetValue(g_settings.getBoolean(Config::CLAUDE_SHOW_THINKING));
	show_thinking->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	buttons->Add(show_thinking, 0, wxALIGN_CENTER_VERTICAL);
	buttons->AddStretchSpacer();
	attach_button = new wxButton(this, wxID_ANY, "Image...");
	attach_button->SetToolTip("Attach images to the next message (you can also paste with Ctrl+V or drop files on the panel)");
	buttons->Add(attach_button, 0, wxRIGHT, FromDIP(4));
	clear_button = new wxButton(this, wxID_ANY, "Clear");
	clear_button->SetToolTip("Forget the conversation (the map is not touched)");
	buttons->Add(clear_button, 0, wxRIGHT, FromDIP(4));
	stop_button = new wxButton(this, wxID_ANY, "Stop");
	stop_button->Enable(false);
	buttons->Add(stop_button, 0, wxRIGHT, FromDIP(4));
	send_button = new wxButton(this, wxID_ANY, "Send");
	buttons->Add(send_button, 0);
	sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(4));

	SetSizer(sizer);

	send_button->Bind(wxEVT_BUTTON, &ClaudePanel::OnSend, this);
	attach_button->Bind(wxEVT_BUTTON, &ClaudePanel::OnAttach, this);
	remove_attachments_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		attachments.clear();
		UpdateAttachments();
	});
	// Image files dropped on the panel or the message box get attached.
	SetDropTarget(new ImageDropTarget(this));
	input->SetDropTarget(new ImageDropTarget(this));
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
	AppendLine("", Theme::Get(Theme::Role::Text));
}

void ClaudePanel::FocusInput() {
	if (input) {
		input->SetFocus();
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
	wxString text;
	if (busy) {
		text = "Working... ";
	}
	text += wxString::FromUTF8(backend->name()) + " | ";
	text += wxString::Format("%d request(s) | in %s (+%s cached) | out %s | $%.3f", u.requests, formatTokens(u.input_tokens), formatTokens(u.cache_read_tokens), formatTokens(u.output_tokens), backend->estimateCost());
	if (!api_agent) {
		text += " (subscription: reference only)";
	}
	status->SetLabel(text);
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
	UpdateAttachments();
	return true;
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
	if (attachments.empty()) {
		attachment_label->SetLabel(wxEmptyString);
		GetSizer()->Show(attachment_row, false, true);
	} else {
		wxString text = wxString::Format("%d image(s):", static_cast<int>(attachments.size()));
		for (const AssistantImage& image : attachments) {
			text += wxString::Format(" %s (%dx%d)", wxString::FromUTF8(image.label), image.width, image.height);
		}
		attachment_label->SetLabel(text);
		attachment_label->SetToolTip(text);
		GetSizer()->Show(attachment_row, true, true);
	}
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
		plan_usage->SetLabel("Plan usage: waiting for the first reply.");
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

	wxString text = "Plan usage: ";
	bool warning = false;
	bool first = true;
	for (const auto& [type, limit] : rate_limits) {
		if (!first) {
			text += "  |  ";
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
		Layout();
		return;
	}
	record_button->SetLabel(wxString::FromUTF8("\xE2\x97\x8F REC"));
	record_status->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
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
	event.Skip();
}
