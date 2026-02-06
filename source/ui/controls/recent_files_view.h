#ifndef RME_UI_CONTROLS_RECENT_FILES_VIEW_H_
#define RME_UI_CONTROLS_RECENT_FILES_VIEW_H_

#include "util/nanovg_canvas.h"
#include <vector>
#include <wx/wx.h>

class RecentFilesView : public NanoVGCanvas {
public:
	RecentFilesView(wxWindow* parent, const std::vector<wxString>& files);
	~RecentFilesView() override;

	void OnNanoVGPaint(NVGcontext* vg, int width, int height) override;
	wxSize DoGetBestClientSize() const override;

	// Events
	void OnMotion(wxMouseEvent& evt);
	void OnLeftDown(wxMouseEvent& evt);
	void OnLeftUp(wxMouseEvent& evt);
	void OnLeave(wxMouseEvent& evt);

	// Helpers
	int HitTest(int x, int y) const;

private:
	std::vector<wxString> m_files;
	int m_hoverIndex = -1;
	int m_pressedIndex = -1;

	const int ITEM_HEIGHT = 50;
	const int PADDING = 10;
};

#endif
