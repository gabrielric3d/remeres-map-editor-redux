//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_UI_TOOLTIP_COLLECTOR_H_
#define RME_RENDERING_UI_TOOLTIP_COLLECTOR_H_

class TooltipDrawer;
class Editor;
struct RenderView;
struct DrawingOptions;

/**
 * Builds the inspection tooltips by walking the map directly.
 *
 * TileRenderer used to fill them while it blitted, which only held because it
 * visited every tile. The chunk cache skips the tiles it baked, so this walk
 * took the job over: it runs on the camera floor alone, only while tooltips
 * are switched on, and touches no sprite or atlas.
 */
class TooltipCollector {
public:
	static void Collect(
		const RenderView& view,
		const DrawingOptions& options,
		TooltipDrawer& out_tooltip_drawer,
		Editor& editor
	);
};

#endif
