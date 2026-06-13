#ifndef RME_MOUNTAIN_OVERLAY_DRAWER_H_
#define RME_MOUNTAIN_OVERLAY_DRAWER_H_

#include "rendering/core/render_view.h"

struct NVGcontext;
class Editor;

class MountainOverlayDrawer {
public:
	MountainOverlayDrawer() = default;
	~MountainOverlayDrawer() = default;

	// Ground brushes with z-order >= this threshold are considered mountains
	static constexpr int Z_ORDER_THRESHOLD = 9000;

	void draw(NVGcontext* vg, const RenderView& view, Editor& editor);
};

#endif
