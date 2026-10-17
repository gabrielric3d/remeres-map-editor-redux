#include "app/main.h"
#include "ui/dialogs/cave_warp_dialog.h"

#include "app/client_version.h"
#include "app/managers/version_manager.h"
#include "app/settings.h"
#include "editor/cave_warp_tool.h"
#include "editor/copybuffer.h"
#include "editor/editor.h"
#include "item_definitions/core/item_definition_store.h"
#include "map/map.h"
#include "map/tile.h"
#include "rendering/core/graphics.h"
#include "rendering/ui/map_display.h"
#include "ui/gui.h"
#include "ui/map_window.h"
#include "ui/theme.h"

#include <wx/textdlg.h>

#include <algorithm>
#include <iterator>
#include <memory>

using namespace CaveWarp;

namespace {
	constexpr int ICON_SIZE = 32;
	const int BRUSH_SIZES[] = { 1, 2, 3, 5, 7, 9, 12 };

	wxBitmap GroundBitmap(uint16_t itemId) {
		wxBitmap bmp(ICON_SIZE, ICON_SIZE, 32);
		wxMemoryDC dc(bmp);
		dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
		dc.Clear();
		Sprite* sprite = nullptr;
		if (const auto definition = g_item_definitions.get(itemId)) {
			sprite = g_gui.gfx.getSprite(definition.clientId());
		}
		if (sprite) {
			sprite->DrawTo(&dc, SPRITE_SIZE_32x32, 0, 0, ICON_SIZE, ICON_SIZE);
		}
		dc.SelectObject(wxNullBitmap);
		return bmp;
	}

	const char* ClassName(uint8_t cls) {
		switch (cls) {
			case ROCK:
				return "Rock";
			case FLOOR:
				return "Floor";
			case EXTRA:
				return "Water/Swamp";
			default:
				return "?";
		}
	}
}

// ============================================================================
// Preview: the test cave drawn by the real map renderer, in a private editor
// (same approach as the Structure Manager preview). Drag to pan, wheel to zoom.
// ============================================================================

class CaveWarpDialog::PreviewPanel : public wxPanel {
public:
	explicit PreviewPanel(wxWindow* parent) :
		wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE) {
		SetMinSize(wxSize(520, 400));
		auto* sizer = newd wxBoxSizer(wxVERTICAL);

		m_emptyLabel = newd wxStaticText(this, wxID_ANY, "");
		m_emptyLabel->SetForegroundColour(wxColour(140, 140, 150));

		EnsureEditor();
		m_mapWindow = newd MapWindow(this, *m_editor);
		m_canvas = m_mapWindow->GetCanvas();
		if (m_canvas) {
			m_canvas->Bind(wxEVT_LEFT_DOWN, &PreviewPanel::OnPanStart, this);
			m_canvas->Bind(wxEVT_RIGHT_DOWN, &PreviewPanel::OnPanStart, this);
			m_canvas->Bind(wxEVT_LEFT_UP, &PreviewPanel::OnPanEnd, this);
			m_canvas->Bind(wxEVT_RIGHT_UP, &PreviewPanel::OnPanEnd, this);
			m_canvas->Bind(wxEVT_MOTION, &PreviewPanel::OnPanMove, this);
			m_canvas->Bind(wxEVT_MOUSEWHEEL, &PreviewPanel::OnMouseWheel, this);
			m_canvas->Bind(wxEVT_LEFT_DCLICK, &PreviewPanel::OnIgnoreMouse, this);
			m_canvas->Bind(wxEVT_RIGHT_DCLICK, &PreviewPanel::OnIgnoreMouse, this);
			m_canvas->Bind(wxEVT_MIDDLE_DOWN, &PreviewPanel::OnIgnoreMouse, this);
			m_canvas->Bind(wxEVT_MIDDLE_UP, &PreviewPanel::OnIgnoreMouse, this);
			m_canvas->Bind(wxEVT_KEY_DOWN, &PreviewPanel::OnIgnoreKey, this);
			m_canvas->Bind(wxEVT_KEY_UP, &PreviewPanel::OnIgnoreKey, this);
		}

		sizer->Add(m_mapWindow, 1, wxEXPAND);
		sizer->Add(m_emptyLabel, 0, wxALIGN_CENTER | wxALL, 8);
		SetSizer(sizer);
		ShowMessage("Teach the preset a finished cave to see a test cave here.");
	}

	~PreviewPanel() override {
		// The map window draws m_editor: take it down while the editor still exists.
		if (m_canvas && m_canvas->HasCapture()) {
			m_canvas->ReleaseMouse();
		}
		if (m_mapWindow) {
			m_mapWindow->Destroy();
			m_mapWindow = nullptr;
			m_canvas = nullptr;
		}
	}

	void ShowMessage(const wxString& message) {
		m_emptyLabel->SetLabel(message);
		m_emptyLabel->Show();
		if (m_mapWindow) {
			m_mapWindow->Hide();
		}
		m_hasMap = false;
		Layout();
	}

	void Load(const CaveWarpTool::Preview& preview) {
		EnsureEditor();
		if (!m_editor || !m_mapWindow) {
			return;
		}
		// Offset so the renderer never sees negative scroll values.
		constexpr int OFFSET = 512;
		m_editor->map.clear(true);
		for (const auto& [pos, stack] : preview.tiles) {
			const Position target(pos.x + OFFSET, pos.y + OFFSET, pos.z);
			auto tile = std::make_unique<Tile>(target.x, target.y, target.z);
			stack.applyTo(*tile);
			(void)m_editor->map.setTile(target, std::move(tile));
		}
		m_editor->map.setWidth(OFFSET + preview.width + 1);
		m_editor->map.setHeight(OFFSET + preview.height + 1);
		m_minX = OFFSET;
		m_minY = OFFSET;
		m_width = std::max(1, preview.width);
		m_height = std::max(1, preview.height);
		m_floor = preview.z;

		m_hasMap = true;
		m_emptyLabel->Hide();
		m_mapWindow->Show();
		Layout();
		CallAfter([this]() { ResetView(); });
	}

private:
	void EnsureEditor() {
		if (m_editor) {
			return;
		}
		MapVersion version;
		if (g_version.IsVersionLoaded()) {
			version.otbm = g_version.GetCurrentVersion().getPrefferedMapVersionID();
			version.client = g_version.GetCurrentVersion().getProtocolID();
		} else if (ClientVersion* latest = ClientVersion::getLatestVersion()) {
			version.otbm = latest->getPrefferedMapVersionID();
			version.client = latest->getProtocolID();
		}
		m_editor = std::make_unique<Editor>(m_copyBuffer, version);
	}

	void ResetView() {
		if (!m_hasMap || !m_mapWindow || !m_canvas) {
			return;
		}
		m_canvas->floor = m_floor;

		int viewW = 0;
		int viewH = 0;
		m_mapWindow->GetViewSize(&viewW, &viewH);
		if (viewW <= 0 || viewH <= 0) {
			return;
		}

		// zoom so the whole test cave fits, with one tile of padding
		const int padding = TILE_SIZE;
		const double zoomX = double(m_width * TILE_SIZE + padding * 2) / viewW;
		const double zoomY = double(m_height * TILE_SIZE + padding * 2) / viewH;
		m_canvas->zoom = std::clamp(std::max(zoomX, zoomY), 0.125, 25.0);

		int pixelX = (m_minX + m_width / 2) * TILE_SIZE;
		int pixelY = (m_minY + m_height / 2) * TILE_SIZE;
		if (m_floor <= GROUND_LAYER) {
			pixelX -= (GROUND_LAYER - m_floor) * TILE_SIZE;
			pixelY -= (GROUND_LAYER - m_floor) * TILE_SIZE;
		}
		const int scrollX = pixelX - int(viewW * m_canvas->zoom / 2.0);
		const int scrollY = pixelY - int(viewH * m_canvas->zoom / 2.0);
		const int rangeW = std::max(scrollX + int(viewW * m_canvas->zoom) + padding, m_editor->map.getWidth() * TILE_SIZE);
		const int rangeH = std::max(scrollY + int(viewH * m_canvas->zoom) + padding, m_editor->map.getHeight() * TILE_SIZE);
		m_mapWindow->SetSize(rangeW, rangeH);
		m_mapWindow->Scroll(std::max(0, scrollX), std::max(0, scrollY));
		m_canvas->Refresh();
	}

	void OnPanStart(wxMouseEvent& event) {
		if (!m_hasMap || !m_canvas) {
			return;
		}
		m_dragging = true;
		m_lastDragPos = event.GetPosition();
		if (!m_canvas->HasCapture()) {
			m_canvas->CaptureMouse();
		}
		m_canvas->SetFocus();
	}

	void OnPanEnd(wxMouseEvent& WXUNUSED(event)) {
		if (!m_dragging) {
			return;
		}
		m_dragging = false;
		if (m_canvas && m_canvas->HasCapture()) {
			m_canvas->ReleaseMouse();
		}
	}

	void OnPanMove(wxMouseEvent& event) {
		if (!m_dragging || !m_mapWindow || !m_canvas) {
			return;
		}
		const wxPoint delta = event.GetPosition() - m_lastDragPos;
		m_lastDragPos = event.GetPosition();
		const double speed = g_settings.getFloat(Config::SCROLL_SPEED);
		const double zoom = m_canvas->GetZoom();
		m_mapWindow->ScrollRelative(-int(speed * zoom * delta.x), -int(speed * zoom * delta.y));
		m_canvas->Refresh();
	}

	void OnMouseWheel(wxMouseEvent& event) {
		if (!m_hasMap || !m_canvas || !m_mapWindow) {
			return;
		}
		double diff = -event.GetWheelRotation() * g_settings.getFloat(Config::ZOOM_SPEED) / 640.0;
		const double oldZoom = m_canvas->zoom;
		const double newZoom = std::clamp(oldZoom + diff, 0.125, 25.0);
		diff = newZoom - oldZoom;
		m_canvas->zoom = newZoom;

		// keep the point under the cursor in place
		int viewW = 0;
		int viewH = 0;
		m_mapWindow->GetViewSize(&viewW, &viewH);
		const int scrollX = int(viewW * diff * (std::max(m_canvas->cursor_x, 1) / double(std::max(viewW, 1)))) * m_canvas->GetContentScaleFactor();
		const int scrollY = int(viewH * diff * (std::max(m_canvas->cursor_y, 1) / double(std::max(viewH, 1)))) * m_canvas->GetContentScaleFactor();
		m_mapWindow->ScrollRelative(-scrollX, -scrollY);
		m_canvas->Refresh();
	}

	void OnIgnoreMouse(wxMouseEvent& WXUNUSED(event)) {
		// read-only preview
	}

	void OnIgnoreKey(wxKeyEvent& WXUNUSED(event)) {
		// read-only preview
	}

	CopyBuffer m_copyBuffer;
	std::unique_ptr<Editor> m_editor;
	MapWindow* m_mapWindow = nullptr;
	MapCanvas* m_canvas = nullptr;
	wxStaticText* m_emptyLabel = nullptr;
	bool m_hasMap = false;
	bool m_dragging = false;
	wxPoint m_lastDragPos;
	int m_minX = 0;
	int m_minY = 0;
	int m_width = 1;
	int m_height = 1;
	int m_floor = GROUND_LAYER;
};

// ============================================================================
// Dialog
// ============================================================================

CaveWarpDialog::CaveWarpDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Cave Warp", wxDefaultPosition, wxSize(1000, 640), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
	CreateControls();
	Bind(wxEVT_CLOSE_WINDOW, &CaveWarpDialog::OnClose, this);

	CaveWarpTool& tool = CaveWarpTool::Get();
	tool.SetListener([this]() { RefreshFromTool(); });
	if (tool.GetPresetName().empty()) {
		const auto names = tool.GetPresetNames();
		if (!names.empty()) {
			tool.SelectPreset(names.front());
		}
	}
	RefreshPresetChoice();
	RefreshGrounds();
	RefreshFromTool();
	if (tool.GetModel()) {
		ShowPreview(false);
	}
	CenterOnParent();
}

CaveWarpDialog::~CaveWarpDialog() {
	CaveWarpTool::Get().SetListener(nullptr);
}

void CaveWarpDialog::CreateControls() {
	auto* root = newd wxBoxSizer(wxHORIZONTAL);
	auto* left = newd wxBoxSizer(wxVERTICAL);

	// 1) Model ------------------------------------------------------------------
	auto* modelBox = newd wxStaticBoxSizer(wxVERTICAL, this, "1) Model (a finished cave teaches the style)");
	auto* presetRow = newd wxBoxSizer(wxHORIZONTAL);
	presetRow->Add(newd wxStaticText(modelBox->GetStaticBox(), wxID_ANY, "Preset:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_presetChoice = newd wxChoice(modelBox->GetStaticBox(), wxID_ANY);
	m_presetChoice->Bind(wxEVT_CHOICE, &CaveWarpDialog::OnPresetChanged, this);
	presetRow->Add(m_presetChoice, 1, wxEXPAND | wxRIGHT, 4);
	auto* newButton = newd wxButton(modelBox->GetStaticBox(), wxID_ANY, "New...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	newButton->Bind(wxEVT_BUTTON, &CaveWarpDialog::OnNewPreset, this);
	presetRow->Add(newButton, 0, wxRIGHT, 2);
	auto* deleteButton = newd wxButton(modelBox->GetStaticBox(), wxID_ANY, "Delete", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	deleteButton->Bind(wxEVT_BUTTON, &CaveWarpDialog::OnDeletePreset, this);
	presetRow->Add(deleteButton, 0);
	modelBox->Add(presetRow, 0, wxEXPAND | wxALL, 4);

	m_info = newd wxStaticText(modelBox->GetStaticBox(), wxID_ANY, "");
	modelBox->Add(m_info, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);

	auto* learnRow = newd wxBoxSizer(wxHORIZONTAL);
	auto* learnButton = newd wxButton(modelBox->GetStaticBox(), wxID_ANY, "Learn from selection");
	learnButton->SetToolTip("Adds the selected cave to the preset. Select it with about 4 tiles of rock around.");
	learnButton->Bind(wxEVT_BUTTON, &CaveWarpDialog::OnLearn, this);
	learnRow->Add(learnButton, 0, wxRIGHT, 4);
	auto* testButton = newd wxButton(modelBox->GetStaticBox(), wxID_ANY, "Test selection");
	testButton->SetToolTip("Draws a test cave with the preset plus the selected cave, without saving anything.");
	testButton->Bind(wxEVT_BUTTON, &CaveWarpDialog::OnTestSelection, this);
	learnRow->Add(testButton, 0, wxRIGHT, 4);
	auto* removeButton = newd wxButton(modelBox->GetStaticBox(), wxID_ANY, "Remove last sample");
	removeButton->Bind(wxEVT_BUTTON, &CaveWarpDialog::OnRemoveSample, this);
	learnRow->Add(removeButton, 0);
	modelBox->Add(learnRow, 0, wxALL, 4);
	left->Add(modelBox, 0, wxEXPAND | wxALL, 4);

	// 2) Ground classes ----------------------------------------------------------
	auto* classBox = newd wxStaticBoxSizer(wxVERTICAL, this, "2) Class of each ground");
	auto* classHelp = newd wxStaticText(classBox->GetStaticBox(), wxID_ANY,
		"Rock = the mountain/background. Floor = the cave floor (patches and details come by themselves). "
		"Water/Swamp = what you want to paint apart with its own brush.");
	classHelp->Wrap(400);
	classBox->Add(classHelp, 0, wxEXPAND | wxALL, 4);
	m_groundList = newd wxListCtrl(classBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxSize(-1, 150), wxLC_REPORT | wxLC_SINGLE_SEL);
	m_groundList->AppendColumn("Ground", wxLIST_FORMAT_LEFT, 110);
	m_groundList->AppendColumn("Class", wxLIST_FORMAT_LEFT, 110);
	m_groundList->AppendColumn("Tiles", wxLIST_FORMAT_RIGHT, 70);
	m_groundImages = newd wxImageList(ICON_SIZE, ICON_SIZE, true);
	m_groundList->AssignImageList(m_groundImages, wxIMAGE_LIST_SMALL); // the list owns it
	classBox->Add(m_groundList, 1, wxEXPAND | wxLEFT | wxRIGHT, 4);
	auto* classRow = newd wxBoxSizer(wxHORIZONTAL);
	const std::pair<uint8_t, const char*> classButtons[] = { { ROCK, "-> Rock" }, { FLOOR, "-> Floor" }, { EXTRA, "-> Water/Swamp" } };
	for (const auto& [cls, label] : classButtons) {
		auto* button = newd wxButton(classBox->GetStaticBox(), wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
		const uint8_t value = cls;
		button->Bind(wxEVT_BUTTON, [this, value](wxCommandEvent&) { OnSetClass(value); });
		classRow->Add(button, 0, wxRIGHT, 4);
	}
	classBox->Add(classRow, 0, wxALL, 4);
	left->Add(classBox, 1, wxEXPAND | wxALL, 4);

	// 3) Brush -------------------------------------------------------------------
	auto* brushBox = newd wxStaticBoxSizer(wxVERTICAL, this, "3) Brush (paint on the map, Esc releases the mouse)");
	auto* toolRow = newd wxBoxSizer(wxHORIZONTAL);
	const std::pair<Tool, const char*> tools[] = {
		{ Tool::Open, "Open" },
		{ Tool::Close, "Close" },
		{ Tool::Extra, "Water/Swamp" },
		{ Tool::Dry, "Dry" },
		{ Tool::Reroll, "Reroll" },
	};
	for (const auto& [kind, label] : tools) {
		auto* button = newd wxToggleButton(brushBox->GetStaticBox(), wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
		const Tool value = kind;
		button->Bind(wxEVT_TOGGLEBUTTON, [this, value](wxCommandEvent&) { OnToolButton(value); });
		toolRow->Add(button, 0, wxRIGHT, 2);
		m_toolButtons.emplace_back(kind, button);
	}
	auto* stopButton = newd wxButton(brushBox->GetStaticBox(), wxID_ANY, "Stop", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	stopButton->Bind(wxEVT_BUTTON, &CaveWarpDialog::OnStop, this);
	toolRow->Add(stopButton, 0, wxLEFT, 6);
	brushBox->Add(toolRow, 0, wxALL, 4);

	CaveWarpTool& tool = CaveWarpTool::Get();
	auto* shapeRow = newd wxBoxSizer(wxHORIZONTAL);
	shapeRow->Add(newd wxStaticText(brushBox->GetStaticBox(), wxID_ANY, "Shape:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_shapeChoice = newd wxChoice(brushBox->GetStaticBox(), wxID_ANY);
	m_shapeChoice->Append("Circle");
	m_shapeChoice->Append("Square");
	m_shapeChoice->SetSelection(tool.IsCircle() ? 0 : 1);
	shapeRow->Add(m_shapeChoice, 0, wxRIGHT, 8);
	shapeRow->Add(newd wxStaticText(brushBox->GetStaticBox(), wxID_ANY, "Size:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_sizeChoice = newd wxChoice(brushBox->GetStaticBox(), wxID_ANY);
	int sizeIndex = 2;
	for (size_t i = 0; i < std::size(BRUSH_SIZES); ++i) {
		m_sizeChoice->Append(wxString::Format("%d", BRUSH_SIZES[i]));
		if (BRUSH_SIZES[i] == tool.GetSize()) {
			sizeIndex = static_cast<int>(i);
		}
	}
	m_sizeChoice->SetSelection(sizeIndex);
	shapeRow->Add(m_sizeChoice, 0, wxRIGHT, 8);
	shapeRow->Add(newd wxStaticText(brushBox->GetStaticBox(), wxID_ANY, "Redrawn margin:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_marginSpin = newd wxSpinCtrl(brushBox->GetStaticBox(), wxID_ANY, "", wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 1, 4, tool.GetMargin());
	m_marginSpin->SetToolTip("Tiles around the stroke that are redrawn when they no longer fit.");
	shapeRow->Add(m_marginSpin, 0);
	brushBox->Add(shapeRow, 0, wxALL, 4);

	m_fixCheck = newd wxCheckBox(brushBox->GetStaticBox(), wxID_ANY, "Fix shapes the model does not have (1-tile walls, corridors...)");
	m_fixCheck->SetValue(tool.GetFixShapes());
	brushBox->Add(m_fixCheck, 0, wxALL, 4);
	m_protectCheck = newd wxCheckBox(brushBox->GetStaticBox(), wxID_ANY, "Protect tiles whose ground is not in the preset (they change only where painted)");
	m_protectCheck->SetValue(tool.GetProtectForeign());
	brushBox->Add(m_protectCheck, 0, wxALL, 4);

	m_shapeChoice->Bind(wxEVT_CHOICE, &CaveWarpDialog::OnOptionChanged, this);
	m_sizeChoice->Bind(wxEVT_CHOICE, &CaveWarpDialog::OnOptionChanged, this);
	m_marginSpin->Bind(wxEVT_SPINCTRL, &CaveWarpDialog::OnOptionChanged, this);
	m_fixCheck->Bind(wxEVT_CHECKBOX, &CaveWarpDialog::OnOptionChanged, this);
	m_protectCheck->Bind(wxEVT_CHECKBOX, &CaveWarpDialog::OnOptionChanged, this);
	left->Add(brushBox, 0, wxEXPAND | wxALL, 4);

	m_status = newd wxStaticText(this, wxID_ANY, "Ready.");
	left->Add(m_status, 0, wxEXPAND | wxALL, 6);

	// Preview ----------------------------------------------------------------------
	auto* right = newd wxBoxSizer(wxVERTICAL);
	auto* previewHeader = newd wxBoxSizer(wxHORIZONTAL);
	m_previewLabel = newd wxStaticText(this, wxID_ANY, "Test cave");
	previewHeader->Add(m_previewLabel, 1, wxALIGN_CENTER_VERTICAL);
	auto* rerollButton = newd wxButton(this, wxID_ANY, "New test cave", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	rerollButton->SetToolTip("Draws another random cave with the current preset.");
	rerollButton->Bind(wxEVT_BUTTON, &CaveWarpDialog::OnNewTestCave, this);
	previewHeader->Add(rerollButton, 0);
	right->Add(previewHeader, 0, wxEXPAND | wxALL, 4);
	m_preview = newd PreviewPanel(this);
	right->Add(m_preview, 1, wxEXPAND | wxALL, 4);

	root->Add(left, 0, wxEXPAND);
	root->Add(right, 1, wxEXPAND);
	SetSizer(root);
	Layout();
}

void CaveWarpDialog::RefreshPresetChoice() {
	CaveWarpTool& tool = CaveWarpTool::Get();
	m_presetChoice->Clear();
	for (const std::string& name : tool.GetPresetNames()) {
		m_presetChoice->Append(wxString::FromUTF8(name));
	}
	const int index = m_presetChoice->FindString(wxString::FromUTF8(tool.GetPresetName()));
	if (index != wxNOT_FOUND) {
		m_presetChoice->SetSelection(index);
	}
}

void CaveWarpDialog::RefreshGrounds() {
	m_groundList->DeleteAllItems();
	m_groundImages->RemoveAll();
	const Preset* preset = CaveWarpTool::Get().GetPreset();
	if (!preset) {
		return;
	}
	long row = 0;
	for (const GroundClass& g : preset->grounds) {
		const int image = m_groundImages->Add(GroundBitmap(g.id));
		const long item = m_groundList->InsertItem(row, wxString::Format("%d", g.id), image);
		m_groundList->SetItem(item, 1, ClassName(g.cls));
		m_groundList->SetItem(item, 2, wxString::Format("%d", g.count));
		++row;
	}
}

void CaveWarpDialog::RefreshFromTool() {
	CaveWarpTool& tool = CaveWarpTool::Get();
	m_info->SetLabel(wxString::FromUTF8(tool.DescribePreset()));
	for (const auto& [kind, button] : m_toolButtons) {
		button->SetValue(tool.GetTool() == kind);
	}
	m_status->SetLabel(wxString::FromUTF8(tool.GetStatus().empty() ? std::string("Ready.") : tool.GetStatus()));
	m_status->Wrap(420);
	Layout();
}

void CaveWarpDialog::ShowPreview(bool withSelection) {
	Editor* editor = withSelection ? g_gui.GetCurrentEditor() : nullptr;
	if (withSelection && !editor) {
		return;
	}
	CaveWarpTool::Preview preview;
	std::string error;
	if (!CaveWarpTool::Get().GeneratePreview(editor, preview, error)) {
		m_preview->ShowMessage(wxString::FromUTF8(error));
		return;
	}
	m_previewLabel->SetLabel(withSelection ? "Test cave (preset + selection, not saved)" : "Test cave (current preset)");
	m_preview->Load(preview);
}

void CaveWarpDialog::OnPresetChanged(wxCommandEvent& WXUNUSED(event)) {
	const int index = m_presetChoice->GetSelection();
	if (index == wxNOT_FOUND) {
		return;
	}
	CaveWarpTool::Get().SelectPreset(m_presetChoice->GetString(index).ToStdString());
	RefreshGrounds();
	RefreshFromTool();
	ShowPreview(false);
}

void CaveWarpDialog::OnNewPreset(wxCommandEvent& WXUNUSED(event)) {
	const wxString name = wxGetTextFromUser("Name of the new preset (letters, digits, '-' or '_'):", "New Cave Warp preset", "", this);
	if (name.IsEmpty()) {
		return;
	}
	std::string error;
	if (!CaveWarpTool::Get().CreatePreset(name.ToStdString(), error)) {
		wxMessageBox(wxString::FromUTF8(error), "Cave Warp", wxOK | wxICON_WARNING, this);
		return;
	}
	RefreshPresetChoice();
	RefreshGrounds();
	RefreshFromTool();
	ShowPreview(false);
}

void CaveWarpDialog::OnDeletePreset(wxCommandEvent& WXUNUSED(event)) {
	CaveWarpTool& tool = CaveWarpTool::Get();
	const std::string name = tool.GetPresetName();
	if (name.empty()) {
		return;
	}
	const int answer = wxMessageBox(wxString::Format("Delete the preset '%s' and all its samples?", wxString::FromUTF8(name)), "Cave Warp", wxYES_NO | wxICON_QUESTION, this);
	if (answer != wxYES) {
		return;
	}
	tool.DeletePreset(name);
	const auto names = tool.GetPresetNames();
	if (!names.empty()) {
		tool.SelectPreset(names.front());
	}
	RefreshPresetChoice();
	RefreshGrounds();
	RefreshFromTool();
	ShowPreview(false);
}

void CaveWarpDialog::OnLearn(wxCommandEvent& WXUNUSED(event)) {
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}
	std::string error;
	if (!CaveWarpTool::Get().LearnFromSelection(*editor, error)) {
		m_status->SetLabel(wxString::FromUTF8(error));
		m_status->Wrap(420);
		Layout();
		return;
	}
	RefreshGrounds();
	RefreshFromTool();
	ShowPreview(false);
}

void CaveWarpDialog::OnTestSelection(wxCommandEvent& WXUNUSED(event)) {
	ShowPreview(true);
}

void CaveWarpDialog::OnRemoveSample(wxCommandEvent& WXUNUSED(event)) {
	CaveWarpTool::Get().RemoveLastSample();
	RefreshGrounds();
	RefreshFromTool();
	ShowPreview(false);
}

void CaveWarpDialog::OnNewTestCave(wxCommandEvent& WXUNUSED(event)) {
	ShowPreview(false);
}

void CaveWarpDialog::OnSetClass(uint8_t cls) {
	const long selected = m_groundList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (selected < 0) {
		m_status->SetLabel("Pick a ground in the list first.");
		Layout();
		return;
	}
	CaveWarpTool::Get().SetGroundClass(static_cast<size_t>(selected), cls);
	RefreshGrounds();
	m_groundList->SetItemState(selected, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
	RefreshFromTool();
	ShowPreview(false);
}

void CaveWarpDialog::OnToolButton(Tool kind) {
	CaveWarpTool& tool = CaveWarpTool::Get();
	if (tool.GetTool() == kind) {
		tool.Deactivate();
		return;
	}
	std::string error;
	if (!tool.Activate(kind, error)) {
		m_status->SetLabel(wxString::FromUTF8(error));
		m_status->Wrap(420);
	}
	RefreshFromTool();
}

void CaveWarpDialog::OnStop(wxCommandEvent& WXUNUSED(event)) {
	CaveWarpTool::Get().Deactivate();
}

void CaveWarpDialog::OnOptionChanged(wxCommandEvent& WXUNUSED(event)) {
	CaveWarpTool& tool = CaveWarpTool::Get();
	tool.SetCircle(m_shapeChoice->GetSelection() != 1);
	const int sizeIndex = m_sizeChoice->GetSelection();
	if (sizeIndex >= 0 && sizeIndex < static_cast<int>(std::size(BRUSH_SIZES))) {
		tool.SetSize(BRUSH_SIZES[sizeIndex]);
	}
	tool.SetMargin(m_marginSpin->GetValue());
	tool.SetFixShapes(m_fixCheck->GetValue());
	tool.SetProtectForeign(m_protectCheck->GetValue());
	g_gui.RefreshView();
}

void CaveWarpDialog::OnClose(wxCloseEvent& WXUNUSED(event)) {
	CaveWarpTool::Get().Deactivate();
	g_gui.DestroyCaveWarpDialog();
}
