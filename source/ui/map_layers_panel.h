//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_UI_MAP_LAYERS_PANEL_H_
#define RME_UI_MAP_LAYERS_PANEL_H_

#include "app/main.h"

class wxCheckListBox;
class wxSlider;
struct MapLayerGroup;

/**
 * Dockable "Map Layers" panel: the layers of the group the current map tab belongs to.
 * Clicking a layer makes it the active one (its tab comes to the front), the check box
 * shows or hides it, and the slider sets how opaque the other layers are drawn.
 */
class MapLayersPanel : public wxPanel {
public:
	explicit MapLayersPanel(wxWindow* parent);
	~MapLayersPanel() override;

	static MapLayersPanel* Get() {
		return instance;
	}
	static void ShowPane();
	static void TogglePane();

	void RefreshFromState();

private:
	MapLayerGroup* CurrentGroup() const;
	void QueueRefresh();
	void UpdateButtons();

	void OnSelectLayer(wxCommandEvent&);
	void OnToggleLayer(wxCommandEvent&);
	void OnMoveLayer(int direction);
	void OnOpacity(wxCommandEvent&);
	void OnSaveChanged(wxCommandEvent&);
	void OnCloseLayers(wxCommandEvent&);
	void OnOpenProfile(wxCommandEvent&);

	static MapLayersPanel* instance;

	bool updating = false;
	bool refresh_pending = false;

	wxStaticText* group_label;
	wxCheckListBox* layer_list;
	wxButton* up_button;
	wxButton* down_button;
	wxStaticText* opacity_label;
	wxSlider* opacity_slider;
	wxButton* save_button;
	wxButton* close_button;
	wxStaticText* hint_label;
	wxButton* open_button;
};

#endif // RME_UI_MAP_LAYERS_PANEL_H_
