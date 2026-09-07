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
// exposes ClaudeTools to a local Claude Code process. Listens on 127.0.0.1 on
// a random port; tools/call is marshalled to the GUI thread, where the tools
// touch the editor, and the HTTP thread waits for the result.
class McpServer {
public:
	// Called on the GUI thread right before a tool runs (result == nullptr) and
	// right after (result set), so the chat can show the call and its outcome.
	using ToolObserver = std::function<void(const std::string& name, const nlohmann::json& input, const ClaudeToolResult* result)>;

	McpServer();
	~McpServer();

	McpServer(const McpServer&) = delete;
	McpServer& operator=(const McpServer&) = delete;

	bool start();
	void stop();
	bool isRunning() const;
	int port() const;
	// e.g. "http://127.0.0.1:52341/mcp"
	std::string url() const;
	// Name Claude Code sees; tools become "mcp__<name>__<tool>".
	static const char* serverName();

	void setObserver(ToolObserver observer);

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

#endif
