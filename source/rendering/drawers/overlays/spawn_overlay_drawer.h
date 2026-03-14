#ifndef RME_SPAWN_OVERLAY_DRAWER_H_
#define RME_SPAWN_OVERLAY_DRAWER_H_

#include "rendering/core/render_view.h"
#include "rendering/core/drawing_options.h"
#include "map/position.h"
#include <vector>
#include <cstdint>

class PrimitiveRenderer;
class Editor;
struct NVGcontext;

struct SpawnOverlayData {
	Position center;
	int radius;
	int creature_count;
	bool selected;
};

class SpawnOverlayDrawer {
public:
	SpawnOverlayDrawer();
	~SpawnOverlayDrawer();

	void clear();
	void collect(Editor& editor, const RenderView& view, const DrawingOptions& options);
	void draw(PrimitiveRenderer& renderer, const RenderView& view, const DrawingOptions& options);
	void drawLabels(NVGcontext* vg, const RenderView& view);

private:
	std::vector<SpawnOverlayData> overlays;
};

#endif
