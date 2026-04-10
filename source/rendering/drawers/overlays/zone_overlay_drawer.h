//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ZONE_OVERLAY_DRAWER_H_
#define RME_ZONE_OVERLAY_DRAWER_H_

#include "rendering/core/render_view.h"
#include "rendering/core/drawing_options.h"
#include <vector>

class PrimitiveRenderer;
struct NVGcontext;
struct ForcedLightZone;

class ZoneOverlayDrawer {
public:
	ZoneOverlayDrawer() = default;
	~ZoneOverlayDrawer() = default;

	// Draw zone boundaries for all visible zones on the current floor
	void draw(PrimitiveRenderer& renderer, const RenderView& view, const DrawingOptions& options, int floor);

	// Draw zone labels using NanoVG
	void drawLabels(NVGcontext* vg, const RenderView& view, const DrawingOptions& options, int floor);

private:
	void drawRectangularZone(PrimitiveRenderer& renderer, const ForcedLightZone& zone, const RenderView& view);
	void drawCircularZone(PrimitiveRenderer& renderer, const ForcedLightZone& zone, const RenderView& view);
};

#endif
