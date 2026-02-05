#ifndef RME_UI_DIALOGS_OUTFIT_COLOR_GRID_H_
#define RME_UI_DIALOGS_OUTFIT_COLOR_GRID_H_

#include "app/main.h"
#include "util/nanovg_canvas.h"
#include "rendering/core/outfit_colors.h"

class OutfitColorGrid : public NanoVGCanvas {
public:
	OutfitColorGrid(wxWindow* parent, wxWindowID id = wxID_ANY);
	virtual ~OutfitColorGrid();

	// Selection
	void SetSelectedColor(int index);
	int GetSelectedColor() const {
		return selected_index;
	}

protected:
	void OnNanoVGPaint(NVGcontext* vg, int width, int height) override;
	wxSize DoGetBestClientSize() const override;

	// Events
	void OnMouseDown(wxMouseEvent& event);
	void OnMotion(wxMouseEvent& event);
	void OnLeave(wxMouseEvent& event);

	int HitTest(int x, int y) const;
	wxRect GetItemRect(int index) const;

private:
	int selected_index;
	int hover_index;

	int item_size;
	int padding;
	int columns;
	int rows;
};

#endif
