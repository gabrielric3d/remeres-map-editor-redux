#include "ui/controls/recent_files_view.h"
#include "rendering/core/text_renderer.h"
#include <nanovg.h>
#include <wx/filename.h>

RecentFilesView::RecentFilesView(wxWindow* parent, const std::vector<wxString>& files) :
	NanoVGCanvas(parent, wxID_ANY, wxVSCROLL | wxWANTS_CHARS),
	m_files(files) {

	Bind(wxEVT_MOTION, &RecentFilesView::OnMotion, this);
	Bind(wxEVT_LEFT_DOWN, &RecentFilesView::OnLeftDown, this);
	Bind(wxEVT_LEFT_UP, &RecentFilesView::OnLeftUp, this);
	Bind(wxEVT_LEAVE_WINDOW, &RecentFilesView::OnLeave, this);

	UpdateScrollbar(static_cast<int>(m_files.size()) * ITEM_HEIGHT + PADDING);
}

RecentFilesView::~RecentFilesView() {
}

wxSize RecentFilesView::DoGetBestClientSize() const {
	return FromDIP(wxSize(300, 300));
}

void RecentFilesView::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
	// Background
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, width, height);
	nvgFillColor(vg, nvgRGBA(255, 255, 255, 255)); // White/Window bg
	nvgFill(vg);

	int scrollPos = GetScrollPosition();

	int startIdx = scrollPos / ITEM_HEIGHT;
	int endIdx = (scrollPos + height + ITEM_HEIGHT - 1) / ITEM_HEIGHT;

	if (startIdx < 0) startIdx = 0;
	if (endIdx > static_cast<int>(m_files.size())) endIdx = static_cast<int>(m_files.size());

	for (int i = startIdx; i < endIdx; ++i) {
		float x = PADDING;
		float y = i * ITEM_HEIGHT - scrollPos;
		float w = width - PADDING * 2;
		float h = ITEM_HEIGHT - 4; // Spacing

		bool isHover = (i == m_hoverIndex);
		bool isPressed = (i == m_pressedIndex);

		// Background
		nvgBeginPath(vg);
		nvgRoundedRect(vg, x, y, w, h, 4.0f);

		if (isPressed) {
			nvgFillColor(vg, nvgRGBA(0, 120, 215, 30));
		} else if (isHover) {
			nvgFillColor(vg, nvgRGBA(0, 120, 215, 15));
		} else {
			nvgFillColor(vg, nvgRGBA(245, 245, 245, 255));
		}
		nvgFill(vg);

		if (isHover) {
			nvgStrokeColor(vg, nvgRGBA(0, 120, 215, 50));
			nvgStrokeWidth(vg, 1.0f);
			nvgStroke(vg);
		}

		// Icon (Simple generic file icon)
		float iconSz = 24.0f;
		float iconX = x + 10.0f;
		float iconY = y + (h - iconSz) / 2.0f;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, iconX, iconY, iconSz, iconSz, 3.0f);
		nvgFillColor(vg, nvgRGBA(200, 200, 200, 255));
		nvgFill(vg);

		// Text
		wxFileName fn(m_files[i]);
		wxString filename = fn.GetFullName();
		wxString path = fn.GetPath();

		nvgFontSize(vg, 14.0f);
		nvgFontFace(vg, "sans");
		nvgFillColor(vg, nvgRGBA(50, 50, 50, 255));
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		nvgText(vg, iconX + iconSz + 10, y + 6, filename.c_str(), nullptr);

		nvgFontSize(vg, 11.0f);
		nvgFontFace(vg, "sans");
		nvgFillColor(vg, nvgRGBA(150, 150, 150, 255));
		nvgText(vg, iconX + iconSz + 10, y + 24, path.c_str(), nullptr);
	}
}

int RecentFilesView::HitTest(int x, int y) const {
	int scrollPos = GetScrollPosition();
	int realY = y + scrollPos;

	if (x < PADDING || x > GetClientSize().x - PADDING) return -1;

	int index = realY / ITEM_HEIGHT;
	if (index >= 0 && index < static_cast<int>(m_files.size())) {
		int itemTop = index * ITEM_HEIGHT;
		if (realY >= itemTop && realY < itemTop + ITEM_HEIGHT - 4) {
			return index;
		}
	}
	return -1;
}

void RecentFilesView::OnMotion(wxMouseEvent& evt) {
	int idx = HitTest(evt.GetX(), evt.GetY());
	if (idx != m_hoverIndex) {
		m_hoverIndex = idx;
		Refresh();
	}
}

void RecentFilesView::OnLeftDown(wxMouseEvent& evt) {
	int idx = HitTest(evt.GetX(), evt.GetY());
	if (idx != -1) {
		m_pressedIndex = idx;
		CaptureMouse();
		Refresh();
	}
}

void RecentFilesView::OnLeftUp(wxMouseEvent& evt) {
	if (GetCapture() == this) {
		ReleaseMouse();
	}

	if (m_pressedIndex != -1) {
		int idx = HitTest(evt.GetX(), evt.GetY());
		if (idx == m_pressedIndex) {
			// Clicked!
			wxCommandEvent event(wxEVT_BUTTON, GetId());
			event.SetString(m_files[idx]);
			event.SetEventObject(this);
			GetEventHandler()->ProcessEvent(event);
		}
		m_pressedIndex = -1;
		Refresh();
	}
}

void RecentFilesView::OnLeave(wxMouseEvent& evt) {
	m_hoverIndex = -1;
	m_pressedIndex = -1;
	Refresh();
}
