//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_IO_MAP_RECORDER_H_
#define RME_RENDERING_IO_MAP_RECORDER_H_

#include <functional>
#include <memory>
#include <string>
#include <wx/event.h>
#include <wx/timer.h>

class MapCanvas;

// Time-lapse of the map view to an .mp4 (Claude panel > REC). A frame is taken
// only when the view was redrawn since the last one (edits, camera, cursors,
// the mouse over the map), so idle time drops out of the video by itself;
// frames are sampled at 30 / speed per second of activity and play at 30 fps,
// i.e. `speed` times faster than real time. The picture is what the canvas
// shows, overlays included, plus the mouse pointer, scaled down to at most
// RECORD_MAX_WIDTH. Encoding runs on its own thread (Media Foundation H.264,
// Windows only). GUI thread otherwise.
class MapRecorder : public wxEvtHandler {
public:
	static MapRecorder& get();
	// The recorder while a recording runs, else nullptr (never creates one).
	static MapRecorder* active();
	// Finishes a running recording (application exit).
	static void shutdown();

	// Starts recording the current map tab. False (and `error`) when it cannot.
	bool start(std::string& error);
	// Stops and finalizes the file. The status callback reports the result.
	void stop();
	bool isRecording() const {
		return recording;
	}

	std::string outputPath() const {
		return path;
	}
	int framesWritten() const;
	double videoSeconds() const;
	double realSeconds() const;
	// Why the last recording stopped on its own ("" when it did not fail).
	std::string lastError() const;

	// Called once a second while recording and when a recording ends.
	void setStatusCallback(std::function<void()> callback) {
		status_callback = std::move(callback);
	}
	// Drops the callback if a recorder exists (its owner is going away).
	static void clearStatusCallback();

	// From MapCanvas::OnPaint, after the overlays and before the buffer swap.
	void onCanvasPainted(MapCanvas* canvas, int width, int height);

private:
	MapRecorder();
	~MapRecorder() override;
	void onTimer(wxTimerEvent& event);

	struct Encoder;
	std::unique_ptr<Encoder> encoder;
	wxTimer timer;
	bool recording = false;
	bool painted = false; // the view was redrawn since the last frame
	bool frame_wanted = false; // grab the next paint of the current map canvas
	long long started_ms = 0;
	long long stopped_ms = 0;
	long long last_status_ms = 0;
	std::string path;
	std::function<void()> status_callback;
};

#endif
