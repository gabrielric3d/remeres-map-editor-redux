//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/io/map_recorder.h"

#include "app/settings.h"
#include "rendering/io/screen_capture.h"
#include "rendering/ui/map_display.h"
#include "ui/gui.h"
#include "ui/map_tab.h"
#include "util/common.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <wx/datetime.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/stdpaths.h>
#include <wx/time.h>
#include <wx/utils.h>

#ifdef _WIN32
#include <wx/msw/wrapwin.h>
// app/main.h targets Windows XP (_WIN32_WINNT 0x0501), and the SDK only
// declares the Media Foundation sink writer for Windows 7 and later. Raise the
// target for the headers below only; nothing else in this file depends on it.
#if !defined(WINVER) || WINVER < 0x0601
#undef WINVER
#define WINVER 0x0601
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#ifdef _MSC_VER
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#endif
#endif

namespace {
	constexpr int OUTPUT_FPS = 30;
	// Frames waiting for the encoder; past this a frame is dropped rather than
	// letting memory grow while the encoder catches up.
	constexpr size_t MAX_QUEUED_FRAMES = 6;

	MapRecorder* instance = nullptr;

	struct Frame {
		std::vector<uint8_t> rgb; // top-down, 3 bytes per pixel
		int width = 0;
		int height = 0;
		int mouse_x = -1; // in frame pixels; -1 = the mouse is not over the map
		int mouse_y = -1;
	};

	// Mouse pointer arrow, tip at (0, 0), sized for a 1280-wide video.
	const double POINTER[][2] = { { 0, 0 }, { 0, 17 }, { 4.8, 12.8 }, { 8.2, 20.2 }, { 11.2, 18.9 }, { 7.9, 11.7 }, { 13.9, 11.7 } };
	constexpr int POINTER_POINTS = sizeof(POINTER) / sizeof(POINTER[0]);

	bool insidePointer(double x, double y, double scale) {
		bool inside = false;
		for (int i = 0, j = POINTER_POINTS - 1; i < POINTER_POINTS; j = i++) {
			const double xi = POINTER[i][0] * scale, yi = POINTER[i][1] * scale;
			const double xj = POINTER[j][0] * scale, yj = POINTER[j][1] * scale;
			if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) {
				inside = !inside;
			}
		}
		return inside;
	}

	// White arrow with a black outline, tip at (px, py), into a BGRA picture.
	void drawPointer(std::vector<uint8_t>& bgra, int width, int height, int px, int py, double scale) {
		const int w = static_cast<int>(std::ceil(15 * scale)) + 2;
		const int h = static_cast<int>(std::ceil(21 * scale)) + 2;
		std::vector<uint8_t> mask(static_cast<size_t>(w) * h, 0);
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				mask[static_cast<size_t>(y) * w + x] = insidePointer(x - 1 + 0.5, y - 1 + 0.5, scale) ? 1 : 0;
			}
		}
		const auto masked = [&](int x, int y) {
			return x >= 0 && y >= 0 && x < w && y < h && mask[static_cast<size_t>(y) * w + x];
		};
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				uint8_t value;
				if (masked(x, y)) {
					value = 255;
				} else if (masked(x - 1, y) || masked(x + 1, y) || masked(x, y - 1) || masked(x, y + 1) || masked(x - 1, y - 1) || masked(x + 1, y + 1) || masked(x - 1, y + 1) || masked(x + 1, y - 1)) {
					value = 0;
				} else {
					continue;
				}
				const int tx = px + x - 1;
				const int ty = py + y - 1;
				if (tx < 0 || ty < 0 || tx >= width || ty >= height) {
					continue;
				}
				uint8_t* pixel = &bgra[(static_cast<size_t>(ty) * width + tx) * 4];
				pixel[0] = pixel[1] = pixel[2] = value;
			}
		}
	}

	std::string hresultText(long hr) {
		char buffer[16];
		std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
		return buffer;
	}
}

struct MapRecorder::Encoder {
	std::thread thread;
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<Frame> queue;
	bool closing = false;
	std::atomic<int> written { 0 };
	std::atomic<bool> failed { false };
	std::string error; // written by the encoder thread; read after it is joined or once `failed` is set

	std::wstring path;
	int max_width = 1280;
	bool show_mouse = true;
	int out_width = 0;
	int out_height = 0;

	bool push(Frame frame) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (closing || queue.size() >= MAX_QUEUED_FRAMES) {
				return false;
			}
			queue.push_back(std::move(frame));
		}
		wake.notify_one();
		return true;
	}

	void finish() {
		{
			std::lock_guard<std::mutex> lock(mutex);
			closing = true;
		}
		wake.notify_one();
		if (thread.joinable()) {
			thread.join();
		}
	}

	// Scales the frame to the video size, converts it to BGRA and draws the pointer.
	void convert(const Frame& frame, std::vector<uint8_t>& bgra) {
		wxImage image(frame.width, frame.height, const_cast<unsigned char*>(frame.rgb.data()), true);
		if (frame.width != out_width || frame.height != out_height) {
			image = image.Scale(out_width, out_height, wxIMAGE_QUALITY_BOX_AVERAGE);
		}
		const unsigned char* src = image.GetData();
		const size_t pixels = static_cast<size_t>(out_width) * out_height;
		bgra.resize(pixels * 4);
		for (size_t i = 0; i < pixels; ++i) {
			bgra[i * 4 + 0] = src[i * 3 + 2];
			bgra[i * 4 + 1] = src[i * 3 + 1];
			bgra[i * 4 + 2] = src[i * 3 + 0];
			bgra[i * 4 + 3] = 255;
		}
		if (show_mouse && frame.mouse_x >= 0 && frame.mouse_y >= 0) {
			const double sx = static_cast<double>(out_width) / frame.width;
			const double sy = static_cast<double>(out_height) / frame.height;
			drawPointer(bgra, out_width, out_height, static_cast<int>(frame.mouse_x * sx), static_cast<int>(frame.mouse_y * sy), std::max(0.6, out_width / 1280.0));
		}
	}

#ifdef _WIN32
	HRESULT createWriter(IMFSinkWriter** out_writer, DWORD* out_stream) {
		IMFSinkWriter* writer = nullptr;
		HRESULT hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &writer);
		if (FAILED(hr)) {
			return hr;
		}
		// Enough for pixel art at this size without making big files.
		const UINT32 bitrate = static_cast<UINT32>(std::clamp(static_cast<double>(out_width) * out_height * OUTPUT_FPS * 0.08, 800'000.0, 4'000'000.0));

		IMFMediaType* output = nullptr;
		hr = MFCreateMediaType(&output);
		if (SUCCEEDED(hr)) {
			output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
			output->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
			output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
			MFSetAttributeSize(output, MF_MT_FRAME_SIZE, out_width, out_height);
			MFSetAttributeRatio(output, MF_MT_FRAME_RATE, OUTPUT_FPS, 1);
			MFSetAttributeRatio(output, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
			hr = writer->AddStream(output, out_stream);
			output->Release();
		}
		IMFMediaType* input = nullptr;
		if (SUCCEEDED(hr)) {
			hr = MFCreateMediaType(&input);
		}
		if (SUCCEEDED(hr)) {
			input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
			input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
			input->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(out_width * 4)); // rows top-down
			MFSetAttributeSize(input, MF_MT_FRAME_SIZE, out_width, out_height);
			MFSetAttributeRatio(input, MF_MT_FRAME_RATE, OUTPUT_FPS, 1);
			MFSetAttributeRatio(input, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
			hr = writer->SetInputMediaType(*out_stream, input, nullptr);
			input->Release();
		}
		if (SUCCEEDED(hr)) {
			hr = writer->BeginWriting();
		}
		if (FAILED(hr)) {
			writer->Release();
			return hr;
		}
		*out_writer = writer;
		return S_OK;
	}

	void run() {
		const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const HRESULT mf = MFStartup(MF_VERSION);
		IMFSinkWriter* writer = nullptr;
		DWORD stream = 0;
		const LONGLONG frame_duration = 10'000'000LL / OUTPUT_FPS; // 100 ns units
		std::vector<uint8_t> bgra;
		const auto fail = [&](const std::string& what, HRESULT hr) {
			if (!failed) {
				error = what + " failed (" + hresultText(hr) + ")";
				failed = true;
			}
		};
		if (FAILED(mf)) {
			fail("Media Foundation start-up", mf);
		}

		while (true) {
			Frame frame;
			{
				std::unique_lock<std::mutex> lock(mutex);
				wake.wait(lock, [&] { return closing || !queue.empty(); });
				if (queue.empty()) {
					break; // closing and drained
				}
				frame = std::move(queue.front());
				queue.pop_front();
			}
			if (failed) {
				continue; // keep draining so the GUI thread never waits on a full queue
			}
			if (!writer) {
				// The first frame decides the video size: at most max_width wide,
				// even sizes (H.264 wants them).
				const double scale = std::min(1.0, static_cast<double>(max_width) / frame.width);
				out_width = std::max(2, static_cast<int>(std::lround(frame.width * scale)) & ~1);
				out_height = std::max(2, static_cast<int>(std::lround(frame.height * scale)) & ~1);
				const HRESULT hr = createWriter(&writer, &stream);
				if (FAILED(hr)) {
					fail("Creating the video file", hr);
					writer = nullptr;
					continue;
				}
			}
			convert(frame, bgra);

			IMFMediaBuffer* buffer = nullptr;
			HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(bgra.size()), &buffer);
			if (SUCCEEDED(hr)) {
				BYTE* data = nullptr;
				hr = buffer->Lock(&data, nullptr, nullptr);
				if (SUCCEEDED(hr)) {
					std::copy(bgra.begin(), bgra.end(), data);
					buffer->Unlock();
					buffer->SetCurrentLength(static_cast<DWORD>(bgra.size()));
				}
			}
			IMFSample* sample = nullptr;
			if (SUCCEEDED(hr)) {
				hr = MFCreateSample(&sample);
			}
			if (SUCCEEDED(hr)) {
				sample->AddBuffer(buffer);
				sample->SetSampleTime(static_cast<LONGLONG>(written.load()) * frame_duration);
				sample->SetSampleDuration(frame_duration);
				hr = writer->WriteSample(stream, sample);
			}
			if (sample) {
				sample->Release();
			}
			if (buffer) {
				buffer->Release();
			}
			if (FAILED(hr)) {
				fail("Writing a frame", hr);
			} else {
				++written;
			}
		}

		if (writer) {
			if (written > 0) {
				const HRESULT hr = writer->Finalize();
				if (FAILED(hr)) {
					fail("Finishing the video", hr);
				}
			}
			writer->Release();
		}
		if (SUCCEEDED(mf)) {
			MFShutdown();
		}
		if (SUCCEEDED(com)) {
			CoUninitialize();
		}
	}
#else
	void run() {
		error = "Recording is only available on Windows.";
		failed = true;
		std::unique_lock<std::mutex> lock(mutex);
		wake.wait(lock, [&] { return closing; });
		queue.clear();
	}
#endif
};

MapRecorder& MapRecorder::get() {
	if (!instance) {
		instance = new MapRecorder();
	}
	return *instance;
}

MapRecorder* MapRecorder::active() {
	return instance && instance->recording ? instance : nullptr;
}

void MapRecorder::clearStatusCallback() {
	if (instance) {
		instance->status_callback = nullptr;
	}
}

void MapRecorder::shutdown() {
	if (instance) {
		instance->status_callback = nullptr;
		instance->stop();
		delete instance;
		instance = nullptr;
	}
}

MapRecorder::MapRecorder() {
	timer.SetOwner(this);
	Bind(wxEVT_TIMER, &MapRecorder::onTimer, this);
}

MapRecorder::~MapRecorder() {
	timer.Stop();
	if (encoder) {
		encoder->finish();
	}
}

bool MapRecorder::start(std::string& error) {
	if (recording) {
		return true;
	}
#ifndef _WIN32
	error = "Recording is only available on Windows.";
	return false;
#else
	if (!g_gui.GetCurrentMapTab()) {
		error = "Open a map first.";
		return false;
	}
	wxString dir = wxstr(g_settings.getString(Config::SCREENSHOT_DIRECTORY));
	if (dir.empty()) {
		dir = wxStandardPaths::Get().GetDocumentsDir();
	}
	if (!wxFileName::DirExists(dir) && !wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
		error = "Cannot create the folder " + std::string(dir.ToUTF8()) + ".";
		return false;
	}
	const wxFileName file(dir, "timelapse_" + wxDateTime::Now().Format("%Y%m%d_%H%M%S") + ".mp4");
	path = std::string(file.GetFullPath().ToUTF8());

	encoder = std::make_unique<Encoder>();
	encoder->path = file.GetFullPath().ToStdWstring();
	encoder->max_width = std::clamp(g_settings.getInteger(Config::RECORD_MAX_WIDTH), 320, 3840);
	encoder->show_mouse = g_settings.getBoolean(Config::RECORD_SHOW_MOUSE);
	Encoder* raw = encoder.get();
	encoder->thread = std::thread([raw]() { raw->run(); });

	recording = true;
	painted = true; // the first tick takes a frame of how the map looks now
	frame_wanted = false;
	started_ms = wxGetLocalTimeMillis().GetValue();
	stopped_ms = 0;
	last_status_ms = started_ms;
	// One sample every 1/(30/speed) s of activity, played at 30 fps.
	const int speed = std::clamp(g_settings.getInteger(Config::RECORD_SPEED), 1, 16);
	timer.Start(std::max(10, 1000 * speed / OUTPUT_FPS));
	if (status_callback) {
		status_callback();
	}
	return true;
#endif
}

void MapRecorder::stop() {
	if (!recording) {
		return;
	}
	recording = false;
	timer.Stop();
	stopped_ms = wxGetLocalTimeMillis().GetValue();
	if (encoder) {
		encoder->finish();
		if (encoder->written == 0 && wxFileName::FileExists(wxString::FromUTF8(path))) {
			wxRemoveFile(wxString::FromUTF8(path)); // nothing moved: no empty file left behind
		}
	}
	if (status_callback) {
		status_callback();
	}
}

int MapRecorder::framesWritten() const {
	return encoder ? encoder->written.load() : 0;
}

double MapRecorder::videoSeconds() const {
	return framesWritten() / static_cast<double>(OUTPUT_FPS);
}

double MapRecorder::realSeconds() const {
	if (started_ms == 0) {
		return 0.0;
	}
	const long long end = recording ? wxGetLocalTimeMillis().GetValue() : stopped_ms;
	return (end - started_ms) / 1000.0;
}

std::string MapRecorder::lastError() const {
	return encoder && encoder->failed ? encoder->error : std::string();
}

void MapRecorder::onTimer(wxTimerEvent&) {
	if (!recording) {
		return;
	}
	if (encoder && encoder->failed) {
		stop();
		return;
	}
	if (painted) {
		// Something was redrawn since the last frame: take one on the next paint.
		painted = false;
		frame_wanted = true;
		if (MapTab* tab = g_gui.GetCurrentMapTab()) {
			if (MapCanvas* canvas = tab->GetCanvas()) {
				canvas->Refresh();
			}
		}
	}
	const long long now = wxGetLocalTimeMillis().GetValue();
	if (now - last_status_ms >= 1000) {
		last_status_ms = now;
		if (status_callback) {
			status_callback();
		}
	}
}

void MapRecorder::onCanvasPainted(MapCanvas* canvas, int width, int height) {
	if (!recording || !encoder || width <= 0 || height <= 0) {
		return;
	}
	MapTab* tab = g_gui.GetCurrentMapTab();
	if (!tab || tab->GetCanvas() != canvas) {
		return; // only the map on screen is recorded
	}
	if (!frame_wanted) {
		painted = true;
		return;
	}
	frame_wanted = false;

	Frame frame;
	frame.width = width;
	frame.height = height;
	frame.rgb.resize(static_cast<size_t>(width) * height * 3);
	ScreenCapture::Capture(width, height, frame.rgb.data());

	// Where the mouse is, in the same pixels as the frame (the canvas may be DPI-scaled).
	const wxPoint mouse = canvas->ScreenToClient(wxGetMousePosition());
	const wxSize size = canvas->GetClientSize();
	if (mouse.x >= 0 && mouse.y >= 0 && mouse.x < size.x && mouse.y < size.y && size.x > 0 && size.y > 0) {
		frame.mouse_x = static_cast<int>(mouse.x * static_cast<double>(width) / size.x);
		frame.mouse_y = static_cast<int>(mouse.y * static_cast<double>(height) / size.y);
	}
	encoder->push(std::move(frame));
}
