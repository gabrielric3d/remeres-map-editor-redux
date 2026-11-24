//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ai/claude_code_backend.h"

#include "ai/claude_agent.h"
#include "ai/claude_client.h"
#include "app/settings.h"
#include "lua/lua_script_manager.h"

#include <algorithm>
#include <fstream>
#include <random>
#include <spdlog/spdlog.h>
#include <vector>
#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/txtstrm.h>
#include <wx/utils.h>

using Json = nlohmann::json;

namespace {
	constexpr int POLL_MS = 60;
	constexpr size_t MAX_STDERR = 8000;
	const std::string MCP_TOOL_PREFIX = std::string("mcp__") + McpServer::serverName() + "__";

	std::string newUuid() {
		static std::mt19937_64 rng(std::random_device {}());
		std::uniform_int_distribution<uint32_t> dist(0, 15);
		const char* hex = "0123456789abcdef";
		std::string out = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
		for (char& c : out) {
			if (c == 'x') {
				c = hex[dist(rng)];
			} else if (c == 'y') {
				c = hex[8 + (dist(rng) & 3)];
			}
		}
		return out;
	}

	std::string quoteArg(const std::string& arg) {
		if (arg.find_first_of(" \t\"") == std::string::npos) {
			return arg;
		}
		std::string out = "\"";
		for (char c : arg) {
			if (c == '"') {
				out += "\\\"";
			} else {
				out += c;
			}
		}
		out += "\"";
		return out;
	}

	std::string tempPath(const std::string& file_name) {
		wxFileName fn(wxStandardPaths::Get().GetTempDir(), file_name);
		return std::string(fn.GetFullPath().ToUTF8());
	}
}

void ClaudeCodeBackend::Process::OnTerminate(int, int status) {
	if (owner) {
		owner->onProcessExited(status);
	}
	delete this;
}

ClaudeCodeBackend::ClaudeCodeBackend() {
	timer.SetOwner(this);
	Bind(wxEVT_TIMER, &ClaudeCodeBackend::onTimer, this);
	// A tool result can arrive after this backend is gone (an animated paint
	// outlives a backend switch), hence the token check before touching `this`.
	const std::weak_ptr<int> alive = alive_token;
	mcp.setObserver([this, alive](const std::string& name, const Json& input, const ClaudeToolResult* result) {
		if (alive.expired() || !listener) {
			return;
		}
		if (!result) {
			listener->onToolCall(name, input);
		} else {
			listener->onToolResult(name, *result);
		}
	});
}

ClaudeCodeBackend::~ClaudeCodeBackend() {
	listener = nullptr;
	mcp.setObserver(nullptr);
	killProcess(false);
	mcp.stop();
}

std::string ClaudeCodeBackend::resolveExecutable() {
	const std::string configured = g_settings.getString(Config::CLAUDE_CODE_PATH);
	if (!configured.empty() && wxFileName::FileExists(wxString::FromUTF8(configured))) {
		return configured;
	}
	const wxString sep = wxFILE_SEP_PATH;
	const wxString home = wxGetHomeDir();
	wxString appdata;
	wxGetEnv("APPDATA", &appdata);
	wxString local_appdata;
	wxGetEnv("LOCALAPPDATA", &local_appdata);

	// 1. Fixed install places (the native installer keeps itself updated here).
	std::vector<wxString> candidates = {
		home + sep + ".local" + sep + "bin" + sep + "claude.exe",
		home + sep + ".local" + sep + "bin" + sep + "claude",
		home + sep + ".claude" + sep + "local" + sep + "claude.exe",
		home + sep + ".claude" + sep + "local" + sep + "claude",
		appdata + sep + "npm" + sep + "claude.cmd",
		local_appdata + sep + "Programs" + sep + "claude" + sep + "claude.exe",
	};
	// 2. Every folder on PATH.
	wxString path_env;
	if (wxGetEnv("PATH", &path_env)) {
		for (const wxString& dir : wxSplit(path_env, wxPATH_SEP[0], 0)) {
			if (dir.empty()) {
				continue;
			}
			for (const char* name : { "claude.exe", "claude.cmd", "claude" }) {
				candidates.push_back(dir + sep + name);
			}
		}
	}
	for (const wxString& candidate : candidates) {
		if (!candidate.empty() && wxFileName::FileExists(candidate)) {
			return std::string(candidate.ToUTF8());
		}
	}

	// 3. The binary bundled with the IDE extension; newest one wins.
	wxString best;
	wxDateTime best_time;
	for (const char* ide : { ".vscode", ".vscode-insiders", ".cursor", ".windsurf" }) {
		const wxString extensions = home + sep + ide + sep + "extensions";
		wxDir dir;
		if (!wxDir::Exists(extensions) || !dir.Open(extensions)) {
			continue;
		}
		wxString name;
		for (bool more = dir.GetFirst(&name, "anthropic.claude-code-*", wxDIR_DIRS); more; more = dir.GetNext(&name)) {
			for (const char* exe : { "claude.exe", "claude" }) {
				const wxString candidate = extensions + sep + name + sep + "resources" + sep + "native-binary" + sep + exe;
				if (!wxFileName::FileExists(candidate)) {
					continue;
				}
				const wxDateTime modified = wxFileName(candidate).GetModificationTime();
				if (best.empty() || (modified.IsValid() && best_time.IsValid() && modified.IsLaterThan(best_time))) {
					best = candidate;
					best_time = modified;
				}
			}
		}
	}
	return std::string(best.ToUTF8());
}

std::string ClaudeCodeBackend::notInstalledMessage() {
	return "Claude Code was not found on this PC. Install it once (it keeps itself updated): open PowerShell and run\n"
		   "    irm https://claude.ai/install.ps1 | iex\n"
		   "then run `claude` once to log in, and send the message again. "
		   "(Or set the path to claude.exe in Preferences.)";
}

bool ClaudeCodeBackend::writeSupportFiles() {
	if (!mcp.isRunning() && !mcp.start()) {
		return false;
	}
	mcp_config_path = tempPath("rme_claude_mcp.json");
	system_prompt_path = tempPath("rme_claude_system.md");

	Json config = { { "mcpServers", { { McpServer::serverName(), { { "type", "http" }, { "url", mcp.url() } } } } } };
	{
		std::ofstream out(mcp_config_path, std::ios::binary | std::ios::trunc);
		if (!out) {
			return false;
		}
		out << config.dump(2);
	}

	std::string prompt = ClaudeAgent::systemHeader();
	prompt += "\n## This session\n";
	std::string tool_names;
	for (const auto& def : ClaudeTools::definitions()) {
		tool_names += (tool_names.empty() ? "" : ", ") + MCP_TOOL_PREFIX + def["name"].get<std::string>();
	}
	prompt += "- You are running through Claude Code with the editor exposed as the MCP server \"" + std::string(McpServer::serverName()) + "\": the editor tools are " + tool_names + ". Besides them you have Read/Glob/Grep, and Edit/Write ONLY inside your knowledge (`claude_style.md` and the folder `claude_style/` next to it). Any other file is blocked; never try.\n";
	prompt += "- The Lua scripting API reference is the file `" + ClaudeAgent::apiReferencePath() + "`. Read the relevant sections before writing run_lua code; do not guess function names.\n";
	const std::string style_path = ClaudeAgent::stylePath();
	if (wxFileName::FileExists(wxString::FromUTF8(style_path))) {
		prompt += "- The mapper's style guide is `" + style_path + "`. Read it at the start of a task and follow it.\n";
	}
	prompt +=
		"\n## Your knowledge (the only memory you keep between conversations)\n"
		"Every conversation starts from zero: what is not written in `claude_style.md` or `claude_style/` is lost. "
		"The mapper spends hours teaching you; losing it is the worst failure you can have.\n"
		"- At the start of a conversation read `claude_style/diario.md` (where the studies stopped, what comes next) and the guide of the theme you will work on.\n"
		"- Save AS YOU LEARN: when the mapper corrects you or approves a rule, write it into the guide of that theme (borders in `claude_style/piso-e-bordas.md`, structure in `estrutura.md`, details in `detalhes.md`) in the same turn. Do not wait for the end of the day.\n"
		"- Write the rule itself, concrete and checkable (item ids, coordinates of the mapper's example, what is forbidden and why), in Portuguese, in the mapper's own words where you can. Edit the section that already covers it instead of adding a duplicate; if a new rule contradicts an old one, replace the old one.\n"
		"- When the mapper ends the day (\"vamos parar\", \"continuamos amanha\", \"salve seu conhecimento\", \"boa noite\"...), or the conversation is wrapping up: 1) make sure every lesson of this conversation is in the guides; 2) rewrite `claude_style/diario.md`: date, what was studied, where it is on the map (coordinates), what the mapper approved, what is still wrong, the next step; 3) tell the mapper in two lines what you saved and in which files.\n"
		"- Never mark something as \"perfect\" or \"solved\" unless the mapper said so.\n"
		"- A new guide file (`claude_style/<tema>.md`) is fine for a new theme; add a line for it in the list in `claude_style.md`.\n";
	{
		std::ofstream out(system_prompt_path, std::ios::binary | std::ios::trunc);
		if (!out) {
			return false;
		}
		out << prompt;
	}
	return true;
}

std::string ClaudeCodeBackend::buildCommand() const {
	std::string exe = resolveExecutable();
	std::string cmd;
	const std::string lower_exe = [&] {
		std::string s = exe;
		std::transform(s.begin(), s.end(), s.begin(), ::tolower);
		return s;
	}();
	if (lower_exe.size() > 4 && (lower_exe.compare(lower_exe.size() - 4, 4, ".cmd") == 0 || lower_exe.compare(lower_exe.size() - 4, 4, ".bat") == 0)) {
		cmd = "cmd.exe /c " + quoteArg(exe);
	} else {
		cmd = quoteArg(exe);
	}
	cmd += " -p --output-format stream-json --input-format stream-json --verbose --include-partial-messages";
	cmd += " --permission-mode dontAsk";
	cmd += " --tools Read,Glob,Grep,Edit,Write";
	// Edit/Write only on the knowledge files (paths relative to the cwd, the scripts
	// folder); dontAsk denies every other write. An Edit(...) rule also covers Write.
	cmd += " --allowedTools mcp__" + std::string(McpServer::serverName()) + ",Read,Glob,Grep \"Edit(./claude_style/**)\" \"Edit(./claude_style.md)\"";
	cmd += " --strict-mcp-config --mcp-config " + quoteArg(mcp_config_path);
	cmd += " --append-system-prompt-file " + quoteArg(system_prompt_path);
	if (!model.empty()) {
		cmd += " --model " + quoteArg(model);
	}
	if (!effort.empty()) {
		cmd += " --effort " + effort;
	}
	if (resume_next && !session_id.empty()) {
		cmd += " --resume " + session_id;
	} else {
		cmd += " --session-id " + session_id;
	}
	return cmd;
}

bool ClaudeCodeBackend::ensureProcess() {
	if (process) {
		return true;
	}
	if (!writeSupportFiles()) {
		fail("Could not start the local MCP server or write the temp files for Claude Code.");
		return false;
	}
	if (session_id.empty()) {
		session_id = newUuid();
		resume_next = false;
	}

	if (resolveExecutable().empty()) {
		fail(notInstalledMessage());
		return false;
	}

	init_seen = false;
	stdout_buffer.clear();
	stderr_buffer.clear();
	stdin_pending.clear();
	blocks.clear();

	process = new Process(this);
	wxExecuteEnv env;
	env.cwd = wxString::FromUTF8(g_luaScripts.getScriptsDirectory());
	const std::string cmd = buildCommand();
	spdlog::info("ClaudeCodeBackend: {}", cmd);
	{
		wxLogNull quiet; // a failed launch is reported in the chat, not in a popup
		pid = wxExecute(wxString::FromUTF8(cmd), wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE, process, &env);
	}
	if (pid <= 0) {
		delete process;
		process = nullptr;
		pid = 0;
		fail("Could not launch Claude Code (" + resolveExecutable() + "). Check `claude --version` in a terminal.\n" + notInstalledMessage());
		return false;
	}
	resume_next = false;
	timer.Start(POLL_MS);
	return true;
}

void ClaudeCodeBackend::killProcess(bool keep_session) {
	timer.Stop();
	if (process) {
		process->owner = nullptr; // the wxProcess deletes itself in OnTerminate
		if (pid > 0) {
			wxProcess::Kill(static_cast<int>(pid), wxSIGKILL, wxKILL_CHILDREN);
		}
		process = nullptr;
		pid = 0;
	}
	stdin_pending.clear();
	if (keep_session && !session_id.empty()) {
		resume_next = true;
	} else {
		session_id.clear();
		resume_next = false;
	}
}

void ClaudeCodeBackend::onProcessExited(int status) {
	pumpOutput();
	timer.Stop();
	process = nullptr;
	pid = 0;
	stdin_pending.clear();
	if (!session_id.empty()) {
		resume_next = true; // the conversation lives in Claude Code's session file
	}
	if (busy) {
		busy = false;
		std::string message = "Claude Code exited (code " + std::to_string(status) + ").";
		if (!stderr_buffer.empty()) {
			message += "\n" + stderr_buffer;
		}
		if (listener) {
			listener->onError(message);
		}
	}
}

void ClaudeCodeBackend::fail(const std::string& message) {
	busy = false;
	if (listener) {
		listener->onError(message);
	}
}

void ClaudeCodeBackend::send(const std::string& user_text, const std::vector<AssistantImage>& images) {
	// While busy the line still goes in right away: Claude Code hands it to the
	// model at the next tool step, or answers it as a new turn after this one.
	const bool was_busy = busy;
	busy = true;
	if (!ensureProcess()) {
		return;
	}
	Json message = { { "type", "user" }, { "message", { { "role", "user" }, { "content", assistantUserContent(user_text, images) } } } };
	if (!process->GetOutputStream()) {
		fail("Claude Code's stdin is not available.");
		return;
	}
	// Queued, not written in one go: the pipe takes a few KB per write, and a
	// line cut short (an image is hundreds of KB of base64) left Claude Code
	// waiting forever for the rest of the message.
	stdin_pending += message.dump();
	stdin_pending += '\n';
	flushStdin();
	if (!was_busy) {
		text_streamed = false;
		blocks.clear();
	}
}

void ClaudeCodeBackend::cancel() {
	if (!busy) {
		return;
	}
	// No interrupt channel in this mode: kill and resume the session next time.
	killProcess(true);
	busy = false;
	if (listener) {
		listener->onTurnEnd("cancelled");
	}
}

void ClaudeCodeBackend::reset() {
	const bool was_busy = busy;
	killProcess(false);
	busy = false;
	blocks.clear();
	if (was_busy && listener) {
		listener->onTurnEnd("cancelled");
	}
}

void ClaudeCodeBackend::onTimer(wxTimerEvent&) {
	flushStdin();
	pumpOutput();
}

void ClaudeCodeBackend::flushStdin() {
	if (!process || stdin_pending.empty()) {
		return;
	}
	wxOutputStream* in = process->GetOutputStream();
	if (!in) {
		return;
	}
	constexpr size_t CHUNK = 4096;
	size_t sent = 0;
	// Keep going while the child keeps reading; stop at the first write the pipe
	// refuses (full) and try again on the next timer tick.
	while (sent < stdin_pending.size()) {
		const size_t len = std::min(CHUNK, stdin_pending.size() - sent);
		in->Write(stdin_pending.data() + sent, len);
		const size_t n = in->LastWrite();
		if (in->GetLastError() != wxSTREAM_NO_ERROR) {
			in->Reset(); // a full non-blocking pipe reads as an error; not fatal
		}
		if (n == 0) {
			break;
		}
		sent += n;
	}
	stdin_pending.erase(0, sent);
}

void ClaudeCodeBackend::pumpOutput() {
	if (!process) {
		return;
	}
	char chunk[8192];
	wxInputStream* out = process->GetInputStream();
	while (out && process->IsInputAvailable()) {
		out->Read(chunk, sizeof(chunk));
		const size_t n = out->LastRead();
		if (n == 0) {
			break;
		}
		stdout_buffer.append(chunk, n);
	}
	wxInputStream* err = process->GetErrorStream();
	while (err && process->IsErrorAvailable()) {
		err->Read(chunk, sizeof(chunk));
		const size_t n = err->LastRead();
		if (n == 0) {
			break;
		}
		stderr_buffer.append(chunk, n);
		if (stderr_buffer.size() > MAX_STDERR) {
			stderr_buffer.erase(0, stderr_buffer.size() - MAX_STDERR);
		}
	}

	size_t start = 0;
	while (true) {
		const size_t nl = stdout_buffer.find('\n', start);
		if (nl == std::string::npos) {
			break;
		}
		std::string line = stdout_buffer.substr(start, nl - start);
		start = nl + 1;
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (!line.empty()) {
			handleLine(line);
		}
		if (!process) {
			break; // handleLine may have torn things down
		}
	}
	stdout_buffer.erase(0, start);
}

void ClaudeCodeBackend::handleStreamEvent(const Json& event) {
	std::vector<ClaudeStreamEvent> events;
	ClaudeClient::parseApiEvent(event, events);
	for (const ClaudeStreamEvent& ev : events) {
		using T = ClaudeStreamEvent::Type;
		switch (ev.type) {
			case T::MessageStart:
				usage.input_tokens += ev.input_tokens;
				usage.cache_read_tokens += ev.cache_read_tokens;
				usage.cache_write_tokens += ev.cache_write_tokens;
				usage.requests += 1;
				text_streamed = false;
				blocks.clear();
				break;
			case T::TextDelta:
				if (!ev.text.empty()) {
					text_streamed = true;
					if (listener) {
						listener->onAssistantText(ev.text);
					}
				}
				break;
			case T::ThinkingDelta:
				if (listener && !ev.text.empty()) {
					listener->onThinking(ev.text);
				}
				break;
			case T::ToolUseStart: {
				BlockState& b = blocks[ev.block_index];
				b.name = ev.tool_name;
				b.is_mcp = ev.tool_name.rfind(MCP_TOOL_PREFIX, 0) == 0;
				break;
			}
			case T::ToolInputDelta:
				blocks[ev.block_index].partial_json += ev.text;
				break;
			case T::BlockStop: {
				auto it = blocks.find(ev.block_index);
				if (it != blocks.end()) {
					// MCP tools are reported by the server when they actually run;
					// only Claude Code's own tools (Read/Grep...) are announced here.
					if (!it->second.is_mcp && listener) {
						Json input = Json::parse(it->second.partial_json, nullptr, false);
						listener->onToolCall(it->second.name, input.is_discarded() ? Json::object() : input);
					}
					blocks.erase(it);
				}
				break;
			}
			case T::MessageDelta:
				usage.output_tokens += ev.output_tokens;
				break;
			default:
				break;
		}
	}
}

void ClaudeCodeBackend::handleLine(const std::string& line) {
	Json j = Json::parse(line, nullptr, false);
	if (j.is_discarded() || !j.is_object()) {
		return; // stray log line
	}
	const std::string type = j.value("type", "");

	if (type == "system") {
		if (j.value("subtype", "") == "init") {
			init_seen = true;
			if (j.contains("session_id") && j["session_id"].is_string()) {
				session_id = j["session_id"].get<std::string>();
			}
			if (j.contains("mcp_servers") && j["mcp_servers"].is_array()) {
				for (const auto& server : j["mcp_servers"]) {
					if (server.value("name", "") == McpServer::serverName() && server.value("status", "") != "connected") {
						fail("Claude Code could not connect to the editor's MCP server (status: " + server.value("status", "?") + ").");
					}
				}
			}
		}
		return;
	}
	if ((type == "stream_event" || type == "assistant") && !busy) {
		// A message sent during the last turn was not picked up inside it and is
		// being answered now, as a turn of its own.
		busy = true;
		if (listener) {
			listener->onWorkResumed();
		}
	}
	if (type == "stream_event") {
		if (j.contains("event")) {
			handleStreamEvent(j["event"]);
		}
		return;
	}
	if (type == "assistant") {
		// Fallback when partial messages did not stream the text.
		if (!text_streamed && j.contains("message") && j["message"].contains("content")) {
			for (const auto& block : j["message"]["content"]) {
				if (block.value("type", "") == "text" && listener) {
					listener->onAssistantText(block.value("text", ""));
				}
			}
		}
		text_streamed = false;
		return;
	}
	if (type == "rate_limit_event") {
		// Emitted whenever the subscription windows change; same data as /usage.
		const Json info = j.value("rate_limit_info", Json::object());
		if (info.is_object() && listener) {
			AssistantRateLimit limit;
			limit.type = info.value("rateLimitType", info.value("rate_limit_type", ""));
			limit.status = info.value("status", "");
			if (info.contains("utilization") && info["utilization"].is_number()) {
				limit.utilization = info["utilization"].get<double>();
			}
			const Json& resets = info.contains("resetsAt") ? info["resetsAt"] : info.value("resets_at", Json());
			if (resets.is_number()) {
				limit.resets_at = static_cast<long long>(resets.get<double>());
				if (limit.resets_at > 100000000000LL) {
					limit.resets_at /= 1000; // milliseconds -> seconds
				}
			}
			limit.using_overage = info.value("isUsingOverage", false);
			if (!limit.type.empty()) {
				listener->onRateLimit(limit);
			}
		}
		return;
	}
	if (type == "result") {
		const std::string subtype = j.value("subtype", "success");
		if (j.contains("total_cost_usd") && j["total_cost_usd"].is_number()) {
			reported_cost = j["total_cost_usd"].get<double>();
		}
		if (listener) {
			listener->onUsage(usage);
		}
		busy = false;
		if (subtype == "success" && !j.value("is_error", false)) {
			if (listener) {
				listener->onTurnEnd("end_turn");
			}
		} else {
			std::string message = "Claude Code reported " + subtype;
			if (j.contains("result") && j["result"].is_string()) {
				message += ": " + j["result"].get<std::string>();
			}
			if (listener) {
				listener->onError(message);
			}
		}
		return;
	}
	// "user" (tool results echo) and anything else: nothing to show.
}
