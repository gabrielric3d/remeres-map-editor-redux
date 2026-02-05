#include "palette/controls/virtual_brush_list_box.h"
#include "ui/gui.h"
#include "rendering/core/graphics.h"
#include "palette/palette_window.h"
#include "app/settings.h"
#include <glad/glad.h>
#include <nanovg.h>
#include <nanovg_gl.h>

VirtualBrushListBox::VirtualBrushListBox(wxWindow* parent, const TilesetCategory* _tileset) :
	NanoVGCanvas(parent, wxID_ANY, wxVSCROLL | wxWANTS_CHARS),
	BrushBoxInterface(_tileset),
	selected_index(-1),
	hover_index(-1),
	item_height(32) {

	Bind(wxEVT_LEFT_DOWN, &VirtualBrushListBox::OnMouseDown, this);
	Bind(wxEVT_MOTION, &VirtualBrushListBox::OnMotion, this);
	Bind(wxEVT_LEAVE_WINDOW, &VirtualBrushListBox::OnLeave, this);
	Bind(wxEVT_KEY_DOWN, &VirtualBrushListBox::OnKeyDown, this);
	Bind(wxEVT_CHAR, &VirtualBrushListBox::OnChar, this);

	UpdateScroll();
}

VirtualBrushListBox::~VirtualBrushListBox() {
}

void VirtualBrushListBox::UpdateScroll() {
	if (tileset) {
		UpdateScrollbar(static_cast<int>(tileset->size()) * item_height);
	}
}

void VirtualBrushListBox::SelectFirstBrush() {
	if (tileset && tileset->size() > 0) {
		selected_index = 0;
		EnsureVisible(0);
		Refresh();
	}
}

Brush* VirtualBrushListBox::GetSelectedBrush() const {
	if (selected_index >= 0 && selected_index < static_cast<int>(tileset->size())) {
		return tileset->brushlist[selected_index];
	}
	// Fallback to first if nothing selected but list not empty (mimic original behavior?)
	// Original BrushListBox::GetSelectedBrush checks selection, if not found, returns index 0.
	if (tileset && tileset->size() > 0) {
		return tileset->brushlist[0];
	}
	return nullptr;
}

bool VirtualBrushListBox::SelectBrush(const Brush* brush) {
	if (!tileset) return false;

	for (size_t i = 0; i < tileset->size(); ++i) {
		if (tileset->brushlist[i] == brush) {
			selected_index = static_cast<int>(i);
			EnsureVisible(selected_index);
			Refresh();
			return true;
		}
	}
	return false;
}

void VirtualBrushListBox::EnsureVisible(int index) {
	if (index < 0) return;

	int top = index * item_height;
	int bottom = top + item_height;
	int scrollPos = GetScrollPosition();
	int clientH = GetClientSize().y;

	if (top < scrollPos) {
		SetScrollPosition(top);
	} else if (bottom > scrollPos + clientH) {
		SetScrollPosition(bottom - clientH);
	}
}

wxSize VirtualBrushListBox::DoGetBestClientSize() const {
	return FromDIP(wxSize(200, 300));
}

void VirtualBrushListBox::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
	// Background
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, width, std::max(height, GetScrollPosition() + height));
	nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
	nvgFill(vg);

	if (!tileset || tileset->size() == 0) return;

	int scrollPos = GetScrollPosition();
	int startIdx = scrollPos / item_height;
	int endIdx = (scrollPos + height + item_height - 1) / item_height;

	startIdx = std::max(0, startIdx);
	endIdx = std::min((int)tileset->size(), endIdx);

	for (int i = startIdx; i < endIdx; ++i) {
		int y = i * item_height;

		// Selection Background
		if (i == selected_index) {
			nvgBeginPath(vg);
			nvgRect(vg, 0, y, width, item_height);
			if (HasFocus()) {
				nvgFillColor(vg, nvgRGBA(0, 120, 215, 255));
			} else {
				nvgFillColor(vg, nvgRGBA(200, 200, 200, 255));
			}
			nvgFill(vg);
		} else if (i == hover_index) {
			nvgBeginPath(vg);
			nvgRect(vg, 0, y, width, item_height);
			nvgFillColor(vg, nvgRGBA(240, 240, 240, 255));
			nvgFill(vg);
		}

		Brush* brush = tileset->brushlist[i];

		// Icon
		int tex = GetOrCreateBrushTexture(vg, brush);
		if (tex > 0) {
			NVGpaint imgPaint = nvgImagePattern(vg, 4, y, 32, 32, 0, tex, 1.0f);
			nvgBeginPath(vg);
			nvgRect(vg, 4, y, 32, 32);
			nvgFillPaint(vg, imgPaint);
			nvgFill(vg);
		}

		// Text
		if (i == selected_index && HasFocus()) {
			nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
		} else {
			nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
		}
		nvgFontSize(vg, 14.0f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgText(vg, 40, y + item_height / 2, wxstr(brush->getName()).ToUTF8().data(), nullptr);
	}
}

int VirtualBrushListBox::GetOrCreateBrushTexture(NVGcontext* vg, Brush* brush) {
	if (!brush) return 0;

	uint32_t brushId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(brush) & 0xFFFFFFFF);
	int existingTex = GetCachedImage(brushId);
	if (existingTex > 0) return existingTex;

	// Duplicated logic from VirtualBrushGrid/FindList to ensure self-containment
	Sprite* spr = brush->getSprite();
	if (!spr) spr = g_gui.gfx.getSprite(brush->getLookID());
	if (!spr) return 0;

	GameSprite* gs = dynamic_cast<GameSprite*>(spr);
	if (!gs || gs->spriteList.empty()) return 0;

	int w = gs->width * 32;
	int h = gs->height * 32;
	size_t bufferSize = w * h * 4;
	std::vector<uint8_t> composite(bufferSize, 0);

	int px = (gs->pattern_x >= 3) ? 2 : 0;
	for (int l = 0; l < gs->layers; ++l) {
		for (int sw = 0; sw < gs->width; ++sw) {
			for (int sh = 0; sh < gs->height; ++sh) {
				int idx = gs->getIndex(sw, sh, l, px, 0, 0, 0);
				if (idx < 0 || (size_t)idx >= gs->spriteList.size()) continue;
				auto data = gs->spriteList[idx]->getRGBAData();
				if (!data) continue;

				int part_x = (gs->width - sw - 1) * 32;
				int part_y = (gs->height - sh - 1) * 32;

				for (int sy = 0; sy < 32; ++sy) {
					for (int sx = 0; sx < 32; ++sx) {
						int dy = part_y + sy;
						int dx = part_x + sx;
						int di = (dy * w + dx) * 4;
						int si = (sy * 32 + sx) * 4;

						uint8_t sa = data[si + 3];
						if (sa == 0) continue;

						if (sa == 255) {
							composite[di] = data[si];
							composite[di+1] = data[si+1];
							composite[di+2] = data[si+2];
							composite[di+3] = 255;
						} else {
							float a = sa / 255.0f;
							float ia = 1.0f - a;
							composite[di] = (uint8_t)(data[si] * a + composite[di] * ia);
							composite[di+1] = (uint8_t)(data[si+1] * a + composite[di+1] * ia);
							composite[di+2] = (uint8_t)(data[si+2] * a + composite[di+2] * ia);
							composite[di+3] = std::max(composite[di+3], sa);
						}
					}
				}
			}
		}
	}

	return GetOrCreateImage(brushId, composite.data(), w, h);
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
	SetFocus();
	int index = HitTest(event.GetX(), event.GetY());
	if (index != -1 && index != selected_index) {
		selected_index = index;
		Refresh();

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
	}
}

void VirtualBrushListBox::OnMotion(wxMouseEvent& event) {
	int index = HitTest(event.GetX(), event.GetY());
	if (index != hover_index) {
		hover_index = index;
		Refresh();
	}

	if (index != -1) {
		Brush* brush = tileset->brushlist[index];
		if (brush) {
			wxString tip = wxstr(brush->getName());
			if (GetToolTipText() != tip) {
				SetToolTip(tip);
			}
		}
	} else {
		UnsetToolTip();
	}
	event.Skip();
}

void VirtualBrushListBox::OnLeave(wxMouseEvent& event) {
	hover_index = -1;
	Refresh();
	event.Skip();
}

void VirtualBrushListBox::OnKeyDown(wxKeyEvent& event) {
	int key = event.GetKeyCode();
	int count = (int)tileset->size();

	if (count == 0) {
		event.Skip();
		return;
	}

	int nextSelection = selected_index;
	int visibleRows = GetClientSize().y / item_height;
	bool handled = false;

	if (key == WXK_DOWN) {
		nextSelection++;
		handled = true;
	} else if (key == WXK_UP) {
		nextSelection--;
		handled = true;
	} else if (key == WXK_PAGEDOWN) {
		nextSelection += visibleRows;
		handled = true;
	} else if (key == WXK_PAGEUP) {
		nextSelection -= visibleRows;
		handled = true;
	} else if (key == WXK_HOME) {
		nextSelection = 0;
		handled = true;
	} else if (key == WXK_END) {
		nextSelection = count - 1;
		handled = true;
	}

	if (handled) {
		nextSelection = std::clamp(nextSelection, 0, count - 1);
		if (nextSelection != selected_index) {
			selected_index = nextSelection;
			EnsureVisible(selected_index);
			Refresh();

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
		}
	} else {
		// Forward key events logic
		if (g_settings.getInteger(Config::LISTBOX_EATS_ALL_EVENTS)) {
			event.Skip(true);
		} else {
			if (g_gui.GetCurrentTab() != nullptr) {
				g_gui.GetCurrentMapTab()->GetEventHandler()->AddPendingEvent(event);
			}
		}
	}
}

void VirtualBrushListBox::OnChar(wxKeyEvent& event) {
	if (g_gui.GetCurrentTab() != nullptr) {
		g_gui.GetCurrentMapTab()->GetEventHandler()->AddPendingEvent(event);
	}
}

void VirtualBrushListBox::OnSize(wxSizeEvent& event) {
	UpdateScroll();
	Refresh();
	event.Skip();
}
