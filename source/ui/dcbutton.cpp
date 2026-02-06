//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "app/settings.h"
#include "ui/dcbutton.h"
#include "game/sprites.h"
#include "ui/gui.h"

#include <glad/glad.h>
#include <nanovg.h>
#include <nanovg_gl.h>

IMPLEMENT_DYNAMIC_CLASS(DCButton, NanoVGCanvas)

DCButton::DCButton() :
	NanoVGCanvas(nullptr, wxID_ANY, wxWANTS_CHARS),
	type(DC_BTN_NORMAL),
	state(false),
	size(RENDER_SIZE_16x16),
	sprite(nullptr),
	overlay(nullptr) {
	SetMinSize(wxSize(36, 36));
	Bind(wxEVT_LEFT_DOWN, &DCButton::OnClick, this);
	SetSprite(0);
}

DCButton::DCButton(wxWindow* parent, wxWindowID id, wxPoint pos, int type, RenderSize sz, int sprite_id) :
	NanoVGCanvas(parent, id, wxWANTS_CHARS),
	type(type),
	state(false),
	size(sz),
	sprite(nullptr),
	overlay(nullptr) {

	if (pos != wxDefaultPosition) {
		SetPosition(pos);
	}

	wxSize winSize;
	if (sz == RENDER_SIZE_64x64) {
		winSize = wxSize(68, 68);
	} else if (sz == RENDER_SIZE_32x32) {
		winSize = wxSize(36, 36);
	} else {
		winSize = wxSize(20, 20);
	}
	SetMinSize(winSize);
	SetSize(winSize);

	Bind(wxEVT_LEFT_DOWN, &DCButton::OnClick, this);
	SetSprite(sprite_id);
}

DCButton::~DCButton() {
	////
}

void DCButton::SetSprite(int _sprid) {
	if (_sprid != 0) {
		sprite = g_gui.gfx.getSprite(_sprid);
	} else {
		sprite = nullptr;
	}
	Refresh();
}

void DCButton::SetSprite(Sprite* _sprite) {
	sprite = _sprite;
	Refresh();
}

void DCButton::SetOverlay(Sprite* espr) {
	overlay = espr;
	Refresh();
}

void DCButton::SetValue(bool val) {
	ASSERT(type == DC_BTN_TOGGLE);
	bool oldval = val;
	state = val;
	if (state == oldval) {
		// Cheap to change value to the old one (which is done ALOT)
		if (GetValue() && g_settings.getInteger(Config::USE_GUI_SELECTION_SHADOW)) {
			SetOverlay(g_gui.gfx.getSprite(EDITOR_SPRITE_SELECTION_MARKER));
		} else {
			SetOverlay(nullptr);
		}
		Refresh();
	}
}

bool DCButton::GetValue() const {
	ASSERT(type == DC_BTN_TOGGLE);
	return state;
}

wxSize DCButton::DoGetBestClientSize() const {
	if (size == RENDER_SIZE_64x64) {
		return FromDIP(wxSize(68, 68));
	} else if (size == RENDER_SIZE_32x32) {
		return FromDIP(wxSize(36, 36));
	} else {
		return FromDIP(wxSize(20, 20));
	}
}

int DCButton::GetSpriteTexture(NVGcontext* vg, Sprite* spr) {
	if (!spr) {
		return 0;
	}

	// Stable ID based on pointer
	uint32_t sprId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(spr) & 0xFFFFFFFF);
	int existingTex = GetCachedImage(sprId);
	if (existingTex > 0) {
		return existingTex;
	}

	GameSprite* gs = dynamic_cast<GameSprite*>(spr);
	if (!gs || gs->spriteList.empty()) {
		// Maybe EditorSprite? For now support GameSprite as primary use case
		return 0;
	}

	int w = gs->width * 32;
	int h = gs->height * 32;
	if (w <= 0 || h <= 0) return 0;

	size_t bufferSize = static_cast<size_t>(w) * h * 4;
	std::vector<uint8_t> composite(bufferSize, 0);

	int px = (gs->pattern_x >= 3) ? 2 : 0;
	for (int l = 0; l < gs->layers; ++l) {
		for (int sw = 0; sw < gs->width; ++sw) {
			for (int sh = 0; sh < gs->height; ++sh) {
				int idx = gs->getIndex(sw, sh, l, px, 0, 0, 0);
				if (idx < 0 || static_cast<size_t>(idx) >= gs->spriteList.size()) continue;

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

	return GetOrCreateImage(sprId, composite.data(), w, h);
}

void DCButton::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
	int size_x = width;
	int size_y = height;

	// Background
	nvgBeginPath(vg);
	nvgRect(vg, 0, 0, size_x, size_y);
	nvgFillColor(vg, nvgRGBA(0, 0, 0, 255)); // Black background
	nvgFill(vg);

	// Colors
	NVGcolor highlight = nvgRGBA(255, 255, 255, 255);
	NVGcolor dark_highlight = nvgRGBA(212, 208, 200, 255); // 0xD4D0C8
	NVGcolor light_shadow = nvgRGBA(128, 128, 128, 255);
	NVGcolor shadow = nvgRGBA(64, 64, 64, 255);

	bool pressed = (type == DC_BTN_TOGGLE && GetValue());

	nvgStrokeWidth(vg, 1.0f);

	if (pressed) {
		// Pressed state (Sunken)
		nvgBeginPath(vg);
		nvgMoveTo(vg, 0.5f, size_y - 0.5f);
		nvgLineTo(vg, 0.5f, 0.5f);
		nvgLineTo(vg, size_x - 0.5f, 0.5f);
		nvgStrokeColor(vg, shadow);
		nvgStroke(vg);

		nvgBeginPath(vg);
		nvgMoveTo(vg, 1.5f, size_y - 1.5f);
		nvgLineTo(vg, 1.5f, 1.5f);
		nvgLineTo(vg, size_x - 1.5f, 1.5f);
		nvgStrokeColor(vg, light_shadow);
		nvgStroke(vg);

		nvgBeginPath(vg);
		nvgMoveTo(vg, size_x - 1.5f, 1.5f);
		nvgLineTo(vg, size_x - 1.5f, size_y - 1.5f);
		nvgLineTo(vg, 1.5f, size_y - 1.5f);
		nvgStrokeColor(vg, dark_highlight);
		nvgStroke(vg);

		nvgBeginPath(vg);
		nvgMoveTo(vg, size_x - 0.5f, 0.5f);
		nvgLineTo(vg, size_x - 0.5f, size_y - 0.5f);
		nvgLineTo(vg, 0.5f, size_y - 0.5f);
		nvgStrokeColor(vg, highlight);
		nvgStroke(vg);

	} else {
		// Raised state
		nvgBeginPath(vg);
		nvgMoveTo(vg, 0.5f, size_y - 0.5f);
		nvgLineTo(vg, 0.5f, 0.5f);
		nvgLineTo(vg, size_x - 0.5f, 0.5f);
		nvgStrokeColor(vg, highlight);
		nvgStroke(vg);

		nvgBeginPath(vg);
		nvgMoveTo(vg, 1.5f, size_y - 1.5f);
		nvgLineTo(vg, 1.5f, 1.5f);
		nvgLineTo(vg, size_x - 1.5f, 1.5f);
		nvgStrokeColor(vg, dark_highlight);
		nvgStroke(vg);

		nvgBeginPath(vg);
		nvgMoveTo(vg, size_x - 1.5f, 1.5f);
		nvgLineTo(vg, size_x - 1.5f, size_y - 1.5f);
		nvgLineTo(vg, 1.5f, size_y - 1.5f);
		nvgStrokeColor(vg, light_shadow);
		nvgStroke(vg);

		nvgBeginPath(vg);
		nvgMoveTo(vg, size_x - 0.5f, 0.5f);
		nvgLineTo(vg, size_x - 0.5f, size_y - 0.5f);
		nvgLineTo(vg, 0.5f, size_y - 0.5f);
		nvgStrokeColor(vg, shadow);
		nvgStroke(vg);
	}

	// Draw Sprite
	if (sprite) {
		int tex = GetSpriteTexture(vg, sprite);
		if (tex > 0) {
			float drawSize = 32.0f;
			if (size == RENDER_SIZE_16x16) drawSize = 16.0f;
			if (size == RENDER_SIZE_64x64) drawSize = 64.0f;

			// Center it
			float dx = (size_x - drawSize) / 2.0f;
			float dy = (size_y - drawSize) / 2.0f;

			NVGpaint imgPaint = nvgImagePattern(vg, dx, dy, drawSize, drawSize, 0.0f, tex, 1.0f);
			nvgBeginPath(vg);
			nvgRect(vg, dx, dy, drawSize, drawSize);
			nvgFillPaint(vg, imgPaint);
			nvgFill(vg);

			// Overlay
			if (overlay && pressed) {
				int oTex = GetSpriteTexture(vg, overlay);
				if (oTex > 0) {
					NVGpaint oPaint = nvgImagePattern(vg, dx, dy, drawSize, drawSize, 0.0f, oTex, 1.0f);
					nvgBeginPath(vg);
					nvgRect(vg, dx, dy, drawSize, drawSize);
					nvgFillPaint(vg, oPaint);
					nvgFill(vg);
				}
			}
		}
	}
}

void DCButton::OnClick(wxMouseEvent& WXUNUSED(evt)) {
	wxCommandEvent event(type == DC_BTN_TOGGLE ? wxEVT_COMMAND_TOGGLEBUTTON_CLICKED : wxEVT_COMMAND_BUTTON_CLICKED, GetId());
	event.SetEventObject(this);

	if (type == DC_BTN_TOGGLE) {
		SetValue(!GetValue());
	}
	SetFocus();

	GetEventHandler()->ProcessEvent(event);
}
