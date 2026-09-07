//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ai/claude_agent.h"

#include "lua/lua_script_manager.h"

#include <fstream>
#include <sstream>
#include <wx/filename.h>

using Json = nlohmann::json;

namespace {
	constexpr int MAX_OUTPUT_TOKENS = 32000;

	std::string readTextFile(const std::string& path) {
		std::ifstream in(path, std::ios::binary);
		if (!in.is_open()) {
			return "";
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	const char* SYSTEM_HEADER = R"(You are Claude, working inside Remere's Map Editor (RME Redux), an editor for Tibia-style OTBM maps. You are talking with the mapper through a chat panel docked in the editor, and you can act on the open map with tools. The mapper is a map designer, not necessarily a programmer, and usually writes in Portuguese; answer in the language they use.

## How to work
- Start a task by calling get_editor_state, then read_region or screenshot around the area the user is talking about. Never assume what is on the map.
- Brush names must match exactly. Use list_brushes (with a filter) before painting with a brush you have not seen in this conversation.
- Prefer the `paint` tool for terrain, walls, carpets and doodads: it runs the editor's real brush pipeline, so auto-borders and composites come out right. Use `run_lua` for procedural work (noise, cellular automata, scatter, rooms) and for anything the other tools cannot express.
- Build large things in stages: rough shape first, look at it (screenshot / read_region), then borders, then decoration. Check your work visually with `screenshot` before you say you are done.
- Every paint/run_lua call is one undo step. If a step came out wrong, call `undo` and try again instead of painting over it.
- Only touch the area the user asked about. Do not modify other floors or distant parts of the map unless asked. For destructive or very large operations (thousands of tiles), say what you are about to do first.
- Be concise in chat. Describe what you did in a few lines and point out anything the user should check.

## Coordinates
- x grows to the right, y grows downwards, z is the floor: 7 is ground level, 6..0 are above ground (lower number = higher floor), 8..15 are underground.
- A tile is 32x32 pixels on screenshots. Screenshots show the current view only; the tool result tells you the visible size and the approximate top-left tile.

## Lua notes (run_lua)
- The full scripting reference follows below. Key points: `app.map:getTile(x, y, z)` / `getOrCreateTile`, `tile.ground = itemId`, `tile:addItem(id)`, `tile:borderize()` after changing grounds by id, `tile:wallize()` after placing walls by id, `Brushes.get(name)`, `noise.*`, `algo.*`, `geo.*`.
- Edits are wrapped in app.transaction automatically unless you call it yourself; keep one transaction per logical step so undo stays meaningful.
- Use print() to report counts or positions you will need next; the output is returned to you.
- Painting by item id skips auto-border unless you call tile:borderize() on the changed tiles and their neighbours. When you need borders, prefer the `paint` tool with the ground brush.
)";
}

ClaudeAgent::ClaudeAgent() = default;
ClaudeAgent::~ClaudeAgent() = default;

std::string ClaudeAgent::systemHeader() {
	return SYSTEM_HEADER;
}

std::string ClaudeAgent::stylePath() {
	return g_luaScripts.getScriptsDirectory() + wxString(wxFileName::GetPathSeparator()).ToStdString() + "claude_style.md";
}

std::string ClaudeAgent::apiReferencePath() {
	return g_luaScripts.getScriptsDirectory() + wxString(wxFileName::GetPathSeparator()).ToStdString() + "README.md";
}

bool ClaudeAgent::isBusy() const {
	return busy;
}

double ClaudeAgent::estimateCost() const {
	double in = 5.0, out = 25.0; // USD per million tokens
	if (model.find("sonnet") != std::string::npos) {
		in = 2.0;
		out = 10.0;
	} else if (model.find("fable") != std::string::npos) {
		in = 10.0;
		out = 50.0;
	} else if (model.find("haiku") != std::string::npos) {
		in = 1.0;
		out = 5.0;
	}
	const double cache_read = in * 0.1;
	const double cache_write = in * 1.25;
	return (usage.input_tokens * in + usage.output_tokens * out + usage.cache_read_tokens * cache_read + usage.cache_write_tokens * cache_write) / 1'000'000.0;
}

Json ClaudeAgent::buildSystem() {
	if (cached_system.empty()) {
		std::string text = SYSTEM_HEADER;
		const std::string style = readTextFile(stylePath());
		if (!style.empty()) {
			text += "\n## Map style guide (written by the mapper; follow it)\n" + style + "\n";
		}
		const std::string reference = readTextFile(apiReferencePath());
		if (!reference.empty()) {
			text += "\n## Lua scripting API reference\n" + reference + "\n";
		} else {
			text += "\n(The Lua API reference file scripts/README.md was not found; rely on the notes above.)\n";
		}
		cached_system = text;
	}
	// One block, cached: the reference is long and identical on every request.
	return Json::array({ { { "type", "text" }, { "text", cached_system }, { "cache_control", { { "type", "ephemeral" } } } } });
}

Json ClaudeAgent::buildBody() {
	Json body;
	body["model"] = model;
	body["max_tokens"] = MAX_OUTPUT_TOKENS;
	body["system"] = buildSystem();
	body["messages"] = messages;
	body["tools"] = ClaudeTools::definitions();
	body["thinking"] = { { "type", "adaptive" }, { "display", "summarized" } };
	if (!effort.empty()) {
		body["output_config"] = { { "effort", effort } };
	}
	return body;
}

void ClaudeAgent::send(const std::string& user_text) {
	if (busy) {
		return;
	}
	if (api_key.empty()) {
		fail("No API key configured. Click 'API key' and paste your Anthropic key (or set ANTHROPIC_API_KEY).");
		return;
	}
	messages.push_back({ { "role", "user" }, { "content", Json::array({ { { "type", "text" }, { "text", user_text } } }) } });
	tool_rounds = 0;
	startRequest();
}

void ClaudeAgent::startRequest() {
	blocks.clear();
	stop_reason.clear();
	busy = true;
	const bool started = client.start(api_key, buildBody(), [this](const ClaudeStreamEvent& event) {
		onEvent(event);
	});
	if (!started) {
		fail("Could not start the request (another one is still running?).");
	}
}

void ClaudeAgent::trimDanglingHistory() {
	while (!messages.empty()) {
		const Json& last = messages.back();
		if (last["role"] == "user") {
			messages.erase(messages.end() - 1);
			continue;
		}
		bool has_tool_use = false;
		if (last.contains("content") && last["content"].is_array()) {
			for (const auto& block : last["content"]) {
				if (block.value("type", "") == "tool_use") {
					has_tool_use = true;
					break;
				}
			}
		}
		if (has_tool_use) {
			messages.erase(messages.end() - 1);
			continue;
		}
		break;
	}
}

void ClaudeAgent::cancel() {
	if (!busy) {
		return;
	}
	client.cancel();
	blocks.clear();
	busy = false;
	trimDanglingHistory();
	if (listener) {
		listener->onTurnEnd("cancelled");
	}
}

void ClaudeAgent::reset() {
	cancel();
	messages = Json::array();
	blocks.clear();
	cached_system.clear();
}

void ClaudeAgent::fail(const std::string& message) {
	busy = false;
	blocks.clear();
	trimDanglingHistory();
	if (listener) {
		listener->onError(message);
	}
}

void ClaudeAgent::onEvent(const ClaudeStreamEvent& event) {
	using T = ClaudeStreamEvent::Type;
	switch (event.type) {
		case T::MessageStart:
			usage.input_tokens += event.input_tokens;
			usage.cache_read_tokens += event.cache_read_tokens;
			usage.cache_write_tokens += event.cache_write_tokens;
			usage.requests += 1;
			break;
		case T::BlockStart:
			blocks[event.block_index].type = event.block_type;
			break;
		case T::TextDelta: {
			Block& b = blocks[event.block_index];
			if (b.type.empty()) {
				b.type = "text";
			}
			b.text += event.text;
			if (listener && !event.text.empty()) {
				listener->onAssistantText(event.text);
			}
			break;
		}
		case T::ThinkingDelta: {
			Block& b = blocks[event.block_index];
			b.type = "thinking";
			b.text += event.text;
			if (listener && !event.text.empty()) {
				listener->onThinking(event.text);
			}
			break;
		}
		case T::ThinkingSignature:
			blocks[event.block_index].signature += event.text;
			break;
		case T::RedactedThinking: {
			Block& b = blocks[event.block_index];
			b.type = "redacted_thinking";
			b.text = event.text;
			break;
		}
		case T::ToolUseStart: {
			Block& b = blocks[event.block_index];
			b.type = "tool_use";
			b.tool_id = event.tool_id;
			b.tool_name = event.tool_name;
			break;
		}
		case T::ToolInputDelta:
			blocks[event.block_index].partial_json += event.text;
			break;
		case T::BlockStop:
			break;
		case T::MessageDelta:
			if (!event.stop_reason.empty()) {
				stop_reason = event.stop_reason;
			}
			usage.output_tokens += event.output_tokens;
			break;
		case T::MessageDone:
			if (listener) {
				listener->onUsage(usage);
			}
			finishAssistantMessage();
			break;
		case T::Error:
			fail(event.text);
			break;
	}
}

void ClaudeAgent::finishAssistantMessage() {
	Json content = Json::array();
	std::vector<std::pair<std::string, Json>> tool_calls; // (id, input) in order, with names
	std::vector<std::string> tool_names;
	for (const auto& [index, b] : blocks) {
		if (b.type == "text") {
			if (!b.text.empty()) {
				content.push_back({ { "type", "text" }, { "text", b.text } });
			}
		} else if (b.type == "thinking") {
			Json block = { { "type", "thinking" }, { "thinking", b.text } };
			if (!b.signature.empty()) {
				block["signature"] = b.signature;
			}
			content.push_back(block);
		} else if (b.type == "redacted_thinking") {
			content.push_back({ { "type", "redacted_thinking" }, { "data", b.text } });
		} else if (b.type == "tool_use") {
			Json input = Json::object();
			if (!b.partial_json.empty()) {
				Json parsed = Json::parse(b.partial_json, nullptr, false);
				if (!parsed.is_discarded() && parsed.is_object()) {
					input = parsed;
				}
			}
			content.push_back({ { "type", "tool_use" }, { "id", b.tool_id }, { "name", b.tool_name }, { "input", input } });
			tool_calls.emplace_back(b.tool_id, input);
			tool_names.push_back(b.tool_name);
		}
	}
	blocks.clear();
	if (content.empty()) {
		// Nothing usable came back; keep the history consistent and stop.
		fail("The model returned an empty response.");
		return;
	}
	messages.push_back({ { "role", "assistant" }, { "content", content } });

	if (stop_reason != "tool_use" || tool_calls.empty()) {
		busy = false;
		if (listener) {
			listener->onTurnEnd(stop_reason.empty() ? "end_turn" : stop_reason);
		}
		return;
	}

	if (++tool_rounds > MAX_TOOL_ROUNDS) {
		fail("Stopped after " + std::to_string(MAX_TOOL_ROUNDS) + " tool rounds in one turn. Send another message to continue.");
		return;
	}

	// Execute every call and answer all of them in a single user message.
	Json results = Json::array();
	for (size_t i = 0; i < tool_calls.size(); ++i) {
		const std::string& name = tool_names[i];
		const Json& input = tool_calls[i].second;
		if (listener) {
			listener->onToolCall(name, input);
		}
		const ClaudeToolResult result = ClaudeTools::execute(name, input);
		if (listener) {
			listener->onToolResult(name, result);
		}
		Json block = { { "type", "tool_result" }, { "tool_use_id", tool_calls[i].first } };
		if (result.is_error) {
			block["is_error"] = true;
		}
		if (!result.image_png_base64.empty()) {
			block["content"] = Json::array({
				{ { "type", "text" }, { "text", result.text } },
				{ { "type", "image" }, { "source", { { "type", "base64" }, { "media_type", "image/png" }, { "data", result.image_png_base64 } } } },
			});
		} else {
			block["content"] = result.text;
		}
		results.push_back(block);
	}
	messages.push_back({ { "role", "user" }, { "content", results } });
	startRequest();
}
