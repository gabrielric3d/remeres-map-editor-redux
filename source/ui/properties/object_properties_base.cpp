//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#include "app/main.h"
#include "ui/properties/object_properties_base.h"
#include "game/item.h"
#include "item_definitions/core/item_definition_store.h"
#include "editor/editor.h"
#include "ui/gui.h"

#include <wx/spinctrl.h>

// ============================================================================
// Object properties base

ObjectPropertiesWindowBase::ObjectPropertiesWindowBase(wxWindow* parent, wxString title, const Map* map, const Tile* tile, Item* item, wxPoint position /* = wxDefaultPosition */) :
	wxDialog(parent, wxID_ANY, title, position, wxSize(600, 400), wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER),
	edit_map(map),
	edit_tile(tile),
	edit_item(item),
	edit_creature(nullptr),
	edit_spawn(nullptr) {
	////
}

ObjectPropertiesWindowBase::ObjectPropertiesWindowBase(wxWindow* parent, wxString title, const Map* map, const Tile* tile, Creature* creature, wxPoint position /* = wxDefaultPosition */) :
	wxDialog(parent, wxID_ANY, title, position, wxSize(600, 400), wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER),
	edit_map(map),
	edit_tile(tile),
	edit_item(nullptr),
	edit_creature(creature),
	edit_spawn(nullptr) {
	////
}

ObjectPropertiesWindowBase::ObjectPropertiesWindowBase(wxWindow* parent, wxString title, const Map* map, const Tile* tile, Spawn* spawn, wxPoint position /* = wxDefaultPosition */) :
	wxDialog(parent, wxID_ANY, title, position, wxSize(600, 400), wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER),
	edit_map(map),
	edit_tile(tile),
	edit_item(nullptr),
	edit_creature(nullptr),
	edit_spawn(spawn) {
	////
}

ObjectPropertiesWindowBase::ObjectPropertiesWindowBase(wxWindow* parent, wxString title, wxPoint position /* = wxDefaultPosition */) :
	wxDialog(parent, wxID_ANY, title, position, wxSize(600, 400), wxCAPTION | wxCLOSE_BOX | wxRESIZE_BORDER) {
	////
}

Item* ObjectPropertiesWindowBase::getItemBeingEdited() {
	return edit_item;
}

Creature* ObjectPropertiesWindowBase::getCreatureBeingEdited() {
	return edit_creature;
}

Spawn* ObjectPropertiesWindowBase::getSpawnBeingEdited() {
	return edit_spawn;
}

// ============================================================================
// BlackTalon: draw offset da instancia

void ObjectPropertiesWindowBase::addDrawOffsetFields(wxFlexGridSizer* sizer, wxWindow* parent) {
	if (!edit_item) {
		return;
	}

	const std::pair<int, int> effective = edit_item->getEffectiveDrawOffset();
	const wxString tooltip = "Draw offset in px (+x right, +y down). Equal to the item type default = no override.";

	sizer->Add(newd wxStaticText(parent, wxID_ANY, "Offset X"));
	draw_offset_x_field = newd wxSpinCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, -64, 64, effective.first);
	draw_offset_x_field->SetToolTip(tooltip);
	sizer->Add(draw_offset_x_field, wxSizerFlags(1).Expand());

	sizer->Add(newd wxStaticText(parent, wxID_ANY, "Offset Y"));
	draw_offset_y_field = newd wxSpinCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, -64, 64, effective.second);
	draw_offset_y_field->SetToolTip(tooltip);
	sizer->Add(draw_offset_y_field, wxSizerFlags(1).Expand());

	// Preview ao vivo: setas/roda (SPINCTRL) e digitacao (TEXT) redesenham a tile no mapa.
	for (wxSpinCtrl* field : { draw_offset_x_field, draw_offset_y_field }) {
		field->Bind(wxEVT_SPINCTRL, &ObjectPropertiesWindowBase::onDrawOffsetChanged, this);
		field->Bind(wxEVT_TEXT, &ObjectPropertiesWindowBase::onDrawOffsetChanged, this);
	}
}

void ObjectPropertiesWindowBase::applyDrawOffsetFields() {
	if (!edit_item || !draw_offset_x_field || !draw_offset_y_field) {
		return;
	}

	// OK: o valor entra na copia e o commit troca a tile; o preview acaba aqui.
	endDrawOffsetPreview();

	const int x = draw_offset_x_field->GetValue();
	const int y = draw_offset_y_field->GetValue();
	const ItemDefinitionView definition = edit_item->getDefinition();
	const int type_x = static_cast<int>(definition.attribute(ItemAttributeKey::DrawOffsetX));
	const int type_y = static_cast<int>(definition.attribute(ItemAttributeKey::DrawOffsetY));

	// Igual ao padrao do tipo = sem override na instancia.
	if (x == type_x && y == type_y) {
		edit_item->clearInstanceDrawOffset();
	} else {
		edit_item->setInstanceDrawOffset(x, y);
	}
}

ObjectPropertiesWindowBase::~ObjectPropertiesWindowBase() {
	// Rede de seguranca: qualquer caminho de saida deixa o item no lugar original.
	endDrawOffsetPreview();
}

void ObjectPropertiesWindowBase::setDrawOffsetPreviewSource(const Item* original, const Position& position) {
	preview_original_item = original;
	preview_position = position;
}

void ObjectPropertiesWindowBase::onDrawOffsetChanged(wxCommandEvent& event) {
	refreshDrawOffsetPreview();
	event.Skip();
}

void ObjectPropertiesWindowBase::refreshDrawOffsetPreview() {
	if (!preview_original_item || !draw_offset_x_field || !draw_offset_y_field) {
		return;
	}

	Item::setDrawOffsetPreview(preview_original_item, draw_offset_x_field->GetValue(), draw_offset_y_field->GetValue());

	// O chunk cache guarda a tile ja montada: marcar suja forca o rebuild com o novo offset.
	if (Editor* editor = g_gui.GetCurrentEditor()) {
		editor->map.getChangeTracker().markTileDirty(preview_position);
	}
	g_gui.RefreshView();
}

void ObjectPropertiesWindowBase::endDrawOffsetPreview() {
	if (!preview_original_item) {
		return;
	}

	// So desfaz o que este dialogo pos (o preview e global, de um item por vez).
	if (Item::getDrawOffsetPreviewItem() == preview_original_item) {
		Item::clearDrawOffsetPreview();
	}
	preview_original_item = nullptr;

	if (Editor* editor = g_gui.GetCurrentEditor()) {
		editor->map.getChangeTracker().markTileDirty(preview_position);
	}
	g_gui.RefreshView();
}
