//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// BlackTalon: draws a zone's name as a big label centered over its painted area,
// so the mapper can tell at a glance which region is which. Used by BOTH the
// instance zones and the sound zones.
//
// The drawer is deliberately dumb: it takes a ready list of labels (text + map
// bounds + color) and only worries about placing and sizing them. Whoever calls it
// decides which zones are visible and where the color comes from. That way the
// tricky part -- the map->screen transform, the fixed font size, the edge clamp --
// exists in exactly one place instead of once per zone system.
//
// Two rules that took a couple of bugs to get right:
//   - POSITION follows the map (the text is glued to the terrain and scrolls with
//     it), but SIZE does not follow the zoom. Sizing from the on-screen area made
//     the text grow when zooming out.
//   - RenderView::getScreenPosition returns MAP space, not window space: it never
//     divides by the zoom. See reference in render_view.cpp:95.

#ifndef RME_ZONE_LABEL_DRAWER_H_
#define RME_ZONE_LABEL_DRAWER_H_

#include "rendering/core/render_view.h"

#include <cstdint>
#include <string>
#include <vector>

struct NVGcontext;

// Drawn with NanoVG paths, not loaded from an SVG: the glyph then scales with the
// font, takes the zone's color for free and needs no asset on disk.
enum class ZoneLabelIcon {
	None,
	Music,    // sound zones
	Instance, // instance zones -- stacked copies
};

struct ZoneLabel {
	std::string text;     // the zone name, drawn big
	std::string subtext;  // small line under it (instance count, sound track...) -- may be empty
	int min_x = 0, min_y = 0, max_x = 0, max_y = 0; // painted bounds on this floor, in tiles
	uint8_t r = 255, g = 255, b = 255;              // the zone's own color
	ZoneLabelIcon icon = ZoneLabelIcon::None;
};

class ZoneLabelDrawer {
public:
	ZoneLabelDrawer() = default;
	~ZoneLabelDrawer() = default;

	void draw(NVGcontext* vg, const RenderView& view, int floor, const std::vector<ZoneLabel>& labels);
};

#endif
