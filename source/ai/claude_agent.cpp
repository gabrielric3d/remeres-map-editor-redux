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
- Build RAW. This mapper's style (irregular edges, borders broken and layered by hand, grounds of a similar colour laid under each other) cannot come out of auto-border, so grounds, border pieces, walls, mountains and details all go in by item id with `place`. Get the right piece for each direction from `list_borders` instead of guessing ids, and read the mapper's own areas with `read_region` raw=true to see how they are built piece by piece before you build something similar.
- Only use `paint` (brushes with auto-border) when the mapper asks for it. Use `run_lua` for procedural work (noise, scatter, cellular automata) that ends in raw item ids too.
- Borders: for every patch of ground start from `border_pieces` (the complete standard border of the set, sides, corners and diagonals), place it, then break or layer it by hand where the style asks. Never leave two grounds touching with no border piece.
- Build large things in stages: rough shape first, look at it, then borders, then decoration.
- GROUNDING view: while you lay grounds and borders, call `set_display` with flat_colors "on" and ground_only true. Every colour family turns flat, so straight edges, missing corners and seams show at once in your screenshots and on the mapper's screen. Turn both off when you move on to details (nature, objects, decoration).
- Before you say you are done: run `check_region` over everything you changed and fix what it reports, then look at it with `screenshot` zoom 1 and width/height around the area (a whole-screen screenshot is scaled down so much that missing pieces are invisible). Compare it with the mapper's own areas you read.
- Looking: `set_view` moves AND zooms the mapper's view. Zoom in (zoom 0.5, or width/height of a small area) to study details, borders and single tiles; zoom out (zoom 2-4, or width/height of the whole area) to judge shapes and the overall layout. When you need to know exactly which tile something is on, turn on the tile grid (Shift+G): `set_display` grid true for the whole task (turn it off when you finish) or `screenshot` grid true for one picture.
- Every place/paint/run_lua call is one undo step. If a step came out wrong, call `undo` and try again instead of building over it.
- Only touch the area the user asked about. Do not modify other floors or distant parts of the map unless asked. For destructive or very large operations (thousands of tiles), say what you are about to do first.
- Be concise in chat. Describe what you did in a few lines and point out anything the user should check.

## Coordinates
- x grows to the right, y grows downwards, z is the floor: 7 is ground level, 6..0 are above ground (lower number = higher floor), 8..15 are underground.
- A tile is 32x32 pixels at zoom 1 (64 at zoom 0.5, 16 at zoom 2). Screenshots show the current view only; the tool result tells you the visible size, the pixels per tile and the pixel where a known tile starts.

## Lua notes (run_lua)
- The full scripting reference follows below. Key points: `app.map:getTile(x, y, z)` / `getOrCreateTile`, `tile.ground = itemId`, `tile:addItem(id)`, `tile:borderize()` after changing grounds by id, `tile:wallize()` after placing walls by id, `Brushes.get(name)`, `noise.*`, `algo.*`, `geo.*`.
- Edits are wrapped in app.transaction automatically unless you call it yourself; keep one transaction per logical step so undo stays meaningful.
- Use print() to report counts or positions you will need next; the output is returned to you.
- Edits by item id are raw like `place`. Never call tile:borderize() or tile:wallize() on the mapper's areas: they replace hand-made borders and walls with automatic ones.
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
	const bool opus_5_5 = model.find("opus-5-5") != std::string::npos;
	if (opus_5_5) {
		in = 4.0;
		out = 20.0;
	} else if (model.find("sonnet") != std::string::npos) {
		in = 2.0;
		out = 10.0;
	} else if (model.find("fable") != std::string::npos) {
		in = 10.0;
		out = 50.0;
	} else if (model.find("haiku") != std::string::npos) {
		in = 1.0;
		out = 5.0;
	}
	const double cache_read = opus_5_5 ? 0.20 : in * 0.1;
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

void ClaudeAgent::send(const std::string& user_text, const std::vector<AssistantImage>& images) {
	const Json content = assistantUserContent(user_text, images);
	if (content.empty()) {
		return;
	}
	if (busy) {
		for (const auto& block : content) {
			pending_user_content.push_back(block);
		}
		return;
	}
	if (api_key.empty()) {
		fail("No API key configured. Click 'API key' and paste your Anthropic key (or set ANTHROPIC_API_KEY).");
		return;
	}
	messages.push_back({ { "role", "user" }, { "content", content } });
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
	dropPendingTools();
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
	dropPendingTools();
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
		if (!pending_user_content.empty()) {
			// The mapper wrote while this answer was streaming: that is the next turn.
			messages.push_back({ { "role", "user" }, { "content", pending_user_content } });
			pending_user_content = Json::array();
			tool_rounds = 0;
			if (listener) {
				listener->onWorkResumed();
			}
			startRequest();
			return;
		}
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

	// Execute every call, one after the other (a paint may take a moment while
	// it animates), and answer all of them in a single user message.
	pending_tools.clear();
	for (size_t i = 0; i < tool_calls.size(); ++i) {
		pending_tools.push_back({ tool_calls[i].first, tool_names[i], tool_calls[i].second });
	}
	pending_results = Json::array();
	next_tool = 0;
	runNextTool();
}

void ClaudeAgent::runNextTool() {
	if (next_tool >= pending_tools.size()) {
		// Whatever the mapper sent while the tools ran rides along, after the
		// results (the API wants tool_result blocks first).
		for (const auto& block : pending_user_content) {
			pending_results.push_back(block);
		}
		pending_user_content = Json::array();
		messages.push_back({ { "role", "user" }, { "content", pending_results } });
		pending_tools.clear();
		pending_results = Json::array();
		next_tool = 0;
		startRequest();
		return;
	}
	const PendingTool& call = pending_tools[next_tool];
	if (listener) {
		listener->onToolCall(call.name, call.input);
	}
	const std::weak_ptr<int> alive = alive_token;
	const unsigned generation = tool_generation;
	ClaudeTools::executeAsync(call.name, call.input, [this, alive, generation](ClaudeToolResult result) {
		// Ignore a tool that finishes after cancel/reset or after we are gone.
		if (alive.expired() || generation != tool_generation || !busy) {
			return;
		}
		onToolDone(std::move(result));
	});
}

void ClaudeAgent::onToolDone(ClaudeToolResult result) {
	if (next_tool >= pending_tools.size()) {
		return;
	}
	const PendingTool& call = pending_tools[next_tool];
	if (listener) {
		listener->onToolResult(call.name, result);
	}
	Json block = { { "type", "tool_result" }, { "tool_use_id", call.id } };
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
	pending_results.push_back(block);
	++next_tool;
	runNextTool();
}

void ClaudeAgent::dropPendingTools() {
	++tool_generation;
	pending_tools.clear();
	pending_results = Json::array();
	pending_user_content = Json::array(); // still in the transcript; the mapper can send it again
	next_tool = 0;
}
