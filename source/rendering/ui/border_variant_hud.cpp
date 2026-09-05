//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "rendering/ui/border_variant_hud.h"
#include "rendering/ui/toast_renderer.h"
#include "brushes/ground/ground_brush.h"
#include "app/settings.h"
#include "ui/gui.h"

#include <nanovg.h>
#include <cctype>
#include <cstdlib>

namespace {

// Ground brush currently held by the palette, or nullptr for anything else.
GroundBrush* CurrentGroundBrush() {
	Brush* brush = g_gui.GetCurrentBrush();
	return brush ? brush->as<GroundBrush>() : nullptr;
}

// Distinct accent per variant so the badge reads at a glance without counting digits.
NVGcolor VariantColor(int variant) {
	switch (variant) {
		case 1:
			return nvgRGBA(90, 170, 255, 235);
		case 2:
			return nvgRGBA(255, 170, 60, 235);
		case 3:
			return nvgRGBA(120, 220, 130, 235);
		case 4:
			return nvgRGBA(230, 120, 220, 235);
		default:
			return nvgRGBA(220, 220, 220, 235);
	}
}

// "1/2" style counter: which of the brush's declared variants is in use.
std::string VariantPosition(uint32_t mask, int variant) {
	int total = 0;
	int index = 0;
	for (int i = 1; i <= 32; ++i) {
		if (mask & (1u << (i - 1))) {
			++total;
			if (i == variant) {
				index = total;
			}
		}
	}
	if (total <= 1 || index == 0) {
		return std::to_string(variant);
	}
	return std::to_string(index) + "/" + std::to_string(total);
}

} // namespace

int BorderVariantHUD::GetHotkeyCode() {
	const std::string key = g_settings.getString(Config::BORDER_VARIANT_HOTKEY);
	if (key.empty() || key == "None") {
		return 0;
	}
	if (key.size() == 1) {
		return std::toupper(static_cast<unsigned char>(key[0]));
	}
	if ((key[0] == 'F' || key[0] == 'f') && key.size() <= 3) {
		const int number = std::atoi(key.c_str() + 1);
		if (number >= 1 && number <= 12) {
			return WXK_F1 + (number - 1);
		}
	}
	return 0;
}

int BorderVariantHUD::GetHotkeyVirtualKey() {
	const int code = GetHotkeyCode();
	if (code == 0) {
		return 0;
	}
	if (code >= WXK_F1 && code <= WXK_F12) {
		return 0x70 + (code - WXK_F1); // VK_F1..VK_F12
	}
	// Letters and digits share their ASCII value with the virtual-key code.
	return (code >= 0x20 && code <= 0x7F) ? code : 0;
}

void BorderVariantHUD::CycleAndNotify() {
	GroundBrush* ground = CurrentGroundBrush();
	const int variant = GroundBrush::cycleActiveBorderVariant(ground);

	std::string message = "Border variant: " + std::to_string(variant);
	if (ground && ground->hasBorderVariants()) {
		message += " (" + VariantPosition(ground->getVariantMask(), ground->getEffectiveVariant()) + " of " + ground->getName() + ")";
	} else if (ground) {
		// Nothing to cycle on this brush: it tags at most one variant. Borders marked
		// enabled="false" are dropped while loading, which is the usual reason the
		// second variant silently goes missing.
		message += ground->getVariantMask() == 0
			? " - " + ground->getName() + " declares no border variants"
			: " - " + ground->getName() + " declares only this one (are the others disabled?)";
	} else if (GroundBrush::getGlobalVariantMask() == 0) {
		message += " - no brush declares border variants yet";
	}
	g_toast.Show(message);
	g_gui.RefreshView();
}

std::string BorderVariantHUD::GetStatusLabel() {
	GroundBrush* ground = CurrentGroundBrush();
	if (ground && ground->hasBorderVariants()) {
		const int effective = ground->getEffectiveVariant();
		return "Border " + VariantPosition(ground->getVariantMask(), effective) + "  -  " + ground->getName();
	}

	// No brush with a choice in hand: only worth showing when the active variant
	// is not the default one, so the user does not forget the toggle is flipped.
	const int active = GroundBrush::getActiveBorderVariant();
	if (active != 1) {
		return "Border variant " + std::to_string(active);
	}
	return std::string();
}

void BorderVariantHUD::Draw(NVGcontext* vg, int canvas_width, int canvas_height) {
	if (!vg) {
		return;
	}
	const std::string label = GetStatusLabel();
	if (label.empty()) {
		return;
	}

	GroundBrush* ground = CurrentGroundBrush();
	const int variant = ground && ground->hasBorderVariants()
		? ground->getEffectiveVariant()
		: GroundBrush::getActiveBorderVariant();

	nvgSave(vg);

	nvgFontSize(vg, 14.0f);
	nvgFontFace(vg, "sans");

	float bounds[4];
	nvgTextBounds(vg, 0, 0, label.c_str(), nullptr, bounds);
	const float text_w = bounds[2] - bounds[0];
	const float text_h = bounds[3] - bounds[1];

	const float pad_x = 10.0f;
	const float pad_y = 7.0f;
	const float swatch = 12.0f;
	const float box_w = text_w + swatch + pad_x * 3.0f;
	const float box_h = text_h + pad_y * 2.0f;
	const float box_x = 12.0f;
	const float box_y = canvas_height - box_h - 12.0f;

	nvgBeginPath(vg);
	nvgRoundedRect(vg, box_x, box_y, box_w, box_h, 5.0f);
	nvgFillColor(vg, nvgRGBA(20, 20, 20, 205));
	nvgFill(vg);
	nvgStrokeColor(vg, VariantColor(variant));
	nvgStrokeWidth(vg, 1.5f);
	nvgStroke(vg);

	nvgBeginPath(vg);
	nvgRoundedRect(vg, box_x + pad_x, box_y + (box_h - swatch) / 2.0f, swatch, swatch, 3.0f);
	nvgFillColor(vg, VariantColor(variant));
	nvgFill(vg);

	nvgFillColor(vg, nvgRGBA(240, 240, 240, 235));
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	nvgText(vg, box_x + pad_x * 2.0f + swatch, box_y + box_h / 2.0f, label.c_str(), nullptr);

	nvgRestore(vg);
}
