#ifndef RME_STAIR_DIRECTION_DRAWER_H_
#define RME_STAIR_DIRECTION_DRAWER_H_

#include "rendering/core/render_view.h"

struct NVGcontext;
class Editor;

class StairDirectionDrawer {
public:
	StairDirectionDrawer() = default;
	~StairDirectionDrawer() = default;

	void draw(NVGcontext* vg, const RenderView& view, Editor& editor);
};

#endif
