#ifndef RME_RENDERING_SELECTION_DRAWER_H_
#define RME_RENDERING_SELECTION_DRAWER_H_

#include <vector>
#include <wx/gdicmn.h>

struct RenderView;
class MapCanvas;
struct DrawingOptions;
class PrimitiveRenderer;

class SelectionDrawer {
public:
	void draw(PrimitiveRenderer& primitive_renderer, const RenderView& view, const MapCanvas* canvas, const DrawingOptions& options);
	void drawLasso(PrimitiveRenderer& primitive_renderer, const RenderView& view, const std::vector<wxPoint>& screen_points, int cursor_x, int cursor_y);
};

#endif
