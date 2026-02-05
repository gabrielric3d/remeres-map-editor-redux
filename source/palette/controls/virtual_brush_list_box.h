#ifndef RME_PALETTE_CONTROLS_VIRTUAL_BRUSH_LIST_BOX_H_
#define RME_PALETTE_CONTROLS_VIRTUAL_BRUSH_LIST_BOX_H_

#include "app/main.h"
#include "util/nanovg_canvas.h"
#include "palette/panels/brush_panel.h"

class VirtualBrushListBox : public NanoVGCanvas, public BrushBoxInterface {
public:
	VirtualBrushListBox(wxWindow* parent, const TilesetCategory* _tileset);
	virtual ~VirtualBrushListBox();

	wxWindow* GetSelfWindow() override {
		return this;
	}

	// BrushBoxInterface implementation
	void SelectFirstBrush() override;
	Brush* GetSelectedBrush() const override;
	bool SelectBrush(const Brush* brush) override;

protected:
	void OnNanoVGPaint(NVGcontext* vg, int width, int height) override;
	wxSize DoGetBestClientSize() const override;

	// Events
	void OnMouseDown(wxMouseEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnMotion(wxMouseEvent& event);
	void OnLeave(wxMouseEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnChar(wxKeyEvent& event);

	int HitTest(int x, int y) const;
	void EnsureVisible(int index);
	void UpdateScroll();

	// Helpers
	int GetOrCreateBrushTexture(NVGcontext* vg, Brush* brush);

private:
	int selected_index;
	int hover_index;
	int item_height;
};

#endif
