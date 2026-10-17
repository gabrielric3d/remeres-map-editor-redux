//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_AI_EXTERNAL_MCP_H_
#define RME_AI_EXTERNAL_MCP_H_

#include <string>

// The editor tools served to programs outside the editor (Claude Code in a
// terminal or IDE, Claude Desktop through a stdio bridge such as mcp-remote)
// on a fixed localhost port, guarded by a Bearer token. Independent from the
// private server the chat panel gives its own Claude Code process.
// GUI thread only.
namespace ExternalMcp {
	// Starts or stops the server to match CLAUDE_MCP_EXTERNAL / CLAUDE_MCP_PORT
	// (restarting on a port change). False when it should run but could not.
	bool applySettings();
	void stop();
	bool isRunning();
	std::string url();
	// Why the last start failed ("" when it did not).
	std::string lastError();
	// The token clients must send; generated and saved the first time.
	std::string token();
	// Ready-to-paste command that registers the server in Claude Code.
	std::string claudeAddCommand();
}

#endif
