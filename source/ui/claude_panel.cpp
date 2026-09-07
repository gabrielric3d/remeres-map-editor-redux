//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/claude_panel.h"

#include "ai/claude_code_backend.h"
#include "app/settings.h"
#include "editor/hotkey_manager.h"
#include "ui/gui.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdlib>
#include <spdlog/spdlog.h>
#include <iterator>
#include <wx/choice.h>
#include <wx/datetime.h>
#include <wx/textdlg.h>

namespace {
	struct ModelOption {
		const char* label;
		const char* id;
	};
	const ModelOption MODELS[] = {
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
	CreateBackend();
	UpdateStatus();
}

ClaudePanel::~ClaudePanel() {
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
	model_choice->SetToolTip("Model. Opus 5 is the default; Sonnet 5 is cheaper, Fable 5.1 is the strongest and most expensive.");
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
	auto* hint = new wxStaticText(this, wxID_ANY, "Enter sends, Shift+Enter adds a line.");
	hint->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	sizer->Add(hint, 0, wxLEFT | wxRIGHT, FromDIP(6));

	auto* buttons = new wxBoxSizer(wxHORIZONTAL);
	show_thinking = new wxCheckBox(this, wxID_ANY, "Show thinking");
	show_thinking->SetValue(g_settings.getBoolean(Config::CLAUDE_SHOW_THINKING));
	show_thinking->SetForegroundColour(Theme::Get(Theme::Role::TextSubtle));
	buttons->Add(show_thinking, 0, wxALIGN_CENTER_VERTICAL);
	buttons->AddStretchSpacer();
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
	stop_button->Bind(wxEVT_BUTTON, &ClaudePanel::OnStop, this);
	clear_button->Bind(wxEVT_BUTTON, &ClaudePanel::OnClear, this);
	api_key_button->Bind(wxEVT_BUTTON, &ClaudePanel::OnApiKey, this);
	backend_choice->Bind(wxEVT_CHOICE, &ClaudePanel::OnBackendChanged, this);
	model_choice->Bind(wxEVT_CHOICE, &ClaudePanel::OnModelChanged, this);
	effort_choice->Bind(wxEVT_CHOICE, &ClaudePanel::OnEffortChanged, this);
	show_thinking->Bind(wxEVT_CHECKBOX, &ClaudePanel::OnShowThinkingChanged, this);
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
	send_button->Enable(!value);
	stop_button->Enable(value);
	clear_button->Enable(!value);
	model_choice->Enable(!value);
	backend_choice->Enable(!value);
	UpdateStatus();
}

void ClaudePanel::SendCurrentInput() {
	if (busy) {
		return;
	}
	wxString text = input->GetValue();
	text.Trim(true).Trim(false);
	if (text.IsEmpty()) {
		return;
	}
	if (!g_gui.IsEditorOpen()) {
		AppendLine("Open a map first.", Theme::Get(Theme::Role::Error));
		return;
	}
	input->Clear();
	AppendLine("You", Theme::Get(Theme::Role::Success), true);
	AppendLine(text, Theme::Get(Theme::Role::Text));
	AppendLine("", Theme::Get(Theme::Role::Text));
	assistant_header_written = false;
	thinking_header_written = false;
	SetBusy(true);
	backend->send(std::string(text.ToUTF8()));
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
	backend->cancel();
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

void ClaudePanel::OnInputKeyDown(wxKeyEvent& event) {
	const int key = event.GetKeyCode();
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
