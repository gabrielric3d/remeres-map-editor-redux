## 2024-10-26 - Optimization of Item Tooltips and Viewport Culling
Finding: Generating tooltip data for every item in the viewport involved excessive string allocation and copying, even for items with no tooltip info. Additionally, the render loop calculated screen coordinates twice for every visible tile.
Impact: Significantly reduced memory allocations per frame by avoiding string copies for items without text/description. Reduced arithmetic operations per tile by ~50% in the hot loop.
Learning: `std::string` return by value in accessor methods (`getText`) can be a silent killer in tight loops. Combining visibility checks with coordinate calculation avoids redundant math.

## 2024-10-27 - Unconditional Tooltip Generation Bottleneck
Finding: `TileRenderer::DrawTile` generated tooltip data (allocating strings) for EVERY visible item when `options.show_tooltips` was enabled, causing massive CPU overhead and visual clutter.
Impact: Reduced tooltip generation from O(N) (all visible items) to O(K) (items on hovered tile). Eliminated thousands of allocations per frame when tooltips are enabled.
Learning: Features like "show tooltips" must be context-aware (hover only) to avoid scaling linearly with scene complexity.
