//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_AI_ASSISTANT_LIVE_H_
#define RME_AI_ASSISTANT_LIVE_H_

#include "ai/claude_tools.h"
#include "map/position.h"

#include <chrono>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include <wx/event.h>
#include <wx/timer.h>

class BatchAction;
class Editor;
struct NVGcontext;
struct RenderView;

// Lets the mapper watch the assistant work. Tool calls (from the chat panel or
// any MCP client) run one at a time through a queue, so an animated one ends
// before the next starts; paint is applied a few tiles per frame and still
// lands as ONE undo step; the camera glides to where the work happens; and a
// "Claude" cursor with a name tag marks the spot. GUI thread only.
class AssistantLive : public wxEvtHandler {
public:
	using Done = std::function<void(ClaudeToolResult)>;
	// One queued tool call. It must call `done` exactly once, now or later.
	using Job = std::function<void(Done done)>;
	// Applies items [begin, end) of an animated edit. Returns an error message
	// to stop the animation, or "" to go on.
	using ApplyRange = std::function<std::string(size_t begin, size_t end)>;
	// Builds the tool result once an animated edit stops. `interruption` is
	// empty when every item was applied.
	using Finish = std::function<ClaudeToolResult(size_t applied, const std::string& interruption)>;

	static AssistantLive& get();
	// Application exit: drops the running and queued calls without calling
	// them back, and makes later calls fail right away.
	static void shutdown();

	static bool animationEnabled();
	static bool followEnabled();
	static bool cursorEnabled();

	// Runs `job` once every job queued before it has finished; `done` gets
	// its result.
	void enqueue(Job job, Done done);

	// For use inside a job. Applies `items` at CLAUDE_LIVE_RATE tiles per
	// second (faster if that would take over CLAUDE_LIVE_MAX_MS), all merged
	// into a single undo step, after the camera has brought the area into view.
	// Stops early (and reports it) when the map is edited, undone or closed.
	void animateEdit(Editor* editor, std::vector<Position> items, ApplyRange apply, Finish finish, Done done);

	// For use inside a job. Glides the camera until the area is in view (when
	// follow is on), then calls `then`.
	void bringIntoView(const Position& min, const Position& max, std::function<void()> then);
	// Glides the camera to center (x, y) on floor z (instantly when follow is
	// off), then calls `then`. `zoom` > 0 also eases the view to that zoom.
	void glideTo(double x, double y, int z, std::function<void()> then, double zoom = 0.0);
	// Eases the view to `zoom` around its current center, then calls `then`.
	void zoomTo(double zoom, std::function<void()> then);

	// Cursor: marks the area the assistant works on. `editing` picks the
	// stronger style (writing) over the light one (reading).
	void showActivity(Editor* editor, const Position& min, const Position& max, const std::string& label, bool editing);

	// Stop button: aborts the running animation and fails the queued calls.
	void stopAll(const std::string& reason);

	bool isBusy() const {
		return running || !queue.empty();
	}

	// Map overlay for `editor`'s canvas: work area, pen and name tag. Does
	// nothing (and creates nothing) until the assistant has been active.
	static void drawOverlay(NVGcontext* vg, const RenderView& view, const Editor* editor);

private:
	using Clock = std::chrono::steady_clock;

	AssistantLive();
	~AssistantLive() override;

	struct Pending {
		Job job;
		Done done;
	};

	struct Edit {
		bool active = false;
		bool stepping = false; // false while the camera is still gliding there
		Editor* editor = nullptr;
		std::vector<Position> items;
		size_t next = 0;
		size_t per_step = 1;
		double interval_ms = 16.0;
		Clock::time_point next_step;
		const BatchAction* group = nullptr;
		ApplyRange apply;
		Finish finish;
		Done done;
	};

	struct Glide {
		bool active = false;
		double from_x = 0, from_y = 0;
		double to_x = 0, to_y = 0;
		double from_zoom = 0, to_zoom = 0; // 0: the zoom stays as it is
		int z = 7;
		double duration_ms = 0;
		Clock::time_point start;
		std::function<void()> then;
	};

	void pump();
	void startEditSteps();
	void stepEdit();
	void endEdit(const std::string& interruption);
	void stepGlide();
	void followPen();
	void setPen(const Position& pen);
	void touch();
	void ensureTimer();
	void onTimer(wxTimerEvent& event);
	double presenceAlpha() const;
	void draw(NVGcontext* vg, const RenderView& view, const Editor* editor);

	wxTimer timer;
	std::deque<Pending> queue;
	bool running = false;
	bool pumping = false;
	Edit edit;
	Glide glide;

	// Cursor state.
	const Editor* presence_editor = nullptr;
	Position area_min, area_max;
	std::string label;
	bool editing = false;
	bool has_pen = false;
	Position pen_from, pen_to;
	Clock::time_point pen_moved;
	Clock::time_point last_activity;
	bool presence_visible = false;
};

#endif
