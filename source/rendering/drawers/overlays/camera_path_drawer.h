#ifndef RME_CAMERA_PATH_DRAWER_H_
#define RME_CAMERA_PATH_DRAWER_H_

#include "rendering/core/render_view.h"

class PrimitiveRenderer;
class Editor;
struct DrawingOptions;
struct NVGcontext;

class CameraPathDrawer {
public:
	CameraPathDrawer();
	~CameraPathDrawer();

	void draw(PrimitiveRenderer& renderer, const RenderView& view, const DrawingOptions& options, Editor& editor);
	// Keyframe numbers and the hold badge, in the NanoVG overlay pass.
	void drawLabels(NVGcontext* vg, const RenderView& view, const DrawingOptions& options, Editor& editor);
};

#endif
