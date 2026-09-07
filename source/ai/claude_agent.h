//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_AI_CLAUDE_AGENT_H_
#define RME_AI_CLAUDE_AGENT_H_

#include "ai/assistant_backend.h"
#include "ai/claude_client.h"
#include "ai/claude_tools.h"

#include <map>
#include <string>
#include <nlohmann/json.hpp>

// Conversation + agent loop on top of ClaudeClient: keeps the message history,
// streams one assistant turn, executes the tool calls it asks for against the
// editor, feeds the results back and repeats until the model stops. Everything
// happens on the GUI thread (the client marshals its events there).
class ClaudeAgent : public AssistantBackend {
public:
	using Usage = AssistantUsage;
	using Listener = AssistantListener;

	ClaudeAgent();
	~ClaudeAgent() override;

	void setListener(Listener* listener) override {
		this->listener = listener;
	}
	void setApiKey(const std::string& key) {
		api_key = key;
	}
	void setModel(const std::string& model_id) override {
		model = model_id;
	}
	void setEffort(const std::string& effort_level) override {
		effort = effort_level;
	}
	const std::string& getModel() const {
		return model;
	}

	bool isBusy() const override;
	// Appends a user message and runs the agent loop for it.
	void send(const std::string& user_text) override;
	// Aborts the current request; the partial assistant turn is discarded.
	void cancel() override;
	// Forgets the conversation (usage totals are kept).
	void reset() override;

	const Usage& getUsage() const override {
		return usage;
	}
	// Rough USD estimate from the public per-token prices of the current model.
	double estimateCost() const override;
	std::string name() const override {
		return "API";
	}

	// The role/rules part of the system prompt, shared with the Claude Code backend.
	static std::string systemHeader();

	// Files the system prompt is built from (both optional): the Lua API
	// reference and the user's map style guide, looked up in the scripts dir.
	static std::string stylePath();
	static std::string apiReferencePath();

private:
	struct Block {
		std::string type; // text | thinking | redacted_thinking | tool_use
		std::string text;
		std::string signature;
		std::string tool_id;
		std::string tool_name;
		std::string partial_json;
	};

	static constexpr int MAX_TOOL_ROUNDS = 80;

	void startRequest();
	void onEvent(const ClaudeStreamEvent& event);
	void finishAssistantMessage();
	nlohmann::json buildSystem();
	nlohmann::json buildBody();
	void fail(const std::string& message);
	// Drops trailing messages that would make the next request invalid: a user
	// message with no answer, or an assistant tool_use with no tool_result.
	void trimDanglingHistory();

	Listener* listener = nullptr;
	ClaudeClient client;
	std::string api_key;
	std::string model = "claude-opus-5";
	std::string effort = "high";

	nlohmann::json messages = nlohmann::json::array();
	std::map<int, Block> blocks;
	std::string stop_reason;
	int tool_rounds = 0;
	bool busy = false;
	Usage usage;
	std::string cached_system; // rebuilt on reset() so style edits are picked up
};

#endif
