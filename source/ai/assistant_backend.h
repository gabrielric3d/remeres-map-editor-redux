//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_AI_ASSISTANT_BACKEND_H_
#define RME_AI_ASSISTANT_BACKEND_H_

#include "ai/claude_tools.h"

#include <string>
#include <nlohmann/json.hpp>

struct AssistantUsage {
	long long input_tokens = 0;
	long long output_tokens = 0;
	long long cache_read_tokens = 0;
	long long cache_write_tokens = 0;
	int requests = 0;
};

// Subscription rate-limit window as reported by Claude Code (the same data as
// its /usage screen): "five_hour" is the rolling session window, "seven_day" the
// weekly one (model-specific variants like "seven_day_opus" also exist).
struct AssistantRateLimit {
	std::string type; // five_hour | seven_day | seven_day_opus | ...
	std::string status; // allowed | allowed_warning | rejected
	double utilization = 0.0; // 0..1
	long long resets_at = 0; // unix seconds, 0 = unknown
	bool using_overage = false;
};

// What the chat panel wants to hear from whichever brain is driving the
// conversation. Every call happens on the GUI thread.
class AssistantListener {
public:
	virtual ~AssistantListener() = default;
	// Plan usage changed (Claude Code backend only; the API has no such window).
	virtual void onRateLimit(const AssistantRateLimit&) { }
	virtual void onAssistantText(const std::string& delta) = 0;
	virtual void onThinking(const std::string& delta) = 0;
	virtual void onToolCall(const std::string& name, const nlohmann::json& input) = 0;
	virtual void onToolResult(const std::string& name, const ClaudeToolResult& result) = 0;
	// A whole user turn finished (no more tool calls). stop_reason as sent by the API.
	virtual void onTurnEnd(const std::string& stop_reason) = 0;
	virtual void onError(const std::string& message) = 0;
	// Usage/cost changed (after every request).
	virtual void onUsage(const AssistantUsage& usage) = 0;
};

// A conversation engine the panel can talk to: the direct Messages API
// (ClaudeAgent, billed per token with an API key) or a local Claude Code
// process (ClaudeCodeBackend, covered by the user's subscription).
class AssistantBackend {
public:
	virtual ~AssistantBackend() = default;

	virtual void setListener(AssistantListener* listener) = 0;
	virtual void setModel(const std::string& model_id) = 0;
	virtual void setEffort(const std::string& effort) = 0;

	virtual bool isBusy() const = 0;
	// Appends a user message and runs the turn (tools included) until the model stops.
	virtual void send(const std::string& user_text) = 0;
	// Aborts the current turn.
	virtual void cancel() = 0;
	// Forgets the conversation.
	virtual void reset() = 0;

	virtual const AssistantUsage& getUsage() const = 0;
	// USD spent so far (estimated for the API, reported for Claude Code).
	virtual double estimateCost() const = 0;
	// Short label for the status line, e.g. "API" or "Claude Code".
	virtual std::string name() const = 0;
};

#endif
