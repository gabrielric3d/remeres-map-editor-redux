#include "app/main.h"
#include "ui/replace_tool/rule_card_renderer.h"
#include "util/nanovg_canvas.h"
#include "ui/replace_tool/rule_manager.h"
#include "item_definitions/core/item_definition_store.h"
#include "util/nvg_utils.h"
#include <string>
#include <algorithm>
#include <format>
#include "ui/replace_tool/rule_builder_panel.h"
#include "ui/replace_tool/brush_mapping_service.h"
#include "brushes/brush.h"
#include "ui/theme.h"

const int RuleCardRenderer::CARD_W = RuleCardRenderer::ITEM_SIZE + 20;

void RuleCardRenderer::DrawTrashIcon(NVGcontext* vg, float x, float y, float size, bool highlight) {
	nvgBeginPath(vg);
	float w = size * 0.6f;
	float h = size * 0.7f;
	float ox = x + (size - w) / 2;
	float oy = y + (size - h) / 2;

	nvgFillColor(vg, highlight ? nvgRGBA(255, 80, 80, 255) : nvgRGBA(200, 50, 50, 255));
	nvgRect(vg, ox, oy + 5, w, h - 5);
	nvgRect(vg, ox - 2, oy, w + 4, 3);
	nvgRect(vg, ox + w / 2 - 3, oy - 2, 6, 2);
	nvgFill(vg);
}

void RuleCardRenderer::DrawHeader(NVGcontext* vg, float width) {
	NVGcolor subTextCol = NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle));

	nvgFontSize(vg, 10.0f);
	nvgFillColor(vg, subTextCol);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

	float subTextLabelY = HEADER_HEIGHT / 2.0f;
	float sourceLabelX = CARD_MARGIN_X + CARD_PADDING;
	nvgText(vg, sourceLabelX, subTextLabelY, "IF FOUND", nullptr);

	float targetLabelX = sourceLabelX + CARD_W + 10 + ARROW_WIDTH;
	nvgText(vg, targetLabelX, subTextLabelY, "REPLACE WITH", nullptr);
}

void RuleCardRenderer::DrawClearButton(NVGcontext* vg, float width, bool isHovered) {
	const int BTN_W = 80;
	const int BTN_H = 24;
	const int GAP = 10;
	float cbX = width - BTN_W - GAP;
	float cbY = (HEADER_HEIGHT - BTN_H) / 2.0f;

	nvgBeginPath(vg);
	nvgRoundedRect(vg, cbX, cbY, BTN_W, BTN_H, 4);
	if (isHovered) {
		nvgFillColor(vg, nvgRGBA(200, 50, 50, 255));
	} else {
		nvgFillColor(vg, nvgRGBA(180, 40, 40, 255)); // Always Red
	}
	nvgFill(vg);

	nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
	nvgFontSize(vg, 11.0f);
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	nvgText(vg, cbX + BTN_W / 2.0f, cbY + BTN_H / 2.0f, "CLEAR", nullptr);
}

void RuleCardRenderer::DrawSaveButton(NVGcontext* vg, float width, bool isHovered) {
	const int BTN_W = 80;
	const int BTN_H = 24;
	const int GAP = 10;
	float cbX = width - (BTN_W * 2) - (GAP * 2);
	float cbY = (HEADER_HEIGHT - BTN_H) / 2.0f;

	nvgBeginPath(vg);
	nvgRoundedRect(vg, cbX, cbY, BTN_W, BTN_H, 4);
	if (isHovered) {
		nvgFillColor(vg, nvgRGBA(50, 200, 50, 255));
	} else {
		nvgFillColor(vg, nvgRGBA(40, 180, 40, 255)); // Always Green
	}
	nvgFill(vg);

	nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
	nvgFontSize(vg, 11.0f);
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	nvgText(vg, cbX + BTN_W / 2.0f, cbY + BTN_H / 2.0f, "SAVE", nullptr);
}

void RuleCardRenderer::DrawRuleCard(RuleBuilderPanel* panel, NVGcontext* vg, int ruleIndex, int y, int width, bool hoverDelete, int dragHoverTargetIdx, int dragHoverType, bool isExternalDrag) {
	const auto& rules = panel->GetRules();
	if (ruleIndex < 0 || ruleIndex >= rules.size()) {
		return;
	}
	const auto& rule = rules[ruleIndex];

	// Note: We compute height here to draw the BG correctly
	// Ideally we'd pass height in or compute it in a shared way.
	const float TARGET_START_X = CARD_PADDING + CARD_W + 10 + ARROW_WIDTH;
	float availableWidth = width - CARD_MARGIN_X * 2 - TARGET_START_X - CARD_PADDING;
	if (availableWidth < CARD_W) {
		availableWidth = CARD_W;
	}
	int columns = std::max(1, (int)(availableWidth / (CARD_W + ITEM_SPACING)));

	int targetCount = rule.targets.size();
	// Shared with HitTest/GetRuleHeight so the drawn ghost slot, the clickable
	// area and the card height can never disagree.
	const bool hasTrash = ReplaceToolSuppressAddSlot(rule);
	if (!hasTrash) {
		targetCount++;
	}

	int rows = std::max(1, (targetCount + columns - 1) / columns);
	int ruleH = rows * (ITEM_H + ITEM_SPACING) + CARD_PADDING * 2;

	// 1. Card Background
	nvgBeginPath(vg);
	nvgRoundedRect(vg, CARD_MARGIN_X, y, width - CARD_MARGIN_X * 2, ruleH, 4);
	nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBase)));
	nvgFill(vg);
	nvgStrokeColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBorder)));
	nvgStrokeWidth(vg, 1.0f);
	nvgStroke(vg);

	float startX = CARD_MARGIN_X + CARD_PADDING;
	float itemY = y + CARD_PADDING;

	// Hover types come in as raw ints; compare against the enum instead of magic
	// numbers so appending to HitResult::Type can never silently change meaning.
	using Hit = RuleBuilderPanel::HitResult;

	// 2. Source slot (item or brush)
	bool hoverSource = ((dragHoverType == Hit::Source || dragHoverType == Hit::PickBrush) && dragHoverTargetIdx == -1);
	if (rule.fromKind == SlotKind::Brush) {
		DrawRuleBrushCard(panel, vg, startX, itemY, CARD_W, ITEM_H, rule.fromBrushName, hoverSource, false);
	} else {
		DrawRuleItemCard(panel, vg, startX, itemY, CARD_W, ITEM_H, rule.fromId, hoverSource, false, false);
	}
	DrawSlotKindBadge(vg, startX, itemY, CARD_W, ITEM_H, rule.fromKind == SlotKind::Brush, dragHoverType == Hit::ToggleSourceKind && dragHoverTargetIdx == -1);

	// 3. Arrow
	float arrowX = startX + CARD_W + 10;
	DrawRuleArrow(vg, arrowX, itemY, ITEM_H);

	// 3b. Offset badge (clickable) sitting just above the arrow.
	{
		const bool hasOffset = (rule.offsetX != 0 || rule.offsetY != 0);
		const float bx = arrowX;
		const float bw = (float)ARROW_WIDTH;
		const float bh = 18.0f;
		const float by = y + CARD_PADDING + 35.0f; // raw constants so HitTest matches on any DPI

		NVGcolor accentCol = NvgUtils::ToNvColor(Theme::Get(Theme::Role::Accent));
		nvgBeginPath(vg);
		nvgRoundedRect(vg, bx, by, bw, bh, 4);
		if (hasOffset) {
			nvgFillColor(vg, accentCol);
			nvgFill(vg);
		} else {
			nvgStrokeColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBorder)));
			nvgStrokeWidth(vg, 1.0f);
			nvgStroke(vg);
		}

		std::string label = hasOffset ? std::format("{:+d},{:+d}", rule.offsetX, rule.offsetY) : "offset";
		nvgFontSize(vg, 9.0f);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, hasOffset ? nvgRGBA(255, 255, 255, 255) : NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle)));
		nvgText(vg, bx + bw / 2.0f, by + bh / 2.0f, label.c_str(), nullptr);
	}

	// 4. Targets
	float txStartX = arrowX + ARROW_WIDTH;
	for (size_t j = 0; j < rule.targets.size(); ++j) {
		const auto& target = rule.targets[j];
		int row = j / columns;
		int col = j % columns;
		float tx = txStartX + col * (CARD_W + ITEM_SPACING);
		float ty = y + CARD_PADDING + row * (ITEM_H + ITEM_SPACING);

		bool isThisHovered = (dragHoverTargetIdx == (int)j && (dragHoverType == Hit::Target || dragHoverType == Hit::DeleteTarget || dragHoverType == Hit::PickBrush));

		if (target.kind == SlotKind::Brush) {
			// No delete overlay: the body of a brush slot re-opens the picker,
			// it does not remove the target (toggle back to ITEM for that).
			DrawRuleBrushCard(panel, vg, tx, ty, CARD_W, ITEM_H, target.brushName, isThisHovered, false, target.probability);
		} else {
			bool isTargetTrash = (target.id == TRASH_ITEM_ID);
			DrawRuleItemCard(panel, vg, tx, ty, CARD_W, ITEM_H, target.id, isThisHovered, isTargetTrash, isThisHovered, target.probability);
		}
		DrawSlotKindBadge(vg, tx, ty, CARD_W, ITEM_H, target.kind == SlotKind::Brush, dragHoverType == Hit::ToggleTargetKind && dragHoverTargetIdx == (int)j);
	}

	// 5. Ghost Slot (Add Target)
	if (!hasTrash) {
		int tIdx = rule.targets.size();
		int row = tIdx / columns;
		int col = tIdx % columns;
		float tx = txStartX + col * (CARD_W + ITEM_SPACING);
		float ty = y + CARD_PADDING + row * (ITEM_H + ITEM_SPACING);

		bool hoverAdd = (dragHoverType == Hit::AddTarget);
		NVGcolor accentCol = NvgUtils::ToNvColor(Theme::Get(Theme::Role::Accent));

		nvgBeginPath(vg);
		nvgRoundedRect(vg, tx, ty, CARD_W, ITEM_H, 4);
		if (hoverAdd) {
			nvgStrokeColor(vg, accentCol);
			nvgStrokeWidth(vg, 2.0f);
		} else {
			nvgStrokeColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBorder)));
			nvgStrokeWidth(vg, 1.0f);
		}
		nvgStroke(vg);

		if (hoverAdd && rule.targets.empty() && !isExternalDrag) {
			DrawTrashIcon(vg, tx + (CARD_W - 32.0f) / 2, ty + 8, 32.0f, true);
			nvgFillColor(vg, nvgRGBA(255, 100, 100, 255));
			nvgFontSize(vg, 11.0f);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgText(vg, tx + CARD_W / 2, ty + 44, "REMOVE", nullptr);
		} else {
			nvgFontSize(vg, 30.0f);
			nvgFillColor(vg, hoverAdd ? accentCol : NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle)));
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgText(vg, tx + CARD_W / 2, ty + ITEM_H / 2, "+", nullptr);
		}
	}

	// 6. Delete Rule Button (Top Right)
	NVGcolor subTextCol = NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle));
	nvgFillColor(vg, hoverDelete ? nvgRGBA(255, 80, 80, 255) : subTextCol);
	nvgFontSize(vg, 16.0f);
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	nvgText(vg, CARD_MARGIN_X + (width - CARD_MARGIN_X * 2) - 15, y + 15, "X", nullptr);
}

void RuleCardRenderer::DrawRuleArrow(NVGcontext* vg, float x, float y, float h) {
	float arrowYCenter = y + h / 2.0f;
	NVGcolor subTextCol = NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle));

	nvgBeginPath(vg);
	nvgMoveTo(vg, x, arrowYCenter);
	nvgLineTo(vg, x + ARROW_WIDTH - 20, arrowYCenter);
	nvgStrokeColor(vg, subTextCol);
	nvgStrokeWidth(vg, 2.0f);
	nvgStroke(vg);

	float tailX = x + ARROW_WIDTH - 20;
	nvgBeginPath(vg);
	nvgMoveTo(vg, tailX - 5, arrowYCenter - 5);
	nvgLineTo(vg, tailX, arrowYCenter);
	nvgLineTo(vg, tailX - 5, arrowYCenter + 5);
	nvgStroke(vg);
}

void RuleCardRenderer::GetSwapButtonRect(float width, float y, float& bx, float& by, float& bw, float& bh) {
	// Raw constants (no FromDIP), matching the rest of this renderer so the
	// hit-test lands on the same pixels at any DPI.
	bw = 132.0f;
	bh = 28.0f;
	bx = CARD_MARGIN_X + (width - CARD_MARGIN_X * 2) - bw - 12.0f;
	by = y + ((float)NEW_RULE_H - bh) / 2.0f;
}

void RuleCardRenderer::DrawNewRuleArea(NVGcontext* vg, float width, float y, bool isHovered, bool swapHovered) {
	NVGcolor cardBgTop = NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBase));
	NVGcolor cardBgBot = NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBaseHover));
	NVGcolor borderColor = NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBorder));
	NVGcolor accentCol = NvgUtils::ToNvColor(Theme::Get(Theme::Role::Accent));
	NVGcolor subTextCol = NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle));

	float dropH = (float)NEW_RULE_H;
	float cardX = CARD_MARGIN_X;
	float cardW = width - CARD_MARGIN_X * 2;

	NVGpaint bgPaint = nvgLinearGradient(vg, cardX, y, cardX, y + dropH, cardBgTop, cardBgBot);
	nvgBeginPath(vg);
	nvgRoundedRect(vg, cardX, y, cardW, dropH, 4);
	nvgFillPaint(vg, bgPaint);
	nvgFill(vg);

	if (isHovered) {
		nvgStrokeColor(vg, accentCol);
		nvgStrokeWidth(vg, 2.0f);
		nvgFillColor(vg, nvgRGBAf(accentCol.r, accentCol.g, accentCol.b, 0.1f));
		nvgFill(vg);
	} else {
		nvgStrokeColor(vg, borderColor);
		nvgStrokeWidth(vg, 1.0f);
	}
	nvgStroke(vg);

	float bx, by, bw, bh;
	GetSwapButtonRect(width, y, bx, by, bw, bh);

	// Hint text is centred in what is left of the area once the button is placed,
	// so the two never overlap on a narrow panel.
	nvgFontSize(vg, 14.0f);
	nvgFillColor(vg, subTextCol);
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	nvgText(vg, (cardX + bx) / 2.0f, y + dropH / 2.0f, "Drop Item Here to Add New Rule", nullptr);

	// "Swap Brush" button: expands two brushes into one rule per item pair.
	nvgBeginPath(vg);
	nvgRoundedRect(vg, bx, by, bw, bh, 4.0f);
	if (swapHovered) {
		nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::AccentHover)));
	} else {
		nvgFillColor(vg, accentCol);
	}
	nvgFill(vg);
	nvgStrokeColor(vg, borderColor);
	nvgStrokeWidth(vg, 1.0f);
	nvgStroke(vg);

	nvgFontSize(vg, 12.0f);
	nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextOnAccent)));
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	nvgText(vg, bx + bw / 2.0f, by + bh / 2.0f, "Swap Brush...", nullptr);
}

void RuleCardRenderer::DrawRuleItemCard(NanoVGCanvas* canvas, NVGcontext* vg, float x, float y, float w, float h, uint16_t id, bool highlight, bool isTrash, bool showDeleteOverlay, int probability) {
	NVGpaint bgPaint = nvgLinearGradient(vg, x, y, x, y + h, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBase)), NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBaseHover)));
	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, h, 4.0f);
	nvgFillPaint(vg, bgPaint);
	nvgFill(vg);

	if (highlight) {
		nvgStrokeColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::Accent)));
		nvgStrokeWidth(vg, 2.0f);
	} else {
		nvgStrokeColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBorder)));
		nvgStrokeWidth(vg, 1.0f);
	}
	nvgStroke(vg);

	float drawSize = 32.0f;

	if (isTrash) {
		DrawTrashIcon(vg, x + (w - drawSize) / 2, y + 8, drawSize, highlight);
		nvgFillColor(vg, nvgRGBA(200, 200, 200, 255));
		nvgFontSize(vg, 12.0f);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		nvgText(vg, x + w / 2, y + 44, "REMOVE", nullptr);
	} else if (id != 0) {
		// Use the canvas-provided caching mechanism to avoid leaks
		int tex = canvas->GetOrCreateItemImage(id);
		if (tex > 0) {
			int tw, th;
			nvgImageSize(vg, tex, &tw, &th);
			float scale = drawSize / std::max(tw, th);
			float dw = tw * scale;
			float dh = th * scale;
			float dx = x + (w - dw) / 2;
			float dy = y + 8 + (drawSize - dh) / 2;

			NVGpaint imgPaint = nvgImagePattern(vg, dx, dy, dw, dh, 0.0f, tex, 1.0f);
			nvgBeginPath(vg);
			nvgRect(vg, dx, dy, dw, dh);
			nvgFillPaint(vg, imgPaint);
			nvgFill(vg);
		}

		const auto it = g_item_definitions.get(id);
		std::string label = std::format("{} - {}", id, it.name());

		nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::Text)));
		nvgFontSize(vg, 11.0f);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		nvgTextBox(vg, x + 5, y + 44, w - 10, label.c_str(), nullptr);

		if (probability >= 0) {
			std::string probLabel = std::format("Chance: {}%", probability);
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle)));
			nvgFontSize(vg, 11.0f);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgText(vg, x + w / 2, y + 84, probLabel.c_str(), nullptr);
		}
	}

	if (showDeleteOverlay) {
		float cx = x + w - 8;
		float cy = y + 8;
		float r = 8.0f;
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, r);
		nvgFillColor(vg, nvgRGBA(200, 50, 50, 255));
		nvgFill(vg);
		nvgBeginPath(vg);
		float crossArr = r * 0.5f;
		nvgMoveTo(vg, cx - crossArr, cy - crossArr);
		nvgLineTo(vg, cx + crossArr, cy + crossArr);
		nvgMoveTo(vg, cx + crossArr, cy - crossArr);
		nvgLineTo(vg, cx - crossArr, cy + crossArr);
		nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
		nvgStrokeWidth(vg, 1.5f);
		nvgStroke(vg);
	}
}

void RuleCardRenderer::DrawRuleBrushCard(NanoVGCanvas* canvas, NVGcontext* vg, float x, float y, float w, float h, const std::string& brushName, bool highlight, bool showDeleteOverlay, int probability) {
	// Same shell as the item card so both slot kinds read as one family.
	NVGpaint bgPaint = nvgLinearGradient(vg, x, y, x, y + h, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBase)), NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBaseHover)));
	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, h, 4.0f);
	nvgFillPaint(vg, bgPaint);
	nvgFill(vg);

	if (highlight) {
		nvgStrokeColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::Accent)));
		nvgStrokeWidth(vg, 2.0f);
	} else {
		nvgStrokeColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBorder)));
		nvgStrokeWidth(vg, 1.0f);
	}
	nvgStroke(vg);

	const float drawSize = 32.0f;
	Brush* brush = brushName.empty() ? nullptr : BrushMappingService::FindBrush(brushName);

	if (brushName.empty()) {
		// Slot switched to brush mode but nothing picked yet.
		nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle)));
		nvgFontSize(vg, 11.0f);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgText(vg, x + w / 2, y + h / 2 - KIND_BADGE_H / 2.0f, "Pick brush...", nullptr);
	} else {
		if (brush) {
			const uint16_t previewId = BrushMappingService::GetPreviewItemId(brush);
			if (previewId != 0) {
				int tex = canvas->GetOrCreateItemImage(previewId);
				if (tex > 0) {
					int tw, th;
					nvgImageSize(vg, tex, &tw, &th);
					float scale = drawSize / std::max(tw, th);
					float dw = tw * scale;
					float dh = th * scale;
					float dx = x + (w - dw) / 2;
					float dy = y + 8 + (drawSize - dh) / 2;

					NVGpaint imgPaint = nvgImagePattern(vg, dx, dy, dw, dh, 0.0f, tex, 1.0f);
					nvgBeginPath(vg);
					nvgRect(vg, dx, dy, dw, dh);
					nvgFillPaint(vg, imgPaint);
					nvgFill(vg);
				}
			}
		}

		// A name that no longer resolves to a brush is drawn in the Error color so
		// a rule broken by an edited/renamed brush set is immediately visible.
		nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(brush ? Theme::Role::Text : Theme::Role::Error)));
		nvgFontSize(vg, 11.0f);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		nvgTextBox(vg, x + 5, y + 44, w - 10, brushName.c_str(), nullptr);

		if (probability >= 0) {
			std::string probLabel = std::format("Chance: {}%", probability);
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle)));
			nvgFontSize(vg, 11.0f);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgText(vg, x + w / 2, y + 84, probLabel.c_str(), nullptr);
		}
	}

	if (showDeleteOverlay) {
		float cx = x + w - 8;
		float cy = y + 8;
		float r = 8.0f;
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, r);
		nvgFillColor(vg, nvgRGBA(200, 50, 50, 255));
		nvgFill(vg);
		nvgBeginPath(vg);
		float crossArr = r * 0.5f;
		nvgMoveTo(vg, cx - crossArr, cy - crossArr);
		nvgLineTo(vg, cx + crossArr, cy + crossArr);
		nvgMoveTo(vg, cx + crossArr, cy - crossArr);
		nvgLineTo(vg, cx - crossArr, cy + crossArr);
		nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
		nvgStrokeWidth(vg, 1.5f);
		nvgStroke(vg);
	}
}

void RuleCardRenderer::DrawSlotKindBadge(NVGcontext* vg, float x, float y, float w, float h, bool isBrush, bool hovered) {
	// Raw constants (no FromDIP) so the pill lands inside the strip that
	// RuleBuilderPanel::HitTest reserves for it.
	const float bh = (float)KIND_BADGE_H - 3.0f;
	const float by = y + h - (float)KIND_BADGE_H + 1.0f;
	const float bx = x + 6.0f;
	const float bw = w - 12.0f;

	NVGcolor accentCol = NvgUtils::ToNvColor(Theme::Get(Theme::Role::Accent));
	NVGcolor accentHoverCol = NvgUtils::ToNvColor(Theme::Get(Theme::Role::AccentHover));

	nvgBeginPath(vg);
	nvgRoundedRect(vg, bx, by, bw, bh, bh / 2.0f);
	if (isBrush) {
		// Filled pill, mirroring the "active" look of the offset badge.
		nvgFillColor(vg, hovered ? accentHoverCol : accentCol);
		nvgFill(vg);
	} else {
		nvgStrokeColor(vg, hovered ? accentCol : NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBorder)));
		nvgStrokeWidth(vg, 1.0f);
		nvgStroke(vg);
	}

	nvgFontSize(vg, 8.0f);
	nvgFontFace(vg, "sans");
	nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
	nvgFillColor(vg, isBrush ? NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextOnAccent)) : NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle)));
	nvgText(vg, bx + bw / 2.0f, by + bh / 2.0f, isBrush ? "BRUSH" : "ITEM", nullptr);
}
