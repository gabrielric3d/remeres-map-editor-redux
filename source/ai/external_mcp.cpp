//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ai/external_mcp.h"

#include "ai/claude_agent.h"
#include "ai/mcp_server.h"
#include "app/settings.h"

#include <algorithm>
#include <memory>
#include <random>
#include <spdlog/spdlog.h>
#include <wx/filename.h>

namespace {
	std::unique_ptr<McpServer> server;
	int server_port = 0;
	std::string last_error;

	std::string randomToken() {
		std::random_device device;
		std::mt19937_64 rng((static_cast<uint64_t>(device()) << 32) ^ device());
		std::uniform_int_distribution<int> digit(0, 15);
		const char* hex = "0123456789abcdef";
		std::string out(40, '0');
		for (char& c : out) {
			c = hex[digit(rng)];
		}
		return out;
	}

	// What an outside client learns about the editor when it connects: the
	// same working rules the chat panel's prompt has, plus where the Lua
	// reference and the mapper's style guide live.
	std::string instructions() {
		std::string text = "These tools read and edit the map open in Remere's Map Editor (RME Redux), an editor for Tibia-style OTBM maps. The mapper watches the editor while you work: paint is animated, the camera follows you and a \"Claude\" cursor marks where you act.\n\n";
		const std::string header = ClaudeAgent::systemHeader();
		const size_t from = header.find("## How to work");
		if (from != std::string::npos) {
			text += header.substr(from);
		}
		text += "\n## Files\n";
		text += "- Lua scripting API reference: `" + ClaudeAgent::apiReferencePath() + "`. Read the relevant sections before writing run_lua code; do not guess function names.\n";
		const std::string style = ClaudeAgent::stylePath();
		if (wxFileName::FileExists(wxString::FromUTF8(style))) {
			text += "- The mapper's style guide: `" + style + "`. Read it at the start of a task and follow it.\n";
		}
		return text;
	}
}

namespace ExternalMcp {
	std::string token() {
		std::string value = g_settings.getString(Config::CLAUDE_MCP_TOKEN);
		if (value.empty()) {
			value = randomToken();
			g_settings.setString(Config::CLAUDE_MCP_TOKEN, value);
		}
		return value;
	}

	bool applySettings() {
		const bool wanted = g_settings.getBoolean(Config::CLAUDE_MCP_EXTERNAL);
		const int port = std::clamp(g_settings.getInteger(Config::CLAUDE_MCP_PORT), 1, 65535);
		if (!wanted) {
			stop();
			last_error.clear();
			return true;
		}
		if (server && server->isRunning() && server_port == port) {
			server->setAuthToken(token());
			return true;
		}
		stop();
		server = std::make_unique<McpServer>();
		server->setAuthToken(token());
		server->setInstructions(instructions());
		if (!server->start(port)) {
			server.reset();
			last_error = "Could not listen on 127.0.0.1:" + std::to_string(port) + " (is another program using that port?)";
			spdlog::warn("ExternalMcp: {}", last_error);
			return false;
		}
		server_port = port;
		last_error.clear();
		return true;
	}

	void stop() {
		if (server) {
			server->stop();
			server.reset();
		}
		server_port = 0;
	}

	bool isRunning() {
		return server && server->isRunning();
	}

	std::string url() {
		const int port = isRunning() ? server_port : std::clamp(g_settings.getInteger(Config::CLAUDE_MCP_PORT), 1, 65535);
		return "http://127.0.0.1:" + std::to_string(port) + "/mcp";
	}

	std::string lastError() {
		return last_error;
	}

	std::string claudeAddCommand() {
		return "claude mcp add --scope user --transport http rme " + url() + " --header \"Authorization: Bearer " + token() + "\"";
	}
}
