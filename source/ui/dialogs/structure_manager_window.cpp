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

#include "ui/dialogs/structure_manager_window.h"

#include <wx/dir.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/dcbuffer.h>
#include <wx/msgdlg.h>
#include <wx/popupwin.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>
#include <wx/textdlg.h>
#include <wx/treectrl.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "app/client_version.h"
#include "editor/copybuffer.h"
#include "editor/editor.h"
#include "io/filehandle.h"
#include "io/iomap_otbm.h"
#include "map/map.h"
#include "map/tile_operations.h"
#include "ui/gui.h"
#include "ui/main_menubar.h"
#include "ui/map_window.h"
#include "ui/theme.h"
#include "util/file_system.h"
#include "app/settings.h"
#include "rendering/ui/map_display.h"
#include "editor/managers/editor_manager.h"
#include "app/managers/version_manager.h"
#include "util/image_manager.h"

namespace {
	enum {
		ID_STRUCTURE_LIST = wxID_HIGHEST + 370,
		ID_SAVE_STRUCTURE,
		ID_PASTE_STRUCTURE,
		ID_PASTE_ROTATION,
		ID_DELETE_STRUCTURE,
		ID_RENAME_STRUCTURE,
		ID_MOVE_STRUCTURE_UP,
		ID_MOVE_STRUCTURE_DOWN,
		ID_RENAME_CATEGORY,
		ID_ADD_CATEGORY,
		ID_ADD_SUBCATEGORY,
		ID_REMOVE_CATEGORY,
		ID_REMOVE_SUBCATEGORY,
		ID_SEARCH_FILTER,
		ID_CATEGORY_TREE,
		ID_TUTORIAL_HELP
	};

	const int kTutorialWrapWidth = 360;
	const wxSize kTutorialMinSize(440, 150);
	const int kTutorialFontDelta = -1;
	const int kTutorialOverlayAlpha = 120;
	const int kTutorialHighlightAlpha = 40;

	class StructureIOMapOTBM : public IOMapOTBM {
	public:
		explicit StructureIOMapOTBM(MapVersion ver) : IOMapOTBM(ver) {}

		bool Save(Map& map, NodeFileWriteHandle& handle) {
			return IOMapOTBM::saveMap(map, handle);
		}

		bool Load(Map& map, NodeFileReadHandle& handle) {
			return IOMapOTBM::loadMap(map, handle);
		}
	};

	class CategoryItemData : public wxTreeItemData {
	public:
		explicit CategoryItemData(const wxString& path) : categoryPath(path) {}
		wxString categoryPath;
	};

	wxString GetStructuresDirectory() {
		wxFileName dir;
		dir.AssignDir(FileSystem::GetDataDirectory());
		dir.AppendDir("presets");
		if (!dir.DirExists()) {
			dir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
		}
		dir.AppendDir("structures");
		if (!dir.DirExists()) {
			dir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
		}
		return dir.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	}

	std::string SanitizeFilename(const std::string& name) {
		std::string sanitized = name;
		for (char& c : sanitized) {
			if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
				c = '_';
			}
		}
		return sanitized;
	}

	int NaturalCompareNoCase(const wxString& a, const wxString& b) {
		size_t ia = 0, ib = 0;
		while (ia < a.length() && ib < b.length()) {
			wxUniChar ca = a[ia];
			wxUniChar cb = b[ib];
			bool digitA = wxIsdigit(ca);
			bool digitB = wxIsdigit(cb);

			if (digitA && digitB) {
				size_t startA = ia, startB = ib;
				while (ia < a.length() && wxIsdigit(a[ia])) ++ia;
				while (ib < b.length() && wxIsdigit(b[ib])) ++ib;
				size_t lenA = ia - startA;
				size_t lenB = ib - startB;
				if (lenA != lenB)
					return lenA < lenB ? -1 : 1;
				for (size_t k = 0; k < lenA; ++k) {
					if (a[startA + k] != b[startB + k])
						return a[startA + k] < b[startB + k] ? -1 : 1;
				}
			} else {
				wxUniChar la = wxTolower(ca);
				wxUniChar lb = wxTolower(cb);
				if (la != lb)
					return la < lb ? -1 : 1;
				++ia;
				++ib;
			}
		}
		if (ia < a.length()) return 1;
		if (ib < b.length()) return -1;
		return 0;
	}

	bool BuildSelectionBuffer(Editor& editor, BaseMap& outMap, Position& outMinPos, Position& outMaxPos, int& outTiles, int& outItems) {
		if (!editor.hasSelection()) {
			return false;
		}

		outMap.clear(true);
		outTiles = 0;
		outItems = 0;
		bool hasPos = false;

		for (Tile* tile : editor.selection) {
			if (!tile) {
				continue;
			}

			++outTiles;

			TileLocation* location = outMap.createTileL(tile->getPosition());
			auto copiedTile = outMap.allocator(location);

			if (tile->ground && tile->ground->isSelected()) {
				copiedTile->house_id = tile->house_id;
				copiedTile->setMapFlags(tile->getMapFlags());
			}

			auto selectedItems = TileOperations::getSelectedItems(tile);
			for (Item* item : selectedItems) {
				++outItems;
				copiedTile->addItem(item->deepCopy());
			}

			if (tile->creature && tile->creature->isSelected()) {
				copiedTile->creature = tile->creature->deepCopy();
			}
			if (tile->spawn && tile->spawn->isSelected()) {
				copiedTile->spawn = tile->spawn->deepCopy();
			}

			(void)outMap.setTile(std::move(copiedTile));

			const Position& pos = tile->getPosition();
			if (!hasPos) {
				outMinPos = pos;
				outMaxPos = pos;
				hasPos = true;
			} else {
				outMinPos.x = std::min(outMinPos.x, pos.x);
				outMinPos.y = std::min(outMinPos.y, pos.y);
				outMinPos.z = std::min(outMinPos.z, pos.z);
				outMaxPos.x = std::max(outMaxPos.x, pos.x);
				outMaxPos.y = std::max(outMaxPos.y, pos.y);
				outMaxPos.z = std::max(outMaxPos.z, pos.z);
			}
		}

		return hasPos;
	}

	bool SaveStructureFile(BaseMap& source, const Position& minPos, const Position& maxPos, const MapVersion& version, const wxString& path, wxString& errorOut) {
		Map structureMap;
		structureMap.convert(version, false);

		int width = std::max(1, maxPos.x - minPos.x + 1);
		int height = std::max(1, maxPos.y - minPos.y + 1);
		structureMap.setWidth(width);
		structureMap.setHeight(height);

		for (auto it = source.begin(); it != source.end(); ++it) {
			Tile* tile = it->get();
			if (!tile || tile->size() == 0) {
				continue;
			}

			Position relPos = tile->getPosition() - minPos;
			auto copiedTile = TileOperations::deepCopy(tile, structureMap);
			(void)structureMap.setTile(relPos, std::move(copiedTile));
		}

		StructureIOMapOTBM saver(structureMap.getVersion());
		DiskNodeFileWriteHandle handle(nstr(path), "OTBM");
		if (!handle.isOk()) {
			errorOut = "Failed to open file for writing.";
			return false;
		}

		if (!saver.Save(structureMap, handle)) {
			errorOut = wxstr(saver.getError());
			return false;
		}

		return true;
	}

	bool LoadStructureFile(const wxString& path, BaseMap& outMap, Position& outCopyPos, wxString& errorOut) {
		outMap.clear(true);

		Map tempMap;
		StructureIOMapOTBM loader(tempMap.getVersion());
		std::vector<std::string> acceptedIds = {"OTBM"};
		DiskNodeFileReadHandle handle(nstr(path), acceptedIds);
		if (!handle.isOk()) {
			errorOut = "Failed to open file.";
			return false;
		}

		if (!loader.Load(tempMap, handle)) {
			errorOut = wxstr(loader.getError());
			return false;
		}

		bool hasPos = false;
		Position minPos;
		Position maxPos;

		for (auto it = tempMap.begin(); it != tempMap.end(); ++it) {
			Tile* tile = it->get();
			if (!tile || tile->size() == 0) {
				continue;
			}

			const Position& pos = tile->getPosition();
			if (!hasPos) {
				minPos = pos;
				maxPos = pos;
				hasPos = true;
			} else {
				minPos.x = std::min(minPos.x, pos.x);
				minPos.y = std::min(minPos.y, pos.y);
				minPos.z = std::min(minPos.z, pos.z);
				maxPos.x = std::max(maxPos.x, pos.x);
				maxPos.y = std::max(maxPos.y, pos.y);
				maxPos.z = std::max(maxPos.z, pos.z);
			}
		}

		if (!hasPos) {
			errorOut = "Structure is empty.";
			return false;
		}

		Map* outRealMap = dynamic_cast<Map*>(&outMap);
		if (outRealMap) {
			outRealMap->convert(tempMap.getVersion(), false);
		}

		for (auto it = tempMap.begin(); it != tempMap.end(); ++it) {
			Tile* tile = it->get();
			if (!tile || tile->size() == 0) {
				continue;
			}

			Position relPos = tile->getPosition() - minPos;
			auto copiedTile = TileOperations::deepCopy(tile, outMap);
			(void)outMap.setTile(relPos, std::move(copiedTile));
		}

		if (outRealMap) {
			const int width = std::max(1, maxPos.x - minPos.x + 1);
			const int height = std::max(1, maxPos.y - minPos.y + 1);
			outRealMap->setWidth(width);
			outRealMap->setHeight(height);
		}

		outCopyPos = Position(0, 0, maxPos.z - minPos.z);
		return true;
	}

	wxString BuildCategoryDirectory(const wxString& baseDir, const wxString& categoryPath) {
		wxFileName dirName;
		dirName.AssignDir(baseDir);
		if (!categoryPath.empty()) {
			wxStringTokenizer tokenizer(categoryPath, "/");
			while (tokenizer.HasMoreTokens()) {
				wxString token = tokenizer.GetNextToken();
				if (!token.empty()) {
					dirName.AppendDir(token);
				}
			}
		}
		return dirName.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
	}

	bool CategoryNameExists(const std::vector<wxString>& paths, const wxString& name, const wxString& excludePath = wxString()) {
		for (const auto& path : paths) {
			if (path.empty()) {
				continue;
			}
			if (!excludePath.empty() && path.CmpNoCase(excludePath) == 0) {
				continue;
			}

			wxString leaf = path;
			int sep = leaf.Find('/', true);
			if (sep != wxNOT_FOUND) {
				leaf = leaf.Mid(sep + 1);
			}
			if (leaf.CmpNoCase(name) == 0) {
				return true;
			}
		}
		return false;
	}
} // namespace

// ---------------------------------------------------------------------------
// StructurePreviewPanel — embedded map preview for structures
// ---------------------------------------------------------------------------
class StructureManagerDialog::StructurePreviewPanel : public wxPanel {
public:
	explicit StructurePreviewPanel(wxWindow* parent) :
		wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE),
		m_mapWindow(nullptr),
		m_canvas(nullptr),
		m_emptyLabel(nullptr),
		m_hasMap(false),
		m_dragging(false) {
		SetMinSize(wxSize(300, 240));

		auto* sizer = newd wxBoxSizer(wxVERTICAL);

		m_emptyLabel = newd wxStaticText(this, wxID_ANY, "");
		m_emptyLabel->SetForegroundColour(wxColour(140, 140, 150));
		m_emptyLabel->Hide();

		EnsureEditor();

		m_mapWindow = newd MapWindow(this, *m_editor);
		m_canvas = m_mapWindow->GetCanvas();
		if (m_canvas) {
			BindPreviewEvents();
		}

		sizer->Add(m_mapWindow, 1, wxEXPAND);
		sizer->Add(m_emptyLabel, 0, wxALIGN_CENTER | wxALL, 8);
		SetSizer(sizer);

		ShowMessage("Select a structure to preview.");
	}

	void Clear(const wxString& message) {
		ShowMessage(message);
	}

	bool LoadStructure(const wxString& path, wxString& error) {
		EnsureEditor();
		if (!m_editor) {
			error = "Preview editor unavailable.";
			ShowMessage("Preview failed to load.");
			return false;
		}

		// Load into a temporary BaseMap first (positions start at 0,0)
		BaseMap tempMap;
		Position copyPos;
		if (!LoadStructureFile(path, tempMap, copyPos, error)) {
			ShowMessage("Preview failed to load.");
			return false;
		}

		// Reload into editor map with an offset so tiles have coordinates large enough
		// to avoid negative scroll values (the renderer offsets above-ground tiles)
		m_editor->map.clear(true);
		const int PREVIEW_OFFSET = 512; // large enough offset
		int maxX = 0, maxY = 0;

		for (auto it = tempMap.begin(); it != tempMap.end(); ++it) {
			Tile* tile = it->get();
			if (!tile || tile->size() == 0) {
				continue;
			}
			const Position& pos = tile->getPosition();
			Position newPos(pos.x + PREVIEW_OFFSET, pos.y + PREVIEW_OFFSET, pos.z);
			auto copiedTile = TileOperations::deepCopy(tile, m_editor->map);
			(void)m_editor->map.setTile(newPos, std::move(copiedTile));
			maxX = std::max(maxX, newPos.x);
			maxY = std::max(maxY, newPos.y);
		}

		m_editor->map.setWidth(maxX + 1);
		m_editor->map.setHeight(maxY + 1);

		m_hasMap = true;
		ShowMap();
		// Defer view reset until after layout so GetViewSize returns real dimensions
		CallAfter([this]() { ResetViewToMap(); });
		return true;
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
		} else {
			ClientVersion* latest = ClientVersion::getLatestVersion();
			if (latest) {
				version.otbm = latest->getPrefferedMapVersionID();
				version.client = latest->getProtocolID();
			}
		}

		m_editor = std::make_unique<Editor>(m_copyBuffer, version);
	}

	void ShowMessage(const wxString& message) {
		m_hasMap = false;
		if (m_emptyLabel) {
			m_emptyLabel->SetLabel(message);
			m_emptyLabel->Show();
		}
		if (m_mapWindow) {
			m_mapWindow->Hide();
		}
		Layout();
	}

	void ShowMap() {
		if (m_emptyLabel) {
			m_emptyLabel->Hide();
		}
		if (m_mapWindow) {
			m_mapWindow->Show();
		}
		Layout();
	}

	void ResetViewToMap() {
		if (!m_editor || !m_mapWindow || !m_canvas) {
			return;
		}

		int floor = FindLowestFloor(m_editor->map);
		m_canvas->floor = floor;
		UpdateBounds(m_editor->map);

		// Get canvas pixel size (DPI-scaled)
		int viewW = 0, viewH = 0;
		m_mapWindow->GetViewSize(&viewW, &viewH);
		if (viewW <= 0 || viewH <= 0) {
			return;
		}

		const int mapW = std::max(1, m_boundsWidth);
		const int mapH = std::max(1, m_boundsHeight);
		const int mapPixelsW = mapW * TILE_SIZE;
		const int mapPixelsH = mapH * TILE_SIZE;

		// Calculate zoom so the structure fits in the view with some padding
		const int padding = TILE_SIZE;
		double zoomX = double(mapPixelsW + padding * 2) / viewW;
		double zoomY = double(mapPixelsH + padding * 2) / viewH;
		double zoom = std::max(zoomX, zoomY);
		zoom = std::clamp(zoom, 0.125, 25.0);
		m_canvas->zoom = zoom;

		// Calculate the pixel coordinate of the structure center with floor offset
		int centerTileX = m_boundsMinX + mapW / 2;
		int centerTileY = m_boundsMinY + mapH / 2;
		int pixelX = centerTileX * TILE_SIZE;
		int pixelY = centerTileY * TILE_SIZE;
		if (floor <= GROUND_LAYER) {
			pixelX -= (GROUND_LAYER - floor) * TILE_SIZE;
			pixelY -= (GROUND_LAYER - floor) * TILE_SIZE;
		}

		// The scroll position to center the structure on screen
		int scrollX = pixelX - int(viewW * zoom / 2.0);
		int scrollY = pixelY - int(viewH * zoom / 2.0);

		// Set scrollbar range large enough to hold the scroll position
		// Range = max(needed scroll + viewport, map pixel size)
		int rangeW = std::max(scrollX + int(viewW * zoom) + padding, m_editor->map.getWidth() * TILE_SIZE);
		int rangeH = std::max(scrollY + int(viewH * zoom) + padding, m_editor->map.getHeight() * TILE_SIZE);
		m_mapWindow->SetSize(rangeW, rangeH);

		// Now set scroll position (must be >= 0 for scrollbars)
		m_mapWindow->Scroll(std::max(0, scrollX), std::max(0, scrollY));
		m_canvas->Refresh();
	}

	void UpdateBounds(BaseMap& map) {
		bool hasPos = false;
		int minX = 0, minY = 0, maxX = 0, maxY = 0;

		for (auto it = map.begin(); it != map.end(); ++it) {
			Tile* tile = it->get();
			if (!tile || tile->size() == 0) {
				continue;
			}

			const Position& pos = tile->getPosition();
			if (!hasPos) {
				minX = maxX = pos.x;
				minY = maxY = pos.y;
				hasPos = true;
			} else {
				minX = std::min(minX, pos.x);
				maxX = std::max(maxX, pos.x);
				minY = std::min(minY, pos.y);
				maxY = std::max(maxY, pos.y);
			}
		}

		if (hasPos) {
			m_boundsMinX = minX;
			m_boundsMinY = minY;
			m_boundsWidth = std::max(1, maxX - minX + 1);
			m_boundsHeight = std::max(1, maxY - minY + 1);
		} else {
			m_boundsMinX = 0;
			m_boundsMinY = 0;
			m_boundsWidth = 1;
			m_boundsHeight = 1;
		}
	}

	int FindLowestFloor(BaseMap& map) const {
		bool hasPos = false;
		int minZ = MAP_MAX_LAYER;
		for (auto it = map.begin(); it != map.end(); ++it) {
			Tile* tile = it->get();
			if (!tile || tile->size() == 0) {
				continue;
			}

			const int z = tile->getPosition().z;
			if (!hasPos) {
				minZ = z;
				hasPos = true;
			} else {
				minZ = std::min(minZ, z);
			}
		}
		return hasPos ? minZ : GROUND_LAYER;
	}

	void BindPreviewEvents() {
		if (!m_canvas) {
			return;
		}

		m_canvas->Bind(wxEVT_LEFT_DOWN, &StructurePreviewPanel::OnPanStart, this);
		m_canvas->Bind(wxEVT_RIGHT_DOWN, &StructurePreviewPanel::OnPanStart, this);
		m_canvas->Bind(wxEVT_LEFT_UP, &StructurePreviewPanel::OnPanEnd, this);
		m_canvas->Bind(wxEVT_RIGHT_UP, &StructurePreviewPanel::OnPanEnd, this);
		m_canvas->Bind(wxEVT_MOTION, &StructurePreviewPanel::OnPanMove, this);
		m_canvas->Bind(wxEVT_MOUSEWHEEL, &StructurePreviewPanel::OnMouseWheel, this);
		m_canvas->Bind(wxEVT_LEFT_DCLICK, &StructurePreviewPanel::OnIgnoreMouse, this);
		m_canvas->Bind(wxEVT_RIGHT_DCLICK, &StructurePreviewPanel::OnIgnoreMouse, this);
		m_canvas->Bind(wxEVT_MIDDLE_DCLICK, &StructurePreviewPanel::OnIgnoreMouse, this);
		m_canvas->Bind(wxEVT_MIDDLE_DOWN, &StructurePreviewPanel::OnIgnoreMouse, this);
		m_canvas->Bind(wxEVT_MIDDLE_UP, &StructurePreviewPanel::OnIgnoreMouse, this);
#ifdef wxEVT_AUX1_DOWN
		m_canvas->Bind(wxEVT_AUX1_DOWN, &StructurePreviewPanel::OnIgnoreMouse, this);
#endif
#ifdef wxEVT_AUX1_UP
		m_canvas->Bind(wxEVT_AUX1_UP, &StructurePreviewPanel::OnIgnoreMouse, this);
#endif
#ifdef wxEVT_AUX2_DOWN
		m_canvas->Bind(wxEVT_AUX2_DOWN, &StructurePreviewPanel::OnIgnoreMouse, this);
#endif
#ifdef wxEVT_AUX2_UP
		m_canvas->Bind(wxEVT_AUX2_UP, &StructurePreviewPanel::OnIgnoreMouse, this);
#endif
#ifdef wxEVT_AUX1_DCLICK
		m_canvas->Bind(wxEVT_AUX1_DCLICK, &StructurePreviewPanel::OnIgnoreMouse, this);
#endif
#ifdef wxEVT_AUX2_DCLICK
		m_canvas->Bind(wxEVT_AUX2_DCLICK, &StructurePreviewPanel::OnIgnoreMouse, this);
#endif
		m_canvas->Bind(wxEVT_KEY_DOWN, &StructurePreviewPanel::OnIgnoreKey, this);
		m_canvas->Bind(wxEVT_KEY_UP, &StructurePreviewPanel::OnIgnoreKey, this);
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

		wxPoint delta = event.GetPosition() - m_lastDragPos;
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
		double oldZoom = m_canvas->zoom;
		double newZoom = std::clamp(oldZoom + diff, 0.125, 25.0);
		diff = newZoom - oldZoom;
		m_canvas->zoom = newZoom;

		// Scroll relative to keep cursor position stable
		int viewW = 0, viewH = 0;
		m_mapWindow->GetViewSize(&viewW, &viewH);
		int scrollX = int(viewW * diff * (std::max(m_canvas->cursor_x, 1) / double(std::max(viewW, 1)))) * m_canvas->GetContentScaleFactor();
		int scrollY = int(viewH * diff * (std::max(m_canvas->cursor_y, 1) / double(std::max(viewH, 1)))) * m_canvas->GetContentScaleFactor();
		m_mapWindow->ScrollRelative(-scrollX, -scrollY);
		m_canvas->Refresh();
	}

	void OnIgnoreMouse(wxMouseEvent& WXUNUSED(event)) {
		// Swallow events to keep preview read-only.
	}

	void OnIgnoreKey(wxKeyEvent& WXUNUSED(event)) {
		// Swallow events to keep preview read-only.
	}

	CopyBuffer m_copyBuffer;
	std::unique_ptr<Editor> m_editor;
	MapWindow* m_mapWindow;
	MapCanvas* m_canvas;
	wxStaticText* m_emptyLabel;
	bool m_hasMap;
	bool m_dragging;
	wxPoint m_lastDragPos;
	int m_boundsMinX = 0;
	int m_boundsMinY = 0;
	int m_boundsWidth = 1;
	int m_boundsHeight = 1;
};

StructureManagerDialog* StructureManagerDialog::s_active = nullptr;

BEGIN_EVENT_TABLE(StructureManagerDialog, wxDialog)
	EVT_BUTTON(ID_SAVE_STRUCTURE, StructureManagerDialog::OnSaveSelection)
	EVT_BUTTON(ID_PASTE_STRUCTURE, StructureManagerDialog::OnPaste)
	EVT_BUTTON(ID_DELETE_STRUCTURE, StructureManagerDialog::OnDelete)
	EVT_CLOSE(StructureManagerDialog::OnClose)
END_EVENT_TABLE()

StructureManagerDialog::StructureManagerDialog(wxWindow* parent) :
	wxDialog(parent, wxID_ANY, "Structure Manager", wxDefaultPosition, wxSize(1080, 630),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	s_active = this;
	CreateControls();
	LoadStructures();
	Bind(wxEVT_CHAR_HOOK, &StructureManagerDialog::OnCharHook, this);
	Bind(wxEVT_PAINT, &StructureManagerDialog::OnPaint, this);
	Bind(wxEVT_SIZE, &StructureManagerDialog::OnSize, this);
	Bind(wxEVT_MOVE, &StructureManagerDialog::OnMove, this);
	CenterOnParent();
}

StructureManagerDialog::~StructureManagerDialog() {
	if (s_active == this) {
		s_active = nullptr;
	}
}

bool StructureManagerDialog::GetFixedSavePreview(int& width, int& height, int& zFrom, int& zTo) {
	if (!s_active || !s_active->IsShown()) {
		return false;
	}
	if (s_active->m_tutorialActive) {
		return false;
	}
	if (!s_active->m_fixedSizeCheck || !s_active->m_fixedSizeCheck->GetValue()) {
		return false;
	}
	width = s_active->m_fixedWidthSpin ? s_active->m_fixedWidthSpin->GetValue() : 0;
	height = s_active->m_fixedHeightSpin ? s_active->m_fixedHeightSpin->GetValue() : 0;
	zFrom = s_active->m_fixedZFromSpin ? s_active->m_fixedZFromSpin->GetValue() : 0;
	zTo = s_active->m_fixedZToSpin ? s_active->m_fixedZToSpin->GetValue() : 0;
	return width > 0 && height > 0;
}

void StructureManagerDialog::CreateControls() {
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* topSizer = new wxBoxSizer(wxHORIZONTAL);
	m_statusText = newd wxStaticText(this, wxID_ANY, "Select a structure to paste.");
	topSizer->Add(m_statusText, 1, wxALIGN_CENTER_VERTICAL);

	mainSizer->Add(topSizer, 0, wxALL | wxEXPAND, 8);

	wxBoxSizer* contentSizer = new wxBoxSizer(wxHORIZONTAL);

	// Left panel - categories
	wxPanel* leftPanel = newd wxPanel(this);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);

	wxStaticText* searchLabel = newd wxStaticText(leftPanel, wxID_ANY, "Search");
	leftSizer->Add(searchLabel, 0, wxLEFT | wxRIGHT | wxTOP, 6);

	m_searchCtrl = newd wxTextCtrl(leftPanel, ID_SEARCH_FILTER, "");
	m_searchCtrl->SetHint("Type to filter...");
	m_searchCtrl->Bind(wxEVT_TEXT, &StructureManagerDialog::OnSearchChanged, this);
	leftSizer->Add(m_searchCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	m_categoryTree = newd wxTreeCtrl(leftPanel, ID_CATEGORY_TREE, wxDefaultPosition, wxSize(200, -1),
		wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_HIDE_ROOT | wxTR_SINGLE);
	m_categoryTree->Bind(wxEVT_TREE_SEL_CHANGED, &StructureManagerDialog::OnCategoryChanged, this);
	leftSizer->Add(m_categoryTree, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	m_addCategoryButton = newd wxButton(leftPanel, ID_ADD_CATEGORY, "Add Category");
	m_addCategoryButton->Bind(wxEVT_BUTTON, &StructureManagerDialog::OnAddCategory, this);
	leftSizer->Add(m_addCategoryButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

	m_addSubcategoryButton = newd wxButton(leftPanel, ID_ADD_SUBCATEGORY, "Add Subcategory");
	m_addSubcategoryButton->Bind(wxEVT_BUTTON, &StructureManagerDialog::OnAddSubcategory, this);
	leftSizer->Add(m_addSubcategoryButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	m_removeCategoryButton = newd wxButton(leftPanel, ID_REMOVE_CATEGORY, "Remove Category");
	m_removeCategoryButton->Bind(wxEVT_BUTTON, &StructureManagerDialog::OnRemoveCategory, this);
	leftSizer->Add(m_removeCategoryButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

	m_removeSubcategoryButton = newd wxButton(leftPanel, ID_REMOVE_SUBCATEGORY, "Remove Subcategory");
	m_removeSubcategoryButton->Bind(wxEVT_BUTTON, &StructureManagerDialog::OnRemoveSubcategory, this);
	leftSizer->Add(m_removeSubcategoryButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	m_renameCategoryButton = newd wxButton(leftPanel, ID_RENAME_CATEGORY, "Rename Category...");
	m_renameCategoryButton->Bind(wxEVT_BUTTON, &StructureManagerDialog::OnRenameCategory, this);
	leftSizer->Add(m_renameCategoryButton, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

	leftPanel->SetSizer(leftSizer);
	contentSizer->Add(leftPanel, 0, wxEXPAND | wxALL, 8);

	// Center panel - structure list
	wxPanel* centerPanel = newd wxPanel(this);
	wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);
	m_list = newd wxListBox(centerPanel, ID_STRUCTURE_LIST, wxDefaultPosition, wxSize(320, -1));
	m_list->Bind(wxEVT_LISTBOX, &StructureManagerDialog::OnSelectionChanged, this);
	m_list->Bind(wxEVT_KEY_DOWN, &StructureManagerDialog::OnListKeyDown, this);
	m_list->Bind(wxEVT_LEFT_DOWN, &StructureManagerDialog::OnListLeftDown, this);
	m_list->Bind(wxEVT_LEFT_UP, &StructureManagerDialog::OnListLeftUp, this);
	m_list->Bind(wxEVT_MOTION, &StructureManagerDialog::OnListMouseMove, this);
	centerSizer->Add(m_list, 1, wxEXPAND | wxALL, 4);
	m_renameStructureButton = newd wxButton(centerPanel, ID_RENAME_STRUCTURE, "Rename...");
	m_renameStructureButton->Bind(wxEVT_BUTTON, &StructureManagerDialog::OnRenameStructure, this);
	m_moveStructureUpButton = newd wxButton(centerPanel, ID_MOVE_STRUCTURE_UP, "Move Up");
	m_moveStructureUpButton->Bind(wxEVT_BUTTON, &StructureManagerDialog::OnMoveStructureUp, this);
	m_moveStructureDownButton = newd wxButton(centerPanel, ID_MOVE_STRUCTURE_DOWN, "Move Down");
	m_moveStructureDownButton->Bind(wxEVT_BUTTON, &StructureManagerDialog::OnMoveStructureDown, this);

	wxBoxSizer* moveSizer = new wxBoxSizer(wxVERTICAL);
	moveSizer->Add(m_moveStructureUpButton, 0, wxBOTTOM | wxEXPAND, 4);
	moveSizer->Add(m_moveStructureDownButton, 0, wxEXPAND, 0);

	wxBoxSizer* listActionRow = new wxBoxSizer(wxHORIZONTAL);
	listActionRow->Add(m_renameStructureButton, 1, wxRIGHT | wxEXPAND, 6);
	listActionRow->Add(moveSizer, 0, wxEXPAND, 0);

	wxStaticBoxSizer* listActions = newd wxStaticBoxSizer(wxVERTICAL, centerPanel, "Structure Actions");
	listActions->Add(listActionRow, 1, wxEXPAND | wxALL, 4);
	centerSizer->Add(listActions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

	wxStaticText* rotateHint = newd wxStaticText(centerPanel, wxID_ANY, "Z: rotate paste (works with map focus)");
	rotateHint->SetForegroundColour(wxColour(255, 165, 0));
	centerSizer->Add(rotateHint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 2);
	wxStaticText* reorderHint = newd wxStaticText(centerPanel, wxID_ANY, "Drag to reorder (or use Move Up/Down)");
	reorderHint->SetForegroundColour(wxColour(255, 165, 0));
	centerSizer->Add(reorderHint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 2);
	wxStaticText* navHint = newd wxStaticText(centerPanel, wxID_ANY, "PgUp/PgDn: previous/next item (works with map focus)");
	navHint->SetForegroundColour(wxColour(255, 165, 0));
	centerSizer->Add(navHint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 2);
	wxStaticText* saveHint = newd wxStaticText(centerPanel, wxID_ANY, "Ctrl+Shift+S: save selection (works with map focus)");
	saveHint->SetForegroundColour(wxColour(255, 165, 0));
	centerSizer->Add(saveHint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);
	centerPanel->SetSizer(centerSizer);
	centerPanel->SetMinSize(wxSize(260, -1));
	contentSizer->Add(centerPanel, 0, wxEXPAND | wxALL, 8);

	// Right panel - actions and preview
	wxPanel* rightPanel = newd wxPanel(this);
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);

	m_saveButton = newd wxButton(rightPanel, ID_SAVE_STRUCTURE, "Save Current Selection...");
	{
		wxFont font = m_saveButton->GetFont();
		font.SetWeight(wxFONTWEIGHT_BOLD);
		m_saveButton->SetFont(font);
		m_saveButton->SetBackgroundColour(Theme::Get(Theme::Role::Accent));
		m_saveButton->SetForegroundColour(Theme::Get(Theme::Role::TextOnAccent));
	}

	m_detailsText = newd wxStaticText(rightPanel, wxID_ANY, "Details: Select item...");
	wxStaticBoxSizer* detailsSizer = newd wxStaticBoxSizer(wxVERTICAL, rightPanel, "Selection");
	detailsSizer->Add(m_saveButton, 0, wxBOTTOM | wxEXPAND, 6);
	detailsSizer->Add(m_detailsText, 0, wxBOTTOM | wxEXPAND, 4);
	rightSizer->Add(detailsSizer, 0, wxEXPAND | wxBOTTOM, 10);

	m_fixedSizeCheck = newd wxCheckBox(rightPanel, wxID_ANY, "Fixed size (centered on mouse)");
	m_fixedSizeCheck->Bind(wxEVT_CHECKBOX, &StructureManagerDialog::OnFixedSizeToggle, this);

	m_fixedWidthSpin = newd wxSpinCtrl(rightPanel, wxID_ANY, "5", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, MAP_MAX_WIDTH, 5);
	m_fixedHeightSpin = newd wxSpinCtrl(rightPanel, wxID_ANY, "5", wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, MAP_MAX_HEIGHT, 5);

	int defaultZ = GROUND_LAYER;
	if (g_gui.IsEditorOpen()) {
		defaultZ = g_gui.GetCurrentFloor();
	}
	m_fixedZFromSpin = newd wxSpinCtrl(rightPanel, wxID_ANY, i2ws(defaultZ), wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, MAP_MAX_LAYER, defaultZ);
	m_fixedZToSpin = newd wxSpinCtrl(rightPanel, wxID_ANY, i2ws(defaultZ), wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, MAP_MAX_LAYER, defaultZ);

	wxBoxSizer* sizeRow = new wxBoxSizer(wxHORIZONTAL);
	sizeRow->Add(newd wxStaticText(rightPanel, wxID_ANY, "W"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	sizeRow->Add(m_fixedWidthSpin, 0, wxRIGHT, 10);
	sizeRow->Add(newd wxStaticText(rightPanel, wxID_ANY, "H"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	sizeRow->Add(m_fixedHeightSpin, 0);

	wxBoxSizer* zRow = new wxBoxSizer(wxHORIZONTAL);
	zRow->Add(newd wxStaticText(rightPanel, wxID_ANY, "Z from"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	zRow->Add(m_fixedZFromSpin, 0, wxRIGHT, 8);
	zRow->Add(newd wxStaticText(rightPanel, wxID_ANY, "to"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	zRow->Add(m_fixedZToSpin, 0);

	m_autoNameCheck = newd wxCheckBox(rightPanel, wxID_ANY, "Auto name");
	m_autoNameCheck->Bind(wxEVT_CHECKBOX, &StructureManagerDialog::OnAutoNameToggle, this);

	m_autoNameBaseCtrl = newd wxTextCtrl(rightPanel, wxID_ANY, "structure");
	m_autoNameBaseCtrl->Bind(wxEVT_TEXT, &StructureManagerDialog::OnAutoNameBaseChanged, this);
	m_autoNamePreview = newd wxStaticText(rightPanel, wxID_ANY, "Next: -");

	wxBoxSizer* autoRow = new wxBoxSizer(wxHORIZONTAL);
	autoRow->Add(newd wxStaticText(rightPanel, wxID_ANY, "Base"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
	autoRow->Add(m_autoNameBaseCtrl, 1, wxEXPAND);

	wxStaticBoxSizer* saveOptionsSizer = newd wxStaticBoxSizer(wxVERTICAL, rightPanel, "Save Options");
	saveOptionsSizer->Add(m_fixedSizeCheck, 0, wxBOTTOM, 6);
	saveOptionsSizer->Add(sizeRow, 0, wxBOTTOM, 4);
	saveOptionsSizer->Add(zRow, 0, wxBOTTOM, 8);
	saveOptionsSizer->Add(m_autoNameCheck, 0, wxBOTTOM, 6);
	saveOptionsSizer->Add(autoRow, 0, wxBOTTOM | wxEXPAND, 4);
	saveOptionsSizer->Add(m_autoNamePreview, 0);
	rightSizer->Add(saveOptionsSizer, 0, wxEXPAND | wxBOTTOM, 10);

	m_pasteButton = newd wxButton(rightPanel, ID_PASTE_STRUCTURE, "Paste");

	wxStaticText* rotationLabel = newd wxStaticText(rightPanel, wxID_ANY, "Paste rotation");

	wxArrayString rotationChoices;
	rotationChoices.Add("0 deg");
	rotationChoices.Add("90 deg clockwise");
	rotationChoices.Add("180 deg");
	rotationChoices.Add("90 deg counterclockwise");
	m_pasteRotationChoice = newd wxChoice(rightPanel, ID_PASTE_ROTATION, wxDefaultPosition, wxDefaultSize, rotationChoices);
	m_pasteRotationChoice->SetSelection(0);
	m_pasteRotationChoice->Bind(wxEVT_CHOICE, &StructureManagerDialog::OnPasteRotationChanged, this);

	m_keepPasteCheck = newd wxCheckBox(rightPanel, wxID_ANY, "Keep paste active");

	wxStaticBoxSizer* pasteSizer = newd wxStaticBoxSizer(wxVERTICAL, rightPanel, "Paste");
	pasteSizer->Add(m_pasteButton, 0, wxBOTTOM | wxEXPAND, 6);
	pasteSizer->Add(rotationLabel, 0, wxBOTTOM, 3);
	pasteSizer->Add(m_pasteRotationChoice, 0, wxBOTTOM | wxEXPAND, 6);
	pasteSizer->Add(m_keepPasteCheck, 0);
	rightSizer->Add(pasteSizer, 0, wxEXPAND | wxBOTTOM, 10);

	m_deleteButton = newd wxButton(rightPanel, ID_DELETE_STRUCTURE, "Delete");

	wxStaticBoxSizer* manageSizer = newd wxStaticBoxSizer(wxVERTICAL, rightPanel, "Manage");
	manageSizer->Add(m_deleteButton, 0, wxBOTTOM | wxEXPAND, 6);
	rightSizer->Add(manageSizer, 0, wxEXPAND | wxBOTTOM, 10);

	m_helpButton = newd wxButton(rightPanel, ID_TUTORIAL_HELP, "How to Use");
	m_helpButton->Bind(wxEVT_BUTTON, &StructureManagerDialog::OnHowToUse, this);

	{
		const wxSize iconSize(16, 16);
		m_saveButton->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_SAVE, iconSize));
		m_helpButton->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_CIRCLE_QUESTION, iconSize));
		if (m_moveStructureUpButton) {
			m_moveStructureUpButton->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_ARROW_UP, iconSize));
		}
		if (m_moveStructureDownButton) {
			m_moveStructureDownButton->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_ARROW_DOWN, iconSize));
		}
	}

	{
		int buttonWidth = 0;
		wxButton* buttons[] = {m_saveButton, m_pasteButton, m_deleteButton, m_helpButton};
		for (wxButton* button : buttons) {
			if (!button) continue;
			buttonWidth = std::max(buttonWidth, button->GetBestSize().GetWidth());
		}
		if (buttonWidth > 0) {
			for (wxButton* button : buttons) {
				if (!button) continue;
				wxSize size = button->GetBestSize();
				button->SetMinSize(wxSize(buttonWidth, size.GetHeight()));
			}
		}
	}

	{
		int moveWidth = 0;
		wxButton* moveButtons[] = {m_moveStructureUpButton, m_moveStructureDownButton};
		for (wxButton* button : moveButtons) {
			if (!button) continue;
			moveWidth = std::max(moveWidth, button->GetBestSize().GetWidth());
		}
		if (moveWidth > 0) {
			for (wxButton* button : moveButtons) {
				if (!button) continue;
				wxSize size = button->GetBestSize();
				button->SetMinSize(wxSize(moveWidth, size.GetHeight()));
			}
		}
	}

	wxStaticBoxSizer* helpSizer = newd wxStaticBoxSizer(wxVERTICAL, rightPanel, "Help");
	helpSizer->Add(m_helpButton, 0, wxEXPAND, 0);
	rightSizer->Add(helpSizer, 0, wxEXPAND | wxBOTTOM, 10);

	wxStaticBoxSizer* previewSizer = newd wxStaticBoxSizer(wxVERTICAL, rightPanel, "Preview");
	m_previewPanel = newd StructurePreviewPanel(rightPanel);
	previewSizer->Add(m_previewPanel, 1, wxEXPAND | wxALL, 4);
	rightSizer->Add(previewSizer, 1, wxEXPAND);

	rightPanel->SetSizer(rightSizer);
	rightPanel->SetMinSize(wxSize(360, -1));
	contentSizer->Add(rightPanel, 1, wxALL | wxEXPAND, 8);

	mainSizer->Add(contentSizer, 1, wxEXPAND);

	SetSizerAndFit(mainSizer);
	SetMinSize(wxSize(960, 540));

	UpdateSaveOptionsUi();
}

void StructureManagerDialog::LoadStructures() {
	m_entries.clear();
	m_listEntries.clear();
	m_categoryPaths.clear();

	m_baseDir = GetStructuresDirectory();
	m_currentCategoryPath = "";
	m_categoryPaths.push_back("");

	std::function<void(const wxString&, const wxString&)> scanDir;
	scanDir = [&](const wxString& fullPath, const wxString& relativePath) {
		wxDir dir(fullPath);
		if (!dir.IsOpened()) {
			return;
		}

		wxString filename;
		bool contFiles = dir.GetFirst(&filename, "*.otbm", wxDIR_FILES);
		while (contFiles) {
			wxFileName file(fullPath, filename);
			StructureEntry entry;
			entry.name = file.GetName();
			entry.path = file.GetFullPath();
			entry.category = relativePath;
			m_entries.push_back(entry);
			contFiles = dir.GetNext(&filename);
		}

		wxString dirname;
		bool contDirs = dir.GetFirst(&dirname, "", wxDIR_DIRS);
		while (contDirs) {
			wxString childFull = BuildCategoryDirectory(fullPath, dirname);
			wxString childRel = relativePath.empty() ? dirname : (relativePath + "/" + dirname);
			m_categoryPaths.push_back(childRel);
			scanDir(childFull, childRel);
			contDirs = dir.GetNext(&dirname);
		}
	};

	scanDir(m_baseDir, "");

	std::sort(m_entries.begin(), m_entries.end(), [](const StructureEntry& a, const StructureEntry& b) {
		return NaturalCompareNoCase(a.name, b.name) < 0;
	});

	std::sort(m_categoryPaths.begin(), m_categoryPaths.end(), [](const wxString& a, const wxString& b) {
		return NaturalCompareNoCase(a, b) < 0;
	});
	m_categoryPaths.erase(std::unique(m_categoryPaths.begin(), m_categoryPaths.end(),
		[](const wxString& a, const wxString& b) { return a.CmpNoCase(b) == 0; }), m_categoryPaths.end());

	BuildCategoryTree();
	RefreshItemList();
}

void StructureManagerDialog::BuildCategoryTree() {
	if (!m_categoryTree) {
		return;
	}

	m_categoryTree->Freeze();
	m_categoryTree->DeleteAllItems();

	wxTreeItemId root = m_categoryTree->AddRoot("Categories", -1, -1, new CategoryItemData(""));
	std::unordered_map<std::string, wxTreeItemId> nodeByPath;
	nodeByPath[""] = root;

	std::function<wxTreeItemId(const wxString&)> ensureNode = [&](const wxString& path) {
		if (path.empty()) {
			return root;
		}
		std::string key = nstr(path);
		auto it = nodeByPath.find(key);
		if (it != nodeByPath.end()) {
			return it->second;
		}
		wxString parentPath;
		wxString name = path;
		int sep = path.Find('/', true);
		if (sep != wxNOT_FOUND) {
			parentPath = path.Left(sep);
			name = path.Mid(sep + 1);
		}
		wxTreeItemId parentId = parentPath.empty() ? root : ensureNode(parentPath);
		wxTreeItemId id = m_categoryTree->AppendItem(parentId, name, -1, -1, new CategoryItemData(path));
		nodeByPath[key] = id;
		return id;
	};

	for (const auto& path : m_categoryPaths) {
		if (path.empty()) {
			continue;
		}
		ensureNode(path);
	}

	wxTreeItemIdValue cookie;
	wxTreeItemId firstChild = m_categoryTree->GetFirstChild(root, cookie);
	wxTreeItemId child = firstChild;
	while (child.IsOk()) {
		m_categoryTree->Expand(child);
		child = m_categoryTree->GetNextChild(root, cookie);
	}
	if (firstChild.IsOk()) {
		m_categoryTree->SelectItem(firstChild);
	} else {
		m_currentCategoryPath.clear();
	}
	m_categoryTree->Thaw();
}

void StructureManagerDialog::RefreshItemList() {
	if (!m_list) {
		return;
	}

	m_list->Clear();
	m_listEntries.clear();

	wxString filter = m_searchCtrl ? m_searchCtrl->GetValue() : wxString();
	filter.MakeLower();

	const auto orderedEntries = GetOrderedEntriesForCategory(m_currentCategoryPath);
	for (const auto* entry : orderedEntries) {
		if (!entry) {
			continue;
		}
		if (!filter.empty()) {
			wxString lowered = entry->name;
			lowered.MakeLower();
			if (lowered.Find(filter) == wxNOT_FOUND) {
				continue;
			}
		}
		m_list->Append(entry->name);
		m_listEntries.push_back(entry);
	}
	UpdateSelectionUi();
}

void StructureManagerDialog::UpdateSelectionUi() {
	bool hasAnyCategory = false;
	if (m_categoryTree) {
		wxTreeItemId root = m_categoryTree->GetRootItem();
		if (root.IsOk()) {
			wxTreeItemIdValue cookie;
			hasAnyCategory = m_categoryTree->GetFirstChild(root, cookie).IsOk();
		}
	}

	const int selection = m_list ? m_list->GetSelection() : wxNOT_FOUND;
	const bool hasSelection = selection != wxNOT_FOUND &&
		selection >= 0 && selection < static_cast<int>(m_listEntries.size());
	const bool canReorder = CanReorderCurrentList();
	if (m_saveButton) {
		m_saveButton->Enable(!m_tutorialActive && hasAnyCategory);
	}
	if (m_pasteButton) {
		m_pasteButton->Enable(!m_tutorialActive && hasSelection);
	}
	if (m_pasteRotationChoice) {
		m_pasteRotationChoice->Enable(!m_tutorialActive && hasSelection);
	}
	if (m_deleteButton) {
		m_deleteButton->Enable(!m_tutorialActive && hasSelection);
	}
	if (m_renameStructureButton) {
		m_renameStructureButton->Enable(!m_tutorialActive && hasSelection);
	}
	if (m_moveStructureUpButton) {
		bool canMoveUp = canReorder && hasSelection && selection > 0;
		m_moveStructureUpButton->Enable(!m_tutorialActive && canMoveUp);
	}
	if (m_moveStructureDownButton) {
		bool canMoveDown = canReorder && hasSelection &&
			selection < static_cast<int>(m_listEntries.size()) - 1;
		m_moveStructureDownButton->Enable(!m_tutorialActive && canMoveDown);
	}
	if (m_detailsText) {
		if (hasSelection) {
			const auto& entry = *m_listEntries[selection];
			wxString categoryLabel = entry.category.empty() ? wxString("General") : entry.category;
			m_detailsText->SetLabel("Details: " + entry.name + " (" + categoryLabel + ")");
		} else {
			m_detailsText->SetLabel("Details: Select item...");
		}
	}

	if (m_renameCategoryButton) {
		bool canRename = !m_currentCategoryPath.empty();
		m_renameCategoryButton->Enable(!m_tutorialActive && canRename);
	}

	const bool hasCategory = !m_currentCategoryPath.empty();
	const bool isSubcategory = hasCategory && m_currentCategoryPath.Find('/') != wxNOT_FOUND;
	if (m_addSubcategoryButton) {
		m_addSubcategoryButton->Enable(!m_tutorialActive && hasCategory);
	}
	if (m_removeCategoryButton) {
		m_removeCategoryButton->Enable(!m_tutorialActive && hasCategory && !isSubcategory);
	}
	if (m_removeSubcategoryButton) {
		m_removeSubcategoryButton->Enable(!m_tutorialActive && isSubcategory);
	}

	UpdatePreview(GetSelectedEntry(nullptr));
	UpdateSaveOptionsUi();
}

void StructureManagerDialog::SetStatusText(const wxString& text) {
	if (m_statusText) {
		m_statusText->SetLabel(text);
	}
}

const StructureManagerDialog::StructureEntry* StructureManagerDialog::GetSelectedEntry(int* outIndex) const {
	if (outIndex) {
		*outIndex = wxNOT_FOUND;
	}
	if (!m_list) {
		return nullptr;
	}
	int selection = m_list->GetSelection();
	if (selection == wxNOT_FOUND || selection < 0 || selection >= static_cast<int>(m_listEntries.size())) {
		return nullptr;
	}
	if (outIndex) {
		*outIndex = selection;
	}
	return m_listEntries[selection];
}

wxString StructureManagerDialog::GetSelectedCategoryPath() const {
	if (!m_categoryTree) {
		return m_currentCategoryPath;
	}

	wxTreeItemId item = m_categoryTree->GetSelection();
	if (!item.IsOk()) {
		return m_currentCategoryPath;
	}

	wxTreeItemId root = m_categoryTree->GetRootItem();
	if (!root.IsOk()) {
		return m_currentCategoryPath;
	}

	wxArrayString parts;
	wxTreeItemId current = item;
	while (current.IsOk() && current != root) {
		parts.Add(m_categoryTree->GetItemText(current));
		current = m_categoryTree->GetItemParent(current);
	}

	wxString path;
	for (int i = static_cast<int>(parts.size()) - 1; i >= 0; --i) {
		if (!path.empty()) {
			path += "/";
		}
		path += parts[static_cast<size_t>(i)];
	}

	return path;
}

bool StructureManagerDialog::CanReorderCurrentList() const {
	if (!m_searchCtrl) {
		return true;
	}
	wxString filter = m_searchCtrl->GetValue();
	filter.Trim(true).Trim(false);
	return filter.empty();
}

wxString StructureManagerDialog::GetOrderFilePath(const wxString& category) const {
	wxString dir = BuildCategoryDirectory(m_baseDir, category);
	return wxFileName(dir, ".order.txt").GetFullPath();
}

bool StructureManagerDialog::ReadOrderFile(const wxString& category, std::vector<std::string>& out) const {
	out.clear();
	wxString path = GetOrderFilePath(category);
	if (!wxFileExists(path)) {
		return false;
	}

	wxTextFile file;
	if (!file.Open(path)) {
		return false;
	}

	std::unordered_set<std::string> seen;
	for (size_t i = 0; i < file.GetLineCount(); ++i) {
		wxString line = file.GetLine(i);
		line.Trim(true).Trim(false);
		if (line.empty()) {
			continue;
		}
		std::string key = nstr(line);
		std::string lower = as_lower_str(key);
		if (seen.insert(lower).second) {
			out.push_back(key);
		}
	}

	file.Close();
	return true;
}

void StructureManagerDialog::WriteOrderFile(const wxString& category, const std::vector<std::string>& names) const {
	wxString path = GetOrderFilePath(category);
	wxTextFile file;
	if (wxFileExists(path)) {
		if (!file.Open(path)) {
			return;
		}
		file.Clear();
	} else {
		if (!file.Create(path)) {
			return;
		}
	}

	for (const auto& name : names) {
		if (name.empty()) {
			continue;
		}
		file.AddLine(wxstr(name));
	}

	file.Write();
	file.Close();
}

void StructureManagerDialog::WriteOrderFile(const wxString& category, const std::vector<const StructureEntry*>& entries) const {
	std::vector<std::string> names;
	names.reserve(entries.size());
	for (const auto* entry : entries) {
		if (!entry) {
			continue;
		}
		names.push_back(nstr(entry->name));
	}
	WriteOrderFile(category, names);
}

std::vector<const StructureManagerDialog::StructureEntry*> StructureManagerDialog::GetOrderedEntriesForCategory(const wxString& category) const {
	std::vector<const StructureEntry*> entries;
	for (const auto& entry : m_entries) {
		if (entry.category == category) {
			entries.push_back(&entry);
		}
	}

	if (entries.size() <= 1) {
		return entries;
	}

	std::vector<std::string> order;
	ReadOrderFile(category, order);
	if (order.empty()) {
		std::sort(entries.begin(), entries.end(), [](const StructureEntry* a, const StructureEntry* b) {
			return NaturalCompareNoCase(a->name, b->name) < 0;
		});
		return entries;
	}

	std::unordered_map<std::string, const StructureEntry*> byName;
	byName.reserve(entries.size());
	for (const auto* entry : entries) {
		byName.emplace(as_lower_str(nstr(entry->name)), entry);
	}

	std::vector<const StructureEntry*> ordered;
	ordered.reserve(entries.size());
	std::unordered_set<std::string> used;
	for (const auto& name : order) {
		std::string lower = as_lower_str(name);
		auto it = byName.find(lower);
		if (it != byName.end() && used.insert(lower).second) {
			ordered.push_back(it->second);
		}
	}

	std::vector<const StructureEntry*> remaining;
	for (const auto* entry : entries) {
		std::string lower = as_lower_str(nstr(entry->name));
		if (used.find(lower) == used.end()) {
			remaining.push_back(entry);
		}
	}
	std::sort(remaining.begin(), remaining.end(), [](const StructureEntry* a, const StructureEntry* b) {
		return NaturalCompareNoCase(a->name, b->name) < 0;
	});
	ordered.insert(ordered.end(), remaining.begin(), remaining.end());
	return ordered;
}

void StructureManagerDialog::UpdateSaveOptionsUi() {
	if (m_tutorialActive) {
		return;
	}

	const bool fixed = m_fixedSizeCheck && m_fixedSizeCheck->GetValue();
	if (m_fixedWidthSpin) m_fixedWidthSpin->Enable(fixed);
	if (m_fixedHeightSpin) m_fixedHeightSpin->Enable(fixed);
	if (m_fixedZFromSpin) m_fixedZFromSpin->Enable(fixed);
	if (m_fixedZToSpin) m_fixedZToSpin->Enable(fixed);

	const bool autoName = m_autoNameCheck && m_autoNameCheck->GetValue();
	if (m_autoNameBaseCtrl) m_autoNameBaseCtrl->Enable(autoName);
	if (m_autoNamePreview) {
		if (!autoName) {
			m_autoNamePreview->SetLabel("Next: -");
		} else {
			wxString base = GetAutoNameBase();
			if (base.empty()) {
				m_autoNamePreview->SetLabel("Next: -");
			} else {
				const wxString categoryPath = GetSelectedCategoryPath();
				const wxString safeBase = wxstr(SanitizeFilename(nstr(base)));
				int next = GetNextAutoNameIndex(safeBase, categoryPath);
				const wxString safePrefix = GetAutoNamePrefix(safeBase);
				m_autoNamePreview->SetLabel("Next: " + safePrefix + i2ws(next));
			}
		}
	}
}

wxString StructureManagerDialog::GetAutoNameBase() const {
	if (!m_autoNameBaseCtrl) {
		return wxString();
	}
	wxString base = m_autoNameBaseCtrl->GetValue();
	base.Trim(true).Trim(false);
	return base;
}

int StructureManagerDialog::GetNextAutoNameIndex(const wxString& base, const wxString& categoryPath) const {
	if (base.empty()) {
		return 1;
	}

	wxString prefix = base;
	int startIndex = 0;
	{
		size_t digitStart = base.length();
		while (digitStart > 0 && wxIsdigit(base[digitStart - 1])) {
			--digitStart;
		}
		if (digitStart < base.length()) {
			prefix = base.Mid(0, digitStart);
			wxString numPart = base.Mid(digitStart);
			long val = 0;
			numPart.ToLong(&val);
			startIndex = static_cast<int>(val);
		}
	}

	if (prefix.empty()) {
		prefix = base;
		startIndex = 0;
	}

	wxString prefixLower = prefix;
	prefixLower.MakeLower();
	int maxIndex = startIndex > 0 ? startIndex - 1 : 0;

	for (const auto& entry : m_entries) {
		if (entry.category != categoryPath) {
			continue;
		}
		wxString nameLower = entry.name;
		nameLower.MakeLower();
		if (!nameLower.StartsWith(prefixLower)) {
			continue;
		}
		wxString suffix = entry.name.Mid(prefix.length());
		if (suffix.empty()) {
			continue;
		}
		bool allDigits = true;
		for (size_t i = 0; i < suffix.length(); ++i) {
			if (!wxIsdigit(suffix[i])) {
				allDigits = false;
				break;
			}
		}
		if (!allDigits) {
			continue;
		}
		long value = 0;
		if (suffix.ToLong(&value) && value > maxIndex) {
			maxIndex = static_cast<int>(value);
		}
	}

	return maxIndex + 1;
}

wxString StructureManagerDialog::GetAutoNamePrefix(const wxString& base) const {
	size_t digitStart = base.length();
	while (digitStart > 0 && wxIsdigit(base[digitStart - 1])) {
		--digitStart;
	}
	if (digitStart < base.length() && digitStart > 0) {
		return base.Mid(0, digitStart);
	}
	return base;
}

bool StructureManagerDialog::BuildFixedAreaBuffer(Editor& editor, const Position& center, int width, int height, int zFrom, int zTo,
	BaseMap& outMap, Position& outMinPos, Position& outMaxPos, int& outTiles, int& outItems) const
{
	if (width <= 0 || height <= 0) {
		return false;
	}

	outMap.clear(true);
	outTiles = 0;
	outItems = 0;

	const int minZ = std::min(zFrom, zTo);
	const int maxZ = std::max(zFrom, zTo);
	const int minX = center.x - (width / 2);
	const int minY = center.y - (height / 2);
	const int maxX = minX + width - 1;
	const int maxY = minY + height - 1;

	outMinPos = Position(minX, minY, minZ);
	outMaxPos = Position(maxX, maxY, maxZ);

	Map& map = editor.map;
	bool hasTile = false;

	for (int z = minZ; z <= maxZ; ++z) {
		for (int y = minY; y <= maxY; ++y) {
			for (int x = minX; x <= maxX; ++x) {
				Tile* tile = map.getTile(x, y, z);
				if (!tile || tile->size() == 0) {
					continue;
				}

				auto copiedTile = TileOperations::deepCopy(tile, outMap);
				(void)outMap.setTile(std::move(copiedTile));

				++outTiles;
				outItems += tile->size();
				hasTile = true;
			}
		}
	}

	return hasTile;
}

void StructureManagerDialog::SelectCategoryByPath(const wxString& path) {
	if (!m_categoryTree) {
		return;
	}
	wxTreeItemId root = m_categoryTree->GetRootItem();
	if (!root.IsOk()) {
		return;
	}

	std::function<bool(const wxTreeItemId&)> findNode = [&](const wxTreeItemId& node) {
		if (!node.IsOk()) {
			return false;
		}
		CategoryItemData* data = static_cast<CategoryItemData*>(m_categoryTree->GetItemData(node));
		if (data && data->categoryPath.CmpNoCase(path) == 0) {
			m_categoryTree->SelectItem(node);
			return true;
		}
		wxTreeItemIdValue cookie;
		wxTreeItemId child = m_categoryTree->GetFirstChild(node, cookie);
		while (child.IsOk()) {
			if (findNode(child)) {
				return true;
			}
			child = m_categoryTree->GetNextChild(node, cookie);
		}
		return false;
	};

	findNode(root);
}

void StructureManagerDialog::SelectEntryByName(const wxString& name) {
	if (!m_list) {
		return;
	}
	for (size_t i = 0; i < m_listEntries.size(); ++i) {
		if (m_listEntries[i]->name.CmpNoCase(name) == 0) {
			m_suppressAutoPaste = true;
			m_list->SetSelection(static_cast<int>(i));
			m_suppressAutoPaste = false;
			UpdateSelectionUi();
			return;
		}
	}
}

void StructureManagerDialog::UpdatePreview(const StructureEntry* entry) {
	if (!m_previewPanel) {
		return;
	}

	if (!entry) {
		m_previewPanel->Clear("Select a structure to preview.");
		m_previewPath.clear();
		return;
	}

	if (entry->path == m_previewPath) {
		return;
	}

	wxString error;
	if (!m_previewPanel->LoadStructure(entry->path, error)) {
		m_previewPanel->Clear("Preview failed to load.");
		m_previewPath.clear();
		return;
	}

	m_previewPath = entry->path;
}

void StructureManagerDialog::StartPasteFromEntry(const StructureEntry& entry) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor) {
		return;
	}

	// Load structure into the copybuffer's internal map
	BaseMap& bufferMap = editor->copybuffer.getBufferMap();
	Position copyPos;
	wxString error;
	if (!LoadStructureFile(entry.path, bufferMap, copyPos, error)) {
		wxMessageBox("Failed to load structure:\n" + error, "Structure Manager", wxOK | wxICON_ERROR, this);
		return;
	}

	editor->copybuffer.setPosition(copyPos);
	m_currentPasteRotationTurns = 0;
	g_gui.PreparePaste();
	wxString status = "Paste: Click map to place '" + entry.name + "'. Right-click cancel.";
	SetStatusText(status);
	g_gui.SetStatusText(status);
}

void StructureManagerDialog::RenameSelectedStructure() {
	int selection = wxNOT_FOUND;
	const StructureEntry* entry = GetSelectedEntry(&selection);
	if (!entry) {
		return;
	}

	wxTextEntryDialog dialog(this, "New name:", "Rename Structure", entry->name);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	wxString name = dialog.GetValue();
	name.Trim(true).Trim(false);
	if (name.empty()) {
		wxMessageBox("Structure name cannot be empty.", "Structure Manager", wxOK | wxICON_WARNING, this);
		return;
	}

	std::string sanitized = SanitizeFilename(nstr(name));
	wxString safeName = wxstr(sanitized);
	wxString dir = BuildCategoryDirectory(m_baseDir, entry->category);
	wxString newPath = wxFileName(dir, safeName + ".otbm").GetFullPath();
	if (wxFileExists(newPath)) {
		int ret = wxMessageBox("A structure with this name already exists. Overwrite?", "Structure Manager",
			wxYES_NO | wxICON_WARNING, this);
		if (ret != wxYES) {
			return;
		}
	}

	if (!wxRenameFile(entry->path, newPath, true)) {
		wxMessageBox("Failed to rename structure.", "Structure Manager", wxOK | wxICON_ERROR, this);
		return;
	}

	std::vector<std::string> order;
	if (ReadOrderFile(entry->category, order)) {
		const std::string oldLower = as_lower_str(nstr(entry->name));
		bool updated = false;
		for (auto& item : order) {
			if (as_lower_str(item) == oldLower) {
				item = nstr(safeName);
				updated = true;
			}
		}
		if (!updated) {
			order.push_back(nstr(safeName));
		}
		std::unordered_set<std::string> seen;
		std::vector<std::string> cleaned;
		for (const auto& item : order) {
			std::string lower = as_lower_str(item);
			if (seen.insert(lower).second) {
				cleaned.push_back(item);
			}
		}
		WriteOrderFile(entry->category, cleaned);
	}

	LoadStructures();
	SelectCategoryByPath(entry->category);
	SelectEntryByName(safeName);
}

void StructureManagerDialog::RenameSelectedCategory() {
	if (m_currentCategoryPath.empty()) {
		return;
	}

	wxString currentName = m_currentCategoryPath;
	int sep = currentName.Find('/', true);
	if (sep != wxNOT_FOUND) {
		currentName = currentName.Mid(sep + 1);
	}

	wxTextEntryDialog dialog(this, "New category name:", "Rename Category", currentName);
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	wxString name = dialog.GetValue();
	name.Trim(true).Trim(false);
	if (name.empty()) {
		wxMessageBox("Category name cannot be empty.", "Structure Manager", wxOK | wxICON_WARNING, this);
		return;
	}

	std::string sanitized = SanitizeFilename(nstr(name));
	wxString safeName = wxstr(sanitized);
	wxString baseDir = GetStructuresDirectory();
	if (CategoryNameExists(m_categoryPaths, safeName, m_currentCategoryPath)) {
		wxMessageBox("A category with this name already exists.", "Structure Manager", wxOK | wxICON_INFORMATION, this);
		return;
	}

	wxString newRelativePath;
	int parentSep = m_currentCategoryPath.Find('/', true);
	if (parentSep != wxNOT_FOUND) {
		wxString parentPath = m_currentCategoryPath.Left(parentSep);
		newRelativePath = parentPath + "/" + safeName;
	} else {
		newRelativePath = safeName;
	}
	wxString newDir = BuildCategoryDirectory(baseDir, newRelativePath);
	wxString oldDir = BuildCategoryDirectory(baseDir, m_currentCategoryPath);

	if (wxDirExists(newDir)) {
		wxMessageBox("Category already exists.", "Structure Manager", wxOK | wxICON_INFORMATION, this);
		return;
	}

	if (!wxRenameFile(oldDir, newDir, false)) {
		wxMessageBox("Failed to rename category.", "Structure Manager", wxOK | wxICON_ERROR, this);
		return;
	}

	LoadStructures();
	SelectCategoryByPath(newRelativePath);
}

void StructureManagerDialog::SelectAdjacentEntry(int delta) {
	if (!m_list || m_listEntries.empty()) {
		return;
	}

	const int count = static_cast<int>(m_listEntries.size());
	int selection = m_list->GetSelection();
	if (selection == wxNOT_FOUND) {
		selection = (delta > 0) ? 0 : count - 1;
	} else {
		selection += delta;
		if (selection < 0) {
			selection = count - 1;
		} else if (selection >= count) {
			selection = 0;
		}
	}

	m_suppressAutoPaste = true;
	m_list->SetSelection(selection);
	m_suppressAutoPaste = false;
	UpdateSelectionUi();

	const StructureEntry& entry = *m_listEntries[selection];
	StartPasteFromEntry(entry);
}

void StructureManagerDialog::OnSaveSelection(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		wxMessageBox("Open a map before saving structures.", "Structure Manager", wxOK | wxICON_INFORMATION, this);
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	const wxString categoryPath = GetSelectedCategoryPath();
	wxString dir = BuildCategoryDirectory(m_baseDir, categoryPath);
	wxFileName dirName(dir, "");
	if (!dirName.DirExists()) {
		if (!dirName.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
			wxMessageBox("Failed to create category directory.", "Structure Manager", wxOK | wxICON_ERROR, this);
			return;
		}
	}

	if (!editor) {
		return;
	}

	const bool useFixed = m_fixedSizeCheck && m_fixedSizeCheck->GetValue();
	BaseMap buffer;
	Position minPos;
	Position maxPos;
	int tileCount = 0;
	int itemCount = 0;
	if (useFixed) {
		MapTab* mapTab = g_gui.GetCurrentMapTab();
		MapCanvas* canvas = mapTab ? mapTab->GetCanvas() : nullptr;
		if (!canvas) {
			wxMessageBox("Move the mouse over the map before saving.", "Structure Manager", wxOK | wxICON_INFORMATION, this);
			return;
		}
		const int width = m_fixedWidthSpin ? m_fixedWidthSpin->GetValue() : 0;
		const int height = m_fixedHeightSpin ? m_fixedHeightSpin->GetValue() : 0;
		const int zFrom = m_fixedZFromSpin ? m_fixedZFromSpin->GetValue() : g_gui.GetCurrentFloor();
		const int zTo = m_fixedZToSpin ? m_fixedZToSpin->GetValue() : g_gui.GetCurrentFloor();
		const Position center = canvas->GetCursorPosition();
		if (!BuildFixedAreaBuffer(*editor, center, width, height, zFrom, zTo, buffer, minPos, maxPos, tileCount, itemCount)) {
			wxMessageBox("No tiles found in the fixed area.", "Structure Manager", wxOK | wxICON_INFORMATION, this);
			return;
		}
	} else {
		if (!editor->hasSelection()) {
			wxMessageBox("Select tiles before saving a structure.", "Structure Manager", wxOK | wxICON_INFORMATION, this);
			return;
		}
		if (!BuildSelectionBuffer(*editor, buffer, minPos, maxPos, tileCount, itemCount)) {
			wxMessageBox("Failed to build structure from selection.", "Structure Manager", wxOK | wxICON_ERROR, this);
			return;
		}
	}

	wxString safeName;
	wxString path;
	const bool autoName = m_autoNameCheck && m_autoNameCheck->GetValue();
	if (autoName) {
		wxString base = GetAutoNameBase();
		if (base.empty()) {
			wxMessageBox("Auto name base cannot be empty.", "Structure Manager", wxOK | wxICON_WARNING, this);
			return;
		}
		std::string sanitizedBase = SanitizeFilename(nstr(base));
		wxString safeBase = wxstr(sanitizedBase);
		wxString safePrefix = GetAutoNamePrefix(safeBase);
		int index = GetNextAutoNameIndex(safeBase, categoryPath);
		for (;; ++index) {
			safeName = safePrefix + i2ws(index);
			path = wxFileName(dir, safeName + ".otbm").GetFullPath();
			if (!wxFileExists(path)) {
				break;
			}
		}
	} else {
		wxTextEntryDialog nameDialog(this, "Structure name:", "Save Structure");
		if (nameDialog.ShowModal() != wxID_OK) {
			return;
		}

		wxString name = nameDialog.GetValue();
		name.Trim(true).Trim(false);
		if (name.empty()) {
			wxMessageBox("Structure name cannot be empty.", "Structure Manager", wxOK | wxICON_WARNING, this);
			return;
		}

		std::string sanitized = SanitizeFilename(nstr(name));
		safeName = wxstr(sanitized);
		path = wxFileName(dir, safeName + ".otbm").GetFullPath();

		if (wxFileExists(path)) {
			int ret = wxMessageBox("A structure with this name already exists. Overwrite?", "Structure Manager",
				wxYES_NO | wxICON_WARNING, this);
			if (ret != wxYES) {
				return;
			}
		}
	}

	wxString error;
	if (!SaveStructureFile(buffer, minPos, maxPos, editor->map.getVersion(), path, error)) {
		wxMessageBox("Failed to save structure:\n" + error, "Structure Manager", wxOK | wxICON_ERROR, this);
		return;
	}

	LoadStructures();
	SelectCategoryByPath(categoryPath);
	SelectEntryByName(safeName);
	if (useFixed) {
		int zFrom = m_fixedZFromSpin ? m_fixedZFromSpin->GetValue() : minPos.z;
		int zTo = m_fixedZToSpin ? m_fixedZToSpin->GetValue() : maxPos.z;
		SetStatusText(wxString::Format("Saved '%s' (%d x %d, Z %d-%d, %d tile%s).",
			safeName,
			(maxPos.x - minPos.x + 1),
			(maxPos.y - minPos.y + 1),
			std::min(zFrom, zTo),
			std::max(zFrom, zTo),
			tileCount, tileCount == 1 ? "" : "s"));
	} else {
		SetStatusText(wxString::Format("Saved '%s' (%d tile%s, %d item%s).",
			safeName,
			tileCount, tileCount == 1 ? "" : "s",
			itemCount, itemCount == 1 ? "" : "s"));
	}
	g_gui.SetStatusText("Structure saved.");
	UpdateSaveOptionsUi();
}

void StructureManagerDialog::OnPaste(wxCommandEvent& WXUNUSED(event)) {
	const StructureEntry* entry = GetSelectedEntry(nullptr);
	if (!entry) {
		return;
	}
	StartPasteFromEntry(*entry);
}

void StructureManagerDialog::OnPasteRotationChanged(wxCommandEvent& WXUNUSED(event)) {
	if (!m_pasteRotationChoice) {
		return;
	}

	int desired = m_pasteRotationChoice->GetSelection();
	if (desired == wxNOT_FOUND) {
		desired = 0;
	}

	int delta = (desired - m_currentPasteRotationTurns) % 4;
	if (delta < 0) {
		delta += 4;
	}
	if (delta == 0) {
		return;
	}

	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || !editor->copybuffer.canPaste()) {
		return;
	}

	editor->copybuffer.rotate(delta);
	m_currentPasteRotationTurns = desired;
	g_gui.RefreshView();
}

void StructureManagerDialog::OnDelete(wxCommandEvent& WXUNUSED(event)) {
	int selection = wxNOT_FOUND;
	const StructureEntry* entry = GetSelectedEntry(&selection);
	if (!entry) {
		return;
	}

	int ret = wxMessageBox("Delete structure '" + entry->name + "'?", "Structure Manager",
		wxYES_NO | wxICON_WARNING, this);
	if (ret != wxYES) {
		return;
	}

	if (wxFileExists(entry->path)) {
		wxRemoveFile(entry->path);
	}

	std::vector<std::string> order;
	if (ReadOrderFile(entry->category, order)) {
		const std::string targetLower = as_lower_str(nstr(entry->name));
		std::vector<std::string> cleaned;
		cleaned.reserve(order.size());
		for (const auto& name : order) {
			if (as_lower_str(name) != targetLower) {
				cleaned.push_back(name);
			}
		}
		WriteOrderFile(entry->category, cleaned);
	}

	LoadStructures();
	SelectCategoryByPath(entry->category);
	SetStatusText("Structure deleted.");
	g_gui.SetStatusText("Structure deleted.");
}

void StructureManagerDialog::OnSelectionChanged(wxCommandEvent& WXUNUSED(event)) {
	UpdateSelectionUi();

	if (m_suppressAutoPaste) {
		return;
	}

	const StructureEntry* entry = GetSelectedEntry(nullptr);
	if (entry) {
		StartPasteFromEntry(*entry);
	}
}

void StructureManagerDialog::OnListKeyDown(wxKeyEvent& event) {
	const int key = event.GetKeyCode();
	if (key == WXK_F2) {
		RenameSelectedStructure();
		return;
	}

	if (key == WXK_DELETE || key == WXK_NUMPAD_DELETE) {
		wxCommandEvent dummy;
		OnDelete(dummy);
		return;
	}

	if (key == WXK_UP && event.ControlDown()) {
		SelectAdjacentEntry(-1);
		return;
	}
	if (key == WXK_DOWN && event.ControlDown()) {
		SelectAdjacentEntry(1);
		return;
	}

	event.Skip();
}

void StructureManagerDialog::OnRenameStructure(wxCommandEvent& WXUNUSED(event)) {
	RenameSelectedStructure();
}

void StructureManagerDialog::OnMoveStructureUp(wxCommandEvent& WXUNUSED(event)) {
	MoveSelectedStructure(-1);
}

void StructureManagerDialog::OnMoveStructureDown(wxCommandEvent& WXUNUSED(event)) {
	MoveSelectedStructure(1);
}

void StructureManagerDialog::OnListLeftDown(wxMouseEvent& event) {
	m_listDragActive = false;
	m_listDragStart = wxNOT_FOUND;
	if (m_tutorialActive || !CanReorderCurrentList()) {
		event.Skip();
		return;
	}

	int index = wxNOT_FOUND;
	if (m_list) {
		index = m_list->HitTest(event.GetPosition());
	}
	if (index != wxNOT_FOUND) {
		m_listDragActive = true;
		m_listDragStart = index;
	}
	event.Skip();
}

void StructureManagerDialog::OnListLeftUp(wxMouseEvent& event) {
	if (!m_listDragActive) {
		event.Skip();
		return;
	}

	int target = wxNOT_FOUND;
	if (m_list) {
		target = m_list->HitTest(event.GetPosition());
	}
	if (target != wxNOT_FOUND && m_listDragStart != wxNOT_FOUND && target != m_listDragStart) {
		MoveEntryToIndex(m_listDragStart, target);
	}

	m_listDragActive = false;
	m_listDragStart = wxNOT_FOUND;
	event.Skip();
}

void StructureManagerDialog::OnListMouseMove(wxMouseEvent& event) {
	if (!m_listDragActive || !event.Dragging() || !event.LeftIsDown()) {
		event.Skip();
		return;
	}

	if (!m_list) {
		event.Skip();
		return;
	}

	int index = m_list->HitTest(event.GetPosition());
	if (index != wxNOT_FOUND && index != m_list->GetSelection()) {
		m_list->SetSelection(index);
	}
	event.Skip();
}

void StructureManagerDialog::OnAutoNameToggle(wxCommandEvent& WXUNUSED(event)) {
	UpdateSaveOptionsUi();
}

void StructureManagerDialog::OnAutoNameBaseChanged(wxCommandEvent& WXUNUSED(event)) {
	UpdateSaveOptionsUi();
}

void StructureManagerDialog::OnFixedSizeToggle(wxCommandEvent& WXUNUSED(event)) {
	UpdateSaveOptionsUi();
}

void StructureManagerDialog::MoveSelectedStructure(int delta) {
	if (m_tutorialActive) return;
	if (!m_list) return;
	if (!CanReorderCurrentList()) {
		wxString message = "Clear search to reorder structures.";
		SetStatusText(message);
		g_gui.SetStatusText(message);
		return;
	}

	int selection = m_list->GetSelection();
	if (selection == wxNOT_FOUND) return;
	int target = selection + delta;
	if (target < 0 || target >= static_cast<int>(m_listEntries.size())) return;
	MoveEntryToIndex(selection, target);
}

void StructureManagerDialog::MoveEntryToIndex(int fromIndex, int toIndex) {
	if (m_tutorialActive) return;
	if (!m_list) return;
	if (!CanReorderCurrentList()) return;
	if (fromIndex == toIndex) return;
	if (fromIndex < 0 || toIndex < 0 ||
		fromIndex >= static_cast<int>(m_listEntries.size()) ||
		toIndex >= static_cast<int>(m_listEntries.size())) {
		return;
	}

	std::vector<const StructureEntry*> ordered = m_listEntries;
	const StructureEntry* moved = ordered[static_cast<size_t>(fromIndex)];
	ordered.erase(ordered.begin() + fromIndex);
	ordered.insert(ordered.begin() + toIndex, moved);
	WriteOrderFile(m_currentCategoryPath, ordered);

	RefreshItemList();
	if (m_list) {
		m_list->SetSelection(toIndex);
	}
	UpdateSelectionUi();
}

void StructureManagerDialog::OnRenameCategory(wxCommandEvent& WXUNUSED(event)) {
	RenameSelectedCategory();
}

void StructureManagerDialog::AddCategory(bool asChild) {
	wxString parentPath;
	if (asChild) {
		parentPath = m_currentCategoryPath;
		if (parentPath.empty()) {
			wxMessageBox("Select a category to add a subcategory.", "Structure Manager", wxOK | wxICON_INFORMATION, this);
			return;
		}
		int sep = parentPath.Find('/', true);
		if (sep != wxNOT_FOUND) {
			parentPath = parentPath.Left(sep);
		}
	}

	wxTextEntryDialog dialog(this,
		asChild ? "Subcategory name:" : "Category name:",
		asChild ? "Add Subcategory" : "Add Category");
	if (dialog.ShowModal() != wxID_OK) {
		return;
	}

	wxString name = dialog.GetValue();
	name.Trim(true).Trim(false);
	if (name.empty()) {
		wxMessageBox("Category name cannot be empty.", "Structure Manager", wxOK | wxICON_WARNING, this);
		return;
	}

	std::string sanitized = SanitizeFilename(nstr(name));
	wxString safeName = wxstr(sanitized);
	if (CategoryNameExists(m_categoryPaths, safeName)) {
		wxMessageBox("A category with this name already exists.", "Structure Manager", wxOK | wxICON_INFORMATION, this);
		return;
	}

	wxString newRelativePath = parentPath.empty() ? safeName : (parentPath + "/" + safeName);
	wxString parentDir = BuildCategoryDirectory(m_baseDir, parentPath);
	wxString newDir = BuildCategoryDirectory(parentDir, safeName);

	if (wxDirExists(newDir)) {
		wxMessageBox("Category already exists.", "Structure Manager", wxOK | wxICON_INFORMATION, this);
		return;
	}

	if (!wxFileName::Mkdir(newDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
		wxMessageBox("Failed to create category folder.", "Structure Manager", wxOK | wxICON_ERROR, this);
		return;
	}

	LoadStructures();
	SelectCategoryByPath(newRelativePath);
}

void StructureManagerDialog::OnAddCategory(wxCommandEvent& WXUNUSED(event)) {
	AddCategory(false);
}

void StructureManagerDialog::OnAddSubcategory(wxCommandEvent& WXUNUSED(event)) {
	AddCategory(true);
}

void StructureManagerDialog::OnRemoveCategory(wxCommandEvent& WXUNUSED(event)) {
	if (m_currentCategoryPath.empty()) {
		wxMessageBox("Select a category to remove.", "Structure Manager", wxOK | wxICON_INFORMATION, this);
		return;
	}
	if (m_currentCategoryPath.Find('/') != wxNOT_FOUND) {
		wxMessageBox("Select a top-level category to remove.", "Structure Manager", wxOK | wxICON_INFORMATION, this);
		return;
	}

	const wxString fullPath = BuildCategoryDirectory(m_baseDir, m_currentCategoryPath);
	if (!wxDirExists(fullPath)) {
		wxMessageBox("Category folder not found.", "Structure Manager", wxOK | wxICON_ERROR, this);
		return;
	}

	int ret = wxMessageBox("Remove category '" + m_currentCategoryPath + "' and all its structures?",
		"Structure Manager", wxYES_NO | wxICON_WARNING, this);
	if (ret != wxYES) return;

	if (!wxFileName::Rmdir(fullPath, wxPATH_RMDIR_RECURSIVE)) {
		wxMessageBox("Failed to remove category.", "Structure Manager", wxOK | wxICON_ERROR, this);
		return;
	}

	LoadStructures();
	SelectCategoryByPath("");
	SetStatusText("Category removed.");
	g_gui.SetStatusText("Category removed.");
}

void StructureManagerDialog::OnRemoveSubcategory(wxCommandEvent& WXUNUSED(event)) {
	if (m_currentCategoryPath.empty() || m_currentCategoryPath.Find('/') == wxNOT_FOUND) {
		wxMessageBox("Select a subcategory to remove.", "Structure Manager", wxOK | wxICON_INFORMATION, this);
		return;
	}

	const wxString fullPath = BuildCategoryDirectory(m_baseDir, m_currentCategoryPath);
	if (!wxDirExists(fullPath)) {
		wxMessageBox("Subcategory folder not found.", "Structure Manager", wxOK | wxICON_ERROR, this);
		return;
	}

	int ret = wxMessageBox("Remove subcategory '" + m_currentCategoryPath + "' and all its structures?",
		"Structure Manager", wxYES_NO | wxICON_WARNING, this);
	if (ret != wxYES) return;

	if (!wxFileName::Rmdir(fullPath, wxPATH_RMDIR_RECURSIVE)) {
		wxMessageBox("Failed to remove subcategory.", "Structure Manager", wxOK | wxICON_ERROR, this);
		return;
	}

	wxString parentPath = m_currentCategoryPath;
	int sep = parentPath.Find('/', true);
	if (sep != wxNOT_FOUND) {
		parentPath = parentPath.Left(sep);
	} else {
		parentPath.clear();
	}

	LoadStructures();
	SelectCategoryByPath(parentPath);
	SetStatusText("Subcategory removed.");
	g_gui.SetStatusText("Subcategory removed.");
}

void StructureManagerDialog::OnCategoryChanged(wxTreeEvent& event) {
	wxTreeItemId item = event.GetItem();
	if (!item.IsOk()) return;

	CategoryItemData* data = static_cast<CategoryItemData*>(m_categoryTree->GetItemData(item));
	m_currentCategoryPath = data ? data->categoryPath : wxString();
	RefreshItemList();
}

void StructureManagerDialog::OnSearchChanged(wxCommandEvent& WXUNUSED(event)) {
	RefreshItemList();
}

void StructureManagerDialog::OnHowToUse(wxCommandEvent& WXUNUSED(event)) {
	StartTutorial();
}

void StructureManagerDialog::StartTutorial() {
	m_tutorialActive = true;
	m_tutorialStep = 0;
	m_tutorialLockMove = true;
	m_tutorialLockPos = GetPosition();
	SetTutorialUiEnabled(false);

	if (!m_tutorialPopup) {
		m_tutorialPopup = newd wxPopupWindow(this, wxBORDER_SIMPLE);
		wxPanel* panel = newd wxPanel(m_tutorialPopup, wxID_ANY);
		panel->SetBackgroundColour(wxColour(30, 30, 30));
		panel->SetForegroundColour(*wxWHITE);

		m_tutorialStepLabel = newd wxStaticText(panel, wxID_ANY, "");
		m_tutorialStepLabel->SetForegroundColour(*wxWHITE);
		wxFont bold = m_tutorialStepLabel->GetFont();
		bold.SetWeight(wxFONTWEIGHT_BOLD);
		bold.SetPointSize(std::max(6, bold.GetPointSize() + kTutorialFontDelta));
		m_tutorialStepLabel->SetFont(bold);

		m_tutorialBodyText = newd wxStaticText(panel, wxID_ANY, "");
		m_tutorialBodyText->SetForegroundColour(*wxWHITE);
		wxFont bodyFont = m_tutorialBodyText->GetFont();
		bodyFont.SetPointSize(std::max(6, bodyFont.GetPointSize() + kTutorialFontDelta));
		m_tutorialBodyText->SetFont(bodyFont);
		m_tutorialBodyText->Wrap(kTutorialWrapWidth);

		m_tutorialPrevButton = newd wxButton(panel, wxID_ANY, "Back");
		m_tutorialNextButton = newd wxButton(panel, wxID_ANY, "Next");
		m_tutorialCloseButton = newd wxButton(panel, wxID_ANY, "Close");

		wxFont buttonFont = m_tutorialPrevButton->GetFont();
		buttonFont.SetPointSize(std::max(6, buttonFont.GetPointSize() + kTutorialFontDelta));
		m_tutorialPrevButton->SetFont(buttonFont);
		m_tutorialNextButton->SetFont(buttonFont);
		m_tutorialCloseButton->SetFont(buttonFont);

		m_tutorialPrevButton->Bind(wxEVT_BUTTON, &StructureManagerDialog::OnTutorialPrev, this);
		m_tutorialNextButton->Bind(wxEVT_BUTTON, &StructureManagerDialog::OnTutorialNext, this);
		m_tutorialCloseButton->Bind(wxEVT_BUTTON, &StructureManagerDialog::OnTutorialClose, this);

		wxBoxSizer* navSizer = new wxBoxSizer(wxHORIZONTAL);
		navSizer->Add(m_tutorialPrevButton, 0, wxRIGHT, 4);
		navSizer->Add(m_tutorialNextButton, 0, wxRIGHT, 4);
		navSizer->Add(m_tutorialCloseButton, 0);

		wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
		panelSizer->Add(m_tutorialStepLabel, 0, wxBOTTOM, 4);
		panelSizer->Add(m_tutorialBodyText, 0, wxBOTTOM, 8);
		panelSizer->AddStretchSpacer(1);
		panelSizer->Add(navSizer, 0, wxALIGN_RIGHT | wxBOTTOM, 4);

		wxBoxSizer* outerSizer = new wxBoxSizer(wxVERTICAL);
		outerSizer->Add(panelSizer, 1, wxEXPAND | wxALL, 8);
		panel->SetSizerAndFit(outerSizer);
		panel->SetMinSize(kTutorialMinSize);

		wxBoxSizer* popupSizer = new wxBoxSizer(wxVERTICAL);
		popupSizer->Add(panel, 1, wxEXPAND);
		m_tutorialPopup->SetSizerAndFit(popupSizer);
		m_tutorialPopup->SetMinSize(kTutorialMinSize);
	}

	m_tutorialPopup->Show();
	m_tutorialPopup->Raise();
	UpdateTutorialStep();
}

void StructureManagerDialog::StopTutorial() {
	m_tutorialActive = false;
	m_tutorialLockMove = false;
	ClearTutorialOverlay();
	if (m_tutorialPopup) {
		m_tutorialPopup->Hide();
	}
	SetTutorialUiEnabled(true);
	UpdateSelectionUi();
}

void StructureManagerDialog::UpdateTutorialStep() {
	if (!m_tutorialActive) return;

	const int count = GetTutorialStepCount();
	if (m_tutorialStep < 0) m_tutorialStep = 0;
	else if (m_tutorialStep >= count) m_tutorialStep = count - 1;

	const TutorialStepInfo info = GetTutorialStepInfo(m_tutorialStep);
	if (m_tutorialStepLabel) {
		m_tutorialStepLabel->SetLabel(wxString::Format("Step %d/%d - %s", m_tutorialStep + 1, count, info.title));
	}
	if (m_tutorialBodyText) {
		m_tutorialBodyText->SetLabel(info.body);
		m_tutorialBodyText->Wrap(kTutorialWrapWidth);
	}

	if (m_tutorialPrevButton) m_tutorialPrevButton->Enable(m_tutorialStep > 0);
	if (m_tutorialNextButton) m_tutorialNextButton->SetLabel(m_tutorialStep + 1 < count ? "Next" : "Finish");

	if (m_tutorialPopup) {
		m_tutorialPopup->Fit();
		wxSize desired = m_tutorialPopup->GetSize();
		desired.SetWidth(std::max(desired.x, kTutorialMinSize.x));
		desired.SetHeight(std::max(desired.y, kTutorialMinSize.y));
		m_tutorialPopup->SetSize(desired);
	}
	PositionTutorialPopup();
	RenderTutorialOverlay();
}

void StructureManagerDialog::RenderTutorialOverlay() {
	if (!m_tutorialActive || !IsShown()) return;

	wxClientDC dc(this);
	wxDCOverlay overlaydc(m_tutorialOverlay, &dc);
	overlaydc.Clear();

	wxRect highlight = GetTutorialHighlightRect();
	wxSize clientSize = GetClientSize();
	wxGCDC gcdc(dc);
	const wxColour overlayColor(0, 0, 0, kTutorialOverlayAlpha);
	const wxColour highlightShade(0, 0, 0, kTutorialHighlightAlpha);
	gcdc.SetPen(*wxTRANSPARENT_PEN);

	if (highlight.IsEmpty()) {
		gcdc.SetBrush(wxBrush(overlayColor));
		gcdc.DrawRectangle(0, 0, clientSize.x, clientSize.y);
		return;
	}

	wxRect glowRect = highlight;
	glowRect.Inflate(4);
	glowRect.Intersect(wxRect(0, 0, clientSize.x, clientSize.y));

	auto drawRect = [&](int x, int y, int w, int h) {
		if (w > 0 && h > 0) gcdc.DrawRectangle(x, y, w, h);
	};

	const int left = glowRect.x;
	const int top = glowRect.y;
	const int right = glowRect.x + glowRect.width;
	const int bottom = glowRect.y + glowRect.height;

	gcdc.SetBrush(wxBrush(overlayColor));
	drawRect(0, 0, clientSize.x, top);
	drawRect(0, top, left, clientSize.y - top);
	drawRect(right, top, clientSize.x - right, clientSize.y - top);
	drawRect(left, bottom, right - left, clientSize.y - bottom);

	gcdc.SetBrush(wxBrush(highlightShade));
	drawRect(left, top, right - left, bottom - top);

	gcdc.SetBrush(*wxTRANSPARENT_BRUSH);
	gcdc.SetPen(wxPen(wxColour(255, 205, 80), 3));
	gcdc.DrawRectangle(glowRect);
}

void StructureManagerDialog::ClearTutorialOverlay() {
	if (!IsShown()) return;
	wxClientDC dc(this);
	wxDCOverlay overlaydc(m_tutorialOverlay, &dc);
	overlaydc.Clear();
	m_tutorialOverlay.Reset();
}

void StructureManagerDialog::PositionTutorialPopup() {
	if (!m_tutorialPopup) return;

	wxSize clientSize = GetClientSize();
	wxPoint clientOrigin = ClientToScreen(wxPoint(0, 0));
	wxRect clientScreen(clientOrigin, clientSize);

	wxRect highlight = GetTutorialHighlightRect();
	wxRect highlightScreen = highlight;
	highlightScreen.Offset(clientOrigin);

	wxSize popupSize = m_tutorialPopup->GetSize();
	const int margin = 12;
	wxPoint pos(0, 0);

	if (highlight.IsEmpty()) {
		pos.x = clientScreen.x + (clientScreen.width - popupSize.x) / 2;
		pos.y = clientScreen.y + (clientScreen.height - popupSize.y) / 2;
	} else if (highlightScreen.GetRight() + margin + popupSize.x < clientScreen.x + clientScreen.width) {
		pos.x = highlightScreen.GetRight() + margin;
		pos.y = highlightScreen.GetTop();
	} else if (highlightScreen.GetLeft() - margin - popupSize.x > clientScreen.x) {
		pos.x = highlightScreen.GetLeft() - margin - popupSize.x;
		pos.y = highlightScreen.GetTop();
	} else if (highlightScreen.GetBottom() + margin + popupSize.y < clientScreen.y + clientScreen.height) {
		pos.x = highlightScreen.GetLeft();
		pos.y = highlightScreen.GetBottom() + margin;
	} else {
		pos.x = highlightScreen.GetLeft();
		pos.y = std::max(clientScreen.y + margin, highlightScreen.GetTop() - margin - popupSize.y);
	}

	if (pos.x + popupSize.x > clientScreen.x + clientScreen.width) {
		pos.x = std::max(clientScreen.x + margin, clientScreen.x + clientScreen.width - popupSize.x - margin);
	}
	if (pos.y + popupSize.y > clientScreen.y + clientScreen.height) {
		pos.y = std::max(clientScreen.y + margin, clientScreen.y + clientScreen.height - popupSize.y - margin);
	}
	if (pos.x < clientScreen.x + margin) pos.x = clientScreen.x + margin;
	if (pos.y < clientScreen.y + margin) pos.y = clientScreen.y + margin;

	m_tutorialPopup->Move(pos);
}

wxRect StructureManagerDialog::GetTutorialHighlightRect() const {
	wxWindow* target = nullptr;
	switch (m_tutorialStep) {
		case 0: target = m_addCategoryButton; break;
		case 1: target = m_saveButton; break;
		case 2: target = m_list; break;
		case 3: target = m_pasteButton; break;
		default: break;
	}

	if (!target || !target->IsShownOnScreen()) return wxRect();

	wxPoint screenTopLeft = target->ClientToScreen(wxPoint(0, 0));
	wxPoint clientTopLeft = ScreenToClient(screenTopLeft);
	return wxRect(clientTopLeft, target->GetSize());
}

StructureManagerDialog::TutorialStepInfo StructureManagerDialog::GetTutorialStepInfo(int step) const {
	switch (step) {
		case 0: return {"Create a category", "Click \"Add Category\" to create your first category."};
		case 1: return {"Save a structure", "On the map, select the tiles you want and click \"Save Current Selection...\"."};
		case 2: return {"Choose a structure", "Select a structure in the list to view its details."};
		case 3: return {"Paste on the map", "Click \"Paste\" to place the structure on the map."};
		default: return {"Tutorial", "Use the buttons to navigate."};
	}
}

int StructureManagerDialog::GetTutorialStepCount() const {
	return 4;
}

void StructureManagerDialog::SetTutorialUiEnabled(bool enabled) {
	if (m_searchCtrl) m_searchCtrl->Enable(enabled);
	if (m_categoryTree) m_categoryTree->Enable(enabled);
	if (m_list) m_list->Enable(enabled);
	if (m_keepPasteCheck) m_keepPasteCheck->Enable(enabled);
	if (m_addCategoryButton) m_addCategoryButton->Enable(enabled);
	if (m_addSubcategoryButton) m_addSubcategoryButton->Enable(enabled);
	if (m_removeCategoryButton) m_removeCategoryButton->Enable(enabled);
	if (m_removeSubcategoryButton) m_removeSubcategoryButton->Enable(enabled);
	if (m_renameCategoryButton) m_renameCategoryButton->Enable(enabled);
	if (m_renameStructureButton) m_renameStructureButton->Enable(enabled);
	if (m_moveStructureUpButton) m_moveStructureUpButton->Enable(enabled);
	if (m_moveStructureDownButton) m_moveStructureDownButton->Enable(enabled);
	if (m_saveButton) m_saveButton->Enable(enabled);
	if (m_fixedSizeCheck) m_fixedSizeCheck->Enable(enabled);
	if (m_fixedWidthSpin) m_fixedWidthSpin->Enable(enabled);
	if (m_fixedHeightSpin) m_fixedHeightSpin->Enable(enabled);
	if (m_fixedZFromSpin) m_fixedZFromSpin->Enable(enabled);
	if (m_fixedZToSpin) m_fixedZToSpin->Enable(enabled);
	if (m_autoNameCheck) m_autoNameCheck->Enable(enabled);
	if (m_autoNameBaseCtrl) m_autoNameBaseCtrl->Enable(enabled);
	if (m_pasteButton) m_pasteButton->Enable(enabled);
	if (m_deleteButton) m_deleteButton->Enable(enabled);
	if (m_helpButton) m_helpButton->Enable(enabled);
}

void StructureManagerDialog::OnTutorialPrev(wxCommandEvent& WXUNUSED(event)) {
	if (m_tutorialStep > 0) {
		--m_tutorialStep;
		UpdateTutorialStep();
	}
}

void StructureManagerDialog::OnTutorialNext(wxCommandEvent& WXUNUSED(event)) {
	if (m_tutorialStep + 1 < GetTutorialStepCount()) {
		++m_tutorialStep;
		UpdateTutorialStep();
	} else {
		StopTutorial();
	}
}

void StructureManagerDialog::OnTutorialClose(wxCommandEvent& WXUNUSED(event)) {
	StopTutorial();
}

void StructureManagerDialog::OnPaint(wxPaintEvent& event) {
	event.Skip();
	if (m_tutorialActive) {
		RenderTutorialOverlay();
	}
}

void StructureManagerDialog::OnSize(wxSizeEvent& event) {
	event.Skip();
	if (m_tutorialActive) {
		ClearTutorialOverlay();
		PositionTutorialPopup();
		RenderTutorialOverlay();
	}
}

void StructureManagerDialog::OnMove(wxMoveEvent& event) {
	if (m_tutorialActive && m_tutorialLockMove) {
		if (!m_tutorialMoveGuard) {
			m_tutorialMoveGuard = true;
			Move(m_tutorialLockPos);
			m_tutorialMoveGuard = false;
		}
		return;
	}

	event.Skip();
	if (m_tutorialActive) {
		ClearTutorialOverlay();
		PositionTutorialPopup();
		RenderTutorialOverlay();
	}
}

void StructureManagerDialog::OnCharHook(wxKeyEvent& event) {
	const int key = event.GetKeyCode();
	if (key == WXK_F2) {
		wxWindow* focus = wxWindow::FindFocus();
		if (focus && m_categoryTree && (focus == m_categoryTree || focus->GetParent() == m_categoryTree)) {
			RenameSelectedCategory();
			return;
		}
	}
	if (event.ControlDown() && event.ShiftDown()) {
		if (key == 'S' || key == 's') {
			wxCommandEvent dummy;
			OnSaveSelection(dummy);
			return;
		}
	}

	event.Skip();
}

void StructureManagerDialog::OnClose(wxCloseEvent& WXUNUSED(event)) {
	Hide();
	StopTutorial();
	if (m_previewPanel) {
		m_previewPanel->Hide();
	}
	g_gui.DestroyStructureManagerDialog();
}

bool StructureManagerDialog::HandleGlobalHotkey(wxKeyEvent& event) {
	if (!s_active || !s_active->IsShown()) {
		return false;
	}
	return s_active->HandleGlobalHotkeyInternal(event);
}

bool StructureManagerDialog::RotatePaste() {
	if (!s_active || !s_active->IsShown() || s_active->m_tutorialActive) {
		return false;
	}
	if (!s_active->m_pasteRotationChoice) {
		return false;
	}
	Editor* editor = g_gui.GetCurrentEditor();
	if (!editor || !editor->copybuffer.canPaste()) {
		return false;
	}
	const int count = static_cast<int>(s_active->m_pasteRotationChoice->GetCount());
	if (count <= 0) {
		return false;
	}
	int desired = s_active->m_pasteRotationChoice->GetSelection();
	if (desired == wxNOT_FOUND) {
		desired = 0;
	}
	desired = (desired + 1) % count;
	s_active->m_pasteRotationChoice->SetSelection(desired);
	wxCommandEvent dummy;
	s_active->OnPasteRotationChanged(dummy);
	return true;
}

bool StructureManagerDialog::CanRotatePaste() {
	if (!s_active || !s_active->IsShown() || s_active->m_tutorialActive) {
		return false;
	}
	if (!s_active->m_pasteRotationChoice) {
		return false;
	}
	Editor* editor = g_gui.GetCurrentEditor();
	return editor && editor->copybuffer.canPaste();
}

bool StructureManagerDialog::IsKeepPasteActive() {
	if (!s_active || !s_active->IsShown()) {
		return false;
	}
	return s_active->m_keepPasteCheck && s_active->m_keepPasteCheck->IsChecked();
}

bool StructureManagerDialog::HandleGlobalHotkeyInternal(wxKeyEvent& event) {
	if (m_tutorialActive) {
		return false;
	}
	const int key = event.GetKeyCode();
	if (event.ControlDown() && event.ShiftDown()) {
		if (key == 'S' || key == 's') {
			wxCommandEvent dummy;
			OnSaveSelection(dummy);
			return true;
		}
	}
	if (key == WXK_PAGEUP || key == WXK_PAGEDOWN) {
		SelectAdjacentEntry(key == WXK_PAGEUP ? -1 : 1);
		return true;
	}
	if (event.ControlDown()) {
		if (key == WXK_UP || key == WXK_NUMPAD_UP) {
			SelectAdjacentEntry(-1);
			return true;
		}
		if (key == WXK_DOWN || key == WXK_NUMPAD_DOWN) {
			SelectAdjacentEntry(1);
			return true;
		}
	}
	// Z key rotates the paste buffer
	if (key == 'Z' || key == 'z') {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor || !editor->copybuffer.canPaste() || !m_pasteRotationChoice) {
			return false;
		}
		const int count = static_cast<int>(m_pasteRotationChoice->GetCount());
		if (count <= 0) {
			return false;
		}
		int desired = m_pasteRotationChoice->GetSelection();
		if (desired == wxNOT_FOUND) {
			desired = 0;
		}
		desired = (desired + 1) % count;
		m_pasteRotationChoice->SetSelection(desired);
		wxCommandEvent dummy;
		OnPasteRotationChanged(dummy);
		return true;
	}
	return false;
}
