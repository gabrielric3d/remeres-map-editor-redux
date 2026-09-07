//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_AI_CLAUDE_CODE_BACKEND_H_
#define RME_AI_CLAUDE_CODE_BACKEND_H_

#include "ai/assistant_backend.h"
#include "ai/mcp_server.h"

#include <map>
#include <string>
#include <wx/event.h>
#include <wx/process.h>
#include <wx/timer.h>

// Drives a local `claude` (Claude Code) process in headless streaming mode:
// user messages go in as stream-json on stdin, events come back on stdout, and
// the editor tools are served to it over MCP by McpServer. Runs on the user's
// Claude subscription, so no API key is needed.
class ClaudeCodeBackend : public wxEvtHandler, public AssistantBackend {
public:
	ClaudeCodeBackend();
	~ClaudeCodeBackend() override;

	void setListener(AssistantListener* listener) override {
		this->listener = listener;
	}
	void setModel(const std::string& model_id) override {
		model = model_id;
	}
	void setEffort(const std::string& effort_level) override {
		effort = effort_level;
	}
	bool isBusy() const override {
		return busy;
	}
	void send(const std::string& user_text) override;
	void cancel() override;
	void reset() override;
	const AssistantUsage& getUsage() const override {
		return usage;
	}
	double estimateCost() const override {
		return reported_cost;
	}
	std::string name() const override {
		return "Claude Code";
	}

	// Path of the claude executable: the CLAUDE_CODE_PATH setting, else the usual
	// install locations, else plain "claude" (PATH lookup). Empty when nothing found.
	static std::string resolveExecutable();

private:
	class Process : public wxProcess {
	public:
		explicit Process(ClaudeCodeBackend* owner) :
			owner(owner) {
			Redirect();
		}
		void OnTerminate(int pid, int status) override;
		ClaudeCodeBackend* owner;
	};

	bool ensureProcess();
	void killProcess(bool keep_session);
	void onProcessExited(int status);
	void onTimer(wxTimerEvent& event);
	void pumpOutput();
	void handleLine(const std::string& line);
	void handleStreamEvent(const nlohmann::json& event);
	bool writeSupportFiles();
	std::string buildCommand() const;
	void fail(const std::string& message);

	AssistantListener* listener = nullptr;
	McpServer mcp;
	Process* process = nullptr;
	long pid = 0;
	wxTimer timer;

	std::string model = "claude-opus-5";
	std::string effort = "high";
	std::string session_id; // UUID we assign; reused with --resume after a kill
	bool resume_next = false;
	bool busy = false;
	bool init_seen = false;
	AssistantUsage usage;
	double reported_cost = 0.0;

	std::string stdout_buffer;
	std::string stderr_buffer;
	std::string system_prompt_path;
	std::string mcp_config_path;

	// Per-message stream state (tool_use blocks from Claude Code's own tools).
	struct BlockState {
		std::string name;
		std::string partial_json;
		bool is_mcp = false;
	};
	std::map<int, BlockState> blocks;
	bool text_streamed = false;
};

#endif
