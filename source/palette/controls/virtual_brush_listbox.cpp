#include "app/main.h"
#include "palette/controls/virtual_brush_listbox.h"
#include "ui/gui.h"
#include "rendering/core/graphics.h"

#include <glad/glad.h>

#include <nanovg.h>
#include <nanovg_gl.h>

#include <algorithm>
#include <iterator>

VirtualBrushListBox::VirtualBrushListBox(wxWindow* parent, const TilesetCategory* _tileset) :
	NanoVGCanvas(parent, wxID_ANY, wxVSCROLL | wxWANTS_CHARS),
	BrushBoxInterface(_tileset),
	item_height(36), // 32px icon + 4px padding
	selected_index(-1),
	hover_index(-1) {

	Bind(wxEVT_LEFT_DOWN, &VirtualBrushListBox::OnMouseDown, this);
	Bind(wxEVT_MOTION, &VirtualBrushListBox::OnMotion, this);
	Bind(wxEVT_KEY_DOWN, &VirtualBrushListBox::OnKey, this);

	UpdateLayout();
}

VirtualBrushListBox::~VirtualBrushListBox() {
}

void VirtualBrushListBox::UpdateLayout() {
	if (!tileset) return;

	int count = static_cast<int>(tileset->size());
	int contentHeight = count * item_height;

	UpdateScrollbar(contentHeight);
}

wxSize VirtualBrushListBox::DoGetBestClientSize() const {
	return FromDIP(wxSize(200, 300));
}

int VirtualBrushListBox::GetOrCreateBrushTexture(NVGcontext* vg, Brush* brush) {
	if (!brush) {
		return 0;
	}

	// Use brush pointer address as unique ID (stable during runtime)
	uint32_t brushId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(brush) & 0xFFFFFFFF);

	// Check cache first
	int existingTex = GetCachedImage(brushId);
	if (existingTex > 0) {
		return existingTex;
	}

	// Get sprite RGBA data
	Sprite* spr = brush->getSprite();
	if (!spr) {
		spr = g_gui.gfx.getSprite(brush->getLookID());
	}
	if (!spr) {
		return 0;
	}

	// Try to get as GameSprite for RGBA access
	GameSprite* gs = dynamic_cast<GameSprite*>(spr);
	if (!gs || gs->spriteList.empty()) {
		return 0;
	}

	// Calculate composite size
	int w = gs->width * 32;
	int h = gs->height * 32;
	if (w <= 0 || h <= 0) {
		return 0;
	}

	// Create composite RGBA buffer
	size_t bufferSize = static_cast<size_t>(w) * h * 4;
	std::vector<uint8_t> composite(bufferSize, 0);

	// Composite all layers
	int px = (gs->pattern_x >= 3) ? 2 : 0;
	for (int l = 0; l < gs->layers; ++l) {
		for (int sw = 0; sw < gs->width; ++sw) {
			for (int sh = 0; sh < gs->height; ++sh) {
				int idx = gs->getIndex(sw, sh, l, px, 0, 0, 0);
				if (idx < 0 || static_cast<size_t>(idx) >= gs->spriteList.size()) {
					continue;
				}

				auto data = gs->spriteList[idx]->getRGBAData();
				if (!data) {
					continue;
				}

				int part_x = (gs->width - sw - 1) * 32;
				int part_y = (gs->height - sh - 1) * 32;

				for (int sy = 0; sy < 32; ++sy) {
					for (int sx = 0; sx < 32; ++sx) {
						int dy = part_y + sy;
						int dx = part_x + sx;
						int di = (dy * w + dx) * 4;
						int si = (sy * 32 + sx) * 4;

						uint8_t sa = data[si + 3];
						if (sa == 0) {
							continue;
						}

						if (sa == 255) {
							composite[di + 0] = data[si + 0];
							composite[di + 1] = data[si + 1];
							composite[di + 2] = data[si + 2];
							composite[di + 3] = 255;
						} else {
							float a = sa / 255.0f;
							float ia = 1.0f - a;
							composite[di + 0] = static_cast<uint8_t>(data[si + 0] * a + composite[di + 0] * ia);
							composite[di + 1] = static_cast<uint8_t>(data[si + 1] * a + composite[di + 1] * ia);
							composite[di + 2] = static_cast<uint8_t>(data[si + 2] * a + composite[di + 2] * ia);
							composite[di + 3] = std::max(composite[di + 3], sa);
						}
					}
				}
			}
		}
	}

	// Create NanoVG image
	return GetOrCreateImage(brushId, composite.data(), w, h);
}

void VirtualBrushListBox::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
	if (!tileset) return;

	// Update layout if needed (e.g. if tileset changed size, though usually static here)
	int count = static_cast<int>(tileset->size());
	UpdateScrollbar(count * item_height);

	// Calculate visible range
	int scrollPos = GetScrollPosition();
	int startIdx = scrollPos / item_height;
	int endIdx = (scrollPos + height + item_height - 1) / item_height;

	startIdx = std::clamp(startIdx, 0, count);
	endIdx = std::clamp(endIdx, 0, count);

	// Draw visible items
	for (int i = startIdx; i < endIdx; ++i) {
		DrawBrushItem(vg, i, GetItemRect(i));
	}
}

void VirtualBrushListBox::DrawBrushItem(NVGcontext* vg, int i, const wxRect& rect) {
	float x = static_cast<float>(rect.x);
	float y = static_cast<float>(rect.y);
	float w = static_cast<float>(rect.width);
	float h = static_cast<float>(rect.height);

	// Background
	nvgBeginPath(vg);
	nvgRect(vg, x, y, w, h);

	if (i == selected_index) {
		if (HasFocus()) {
			nvgFillColor(vg, nvgRGBA(51, 153, 255, 255)); // Selection Blue
		} else {
			nvgFillColor(vg, nvgRGBA(64, 64, 64, 255)); // Inactive selection
		}
	} else if (i == hover_index) {
		nvgFillColor(vg, nvgRGBA(230, 240, 250, 40)); // Hover light
	} else {
		// Alternating rows? Or just plain transparent/white
		// nvgFillColor(vg, nvgRGBA(255, 255, 255, 0));
		// If opaque background needed:
		// nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
		// But usually lists have white/light bg. Let's rely on Canvas BG or clear.
		// If text is white (dark mode?)
		// Assuming dark theme based on VirtualBrushGrid colors.
	}

	if (i == selected_index || i == hover_index) {
		nvgFill(vg);
	}

	// Brush Icon
	Brush* brush = tileset->brushlist[i];
	if (brush) {
		int tex = GetOrCreateBrushTexture(vg, brush);
		if (tex > 0) {
			float iconSize = 32.0f;
			float iconX = x + 2.0f;
			float iconY = y + (h - iconSize) * 0.5f;

			NVGpaint imgPaint = nvgImagePattern(vg, iconX, iconY, iconSize, iconSize, 0.0f, tex, 1.0f);
			nvgBeginPath(vg);
			nvgRect(vg, iconX, iconY, iconSize, iconSize);
			nvgFillPaint(vg, imgPaint);
			nvgFill(vg);
		}

		// Brush Name
		nvgFontSize(vg, 14.0f);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

		if (i == selected_index && HasFocus()) {
			nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
		} else {
			nvgFillColor(vg, nvgRGBA(0, 0, 0, 255)); // Black text by default? Or white if dark mode?
			// VirtualBrushGrid used dark cards.
			// Let's assume light text on dark bg if grid was dark.
			// But standard listbox is usually white bg, black text.
			// Let's stick to standard colors: Black text.
			// But if background is dark...
			// The glClearColor in NanoVGCanvas defaults to system color usually.
			// Let's check NanoVGCanvas constructor or Init.
			// It doesn't set color. wxGLCanvas defaults.
			// Let's use white text for now as RME seems dark-ish or we want high contrast.
			// Actually, let's use black, and if selected white.
			// Wait, VirtualBrushGrid draws its own dark background cards.
			// Here we are drawing on the canvas directly.
			// If the canvas is white, we need black text.

			// Let's use a safe gray that is visible on both.
			nvgFillColor(vg, nvgRGBA(200, 200, 200, 255));
		}

		nvgText(vg, x + 40.0f, y + h * 0.5f, brush->getName().c_str(), nullptr);
	}
}

wxRect VirtualBrushListBox::GetItemRect(int index) const {
	return wxRect(0, index * item_height, GetClientSize().x, item_height);
}

int VirtualBrushListBox::HitTest(int x, int y) const {
	int scrollPos = GetScrollPosition();
	int realY = y + scrollPos;

	int index = realY / item_height;

	if (index >= 0 && index < static_cast<int>(tileset->size())) {
		return index;
	}
	return -1;
}

void VirtualBrushListBox::OnMouseDown(wxMouseEvent& event) {
	SetFocus(); // Ensure we get key events
	int index = HitTest(event.GetX(), event.GetY());
	if (index != -1 && index != selected_index) {
		selected_index = index;

		// Notify GUI
		wxWindow* w = GetParent();
		while (w) {
			PaletteWindow* pw = dynamic_cast<PaletteWindow*>(w);
			if (pw) {
				g_gui.ActivatePalette(pw);
				break;
			}
			w = w->GetParent();
		}

		g_gui.SelectBrush(tileset->brushlist[selected_index], tileset->getType());
		Refresh();
	}
}

void VirtualBrushListBox::OnMotion(wxMouseEvent& event) {
	int index = HitTest(event.GetX(), event.GetY());

	if (index != hover_index) {
		hover_index = index;
		Refresh();
	}
	event.Skip();
}

void VirtualBrushListBox::OnKey(wxKeyEvent& event) {
	int key = event.GetKeyCode();
	int count = static_cast<int>(tileset->size());

	if (key == WXK_DOWN) {
		if (selected_index < count - 1) {
			selected_index++;
			SelectBrush(tileset->brushlist[selected_index]);
			// Trigger selection
			g_gui.SelectBrush(tileset->brushlist[selected_index], tileset->getType());
		}
	} else if (key == WXK_UP) {
		if (selected_index > 0) {
			selected_index--;
			SelectBrush(tileset->brushlist[selected_index]);
			g_gui.SelectBrush(tileset->brushlist[selected_index], tileset->getType());
		}
	} else if (key == WXK_HOME) {
		SelectFirstBrush();
		if (selected_index != -1)
			g_gui.SelectBrush(tileset->brushlist[selected_index], tileset->getType());
	} else if (key == WXK_END) {
		if (count > 0) {
			selected_index = count - 1;
			SelectBrush(tileset->brushlist[selected_index]);
			g_gui.SelectBrush(tileset->brushlist[selected_index], tileset->getType());
		}
	} else if (key == WXK_PAGEUP) {
		int pageSize = GetClientSize().y / item_height;
		selected_index = std::max(0, selected_index - pageSize);
		SelectBrush(tileset->brushlist[selected_index]);
		g_gui.SelectBrush(tileset->brushlist[selected_index], tileset->getType());
	} else if (key == WXK_PAGEDOWN) {
		int pageSize = GetClientSize().y / item_height;
		selected_index = std::min(count - 1, selected_index + pageSize);
		SelectBrush(tileset->brushlist[selected_index]);
		g_gui.SelectBrush(tileset->brushlist[selected_index], tileset->getType());
	} else {
		event.Skip();
	}
}

void VirtualBrushListBox::SelectFirstBrush() {
	if (tileset->size() > 0) {
		selected_index = 0;
		Refresh();
	}
}

Brush* VirtualBrushListBox::GetSelectedBrush() const {
	if (selected_index >= 0 && selected_index < static_cast<int>(tileset->size())) {
		return tileset->brushlist[selected_index];
	}
	return nullptr;
}

bool VirtualBrushListBox::SelectBrush(const Brush* brush) {
	auto it = std::ranges::find(tileset->brushlist, brush);
	if (it != tileset->brushlist.end()) {
		selected_index = static_cast<int>(std::distance(tileset->brushlist.begin(), it));

		// Ensure visible
		wxRect rect = GetItemRect(selected_index);
		int scrollPos = GetScrollPosition();
		int clientHeight = GetClientSize().y;

		if (rect.y < scrollPos) {
			SetScrollPosition(rect.y);
		} else if (rect.y + rect.height > scrollPos + clientHeight) {
			SetScrollPosition(rect.y + rect.height - clientHeight);
		}

		Refresh();
		return true;
	}
	selected_index = -1;
	Refresh();
	return false;
}
