//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Wall Brush Editor Dialog - Visual editor for wall brushes (walls.xml)
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/dialogs/wall_brush_editor_dialog.h"
#include "ui/dialogs/wall_scan_dialog.h"
#include "ui/gui.h"
#include "ui/theme.h"
#include "ui/find_item_window.h"
#include "rendering/core/graphics.h"
#include "item_definitions/core/item_definition_store.h"
#include "app/managers/version_manager.h"
#include "app/client_version.h"
#include "ext/pugixml.hpp"

#include <wx/sizer.h>
#include <wx/dcbuffer.h>
#include <wx/dnd.h>
#include <wx/statline.h>
#include <wx/arrstr.h>
#include <algorithm>
#include <cctype>
#include <sstream>

// ============================================================================
// Local IDs
// ============================================================================

#define ID_WALL_EXISTING_COMBO (wxID_HIGHEST + 1)
#define ID_WALL_ADD_ITEM (wxID_HIGHEST + 3)
#define ID_WALL_UPDATE_CHANCE (wxID_HIGHEST + 4)
#define ID_WALL_BROWSE_ITEM (wxID_HIGHEST + 5)
#define ID_WALL_ADD_DOOR (wxID_HIGHEST + 6)
#define ID_WALL_BROWSE_DOOR (wxID_HIGHEST + 7)
#define ID_WALL_FIND_BY_ITEM (wxID_HIGHEST + 8)
#define ID_WALL_TILESET_COMBO (wxID_HIGHEST + 9)
#define ID_WALL_TILESET_BRUSH_LIST (wxID_HIGHEST + 10)
#define ID_WALL_ADD_TO_TILESET (wxID_HIGHEST + 11)
#define ID_WALL_SCAN (wxID_HIGHEST + 12)

// ============================================================================
// Local helpers
// ============================================================================

namespace {

// Minimalist section header: bold accent-colored "▸ TITLE", no box.
wxStaticText* MakeSectionHeader(wxWindow* parent, const wxString& title) {
	wxStaticText* h = newd wxStaticText(parent, wxID_ANY, wxString(wxT("▸ ")) + title.Upper());
	wxFont f = h->GetFont();
	f.SetWeight(wxFONTWEIGHT_BOLD);
	h->SetFont(f);
	h->SetForegroundColour(Theme::Get(Theme::Role::Accent));
	return h;
}

void AddSectionHeader(wxBoxSizer* sizer, wxWindow* parent, const wxString& title) {
	sizer->Add(MakeSectionHeader(parent, title), 0, wxLEFT | wxRIGHT | wxTOP, 6);
	sizer->Add(newd wxStaticLine(parent), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
}

// Door types offered in the Add-door dropdown, in display order.
const char* const DOOR_TYPES[] = {
	"normal", "normal_alt", "locked", "quest", "magic", "archway", "window", "hatch_window"
};
constexpr int DOOR_TYPE_COUNT = static_cast<int>(sizeof(DOOR_TYPES) / sizeof(DOOR_TYPES[0]));

// Window-style door types omit the `open` attribute in walls.xml. Every other type
// requires `open` (the loader warns and skips a non-window door that lacks it).
bool DoorTypeUsesOpen(const std::string& type) {
	return !(type == "window" || type == "hatch_window" || type == "hatch window" || type == "any window");
}

// Strips the " (Terrain)" / " (Terrain - new)" suffix from a tileset combo label.
wxString StripTerrainSuffix(const wxString& label) {
	wxString trimmed = label;
	trimmed.Trim(true).Trim(false);
	size_t parenIdx = trimmed.rfind(" (Terrain");
	if (parenIdx != wxString::npos) {
		return wxString(trimmed.Mid(0, parenIdx)).Trim(true).Trim(false);
	}
	return trimmed;
}

// Parses the payload used by every RME drag source ("RME_ITEM:<id>", and the older
// "ITEM_ID:<id>" some panels still emit). Returns 0 when the text is not an item drag.
uint16_t ParseDraggedItemId(const wxString& data) {
	wxString payload = data;
	if (payload.StartsWith("RME_ITEM:")) {
		payload = payload.Mid(9);
	} else if (payload.StartsWith("ITEM_ID:")) {
		payload = payload.Mid(8);
	} else {
		return 0;
	}

	unsigned long idVal = 0;
	if (!payload.ToULong(&idVal) || idVal == 0 || idVal > 0xFFFF) {
		return 0;
	}
	return static_cast<uint16_t>(idVal);
}

// Walks up from a cell panel to the hosting editor panel.
WallBrushEditorDialog* FindWallEditor(wxWindow* child) {
	wxWindow* parent = child ? child->GetParent() : nullptr;
	while (parent && !dynamic_cast<WallBrushEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}
	return dynamic_cast<WallBrushEditorDialog*>(parent);
}

// Drops a palette item into the items grid of the segment currently being edited.
class WallSegmentItemsDropTarget : public wxTextDropTarget {
public:
	explicit WallSegmentItemsDropTarget(WallSegmentItemsPanel* panel) : m_panel(panel) { }

	bool OnDropText(wxCoord /*x*/, wxCoord /*y*/, const wxString& data) override {
		const uint16_t itemId = ParseDraggedItemId(data);
		if (itemId == 0) return false;

		WallBrushEditorDialog* dialog = FindWallEditor(m_panel);
		if (!dialog) return false;

		dialog->AddItemById(itemId);
		return true;
	}

private:
	WallSegmentItemsPanel* m_panel;
};

// Same, for the doors/windows grid.
class WallDoorsDropTarget : public wxTextDropTarget {
public:
	explicit WallDoorsDropTarget(WallDoorsPanel* panel) : m_panel(panel) { }

	bool OnDropText(wxCoord /*x*/, wxCoord /*y*/, const wxString& data) override {
		const uint16_t itemId = ParseDraggedItemId(data);
		if (itemId == 0) return false;

		WallBrushEditorDialog* dialog = FindWallEditor(m_panel);
		if (!dialog) return false;

		dialog->AddDoorById(itemId);
		return true;
	}

private:
	WallDoorsPanel* m_panel;
};

} // namespace

const char* wallSegmentToString(WallSegmentType seg) {
	switch (seg) {
		case WALL_SEG_HORIZONTAL: return "horizontal";
		case WALL_SEG_VERTICAL: return "vertical";
		// "corner" and "northwest diagonal" are the same alignment for the loader;
		// we keep writing "corner" because that is what every shipped walls.xml uses.
		case WALL_SEG_CORNER: return "corner";
		case WALL_SEG_POLE: return "pole";
		case WALL_SEG_NE_DIAGONAL: return "northeast diagonal";
		case WALL_SEG_SW_DIAGONAL: return "southwest diagonal";
		case WALL_SEG_SE_DIAGONAL: return "southeast diagonal";
		case WALL_SEG_NORTH_T: return "north T";
		case WALL_SEG_SOUTH_T: return "south T";
		case WALL_SEG_EAST_T: return "east T";
		case WALL_SEG_WEST_T: return "west T";
		case WALL_SEG_INTERSECTION: return "intersection";
		case WALL_SEG_NORTH_END: return "north end";
		case WALL_SEG_SOUTH_END: return "south end";
		case WALL_SEG_EAST_END: return "east end";
		case WALL_SEG_WEST_END: return "west end";
		default: return "";
	}
}

WallSegmentType wallSegmentFromString(const std::string& str) {
	// The engine loader matches these names case-sensitively ("south T"), but hand-edited
	// files show up with every casing, so the editor is lenient on the way in and
	// canonical on the way out (wallSegmentToString).
	std::string key = str;
	std::transform(key.begin(), key.end(), key.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (key == "horizontal") return WALL_SEG_HORIZONTAL;
	if (key == "vertical") return WALL_SEG_VERTICAL;
	if (key == "corner" || key == "northwest diagonal") return WALL_SEG_CORNER;
	if (key == "pole") return WALL_SEG_POLE;
	if (key == "northeast diagonal") return WALL_SEG_NE_DIAGONAL;
	if (key == "southwest diagonal") return WALL_SEG_SW_DIAGONAL;
	if (key == "southeast diagonal") return WALL_SEG_SE_DIAGONAL;
	if (key == "north t") return WALL_SEG_NORTH_T;
	if (key == "south t") return WALL_SEG_SOUTH_T;
	if (key == "east t") return WALL_SEG_EAST_T;
	if (key == "west t") return WALL_SEG_WEST_T;
	if (key == "intersection") return WALL_SEG_INTERSECTION;
	if (key == "north end") return WALL_SEG_NORTH_END;
	if (key == "south end") return WALL_SEG_SOUTH_END;
	if (key == "east end") return WALL_SEG_EAST_END;
	if (key == "west end") return WALL_SEG_WEST_END;
	return WALL_SEG_COUNT; // "untouchable" included: preserved verbatim, never modeled
}

const char* wallSegmentLabel(WallSegmentType seg) {
	switch (seg) {
		case WALL_SEG_HORIZONTAL: return "horizontal";
		case WALL_SEG_VERTICAL: return "vertical";
		case WALL_SEG_CORNER: return "corner (north + west)";
		case WALL_SEG_POLE: return "pole (no connections)";
		case WALL_SEG_NE_DIAGONAL: return "corner (north + east)";
		case WALL_SEG_SW_DIAGONAL: return "corner (west + south)";
		case WALL_SEG_SE_DIAGONAL: return "corner (east + south)";
		case WALL_SEG_NORTH_T: return "north T (west + east + south)";
		case WALL_SEG_SOUTH_T: return "south T (north + west + east)";
		case WALL_SEG_EAST_T: return "east T (north + west + south)";
		case WALL_SEG_WEST_T: return "west T (north + east + south)";
		case WALL_SEG_INTERSECTION: return "intersection (all four)";
		case WALL_SEG_NORTH_END: return "north end (south only)";
		case WALL_SEG_SOUTH_END: return "south end (north only)";
		case WALL_SEG_EAST_END: return "east end (west only)";
		case WALL_SEG_WEST_END: return "west end (east only)";
		default: return "";
	}
}

int wallSegmentConnections(WallSegmentType seg) {
	// Same bits the engine uses in WallBrush::full_border_types: N=1, W=2, E=4, S=8.
	constexpr int N = 1;
	constexpr int W = 2;
	constexpr int E = 4;
	constexpr int S = 8;
	switch (seg) {
		case WALL_SEG_HORIZONTAL: return W | E;
		case WALL_SEG_VERTICAL: return N | S;
		case WALL_SEG_CORNER: return N | W;
		case WALL_SEG_POLE: return 0;
		case WALL_SEG_NE_DIAGONAL: return N | E;
		case WALL_SEG_SW_DIAGONAL: return W | S;
		case WALL_SEG_SE_DIAGONAL: return E | S;
		case WALL_SEG_NORTH_T: return W | E | S;
		case WALL_SEG_SOUTH_T: return N | W | E;
		case WALL_SEG_EAST_T: return N | W | S;
		case WALL_SEG_WEST_T: return N | E | S;
		case WALL_SEG_INTERSECTION: return N | W | E | S;
		case WALL_SEG_NORTH_END: return S;
		case WALL_SEG_SOUTH_END: return N;
		case WALL_SEG_EAST_END: return W;
		case WALL_SEG_WEST_END: return E;
		default: return 0;
	}
}

// Serialize a pugixml node (e.g. an unmodeled <wall> segment) to a raw XML string so it
// can be stashed on load and re-appended verbatim on save.
static std::string SerializeXmlNode(const pugi::xml_node& node) {
	std::ostringstream ss;
	node.print(ss, "", pugi::format_raw);
	return ss.str();
}

// True for the brush-level attributes the editor manages itself; every other attribute
// (e.g. activated="true") is preserved verbatim across a load/save round trip.
static bool IsManagedWallBrushAttr(const std::string& name) {
	return name == "name" || name == "type" || name == "server_lookid" || name == "lookid"
		|| name == "draggable" || name == "on_blocking" || name == "thickness";
}

// ============================================================================
// Event tables
// ============================================================================

BEGIN_EVENT_TABLE(WallBrushEditorDialog, wxPanel)
	EVT_BUTTON(ID_WALL_ADD_ITEM, WallBrushEditorDialog::OnAddItem)
	EVT_BUTTON(ID_WALL_UPDATE_CHANCE, WallBrushEditorDialog::OnUpdateChance)
	EVT_BUTTON(ID_WALL_BROWSE_ITEM, WallBrushEditorDialog::OnBrowseItem)
	EVT_BUTTON(ID_WALL_ADD_DOOR, WallBrushEditorDialog::OnAddDoor)
	EVT_BUTTON(ID_WALL_BROWSE_DOOR, WallBrushEditorDialog::OnBrowseDoor)
	EVT_BUTTON(ID_WALL_SCAN, WallBrushEditorDialog::OnScanWall)
	EVT_BUTTON(wxID_CLEAR, WallBrushEditorDialog::OnClear)
	EVT_BUTTON(wxID_SAVE, WallBrushEditorDialog::OnSave)
	EVT_BUTTON(ID_WALL_FIND_BY_ITEM, WallBrushEditorDialog::OnFindByItemId)
	EVT_COMBOBOX(ID_WALL_EXISTING_COMBO, WallBrushEditorDialog::OnLoadWall)
	EVT_TEXT_ENTER(ID_WALL_EXISTING_COMBO, WallBrushEditorDialog::OnLoadWall)
	EVT_TEXT(ID_WALL_EXISTING_COMBO, WallBrushEditorDialog::OnLoadTextChanged)
	EVT_COMBOBOX(ID_WALL_TILESET_COMBO, WallBrushEditorDialog::OnTilesetSelectionChanged)
	EVT_TEXT(ID_WALL_TILESET_COMBO, WallBrushEditorDialog::OnTilesetSelectionChanged)
	EVT_BUTTON(ID_WALL_ADD_TO_TILESET, WallBrushEditorDialog::OnAddToTileset)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(WallSegmentGridPanel, wxPanel)
	EVT_PAINT(WallSegmentGridPanel::OnPaint)
	EVT_LEFT_UP(WallSegmentGridPanel::OnMouseClick)
	EVT_MOTION(WallSegmentGridPanel::OnMotion)
	EVT_LEAVE_WINDOW(WallSegmentGridPanel::OnLeave)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(WallSegmentItemsPanel, wxPanel)
	EVT_PAINT(WallSegmentItemsPanel::OnPaint)
	EVT_LEFT_UP(WallSegmentItemsPanel::OnMouseClick)
	EVT_SIZE(WallSegmentItemsPanel::OnSize)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(WallDoorsPanel, wxPanel)
	EVT_PAINT(WallDoorsPanel::OnPaint)
	EVT_LEFT_UP(WallDoorsPanel::OnMouseClick)
	EVT_SIZE(WallDoorsPanel::OnSize)
END_EVENT_TABLE()

// ============================================================================
// WallBrushEditorDialog
// ============================================================================

WallBrushEditorDialog::WallBrushEditorDialog(wxWindow* parent) :
	wxPanel(parent, wxID_ANY) {

	SetBackgroundColour(Theme::Get(Theme::Role::Surface));

	wxFont compactFont = GetFont();
	compactFont.SetPointSize(std::max(6, compactFont.GetPointSize() - 1));
	SetFont(compactFont);

	CreateGUIControls();
	LoadExistingWalls();
	LoadExistingTilesets();
	RefreshSegmentPanels();
}

WallBrushEditorDialog::~WallBrushEditorDialog() = default;

wxString WallBrushEditorDialog::GetVersionDataDirectory() {
	ClientVersion* version = g_version.getLoadedVersion();
	if (!version) return wxString();
	FileName data_path = version->getDataPath();
	return data_path.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

wxArrayString WallBrushEditorDialog::GetWallMaterialFiles() {
	wxArrayString files;
	wxString dataDir = GetVersionDataDirectory();
	if (dataDir.IsEmpty()) return files;

	// Read the <include> list from materials.xml — a wall brush can live in any of
	// the included files (walls.xml, doodads.xml, ...), not just walls.xml.
	wxString materialsFile = dataDir + "materials.xml";
	if (wxFileExists(materialsFile)) {
		pugi::xml_document doc;
		if (doc.load_file(materialsFile.ToStdString().c_str())) {
			pugi::xml_node materials = doc.child("materials");
			if (materials) {
				for (pugi::xml_node inc = materials.child("include"); inc; inc = inc.next_sibling("include")) {
					wxString f(inc.attribute("file").as_string());
					if (f.IsEmpty()) continue;
					wxString full = dataDir + f;
					if (wxFileExists(full) && files.Index(full) == wxNOT_FOUND) {
						files.Add(full);
					}
				}
			}
		}
	}

	// Always keep walls.xml in the list: it's the default save target and the
	// canonical home for wall brushes even if materials.xml is missing/unreadable.
	wxString wallsFile = dataDir + "walls.xml";
	if (wxFileExists(wallsFile) && files.Index(wallsFile) == wxNOT_FOUND) {
		files.Add(wallsFile);
	}
	return files;
}

void WallBrushEditorDialog::CreateGUIControls() {
	wxBoxSizer* topSizer = newd wxBoxSizer(wxVERTICAL);

	// --- Properties bar (single row, no box) ---
	wxBoxSizer* propsRow = newd wxBoxSizer(wxHORIZONTAL);

	propsRow->Add(newd wxStaticText(this, wxID_ANY, "Look ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_serverLookIdCtrl = newd wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535);
	m_serverLookIdCtrl->SetToolTip("Server-side item ID used as the brush's palette look (server_lookid).");
	propsRow->Add(m_serverLookIdCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

	propsRow->Add(newd wxStaticText(this, wxID_ANY, "Load:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_existingWallsCombo = newd wxComboBox(this, ID_WALL_EXISTING_COMBO, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_DROPDOWN | wxTE_PROCESS_ENTER);
	m_existingWallsCombo->SetToolTip("Type to search, or pick from the list. Press Enter or select to load an existing wall brush.");
	propsRow->Add(m_existingWallsCombo, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_loadPreview = newd BorderNorthPreview(this);
	m_loadPreview->SetToolTip("Preview: look item of the selected wall brush.");
	propsRow->Add(m_loadPreview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

	propsRow->Add(newd wxStaticText(this, wxID_ANY, "Find ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_findItemIdCtrl = newd wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535);
	m_findItemIdCtrl->SetToolTip("Enter an item ID to locate the wall brush that uses it.");
	propsRow->Add(m_findItemIdCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	wxButton* findButton = newd wxButton(this, ID_WALL_FIND_BY_ITEM, "Find", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	findButton->SetToolTip("Find and load the wall brush that uses this item ID.");
	propsRow->Add(findButton, 0, wxALIGN_CENTER_VERTICAL);

	topSizer->Add(propsRow, 0, wxEXPAND | wxALL, 8);

	// --- Two columns: Segments+Items (left 60%) | Doors+Tileset (right 40%) ---
	wxBoxSizer* colsRow = newd wxBoxSizer(wxHORIZONTAL);

	// LEFT — segment selector + items
	wxBoxSizer* leftCol = newd wxBoxSizer(wxVERTICAL);
	AddSectionHeader(leftCol, this, "Wall Segments");

	m_segmentGrid = newd WallSegmentGridPanel(this);
	m_segmentGrid->SetToolTip("Pick which wall piece you are editing. Each piece has its own items and doors.");
	leftCol->Add(m_segmentGrid, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	m_segmentCaption = newd wxStaticText(this, wxID_ANY, "Items for horizontal:");
	leftCol->Add(m_segmentCaption, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
	m_itemsPanel = newd WallSegmentItemsPanel(this);
	m_itemsPanel->SetToolTip("Item variants for this segment. The first non-zero chance item is the one drawn.\n"
		"Drag an item from the palette and drop it here to add it.");
	m_itemsPanel->SetDropTarget(newd WallSegmentItemsDropTarget(m_itemsPanel));
	leftCol->Add(m_itemsPanel, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

	// Item ID + chance row
	wxBoxSizer* itemIdRow = newd wxBoxSizer(wxHORIZONTAL);
	itemIdRow->Add(newd wxStaticText(this, wxID_ANY, "Item ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_itemIdCtrl = newd wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535);
	m_itemIdCtrl->SetToolTip("ID of the wall item to add.");
	itemIdRow->Add(m_itemIdCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	itemIdRow->Add(newd wxStaticText(this, wxID_ANY, "Chance:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_itemChanceCtrl = newd wxSpinCtrl(this, wxID_ANY, "100", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 100000);
	m_itemChanceCtrl->SetToolTip("Chance weight for this item variant (0 = backwards-compat only).");
	itemIdRow->Add(m_itemChanceCtrl, 0, wxALIGN_CENTER_VERTICAL);
	leftCol->Add(itemIdRow, 0, wxEXPAND | wxALL, 6);

	// Item buttons row
	wxBoxSizer* itemBtnRow = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* browseItemButton = newd wxButton(this, ID_WALL_BROWSE_ITEM, "Browse...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	browseItemButton->SetToolTip("Browse for an item.");
	itemBtnRow->Add(browseItemButton, 0, wxRIGHT, 4);
	wxButton* addItemButton = newd wxButton(this, ID_WALL_ADD_ITEM, "+ Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	addItemButton->SetToolTip("Add the item to the selected segment.");
	itemBtnRow->Add(addItemButton, 0, wxRIGHT, 4);
	wxButton* updateChanceButton = newd wxButton(this, ID_WALL_UPDATE_CHANCE, "Set Chance", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	updateChanceButton->SetToolTip("Update the chance of the selected item.");
	itemBtnRow->Add(updateChanceButton, 0, wxRIGHT, 4);

	m_scanButton = newd wxButton(this, ID_WALL_SCAN, "Scan...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	if (g_gui.gfx.isUnloaded()) {
		m_scanButton->Enable(false);
		m_scanButton->SetToolTip("Requires a loaded client (sprites)");
	} else {
		m_scanButton->SetToolTip("Classify candidate items into wall segments by sprite shape");
	}
	itemBtnRow->Add(m_scanButton, 0);
	leftCol->Add(itemBtnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	colsRow->Add(leftCol, 60, wxEXPAND | wxRIGHT, 8);

	// RIGHT — doors (top) + tileset (bottom)
	wxBoxSizer* rightCol = newd wxBoxSizer(wxVERTICAL);

	// ▸ DOORS & WINDOWS
	AddSectionHeader(rightCol, this, "Doors & Windows");

	m_doorsCaption = newd wxStaticText(this, wxID_ANY, "Doors for horizontal:");
	rightCol->Add(m_doorsCaption, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
	m_doorsPanel = newd WallDoorsPanel(this);
	m_doorsPanel->SetToolTip("Door/window pieces for this segment. Usually only horizontal/vertical segments carry doors.\n"
		"Drag an item from the palette and drop it here to add it with the type selected below.");
	m_doorsPanel->SetDropTarget(newd WallDoorsDropTarget(m_doorsPanel));
	rightCol->Add(m_doorsPanel, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

	// Door ID + type + open row
	wxBoxSizer* doorRow = newd wxBoxSizer(wxHORIZONTAL);
	doorRow->Add(newd wxStaticText(this, wxID_ANY, "Door ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_doorIdCtrl = newd wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 65535);
	m_doorIdCtrl->SetToolTip("ID of the door/window item.");
	doorRow->Add(m_doorIdCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	doorRow->Add(newd wxStaticText(this, wxID_ANY, "Type:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	wxArrayString doorTypeChoices;
	for (int i = 0; i < DOOR_TYPE_COUNT; ++i) {
		doorTypeChoices.Add(DOOR_TYPES[i]);
	}
	m_doorTypeCtrl = newd wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, doorTypeChoices);
	m_doorTypeCtrl->SetSelection(0);
	m_doorTypeCtrl->SetToolTip("Door type. 'window' / 'hatch_window' are window pieces (no open state).");
	doorRow->Add(m_doorTypeCtrl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	m_doorOpenCheck = newd wxCheckBox(this, wxID_ANY, "Open");
	m_doorOpenCheck->SetToolTip("Whether this door item is the open variant.");
	doorRow->Add(m_doorOpenCheck, 0, wxALIGN_CENTER_VERTICAL);
	rightCol->Add(doorRow, 0, wxEXPAND | wxALL, 6);

	// Door buttons row
	wxBoxSizer* doorBtnRow = newd wxBoxSizer(wxHORIZONTAL);
	wxButton* browseDoorButton = newd wxButton(this, ID_WALL_BROWSE_DOOR, "Browse...", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	browseDoorButton->SetToolTip("Browse for a door item.");
	doorBtnRow->Add(browseDoorButton, 0, wxRIGHT, 4);
	wxButton* addDoorButton = newd wxButton(this, ID_WALL_ADD_DOOR, "+ Add Door", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	addDoorButton->SetToolTip("Add the door/window to the selected segment.");
	doorBtnRow->Add(addDoorButton, 0);
	rightCol->Add(doorBtnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	// ▸ TILESET (publish)
	AddSectionHeader(rightCol, this, "Tileset (publish)");

	wxBoxSizer* tilesetRow = newd wxBoxSizer(wxHORIZONTAL);
	tilesetRow->Add(newd wxStaticText(this, wxID_ANY, "Tileset:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_tilesetCombo = newd wxComboBox(this, ID_WALL_TILESET_COMBO, "", wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_DROPDOWN);
	m_tilesetCombo->SetToolTip("Pick a tileset. Wall brushes are published into the tileset's Terrain section.");
	tilesetRow->Add(m_tilesetCombo, 1, wxALIGN_CENTER_VERTICAL);
	rightCol->Add(tilesetRow, 0, wxEXPAND | wxALL, 6);

	rightCol->Add(newd wxStaticText(this, wxID_ANY, "Existing brushes (Terrain):"), 0, wxLEFT | wxRIGHT, 6);
	m_tilesetBrushList = newd wxListBox(this, ID_WALL_TILESET_BRUSH_LIST, wxDefaultPosition, wxSize(-1, 80));
	m_tilesetBrushList->SetToolTip("Select a brush to use as reference for the 'After selected' insert option.");
	rightCol->Add(m_tilesetBrushList, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

	wxString positions[] = { "At start", "After selected", "At end" };
	m_tilesetInsertPosition = newd wxRadioBox(this, wxID_ANY, "Insert position",
		wxDefaultPosition, wxDefaultSize, 3, positions, 1, wxRA_SPECIFY_ROWS);
	m_tilesetInsertPosition->SetSelection(2); // default: At end
	rightCol->Add(m_tilesetInsertPosition, 0, wxEXPAND | wxALL, 6);

	m_addToTilesetButton = newd wxButton(this, ID_WALL_ADD_TO_TILESET, "+ Add brush to Tileset");
	m_addToTilesetButton->SetToolTip("Adds the current wall brush to the selected tileset's <terrain> section at the chosen position.");
	rightCol->Add(m_addToTilesetButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	colsRow->Add(rightCol, 40, wxEXPAND);

	topSizer->Add(colsRow, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	// --- Common Properties (single compact row) ---
	wxBoxSizer* commonRow = newd wxBoxSizer(wxHORIZONTAL);
	commonRow->Add(newd wxStaticText(this, wxID_ANY, "Name:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_nameCtrl = newd wxTextCtrl(this, wxID_ANY);
	m_nameCtrl->SetToolTip("Unique name for the wall brush.");
	commonRow->Add(m_nameCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

	m_draggableCheck = newd wxCheckBox(this, wxID_ANY, "Draggable");
	m_draggableCheck->SetToolTip("Allow click-drag painting of this wall.");
	commonRow->Add(m_draggableCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	m_onBlockingCheck = newd wxCheckBox(this, wxID_ANY, "On blocking");
	m_onBlockingCheck->SetToolTip("Whether the wall sits on a blocking tile (on_blocking).");
	commonRow->Add(m_onBlockingCheck, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

	commonRow->Add(newd wxStaticText(this, wxID_ANY, "Thickness:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_thicknessCtrl = newd wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1));
	m_thicknessCtrl->SetToolTip("Optional thickness, e.g. 100/100. Leave empty to omit.");
	commonRow->Add(m_thicknessCtrl, 0, wxALIGN_CENTER_VERTICAL);

	topSizer->Add(commonRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

	// Separator above the action bar
	topSizer->Add(newd wxStaticLine(this), 0, wxEXPAND | wxTOP | wxBOTTOM, 6);

	// --- Action bar — Clear (left) + Save (primary green, right) ---
	wxBoxSizer* actionBar = newd wxBoxSizer(wxHORIZONTAL);

	m_clearButton = newd wxButton(this, wxID_CLEAR, wxT("✕ Clear"));
	m_clearButton->SetToolTip("Clear the current form.");
	actionBar->Add(m_clearButton, 0, wxALIGN_CENTER_VERTICAL);

	actionBar->AddStretchSpacer(1);

	m_saveButton = newd wxButton(this, wxID_SAVE, wxT("✓ Save Wall"));
	m_saveButton->SetBackgroundColour(Theme::Get(Theme::Role::Success));
	m_saveButton->SetForegroundColour(Theme::Get(Theme::Role::TextOnAccent));
	wxFont saveFont = m_saveButton->GetFont();
	saveFont.SetWeight(wxFONTWEIGHT_BOLD);
	m_saveButton->SetFont(saveFont);
	m_saveButton->SetToolTip("Save this wall brush to walls.xml.");
	actionBar->Add(m_saveButton, 0, wxALIGN_CENTER_VERTICAL);

	topSizer->Add(actionBar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

	SetSizer(topSizer);
	Layout();
}

// ============================================================================
// Load existing walls
// ============================================================================

void WallBrushEditorDialog::LoadExistingWalls() {
	m_existingWallsCombo->Freeze();
	m_existingWallsCombo->Clear();
	m_existingWallsCombo->Append("<Create New>");
	m_existingWallsCombo->SetSelection(0);
	m_wallLookIds.clear();
	m_wallSourceFiles.clear();

	auto cleanup = [&]() { m_existingWallsCombo->Thaw(); };

	wxArrayString files = GetWallMaterialFiles();
	if (files.IsEmpty()) { cleanup(); return; }

	wxArrayString names;
	for (const wxString& file : files) {
		pugi::xml_document doc;
		if (!doc.load_file(file.ToStdString().c_str())) continue;

		pugi::xml_node materials = doc.child("materials");
		if (!materials) continue;

		for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
			pugi::xml_attribute typeAttr = brushNode.attribute("type");
			pugi::xml_attribute nameAttr = brushNode.attribute("name");
			if (!typeAttr || std::string(typeAttr.as_string()) != "wall") continue;
			if (!nameAttr) continue;

			wxString name(nameAttr.as_string());
			wxString key = name.Lower();
			// First file wins if the same wall name appears more than once.
			if (m_wallSourceFiles.find(key) != m_wallSourceFiles.end()) continue;
			names.Add(name);
			m_wallSourceFiles[key] = file;

			uint16_t lookId = 0;
			if (pugi::xml_attribute lookAttr = brushNode.attribute("server_lookid")) {
				lookId = static_cast<uint16_t>(lookAttr.as_uint());
			} else if (pugi::xml_attribute lookAttr2 = brushNode.attribute("lookid")) {
				lookId = static_cast<uint16_t>(lookAttr2.as_uint());
			}
			m_wallLookIds[key] = lookId;
		}
	}

	for (size_t i = 0; i < names.GetCount(); ++i) {
		m_existingWallsCombo->Append(names[i]);
	}
	m_existingWallsCombo->AutoComplete(names);

	cleanup();
}

uint16_t WallBrushEditorDialog::LookupWallLookId(const wxString& name) const {
	wxString trimmed = name;
	trimmed.Trim(true).Trim(false);
	if (trimmed.IsEmpty() || trimmed == "<Create New>") return 0;

	auto it = m_wallLookIds.find(trimmed.Lower());
	return it != m_wallLookIds.end() ? it->second : 0;
}

void WallBrushEditorDialog::OnLoadTextChanged(wxCommandEvent& WXUNUSED(event)) {
	if (!m_loadPreview || !m_existingWallsCombo) return;
	m_loadPreview->SetItemId(LookupWallLookId(m_existingWallsCombo->GetValue()));
}

void WallBrushEditorDialog::OnLoadWall(wxCommandEvent& WXUNUSED(event)) {
	wxString wanted;
	int selection = m_existingWallsCombo->GetSelection();
	if (selection > 0) {
		wanted = m_existingWallsCombo->GetString(selection);
	} else {
		wanted = m_existingWallsCombo->GetValue().Trim(true).Trim(false);
	}

	if (wanted.IsEmpty() || wanted == "<Create New>") {
		ClearAll();
		return;
	}

	LoadWallByName(wanted);
}

bool WallBrushEditorDialog::LoadWallByName(const wxString& name) {
	wxArrayString files = GetWallMaterialFiles();
	if (files.IsEmpty()) return false;

	// Look in the file we already know holds this brush first (if any).
	auto srcIt = m_wallSourceFiles.find(name.Lower());
	if (srcIt != m_wallSourceFiles.end() && files.Index(srcIt->second) != wxNOT_FOUND) {
		files.Remove(srcIt->second);
		files.Insert(srcIt->second, 0);
	}

	// `doc` is reused across files and must outlive `brushNode` (a handle into it),
	// so it lives at function scope; we stop reloading it once we find the brush.
	pugi::xml_document doc;
	pugi::xml_node brushNode;
	for (const wxString& file : files) {
		if (!doc.load_file(file.ToStdString().c_str())) continue;
		pugi::xml_node materials = doc.child("materials");
		if (!materials) continue;

		for (pugi::xml_node node = materials.child("brush"); node; node = node.next_sibling("brush")) {
			pugi::xml_attribute typeAttr = node.attribute("type");
			pugi::xml_attribute nameAttr = node.attribute("name");
			if (!typeAttr || std::string(typeAttr.as_string()) != "wall") continue;
			if (nameAttr && wxString(nameAttr.as_string()) == name) {
				brushNode = node;
				break;
			}
		}
		if (brushNode) {
			m_wallSourceFiles[name.Lower()] = file;
			break;
		}
	}

	if (!brushNode) {
		wxMessageBox("Could not find a wall brush named '" + name + "' in the material files.", "Not Found", wxICON_INFORMATION);
		return false;
	}

	// Reset working data, then fill from the node.
	for (int i = 0; i < WALL_SEG_COUNT; ++i) {
		m_items[i].clear();
		m_doors[i].clear();
	}
	m_preservedWallNodes.clear();
	m_preservedBrushAttrs.clear();
	m_preservedForName = name;

	// Preserve brush-level attributes we don't manage (e.g. activated="true").
	for (pugi::xml_attribute attr = brushNode.first_attribute(); attr; attr = attr.next_attribute()) {
		if (!IsManagedWallBrushAttr(attr.name())) {
			m_preservedBrushAttrs.emplace_back(attr.name(), attr.value());
		}
	}

	m_nameCtrl->SetValue(name);

	uint16_t lookId = 0;
	if (pugi::xml_attribute lookAttr = brushNode.attribute("server_lookid")) {
		lookId = static_cast<uint16_t>(lookAttr.as_uint());
	} else if (pugi::xml_attribute lookAttr2 = brushNode.attribute("lookid")) {
		lookId = static_cast<uint16_t>(lookAttr2.as_uint());
	}
	m_serverLookIdCtrl->SetValue(lookId);

	m_draggableCheck->SetValue(brushNode.attribute("draggable").as_bool());
	// on_blocking defaults to true in the engine; reflect the stored value if present.
	m_onBlockingCheck->SetValue(brushNode.attribute("on_blocking") ? brushNode.attribute("on_blocking").as_bool() : true);
	m_thicknessCtrl->SetValue(brushNode.attribute("thickness").as_string());

	for (pugi::xml_node wallNode = brushNode.child("wall"); wallNode; wallNode = wallNode.next_sibling("wall")) {
		std::string typeStr = wallNode.attribute("type").as_string();
		WallSegmentType seg = wallSegmentFromString(typeStr);
		if (seg == WALL_SEG_COUNT) {
			// A segment type the editor does not model ("untouchable", or anything a
			// newer data set introduces). Keep it verbatim so saving re-emits it
			// instead of silently dropping it.
			m_preservedWallNodes.push_back(SerializeXmlNode(wallNode));
			continue;
		}

		for (pugi::xml_node child = wallNode.first_child(); child; child = child.next_sibling()) {
			std::string childName = child.name();
			if (childName == "item") {
				uint16_t id = static_cast<uint16_t>(child.attribute("id").as_uint());
				if (id == 0) continue;
				int chance = child.attribute("chance") ? child.attribute("chance").as_int() : 100;
				m_items[seg].push_back(WallItemEntry(id, chance));
			} else if (childName == "door") {
				uint16_t id = static_cast<uint16_t>(child.attribute("id").as_uint());
				if (id == 0) continue;
				std::string dtype = child.attribute("type").as_string();
				if (dtype.empty()) dtype = "normal";
				bool open = child.attribute("open").as_bool();
				m_doors[seg].push_back(WallDoorEntry(id, dtype, open));
			}
		}
	}

	if (m_segmentGrid) m_segmentGrid->SetSelected(WALL_SEG_HORIZONTAL);
	RefreshSegmentPanels();

	if (m_loadPreview) m_loadPreview->SetItemId(lookId);
	return true;
}

// Locates the wall brush (by name) and the segment that uses `itemId` (as an item
// or door). Returns true and fills outName/outSeg on success.
bool WallBrushEditorDialog::FindWallByItemId(uint16_t itemId, wxString& outName, WallSegmentType& outSeg) {
	if (itemId == 0) return false;

	wxArrayString files = GetWallMaterialFiles();
	if (files.IsEmpty()) return false;

	for (const wxString& file : files) {
	pugi::xml_document doc;
	if (!doc.load_file(file.ToStdString().c_str())) continue;
	pugi::xml_node materials = doc.child("materials");
	if (!materials) continue;

	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute typeAttr = brushNode.attribute("type");
		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (!typeAttr || std::string(typeAttr.as_string()) != "wall" || !nameAttr) continue;

		for (pugi::xml_node wallNode = brushNode.child("wall"); wallNode; wallNode = wallNode.next_sibling("wall")) {
			WallSegmentType seg = wallSegmentFromString(wallNode.attribute("type").as_string());
			for (pugi::xml_node child = wallNode.first_child(); child; child = child.next_sibling()) {
				std::string cn = child.name();
				if ((cn == "item" || cn == "door") && static_cast<uint16_t>(child.attribute("id").as_uint()) == itemId) {
					outName = wxString(nameAttr.as_string());
					outSeg = (seg == WALL_SEG_COUNT) ? WALL_SEG_HORIZONTAL : seg;
					m_wallSourceFiles[outName.Lower()] = file;
					return true;
				}
			}
		}
	}
	}

	return false;
}

bool WallBrushEditorDialog::OpenItemInEditor(uint16_t itemId) {
	wxString name;
	WallSegmentType seg = WALL_SEG_HORIZONTAL;
	if (!FindWallByItemId(itemId, name, seg)) {
		return false;
	}

	m_existingWallsCombo->SetValue(name);
	if (m_findItemIdCtrl) m_findItemIdCtrl->SetValue(itemId);
	if (!LoadWallByName(name)) {
		return false;
	}

	// LoadWallByName resets the segment to horizontal; jump to the one that holds the item.
	SelectSegment(seg);
	return true;
}

void WallBrushEditorDialog::OnFindByItemId(wxCommandEvent& WXUNUSED(event)) {
	uint16_t wanted = static_cast<uint16_t>(m_findItemIdCtrl->GetValue());
	if (wanted == 0) {
		wxMessageBox("Enter an item ID to search for.", "Error", wxICON_ERROR);
		return;
	}

	if (!OpenItemInEditor(wanted)) {
		wxMessageBox(wxString::Format("No wall brush uses item ID %u.", static_cast<unsigned>(wanted)), "Not Found", wxICON_INFORMATION);
	}
}

// ============================================================================
// Segment handling
// ============================================================================

WallSegmentType WallBrushEditorDialog::CurrentSegment() const {
	return m_segmentGrid ? m_segmentGrid->GetSelected() : WALL_SEG_HORIZONTAL;
}

void WallBrushEditorDialog::SelectSegment(WallSegmentType seg) {
	if (seg >= WALL_SEG_COUNT) return;
	if (m_segmentGrid) m_segmentGrid->SetSelected(seg);
	RefreshSegmentPanels();
}

void WallBrushEditorDialog::RefreshSegmentPanels() {
	WallSegmentType seg = CurrentSegment();
	if (m_itemsPanel) m_itemsPanel->SetItems(m_items[seg]);
	if (m_doorsPanel) m_doorsPanel->SetItems(m_doors[seg]);

	// Keep the grid cells in sync: each shows the first item of its segment plus a
	// count badge, so the whole brush is readable at a glance.
	if (m_segmentGrid) {
		for (int s = 0; s < WALL_SEG_COUNT; ++s) {
			const uint16_t firstItem = m_items[s].empty() ? 0 : m_items[s].front().itemId;
			m_segmentGrid->SetSegmentPreview(static_cast<WallSegmentType>(s), firstItem,
				static_cast<int>(m_items[s].size()), static_cast<int>(m_doors[s].size()));
		}
	}

	const wxString label(wallSegmentLabel(seg));
	if (m_segmentCaption) m_segmentCaption->SetLabel("Items for " + label + ":");
	if (m_doorsCaption) m_doorsCaption->SetLabel("Doors for " + label + ":");
	Layout();
}

// ============================================================================
// Items
// ============================================================================

void WallBrushEditorDialog::OnAddItem(wxCommandEvent& WXUNUSED(event)) {
	uint16_t itemId = static_cast<uint16_t>(m_itemIdCtrl->GetValue());
	if (itemId == 0) {
		wxMessageBox("Please enter a valid item ID.", "Error", wxICON_ERROR);
		return;
	}
	int chance = m_itemChanceCtrl->GetValue();
	m_items[CurrentSegment()].push_back(WallItemEntry(itemId, chance));
	RefreshSegmentPanels();
}

void WallBrushEditorDialog::AddItemById(uint16_t itemId) {
	if (itemId == 0) return;

	// Mirror the drop into the Item ID spin so the follow-up buttons (Set Chance,
	// + Add) act on what was just dropped.
	if (m_itemIdCtrl) m_itemIdCtrl->SetValue(itemId);

	int chance = m_itemChanceCtrl ? m_itemChanceCtrl->GetValue() : 100;
	m_items[CurrentSegment()].push_back(WallItemEntry(itemId, chance));
	RefreshSegmentPanels();
}

void WallBrushEditorDialog::OnUpdateChance(wxCommandEvent& WXUNUSED(event)) {
	int selection = m_itemsPanel ? m_itemsPanel->GetSelectedIndex() : -1;
	std::vector<WallItemEntry>& items = m_items[CurrentSegment()];
	if (selection < 0 || selection >= static_cast<int>(items.size())) {
		wxMessageBox("Select an item first to update its chance.", "Error", wxICON_ERROR);
		return;
	}
	items[selection].chance = m_itemChanceCtrl->GetValue();
	RefreshSegmentPanels();
}

void WallBrushEditorDialog::OnBrowseItem(wxCommandEvent& WXUNUSED(event)) {
	FindItemDialog dialog(this, "Select Wall Item");
	if (dialog.ShowModal() == wxID_OK) {
		uint16_t itemId = dialog.getResultID();
		if (m_itemIdCtrl && itemId > 0) {
			m_itemIdCtrl->SetValue(itemId);
		}
	}
}

void WallBrushEditorDialog::RemoveItemAt(int index) {
	std::vector<WallItemEntry>& items = m_items[CurrentSegment()];
	if (index < 0 || index >= static_cast<int>(items.size())) return;
	items.erase(items.begin() + index);
	RefreshSegmentPanels();
}

void WallBrushEditorDialog::SelectItemChance(int index) {
	const std::vector<WallItemEntry>& items = m_items[CurrentSegment()];
	if (index < 0 || index >= static_cast<int>(items.size())) return;
	if (m_itemChanceCtrl) m_itemChanceCtrl->SetValue(items[index].chance);
}

// ============================================================================
// Doors
// ============================================================================

void WallBrushEditorDialog::OnAddDoor(wxCommandEvent& WXUNUSED(event)) {
	uint16_t doorId = static_cast<uint16_t>(m_doorIdCtrl->GetValue());
	if (doorId == 0) {
		wxMessageBox("Please enter a valid door item ID.", "Error", wxICON_ERROR);
		return;
	}
	int typeSel = m_doorTypeCtrl->GetSelection();
	if (typeSel < 0) typeSel = 0;
	std::string type = DOOR_TYPES[typeSel];
	bool open = m_doorOpenCheck->GetValue();

	m_doors[CurrentSegment()].push_back(WallDoorEntry(doorId, type, open));
	RefreshSegmentPanels();
}

void WallBrushEditorDialog::AddDoorById(uint16_t itemId) {
	if (itemId == 0) return;

	if (m_doorIdCtrl) m_doorIdCtrl->SetValue(itemId);

	int typeSel = m_doorTypeCtrl ? m_doorTypeCtrl->GetSelection() : 0;
	if (typeSel < 0 || typeSel >= DOOR_TYPE_COUNT) typeSel = 0;
	bool open = m_doorOpenCheck && m_doorOpenCheck->GetValue();

	m_doors[CurrentSegment()].push_back(WallDoorEntry(itemId, DOOR_TYPES[typeSel], open));
	RefreshSegmentPanels();
}

void WallBrushEditorDialog::OnBrowseDoor(wxCommandEvent& WXUNUSED(event)) {
	FindItemDialog dialog(this, "Select Door Item");
	if (dialog.ShowModal() == wxID_OK) {
		uint16_t itemId = dialog.getResultID();
		if (m_doorIdCtrl && itemId > 0) {
			m_doorIdCtrl->SetValue(itemId);
		}
	}
}

void WallBrushEditorDialog::RemoveDoorAt(int index) {
	std::vector<WallDoorEntry>& doors = m_doors[CurrentSegment()];
	if (index < 0 || index >= static_cast<int>(doors.size())) return;
	doors.erase(doors.begin() + index);
	RefreshSegmentPanels();
}

// ============================================================================
// Scan
// ============================================================================

void WallBrushEditorDialog::OnScanWall(wxCommandEvent& WXUNUSED(event)) {
	// Re-check: sprites may have been unloaded after the button was created.
	if (g_gui.gfx.isUnloaded()) {
		wxMessageBox("Requires a loaded client (sprites).", "Wall Scan", wxICON_WARNING);
		return;
	}

	WallScanDialog dlg(this);
	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	// APPEND semantics: a segment holds several item variants, so scanned items are
	// added to what is already there. Items the segment already lists are skipped so
	// re-running the scan never duplicates them.
	int added = 0;
	int skipped = 0;
	for (const auto& [seg, itemIds] : dlg.GetSegmentAssignments()) {
		if (seg >= WALL_SEG_COUNT) continue;

		std::vector<WallItemEntry>& target = m_items[seg];
		for (uint16_t itemId : itemIds) {
			const bool present = std::any_of(target.begin(), target.end(),
				[itemId](const WallItemEntry& entry) { return entry.itemId == itemId; });
			if (present) {
				++skipped;
				continue;
			}
			target.push_back(WallItemEntry(itemId, 100));
			++added;
		}
	}

	RefreshSegmentPanels();

	wxString message = wxString::Format("Added %d item(s) from the scan.", added);
	if (skipped > 0) {
		message += wxString::Format("\n%d item(s) were already in their segment and were skipped.", skipped);
	}
	if (added > 0) {
		message += "\n\nReview the chances, then click Save Wall to write the brush.";
	}
	wxMessageBox(message, "Wall Scan", wxICON_INFORMATION);
}

// ============================================================================
// Clear / Validate / Save
// ============================================================================

void WallBrushEditorDialog::OnClear(wxCommandEvent& WXUNUSED(event)) {
	ClearAll();
}

void WallBrushEditorDialog::ClearAll() {
	for (int i = 0; i < WALL_SEG_COUNT; ++i) {
		m_items[i].clear();
		m_doors[i].clear();
	}
	m_preservedWallNodes.clear();
	m_preservedBrushAttrs.clear();
	m_preservedForName.clear();
	m_nameCtrl->SetValue("");
	m_serverLookIdCtrl->SetValue(0);
	m_draggableCheck->SetValue(false);
	m_onBlockingCheck->SetValue(true);
	m_thicknessCtrl->SetValue("");
	m_existingWallsCombo->SetSelection(0);
	if (m_loadPreview) m_loadPreview->Clear();
	if (m_segmentGrid) {
		m_segmentGrid->ClearPreviews();
		m_segmentGrid->SetSelected(WALL_SEG_HORIZONTAL);
	}
	RefreshSegmentPanels();
}

bool WallBrushEditorDialog::Validate() {
	if (m_nameCtrl->GetValue().Trim(true).Trim(false).IsEmpty()) {
		wxMessageBox("Please enter a name for the wall brush.", "Validation Error", wxICON_ERROR);
		return false;
	}
	if (m_serverLookIdCtrl->GetValue() <= 0) {
		wxMessageBox("Please set a Look ID (server_lookid) for the wall brush.", "Validation Error", wxICON_ERROR);
		return false;
	}

	bool hasAnyItem = false;
	for (int i = 0; i < WALL_SEG_COUNT; ++i) {
		if (!m_items[i].empty()) {
			hasAnyItem = true;
			break;
		}
	}
	if (!hasAnyItem) {
		wxMessageBox("The wall brush must have at least one item in one of its segments.", "Validation Error", wxICON_ERROR);
		return false;
	}
	return true;
}

void WallBrushEditorDialog::OnSave(wxCommandEvent& WXUNUSED(event)) {
	SaveWall();
}

void WallBrushEditorDialog::SaveWall() {
	if (!Validate()) return;

	wxString name = m_nameCtrl->GetValue().Trim(true).Trim(false);
	int serverLookId = m_serverLookIdCtrl->GetValue();

	wxString dataDir = GetVersionDataDirectory();

	// Save back to the file the brush was loaded from (e.g. doodads.xml) so we edit it
	// in place instead of duplicating it into walls.xml. New brushes default to walls.xml.
	auto srcIt = m_wallSourceFiles.find(name.Lower());
	wxString targetFile = (srcIt != m_wallSourceFiles.end()) ? srcIt->second : (dataDir + "walls.xml");
	// Every candidate path is built as dataDir + <file>, so the bare file name is just
	// the tail past dataDir (used only for user-facing messages).
	wxString targetName = targetFile.StartsWith(dataDir) ? targetFile.Mid(dataDir.Length()) : targetFile;

	if (!wxFileExists(targetFile)) {
		wxMessageBox("Cannot find " + targetName + " file in the data directory.", "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(targetFile.ToStdString().c_str());
	if (!result) {
		wxMessageBox("Failed to load " + targetName + ": " + wxString(result.description()), "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		wxMessageBox("Invalid " + targetName + " file: missing 'materials' node.", "Error", wxICON_ERROR);
		return;
	}

	// Find an existing wall brush with the same name.
	pugi::xml_node existingBrush;
	for (pugi::xml_node brushNode = materials.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute typeAttr = brushNode.attribute("type");
		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (typeAttr && std::string(typeAttr.as_string()) == "wall" && nameAttr && wxString(nameAttr.as_string()) == name) {
			existingBrush = brushNode;
			break;
		}
	}

	if (existingBrush) {
		if (wxMessageBox("A wall brush named '" + name + "' already exists. Do you want to overwrite it?",
				"Confirm Overwrite", wxYES_NO | wxICON_QUESTION) != wxYES) {
			return;
		}
		materials.remove_child(existingBrush);
	}

	// Preserved data only applies when we're saving the same brush we loaded it from.
	const bool reemitPreserved = !m_preservedForName.IsEmpty()
		&& m_preservedForName.Lower() == name.Lower();

	// Create the new brush node.
	pugi::xml_node brushNode = materials.append_child("brush");
	brushNode.append_attribute("name").set_value(name.ToStdString().c_str());
	brushNode.append_attribute("type").set_value("wall");
	brushNode.append_attribute("server_lookid").set_value(serverLookId);

	if (m_draggableCheck->GetValue()) {
		brushNode.append_attribute("draggable").set_value("true");
	}
	// Only emit on_blocking when the user unchecked it (engine defaults to true).
	if (!m_onBlockingCheck->GetValue()) {
		brushNode.append_attribute("on_blocking").set_value("false");
	}
	wxString thickness = m_thicknessCtrl->GetValue().Trim(true).Trim(false);
	if (!thickness.IsEmpty()) {
		brushNode.append_attribute("thickness").set_value(thickness.ToStdString().c_str());
	}

	// Re-emit brush attributes the editor doesn't manage (e.g. activated="true").
	if (reemitPreserved) {
		for (const auto& attr : m_preservedBrushAttrs) {
			brushNode.append_attribute(attr.first.c_str()).set_value(attr.second.c_str());
		}
	}

	// Write each segment that has any content, in canonical order.
	for (int s = 0; s < WALL_SEG_COUNT; ++s) {
		WallSegmentType seg = static_cast<WallSegmentType>(s);
		if (m_items[s].empty() && m_doors[s].empty()) continue;

		pugi::xml_node wallNode = brushNode.append_child("wall");
		wallNode.append_attribute("type").set_value(wallSegmentToString(seg));

		for (const WallItemEntry& item : m_items[s]) {
			pugi::xml_node itemNode = wallNode.append_child("item");
			itemNode.append_attribute("id").set_value(item.itemId);
			itemNode.append_attribute("chance").set_value(item.chance);
		}

		for (const WallDoorEntry& door : m_doors[s]) {
			pugi::xml_node doorNode = wallNode.append_child("door");
			doorNode.append_attribute("id").set_value(door.itemId);
			doorNode.append_attribute("type").set_value(door.type.c_str());
			if (DoorTypeUsesOpen(door.type)) {
				doorNode.append_attribute("open").set_value(door.open ? "true" : "false");
			}
		}
	}

	// Re-append the <wall> segments the editor doesn't model ("untouchable", unknown
	// types) exactly as they were loaded, so richer wall brushes survive a save.
	if (reemitPreserved) {
		for (const std::string& raw : m_preservedWallNodes) {
			pugi::xml_document fragment;
			// Older pugixml: load(const char_t*) is the string overload (later load_string).
			if (fragment.load(raw.c_str()) && fragment.first_child()) {
				brushNode.append_copy(fragment.first_child());
			}
		}
	}

	if (!doc.save_file(targetFile.ToStdString().c_str())) {
		wxMessageBox("Failed to save changes to " + targetName + ".", "Error", wxICON_ERROR);
		return;
	}

	// Remember where this brush now lives so a follow-up save stays in the same file.
	m_wallSourceFiles[name.Lower()] = targetFile;

	wxMessageBox("Wall brush saved successfully.\n"
		"Restart the editor (or reload the client) to see changes in the palette.",
		"Success", wxICON_INFORMATION);

	LoadExistingWalls();
	m_existingWallsCombo->SetValue(name);
}

// ============================================================================
// Tileset (publish) — wall brushes live under <terrain>, same as ground brushes
// ============================================================================

void WallBrushEditorDialog::LoadExistingTilesets() {
	if (!m_tilesetCombo) return;
	m_tilesetCombo->Clear();

	wxString dataDir = GetVersionDataDirectory();
	if (dataDir.IsEmpty()) return;

	wxString tilesetsFile = dataDir + "tilesets.xml";
	if (!wxFileExists(tilesetsFile)) return;

	pugi::xml_document doc;
	if (!doc.load_file(tilesetsFile.ToStdString().c_str())) return;

	pugi::xml_node materials = doc.child("materials");
	if (!materials) return;

	// Collapse duplicate tileset names; track whether any block already has <terrain>.
	std::map<std::string, bool> nameHasTerrain;
	std::vector<std::string> orderedNames;
	for (pugi::xml_node tilesetNode = materials.child("tileset"); tilesetNode; tilesetNode = tilesetNode.next_sibling("tileset")) {
		pugi::xml_attribute nameAttr = tilesetNode.attribute("name");
		if (!nameAttr) continue;
		std::string name = nameAttr.as_string();
		bool hasTerrain = static_cast<bool>(tilesetNode.child("terrain"));
		auto it = nameHasTerrain.find(name);
		if (it == nameHasTerrain.end()) {
			nameHasTerrain[name] = hasTerrain;
			orderedNames.push_back(name);
		} else if (hasTerrain) {
			it->second = true;
		}
	}

	for (const std::string& name : orderedNames) {
		wxString label = wxString(name);
		label += nameHasTerrain[name] ? " (Terrain)" : " (Terrain - new)";
		m_tilesetCombo->Append(label);
	}

	if (m_tilesetBrushList) m_tilesetBrushList->Clear();
}

void WallBrushEditorDialog::RefreshTilesetBrushList() {
	if (!m_tilesetBrushList) return;
	m_tilesetBrushList->Clear();

	wxString tilesetName = StripTerrainSuffix(m_tilesetCombo->GetValue());
	if (tilesetName.IsEmpty()) return;

	wxString dataDir = GetVersionDataDirectory();
	if (dataDir.IsEmpty()) return;
	wxString tilesetsFile = dataDir + "tilesets.xml";
	if (!wxFileExists(tilesetsFile)) return;

	pugi::xml_document doc;
	if (!doc.load_file(tilesetsFile.ToStdString().c_str())) return;
	pugi::xml_node materials = doc.child("materials");
	if (!materials) return;

	for (pugi::xml_node tilesetNode = materials.child("tileset"); tilesetNode; tilesetNode = tilesetNode.next_sibling("tileset")) {
		pugi::xml_attribute nameAttr = tilesetNode.attribute("name");
		if (!nameAttr || wxString(nameAttr.as_string()) != tilesetName) continue;
		pugi::xml_node terrain = tilesetNode.child("terrain");
		if (!terrain) continue;
		for (pugi::xml_node brushNode = terrain.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
			pugi::xml_attribute bn = brushNode.attribute("name");
			if (bn) m_tilesetBrushList->Append(wxString(bn.as_string()));
		}
	}
}

void WallBrushEditorDialog::OnTilesetSelectionChanged(wxCommandEvent& WXUNUSED(event)) {
	RefreshTilesetBrushList();
}

void WallBrushEditorDialog::OnAddToTileset(wxCommandEvent& WXUNUSED(event)) {
	wxString brushName = m_nameCtrl->GetValue().Trim(true).Trim(false);
	if (brushName.IsEmpty()) {
		wxMessageBox("Please enter a name for the wall brush first.", "Error", wxICON_ERROR);
		return;
	}

	wxString tilesetName = StripTerrainSuffix(m_tilesetCombo->GetValue());
	if (tilesetName.IsEmpty()) {
		wxMessageBox("Please select or type a tileset name.", "Error", wxICON_ERROR);
		return;
	}

	int insertMode = m_tilesetInsertPosition ? m_tilesetInsertPosition->GetSelection() : 2;
	wxString afterBrushName;
	if (insertMode == 1) { // After selected
		int sel = m_tilesetBrushList ? m_tilesetBrushList->GetSelection() : wxNOT_FOUND;
		if (sel == wxNOT_FOUND) {
			wxMessageBox("'After selected' was chosen but no brush is selected in the list.\n"
				"Pick a reference brush, or switch to 'At start' / 'At end'.",
				"Error", wxICON_ERROR);
			return;
		}
		afterBrushName = m_tilesetBrushList->GetString(sel);
	}

	wxString dataDir = GetVersionDataDirectory();
	wxString tilesetsFile = dataDir + "tilesets.xml";

	if (!wxFileExists(tilesetsFile)) {
		wxMessageBox("Cannot find tilesets.xml in the data directory.", "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(tilesetsFile.ToStdString().c_str());
	if (!result) {
		wxMessageBox("Failed to load tilesets.xml: " + wxString(result.description()), "Error", wxICON_ERROR);
		return;
	}

	pugi::xml_node materials = doc.child("materials");
	if (!materials) {
		wxMessageBox("Invalid tilesets.xml: missing 'materials' node.", "Error", wxICON_ERROR);
		return;
	}

	// Prefer a tileset block that already has <terrain>; otherwise the first block with the name.
	pugi::xml_node targetTileset;
	pugi::xml_node fallbackTileset;
	for (pugi::xml_node tilesetNode = materials.child("tileset"); tilesetNode; tilesetNode = tilesetNode.next_sibling("tileset")) {
		pugi::xml_attribute nameAttr = tilesetNode.attribute("name");
		if (!nameAttr || wxString(nameAttr.as_string()) != tilesetName) continue;
		if (!fallbackTileset) fallbackTileset = tilesetNode;
		if (tilesetNode.child("terrain")) {
			targetTileset = tilesetNode;
			break;
		}
	}
	if (!targetTileset) targetTileset = fallbackTileset;

	if (!targetTileset) {
		if (wxMessageBox("Tileset '" + tilesetName + "' does not exist. Create it?",
				"Create Tileset", wxYES_NO | wxICON_QUESTION) != wxYES) {
			return;
		}
		targetTileset = materials.append_child("tileset");
		targetTileset.append_attribute("name").set_value(tilesetName.ToStdString().c_str());
	}

	pugi::xml_node terrain = targetTileset.child("terrain");
	if (!terrain) {
		terrain = targetTileset.append_child("terrain");
	}

	// Check for duplicates.
	for (pugi::xml_node brushNode = terrain.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
		pugi::xml_attribute nameAttr = brushNode.attribute("name");
		if (nameAttr && wxString(nameAttr.as_string()) == brushName) {
			wxMessageBox("Brush '" + brushName + "' is already in tileset '" + tilesetName + "'.",
				"Already Exists", wxICON_INFORMATION);
			return;
		}
	}

	pugi::xml_node newBrush;
	if (insertMode == 0) { // At start
		newBrush = terrain.prepend_child("brush");
	} else if (insertMode == 1) { // After selected
		pugi::xml_node anchor;
		for (pugi::xml_node brushNode = terrain.child("brush"); brushNode; brushNode = brushNode.next_sibling("brush")) {
			pugi::xml_attribute nameAttr = brushNode.attribute("name");
			if (nameAttr && wxString(nameAttr.as_string()) == afterBrushName) {
				anchor = brushNode;
				break;
			}
		}
		if (anchor) {
			newBrush = terrain.insert_child_after("brush", anchor);
		} else {
			newBrush = terrain.append_child("brush");
		}
	} else { // At end (default)
		newBrush = terrain.append_child("brush");
	}
	newBrush.append_attribute("name").set_value(brushName.ToStdString().c_str());

	if (!doc.save_file(tilesetsFile.ToStdString().c_str())) {
		wxMessageBox("Failed to save tilesets.xml.", "Error", wxICON_ERROR);
		return;
	}

	wxMessageBox("Brush '" + brushName + "' added to tileset '" + tilesetName + "'.\n"
		"Restart the editor (or reload the client) to see it in the palette.",
		"Success", wxICON_INFORMATION);

	LoadExistingTilesets();
	m_tilesetCombo->SetValue(tilesetName + " (Terrain)");
	RefreshTilesetBrushList();
}

// ============================================================================
// WallSegmentGridPanel
// ============================================================================

namespace {

// Where each segment sits in the 4x4 grid. The left 3x3 block is the junction cross
// exactly as the pieces connect on the map, the last column holds the straights and
// the pole, and the last row the four ends.
constexpr WallSegmentType SEGMENT_LAYOUT[4][4] = {
	{ WALL_SEG_SE_DIAGONAL, WALL_SEG_NORTH_T,      WALL_SEG_SW_DIAGONAL, WALL_SEG_HORIZONTAL },
	{ WALL_SEG_WEST_T,      WALL_SEG_INTERSECTION, WALL_SEG_EAST_T,      WALL_SEG_VERTICAL },
	{ WALL_SEG_NE_DIAGONAL, WALL_SEG_SOUTH_T,      WALL_SEG_CORNER,      WALL_SEG_POLE },
	{ WALL_SEG_SOUTH_END,   WALL_SEG_NORTH_END,    WALL_SEG_EAST_END,    WALL_SEG_WEST_END },
};

} // namespace

WallSegmentGridPanel::WallSegmentGridPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition,
			wxSize(GRID_COLS * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN,
				   GRID_ROWS * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN),
			wxBORDER_NONE) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(GRID_COLS * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN,
					  GRID_ROWS * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN));
}

void WallSegmentGridPanel::SetSelected(WallSegmentType seg) {
	if (seg >= WALL_SEG_COUNT) return;
	m_selected = seg;
	Refresh();
}

void WallSegmentGridPanel::SetSegmentPreview(WallSegmentType seg, uint16_t itemId, int itemCount, int doorCount) {
	if (seg >= WALL_SEG_COUNT) return;
	m_cells[seg].itemId = itemId;
	m_cells[seg].itemCount = itemCount;
	m_cells[seg].doorCount = doorCount;
	Refresh();
}

void WallSegmentGridPanel::ClearPreviews() {
	for (int s = 0; s < WALL_SEG_COUNT; ++s) {
		m_cells[s] = CellInfo();
	}
	Refresh();
}

wxRect WallSegmentGridPanel::CellRectFor(WallSegmentType seg) const {
	for (int row = 0; row < GRID_ROWS; ++row) {
		for (int col = 0; col < GRID_COLS; ++col) {
			if (SEGMENT_LAYOUT[row][col] != seg) continue;
			return wxRect(CELL_MARGIN + col * (CELL_SIZE + CELL_MARGIN),
						  CELL_MARGIN + row * (CELL_SIZE + CELL_MARGIN),
						  CELL_SIZE, CELL_SIZE);
		}
	}
	return wxRect();
}

WallSegmentType WallSegmentGridPanel::HitTest(int x, int y) const {
	for (int row = 0; row < GRID_ROWS; ++row) {
		for (int col = 0; col < GRID_COLS; ++col) {
			const wxRect cell(CELL_MARGIN + col * (CELL_SIZE + CELL_MARGIN),
							  CELL_MARGIN + row * (CELL_SIZE + CELL_MARGIN),
							  CELL_SIZE, CELL_SIZE);
			if (cell.Contains(x, y)) {
				return SEGMENT_LAYOUT[row][col];
			}
		}
	}
	return WALL_SEG_COUNT;
}

// Draws the wall shape as a stub from the cell centre towards every connected side,
// which is what makes the grid readable before any sprite is assigned.
void WallSegmentGridPanel::DrawSchematic(wxDC& dc, const wxRect& cell, WallSegmentType seg, const wxColour& colour) const {
	constexpr int N = 1;
	constexpr int W = 2;
	constexpr int E = 4;
	constexpr int S = 8;

	const int connections = wallSegmentConnections(seg);
	const int cx = cell.x + cell.width / 2;
	const int cy = cell.y + cell.height / 2;
	const int thickness = 5;
	const int half = thickness / 2;
	const int inset = 7; // keep the stubs clear of the cell border

	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(colour));

	if (connections == 0) {
		// Pole: a lone stub, drawn slightly larger so it reads as "no connections".
		dc.DrawRectangle(cx - thickness, cy - thickness, thickness * 2, thickness * 2);
		return;
	}

	// Centre block, so three-way and four-way pieces join cleanly.
	dc.DrawRectangle(cx - half, cy - half, thickness, thickness);

	if (connections & N) {
		dc.DrawRectangle(cx - half, cell.y + inset, thickness, cy - cell.y - inset);
	}
	if (connections & S) {
		dc.DrawRectangle(cx - half, cy, thickness, cell.GetBottom() - inset - cy);
	}
	if (connections & W) {
		dc.DrawRectangle(cell.x + inset, cy - half, cx - cell.x - inset, thickness);
	}
	if (connections & E) {
		dc.DrawRectangle(cx, cy - half, cell.GetRight() - inset - cx, thickness);
	}
}

void WallSegmentGridPanel::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);

	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	const int SPRITE_PADDING = 7;
	const int spriteArea = CELL_SIZE - 2 * SPRITE_PADDING;

	for (int row = 0; row < GRID_ROWS; ++row) {
		for (int col = 0; col < GRID_COLS; ++col) {
			const WallSegmentType seg = SEGMENT_LAYOUT[row][col];
			const wxRect cell(CELL_MARGIN + col * (CELL_SIZE + CELL_MARGIN),
							  CELL_MARGIN + row * (CELL_SIZE + CELL_MARGIN),
							  CELL_SIZE, CELL_SIZE);
			const CellInfo& info = m_cells[seg];
			const bool selected = (seg == m_selected);
			const bool hovered = (seg == m_hovered);

			if (selected) {
				dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
				dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Selected)));
			} else if (hovered) {
				dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
				dc.SetBrush(wxBrush(Theme::Get(Theme::Role::CardBaseHover)));
			} else {
				dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
				dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
			}
			dc.DrawRoundedRectangle(cell, 3);

			// The schematic stays visible under the sprite so the grid keeps reading as
			// a map of shapes even on a fully authored brush.
			DrawSchematic(dc, cell, seg,
				info.itemCount > 0 ? Theme::Get(Theme::Role::Border) : Theme::Get(Theme::Role::TextSubtle));

			if (info.itemId > 0) {
				const auto itemDef = g_item_definitions.get(info.itemId);
				if (itemDef) {
					Sprite* sprite = g_gui.gfx.getSprite(itemDef.clientId());
					if (sprite) {
						sprite->DrawTo(&dc, SPRITE_SIZE_32x32,
							cell.x + SPRITE_PADDING, cell.y + SPRITE_PADDING,
							spriteArea, spriteArea);
					}
				}
			}

			// "xN" badge for segments with more than one variant.
			if (info.itemCount > 1) {
				dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
				const wxString badge = wxString::Format("x%d", info.itemCount);
				const wxSize badgeSize = dc.GetTextExtent(badge);
				const int badgeX = cell.GetRight() - badgeSize.GetWidth() - 3;
				const int badgeY = cell.y + 2;
				dc.SetPen(*wxTRANSPARENT_PEN);
				dc.SetBrush(wxBrush(wxColour(40, 40, 40, 200)));
				dc.DrawRoundedRectangle(badgeX - 2, badgeY, badgeSize.GetWidth() + 4, badgeSize.GetHeight() + 1, 2);
				dc.SetTextForeground(Theme::Get(Theme::Role::Accent));
				dc.DrawText(badge, badgeX, badgeY);
			}

			// Door marker, bottom-left.
			if (info.doorCount > 0) {
				dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
				const wxString marker = wxString::Format("D%d", info.doorCount);
				const wxSize markerSize = dc.GetTextExtent(marker);
				dc.SetTextForeground(Theme::Get(Theme::Role::Success));
				dc.DrawText(marker, cell.x + 3, cell.GetBottom() - markerSize.GetHeight() - 2);
			}
		}
	}
}

void WallSegmentGridPanel::OnMouseClick(wxMouseEvent& event) {
	const WallSegmentType seg = HitTest(event.GetX(), event.GetY());
	if (seg >= WALL_SEG_COUNT) return;

	wxWindow* parent = GetParent();
	while (parent && !dynamic_cast<WallBrushEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}
	if (WallBrushEditorDialog* dialog = dynamic_cast<WallBrushEditorDialog*>(parent)) {
		dialog->SelectSegment(seg); // repaints through RefreshSegmentPanels
	} else {
		SetSelected(seg);
	}
}

void WallSegmentGridPanel::OnMotion(wxMouseEvent& event) {
	const WallSegmentType seg = HitTest(event.GetX(), event.GetY());
	if (seg == m_hovered) return;

	m_hovered = seg;
	if (seg < WALL_SEG_COUNT) {
		wxString tip(wallSegmentLabel(seg));
		const CellInfo& info = m_cells[seg];
		if (info.itemCount > 0) {
			tip += wxString::Format("\n%d item(s)", info.itemCount);
		} else {
			tip += "\nempty";
		}
		if (info.doorCount > 0) {
			tip += wxString::Format(", %d door(s)", info.doorCount);
		}
		if (GetToolTipText() != tip) {
			SetToolTip(tip);
		}
	}
	Refresh();
}

void WallSegmentGridPanel::OnLeave(wxMouseEvent& WXUNUSED(event)) {
	if (m_hovered == WALL_SEG_COUNT) return;
	m_hovered = WALL_SEG_COUNT;
	Refresh();
}

// ============================================================================
// WallSegmentItemsPanel
// ============================================================================

WallSegmentItemsPanel::WallSegmentItemsPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxSize(-1, 3 * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN), wxBORDER_NONE) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	// Minimum of two rows only: the segment grid above takes real estate, and the sizer
	// hands this panel every pixel that is left over anyway.
	SetMinSize(wxSize(-1, 2 * (CELL_SIZE + CELL_MARGIN) + CELL_MARGIN));
}

void WallSegmentItemsPanel::SetItems(const std::vector<WallItemEntry>& items) {
	m_items = items;
	if (m_selectedIndex >= static_cast<int>(m_items.size())) {
		m_selectedIndex = -1;
	}
	RecalcLayout();
	Refresh();
}

void WallSegmentItemsPanel::Clear() {
	m_items.clear();
	m_cells.clear();
	m_selectedIndex = -1;
	Refresh();
}

void WallSegmentItemsPanel::RecalcLayout() {
	m_cells.clear();

	int clientWidth = GetClientSize().GetWidth();
	if (clientWidth <= 0) clientWidth = CELL_SIZE + 2 * CELL_MARGIN;

	int perRow = std::max(1, (clientWidth - CELL_MARGIN) / (CELL_SIZE + CELL_MARGIN));

	int x = CELL_MARGIN;
	int y = CELL_MARGIN;
	int col = 0;

	for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
		CellRect cell;
		cell.index = i;
		cell.bounds = wxRect(x, y, CELL_SIZE, CELL_SIZE);
		cell.closeBtn = wxRect(x + CELL_SIZE - CLOSE_BTN_SIZE - 2, y + 2, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
		m_cells.push_back(cell);

		col++;
		if (col >= perRow) {
			col = 0;
			x = CELL_MARGIN;
			y += CELL_SIZE + CELL_MARGIN;
		} else {
			x += CELL_SIZE + CELL_MARGIN;
		}
	}
}

void WallSegmentItemsPanel::OnSize(wxSizeEvent& event) {
	RecalcLayout();
	Refresh();
	event.Skip();
}

void WallSegmentItemsPanel::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);

	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	if (m_items.empty()) {
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
		dc.DrawText("No items. Add items for this segment below.", CELL_MARGIN, CELL_MARGIN + 4);
		return;
	}

	const int SPRITE_PADDING = 4;
	const int spriteArea = CELL_SIZE - 2 * SPRITE_PADDING;

	for (const auto& cell : m_cells) {
		const auto& item = m_items[cell.index];

		bool selected = (cell.index == m_selectedIndex);
		if (selected) {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Selected)));
		} else {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
		}
		dc.DrawRoundedRectangle(cell.bounds, 3);

		const auto itemDef = g_item_definitions.get(item.itemId);
		if (itemDef) {
			Sprite* sprite = g_gui.gfx.getSprite(itemDef.clientId());
			if (sprite) {
				sprite->DrawTo(&dc, SPRITE_SIZE_32x32,
					cell.bounds.x + SPRITE_PADDING,
					cell.bounds.y + SPRITE_PADDING,
					spriteArea, spriteArea);
			}
		}

		dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		wxString chanceLabel = wxString::Format("%d", item.chance);
		wxSize ts = dc.GetTextExtent(chanceLabel);
		dc.DrawText(chanceLabel,
			cell.bounds.x + (CELL_SIZE - ts.GetWidth()) / 2,
			cell.bounds.y + CELL_SIZE - ts.GetHeight() - 2);

		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(wxColour(180, 50, 50)));
		dc.DrawRoundedRectangle(cell.closeBtn, 2);

		dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
		dc.SetTextForeground(*wxWHITE);
		wxSize xSize = dc.GetTextExtent("X");
		dc.DrawText("X",
			cell.closeBtn.x + (cell.closeBtn.width - xSize.GetWidth()) / 2,
			cell.closeBtn.y + (cell.closeBtn.height - xSize.GetHeight()) / 2);
	}
}

void WallSegmentItemsPanel::OnMouseClick(wxMouseEvent& event) {
	int mx = event.GetX();
	int my = event.GetY();

	wxWindow* parent = GetParent();
	while (parent && !dynamic_cast<WallBrushEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}
	WallBrushEditorDialog* dialog = dynamic_cast<WallBrushEditorDialog*>(parent);

	for (const auto& cell : m_cells) {
		if (cell.closeBtn.Contains(mx, my)) {
			if (dialog) dialog->RemoveItemAt(cell.index);
			return;
		}
		if (cell.bounds.Contains(mx, my)) {
			m_selectedIndex = cell.index;
			if (dialog) dialog->SelectItemChance(cell.index);
			Refresh();
			return;
		}
	}

	m_selectedIndex = -1;
	Refresh();
}

// ============================================================================
// WallDoorsPanel
// ============================================================================

WallDoorsPanel::WallDoorsPanel(wxWindow* parent, wxWindowID id) :
	wxPanel(parent, id, wxDefaultPosition, wxSize(-1, 2 * (CELL_H + CELL_MARGIN) + CELL_MARGIN), wxBORDER_NONE) {
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(-1, 2 * (CELL_H + CELL_MARGIN) + CELL_MARGIN));
}

void WallDoorsPanel::SetItems(const std::vector<WallDoorEntry>& items) {
	m_items = items;
	if (m_selectedIndex >= static_cast<int>(m_items.size())) {
		m_selectedIndex = -1;
	}
	RecalcLayout();
	Refresh();
}

void WallDoorsPanel::Clear() {
	m_items.clear();
	m_cells.clear();
	m_selectedIndex = -1;
	Refresh();
}

void WallDoorsPanel::RecalcLayout() {
	m_cells.clear();

	int clientWidth = GetClientSize().GetWidth();
	if (clientWidth <= 0) clientWidth = CELL_W + 2 * CELL_MARGIN;

	int perRow = std::max(1, (clientWidth - CELL_MARGIN) / (CELL_W + CELL_MARGIN));

	int x = CELL_MARGIN;
	int y = CELL_MARGIN;
	int col = 0;

	for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
		CellRect cell;
		cell.index = i;
		cell.bounds = wxRect(x, y, CELL_W, CELL_H);
		cell.closeBtn = wxRect(x + CELL_W - CLOSE_BTN_SIZE - 2, y + 2, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE);
		m_cells.push_back(cell);

		col++;
		if (col >= perRow) {
			col = 0;
			x = CELL_MARGIN;
			y += CELL_H + CELL_MARGIN;
		} else {
			x += CELL_W + CELL_MARGIN;
		}
	}
}

void WallDoorsPanel::OnSize(wxSizeEvent& event) {
	RecalcLayout();
	Refresh();
	event.Skip();
}

void WallDoorsPanel::OnPaint(wxPaintEvent& WXUNUSED(event)) {
	wxAutoBufferedPaintDC dc(this);

	dc.SetBackground(wxBrush(Theme::Get(Theme::Role::Background)));
	dc.Clear();

	if (m_items.empty()) {
		dc.SetTextForeground(Theme::Get(Theme::Role::TextSubtle));
		dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_ITALIC, wxFONTWEIGHT_NORMAL));
		dc.DrawText("No doors. Optional — add doors/windows below.", CELL_MARGIN, CELL_MARGIN + 4);
		return;
	}

	const int SPRITE_PADDING = 4;
	const int spriteArea = CELL_W - 2 * SPRITE_PADDING;

	for (const auto& cell : m_cells) {
		const auto& door = m_items[cell.index];

		bool selected = (cell.index == m_selectedIndex);
		if (selected) {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Accent), 2));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Selected)));
		} else {
			dc.SetPen(wxPen(Theme::Get(Theme::Role::Border)));
			dc.SetBrush(wxBrush(Theme::Get(Theme::Role::Surface)));
		}
		dc.DrawRoundedRectangle(cell.bounds, 3);

		const auto itemDef = g_item_definitions.get(door.itemId);
		if (itemDef) {
			Sprite* sprite = g_gui.gfx.getSprite(itemDef.clientId());
			if (sprite) {
				sprite->DrawTo(&dc, SPRITE_SIZE_32x32,
					cell.bounds.x + SPRITE_PADDING,
					cell.bounds.y + SPRITE_PADDING,
					spriteArea, spriteArea);
			}
		}

		// Type label
		dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
		dc.SetTextForeground(Theme::Get(Theme::Role::Text));
		wxString typeLabel(door.type);
		wxSize ts = dc.GetTextExtent(typeLabel);
		// Truncate visually if too wide.
		dc.DrawText(typeLabel,
			cell.bounds.x + std::max(2, (CELL_W - ts.GetWidth()) / 2),
			cell.bounds.y + spriteArea + SPRITE_PADDING);

		// Open / closed badge (windows have no open state).
		if (DoorTypeUsesOpen(door.type)) {
			dc.SetFont(wxFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
			dc.SetTextForeground(door.open ? wxColour(60, 170, 60) : Theme::Get(Theme::Role::TextSubtle));
			wxString stateLabel = door.open ? "open" : "closed";
			wxSize ss = dc.GetTextExtent(stateLabel);
			dc.DrawText(stateLabel,
				cell.bounds.x + std::max(2, (CELL_W - ss.GetWidth()) / 2),
				cell.bounds.y + CELL_H - ss.GetHeight() - 2);
		}

		// Close button (X)
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(wxColour(180, 50, 50)));
		dc.DrawRoundedRectangle(cell.closeBtn, 2);

		dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
		dc.SetTextForeground(*wxWHITE);
		wxSize xSize = dc.GetTextExtent("X");
		dc.DrawText("X",
			cell.closeBtn.x + (cell.closeBtn.width - xSize.GetWidth()) / 2,
			cell.closeBtn.y + (cell.closeBtn.height - xSize.GetHeight()) / 2);
	}
}

void WallDoorsPanel::OnMouseClick(wxMouseEvent& event) {
	int mx = event.GetX();
	int my = event.GetY();

	wxWindow* parent = GetParent();
	while (parent && !dynamic_cast<WallBrushEditorDialog*>(parent)) {
		parent = parent->GetParent();
	}
	WallBrushEditorDialog* dialog = dynamic_cast<WallBrushEditorDialog*>(parent);

	for (const auto& cell : m_cells) {
		if (cell.closeBtn.Contains(mx, my)) {
			if (dialog) dialog->RemoveDoorAt(cell.index);
			return;
		}
		if (cell.bounds.Contains(mx, my)) {
			m_selectedIndex = cell.index;
			Refresh();
			return;
		}
	}

	m_selectedIndex = -1;
	Refresh();
}
