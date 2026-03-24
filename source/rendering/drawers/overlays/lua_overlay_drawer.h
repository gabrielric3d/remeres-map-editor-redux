#ifndef RME_LUA_OVERLAY_DRAWER_H_
#define RME_LUA_OVERLAY_DRAWER_H_

#include "rendering/core/drawing_options.h"
#include "rendering/core/render_view.h"

class MapDrawer;
class CoordinateMapper;

class LuaOverlayDrawer {
public:
	LuaOverlayDrawer(MapDrawer* mapDrawer);
	~LuaOverlayDrawer();

	void Draw(const RenderView& view, const DrawingOptions& options);

private:
	MapDrawer* mapDrawer;
};

#endif
