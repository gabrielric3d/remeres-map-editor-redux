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
#include "ui/dialogs/instance_layout_dialog.h"

#include "editor/action.h"
#include "editor/editor.h"
#include "editor/action_queue.h"
#include "editor/selection.h"
#include "ui/gui.h"
#include "map/map.h"
#include "map/tile.h"
#include "map/tile_operations.h"
#include "ui/positionctrl.h"
#include "app/settings.h"
#include "ui/theme.h"
#include "rendering/core/game_sprite.h"
#include "game/outfit.h"

#include <wx/dcbuffer.h>
#include <wx/notebook.h>

#include <set>
#include <sstream>

namespace {

// Preview-only looktypes. In this dataset, 107 resolves to demon-like sprite.
constexpr int PLAYER_SPAWN_LOOKTYPE = 127;
constexpr int BOSS_SPAWN_LOOKTYPE = 107;

enum {
	ID_PICK_AREA = wxID_HIGHEST + 7900,
	ID_USE_SELECTION,
	ID_LEFT_COUNT,
	ID_RIGHT_COUNT,
	ID_NORTH_COUNT,
	ID_SOUTH_COUNT,
	ID_SPACING_X,
	ID_SPACING_Y,
	ID_INCLUDE_CENTER_TEXT,
	ID_INCLUDE_CENTER_COPY,
	ID_COPY_TEXT_TO_CLIPBOARD,
	ID_GENERATE_TEXT,
	ID_APPLY_INSTANCES,
};

struct NormalizedArea
{
	Position from;
	Position to;
	int width = 1;
	int height = 1;
	int floors = 1;
};

NormalizedArea NormalizeArea(const Position& first, const Position& second)
{
	NormalizedArea area;
	area.from.x = std::min(first.x, second.x);
	area.from.y = std::min(first.y, second.y);
	area.from.z = std::min(first.z, second.z);

	area.to.x = std::max(first.x, second.x);
	area.to.y = std::max(first.y, second.y);
	area.to.z = std::max(first.z, second.z);

	area.width = area.to.x - area.from.x + 1;
	area.height = area.to.y - area.from.y + 1;
	area.floors = area.to.z - area.from.z + 1;
	return area;
}

Position OffsetPosition(const Position& base, const Position& offset)
{
	return Position(base.x + offset.x, base.y + offset.y, base.z + offset.z);
}

std::string FormatLuaPosition(const Position& pos)
{
	std::ostringstream ss;
	ss << "Position(" << pos.x << ", " << pos.y << ", " << pos.z << ")";
	return ss.str();
}

class InstanceBasePreviewPanel : public wxPanel
{
public:
	InstanceBasePreviewPanel(
		wxWindow* parent,
		PositionCtrl* fromPosCtrl,
		PositionCtrl* toPosCtrl,
		PositionCtrl* playerSpawnPosCtrl,
		PositionCtrl* bossSpawnPosCtrl
	) :
		wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 190)),
		fromPosCtrl(fromPosCtrl),
		toPosCtrl(toPosCtrl),
		playerSpawnPosCtrl(playerSpawnPosCtrl),
		bossSpawnPosCtrl(bossSpawnPosCtrl)
	{
		SetMinSize(wxSize(-1, 190));
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Bind(wxEVT_PAINT, &InstanceBasePreviewPanel::OnPaint, this);
	}

private:
	static int ClampInt(int value, int minValue, int maxValue)
	{
		return std::max(minValue, std::min(maxValue, value));
	}

	static double ClampNormalized(double value)
	{
		if(value < 0.0) {
			return 0.0;
		}
		if(value > 1.0) {
			return 1.0;
		}
		return value;
	}

	static wxString FormatPos(const Position& pos)
	{
		return wxString::Format("(%d, %d, %d)", pos.x, pos.y, pos.z);
	}

	static wxPoint ProjectToArea(const Position& pos, const NormalizedArea& area, const wxRect& areaRect)
	{
		const double px = area.width > 1
			? ClampNormalized(static_cast<double>(pos.x - area.from.x) / static_cast<double>(area.width - 1))
			: 0.5;
		const double py = area.height > 1
			? ClampNormalized(static_cast<double>(pos.y - area.from.y) / static_cast<double>(area.height - 1))
			: 0.5;

		const int pointX = areaRect.GetLeft() + static_cast<int>(px * areaRect.GetWidth());
		const int pointY = areaRect.GetTop() + static_cast<int>(py * areaRect.GetHeight());
		return wxPoint(
			ClampInt(pointX, areaRect.GetLeft(), areaRect.GetRight()),
			ClampInt(pointY, areaRect.GetTop(), areaRect.GetBottom())
		);
	}

	static void DrawCreatureSprite(wxDC& dc, const wxRect& rect, int lookType)
	{
		if(GameSprite* creatureSprite = g_gui.gfx.getCreatureSprite(lookType)) {
			Outfit outfit;
			outfit.lookType = lookType;
			creatureSprite->DrawTo(&dc, SPRITE_SIZE_32x32, outfit, rect.GetX(), rect.GetY(), rect.GetWidth(), rect.GetHeight());
			return;
		}

		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
		dc.DrawRoundedRectangle(rect, 3);
		dc.SetTextForeground(Theme::Get(Theme::Role::Text));
		dc.DrawText("?", rect.GetX() + rect.GetWidth() / 2 - 3, rect.GetY() + rect.GetHeight() / 2 - 8);
	}

	static void DrawSpawnCard(wxDC& dc, const wxRect& rect, const wxString& title, const Position& pos, int lookType, const wxColour& accent)
	{
		dc.SetPen(wxPen(accent, 1));
		dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
		dc.DrawRoundedRectangle(rect, 4);

		const wxRect iconRect(rect.GetX() + 6, rect.GetY() + 6, 28, 28);
		DrawCreatureSprite(dc, iconRect, lookType);

		dc.SetTextForeground(Theme::Get(Theme::Role::Text));
		dc.DrawText(title, rect.GetX() + 42, rect.GetY() + 5);
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		dc.DrawText(FormatPos(pos), rect.GetX() + 42, rect.GetY() + 22);
	}

	void DrawSpawnMarker(
		wxDC& dc,
		const wxRect& areaRect,
		const NormalizedArea& area,
		const Position& pos,
		int lookType,
		const wxString& markerLabel,
		const wxColour& outline
	) const {
		const wxPoint point = ProjectToArea(pos, area, areaRect);
		const int markerSize = 24;
		const int drawX = ClampInt(point.x - markerSize / 2, areaRect.GetLeft(), areaRect.GetRight() - markerSize);
		const int drawY = ClampInt(point.y - markerSize / 2, areaRect.GetTop(), areaRect.GetBottom() - markerSize);
		const wxRect markerRect(drawX, drawY, markerSize, markerSize);

		dc.SetPen(wxPen(outline, 1));
		dc.SetBrush(*wxTRANSPARENT_BRUSH);
		dc.DrawRoundedRectangle(markerRect, 4);
		DrawCreatureSprite(dc, markerRect, lookType);
		dc.SetTextForeground(outline);
		dc.DrawText(markerLabel, markerRect.GetX() - 1, markerRect.GetY() - 16);
	}

	void OnPaint(wxPaintEvent&)
	{
		wxAutoBufferedPaintDC dc(this);
		const wxRect clientRect = GetClientRect();
		dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
		dc.Clear();

		if(!fromPosCtrl || !toPosCtrl || !playerSpawnPosCtrl || !bossSpawnPosCtrl) {
			return;
		}

		const Position fromPos = fromPosCtrl->GetPosition();
		const Position toPos = toPosCtrl->GetPosition();
		const Position playerSpawnPos = playerSpawnPosCtrl->GetPosition();
		const Position bossSpawnPos = bossSpawnPosCtrl->GetPosition();
		const NormalizedArea area = NormalizeArea(fromPos, toPos);

		wxRect panelRect = clientRect;
		panelRect.Deflate(8, 8);

		dc.SetPen(wxPen(Theme::Get(Theme::Role::Border), 1));
		dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
		dc.DrawRoundedRectangle(panelRect, 6);

		dc.SetTextForeground(Theme::Get(Theme::Role::Text));
		dc.DrawText("Preview: fromPos = canto superior esquerdo, toPos = canto inferior direito", panelRect.GetX() + 10, panelRect.GetY() + 8);

		const int legendHeight = 54;
		wxRect areaRect(panelRect.GetX() + 12, panelRect.GetY() + 30, panelRect.GetWidth() - 24, panelRect.GetHeight() - legendHeight - 44);
		areaRect.SetWidth(std::max(80, areaRect.GetWidth()));
		areaRect.SetHeight(std::max(60, areaRect.GetHeight()));

		dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
		dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Background)));
		dc.DrawRectangle(areaRect);

		dc.SetPen(wxPen(Theme::Get(Theme::Role::AccentHover), 1, wxPENSTYLE_DOT));
		dc.DrawLine(areaRect.GetLeft(), areaRect.GetTop(), areaRect.GetRight(), areaRect.GetBottom());

		dc.SetBrush(wxBrush(Theme::Get(Theme::Role::AccentHover)));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawCircle(areaRect.GetTopLeft(), 3);

		dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Warning)));
		dc.DrawCircle(areaRect.GetBottomRight(), 3);

		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		dc.DrawText("fromPos " + FormatPos(area.from), areaRect.GetLeft() + 4, areaRect.GetTop() + 4);
		wxString toLabel = "toPos " + FormatPos(area.to);
		int toLabelWidth = 0;
		int toLabelHeight = 0;
		dc.GetTextExtent(toLabel, &toLabelWidth, &toLabelHeight);
		dc.DrawText(toLabel, areaRect.GetRight() - toLabelWidth - 4, areaRect.GetBottom() - toLabelHeight - 4);

		// Player/Boss spawn markers - domain-specific accent colors remain hardcoded
		DrawSpawnMarker(dc, areaRect, area, playerSpawnPos, PLAYER_SPAWN_LOOKTYPE, "P", wxColour(105, 203, 255));
		DrawSpawnMarker(dc, areaRect, area, bossSpawnPos, BOSS_SPAWN_LOOKTYPE, "B", wxColour(255, 126, 126));

		wxRect legendRect(panelRect.GetX() + 12, areaRect.GetBottom() + 10, panelRect.GetWidth() - 24, legendHeight);
		const int gap = 8;
		const int cardWidth = (legendRect.GetWidth() - gap) / 2;
		wxRect playerCard(legendRect.GetX(), legendRect.GetY(), cardWidth, legendRect.GetHeight());
		wxRect bossCard(playerCard.GetRight() + gap, legendRect.GetY(), cardWidth, legendRect.GetHeight());

		DrawSpawnCard(
			dc,
			playerCard,
			wxString::Format("Player Spawn (lookType %d)", PLAYER_SPAWN_LOOKTYPE),
			playerSpawnPos,
			PLAYER_SPAWN_LOOKTYPE,
			wxColour(98, 179, 242)
		);
		DrawSpawnCard(
			dc,
			bossCard,
			wxString::Format("Boss Spawn (lookType %d)", BOSS_SPAWN_LOOKTYPE),
			bossSpawnPos,
			BOSS_SPAWN_LOOKTYPE,
			wxColour(229, 108, 108)
		);
	}

	PositionCtrl* fromPosCtrl = nullptr;
	PositionCtrl* toPosCtrl = nullptr;
	PositionCtrl* playerSpawnPosCtrl = nullptr;
	PositionCtrl* bossSpawnPosCtrl = nullptr;
};

} // namespace

wxBEGIN_EVENT_TABLE(InstanceLayoutDialog, wxDialog)
	EVT_BUTTON(ID_PICK_AREA, InstanceLayoutDialog::OnPickArea)
	EVT_BUTTON(ID_USE_SELECTION, InstanceLayoutDialog::OnUseSelection)
	EVT_SPINCTRL(ID_LEFT_COUNT, InstanceLayoutDialog::OnLayoutSpinChanged)
	EVT_SPINCTRL(ID_RIGHT_COUNT, InstanceLayoutDialog::OnLayoutSpinChanged)
	EVT_SPINCTRL(ID_NORTH_COUNT, InstanceLayoutDialog::OnLayoutSpinChanged)
	EVT_SPINCTRL(ID_SOUTH_COUNT, InstanceLayoutDialog::OnLayoutSpinChanged)
	EVT_SPINCTRL(ID_SPACING_X, InstanceLayoutDialog::OnLayoutSpinChanged)
	EVT_SPINCTRL(ID_SPACING_Y, InstanceLayoutDialog::OnLayoutSpinChanged)
	EVT_CHECKBOX(ID_INCLUDE_CENTER_TEXT, InstanceLayoutDialog::OnLayoutCheckChanged)
	EVT_CHECKBOX(ID_INCLUDE_CENTER_COPY, InstanceLayoutDialog::OnLayoutCheckChanged)
	EVT_BUTTON(ID_GENERATE_TEXT, InstanceLayoutDialog::OnGenerateText)
	EVT_BUTTON(ID_APPLY_INSTANCES, InstanceLayoutDialog::OnApply)
	EVT_BUTTON(wxID_CLOSE, InstanceLayoutDialog::OnCloseButton)
	EVT_CLOSE(InstanceLayoutDialog::OnClose)
wxEND_EVENT_TABLE()

InstanceLayoutDialog::InstanceLayoutDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Instance Layout Generator", wxDefaultPosition, wxSize(940, 760),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	CreateControls();
	ApplySelectionBounds(false);
	UpdateAreaSummary();
	UpdateLayoutSummary();
	CentreOnParent();
}

void InstanceLayoutDialog::UpdateEditor()
{
	const bool hasEditor = g_gui.IsEditorOpen();
	if(m_applyButton) {
		m_applyButton->Enable(hasEditor);
	}
	if(m_pickStatusText && !hasEditor) {
		m_pickStatusText->SetForegroundColour(Theme::Get(Theme::Role::Warning));
		m_pickStatusText->SetLabel("No map is currently open.");
	}
	UpdateAreaSummary();
	UpdateLayoutSummary();
}

void InstanceLayoutDialog::CreateControls()
{
	SetMinSize(wxSize(760, 620));

	wxBoxSizer* mainSizer = newd wxBoxSizer(wxVERTICAL);
	wxNotebook* notebook = newd wxNotebook(this, wxID_ANY);

	// Step 1: Base positions
	wxPanel* basePanel = newd wxPanel(notebook);
	wxBoxSizer* baseSizer = newd wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer* basePositionsBox = newd wxStaticBoxSizer(wxVERTICAL, basePanel, "Step 1 - Base Positions");

	wxBoxSizer* pickRow = newd wxBoxSizer(wxHORIZONTAL);
	pickRow->Add(newd wxButton(basePanel, ID_PICK_AREA, "Pick Area on Map"), 0, wxRIGHT, 6);
	pickRow->Add(newd wxButton(basePanel, ID_USE_SELECTION, "Use Selection"), 0);
	basePositionsBox->Add(pickRow, 0, wxALL, 5);

	m_centerPosCtrl = newd PositionCtrl(basePanel, "centerPos", 0, 0, 7);
	basePositionsBox->Add(m_centerPosCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	wxBoxSizer* fromToRow = newd wxBoxSizer(wxHORIZONTAL);
	m_fromPosCtrl = newd PositionCtrl(basePanel, "fromPos", 0, 0, 7);
	m_toPosCtrl = newd PositionCtrl(basePanel, "toPos", 0, 0, 7);
	fromToRow->Add(m_fromPosCtrl, 1, wxEXPAND | wxRIGHT, 4);
	fromToRow->Add(m_toPosCtrl, 1, wxEXPAND | wxLEFT, 4);
	basePositionsBox->Add(fromToRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	wxBoxSizer* spawnRow = newd wxBoxSizer(wxHORIZONTAL);
	m_playerSpawnPosCtrl = newd PositionCtrl(basePanel, "playerSpawnPos", 0, 0, 7);
	m_bossSpawnPosCtrl = newd PositionCtrl(basePanel, "bossSpawnPos", 0, 0, 7);
	spawnRow->Add(m_playerSpawnPosCtrl, 1, wxEXPAND | wxRIGHT, 4);
	spawnRow->Add(m_bossSpawnPosCtrl, 1, wxEXPAND | wxLEFT, 4);
	basePositionsBox->Add(spawnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	m_basePreviewPanel = newd InstanceBasePreviewPanel(
		basePanel,
		m_fromPosCtrl,
		m_toPosCtrl,
		m_playerSpawnPosCtrl,
		m_bossSpawnPosCtrl
	);
	basePositionsBox->Add(m_basePreviewPanel, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	m_pickStatusText = newd wxStaticText(basePanel, wxID_ANY, "Tip: Pick Area or Use Selection to fill fromPos/toPos quickly.");
	basePositionsBox->Add(m_pickStatusText, 0, wxLEFT | wxRIGHT | wxTOP, 6);

	m_areaSummaryText = newd wxStaticText(basePanel, wxID_ANY, "Area: not defined");
	basePositionsBox->Add(m_areaSummaryText, 0, wxALL, 6);

	baseSizer->Add(basePositionsBox, 1, wxEXPAND | wxALL, 8);
	basePanel->SetSizer(baseSizer);
	basePanel->Bind(wxEVT_TEXT, &InstanceLayoutDialog::OnBasePositionTextChanged, this);

	// Step 2: Layout
	wxPanel* layoutPanel = newd wxPanel(notebook);
	wxBoxSizer* layoutSizer = newd wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer* distributionBox = newd wxStaticBoxSizer(wxVERTICAL, layoutPanel, "Step 2 - Instance Distribution");

	wxFlexGridSizer* layoutGrid = newd wxFlexGridSizer(2, 6, 8);
	layoutGrid->AddGrowableCol(1);
	layoutGrid->Add(newd wxStaticText(layoutPanel, wxID_ANY, "Right instances"), 0, wxALIGN_CENTER_VERTICAL);
	layoutGrid->Add(m_rightCountSpin = newd wxSpinCtrl(layoutPanel, ID_RIGHT_COUNT, "0", wxDefaultPosition, wxSize(110, -1), wxSP_ARROW_KEYS, 0, 1000, 0), 0, wxALIGN_CENTER_VERTICAL);

	layoutGrid->Add(newd wxStaticText(layoutPanel, wxID_ANY, "Left instances"), 0, wxALIGN_CENTER_VERTICAL);
	layoutGrid->Add(m_leftCountSpin = newd wxSpinCtrl(layoutPanel, ID_LEFT_COUNT, "0", wxDefaultPosition, wxSize(110, -1), wxSP_ARROW_KEYS, 0, 1000, 0), 0, wxALIGN_CENTER_VERTICAL);

	layoutGrid->Add(newd wxStaticText(layoutPanel, wxID_ANY, "North instances"), 0, wxALIGN_CENTER_VERTICAL);
	layoutGrid->Add(m_northCountSpin = newd wxSpinCtrl(layoutPanel, ID_NORTH_COUNT, "0", wxDefaultPosition, wxSize(110, -1), wxSP_ARROW_KEYS, 0, 1000, 0), 0, wxALIGN_CENTER_VERTICAL);

	layoutGrid->Add(newd wxStaticText(layoutPanel, wxID_ANY, "South instances"), 0, wxALIGN_CENTER_VERTICAL);
	layoutGrid->Add(m_southCountSpin = newd wxSpinCtrl(layoutPanel, ID_SOUTH_COUNT, "0", wxDefaultPosition, wxSize(110, -1), wxSP_ARROW_KEYS, 0, 1000, 0), 0, wxALIGN_CENTER_VERTICAL);

	layoutGrid->Add(newd wxStaticText(layoutPanel, wxID_ANY, "Spacing X (tiles)"), 0, wxALIGN_CENTER_VERTICAL);
	layoutGrid->Add(m_spacingXSpin = newd wxSpinCtrl(layoutPanel, ID_SPACING_X, "0", wxDefaultPosition, wxSize(110, -1), wxSP_ARROW_KEYS, 0, 2000, 0), 0, wxALIGN_CENTER_VERTICAL);

	layoutGrid->Add(newd wxStaticText(layoutPanel, wxID_ANY, "Spacing Y (tiles)"), 0, wxALIGN_CENTER_VERTICAL);
	layoutGrid->Add(m_spacingYSpin = newd wxSpinCtrl(layoutPanel, ID_SPACING_Y, "0", wxDefaultPosition, wxSize(110, -1), wxSP_ARROW_KEYS, 0, 2000, 0), 0, wxALIGN_CENTER_VERTICAL);

	distributionBox->Add(layoutGrid, 0, wxALL | wxEXPAND, 6);

	m_includeCenterInTextCheck = newd wxCheckBox(layoutPanel, ID_INCLUDE_CENTER_TEXT, "Include center instance in Lua text");
	m_includeCenterInTextCheck->SetValue(true);
	distributionBox->Add(m_includeCenterInTextCheck, 0, wxLEFT | wxRIGHT | wxTOP, 6);

	m_includeCenterInCopyCheck = newd wxCheckBox(layoutPanel, ID_INCLUDE_CENTER_COPY, "Paste center instance too (can overwrite source)");
	m_includeCenterInCopyCheck->SetValue(false);
	distributionBox->Add(m_includeCenterInCopyCheck, 0, wxLEFT | wxRIGHT | wxTOP, 6);

	m_layoutSummaryText = newd wxStaticText(layoutPanel, wxID_ANY, "Layout summary");
	distributionBox->Add(m_layoutSummaryText, 0, wxALL, 6);

	layoutSizer->Add(distributionBox, 0, wxEXPAND | wxALL, 8);
	layoutPanel->SetSizer(layoutSizer);

	// Step 3: Generate / apply
	wxPanel* outputPanel = newd wxPanel(notebook);
	wxBoxSizer* outputSizer = newd wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* outputOptionsBox = newd wxStaticBoxSizer(wxVERTICAL, outputPanel, "Step 3 - Generate / Apply");
	m_copyTextToClipboardCheck = newd wxCheckBox(outputPanel, ID_COPY_TEXT_TO_CLIPBOARD, "Copy generated text to clipboard");
	m_copyTextToClipboardCheck->SetValue(true);
	outputOptionsBox->Add(m_copyTextToClipboardCheck, 0, wxLEFT | wxRIGHT | wxTOP, 6);

	m_outputTextCtrl = newd wxTextCtrl(outputPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
		wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	m_outputTextCtrl->SetMinSize(wxSize(-1, 360));
	outputOptionsBox->Add(m_outputTextCtrl, 1, wxALL | wxEXPAND, 6);

	outputSizer->Add(outputOptionsBox, 1, wxEXPAND | wxALL, 8);
	outputPanel->SetSizer(outputSizer);

	notebook->AddPage(basePanel, "1) Base", true);
	notebook->AddPage(layoutPanel, "2) Layout");
	notebook->AddPage(outputPanel, "3) Generate");
	mainSizer->Add(notebook, 1, wxEXPAND | wxALL, 8);

	wxBoxSizer* buttonsRow = newd wxBoxSizer(wxHORIZONTAL);
	buttonsRow->Add(newd wxButton(this, ID_GENERATE_TEXT, "Generate Text"), 0, wxRIGHT, 6);
	m_applyButton = newd wxButton(this, ID_APPLY_INSTANCES, "Apply Instances + Generate");
	buttonsRow->Add(m_applyButton, 0, wxRIGHT, 6);
	buttonsRow->Add(newd wxButton(this, wxID_CLOSE, "Close"), 0);
	mainSizer->Add(buttonsRow, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 10);

	SetSizer(mainSizer);
	Layout();
}

void InstanceLayoutDialog::ApplySelectionBounds(bool showMessageWhenEmpty)
{
	Editor* editor = g_gui.GetCurrentEditor();
	if(!editor) {
		return;
	}

	const Selection& selection = editor->selection;
	if(selection.empty()) {
		if(showMessageWhenEmpty) {
			wxMessageBox("No selection found. Select an area first.", "Instance Layout", wxOK | wxICON_INFORMATION, this);
		}
		return;
	}

	Position minPos(INT_MAX, INT_MAX, INT_MAX);
	Position maxPos(INT_MIN, INT_MIN, INT_MIN);
	for(Tile* tile : selection.getTiles()) {
		if(!tile) {
			continue;
		}
		const Position& pos = tile->getPosition();
		minPos.x = std::min(minPos.x, pos.x);
		minPos.y = std::min(minPos.y, pos.y);
		minPos.z = std::min(minPos.z, pos.z);
		maxPos.x = std::max(maxPos.x, pos.x);
		maxPos.y = std::max(maxPos.y, pos.y);
		maxPos.z = std::max(maxPos.z, pos.z);
	}

	NormalizedArea area = NormalizeArea(minPos, maxPos);
	const Position center(
		(area.from.x + area.to.x) / 2,
		(area.from.y + area.to.y) / 2,
		(area.from.z + area.to.z) / 2
	);

	m_fromPosCtrl->SetPosition(area.from);
	m_toPosCtrl->SetPosition(area.to);
	m_centerPosCtrl->SetPosition(center);
	m_playerSpawnPosCtrl->SetPosition(center);
	m_bossSpawnPosCtrl->SetPosition(center);

	if(m_pickStatusText) {
		m_pickStatusText->SetForegroundColour(Theme::Get(Theme::Role::Success));
		m_pickStatusText->SetLabel(wxString::Format(
			"Selection loaded: (%d,%d,%d) -> (%d,%d,%d)",
			area.from.x, area.from.y, area.from.z,
			area.to.x, area.to.y, area.to.z
		));
	}
}

void InstanceLayoutDialog::UpdateAreaSummary()
{
	if(!m_fromPosCtrl || !m_toPosCtrl || !m_areaSummaryText) {
		return;
	}

	const NormalizedArea area = NormalizeArea(m_fromPosCtrl->GetPosition(), m_toPosCtrl->GetPosition());
	const long long totalTiles = static_cast<long long>(area.width) * area.height * area.floors;
	m_areaSummaryText->SetLabel(wxString::Format(
		"Area: (%d,%d,%d) -> (%d,%d,%d) | %dx%d | %d floor(s) | %lld tiles",
		area.from.x, area.from.y, area.from.z,
		area.to.x, area.to.y, area.to.z,
		area.width, area.height, area.floors, totalTiles
	));
	RefreshBasePreview();
}

void InstanceLayoutDialog::UpdateLayoutSummary()
{
	if(!m_layoutSummaryText || !m_fromPosCtrl || !m_toPosCtrl || !m_leftCountSpin || !m_rightCountSpin ||
		!m_northCountSpin || !m_southCountSpin || !m_spacingXSpin || !m_spacingYSpin ||
		!m_includeCenterInTextCheck || !m_includeCenterInCopyCheck) {
		return;
	}

	const NormalizedArea area = NormalizeArea(m_fromPosCtrl->GetPosition(), m_toPosCtrl->GetPosition());
	const int leftCount = std::max(0, m_leftCountSpin->GetValue());
	const int rightCount = std::max(0, m_rightCountSpin->GetValue());
	const int northCount = std::max(0, m_northCountSpin->GetValue());
	const int southCount = std::max(0, m_southCountSpin->GetValue());
	const int spacingX = std::max(0, m_spacingXSpin->GetValue());
	const int spacingY = std::max(0, m_spacingYSpin->GetValue());

	const int stepX = area.width + spacingX;
	const int stepY = area.height + spacingY;
	const int columns = leftCount + rightCount + 1;
	const int rows = northCount + southCount + 1;
	const int totalPlacements = columns * rows;
	const int textPlacements = totalPlacements - (m_includeCenterInTextCheck->GetValue() ? 0 : 1);
	const int copyPlacements = totalPlacements - (m_includeCenterInCopyCheck->GetValue() ? 0 : 1);

	m_layoutSummaryText->SetLabel(wxString::Format(
		"Grid: %d column(s) x %d row(s) | Total: %d\n"
		"Step: X=%d, Y=%d | Lua entries: %d | Paste targets: %d",
		columns, rows, totalPlacements,
		stepX, stepY,
		std::max(0, textPlacements),
		std::max(0, copyPlacements)
	));
}

void InstanceLayoutDialog::RefreshBasePreview()
{
	if(m_basePreviewPanel) {
		m_basePreviewPanel->Refresh();
	}
}

bool InstanceLayoutDialog::BuildConfig(LayoutConfig& config, wxString& errorMessage) const
{
	if(!m_centerPosCtrl || !m_fromPosCtrl || !m_toPosCtrl || !m_playerSpawnPosCtrl || !m_bossSpawnPosCtrl ||
		!m_leftCountSpin || !m_rightCountSpin || !m_northCountSpin || !m_southCountSpin ||
		!m_spacingXSpin || !m_spacingYSpin || !m_includeCenterInTextCheck || !m_includeCenterInCopyCheck) {
		errorMessage = "Dialog controls are not initialized.";
		return false;
	}

	config.centerPos = m_centerPosCtrl->GetPosition();
	config.fromPos = m_fromPosCtrl->GetPosition();
	config.toPos = m_toPosCtrl->GetPosition();
	config.playerSpawnPos = m_playerSpawnPosCtrl->GetPosition();
	config.bossSpawnPos = m_bossSpawnPosCtrl->GetPosition();
	config.leftCount = std::max(0, m_leftCountSpin->GetValue());
	config.rightCount = std::max(0, m_rightCountSpin->GetValue());
	config.northCount = std::max(0, m_northCountSpin->GetValue());
	config.southCount = std::max(0, m_southCountSpin->GetValue());
	config.spacingX = std::max(0, m_spacingXSpin->GetValue());
	config.spacingY = std::max(0, m_spacingYSpin->GetValue());
	config.includeCenterInText = m_includeCenterInTextCheck->GetValue();
	config.includeCenterInCopy = m_includeCenterInCopyCheck->GetValue();

	const NormalizedArea area = NormalizeArea(config.fromPos, config.toPos);
	config.fromPos = area.from;
	config.toPos = area.to;

	if(!config.centerPos.isValid()) {
		errorMessage = "centerPos is invalid.";
		return false;
	}
	if(!config.fromPos.isValid() || !config.toPos.isValid()) {
		errorMessage = "fromPos/toPos is invalid.";
		return false;
	}
	if(!config.playerSpawnPos.isValid()) {
		errorMessage = "playerSpawnPos is invalid.";
		return false;
	}
	if(!config.bossSpawnPos.isValid()) {
		errorMessage = "bossSpawnPos is invalid.";
		return false;
	}
	if(area.width <= 0 || area.height <= 0 || area.floors <= 0) {
		errorMessage = "The selected area size is invalid.";
		return false;
	}

	return true;
}

std::vector<InstanceLayoutDialog::InstancePlacement> InstanceLayoutDialog::BuildPlacements(const LayoutConfig& config) const
{
	std::vector<InstancePlacement> placements;

	const NormalizedArea area = NormalizeArea(config.fromPos, config.toPos);
	const int stepX = area.width + config.spacingX;
	const int stepY = area.height + config.spacingY;

	const int rowCount = config.northCount + config.southCount + 1;
	const int columnCount = config.leftCount + config.rightCount + 1;
	placements.reserve(static_cast<size_t>(rowCount * columnCount));

	for(int row = -config.northCount; row <= config.southCount; ++row) {
		for(int col = -config.leftCount; col <= config.rightCount; ++col) {
			InstancePlacement placement;
			placement.columnIndex = col;
			placement.rowIndex = row;
			placement.isCenter = (row == 0 && col == 0);
			placement.offset = Position(col * stepX, row * stepY, 0);
			placement.centerPos = OffsetPosition(config.centerPos, placement.offset);
			placement.fromPos = OffsetPosition(config.fromPos, placement.offset);
			placement.toPos = OffsetPosition(config.toPos, placement.offset);
			placement.playerSpawnPos = OffsetPosition(config.playerSpawnPos, placement.offset);
			placement.bossSpawnPos = OffsetPosition(config.bossSpawnPos, placement.offset);
			placements.push_back(placement);
		}
	}

	return placements;
}

wxString InstanceLayoutDialog::BuildLuaText(
	const LayoutConfig& config,
	const std::vector<InstancePlacement>& placements
) const {
	std::ostringstream output;

	int rowNumber = 0;
	bool hasAnyLine = false;
	for(int row = -config.northCount; row <= config.southCount; ++row) {
		std::vector<const InstancePlacement*> rowPlacements;
		for(const InstancePlacement& placement : placements) {
			if(placement.rowIndex != row) {
				continue;
			}
			if(placement.isCenter && !config.includeCenterInText) {
				continue;
			}
			rowPlacements.push_back(&placement);
		}

		if(rowPlacements.empty()) {
			continue;
		}

		++rowNumber;
		output << "-- Fileira " << rowNumber << " (Y = " << rowPlacements.front()->centerPos.y << ")";
		if(row == 0) {
			output << " - centro";
		}
		output << "\n";

		for(const InstancePlacement* placement : rowPlacements) {
			output << "{centerPos = " << FormatLuaPosition(placement->centerPos)
				<< ", fromPos = " << FormatLuaPosition(placement->fromPos)
				<< ", toPos = " << FormatLuaPosition(placement->toPos)
				<< ", spawnPos = " << FormatLuaPosition(placement->bossSpawnPos)
				<< ", destination = " << FormatLuaPosition(placement->playerSpawnPos) << "},\n";
		}
		output << "\n";
		hasAnyLine = true;
	}

	if(!hasAnyLine) {
		output << "-- No instances selected for output.\n";
	}

	return wxstr(output.str());
}

bool InstanceLayoutDialog::ApplyInstancesToMap(
	const LayoutConfig& config,
	const std::vector<InstancePlacement>& placements,
	wxString& errorMessage,
	int& instancesPlaced,
	int& tilesCopied
) {
	instancesPlaced = 0;
	tilesCopied = 0;

	Editor* editor = g_gui.GetCurrentEditor();
	if(!editor) {
		errorMessage = "No map is currently open.";
		return false;
	}

	Map& map = editor->map;
	const NormalizedArea sourceArea = NormalizeArea(config.fromPos, config.toPos);

	std::vector<Position> sourceTilePositions;
	for(int z = sourceArea.from.z; z <= sourceArea.to.z; ++z) {
		for(int y = sourceArea.from.y; y <= sourceArea.to.y; ++y) {
			for(int x = sourceArea.from.x; x <= sourceArea.to.x; ++x) {
				if(map.getTile(x, y, z)) {
					sourceTilePositions.push_back(Position(x, y, z));
				}
			}
		}
	}

	if(sourceTilePositions.empty()) {
		errorMessage = "No tiles were found in fromPos/toPos area.";
		return false;
	}

	std::vector<const InstancePlacement*> copyTargets;
	copyTargets.reserve(placements.size());
	for(const InstancePlacement& placement : placements) {
		if(placement.isCenter && !config.includeCenterInCopy) {
			continue;
		}
		copyTargets.push_back(&placement);
	}

	if(copyTargets.empty()) {
		errorMessage = "No target instances selected for paste. Increase counts or enable center copy.";
		return false;
	}

	auto batch = editor->actionQueue->createBatch(ACTION_PASTE_TILES);
	std::vector<Position> changedPositions;
	changedPositions.reserve(sourceTilePositions.size() * copyTargets.size());

	for(const InstancePlacement* placement : copyTargets) {
		auto action = editor->actionQueue->createAction(batch.get());
		int placementChanges = 0;

		for(const Position& sourcePos : sourceTilePositions) {
			Tile* sourceTile = map.getTile(sourcePos);
			if(!sourceTile) {
				continue;
			}

			const Position destinationPos = OffsetPosition(sourcePos, placement->offset);
			if(!destinationPos.isValid()) {
				continue;
			}

			TileLocation* location = map.createTileL(destinationPos);
			if(!location) {
				continue;
			}

			auto copyTile = TileOperations::deepCopy(sourceTile, map);
			Tile* oldDestinationTile = location->get();
			std::unique_ptr<Tile> newDestinationTile;
			copyTile->setLocation(location);

			if(g_settings.getInteger(Config::MERGE_PASTE) || !copyTile->ground) {
				if(oldDestinationTile) {
					newDestinationTile = TileOperations::deepCopy(oldDestinationTile, map);
				} else {
					newDestinationTile = map.allocator(location);
				}
				TileOperations::merge(newDestinationTile.get(), copyTile.get());
			} else {
				newDestinationTile = std::move(copyTile);
			}

			// Ensure neighbouring tiles exist so border logic can work.
			map.createTile(destinationPos.x - 1, destinationPos.y - 1, destinationPos.z);
			map.createTile(destinationPos.x, destinationPos.y - 1, destinationPos.z);
			map.createTile(destinationPos.x + 1, destinationPos.y - 1, destinationPos.z);
			map.createTile(destinationPos.x - 1, destinationPos.y, destinationPos.z);
			map.createTile(destinationPos.x + 1, destinationPos.y, destinationPos.z);
			map.createTile(destinationPos.x - 1, destinationPos.y + 1, destinationPos.z);
			map.createTile(destinationPos.x, destinationPos.y + 1, destinationPos.z);
			map.createTile(destinationPos.x + 1, destinationPos.y + 1, destinationPos.z);

			action->addChange(std::make_unique<Change>(std::move(newDestinationTile)));
			changedPositions.push_back(destinationPos);
			++placementChanges;
			++tilesCopied;
		}

		batch->addAndCommitAction(std::move(action));
		if(placementChanges > 0) {
			++instancesPlaced;
		}
	}

	if(changedPositions.empty()) {
		// batch is a unique_ptr, auto-deleted
		errorMessage = "No tiles were pasted. Check map bounds and instance spacing.";
		return false;
	}

	if(g_settings.getInteger(Config::USE_AUTOMAGIC) && g_settings.getInteger(Config::BORDERIZE_PASTE)) {
		auto borderAction = editor->actionQueue->createAction(batch.get());
		std::set<Tile*> borderTiles;

		for(const Position& pos : changedPositions) {
			bool addCenter = false;
			for(int y = -1; y <= 1; ++y) {
				for(int x = -1; x <= 1; ++x) {
					if(x == 0 && y == 0) {
						continue;
					}
					Tile* neighbour = map.getTile(pos.x + x, pos.y + y, pos.z);
					if(neighbour && !neighbour->isSelected()) {
						borderTiles.insert(neighbour);
						addCenter = true;
					}
				}
			}

			if(addCenter) {
				Tile* centerTile = map.getTile(pos);
				if(centerTile) {
					borderTiles.insert(centerTile);
				}
			}
		}

		for(Tile* tile : borderTiles) {
			if(!tile) {
				continue;
			}

			auto newTile = TileOperations::deepCopy(tile, map);
			TileOperations::borderize(newTile.get(), &map);
			if(tile->ground && tile->ground->isSelected()) {
				TileOperations::selectGround(newTile.get());
			}
			TileOperations::wallize(newTile.get(), &map);
			borderAction->addChange(std::make_unique<Change>(std::move(newTile)));
		}

		batch->addAndCommitAction(std::move(borderAction));
	}

	editor->addBatch(std::move(batch));
	g_gui.UpdateMenubar();
	return true;
}

bool InstanceLayoutDialog::CopyTextToClipboard(const wxString& text) const
{
	if(text.empty()) {
		return false;
	}

	if(!wxTheClipboard) {
		return false;
	}
	if(!wxTheClipboard->Open()) {
		return false;
	}

	wxTheClipboard->SetData(newd wxTextDataObject(text));
	wxTheClipboard->Close();
	return true;
}

void InstanceLayoutDialog::OnPickArea(wxCommandEvent&)
{
	if(!g_gui.IsEditorOpen()) {
		return;
	}

	if(m_pickStatusText) {
		m_pickStatusText->SetForegroundColour(Theme::Get(Theme::Role::Accent));
		m_pickStatusText->SetLabel("Click first corner on map...");
	}

	g_gui.BeginRectanglePick(
		[this](const Position& first, const Position& second) {
			const NormalizedArea area = NormalizeArea(first, second);
			const Position center(
				(area.from.x + area.to.x) / 2,
				(area.from.y + area.to.y) / 2,
				(area.from.z + area.to.z) / 2
			);

			m_fromPosCtrl->SetPosition(area.from);
			m_toPosCtrl->SetPosition(area.to);
			m_centerPosCtrl->SetPosition(center);

			if(m_pickStatusText) {
				m_pickStatusText->SetForegroundColour(Theme::Get(Theme::Role::Success));
				m_pickStatusText->SetLabel(wxString::Format("Area selected: (%d,%d,%d) -> (%d,%d,%d)",
					area.from.x, area.from.y, area.from.z,
					area.to.x, area.to.y, area.to.z));
			}

			UpdateAreaSummary();
			UpdateLayoutSummary();
		},
		[this]() {
			if(m_pickStatusText) {
				m_pickStatusText->SetForegroundColour(Theme::Get(Theme::Role::Warning));
				m_pickStatusText->SetLabel("Area pick cancelled.");
			}
		},
		[this](const Position& first) {
			if(m_pickStatusText) {
				m_pickStatusText->SetForegroundColour(Theme::Get(Theme::Role::Warning));
				m_pickStatusText->SetLabel(wxString::Format("First corner: (%d,%d,%d). Click second corner...",
					first.x, first.y, first.z));
			}
		}
	);
}

void InstanceLayoutDialog::OnUseSelection(wxCommandEvent&)
{
	ApplySelectionBounds(true);
	UpdateAreaSummary();
	UpdateLayoutSummary();
}

void InstanceLayoutDialog::OnBasePositionTextChanged(wxCommandEvent& event)
{
	UpdateAreaSummary();
	UpdateLayoutSummary();
	event.Skip();
}

void InstanceLayoutDialog::OnLayoutSpinChanged(wxSpinEvent&)
{
	UpdateLayoutSummary();
}

void InstanceLayoutDialog::OnLayoutCheckChanged(wxCommandEvent&)
{
	UpdateLayoutSummary();
}

void InstanceLayoutDialog::OnGenerateText(wxCommandEvent&)
{
	UpdateAreaSummary();
	UpdateLayoutSummary();

	LayoutConfig config;
	wxString errorMessage;
	if(!BuildConfig(config, errorMessage)) {
		wxMessageBox(errorMessage, "Instance Layout", wxOK | wxICON_ERROR, this);
		return;
	}

	const std::vector<InstancePlacement> placements = BuildPlacements(config);
	const wxString outputText = BuildLuaText(config, placements);
	if(m_outputTextCtrl) {
		m_outputTextCtrl->SetValue(outputText);
	}

	bool copied = false;
	if(m_copyTextToClipboardCheck && m_copyTextToClipboardCheck->GetValue()) {
		copied = CopyTextToClipboard(outputText);
	}

	if(copied) {
		g_gui.SetStatusText("Instance layout text generated and copied to clipboard.");
	} else {
		g_gui.SetStatusText("Instance layout text generated.");
	}
}

void InstanceLayoutDialog::OnApply(wxCommandEvent&)
{
	UpdateAreaSummary();
	UpdateLayoutSummary();

	LayoutConfig config;
	wxString errorMessage;
	if(!BuildConfig(config, errorMessage)) {
		wxMessageBox(errorMessage, "Instance Layout", wxOK | wxICON_ERROR, this);
		return;
	}

	const std::vector<InstancePlacement> placements = BuildPlacements(config);
	const wxString outputText = BuildLuaText(config, placements);
	if(m_outputTextCtrl) {
		m_outputTextCtrl->SetValue(outputText);
	}

	int instancesPlaced = 0;
	int tilesCopied = 0;
	if(!ApplyInstancesToMap(config, placements, errorMessage, instancesPlaced, tilesCopied)) {
		wxMessageBox(errorMessage, "Instance Layout", wxOK | wxICON_ERROR, this);
		return;
	}

	bool copied = false;
	if(m_copyTextToClipboardCheck && m_copyTextToClipboardCheck->GetValue()) {
		copied = CopyTextToClipboard(outputText);
	}

	g_gui.RefreshView();
	g_gui.UpdateMenubar();
	g_gui.SetStatusText(wxString::Format("Applied %d instance(s), copied %d tile(s).", instancesPlaced, tilesCopied));

	wxString message = wxString::Format("Instances applied: %d\nTiles copied: %d", instancesPlaced, tilesCopied);
	if(copied) {
		message += "\n\nLua text copied to clipboard.";
	}
	wxMessageBox(message, "Instance Layout", wxOK | wxICON_INFORMATION, this);
}

void InstanceLayoutDialog::OnCloseButton(wxCommandEvent&)
{
	g_gui.CancelRectanglePick();
	Hide();
}

void InstanceLayoutDialog::OnClose(wxCloseEvent& event)
{
	g_gui.CancelRectanglePick();
	Hide();
	event.Veto();
}
