//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ai/assistant_live.h"

#include "app/settings.h"
#include "editor/action.h"
#include "editor/action_queue.h"
#include "editor/editor.h"
#include "rendering/core/render_view.h"
#include "rendering/ui/map_display.h"
#include "ui/gui.h"
#include "ui/map_tab.h"
#include "ui/map_window.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <nanovg.h>

namespace {
	constexpr int TICK_MS = 16;
	// Pace of an animated edit: CLAUDE_LIVE_RATE tiles per second, one chunk
	// every 16..250 ms, so a room goes in tile by tile; an edit that would take
	// longer than CLAUDE_LIVE_MAX_MS speeds up to finish in that time.
	constexpr double MIN_STEP_MS = 16.0;
	constexpr double MAX_STEP_MS = 250.0;
	// The cursor stays this long after the last call (the model is usually
	// thinking about the next one), then fades out.
	constexpr double LINGER_MS = 6000.0;
	constexpr double FADE_MS = 600.0;
	constexpr double PEN_GLIDE_MS = 90.0;
	// Farther than this many screens away the camera jumps instead of gliding.
	constexpr double MAX_GLIDE_SCREENS = 4.0;
	// Claude's accent colour (#D97757).
	constexpr unsigned char ACCENT_R = 217;
	constexpr unsigned char ACCENT_G = 119;
	constexpr unsigned char ACCENT_B = 87;

	AssistantLive* instance = nullptr;
	bool closed = false; // set by shutdown(): the editor is going away

	double msSince(std::chrono::steady_clock::time_point t) {
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
	}

	double easeInOut(double t) {
		return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
	}

	double easeOut(double t) {
		return 1.0 - (1.0 - t) * (1.0 - t);
	}

	// Camera of the current map tab in tile units: the continuous point at the
	// middle of the screen (tile t spans [t, t + 1)) and the half extents.
	struct ViewInfo {
		double cx = 0, cy = 0;
		double half_w = 0, half_h = 0;
		int z = GROUND_LAYER;
	};

	bool currentView(ViewInfo& v) {
		MapTab* tab = g_gui.GetCurrentMapTab();
		MapWindow* window = tab ? tab->GetView() : nullptr;
		MapCanvas* canvas = tab ? tab->GetCanvas() : nullptr;
		if (!window || !canvas) {
			return false;
		}
		int sx = 0, sy = 0, vw = 0, vh = 0;
		window->GetViewStart(&sx, &sy);
		window->GetViewSize(&vw, &vh);
		if (vw <= 0 || vh <= 0) {
			return false;
		}
		// Inverse of MapWindow::SetScreenCenterPosition(double, double, int).
		const double zoom = g_gui.GetCurrentZoom();
		v.z = canvas->GetFloor();
		const double offset = v.z <= GROUND_LAYER ? GROUND_LAYER - v.z : 0;
		v.half_w = vw * zoom / 2.0 / TILE_SIZE;
		v.half_h = vh * zoom / 2.0 / TILE_SIZE;
		v.cx = (sx + vw * zoom / 2.0) / TILE_SIZE + offset;
		v.cy = (sy + vh * zoom / 2.0) / TILE_SIZE + offset;
		return true;
	}

	void setCenter(double x, double y, int z) {
		MapTab* tab = g_gui.GetCurrentMapTab();
		if (tab && tab->GetView()) {
			tab->GetView()->SetScreenCenterPosition(x, y, z);
		}
	}

	// Zoom of one camera frame, set before setCenter() puts the center back.
	// Frames in between set it the way the mouse wheel does; only the last one
	// goes through SetZoom (status bar, minimap).
	void setFrameZoom(double zoom, bool last) {
		MapTab* tab = g_gui.GetCurrentMapTab();
		MapCanvas* canvas = tab ? tab->GetCanvas() : nullptr;
		if (!canvas) {
			return;
		}
		if (last) {
			g_gui.SetCurrentZoom(zoom);
		} else {
			canvas->zoom = zoom;
			canvas->UpdateZoomStatus();
		}
		canvas->Refresh();
	}

	// True when the area [min, max] of floor z is on screen with a small margin.
	bool areaVisible(const ViewInfo& v, const Position& min, const Position& max) {
		if (min.z != v.z) {
			return false;
		}
		const double margin = std::min(2.0, std::min(v.half_w, v.half_h) * 0.1);
		return min.x >= v.cx - v.half_w + margin && max.x + 1 <= v.cx + v.half_w - margin && min.y >= v.cy - v.half_h + margin && max.y + 1 <= v.cy + v.half_h - margin;
	}

	bool areaFits(const ViewInfo& v, const Position& min, const Position& max) {
		return max.x + 1 - min.x <= v.half_w * 1.8 && max.y + 1 - min.y <= v.half_h * 1.8;
	}

	ClaudeToolResult errorResult(const std::string& message) {
		ClaudeToolResult r;
		r.is_error = true;
		r.text = message;
		r.summary = message;
		return r;
	}

	// Mouse-pointer arrow with its tip at (x, y).
	void drawPointer(NVGcontext* vg, float x, float y, float alpha) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, x, y);
		nvgLineTo(vg, x, y + 17.0f);
		nvgLineTo(vg, x + 4.8f, y + 12.8f);
		nvgLineTo(vg, x + 8.2f, y + 20.2f);
		nvgLineTo(vg, x + 11.2f, y + 18.9f);
		nvgLineTo(vg, x + 7.9f, y + 11.7f);
		nvgLineTo(vg, x + 13.9f, y + 11.7f);
		nvgClosePath(vg);
		nvgFillColor(vg, nvgRGBA(ACCENT_R, ACCENT_G, ACCENT_B, static_cast<unsigned char>(255 * alpha)));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(255, 255, 255, static_cast<unsigned char>(235 * alpha)));
		nvgStrokeWidth(vg, 1.5f);
		nvgStroke(vg);
	}
}

AssistantLive& AssistantLive::get() {
	if (!instance) {
		instance = new AssistantLive();
	}
	return *instance;
}

void AssistantLive::shutdown() {
	closed = true;
	// Nobody is left to hear back: the chat panel and the MCP servers are
	// being torn down too (the servers stop waiting on their own).
	delete instance;
	instance = nullptr;
}

void AssistantLive::drawOverlay(NVGcontext* vg, const RenderView& view, const Editor* editor) {
	if (instance) {
		instance->draw(vg, view, editor);
	}
}

bool AssistantLive::animationEnabled() {
	return g_settings.getBoolean(Config::CLAUDE_LIVE_ANIMATE);
}

bool AssistantLive::followEnabled() {
	return g_settings.getBoolean(Config::CLAUDE_LIVE_FOLLOW);
}

bool AssistantLive::cursorEnabled() {
	return g_settings.getBoolean(Config::CLAUDE_LIVE_CURSOR);
}

AssistantLive::AssistantLive() {
	timer.SetOwner(this);
	Bind(wxEVT_TIMER, &AssistantLive::onTimer, this);
}

AssistantLive::~AssistantLive() {
	timer.Stop();
}

// ---- queue -----------------------------------------------------------------

void AssistantLive::enqueue(Job job, Done done) {
	if (closed) {
		if (done) {
			done(errorResult("The editor is closing."));
		}
		return;
	}
	queue.push_back({ std::move(job), std::move(done) });
	pump();
}

void AssistantLive::pump() {
	// A job that finishes synchronously calls back into pump(); the loop below
	// picks up the next one instead of recursing.
	if (pumping) {
		return;
	}
	pumping = true;
	while (!running && !queue.empty()) {
		Pending next = std::move(queue.front());
		queue.pop_front();
		running = true;
		auto done = std::make_shared<Done>(std::move(next.done));
		auto fired = std::make_shared<bool>(false);
		Done finish = [this, done, fired](ClaudeToolResult result) {
			if (*fired) {
				return;
			}
			*fired = true;
			running = false;
			if (*done) {
				(*done)(std::move(result));
			}
			pump();
		};
		try {
			next.job(finish);
		} catch (const std::exception& e) {
			finish(errorResult(std::string("Tool failed: ") + e.what()));
		} catch (...) {
			finish(errorResult("Tool failed with an unknown error."));
		}
	}
	pumping = false;
}

void AssistantLive::stopAll(const std::string& reason) {
	std::deque<Pending> dropped;
	dropped.swap(queue);
	endEdit(reason);
	if (glide.active) {
		glide.active = false;
		std::function<void()> then = std::move(glide.then);
		glide.then = nullptr;
		if (then) {
			then();
		}
	}
	for (Pending& pending : dropped) {
		if (pending.done) {
			pending.done(errorResult(reason));
		}
	}
}

// ---- animated edit ---------------------------------------------------------

void AssistantLive::animateEdit(Editor* editor, std::vector<Position> items, ApplyRange apply, Finish finish, Done done) {
	if (edit.active) {
		// Jobs run one at a time, so this is a bug rather than a race.
		done(finish(0, "another edit is still animating"));
		return;
	}
	if (!editor || items.empty()) {
		done(finish(0, ""));
		return;
	}

	Position min = items.front();
	Position max = items.front();
	for (const Position& p : items) {
		min.x = std::min(min.x, p.x);
		min.y = std::min(min.y, p.y);
		max.x = std::max(max.x, p.x);
		max.y = std::max(max.y, p.y);
	}

	const size_t total = items.size();
	const double rate = std::clamp(g_settings.getInteger(Config::CLAUDE_LIVE_RATE), 1, 5000);
	const double max_duration = std::clamp(g_settings.getInteger(Config::CLAUDE_LIVE_MAX_MS), 500, 120000);
	const double duration = std::min(static_cast<double>(total) * 1000.0 / rate, max_duration);
	edit = Edit {};
	edit.active = true;
	edit.editor = editor;
	edit.interval_ms = std::clamp(duration / static_cast<double>(total), MIN_STEP_MS, MAX_STEP_MS);
	edit.per_step = std::max<size_t>(1, static_cast<size_t>(std::ceil(static_cast<double>(total) * edit.interval_ms / duration)));
	edit.apply = std::move(apply);
	edit.finish = std::move(finish);
	edit.done = std::move(done);
	const Position first = items.front();
	edit.items = std::move(items);

	setPen(first);
	// Whole area on screen when it fits; otherwise where the pen starts, and
	// followPen() keeps up from there.
	ViewInfo v;
	if (currentView(v) && !areaFits(v, min, max)) {
		bringIntoView(first, first, [this]() { startEditSteps(); });
	} else {
		bringIntoView(min, max, [this]() { startEditSteps(); });
	}
}

void AssistantLive::startEditSteps() {
	if (!edit.active || edit.stepping) {
		return;
	}
	edit.stepping = true;
	edit.next_step = Clock::now();
	ensureTimer();
	stepEdit();
}

void AssistantLive::stepEdit() {
	if (!edit.active || !edit.stepping) {
		return;
	}
	Editor* editor = edit.editor;
	if (g_gui.GetCurrentEditor() != editor) {
		endEdit("the map tab was switched or closed");
		return;
	}
	ActionQueue& queue_ref = *editor->actionQueue;
	if (edit.group && (queue_ref.lastBatch() != edit.group || queue_ref.getCurrentIndex() != queue_ref.getSize())) {
		endEdit("the map was edited or undone while Claude was working on it");
		return;
	}

	const size_t begin = edit.next;
	const size_t end = std::min(edit.items.size(), begin + edit.per_step);

	// Every chunk lands in the same undo step (a doodad chunk alone makes one
	// batch per tile).
	std::string error;
	queue_ref.beginGroup(edit.group);
	try {
		error = edit.apply(begin, end);
	} catch (const std::exception& e) {
		error = e.what();
	} catch (...) {
		error = "unknown error";
	}
	if (const BatchAction* group = queue_ref.endGroup()) {
		edit.group = group;
	}
	edit.next = end;
	setPen(edit.items[end - 1]);
	followPen();
	g_gui.RefreshView();

	if (!error.empty()) {
		endEdit(error);
		return;
	}
	if (edit.next >= edit.items.size()) {
		endEdit("");
		return;
	}
	const auto interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(edit.interval_ms));
	edit.next_step += interval;
	// Fell behind (a slow chunk): do not try to catch up in a burst.
	if (edit.next_step < Clock::now() - interval) {
		edit.next_step = Clock::now();
	}
}

void AssistantLive::endEdit(const std::string& interruption) {
	if (!edit.active) {
		return;
	}
	Edit finished = std::move(edit);
	edit = Edit {};
	if (finished.editor && g_gui.GetCurrentEditor() == finished.editor) {
		// The user's next stroke is a step of its own.
		finished.editor->actionQueue->resetTimer();
		g_gui.UpdateMenubar();
	}
	touch();
	ClaudeToolResult result = finished.finish ? finished.finish(finished.next, interruption) : ClaudeToolResult {};
	if (finished.done) {
		finished.done(std::move(result));
	}
}

// ---- camera ----------------------------------------------------------------

void AssistantLive::bringIntoView(const Position& min, const Position& max, std::function<void()> then) {
	ViewInfo v;
	if (!followEnabled() || !currentView(v) || areaVisible(v, min, max)) {
		if (then) {
			then();
		}
		return;
	}
	glideTo((min.x + max.x + 1) / 2.0, (min.y + max.y + 1) / 2.0, min.z, std::move(then));
}

void AssistantLive::glideTo(double x, double y, int z, std::function<void()> then, double zoom) {
	// A newer glide replaces the running one; its continuation still runs so
	// no job is left waiting.
	std::function<void()> replaced;
	if (glide.active) {
		replaced = std::move(glide.then);
		glide = Glide {};
	}

	ViewInfo v;
	const bool have_view = currentView(v);
	const double from_zoom = g_gui.GetCurrentZoom();
	const bool rezoom = zoom > 0.0 && from_zoom > 0.0 && std::abs(zoom - from_zoom) > 0.001;
	const double distance = have_view ? std::hypot(x - v.cx, y - v.cy) : 0.0;
	const double screen = have_view ? std::max(v.half_w, v.half_h) * 2.0 : 0.0;
	if (!have_view || !followEnabled() || z != v.z || (distance < 0.5 && !rezoom) || distance > screen * MAX_GLIDE_SCREENS) {
		if (rezoom) {
			g_gui.SetCurrentZoom(zoom);
		}
		if (have_view || g_gui.GetCurrentMapTab()) {
			setCenter(x, y, z);
		}
		if (replaced) {
			replaced();
		}
		if (then) {
			then();
		}
		return;
	}

	glide.active = true;
	glide.from_x = v.cx;
	glide.from_y = v.cy;
	glide.to_x = x;
	glide.to_y = y;
	glide.z = z;
	if (rezoom) {
		glide.from_zoom = from_zoom;
		glide.to_zoom = zoom;
	}
	// A zoom takes longer the more it changes the scale (x2 and /2 alike).
	const double move_ms = distance < 0.5 ? 0.0 : std::clamp(250.0 + distance * 6.0, 250.0, 700.0);
	const double zoom_ms = rezoom ? std::clamp(250.0 + std::abs(std::log(zoom / from_zoom)) * 300.0, 250.0, 700.0) : 0.0;
	glide.duration_ms = std::max(move_ms, zoom_ms);
	glide.start = Clock::now();
	glide.then = std::move(then);
	touch();
	if (replaced) {
		replaced();
	}
}

void AssistantLive::zoomTo(double zoom, std::function<void()> then) {
	ViewInfo v;
	if (!currentView(v)) {
		if (zoom > 0.0) {
			g_gui.SetCurrentZoom(zoom);
		}
		if (then) {
			then();
		}
		return;
	}
	glideTo(v.cx, v.cy, v.z, std::move(then), zoom);
}

void AssistantLive::stepGlide() {
	if (!glide.active) {
		return;
	}
	const double t = std::min(1.0, msSince(glide.start) / glide.duration_ms);
	const double e = easeInOut(t);
	if (glide.to_zoom > 0.0) {
		// Geometric, so zooming in and out move at the same visual pace.
		setFrameZoom(glide.from_zoom * std::pow(glide.to_zoom / glide.from_zoom, e), t >= 1.0);
	}
	setCenter(glide.from_x + (glide.to_x - glide.from_x) * e, glide.from_y + (glide.to_y - glide.from_y) * e, glide.z);
	if (t >= 1.0) {
		std::function<void()> then = std::move(glide.then);
		glide = Glide {};
		if (then) {
			then();
		}
	}
}

void AssistantLive::followPen() {
	if (!followEnabled() || glide.active || !has_pen) {
		return;
	}
	ViewInfo v;
	if (!currentView(v) || pen_to.z != v.z) {
		return;
	}
	const double px = pen_to.x + 0.5;
	const double py = pen_to.y + 0.5;
	if (std::abs(px - v.cx) <= v.half_w * 0.7 && std::abs(py - v.cy) <= v.half_h * 0.7) {
		return;
	}
	glideTo(px, py, v.z, nullptr);
}

// ---- cursor ----------------------------------------------------------------

void AssistantLive::showActivity(Editor* editor, const Position& min, const Position& max, const std::string& text, bool is_editing) {
	presence_editor = editor;
	area_min = min;
	area_max = max;
	label = text;
	editing = is_editing;
	setPen(min);
}

void AssistantLive::setPen(const Position& pen) {
	if (has_pen && pen_to.z == pen.z) {
		// Start from where the pen is drawn right now, so it never jumps.
		const double t = easeOut(std::clamp(msSince(pen_moved) / PEN_GLIDE_MS, 0.0, 1.0));
		pen_from.x = static_cast<int>(std::lround(pen_from.x + (pen_to.x - pen_from.x) * t));
		pen_from.y = static_cast<int>(std::lround(pen_from.y + (pen_to.y - pen_from.y) * t));
		pen_from.z = pen.z;
	} else {
		pen_from = pen;
	}
	pen_to = pen;
	pen_moved = Clock::now();
	has_pen = true;
	touch();
}

void AssistantLive::touch() {
	last_activity = Clock::now();
	presence_visible = true;
	ensureTimer();
}

double AssistantLive::presenceAlpha() const {
	if (!presence_visible) {
		return 0.0;
	}
	if (running || edit.active || glide.active) {
		return 1.0;
	}
	const double idle = msSince(last_activity);
	if (idle <= LINGER_MS) {
		return 1.0;
	}
	if (idle >= LINGER_MS + FADE_MS) {
		return 0.0;
	}
	return 1.0 - (idle - LINGER_MS) / FADE_MS;
}

void AssistantLive::ensureTimer() {
	if (!timer.IsRunning()) {
		timer.Start(TICK_MS);
	}
}

void AssistantLive::onTimer(wxTimerEvent&) {
	if (glide.active) {
		stepGlide();
	}
	if (edit.active && edit.stepping && !glide.active && Clock::now() >= edit.next_step) {
		stepEdit();
	}

	const double alpha = presenceAlpha();
	if (presence_visible && alpha <= 0.0) {
		presence_visible = false;
		has_pen = false;
		presence_editor = nullptr;
		g_gui.RefreshView();
	} else if (presence_visible && cursorEnabled() && (alpha < 1.0 || msSince(pen_moved) < PEN_GLIDE_MS * 2.0)) {
		g_gui.RefreshView();
	}

	if (!glide.active && !edit.active && !presence_visible) {
		timer.Stop();
	}
}

void AssistantLive::draw(NVGcontext* vg, const RenderView& view, const Editor* editor) {
	if (!vg || !presence_visible || !editor || editor != presence_editor || !cursorEnabled() || area_min.z != view.floor) {
		return;
	}
	const double alpha = presenceAlpha();
	if (alpha <= 0.0) {
		return;
	}

	const float zoom = view.zoom;
	const float tile = TILE_SIZE / zoom;
	int origin_x = 0, origin_y = 0;
	view.getScreenPosition(0, 0, area_min.z, origin_x, origin_y);
	const auto project = [&](double x, double y, float& sx, float& sy) {
		sx = static_cast<float>((origin_x + x * TILE_SIZE) / zoom);
		sy = static_cast<float>((origin_y + y * TILE_SIZE) / zoom);
	};
	const auto a = [&](double value) {
		return static_cast<unsigned char>(std::clamp(value * alpha, 0.0, 255.0));
	};

	nvgSave(vg);

	// Work area.
	float x0, y0, x1, y1;
	project(area_min.x, area_min.y, x0, y0);
	project(area_max.x + 1, area_max.y + 1, x1, y1);
	nvgBeginPath(vg);
	nvgRect(vg, x0, y0, x1 - x0, y1 - y0);
	if (editing) {
		nvgFillColor(vg, nvgRGBA(ACCENT_R, ACCENT_G, ACCENT_B, a(26)));
		nvgFill(vg);
	}
	nvgStrokeColor(vg, nvgRGBA(ACCENT_R, ACCENT_G, ACCENT_B, a(editing ? 220 : 150)));
	nvgStrokeWidth(vg, editing ? 2.0f : 1.5f);
	nvgStroke(vg);

	// Pen: eases between the last two tiles it touched.
	if (has_pen && pen_to.z == area_min.z) {
		const double t = easeOut(std::clamp(msSince(pen_moved) / PEN_GLIDE_MS, 0.0, 1.0));
		float sx, sy;
		project(pen_from.x + (pen_to.x - pen_from.x) * t, pen_from.y + (pen_to.y - pen_from.y) * t, sx, sy);
		if (editing) {
			nvgBeginPath(vg);
			nvgRect(vg, sx, sy, tile, tile);
			nvgFillColor(vg, nvgRGBA(ACCENT_R, ACCENT_G, ACCENT_B, a(90)));
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGBA(ACCENT_R, ACCENT_G, ACCENT_B, a(255)));
			nvgStrokeWidth(vg, 2.0f);
			nvgStroke(vg);
		}

		const float cx = sx + tile * 0.5f;
		const float cy = sy + tile * 0.5f;
		drawPointer(vg, cx, cy, static_cast<float>(alpha));

		// Name tag.
		const std::string text = label.empty() ? std::string("Claude") : "Claude \xC2\xB7 " + label;
		nvgFontFace(vg, "sans");
		nvgFontSize(vg, 12.0f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		float bounds[4] = { 0, 0, 0, 0 };
		nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, bounds);
		const float text_w = bounds[2] - bounds[0];
		const float tag_x = cx + 13.0f;
		const float tag_y = cy + 17.0f;
		const float tag_h = 20.0f;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, tag_x, tag_y, text_w + 14.0f, tag_h, 6.0f);
		nvgFillColor(vg, nvgRGBA(ACCENT_R, ACCENT_G, ACCENT_B, a(235)));
		nvgFill(vg);
		nvgFillColor(vg, nvgRGBA(255, 255, 255, a(255)));
		nvgText(vg, tag_x + 7.0f, tag_y + tag_h / 2.0f, text.c_str(), nullptr);
	}

	nvgRestore(vg);
}
