//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/map_layers_panel.h"

#include "editor/editor.h"
#include "editor/map_layers.h"
#include "editor/persistence/editor_persistence.h"
#include "map/map.h"
#include "ui/dialogs/map_profiles_dialog.h"
#include "ui/gui.h"
#include "ui/main_frame.h"
#include "ui/managers/status_manager.h"
#include "ui/map_tab.h"
#include "util/image_manager.h"

#include <wx/checklst.h>
#include <wx/slider.h>

MapLayersPanel* MapLayersPanel::instance = nullptr;

MapLayersPanel::MapLayersPanel(wxWindow* parent) :
	wxPanel(parent, wxID_ANY) {
	instance = this;

	wxSizer* sizer = newd wxBoxSizer(wxVERTICAL);

	group_label = newd wxStaticText(this, wxID_ANY, "");
	group_label->SetFont(group_label->GetFont().Bold());
	sizer->Add(group_label, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

	layer_list = newd wxCheckListBox(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 220));
	layer_list->SetToolTip("Click a layer to edit it: it takes the group's tab, the camera stays put.\n"
		"Unchecked layers are not drawn around the active one.\n"
		"Lower layers in the list cover the ones above them where they overlap.");
	sizer->Add(layer_list, 1, wxEXPAND | wxALL, 6);

	wxSizer* order_sizer = newd wxBoxSizer(wxHORIZONTAL);
	up_button = newd wxButton(this, wxID_ANY, "Up");
	up_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_ARROW_UP, wxSize(16, 16)));
	up_button->SetToolTip("Draw the active layer earlier (under more layers).");
	order_sizer->Add(up_button, 1, wxRIGHT, 4);
	down_button = newd wxButton(this, wxID_ANY, "Down");
	down_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_ARROW_DOWN, wxSize(16, 16)));
	down_button->SetToolTip("Draw the active layer later (over more layers).");
	order_sizer->Add(down_button, 1);
	sizer->Add(order_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

	opacity_label = newd wxStaticText(this, wxID_ANY, "Other layers: 100%");
	sizer->Add(opacity_label, 0, wxLEFT | wxRIGHT | wxTOP, 6);
	opacity_slider = newd wxSlider(this, wxID_ANY, 100, 0, 100);
	opacity_slider->SetToolTip("How opaque the layers that are not being edited are drawn (0% hides them).");
	sizer->Add(opacity_slider, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

	wxSizer* file_sizer = newd wxBoxSizer(wxHORIZONTAL);
	save_button = newd wxButton(this, wxID_ANY, "Save changed");
	save_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_FLOPPY_DISK, wxSize(16, 16)));
	save_button->SetToolTip("Save every layer with unsaved changes, each to its own file.");
	file_sizer->Add(save_button, 1, wxRIGHT, 4);
	close_button = newd wxButton(this, wxID_ANY, "Close layers");
	close_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_XMARK, wxSize(16, 16)));
	close_button->SetToolTip("Close every layer of this group (asks before dropping unsaved changes).");
	file_sizer->Add(close_button, 1);
	sizer->Add(file_sizer, 0, wxEXPAND | wxALL, 6);

	hint_label = newd wxStaticText(this, wxID_ANY,
		"No layers here. Open a map profile with \"Open as layers\"\n"
		"to edit several maps at once, each in its own file.");
	sizer->Add(hint_label, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
	open_button = newd wxButton(this, wxID_ANY, "Open Map Profile...");
	open_button->SetBitmap(IMAGE_MANAGER.GetBitmap(ICON_LAYER_GROUP, wxSize(16, 16)));
	sizer->Add(open_button, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

	SetSizer(sizer);

	layer_list->Bind(wxEVT_LISTBOX, &MapLayersPanel::OnSelectLayer, this);
	layer_list->Bind(wxEVT_CHECKLISTBOX, &MapLayersPanel::OnToggleLayer, this);
	up_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnMoveLayer(-1); });
	down_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnMoveLayer(1); });
	opacity_slider->Bind(wxEVT_SLIDER, &MapLayersPanel::OnOpacity, this);
	save_button->Bind(wxEVT_BUTTON, &MapLayersPanel::OnSaveChanged, this);
	close_button->Bind(wxEVT_BUTTON, &MapLayersPanel::OnCloseLayers, this);
	open_button->Bind(wxEVT_BUTTON, &MapLayersPanel::OnOpenProfile, this);

	MapLayers::SetListener([this]() { QueueRefresh(); });
	RefreshFromState();
}

MapLayersPanel::~MapLayersPanel() {
	MapLayers::SetListener({});
	if (instance == this) {
		instance = nullptr;
	}
}

void MapLayersPanel::ShowPane() {
	if (!instance || !g_gui.aui_manager) {
		return;
	}
	wxAuiPaneInfo& info = g_gui.aui_manager->GetPane(instance);
	if (!info.IsOk()) {
		return;
	}
	if (!info.IsShown()) {
		info.Show();
		g_gui.aui_manager->Update();
	}
	instance->RefreshFromState();
}

void MapLayersPanel::TogglePane() {
	if (!instance || !g_gui.aui_manager) {
		return;
	}
	wxAuiPaneInfo& info = g_gui.aui_manager->GetPane(instance);
	if (!info.IsOk()) {
		return;
	}
	if (info.IsShown()) {
		info.Hide();
		g_gui.aui_manager->Update();
	} else {
		ShowPane();
	}
}

MapLayerGroup* MapLayersPanel::CurrentGroup() const {
	MapTab* tab = g_gui.GetCurrentMapTab();
	return tab ? MapLayers::FindGroup(tab->GetEditor()) : nullptr;
}

void MapLayersPanel::QueueRefresh() {
	// Editors report every change; one refresh per event loop round is plenty.
	if (refresh_pending) {
		return;
	}
	refresh_pending = true;
	CallAfter([this]() {
		refresh_pending = false;
		RefreshFromState();
	});
}

void MapLayersPanel::RefreshFromState() {
	MapTab* tab = g_gui.GetCurrentMapTab();
	const Editor* active = tab ? tab->GetEditor() : nullptr;
	const MapLayerGroup* group = MapLayers::FindGroup(active);
	const MapLayer* active_layer = MapLayers::FindLayer(active);

	updating = true;
	if (!group || !active_layer) {
		group_label->SetLabel("Map Layers");
		layer_list->Clear();
	} else {
		group_label->SetLabel(wxString::Format("%s (%zu layers)", group->name, group->layers.size()));

		if (layer_list->GetCount() != group->layers.size()) {
			layer_list->Clear();
			for (size_t i = 0; i < group->layers.size(); ++i) {
				layer_list->Append("");
			}
		}

		int active_index = wxNOT_FOUND;
		for (size_t i = 0; i < group->layers.size(); ++i) {
			const MapLayer& layer = group->layers[i];
			const bool is_active = layer.editor == active;
			wxString label = is_active ? wxString(wxUniChar(0x25B6)) + " " : wxString("     ");
			label += layer.name;
			if (layer.editor && layer.editor->map.hasChanged()) {
				label += " *";
			}
			if (layer.offset_x != active_layer->offset_x || layer.offset_y != active_layer->offset_y) {
				// Another server offset: edited in its own coordinates, not drawn around this one.
				label += wxString::Format("  [offset %d, %d]", layer.offset_x, layer.offset_y);
			}
			const unsigned int index = static_cast<unsigned int>(i);
			if (layer_list->GetString(index) != label) {
				layer_list->SetString(index, label);
			}
			if (layer_list->IsChecked(index) != layer.visible) {
				layer_list->Check(index, layer.visible);
			}
			if (is_active) {
				active_index = static_cast<int>(i);
			}
		}
		if (layer_list->GetSelection() != active_index) {
			layer_list->SetSelection(active_index);
		}

		opacity_slider->SetValue(group->inactive_opacity);
		opacity_label->SetLabel(wxString::Format("Other layers: %d%%", group->inactive_opacity));
	}
	updating = false;
	UpdateButtons();
}

void MapLayersPanel::UpdateButtons() {
	const MapLayerGroup* group = CurrentGroup();
	const bool has_group = group != nullptr;
	const int selection = layer_list->GetSelection();
	const int count = static_cast<int>(layer_list->GetCount());

	bool any_changed = false;
	if (group) {
		for (const MapLayer& layer : group->layers) {
			any_changed |= layer.editor && layer.editor->map.hasChanged();
		}
	}

	group_label->Show(has_group);
	layer_list->Show(has_group);
	up_button->Show(has_group);
	down_button->Show(has_group);
	opacity_label->Show(has_group);
	opacity_slider->Show(has_group);
	save_button->Show(has_group);
	close_button->Show(has_group);
	hint_label->Show(!has_group);
	open_button->Show(!has_group);

	up_button->Enable(has_group && selection > 0);
	down_button->Enable(has_group && selection != wxNOT_FOUND && selection + 1 < count);
	save_button->Enable(any_changed);
	Layout();
}

void MapLayersPanel::OnSelectLayer(wxCommandEvent& event) {
	if (updating) {
		return;
	}
	MapLayerGroup* group = CurrentGroup();
	const int index = event.GetInt();
	if (!group || index < 0 || index >= static_cast<int>(group->layers.size())) {
		return;
	}
	MapLayers::Activate(group->layers[index].editor);
	QueueRefresh();
}

void MapLayersPanel::OnToggleLayer(wxCommandEvent& event) {
	if (updating) {
		return;
	}
	MapLayerGroup* group = CurrentGroup();
	const int index = event.GetInt();
	if (!group || index < 0 || index >= static_cast<int>(group->layers.size())) {
		return;
	}
	group->layers[index].visible = layer_list->IsChecked(index);
	MapLayers::NotifyChanged();
}

void MapLayersPanel::OnMoveLayer(int direction) {
	MapLayerGroup* group = CurrentGroup();
	const int selection = layer_list->GetSelection();
	const int target = selection + direction;
	if (!group || selection == wxNOT_FOUND || target < 0 || target >= static_cast<int>(group->layers.size())) {
		return;
	}
	std::swap(group->layers[selection], group->layers[target]);
	MapLayers::NotifyChanged();
}

void MapLayersPanel::OnOpacity(wxCommandEvent&) {
	MapLayerGroup* group = CurrentGroup();
	if (updating || !group) {
		return;
	}
	group->inactive_opacity = opacity_slider->GetValue();
	opacity_label->SetLabel(wxString::Format("Other layers: %d%%", group->inactive_opacity));
	g_gui.RefreshView();
}

void MapLayersPanel::OnSaveChanged(wxCommandEvent&) {
	MapLayerGroup* group = CurrentGroup();
	if (!group) {
		return;
	}
	int saved = 0;
	for (const MapLayer& layer : group->layers) {
		if (!layer.editor || !layer.editor->map.hasChanged()) {
			continue;
		}
		g_status.SetStatusText("Saving " + layer.name + "...");
		EditorPersistence::saveMap(*layer.editor, FileName(""), true);
		++saved;
	}
	g_status.SetStatusText(wxString::Format("%d layer(s) saved.", saved));
	g_status.UpdateTitle();
	g_gui.root->UpdateMenubar();
	RefreshFromState();
}

void MapLayersPanel::OnCloseLayers(wxCommandEvent&) {
	MapTab* tab = g_gui.GetCurrentMapTab();
	if (!tab || !MapLayers::IsLayer(tab->GetEditor())) {
		return;
	}
	MapLayers::CloseGroup(tab->GetEditor());
	RefreshFromState();
}

void MapLayersPanel::OnOpenProfile(wxCommandEvent&) {
	MapProfilesDialog::ShowAndOpen(g_gui.root);
}
