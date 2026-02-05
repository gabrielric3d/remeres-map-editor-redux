#include "ui/dialogs/virtual_find_list.h"
#include "ui/gui.h"
#include "rendering/core/graphics.h"
#include <glad/glad.h>
#include <nanovg.h>
#include <nanovg_gl.h>
#include <spdlog/spdlog.h>

VirtualFindList::VirtualFindList(wxWindow* parent, wxWindowID id) :
	NanoVGCanvas(parent, id, wxVSCROLL | wxWANTS_CHARS),
	cleared(true),
	no_matches(false),
	selected_index(-1),
	hover_index(-1),
	item_height(32) {

	Bind(wxEVT_LEFT_DOWN, &VirtualFindList::OnMouseDown, this);
	Bind(wxEVT_LEFT_DCLICK, &VirtualFindList::OnMouseDown, this); // Handle DClick
	Bind(wxEVT_MOTION, &VirtualFindList::OnMotion, this);
	Bind(wxEVT_LEAVE_WINDOW, &VirtualFindList::OnLeave, this);
	Bind(wxEVT_KEY_DOWN, &VirtualFindList::OnKeyDown, this);
	Bind(wxEVT_CHAR, &VirtualFindList::OnChar, this);
}

VirtualFindList::~VirtualFindList() {
}

void VirtualFindList::Clear() {
	cleared = true;
	no_matches = false;
	brushlist.clear();
	selected_index = -1;
	hover_index = -1;
	ClearImageCache();
	UpdateScroll();
	Refresh();
}

void VirtualFindList::SetNoMatches() {
	cleared = false;
	no_matches = true;
	brushlist.clear();
	selected_index = -1;
	hover_index = -1;
	UpdateScroll();
	Refresh();
}

void VirtualFindList::AddBrush(Brush* brush) {
	if (cleared || no_matches) {
		cleared = false;
		no_matches = false;
		brushlist.clear();
	}
	brushlist.push_back(brush);
	UpdateScroll();
	Refresh();
}

Brush* VirtualFindList::GetSelectedBrush() const {
	if (selected_index >= 0 && selected_index < (int)brushlist.size()) {
		return brushlist[selected_index];
	}
	return nullptr;
}

void VirtualFindList::SetSelection(int index) {
	if (index >= -1 && index < (int)brushlist.size()) {
		selected_index = index;
		EnsureVisible(index);
		Refresh();

		// Notify parent
		wxCommandEvent event(wxEVT_LISTBOX, GetId());
		event.SetEventObject(this);
		event.SetInt(selected_index);
		ProcessEvent(event);
	}
}

wxSize VirtualFindList::DoGetBestClientSize() const {
	return FromDIP(wxSize(300, 400));
}

void VirtualFindList::UpdateScroll() {
	int count = (cleared || no_matches) ? 1 : (int)brushlist.size();
	UpdateScrollbar(count * item_height);
}

void VirtualFindList::EnsureVisible(int index) {
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

void VirtualFindList::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
	// Background
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, width, std::max(height, GetScrollPosition() + height));
	nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
	nvgFill(vg);

	if (cleared) {
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
		nvgFontSize(vg, 14.0f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgText(vg, 40, item_height / 2, "Please enter your search string.", nullptr);
		return;
	}

	if (no_matches) {
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
		nvgFontSize(vg, 14.0f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgText(vg, 40, item_height / 2, "No matches for your search.", nullptr);
		return;
	}

	int scrollPos = GetScrollPosition();
	int startIdx = scrollPos / item_height;
	int endIdx = (scrollPos + height + item_height - 1) / item_height;

	startIdx = std::max(0, startIdx);
	endIdx = std::min((int)brushlist.size(), endIdx);

	for (int i = startIdx; i < endIdx; ++i) {
		int y = i * item_height;

		// Selection Background
		if (i == selected_index) {
			nvgBeginPath(vg);
			nvgRect(vg, 0, y, width, item_height);
			if (HasFocus()) {
				nvgFillColor(vg, nvgRGBA(0, 120, 215, 255)); // Standard selection blue
			} else {
				nvgFillColor(vg, nvgRGBA(200, 200, 200, 255)); // Grey when inactive
			}
			nvgFill(vg);
		} else if (i == hover_index) {
			nvgBeginPath(vg);
			nvgRect(vg, 0, y, width, item_height);
			nvgFillColor(vg, nvgRGBA(240, 240, 240, 255));
			nvgFill(vg);
		}

		Brush* brush = brushlist[i];
		if (!brush) continue;

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

int VirtualFindList::GetOrCreateBrushTexture(NVGcontext* vg, Brush* brush) {
	if (!brush) return 0;

	// Check cache
	uint32_t brushId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(brush) & 0xFFFFFFFF);
	int existingTex = GetCachedImage(brushId);
	if (existingTex > 0) return existingTex;

	// Create texture logic (simplified from VirtualBrushGrid - assume basic sprite)
	Sprite* spr = brush->getSprite();
	if (!spr) spr = g_gui.gfx.getSprite(brush->getLookID());
	if (!spr) return 0;

	// Convert using wxImage if needed, or use GameSprite logic if sophisticated rendering needed
	// For now, let's use the robust GameSprite logic from VirtualBrushGrid as it handles layers

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

int VirtualFindList::HitTest(int x, int y) const {
	int scrollPos = GetScrollPosition();
	int realY = y + scrollPos;
	int index = realY / item_height;

	if (index >= 0 && index < (int)brushlist.size()) {
		return index;
	}
	return -1;
}

void VirtualFindList::OnMouseDown(wxMouseEvent& event) {
	SetFocus();
	int index = HitTest(event.GetX(), event.GetY());
	if (index != -1) {
		SetSelection(index);
		if (event.LeftDClick()) {
			wxCommandEvent evt(wxEVT_LISTBOX_DCLICK, GetId());
			evt.SetEventObject(this);
			evt.SetInt(selected_index);
			ProcessEvent(evt);
		}
	}
}

void VirtualFindList::OnMotion(wxMouseEvent& event) {
	int index = HitTest(event.GetX(), event.GetY());
	if (index != hover_index) {
		hover_index = index;
		Refresh();
	}
	event.Skip();
}

void VirtualFindList::OnLeave(wxMouseEvent& event) {
	hover_index = -1;
	Refresh();
	event.Skip();
}

void VirtualFindList::OnKeyDown(wxKeyEvent& event) {
	int key = event.GetKeyCode();
	int count = (int)brushlist.size();

	if (count == 0) {
		event.Skip();
		return;
	}

	int nextSelection = selected_index;
	int visibleRows = GetClientSize().y / item_height;

	if (key == WXK_DOWN) {
		nextSelection++;
	} else if (key == WXK_UP) {
		nextSelection--;
	} else if (key == WXK_PAGEDOWN) {
		nextSelection += visibleRows;
	} else if (key == WXK_PAGEUP) {
		nextSelection -= visibleRows;
	} else if (key == WXK_HOME) {
		nextSelection = 0;
	} else if (key == WXK_END) {
		nextSelection = count - 1;
	} else if (key == WXK_RETURN) {
		wxCommandEvent evt(wxEVT_LISTBOX_DCLICK, GetId()); // Treat enter as dclick
		evt.SetEventObject(this);
		evt.SetInt(selected_index);
		ProcessEvent(evt);
		return;
	} else {
		event.Skip();
		return;
	}

	nextSelection = std::clamp(nextSelection, 0, count - 1);
	if (nextSelection != selected_index) {
		SetSelection(nextSelection);
	}
}

void VirtualFindList::OnChar(wxKeyEvent& event) {
	// Forward to parent if needed for quick search?
	// Existing behavior forwards keys to text box manually in FindDialog
	event.Skip();
}

void VirtualFindList::OnSize(wxSizeEvent& event) {
	UpdateScroll();
	Refresh();
	event.Skip();
}
