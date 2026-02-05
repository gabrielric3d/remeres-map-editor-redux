#ifndef RME_UI_DIALOGS_VIRTUAL_FIND_LIST_H_
#define RME_UI_DIALOGS_VIRTUAL_FIND_LIST_H_

#include "app/main.h"
#include "util/nanovg_canvas.h"
#include "brushes/brush.h"
#include <vector>

class VirtualFindList : public NanoVGCanvas {
public:
	VirtualFindList(wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~VirtualFindList();

	void Clear();
	void SetNoMatches();
	void AddBrush(Brush* brush);
	Brush* GetSelectedBrush() const;

	// List control methods
	void SetSelection(int index);
	int GetSelection() const { return selected_index; }
	int GetItemCount() const { return static_cast<int>(brushlist.size()); }

protected:
	void OnNanoVGPaint(NVGcontext* vg, int width, int height) override;
	wxSize DoGetBestClientSize() const override;

	// Events
	void OnMouseDown(wxMouseEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnMotion(wxMouseEvent& event);
	void OnLeave(wxMouseEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnChar(wxKeyEvent& event); // For catching generic input if needed

	int HitTest(int x, int y) const;
	void EnsureVisible(int index);
	void UpdateScroll();

	// Drawing helpers
	int GetOrCreateBrushTexture(NVGcontext* vg, Brush* brush);

private:
	std::vector<Brush*> brushlist;
	bool cleared;
	bool no_matches;

	int selected_index;
	int hover_index;
	int item_height;
};

#endif
