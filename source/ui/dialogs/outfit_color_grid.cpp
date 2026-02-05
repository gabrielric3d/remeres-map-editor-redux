#include "ui/dialogs/outfit_color_grid.h"
#include <glad/glad.h>
#include <nanovg.h>
#include <nanovg_gl.h>
#include <wx/event.h>

OutfitColorGrid::OutfitColorGrid(wxWindow* parent, wxWindowID id) :
	NanoVGCanvas(parent, id, wxBORDER_NONE),
	selected_index(0),
	hover_index(-1),
	item_size(14), // Slightly smaller to fit 19 cols in similar space, or keep 16? Original was 16.
	padding(2),
	columns(19),
	rows(7) {

	item_size = 16;
	padding = 2; // 1px gap in original, let's do 2 for NanoVG AA comfort

	Bind(wxEVT_LEFT_DOWN, &OutfitColorGrid::OnMouseDown, this);
	Bind(wxEVT_MOTION, &OutfitColorGrid::OnMotion, this);
	Bind(wxEVT_LEAVE_WINDOW, &OutfitColorGrid::OnLeave, this);
}

OutfitColorGrid::~OutfitColorGrid() {
}

void OutfitColorGrid::SetSelectedColor(int index) {
	if (index >= 0 && index < (int)TemplateOutfitLookupTableSize) {
		selected_index = index;
		Refresh();
	}
}

wxSize OutfitColorGrid::DoGetBestClientSize() const {
	int w = columns * item_size + (columns - 1) * padding + padding * 2;
	int h = rows * item_size + (rows - 1) * padding + padding * 2;
	return FromDIP(wxSize(w, h));
}

void OutfitColorGrid::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
	int startX = padding;
	int startY = padding;

	for (size_t i = 0; i < TemplateOutfitLookupTableSize; ++i) {
		int col = i % columns;
		int row = i / columns;

		float x = startX + col * (item_size + padding);
		float y = startY + row * (item_size + padding);

		uint32_t color = TemplateOutfitLookupTable[i];
		int r = (color >> 16) & 0xFF;
		int g = (color >> 8) & 0xFF;
		int b = color & 0xFF;

		// Hover effect
		if ((int)i == hover_index) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x - 1, y - 1, item_size + 2, item_size + 2, 3.0f);
			nvgFillColor(vg, nvgRGBA(255, 255, 255, 100));
			nvgFill(vg);
		}

		// Main color swatch
		nvgBeginPath(vg);
		nvgRoundedRect(vg, x, y, item_size, item_size, 2.0f);
		nvgFillColor(vg, nvgRGBA(r, g, b, 255));
		nvgFill(vg);

		// Selection highlight
		if ((int)i == selected_index) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x - 1.5f, y - 1.5f, item_size + 3.0f, item_size + 3.0f, 3.0f);
			nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
			nvgStrokeWidth(vg, 2.0f);
			nvgStroke(vg);

			// Inner stroke for contrast on white bg
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x + 0.5f, y + 0.5f, item_size - 1.0f, item_size - 1.0f, 2.0f);
			nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 100));
			nvgStrokeWidth(vg, 1.0f);
			nvgStroke(vg);
		} else {
			// Subtle border
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x + 0.5f, y + 0.5f, item_size - 1.0f, item_size - 1.0f, 2.0f);
			nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 40));
			nvgStrokeWidth(vg, 1.0f);
			nvgStroke(vg);
		}
	}
}

wxRect OutfitColorGrid::GetItemRect(int index) const {
	int col = index % columns;
	int row = index / columns;
	int startX = padding;
	int startY = padding;

	return wxRect(
		startX + col * (item_size + padding),
		startY + row * (item_size + padding),
		item_size,
		item_size
	);
}

int OutfitColorGrid::HitTest(int x, int y) const {
	// Simple grid hit test
	int startX = padding;
	int startY = padding;

	if (x < startX || y < startY) return -1;

	int col = (x - startX) / (item_size + padding);
	int row = (y - startY) / (item_size + padding);

	if (col >= 0 && col < columns && row >= 0 && row < rows) {
		// Check inside item rect (padding exclusion)
		int itemX = startX + col * (item_size + padding);
		int itemY = startY + row * (item_size + padding);
		if (x >= itemX && x < itemX + item_size &&
			y >= itemY && y < itemY + item_size) {

			int index = row * columns + col;
			if (index < (int)TemplateOutfitLookupTableSize) {
				return index;
			}
		}
	}
	return -1;
}

void OutfitColorGrid::OnMouseDown(wxMouseEvent& event) {
	int index = HitTest(event.GetX(), event.GetY());
	if (index != -1 && index != selected_index) {
		selected_index = index;
		Refresh();

		wxCommandEvent evt(wxEVT_BUTTON, GetId());
		evt.SetEventObject(this);
		evt.SetInt(selected_index); // Pass index in event int data
		ProcessEvent(evt);
	}
}

void OutfitColorGrid::OnMotion(wxMouseEvent& event) {
	int index = HitTest(event.GetX(), event.GetY());
	if (index != hover_index) {
		hover_index = index;
		Refresh();
	}
	event.Skip();
}

void OutfitColorGrid::OnLeave(wxMouseEvent& event) {
	if (hover_index != -1) {
		hover_index = -1;
		Refresh();
	}
	event.Skip();
}
