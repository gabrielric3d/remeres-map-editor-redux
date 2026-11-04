//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_EDITOR_MAP_LAYERS_H_
#define RME_EDITOR_MAP_LAYERS_H_

#include <wx/string.h>

#include <functional>
#include <vector>

class Editor;
class Map;
class MapTab;

/**
 * Map layers: several maps open at once and drawn together while each one stays its own
 * file, with its own undo history and Save (a map profile opened "as layers").
 * Every layer has an ordinary map tab, but a group shows a single page in the tab bar: the
 * active layer's. The others are detached (alive, hidden) and the active one draws the
 * visible ones around itself. Picking another layer swaps it into that page and keeps the
 * camera where it was. Brushes, selection and Save act on the active layer only.
 */
struct MapLayer {
	Editor* editor = nullptr; // owned by its map tab
	MapTab* tab = nullptr; // the layer's own tab, a page only while the layer is active
	wxString name;
	// Where the server places this map (maps.xml offsetX/offsetY). A layer is edited in its own
	// coordinates; only the layers placed at the active layer's offset are drawn around it.
	int offset_x = 0;
	int offset_y = 0;
	bool visible = true;
};

struct MapLayerGroup {
	wxString name;
	// Drawing order: a layer covers the ones before it where they overlap.
	std::vector<MapLayer> layers;
	// Opacity of the layers that are not the active one, in percent.
	int inactive_opacity = 100;
};

// Another layer of the active layer's group, as the renderer draws it.
struct MapLayerReference {
	Map* map = nullptr;
	int alpha = 255;
	bool above = false; // drawn after the active layer
};

namespace MapLayers {
	MapLayerGroup& CreateGroup(const wxString& name);
	// Drops groups left without layers (e.g. every map of a profile failed to open).
	void DiscardEmptyGroups();

	MapLayerGroup* FindGroup(const Editor* editor);
	MapLayer* FindLayer(const Editor* editor);
	bool IsLayer(const Editor* editor);

	// Visible layers of the editor's group other than the editor itself, in drawing order.
	void CollectReferences(const Editor& active, std::vector<MapLayerReference>& out);
	// Every map drawn while that editor is in front, itself included, in drawing order (just
	// the editor's own map when it is not a layer). For the minimap and the in-game preview.
	void CollectDrawnMaps(const Editor& active, std::vector<const Map*>& out);

	// Biggest map size in the editor's group. Every layer scrolls over that range, so the
	// camera can move from one layer to another. false when the editor is not a layer.
	bool GetGroupExtent(const Editor& editor, int& width, int& height);

	// The layer's own tab, a page or detached.
	MapTab* FindTab(const Editor* editor);
	// Makes the layer the active one: its tab takes the group's page and comes to the front.
	void Activate(const Editor* editor);

	// Closes every layer of the editor's group, asking about unsaved changes one by one.
	// false when the user cancelled (the layers not reached yet stay open).
	bool CloseGroup(const Editor* editor);

	// MapTabbook::DeleteTab: GroupMates before a page goes, KeepGroupShown right after it --
	// when that was the group's page, the next layer still open takes its place.
	std::vector<const Editor*> GroupMates(const Editor* editor);
	void KeepGroupShown(const std::vector<const Editor*>& group_mates, int page_index);

	// Called by ~MapTab: forget the tab (another view of the same map may take over).
	void OnTabDestroyed(const MapTab* tab);

	// Camera hand-over between the tabs of a group: RememberCamera when a tab is about to be
	// left, ApplyCamera on the tab that comes to the front (same zoom, floor and place).
	void RememberCamera(MapTab& tab);
	void ApplyCamera(MapTab& tab);

	// Called by ~Editor: the layer leaves its group and an empty group goes away.
	void OnEditorDestroyed(const Editor* editor);

	// Visibility, order or opacity changed: redraw the view and tell the listener.
	void NotifyChanged();
	// Tells the listener only (tab switched, a layer was edited or saved).
	void NotifyListener();
	// One listener, the layers panel. Pass an empty function to remove it.
	void SetListener(std::function<void()> listener);
}

#endif // RME_EDITOR_MAP_LAYERS_H_
