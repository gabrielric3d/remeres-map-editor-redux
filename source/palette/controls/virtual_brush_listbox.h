#ifndef RME_PALETTE_CONTROLS_VIRTUAL_BRUSH_LISTBOX_H_
#define RME_PALETTE_CONTROLS_VIRTUAL_BRUSH_LISTBOX_H_

#include "app/main.h"
#include "util/nanovg_canvas.h"
#include "palette/panels/brush_panel.h"
#include "ui/dcbutton.h" // For RenderSize

/**
 * @class VirtualBrushListBox
 * @brief NanoVG-accelerated list box for displaying brushes with icons and text.
 *
 * Replaces the legacy BrushListBox (wxVListBox).
 */
class VirtualBrushListBox : public NanoVGCanvas, public BrushBoxInterface {
public:
	VirtualBrushListBox(wxWindow* parent, const TilesetCategory* _tileset);
	~VirtualBrushListBox() override;

	wxWindow* GetSelfWindow() override {
		return this;
	}

	// BrushBoxInterface
	void SelectFirstBrush() override;
	Brush* GetSelectedBrush() const override;
	bool SelectBrush(const Brush* brush) override;

protected:
	void OnNanoVGPaint(NVGcontext* vg, int width, int height) override;
	wxSize DoGetBestClientSize() const override;

	// Event Handlers
	void OnMouseDown(wxMouseEvent& event);
	void OnMotion(wxMouseEvent& event);
	void OnKey(wxKeyEvent& event);

	// Internal helpers
	void UpdateLayout();
	int HitTest(int x, int y) const;
	wxRect GetItemRect(int index) const;
	int GetOrCreateBrushTexture(NVGcontext* vg, Brush* brush);
	void DrawBrushItem(NVGcontext* vg, int index, const wxRect& rect);

	int item_height;
	int selected_index;
	int hover_index;
};

#endif
