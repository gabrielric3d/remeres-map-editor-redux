//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ai/claude_client.h"

#include <cpr/cpr.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <wx/app.h>

using Json = nlohmann::json;

namespace {
	constexpr const char* API_URL = "https://api.anthropic.com/v1/messages";
	constexpr const char* API_VERSION = "2023-06-01";
	// A single Fable/Opus turn on a hard task can run for minutes.
	constexpr long REQUEST_TIMEOUT_MS = 20 * 60 * 1000;
}

struct ClaudeClient::Shared {
	std::atomic<bool> cancelled { false };
	std::atomic<bool> alive { true }; // false once the owning ClaudeClient is gone
	std::atomic<bool> running { false };
	std::atomic<uint64_t> generation { 0 }; // bumps per start(); stale events are dropped
	EventSink sink;

	// Queues the event for the GUI thread. `self` keeps the block alive until the
	// queued lambda ran, even if the ClaudeClient was destroyed meanwhile.
	static void dispatch(const std::shared_ptr<Shared>& self, uint64_t gen, ClaudeStreamEvent event) {
		if (!wxTheApp) {
			return;
		}
		wxTheApp->CallAfter([self, gen, event = std::move(event)]() {
			if (!self->alive || self->cancelled || self->generation != gen || !self->sink) {
				return;
			}
			self->sink(event);
		});
	}
};

namespace {
	// Incremental SSE parser: feeds raw bytes, emits (event, data) pairs.
	class SseParser {
	public:
		template <typename Fn>
		void feed(std::string_view chunk, Fn&& on_event) {
			buffer.append(chunk);
			size_t pos = 0;
			while (true) {
				const size_t nl = buffer.find('\n', pos);
				if (nl == std::string::npos) {
					break;
				}
				std::string line = buffer.substr(pos, nl - pos);
				pos = nl + 1;
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				if (line.empty()) {
					if (!data.empty()) {
						on_event(event_name, data);
					}
					event_name.clear();
					data.clear();
					continue;
				}
				if (line[0] == ':') {
					continue; // comment / keep-alive
				}
				const size_t colon = line.find(':');
				std::string field = line.substr(0, colon);
				std::string value = colon == std::string::npos ? "" : line.substr(colon + 1);
				if (!value.empty() && value[0] == ' ') {
					value.erase(0, 1);
				}
				if (field == "event") {
					event_name = value;
				} else if (field == "data") {
					if (!data.empty()) {
						data += '\n';
					}
					data += value;
				}
			}
			buffer.erase(0, pos);
		}

	private:
		std::string buffer;
		std::string event_name;
		std::string data;
	};

	bool parseEvent(const std::string& name, const std::string& data, std::vector<ClaudeStreamEvent>& out) {
		Json j = Json::parse(data, nullptr, false);
		if (j.is_discarded()) {
			return false;
		}
		if (!j.contains("type") && !name.empty()) {
			j["type"] = name;
		}
		return ClaudeClient::parseApiEvent(j, out);
	}
}

bool ClaudeClient::parseApiEvent(const Json& j, std::vector<ClaudeStreamEvent>& out) {
	{
		const std::string type = j.value("type", "");
		ClaudeStreamEvent ev;
		if (type == "message_start") {
			ev.type = ClaudeStreamEvent::Type::MessageStart;
			const Json& usage = j["message"]["usage"];
			ev.input_tokens = usage.value("input_tokens", 0);
			ev.cache_read_tokens = usage.value("cache_read_input_tokens", 0);
			ev.cache_write_tokens = usage.value("cache_creation_input_tokens", 0);
			out.push_back(ev);
		} else if (type == "content_block_start") {
			const Json& block = j["content_block"];
			const std::string btype = block.value("type", "");
			ev.block_index = j.value("index", -1);
			ev.block_type = btype;
			if (btype == "tool_use") {
				ev.type = ClaudeStreamEvent::Type::ToolUseStart;
				ev.tool_id = block.value("id", "");
				ev.tool_name = block.value("name", "");
				out.push_back(ev);
			} else if (btype == "redacted_thinking") {
				ev.type = ClaudeStreamEvent::Type::RedactedThinking;
				ev.text = block.value("data", "");
				out.push_back(ev);
			} else if (btype == "text" || btype == "thinking") {
				// Announces the block kind so the agent can assemble it from the deltas.
				ev.type = ClaudeStreamEvent::Type::BlockStart;
				out.push_back(ev);
			}
		} else if (type == "content_block_delta") {
			const Json& delta = j["delta"];
			const std::string dtype = delta.value("type", "");
			ev.block_index = j.value("index", -1);
			if (dtype == "text_delta") {
				ev.type = ClaudeStreamEvent::Type::TextDelta;
				ev.text = delta.value("text", "");
				out.push_back(ev);
			} else if (dtype == "input_json_delta") {
				ev.type = ClaudeStreamEvent::Type::ToolInputDelta;
				ev.text = delta.value("partial_json", "");
				out.push_back(ev);
			} else if (dtype == "thinking_delta") {
				ev.type = ClaudeStreamEvent::Type::ThinkingDelta;
				ev.text = delta.value("thinking", "");
				out.push_back(ev);
			} else if (dtype == "signature_delta") {
				ev.type = ClaudeStreamEvent::Type::ThinkingSignature;
				ev.text = delta.value("signature", "");
				out.push_back(ev);
			}
		} else if (type == "content_block_stop") {
			ev.type = ClaudeStreamEvent::Type::BlockStop;
			ev.block_index = j.value("index", -1);
			out.push_back(ev);
		} else if (type == "message_delta") {
			ev.type = ClaudeStreamEvent::Type::MessageDelta;
			ev.stop_reason = j["delta"].value("stop_reason", "");
			if (j.contains("usage")) {
				ev.output_tokens = j["usage"].value("output_tokens", 0);
			}
			out.push_back(ev);
		} else if (type == "message_stop") {
			ev.type = ClaudeStreamEvent::Type::MessageDone;
			out.push_back(ev);
		} else if (type == "error") {
			ev.type = ClaudeStreamEvent::Type::Error;
			ev.text = j["error"].value("message", "Unknown API error");
			out.push_back(ev);
		}
		// "ping" and unknown events are ignored.
		return true;
	}
}

ClaudeClient::ClaudeClient() :
	shared(std::make_shared<Shared>()) {
}

ClaudeClient::~ClaudeClient() {
	shared->alive = false;
	shared->cancelled = true;
	shared->sink = nullptr;
}

const char* ClaudeClient::endpoint() {
	return API_URL;
}

bool ClaudeClient::isRunning() const {
	return shared->running;
}

void ClaudeClient::cancel() {
	shared->cancelled = true;
}

bool ClaudeClient::start(const std::string& api_key, Json request_body, EventSink sink) {
	if (shared->running || api_key.empty()) {
		return false;
	}
	request_body["stream"] = true;

	shared->cancelled = false;
	shared->running = true;
	shared->sink = std::move(sink);
	const uint64_t gen = ++shared->generation;
	std::shared_ptr<Shared> self = shared;
	const std::string body = request_body.dump();

	try {
		std::thread([self, gen, api_key, body]() {
			SseParser parser;
			std::string raw_body; // kept only when the response is not an event stream
			bool is_event_stream = false;
			bool header_checked = false;
			std::atomic<bool> got_error { false };
			std::atomic<bool> got_done { false };

			auto write_cb = [&](std::string_view data, intptr_t) -> bool {
				if (self->cancelled) {
					return false; // aborts the transfer
				}
				if (!header_checked) {
					// The API answers errors with a JSON body, not an event stream. cpr
					// gives us no header at this point, so sniff the payload instead.
					header_checked = true;
					size_t first = data.find_first_not_of(" \r\n\t");
					is_event_stream = !(first != std::string_view::npos && data[first] == '{');
				}
				if (!is_event_stream) {
					raw_body.append(data);
					return true;
				}
				parser.feed(data, [&](const std::string& name, const std::string& payload) {
					std::vector<ClaudeStreamEvent> events;
					parseEvent(name, payload, events);
					for (auto& ev : events) {
						if (ev.type == ClaudeStreamEvent::Type::Error) {
							got_error = true;
						} else if (ev.type == ClaudeStreamEvent::Type::MessageDone) {
							got_done = true;
						}
						Shared::dispatch(self, gen, std::move(ev));
					}
				});
				return true;
			};

			cpr::Response response;
			try {
				response = cpr::Post(
					cpr::Url { API_URL },
					cpr::Header {
						{ "x-api-key", api_key },
						{ "anthropic-version", API_VERSION },
						{ "content-type", "application/json" },
						{ "accept", "text/event-stream" },
					},
					cpr::Body { body },
					cpr::WriteCallback { write_cb, 0 },
					cpr::Timeout { REQUEST_TIMEOUT_MS }
				);
			} catch (const std::exception& e) {
				ClaudeStreamEvent ev;
				ev.type = ClaudeStreamEvent::Type::Error;
				ev.text = std::string("HTTP failure: ") + e.what();
				Shared::dispatch(self, gen, std::move(ev));
				self->running = false;
				return;
			}

			if (!self->cancelled) {
				if (response.error && response.error.code != cpr::ErrorCode::ABORTED_BY_CALLBACK && response.error.code != cpr::ErrorCode::WRITE_ERROR) {
					ClaudeStreamEvent ev;
					ev.type = ClaudeStreamEvent::Type::Error;
					ev.text = "Network error: " + response.error.message;
					Shared::dispatch(self, gen, std::move(ev));
				} else if (!is_event_stream || response.status_code < 200 || response.status_code >= 300) {
					ClaudeStreamEvent ev;
					ev.type = ClaudeStreamEvent::Type::Error;
					std::string detail = raw_body.empty() ? response.text : raw_body;
					Json j = Json::parse(detail, nullptr, false);
					if (!j.is_discarded() && j.contains("error")) {
						detail = j["error"].value("type", "error") + ": " + j["error"].value("message", "");
					}
					ev.text = "API error (HTTP " + std::to_string(response.status_code) + "): " + detail;
					Shared::dispatch(self, gen, std::move(ev));
				} else if (!got_error && !got_done) {
					// A clean stream always ends with message_stop; make sure the agent
					// sees a terminal event even if the server closed early.
					ClaudeStreamEvent ev;
					ev.type = ClaudeStreamEvent::Type::MessageDone;
					Shared::dispatch(self, gen, std::move(ev));
				}
			}
			self->running = false;
		}).detach();
	} catch (const std::exception& e) {
		spdlog::error("ClaudeClient: failed to start worker thread: {}", e.what());
		shared->running = false;
		return false;
	}
	return true;
}
