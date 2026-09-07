//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_AI_CLAUDE_CLIENT_H_
#define RME_AI_CLAUDE_CLIENT_H_

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// One parsed server-sent event of a streaming Messages API response, already
// reduced to what the agent loop needs.
struct ClaudeStreamEvent {
	enum class Type {
		MessageStart, // usage.input_tokens / cache counters known
		BlockStart, // block_index / block_type ("text" | "thinking")
		TextDelta, // text: piece of assistant prose
		ThinkingDelta, // text: piece of (summarized) reasoning
		ThinkingSignature, // text: signature to echo back with the thinking block
		RedactedThinking, // text: opaque data blob to echo back unchanged
		ToolUseStart, // tool_id / tool_name / block_index
		ToolInputDelta, // text: partial JSON of the tool input
		BlockStop, // block_index
		MessageDelta, // stop_reason / output_tokens
		MessageDone, // stream finished cleanly
		Error, // text: message (HTTP error body, network failure, API error event)
	};

	Type type = Type::Error;
	std::string text;
	std::string tool_id;
	std::string tool_name;
	int block_index = -1;
	std::string block_type; // "text" | "thinking" | "tool_use" on block starts
	std::string stop_reason;
	int input_tokens = 0;
	int output_tokens = 0;
	int cache_read_tokens = 0;
	int cache_write_tokens = 0;
};

// Streams one Messages API request on a worker thread and delivers the parsed
// events on the main (GUI) thread through wxApp::CallAfter. Cancelling aborts
// the transfer; events already queued after cancel() are dropped.
class ClaudeClient {
public:
	using EventSink = std::function<void(const ClaudeStreamEvent&)>;

	ClaudeClient();
	~ClaudeClient();

	ClaudeClient(const ClaudeClient&) = delete;
	ClaudeClient& operator=(const ClaudeClient&) = delete;

	// Starts a streaming request. Returns false (and reports nothing) when a
	// request is already running or the key is empty. `request_body` must be a
	// complete Messages API body; "stream": true is added here.
	bool start(const std::string& api_key, nlohmann::json request_body, EventSink sink);
	void cancel();
	bool isRunning() const;

	static const char* endpoint();

	// Turns one Messages API streaming event (already parsed JSON, e.g. the
	// "event" object Claude Code forwards in stream_event lines) into
	// ClaudeStreamEvents. Unknown/ping events produce nothing.
	static bool parseApiEvent(const nlohmann::json& event, std::vector<ClaudeStreamEvent>& out);

private:
	struct Shared;
	std::shared_ptr<Shared> shared;
};

#endif
