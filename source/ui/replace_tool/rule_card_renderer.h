#ifndef RME_RULE_CARD_RENDERER_H_
#define RME_RULE_CARD_RENDERER_H_

#include <nanovg.h>
#include <cstdint>
#include <string>

class NanoVGCanvas;

class RuleBuilderPanel;
struct ReplacementRule;

class RuleCardRenderer {
public:
	// Layout Constants (Shared)
	static const int CARD_PADDING = 20;
	static const int CARD_MARGIN_X = 10;
	static const int CARD_MARGIN_Y = 10;
	static const int HEADER_HEIGHT = 40;
	static const int ITEM_SIZE = 56;
	static const int ITEM_H = 110;
	static const int ITEM_SPACING = 10;
	static const int ARROW_WIDTH = 60;
	static const int SECTION_GAP = 20;
	static const int CARD_W; // ITEM_SIZE + 20
	// Height of the ITEM/BRUSH pill drawn at the bottom of every slot. Declared
	// here so RuleBuilderPanel::HitTest reserves exactly the same strip.
	static const int KIND_BADGE_H = 14;

	static void DrawRuleItemCard(NanoVGCanvas* canvas, NVGcontext* vg, float x, float y, float w, float h, uint16_t id, bool highlight, bool isTrash, bool showDeleteOverlay, int probability = -1);
	// Same shell as DrawRuleItemCard, but the payload is a brush: a representative
	// item sprite plus the brush name. Unknown brush names are drawn in the Error
	// color so a stale rule is obvious.
	static void DrawRuleBrushCard(NanoVGCanvas* canvas, NVGcontext* vg, float x, float y, float w, float h, const std::string& brushName, bool highlight, bool showDeleteOverlay, int probability = -1);
	// The ITEM/BRUSH pill at the bottom of a slot. x/y/w/h describe the *slot*;
	// the badge places itself inside it.
	static void DrawSlotKindBadge(NVGcontext* vg, float x, float y, float w, float h, bool isBrush, bool hovered);
	static void DrawTrashIcon(NVGcontext* vg, float x, float y, float size, bool highlight);

	// New Static Helper Methods for RuleBuilderPanel
	static void DrawHeader(NVGcontext* vg, float width);
	static void DrawClearButton(NVGcontext* vg, float width, bool isHovered);
	static void DrawSaveButton(NVGcontext* vg, float width, bool isHovered);
	static void DrawRuleCard(RuleBuilderPanel* panel, NVGcontext* vg, int ruleIndex, int y, int width, bool hoverDelete, int dragHoverTargetIdx, int dragHoverType, bool isExternalDrag);
	static void DrawRuleArrow(NVGcontext* vg, float x, float y, float h);
	static void DrawNewRuleArea(NVGcontext* vg, float width, float y, bool isHovered, bool swapHovered);

	// Height of the "Drop Item Here" area. Shared with RuleBuilderPanel::HitTest.
	static const int NEW_RULE_H = 60;
	// Rect of the "Swap Brush" button sitting inside that area. Computed in one
	// place so the hit-test and the drawing can never drift apart.
	static void GetSwapButtonRect(float width, float y, float& bx, float& by, float& bw, float& bh);
};

#endif
