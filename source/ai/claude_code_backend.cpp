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
#include <wx/filename.h>
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
	mcp.setObserver([this](const std::string& name, const Json& input, const ClaudeToolResult* result) {
		if (!listener) {
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
	const wxString home = wxGetHomeDir();
	wxString appdata;
	wxGetEnv("APPDATA", &appdata);
	const wxString candidates[] = {
		home + wxFILE_SEP_PATH + ".local" + wxFILE_SEP_PATH + "bin" + wxFILE_SEP_PATH + "claude.exe",
		home + wxFILE_SEP_PATH + ".local" + wxFILE_SEP_PATH + "bin" + wxFILE_SEP_PATH + "claude",
		appdata + wxFILE_SEP_PATH + "npm" + wxFILE_SEP_PATH + "claude.cmd",
	};
	for (const wxString& candidate : candidates) {
		if (wxFileName::FileExists(candidate)) {
			return std::string(candidate.ToUTF8());
		}
	}
	return "claude";
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
	prompt += "- You are running through Claude Code with the editor exposed as the MCP server \"" + std::string(McpServer::serverName()) + "\": the editor tools are " + MCP_TOOL_PREFIX + "get_editor_state, " + MCP_TOOL_PREFIX + "list_brushes, " + MCP_TOOL_PREFIX + "read_region, " + MCP_TOOL_PREFIX + "paint, " + MCP_TOOL_PREFIX + "run_lua, " + MCP_TOOL_PREFIX + "screenshot, " + MCP_TOOL_PREFIX + "set_view and " + MCP_TOOL_PREFIX + "undo. Only Read/Glob/Grep are available besides them; never try to edit files.\n";
	prompt += "- The Lua scripting API reference is the file `" + ClaudeAgent::apiReferencePath() + "`. Read the relevant sections before writing run_lua code; do not guess function names.\n";
	const std::string style_path = ClaudeAgent::stylePath();
	if (wxFileName::FileExists(wxString::FromUTF8(style_path))) {
		prompt += "- The mapper's style guide is `" + style_path + "`. Read it at the start of a task and follow it.\n";
	}
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
	cmd += " --tools Read,Glob,Grep";
	cmd += " --allowedTools mcp__" + std::string(McpServer::serverName()) + ",Read,Glob,Grep";
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

	init_seen = false;
	stdout_buffer.clear();
	stderr_buffer.clear();
	blocks.clear();

	process = new Process(this);
	wxExecuteEnv env;
	env.cwd = wxString::FromUTF8(g_luaScripts.getScriptsDirectory());
	const std::string cmd = buildCommand();
	spdlog::info("ClaudeCodeBackend: {}", cmd);
	pid = wxExecute(wxString::FromUTF8(cmd), wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE, process, &env);
	if (pid <= 0) {
		delete process;
		process = nullptr;
		pid = 0;
		fail("Could not launch Claude Code. Is it installed? Set the path in Preferences (or PATH) and check `claude --version` in a terminal.");
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

void ClaudeCodeBackend::send(const std::string& user_text) {
	if (busy) {
		return;
	}
	busy = true;
	if (!ensureProcess()) {
		return;
	}
	Json message = { { "type", "user" }, { "message", { { "role", "user" }, { "content", Json::array({ { { "type", "text" }, { "text", user_text } } }) } } } };
	const std::string line = message.dump() + "\n";
	wxOutputStream* stdin_stream = process->GetOutputStream();
	if (!stdin_stream) {
		fail("Claude Code's stdin is not available.");
		return;
	}
	stdin_stream->Write(line.data(), line.size());
	text_streamed = false;
	blocks.clear();
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
	pumpOutput();
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
