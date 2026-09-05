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

#ifndef RME_BORDER_VARIANT_HUD_H_
#define RME_BORDER_VARIANT_HUD_H_

#include <string>

struct NVGcontext;

/**
 * @brief Hotkey + on-canvas badge for the ground brush border variant.
 *
 * Ground brushes can declare the same border twice with different shapes
 * (variant="1" / variant="2" in grounds.xml, edited in the Brushes Editor).
 * The variant currently painted is a single editor-wide setting
 * (Config::ACTIVE_BORDER_VARIANT); this hotkey cycles it without touching the
 * brush files, and the badge keeps the current choice visible on the canvas.
 */
namespace BorderVariantHUD {

// wx keycode of the configurable toggle key (Preferences > Hotkeys),
// or 0 when set to "None".
int GetHotkeyCode();

// Windows virtual-key code for the same key, or 0 when disabled. MainFrame uses
// it to stop a menu accelerator from swallowing the key while the canvas has focus.
int GetHotkeyVirtualKey();

// Cycles to the next variant declared by the brush in hand and toasts the result.
void CycleAndNotify();

// One-line description of the active variant, empty when there is nothing worth
// showing (no brush with variants selected and the default variant active).
std::string GetStatusLabel();

// Draws the badge in the bottom-left corner. No-op when GetStatusLabel() is empty.
void Draw(NVGcontext* vg, int canvas_width, int canvas_height);

} // namespace BorderVariantHUD

#endif // RME_BORDER_VARIANT_HUD_H_
