#ifndef RME_PATHING_OVERLAY_DRAWER_H_
#define RME_PATHING_OVERLAY_DRAWER_H_

#include "rendering/core/render_view.h"

struct NVGcontext;
class Editor;

class PathingOverlayDrawer {
public:
	PathingOverlayDrawer() = default;
	~PathingOverlayDrawer() = default;

	void draw(NVGcontext* vg, const RenderView& view, Editor& editor);
};

#endif
