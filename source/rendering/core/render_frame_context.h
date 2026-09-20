//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_RENDERING_CORE_RENDER_FRAME_CONTEXT_H_
#define RME_RENDERING_CORE_RENDER_FRAME_CONTEXT_H_

#include <cstdint>

class AtlasManager;
class GraphicManager;
class ItemDefinitionStore;
struct DrawingOptions;
struct RenderView;

// Per-frame render state bundled into one reference, so the chunk cache does
// not have to reach for globals while baking. The legacy drawers still take
// view/options/house id separately; this is built once per floor in MapDrawer
// and handed to the chunk cache path only.
struct RenderFrameContext {
	AtlasManager& atlas;
	GraphicManager& gfx;
	const ItemDefinitionStore& item_definitions;
	const DrawingOptions& options;
	const RenderView& view;
	long elapsed_time = 0;
	uint32_t current_house_id = 0;
};

#endif
