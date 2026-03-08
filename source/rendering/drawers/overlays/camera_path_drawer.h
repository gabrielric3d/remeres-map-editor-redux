#ifndef RME_CAMERA_PATH_DRAWER_H_
#define RME_CAMERA_PATH_DRAWER_H_

#include "rendering/core/render_view.h"

class PrimitiveRenderer;
class Editor;
struct DrawingOptions;

class CameraPathDrawer {
public:
	CameraPathDrawer();
	~CameraPathDrawer();

	void draw(PrimitiveRenderer& renderer, const RenderView& view, const DrawingOptions& options, Editor& editor);
};

#endif
