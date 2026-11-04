//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "app/definitions.h"
#include "editor/map_layers.h"

#include "editor/editor.h"
#include "editor/editor_tabs.h"
#include "map/map.h"
#include "rendering/ui/map_display.h"
#include "ui/gui.h"
#include "ui/main_frame.h"
#include "ui/map_tab.h"

#include <algorithm>
#include <memory>

namespace {
	std::vector<std::unique_ptr<MapLayerGroup>> groups;
	std::function<void()> listener;

	// The camera of the tab that was just left, waiting for the tab that comes to the front.
	struct CameraSnapshot {
		bool valid = false;
		const Editor* editor = nullptr;
		const MapLayerGroup* group = nullptr;
		int offset_x = 0;
		int offset_y = 0;
		double zoom = 1.0;
		int floor = GROUND_LAYER;
		int scroll_x = 0;
		int scroll_y = 0;
		MinimapViewportState minimap; // kept per tab too
	};
	CameraSnapshot snapshot;
}

namespace MapLayers {

MapLayerGroup& CreateGroup(const wxString& name) {
	groups.push_back(std::make_unique<MapLayerGroup>());
	groups.back()->name = name;
	return *groups.back();
}

void DiscardEmptyGroups() {
	std::erase_if(groups, [](const std::unique_ptr<MapLayerGroup>& group) {
		return group->layers.empty();
	});
}

MapLayerGroup* FindGroup(const Editor* editor) {
	if (!editor) {
		return nullptr;
	}
	for (const auto& group : groups) {
		for (const MapLayer& layer : group->layers) {
			if (layer.editor == editor) {
				return group.get();
			}
		}
	}
	return nullptr;
}

MapLayer* FindLayer(const Editor* editor) {
	if (!editor) {
		return nullptr;
	}
	for (const auto& group : groups) {
		for (MapLayer& layer : group->layers) {
			if (layer.editor == editor) {
				return &layer;
			}
		}
	}
	return nullptr;
}

bool IsLayer(const Editor* editor) {
	return FindLayer(editor) != nullptr;
}

void CollectReferences(const Editor& active, std::vector<MapLayerReference>& out) {
	out.clear();
	MapLayerGroup* group = FindGroup(&active);
	const MapLayer* active_layer = FindLayer(&active);
	if (!group || !active_layer) {
		return;
	}
	const int alpha = std::clamp(group->inactive_opacity, 0, 100) * 255 / 100;
	if (alpha <= 0) {
		return;
	}

	bool above = false;
	for (MapLayer& layer : group->layers) {
		if (layer.editor == &active) {
			above = true;
			continue;
		}
		if (!layer.visible || !layer.editor) {
			continue;
		}
		if (layer.offset_x != active_layer->offset_x || layer.offset_y != active_layer->offset_y) {
			continue;
		}
		out.push_back(MapLayerReference { &layer.editor->map, alpha, above });
	}
}

void CollectDrawnMaps(const Editor& active, std::vector<const Map*>& out) {
	out.clear();
	const MapLayerGroup* group = FindGroup(&active);
	const MapLayer* active_layer = FindLayer(&active);
	if (!group || !active_layer) {
		out.push_back(&active.map);
		return;
	}
	const bool others_shown = group->inactive_opacity > 0;
	for (const MapLayer& layer : group->layers) {
		if (layer.editor == &active) {
			out.push_back(&active.map);
			continue;
		}
		if (!others_shown || !layer.visible || !layer.editor) {
			continue;
		}
		if (layer.offset_x != active_layer->offset_x || layer.offset_y != active_layer->offset_y) {
			continue;
		}
		out.push_back(&layer.editor->map);
	}
}

bool GetGroupExtent(const Editor& editor, int& width, int& height) {
	const MapLayerGroup* group = FindGroup(&editor);
	if (!group) {
		return false;
	}
	for (const MapLayer& layer : group->layers) {
		if (layer.editor) {
			width = std::max(width, layer.editor->map.getWidth());
			height = std::max(height, layer.editor->map.getHeight());
		}
	}
	return true;
}

MapTab* FindTab(const Editor* editor) {
	const MapLayer* layer = FindLayer(editor);
	return layer ? layer->tab : nullptr;
}

namespace {
	// First page showing a layer of the group (normally the only one).
	int GroupPage(const MapLayerGroup& group) {
		for (int index = 0; index < g_gui.tabbook->GetTabCount(); ++index) {
			auto* tab = dynamic_cast<MapTab*>(g_gui.tabbook->GetTab(index));
			if (!tab) {
				continue;
			}
			for (const MapLayer& layer : group.layers) {
				if (layer.editor == tab->GetEditor()) {
					return index;
				}
			}
		}
		return wxNOT_FOUND;
	}
}

void Activate(const Editor* editor) {
	MapLayer* layer = FindLayer(editor);
	if (!layer || !layer->tab || !g_gui.tabbook) {
		return;
	}
	MapTabbook& book = *g_gui.tabbook;

	const int page = book.GetTabIndex(layer->tab->GetWindow());
	if (page != wxNOT_FOUND) {
		if (book.GetSelection() != page) {
			book.SetFocusedTab(page);
		}
		return;
	}

	const MapLayerGroup* group = FindGroup(editor);
	const int group_page = group ? GroupPage(*group) : wxNOT_FOUND;
	if (group_page == wxNOT_FOUND) {
		book.InsertTab(book.GetTabCount(), layer->tab, true);
		return;
	}

	// In front of the group's page, selected, and only then the old page leaves: the camera
	// hand-over (page changing/changed) sees the tab that is left and the one that comes in.
	auto* old_tab = dynamic_cast<MapTab*>(book.GetTab(group_page));
	const MapLayer* old_layer = old_tab ? FindLayer(old_tab->GetEditor()) : nullptr;
	const bool own_tab = old_layer && old_layer->tab == old_tab;
	if (old_tab) {
		RememberCamera(*old_tab);
	}
	book.InsertTab(group_page, layer->tab, true);
	if (own_tab) {
		book.DetachTab(group_page + 1);
	} else {
		// An extra view of a layer ("New View"): nothing would bring it back, so it closes.
		book.DeleteTab(group_page + 1);
	}
}

bool CloseGroup(const Editor* editor) {
	for (const Editor* mate : GroupMates(editor)) {
		if (!IsLayer(mate)) {
			continue;
		}
		Activate(mate);
		// Asks about unsaved changes and closes the page in front; Cancel stops here.
		if (!g_gui.root->DoQuerySave(true)) {
			return false;
		}
	}
	return true;
}

std::vector<const Editor*> GroupMates(const Editor* editor) {
	std::vector<const Editor*> mates;
	const MapLayerGroup* group = FindGroup(editor);
	if (!group) {
		return mates;
	}
	// Starting at the editor, then the layers after it: the next one takes over a closed page.
	const auto& layers = group->layers;
	const auto start = std::find_if(layers.begin(), layers.end(), [editor](const MapLayer& layer) {
		return layer.editor == editor;
	});
	const size_t offset = static_cast<size_t>(start - layers.begin());
	for (size_t i = 0; i < layers.size(); ++i) {
		mates.push_back(layers[(offset + i) % layers.size()].editor);
	}
	return mates;
}

void KeepGroupShown(const std::vector<const Editor*>& group_mates, int page_index) {
	if (group_mates.empty() || !g_gui.tabbook) {
		return;
	}
	// The pointers are only compared: the closed layer's editor is gone by now.
	for (int index = 0; index < g_gui.tabbook->GetTabCount(); ++index) {
		auto* tab = dynamic_cast<MapTab*>(g_gui.tabbook->GetTab(index));
		if (tab && std::find(group_mates.begin(), group_mates.end(), tab->GetEditor()) != group_mates.end()) {
			return; // the group still has a page
		}
	}
	for (const Editor* mate : group_mates) {
		const MapLayer* layer = FindLayer(mate);
		if (layer && layer->tab) {
			g_gui.tabbook->InsertTab(page_index, layer->tab, true);
			return;
		}
	}
}

void OnTabDestroyed(const MapTab* tab) {
	// Runs during shutdown too, so the notebook is not touched here.
	for (const auto& group : groups) {
		for (MapLayer& layer : group->layers) {
			if (layer.tab == tab) {
				layer.tab = nullptr;
			}
		}
	}
}

void RememberCamera(MapTab& tab) {
	const MapLayer* layer = FindLayer(tab.GetEditor());
	if (!layer) {
		snapshot.valid = false;
		return;
	}
	snapshot.valid = true;
	snapshot.editor = tab.GetEditor();
	snapshot.group = FindGroup(tab.GetEditor());
	snapshot.offset_x = layer->offset_x;
	snapshot.offset_y = layer->offset_y;
	snapshot.zoom = tab.GetCanvas()->GetZoom();
	snapshot.floor = tab.GetCanvas()->GetFloor();
	tab.GetViewStart(&snapshot.scroll_x, &snapshot.scroll_y);
	snapshot.minimap = tab.GetMinimapViewportState();
}

void ApplyCamera(MapTab& tab) {
	// One hand-over per switch: a tab closed without a "changing" event must not reuse it.
	const CameraSnapshot from = snapshot;
	snapshot.valid = false;

	const MapLayer* layer = FindLayer(tab.GetEditor());
	if (!from.valid || !layer || from.editor == tab.GetEditor() || FindGroup(tab.GetEditor()) != from.group) {
		return;
	}

	MapCanvas* canvas = tab.GetCanvas();
	canvas->SetZoom(from.zoom);
	canvas->ChangeFloor(from.floor);
	// Layers the server shifts are edited in their own coordinates: same world spot.
	tab.Scroll(from.scroll_x + (from.offset_x - layer->offset_x) * TILE_SIZE, from.scroll_y + (from.offset_y - layer->offset_y) * TILE_SIZE);
	// The minimap framing too; a shifted layer gets the camera's spot back instead.
	if (from.offset_x == layer->offset_x && from.offset_y == layer->offset_y) {
		tab.GetMinimapViewportState() = from.minimap;
	} else {
		tab.ResetMinimapViewportToCurrentView();
	}
	g_gui.UpdateMinimap(true);
	canvas->UpdatePositionStatus();
	canvas->Refresh();
}

void OnEditorDestroyed(const Editor* editor) {
	if (snapshot.editor == editor) {
		snapshot.valid = false;
	}
	bool removed = false;
	for (const auto& group : groups) {
		removed |= std::erase_if(group->layers, [editor](const MapLayer& layer) {
			return layer.editor == editor;
		}) > 0;
	}
	if (!removed) {
		return;
	}
	DiscardEmptyGroups();
	// No redraw from here: this also runs while the editor is shutting down.
	NotifyListener();
}

void NotifyChanged() {
	g_gui.RefreshView();
	g_gui.UpdateMinimap(true);
	NotifyListener();
}

void NotifyListener() {
	if (listener) {
		listener();
	}
}

void SetListener(std::function<void()> new_listener) {
	listener = std::move(new_listener);
}

}
