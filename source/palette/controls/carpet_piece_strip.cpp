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
#include "palette/controls/carpet_piece_strip.h"

#include "app/settings.h"
#include "brushes/carpet/carpet_brush.h"
#include "palette/palette_window.h"
#include "ui/dialogs/carpet_editor_panel.h" // carpetAlignToString
#include "ui/gui.h"
#include "ui/theme.h"
#include "util/nvg_utils.h"

#include <wx/aui/aui.h>
#include <wx/settings.h>

#include <nanovg.h>

#include <algorithm>

CarpetPieceStrip* CarpetPieceStrip::s_pane = nullptr;

namespace {

	const char* const PANE_NAME = "BorderPieceStrip";
	constexpr int PANE_MIN_HEIGHT = 80;
	constexpr int PANE_BEST_HEIGHT = 400;

} // namespace

CarpetPieceStrip::CarpetPieceStrip(wxWindow* parent) :
	VirtualItemGrid(parent, wxID_ANY) {
	m_padding = 3;
	ApplySlotSize();

	const wxColour bg = Theme::Get(Theme::Role::Background);
	m_bgRed = bg.Red() / 255.0f;
	m_bgGreen = bg.Green() / 255.0f;
	m_bgBlue = bg.Blue() / 255.0f;

	SyncWithCurrentBrush();
}

CarpetPieceStrip::~CarpetPieceStrip() {
	if (s_pane == this) {
		s_pane = nullptr;
	}
}

int CarpetPieceStrip::PaneWidth() const {
	// One slot plus padding, scrollbar included: the width never changes with the list,
	// so the map beside it does not jump when the brush in hand stops being a carpet.
	return m_itemSize + m_padding * 2 + wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, this);
}

void CarpetPieceStrip::ApplySlotSize() {
	// Same slot size as the brush grid of the palette.
	m_itemSize = std::clamp(g_settings.getInteger(Config::PALETTE_GRID_ICON_SIZE), 32, 64);
	SetMinSize(wxSize(PaneWidth(), PANE_MIN_HEIGHT));
}

wxSize CarpetPieceStrip::DoGetBestClientSize() const {
	return wxSize(m_itemSize + m_padding * 2, PANE_BEST_HEIGHT);
}

size_t CarpetPieceStrip::GetItemCount() const {
	return m_carpet ? m_pieces.size() + 1 : 0;
}

uint16_t CarpetPieceStrip::GetItem(size_t index) const {
	if (!m_carpet) {
		return 0;
	}
	if (index == 0) {
		return m_pieces.empty() ? 0 : m_pieces.front().itemId;
	}
	return index - 1 < m_pieces.size() ? m_pieces[index - 1].itemId : 0;
}

wxString CarpetPieceStrip::GetItemName(size_t index) const {
	if (!m_carpet || IsStale()) {
		return wxString();
	}
	if (index == 0) {
		return CarpetPieces::DescribeSource(*m_carpet) + "\nClick to paint with the automatic brush";
	}
	if (index - 1 < m_pieces.size()) {
		return CarpetPieces::Describe(*m_carpet, m_pieces[index - 1]) + "\nClick to put this piece in hand (RAW)";
	}
	return wxString();
}

int CarpetPieceStrip::GroupOf(int index) const {
	if (index <= 0 || index - 1 >= static_cast<int>(m_pieces.size())) {
		return 0;
	}
	const BorderType align = m_pieces[index - 1].align;
	if (align >= NORTH_HORIZONTAL && align <= WEST_HORIZONTAL) {
		return 1;
	}
	if (align >= NORTHWEST_CORNER && align <= SOUTHEAST_CORNER) {
		return 2;
	}
	if (align >= NORTHWEST_DIAGONAL && align <= SOUTHWEST_DIAGONAL) {
		return 3;
	}
	return 4;
}

bool CarpetPieceStrip::IsStale() const {
	// ResolveSource only reads the brush in hand, never m_carpet.
	return m_carpet && CarpetPieces::ResolveSource(g_gui.GetCurrentBrush()) != m_carpet;
}

bool CarpetPieceStrip::DropIfStale() {
	if (!IsStale()) {
		return false;
	}
	m_carpet = nullptr;
	m_pieces.clear();
	m_hoverIndex = -1;
	m_selectedIndex = -1;
	CallAfter([this]() { SyncWithCurrentBrush(); });
	return true;
}

void CarpetPieceStrip::SyncWithCurrentBrush() {
	Brush* current = g_gui.GetCurrentBrush();
	CarpetBrush* carpet = CarpetPieces::ResolveSource(current);
	if (carpet != m_carpet) {
		m_carpet = carpet;
		m_pieces = carpet ? CarpetPieces::ListPieces(*carpet) : std::vector<CarpetPieces::Piece> {};
		m_hoverIndex = -1;
		m_selectedIndex = -1;
		SetScrollPosition(0);
		RefreshGrid();
	}

	int selected = -1;
	if (m_carpet) {
		if (current == m_carpet) {
			selected = 0;
		} else if (const auto piece = CarpetPieces::CurrentPiece(*m_carpet)) {
			for (size_t i = 0; i < m_pieces.size(); ++i) {
				if (m_pieces[i].itemId == piece->itemId) {
					selected = static_cast<int>(i) + 1;
					break;
				}
			}
		}
	}
	// Set directly: SetSelection() would call OnItemSelected and pick the brush again.
	if (selected != m_selectedIndex) {
		m_selectedIndex = selected;
		EnsureVisible(selected);
		Refresh();
	}
}

void CarpetPieceStrip::Reload() {
	m_carpet = nullptr;
	m_pieces.clear();
	m_hoverIndex = -1;
	m_selectedIndex = -1;
	ClearCache();
	ApplySlotSize();
	RefreshGrid();
	// The brush pointers are rebuilt by the reload that called us; read the brush in
	// hand (and resize the pane to a new slot size) once it is over.
	CallAfter([this]() {
		if (wxAuiManager* aui = g_gui.aui_manager) {
			wxAuiPaneInfo& pane = aui->GetPane(this);
			const int width = PaneWidth();
			if (pane.IsOk() && pane.min_size.x != width) {
				pane.MinSize(width, PANE_MIN_HEIGHT).BestSize(width, PANE_BEST_HEIGHT);
				aui->Update();
			}
		}
		SyncWithCurrentBrush();
	});
}

void CarpetPieceStrip::DockPane() {
	wxAuiManager* aui = g_gui.aui_manager;
	if (!aui || !g_gui.root) {
		return;
	}
	if (!s_pane) {
		s_pane = newd CarpetPieceStrip(g_gui.root);
	}

	// Map side of the palette: same side and layer, the next row inwards (on both
	// sides AUI counts rows from the frame edge towards the map).
	int direction = wxAUI_DOCK_LEFT;
	int layer = 0;
	int row = 0;
	PaletteWindow* palette = g_gui.GetPalette();
	if (palette) {
		const wxAuiPaneInfo& palette_pane = aui->GetPane(palette);
		if (palette_pane.IsOk() && palette_pane.IsDocked() && (palette_pane.dock_direction == wxAUI_DOCK_LEFT || palette_pane.dock_direction == wxAUI_DOCK_RIGHT)) {
			direction = palette_pane.dock_direction;
			layer = palette_pane.dock_layer;
			row = palette_pane.dock_row + 1;
		}
	}

	const int width = s_pane->PaneWidth();
	wxAuiPaneInfo info;
	info.Name(PANE_NAME)
		.Caption("Pieces")
		.CaptionVisible(false)
		.Direction(direction)
		.Layer(layer)
		.Row(row)
		.Position(0)
		.Floatable(false)
		.CloseButton(false)
		.MaximizeButton(false)
		.Fixed()
		.MinSize(width, PANE_MIN_HEIGHT)
		.BestSize(width, PANE_BEST_HEIGHT);
	// A new row there: whatever already sat on the map side of the palette moves one
	// row further in instead of sharing the strip's row. Also moves an existing pane.
	aui->InsertPane(s_pane, info, wxAUI_INSERT_ROW);
	// No client loaded means no palette: no lone column at the window edge then.
	aui->GetPane(s_pane).Show(palette && g_settings.getBoolean(Config::PALETTE_PIECE_STRIP));
	// The caller (LayoutManager::LoadPerspective) runs aui->Update().
}

void CarpetPieceStrip::SetPaneVisible(bool visible) {
	wxAuiManager* aui = g_gui.aui_manager;
	if (!s_pane || !aui) {
		return;
	}
	wxAuiPaneInfo& pane = aui->GetPane(s_pane);
	if (!pane.IsOk()) {
		return;
	}
	pane.Show(visible);
	aui->Update();
	if (visible) {
		s_pane->SyncWithCurrentBrush();
	}
}

void CarpetPieceStrip::SyncPane() {
	if (s_pane) {
		s_pane->SyncWithCurrentBrush();
	}
}

void CarpetPieceStrip::ReloadPane() {
	if (s_pane) {
		s_pane->Reload();
	}
}

void CarpetPieceStrip::OnMouseDown(wxMouseEvent& event) {
	const int index = HitTest(event.GetX(), event.GetY());
	if (index >= 0 && m_carpet && !DropIfStale()) {
		CarpetBrush* carpet = m_carpet;
		if (index == 0) {
			CarpetPieces::SelectSource(carpet);
		} else if (index - 1 < static_cast<int>(m_pieces.size())) {
			// Copied: picking re-syncs this strip, which may rebuild the list.
			const CarpetPieces::Piece piece = m_pieces[index - 1];
			CarpetPieces::SelectPiece(*carpet, piece);
		}
	}
	event.Skip();
}

void CarpetPieceStrip::OnNanoVGPaint(NVGcontext* vg, int width, int height) {
	DropIfStale();

	const int scrollPos = GetScrollPosition();
	const size_t count = GetItemCount();
	if (count == 0) {
		// Not a carpet in hand: a dash keeps the column readable as "nothing here".
		nvgFontSize(vg, 14.0f);
		nvgFontFace(vg, "sans");
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
		nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle)));
		nvgText(vg, width / 2.0f, scrollPos + 10.0f, "-", nullptr);
		return;
	}

	const int rowHeight = m_itemSize + m_padding;
	const int startRow = scrollPos / rowHeight;
	const int endRow = (scrollPos + height + rowHeight - 1) / rowHeight;
	const int startIdx = startRow * m_columns;
	const int endIdx = std::min(static_cast<int>(count), (endRow + 1) * m_columns);

	nvgFontFace(vg, "sans");

	for (int i = std::max(0, startIdx); i < endIdx; ++i) {
		const wxRect r = GetItemRect(i);
		const float x = static_cast<float>(r.x);
		const float y = static_cast<float>(r.y);
		const float w = static_cast<float>(r.width);
		const float h = static_cast<float>(r.height);

		const bool isSelected = (i == m_selectedIndex);
		const bool isHovered = (i == m_hoverIndex);

		// Thin line in the gap where a new group starts (auto | sides | corners | diagonals | center).
		if (i > 0 && GroupOf(i) != GroupOf(i - 1)) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, x + 2.0f, y - m_padding / 2.0f);
			nvgLineTo(vg, x + w - 2.0f, y - m_padding / 2.0f);
			nvgStrokeColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::TextSubtle)));
			nvgStrokeWidth(vg, 1.0f);
			nvgStroke(vg);
		}

		nvgBeginPath(vg);
		nvgRoundedRect(vg, x, y, w, h, 3.0f);
		if (isSelected) {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::Selected)));
		} else if (isHovered) {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBaseHover)));
		} else {
			nvgFillColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::CardBase)));
		}
		nvgFill(vg);

		int tex = 0;
		if (i == 0) {
			tex = GetOrCreateSpriteTexture(vg, g_gui.gfx.getSprite(m_carpet->getLookID()));
		} else {
			tex = GetOrCreateItemTexture(vg, m_pieces[i - 1].itemId);
		}
		if (tex > 0) {
			int tw = 0;
			int th = 0;
			nvgImageSize(vg, tex, &tw, &th);
			if (tw > 0 && th > 0) {
				const float avail = w - 4.0f;
				const float scale = std::min(1.0f, avail / static_cast<float>(std::max(tw, th)));
				const float dw = tw * scale;
				const float dh = th * scale;
				const float dx = x + (w - dw) / 2.0f;
				const float dy = y + (h - dh) / 2.0f;
				nvgBeginPath(vg);
				nvgRect(vg, dx, dy, dw, dh);
				nvgFillPaint(vg, nvgImagePattern(vg, dx, dy, dw, dh, 0.0f, tex, 1.0f));
				nvgFill(vg);
			}
		}

		// Direction code in the corner ("n", "cse"), like the carpet editor grid.
		wxString code;
		if (i == 0) {
			code = "AUTO";
		} else {
			const CarpetPieces::Piece& piece = m_pieces[i - 1];
			code = carpetAlignToString(piece.align);
			if (piece.alternateCount > 1) {
				code += wxString::Format("%d", piece.alternate + 1);
			}
		}
		const wxScopedCharBuffer text = code.utf8_str();
		nvgFontSize(vg, 9.0f);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 210));
		nvgText(vg, x + 3.0f, y + 2.0f, text.data(), nullptr);
		nvgFillColor(vg, nvgRGBA(255, 255, 255, 235));
		nvgText(vg, x + 2.0f, y + 1.0f, text.data(), nullptr);

		if (isSelected) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, 3.0f);
			nvgStrokeColor(vg, NvgUtils::ToNvColor(Theme::Get(Theme::Role::Accent)));
			nvgStrokeWidth(vg, 2.0f);
			nvgStroke(vg);
		}
	}
}
