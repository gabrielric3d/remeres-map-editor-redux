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
#include "rendering/ui/carpet_piece_picker.h"

#include "brushes/carpet/carpet_brush.h"
#include "ui/dialogs/carpet_editor_panel.h" // carpetAlignToString
#include "ui/gui.h"
#include "util/common.h"
#include "util/nvg_utils.h"

#include <nanovg.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

	// Where each alignment sits in the 5x5 block. The 3x3 in the middle is a whole patch
	// (sides and corners around the center); the inner diagonals go on the outer corners.
	struct Slot {
		BorderType align;
		int col;
		int row;
	};

	constexpr Slot SLOTS[] = {
		{ NORTHWEST_DIAGONAL, 0, 0 },
		{ NORTHEAST_DIAGONAL, 4, 0 },
		{ SOUTHWEST_DIAGONAL, 0, 4 },
		{ SOUTHEAST_DIAGONAL, 4, 4 },
		{ NORTHWEST_CORNER, 1, 1 },
		{ NORTH_HORIZONTAL, 2, 1 },
		{ NORTHEAST_CORNER, 3, 1 },
		{ WEST_HORIZONTAL, 1, 2 },
		{ EAST_HORIZONTAL, 3, 2 },
		{ SOUTHWEST_CORNER, 1, 3 },
		{ SOUTH_HORIZONTAL, 2, 3 },
		{ SOUTHEAST_CORNER, 3, 3 },
	};

	constexpr int GRID = 5;
	constexpr int CENTER = 2;
	constexpr float MIN_CELL = 36.0f;
	constexpr float MAX_CELL = 56.0f;
	constexpr float PANEL_PAD = 6.0f;
	constexpr float CANVAS_MARGIN = 10.0f;
	constexpr float LABEL_HEIGHT = 24.0f;
	constexpr float LABEL_GAP = 6.0f;
	constexpr int DRAG_THRESHOLD = 6;

	// Look sprites (client ids) share the cache with items (server ids).
	constexpr uint64_t LOOK_KEY = 1ull << 32;

	// Pixel art scaled to the cell: nearest, so it stays sharp.
	int CreateSpriteImage(NVGcontext* vg, GameSprite* sprite) {
		if (!sprite) {
			return 0;
		}
		int width = 0;
		int height = 0;
		auto rgba = NvgUtils::CreateCompositeRGBA(*sprite, width, height);
		if (!rgba) {
			return 0;
		}
		return nvgCreateImageRGBA(vg, width, height, NVG_IMAGE_NEAREST, rgba.get());
	}

	void DrawImage(NVGcontext* vg, int image, float x, float y, float size, float alpha = 1.0f) {
		int iw = 0;
		int ih = 0;
		nvgImageSize(vg, image, &iw, &ih);
		if (iw <= 0 || ih <= 0) {
			return;
		}
		const float scale = size / static_cast<float>(std::max(iw, ih));
		const float dw = iw * scale;
		const float dh = ih * scale;
		const float dx = x + (size - dw) / 2.0f;
		const float dy = y + (size - dh) / 2.0f;
		nvgBeginPath(vg);
		nvgRect(vg, dx, dy, dw, dh);
		nvgFillPaint(vg, nvgImagePattern(vg, dx, dy, dw, dh, 0.0f, image, alpha));
		nvgFill(vg);
	}

} // namespace

bool CarpetPiecePicker::Open(CarpetBrush* carpet, const CarpetPieces::NeighbourEdges& neighbours, int mouse_x, int mouse_y, int canvas_width, int canvas_height, float tile_pixels) {
	if (!carpet) {
		return false;
	}

	m_carpet = carpet;
	m_cells.clear();

	Cell center;
	center.col = CENTER;
	center.row = CENTER;
	center.source = true;
	center.empty = false;
	m_cells.push_back(center);

	const std::vector<CarpetPieces::Piece> pieces = CarpetPieces::ListPieces(*carpet);
	for (const Slot& slot : SLOTS) {
		Cell cell;
		cell.col = slot.col;
		cell.row = slot.row;
		cell.piece.align = slot.align;
		// The first alternate that continues the neighbours; if none does, the first one.
		for (const CarpetPieces::Piece& piece : pieces) {
			if (piece.align != slot.align) {
				continue;
			}
			const bool fits = CarpetPieces::FitsNeighbours(piece.itemId, neighbours);
			if (cell.empty || (fits && !cell.fits)) {
				cell.piece = piece;
				cell.fits = fits;
				cell.empty = false;
			}
			if (cell.fits) {
				break;
			}
		}
		m_cells.push_back(cell);
	}

	const auto current = CarpetPieces::CurrentPiece(*carpet);
	m_current_align = current ? current->align : BORDER_NONE;
	m_source_in_hand = g_gui.GetCurrentBrush() == carpet;

	m_cell = std::clamp(tile_pixels, MIN_CELL, MAX_CELL);
	m_gap = std::max(2.0f, std::round(m_cell * 0.07f));

	m_open = true;
	m_held = true;
	m_moved = false;
	m_open_x = mouse_x;
	m_open_y = mouse_y;
	m_images_stale = true;

	Layout(mouse_x, mouse_y, canvas_width, canvas_height);
	m_hovered = HitTest(mouse_x, mouse_y);
	return true;
}

void CarpetPiecePicker::Close() {
	m_open = false;
	m_held = false;
	m_moved = false;
	m_hovered = -1;
	m_carpet = nullptr;
	m_cells.clear();
}

bool CarpetPiecePicker::CheckStillValid() {
	if (!m_open) {
		return false;
	}
	// ResolveSource only reads the brush in hand (reset to nullptr on a reload), never m_carpet.
	if (!m_carpet || CarpetPieces::ResolveSource(g_gui.GetCurrentBrush()) != m_carpet) {
		Close();
		return false;
	}
	return true;
}

float CarpetPiecePicker::BlockSize() const {
	return GRID * m_cell + (GRID - 1) * m_gap;
}

void CarpetPiecePicker::Layout(int mouse_x, int mouse_y, int canvas_width, int canvas_height) {
	// The center cell goes under the cursor, then the whole block (and the label under
	// it) is pushed back inside the canvas.
	const float block = BlockSize();
	const float to_center = CENTER * (m_cell + m_gap) + m_cell / 2.0f;
	const float min_x = CANVAS_MARGIN + PANEL_PAD;
	const float min_y = CANVAS_MARGIN + PANEL_PAD;
	const float max_x = std::max(min_x, canvas_width - CANVAS_MARGIN - PANEL_PAD - block);
	const float max_y = std::max(min_y, canvas_height - CANVAS_MARGIN - PANEL_PAD - block - LABEL_GAP - LABEL_HEIGHT);
	m_origin_x = std::clamp(mouse_x - to_center, min_x, max_x);
	m_origin_y = std::clamp(mouse_y - to_center, min_y, max_y);
}

float CarpetPiecePicker::CellX(const Cell& cell) const {
	return m_origin_x + cell.col * (m_cell + m_gap);
}

float CarpetPiecePicker::CellY(const Cell& cell) const {
	return m_origin_y + cell.row * (m_cell + m_gap);
}

int CarpetPiecePicker::HitTest(int x, int y) const {
	for (size_t i = 0; i < m_cells.size(); ++i) {
		const Cell& cell = m_cells[i];
		if (cell.empty) {
			continue;
		}
		const float cx = CellX(cell);
		const float cy = CellY(cell);
		if (x >= cx && x < cx + m_cell && y >= cy && y < cy + m_cell) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

void CarpetPiecePicker::UpdateMouse(int mouse_x, int mouse_y) {
	if (!CheckStillValid()) {
		return;
	}
	if (!m_moved && (std::abs(mouse_x - m_open_x) > DRAG_THRESHOLD || std::abs(mouse_y - m_open_y) > DRAG_THRESHOLD)) {
		m_moved = true;
	}
	m_hovered = HitTest(mouse_x, mouse_y);
}

CarpetPiecePicker::PickResult CarpetPiecePicker::Confirm() {
	if (!CheckStillValid() || m_hovered < 0 || m_hovered >= static_cast<int>(m_cells.size())) {
		return PickResult::None;
	}
	const Cell cell = m_cells[m_hovered];
	CarpetBrush* carpet = m_carpet;
	// Close first: selecting a brush refreshes views and palettes.
	Close();
	if (cell.source) {
		CarpetPieces::SelectSource(carpet);
		return PickResult::Source;
	}
	// The item may have no RAW brush: then nothing new is in hand and nothing gets placed.
	return CarpetPieces::SelectPiece(*carpet, cell.piece) ? PickResult::Piece : PickResult::None;
}

wxString CarpetPiecePicker::HoveredDescription() const {
	if (!m_carpet) {
		return wxString();
	}
	if (m_hovered >= 0 && m_hovered < static_cast<int>(m_cells.size())) {
		const Cell& cell = m_cells[m_hovered];
		if (cell.source) {
			return CarpetPieces::DescribeSource(*m_carpet);
		}
		wxString text = CarpetPieces::Describe(*m_carpet, cell.piece);
		if (!cell.fits) {
			text += " - does not continue the pieces around";
		}
		return text;
	}
	return wxstr(m_carpet->getName()) + " - pick a piece";
}

int CarpetPiecePicker::GetItemImage(NVGcontext* vg, uint16_t itemId) {
	const auto found = m_images.find(itemId);
	if (found != m_images.end()) {
		return found->second;
	}
	const auto definition = g_item_definitions.get(itemId);
	GameSprite* sprite = definition ? dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(definition.clientId())) : nullptr;
	const int image = CreateSpriteImage(vg, sprite);
	// Failures are not cached: the sprite sheet may still be loading.
	if (image > 0) {
		m_images[itemId] = image;
	}
	return image;
}

int CarpetPiecePicker::GetLookImage(NVGcontext* vg, uint16_t clientId) {
	const uint64_t key = LOOK_KEY | clientId;
	const auto found = m_images.find(key);
	if (found != m_images.end()) {
		return found->second;
	}
	GameSprite* sprite = dynamic_cast<GameSprite*>(g_gui.gfx.getSprite(clientId));
	const int image = CreateSpriteImage(vg, sprite);
	if (image > 0) {
		m_images[key] = image;
	}
	return image;
}

void CarpetPiecePicker::ReleaseImages(NVGcontext* vg) {
	// Images of a previous context went away with it; only the live one is cleaned.
	if (m_images_context == vg) {
		for (const auto& [key, image] : m_images) {
			if (image > 0) {
				nvgDeleteImage(vg, image);
			}
		}
	}
	m_images.clear();
	m_images_context = vg;
	m_images_stale = false;
}

void CarpetPiecePicker::Draw(NVGcontext* vg, int canvas_width, int canvas_height) {
	if (!vg || !CheckStillValid()) {
		return;
	}
	if (m_images_stale || vg != m_images_context) {
		ReleaseImages(vg);
	}

	nvgSave(vg);

	const float block = BlockSize();

	// Panel behind the whole block.
	nvgBeginPath(vg);
	nvgRoundedRect(vg, m_origin_x - PANEL_PAD, m_origin_y - PANEL_PAD, block + PANEL_PAD * 2.0f, block + PANEL_PAD * 2.0f, 8.0f);
	nvgFillColor(vg, nvgRGBA(16, 16, 16, 205));
	nvgFill(vg);
	nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 45));
	nvgStrokeWidth(vg, 1.0f);
	nvgStroke(vg);

	nvgFontFace(vg, "sans");

	for (size_t i = 0; i < m_cells.size(); ++i) {
		const Cell& cell = m_cells[i];
		const float x = CellX(cell);
		const float y = CellY(cell);

		if (cell.empty) {
			// This carpet has no piece for the alignment: a faint slot with its code.
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x + 0.5f, y + 0.5f, m_cell - 1.0f, m_cell - 1.0f, 4.0f);
			nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 40));
			nvgStrokeWidth(vg, 1.0f);
			nvgStroke(vg);

			nvgFontSize(vg, 10.0f);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, nvgRGBA(255, 255, 255, 70));
			nvgText(vg, x + m_cell / 2.0f, y + m_cell / 2.0f, carpetAlignToString(cell.piece.align), nullptr);
			continue;
		}

		const bool hovered = static_cast<int>(i) == m_hovered;
		const bool in_hand = cell.source ? m_source_in_hand : (m_current_align != BORDER_NONE && cell.piece.align == m_current_align);

		nvgBeginPath(vg);
		nvgRoundedRect(vg, x, y, m_cell, m_cell, 4.0f);
		if (hovered) {
			nvgFillColor(vg, nvgRGBA(70, 70, 70, 245));
		} else {
			nvgFillColor(vg, cell.fits ? nvgRGBA(42, 42, 42, 240) : nvgRGBA(26, 26, 26, 220));
		}
		nvgFill(vg);

		// A piece that would not continue the pieces around the tile: dimmed, still pickable.
		const float image_alpha = cell.fits ? 1.0f : (hovered ? 0.6f : 0.25f);
		const int image = cell.source ? GetLookImage(vg, static_cast<uint16_t>(m_carpet->getLookID())) : GetItemImage(vg, cell.piece.itemId);
		if (image > 0) {
			const float inset = 2.0f;
			DrawImage(vg, image, x + inset, y + inset, m_cell - inset * 2.0f, image_alpha);
		}

		if (cell.source) {
			// "AUTO" tag: this cell is the whole brush, not a piece.
			nvgFontSize(vg, 9.0f);
			float bounds[4];
			nvgTextBounds(vg, 0, 0, "AUTO", nullptr, bounds);
			const float tw = bounds[2] - bounds[0] + 6.0f;
			const float th = 11.0f;
			const float tx = x + (m_cell - tw) / 2.0f;
			const float ty = y + m_cell - th - 2.0f;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, tx, ty, tw, th, 3.0f);
			nvgFillColor(vg, nvgRGBA(0, 0, 0, 185));
			nvgFill(vg);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(vg, nvgRGBA(255, 255, 255, 230));
			nvgText(vg, tx + tw / 2.0f, ty + th / 2.0f, "AUTO", nullptr);
		} else if (cell.piece.alternateCount > 1) {
			// More items share the alignment; the palette strip lists all of them.
			const wxString badge = wxString::Format("+%d", cell.piece.alternateCount - 1);
			nvgFontSize(vg, 10.0f);
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			nvgFillColor(vg, nvgRGBA(0, 0, 0, 200));
			nvgText(vg, x + m_cell - 2.0f, y + 2.0f, badge.utf8_str().data(), nullptr);
			nvgFillColor(vg, nvgRGBA(255, 255, 255, 230));
			nvgText(vg, x + m_cell - 3.0f, y + 1.0f, badge.utf8_str().data(), nullptr);
		}

		if (in_hand) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x + 1.0f, y + 1.0f, m_cell - 2.0f, m_cell - 2.0f, 4.0f);
			nvgStrokeColor(vg, nvgRGBA(255, 196, 0, 255));
			nvgStrokeWidth(vg, 2.0f);
			nvgStroke(vg);
		}
		if (hovered) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x - 1.0f, y - 1.0f, m_cell + 2.0f, m_cell + 2.0f, 5.0f);
			nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
			nvgStrokeWidth(vg, 2.0f);
			nvgStroke(vg);
		}
	}

	// What the hovered cell is, under the block.
	const wxString description = HoveredDescription();
	if (!description.IsEmpty()) {
		const wxScopedCharBuffer text = description.utf8_str();
		nvgFontSize(vg, 13.0f);
		float bounds[4];
		nvgTextBounds(vg, 0, 0, text.data(), nullptr, bounds);
		const float lw = bounds[2] - bounds[0] + 18.0f;
		const float lx = std::clamp(m_origin_x + block / 2.0f - lw / 2.0f, CANVAS_MARGIN, std::max(CANVAS_MARGIN, canvas_width - CANVAS_MARGIN - lw));
		const float ly = m_origin_y + block + PANEL_PAD + LABEL_GAP;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, lx, ly, lw, LABEL_HEIGHT, 5.0f);
		nvgFillColor(vg, nvgRGBA(16, 16, 16, 220));
		nvgFill(vg);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, nvgRGBA(240, 240, 240, 240));
		nvgText(vg, lx + lw / 2.0f, ly + LABEL_HEIGHT / 2.0f, text.data(), nullptr);
	}

	nvgRestore(vg);
}
