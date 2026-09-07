//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_AI_CLAUDE_TOOLS_H_
#define RME_AI_CLAUDE_TOOLS_H_

#include <string>
#include <nlohmann/json.hpp>

// Result of one tool call executed against the open map. `text` goes back to
// the model as the tool result; `image_png_base64`, when set, is attached as an
// image block so the model can look at the map.
struct ClaudeToolResult {
	std::string text;
	std::string image_png_base64;
	bool is_error = false;
	// One-line description shown in the chat transcript ("painted 120 tiles").
	std::string summary;
};

// The editor-side tools the assistant can call. Everything runs on the main
// thread against the current editor; every mutation goes through the action
// queue, so it is a normal Undo.
class ClaudeTools {
public:
	// Tool definitions in Messages API format (array of {name, description, input_schema}).
	static nlohmann::json definitions();

	// Executes one call. Unknown tools and bad arguments come back as is_error
	// results rather than exceptions, so the model can recover.
	static ClaudeToolResult execute(const std::string& name, const nlohmann::json& input);
};

#endif
