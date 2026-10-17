//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ai/mcp_server.h"

#include <algorithm>
#include <boost/asio.hpp>
#include <chrono>
#include <future>
#include <spdlog/spdlog.h>
#include <wx/app.h>

using Json = nlohmann::json;
using boost::asio::ip::tcp;

namespace {
	constexpr const char* SERVER_NAME = "rme";
	constexpr const char* DEFAULT_PROTOCOL = "2025-03-26";
	constexpr size_t MAX_BODY = 8 * 1024 * 1024;

	struct HttpRequest {
		std::string method;
		std::string target;
		std::string body;
		std::string authorization;
		std::string origin;
		bool valid = false;
	};

	std::string trim(const std::string& s) {
		const size_t first = s.find_first_not_of(" \t");
		if (first == std::string::npos) {
			return "";
		}
		const size_t last = s.find_last_not_of(" \t");
		return s.substr(first, last - first + 1);
	}

	// Browsers always send Origin; a page on some website must not be able to
	// drive the editor through the user's browser (DNS rebinding and friends).
	// Local tools (Claude Code, curl) send none.
	bool originAllowed(const std::string& origin) {
		if (origin.empty() || origin == "null") {
			return true;
		}
		for (const char* allowed : { "http://127.0.0.1", "http://localhost", "https://127.0.0.1", "https://localhost" }) {
			const std::string prefix = allowed;
			if (origin.compare(0, prefix.size(), prefix) == 0 && (origin.size() == prefix.size() || origin[prefix.size()] == ':' || origin[prefix.size()] == '/')) {
				return true;
			}
		}
		return false;
	}

	// Reads one HTTP/1.1 request (headers + Content-Length body) from the socket.
	HttpRequest readRequest(tcp::socket& socket) {
		HttpRequest request;
		boost::asio::streambuf buffer;
		boost::system::error_code ec;
		const size_t header_bytes = boost::asio::read_until(socket, buffer, "\r\n\r\n", ec);
		if (ec) {
			return request;
		}
		std::string head(boost::asio::buffers_begin(buffer.data()), boost::asio::buffers_begin(buffer.data()) + header_bytes);
		buffer.consume(header_bytes);

		// Request line.
		const size_t line_end = head.find("\r\n");
		std::string request_line = head.substr(0, line_end);
		const size_t sp1 = request_line.find(' ');
		const size_t sp2 = request_line.find(' ', sp1 == std::string::npos ? 0 : sp1 + 1);
		if (sp1 == std::string::npos || sp2 == std::string::npos) {
			return request;
		}
		request.method = request_line.substr(0, sp1);
		request.target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);

		// Headers: Content-Length, plus Authorization and Origin for the checks.
		size_t content_length = 0;
		size_t pos = line_end + 2;
		while (pos < head.size()) {
			const size_t next = head.find("\r\n", pos);
			if (next == std::string::npos || next == pos) {
				break;
			}
			std::string line = head.substr(pos, next - pos);
			pos = next + 2;
			const size_t colon = line.find(':');
			if (colon == std::string::npos) {
				continue;
			}
			std::string key = line.substr(0, colon);
			std::transform(key.begin(), key.end(), key.begin(), ::tolower);
			if (key == "content-length") {
				try {
					content_length = static_cast<size_t>(std::stoull(line.substr(colon + 1)));
				} catch (...) {
					content_length = 0;
				}
			} else if (key == "authorization") {
				request.authorization = trim(line.substr(colon + 1));
			} else if (key == "origin") {
				request.origin = trim(line.substr(colon + 1));
			}
		}
		if (content_length > MAX_BODY) {
			return request;
		}

		// Body: whatever read_until already pulled in, then the rest.
		request.body.assign(boost::asio::buffers_begin(buffer.data()), boost::asio::buffers_end(buffer.data()));
		if (request.body.size() < content_length) {
			const size_t remaining = content_length - request.body.size();
			std::string rest(remaining, '\0');
			boost::asio::read(socket, boost::asio::buffer(rest.data(), remaining), ec);
			if (ec) {
				return request;
			}
			request.body += rest;
		} else if (request.body.size() > content_length) {
			request.body.resize(content_length);
		}
		request.valid = true;
		return request;
	}

	void writeResponse(tcp::socket& socket, int status, const std::string& body, const std::string& content_type, const std::string& extra_headers = "") {
		const char* reason = status == 200 ? "OK" : status == 202 ? "Accepted" : status == 401 ? "Unauthorized" : status == 403 ? "Forbidden" : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : status == 400 ? "Bad Request" : "Internal Server Error";
		std::string response = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
		if (!body.empty()) {
			response += "Content-Type: " + content_type + "\r\n";
		}
		response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
		response += extra_headers;
		response += "Connection: close\r\n\r\n";
		response += body;
		boost::system::error_code ec;
		boost::asio::write(socket, boost::asio::buffer(response), ec);
	}

	Json rpcError(const Json& id, int code, const std::string& message) {
		return { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", message } } } };
	}

	Json rpcResult(const Json& id, Json result) {
		return { { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move(result) } };
	}

	Json toolsListResult() {
		Json tools = Json::array();
		for (const auto& def : ClaudeTools::definitions()) {
			tools.push_back({
				{ "name", def["name"] },
				{ "description", def["description"] },
				{ "inputSchema", def["input_schema"] },
			});
		}
		return { { "tools", tools } };
	}
}

struct McpServer::Impl {
	boost::asio::io_context io;
	std::unique_ptr<tcp::acceptor> acceptor;
	std::thread thread;
	std::atomic<bool> running { false };
	int port = 0;
	std::mutex observer_mutex; // guards observer, auth_token and instructions
	ToolObserver observer;
	std::string auth_token;
	std::string instructions = "Tools for reading and editing the map open in Remere's Map Editor. Every edit is one undo step.";

	std::string authToken() {
		std::lock_guard<std::mutex> lock(observer_mutex);
		return auth_token;
	}

	// Runs the tool on the GUI thread and blocks the HTTP thread until it is
	// done (an animated paint finishes a moment after it starts).
	ClaudeToolResult callTool(const std::string& name, const Json& input) {
		struct Pending {
			std::promise<ClaudeToolResult> promise;
		};
		auto pending = std::make_shared<Pending>();
		std::future<ClaudeToolResult> future = pending->promise.get_future();

		ToolObserver observer_copy;
		{
			std::lock_guard<std::mutex> lock(observer_mutex);
			observer_copy = observer;
		}

		if (!wxTheApp) {
			ClaudeToolResult r;
			r.is_error = true;
			r.text = "Editor is shutting down.";
			return r;
		}
		wxTheApp->CallAfter([pending, name, input, observer_copy]() {
			if (observer_copy) {
				observer_copy(name, input, nullptr);
			}
			ClaudeTools::executeAsync(name, input, [pending, name, input, observer_copy](ClaudeToolResult result) {
				if (observer_copy) {
					observer_copy(name, input, &result);
				}
				pending->promise.set_value(std::move(result));
			});
		});

		// Wait, but give up if the server is being stopped (the GUI may never run
		// the queued call during shutdown).
		while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
			if (!running) {
				ClaudeToolResult r;
				r.is_error = true;
				r.text = "Tool call abandoned: server stopping.";
				return r;
			}
		}
		return future.get();
	}

	Json handleRpc(const Json& request, bool& is_notification) {
		is_notification = !request.contains("id") || request["id"].is_null();
		const Json id = request.value("id", Json(nullptr));
		const std::string method = request.value("method", "");
		const Json params = request.value("params", Json::object());

		if (method.rfind("notifications/", 0) == 0) {
			is_notification = true;
			return Json();
		}
		if (method == "initialize") {
			std::string version = params.value("protocolVersion", DEFAULT_PROTOCOL);
			if (version != "2024-11-05" && version != "2025-03-26" && version != "2025-06-18") {
				version = DEFAULT_PROTOCOL;
			}
			std::string text;
			{
				std::lock_guard<std::mutex> lock(observer_mutex);
				text = instructions;
			}
			return rpcResult(id, {
				{ "protocolVersion", version },
				{ "capabilities", { { "tools", Json::object() } } },
				{ "serverInfo", { { "name", "RME Redux" }, { "version", "1.0" } } },
				{ "instructions", text },
			});
		}
		if (method == "ping") {
			return rpcResult(id, Json::object());
		}
		if (method == "tools/list") {
			return rpcResult(id, toolsListResult());
		}
		if (method == "tools/call") {
			const std::string name = params.value("name", "");
			const Json arguments = params.value("arguments", Json::object());
			const ClaudeToolResult result = callTool(name, arguments.is_object() ? arguments : Json::object());
			Json content = Json::array();
			content.push_back({ { "type", "text" }, { "text", result.text } });
			if (!result.image_png_base64.empty()) {
				content.push_back({ { "type", "image" }, { "data", result.image_png_base64 }, { "mimeType", "image/png" } });
			}
			return rpcResult(id, { { "content", content }, { "isError", result.is_error } });
		}
		return rpcError(id, -32601, "Method not found: " + method);
	}

	void handleConnection(tcp::socket& socket) {
		HttpRequest request = readRequest(socket);
		if (!request.valid) {
			writeResponse(socket, 400, "bad request", "text/plain");
			return;
		}
		if (request.target != "/mcp" && request.target != "/mcp/") {
			writeResponse(socket, 404, "not found", "text/plain");
			return;
		}
		if (!originAllowed(request.origin)) {
			writeResponse(socket, 403, "forbidden origin", "text/plain");
			return;
		}
		const std::string token = authToken();
		if (!token.empty() && request.authorization != "Bearer " + token) {
			writeResponse(socket, 401, "missing or wrong bearer token", "text/plain");
			return;
		}
		if (request.method == "GET") {
			// No server-initiated stream: the spec lets us refuse it.
			writeResponse(socket, 405, "", "text/plain", "Allow: POST\r\n");
			return;
		}
		if (request.method == "DELETE") {
			writeResponse(socket, 200, "", "text/plain");
			return;
		}
		if (request.method != "POST") {
			writeResponse(socket, 405, "", "text/plain", "Allow: POST\r\n");
			return;
		}

		Json parsed = Json::parse(request.body, nullptr, false);
		if (parsed.is_discarded()) {
			writeResponse(socket, 400, rpcError(nullptr, -32700, "Parse error").dump(), "application/json");
			return;
		}

		// Batches are rare but cheap to support.
		if (parsed.is_array()) {
			Json responses = Json::array();
			for (const auto& item : parsed) {
				bool notification = false;
				Json response = handleRpc(item, notification);
				if (!notification) {
					responses.push_back(response);
				}
			}
			if (responses.empty()) {
				writeResponse(socket, 202, "", "application/json");
			} else {
				writeResponse(socket, 200, responses.dump(), "application/json");
			}
			return;
		}

		bool notification = false;
		Json response = handleRpc(parsed, notification);
		if (notification) {
			writeResponse(socket, 202, "", "application/json");
		} else {
			writeResponse(socket, 200, response.dump(), "application/json");
		}
	}

	void run() {
		while (running) {
			tcp::socket socket(io);
			boost::system::error_code ec;
			acceptor->accept(socket, ec);
			if (ec || !running) {
				if (!running) {
					break;
				}
				continue;
			}
			try {
				handleConnection(socket);
			} catch (const std::exception& e) {
				spdlog::warn("McpServer: request failed: {}", e.what());
			} catch (...) {
				spdlog::warn("McpServer: request failed with unknown error");
			}
			boost::system::error_code ignored;
			socket.shutdown(tcp::socket::shutdown_both, ignored);
			socket.close(ignored);
		}
	}
};

McpServer::McpServer() :
	impl(std::make_unique<Impl>()) {
}

McpServer::~McpServer() {
	stop();
}

const char* McpServer::serverName() {
	return SERVER_NAME;
}

bool McpServer::isRunning() const {
	return impl->running;
}

int McpServer::port() const {
	return impl->port;
}

std::string McpServer::url() const {
	return "http://127.0.0.1:" + std::to_string(impl->port) + "/mcp";
}

void McpServer::setObserver(ToolObserver observer) {
	std::lock_guard<std::mutex> lock(impl->observer_mutex);
	impl->observer = std::move(observer);
}

void McpServer::setAuthToken(const std::string& token) {
	std::lock_guard<std::mutex> lock(impl->observer_mutex);
	impl->auth_token = token;
}

void McpServer::setInstructions(const std::string& text) {
	std::lock_guard<std::mutex> lock(impl->observer_mutex);
	impl->instructions = text;
}

bool McpServer::start(int port) {
	if (impl->running) {
		return true;
	}
	try {
		const tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(std::clamp(port, 0, 65535)));
		impl->acceptor = std::make_unique<tcp::acceptor>(impl->io);
		impl->acceptor->open(endpoint.protocol());
#ifndef _WIN32
		// On Windows SO_REUSEADDR would let us share a port another program
		// already listens on; elsewhere it only skips TIME_WAIT after a restart.
		impl->acceptor->set_option(tcp::acceptor::reuse_address(true));
#endif
		impl->acceptor->bind(endpoint);
		impl->acceptor->listen();
		impl->port = static_cast<int>(impl->acceptor->local_endpoint().port());
	} catch (const std::exception& e) {
		spdlog::error("McpServer: could not listen on 127.0.0.1:{}: {}", port, e.what());
		impl->acceptor.reset();
		return false;
	}
	impl->running = true;
	impl->thread = std::thread([this]() {
		impl->run();
	});
	spdlog::info("McpServer: listening on {}", url());
	return true;
}

void McpServer::stop() {
	if (!impl->running) {
		return;
	}
	impl->running = false;
	boost::system::error_code ec;
	if (impl->acceptor) {
		impl->acceptor->close(ec);
	}
	// Wake a blocking accept() in case close() did not.
	try {
		boost::asio::io_context wake_io;
		tcp::socket wake(wake_io);
		wake.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(impl->port)), ec);
		wake.close(ec);
	} catch (...) {
	}
	if (impl->thread.joinable()) {
		impl->thread.join();
	}
	impl->acceptor.reset();
	impl->port = 0;
}
