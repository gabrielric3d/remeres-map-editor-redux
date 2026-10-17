//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_AI_MCP_SERVER_H_
#define RME_AI_MCP_SERVER_H_

#include "ai/claude_tools.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <nlohmann/json.hpp>

// Minimal MCP server (Streamable HTTP transport, JSON responses only) that
// exposes ClaudeTools to a local Claude Code process or any other MCP client.
// Listens on 127.0.0.1 (a random port unless one is given); tools/call is
// marshalled to the GUI thread, where the tools touch the editor, and the HTTP
// thread waits for the result. Requests from browser pages (a foreign Origin)
// are refused, and with a token set every request needs it as a Bearer header.
class McpServer {
public:
	// Called on the GUI thread right before a tool runs (result == nullptr) and
	// right after (result set), so the chat can show the call and its outcome.
	using ToolObserver = std::function<void(const std::string& name, const nlohmann::json& input, const ClaudeToolResult* result)>;

	McpServer();
	~McpServer();

	McpServer(const McpServer&) = delete;
	McpServer& operator=(const McpServer&) = delete;

	// port 0 picks a free one. False when the port cannot be bound.
	bool start(int port = 0);
	void stop();
	bool isRunning() const;
	int port() const;
	// e.g. "http://127.0.0.1:52341/mcp"
	std::string url() const;
	// Name Claude Code sees; tools become "mcp__<name>__<tool>".
	static const char* serverName();

	void setObserver(ToolObserver observer);
	// Empty = no token check (fine for the private server of the chat panel).
	void setAuthToken(const std::string& token);
	// Sent to the client in the initialize result.
	void setInstructions(const std::string& text);

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

#endif
