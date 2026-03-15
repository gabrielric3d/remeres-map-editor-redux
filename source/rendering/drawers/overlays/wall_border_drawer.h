#ifndef RME_WALL_BORDER_DRAWER_H_
#define RME_WALL_BORDER_DRAWER_H_

#include "rendering/core/render_view.h"

struct NVGcontext;
class Editor;

class WallBorderDrawer {
public:
	WallBorderDrawer() = default;
	~WallBorderDrawer() = default;

	void draw(NVGcontext* vg, const RenderView& view, Editor& editor);
};

#endif
