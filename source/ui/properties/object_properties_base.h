//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

#ifndef RME_ObjectPropertiesWindowBase_H
#define RME_ObjectPropertiesWindowBase_H

#include "app/main.h"
#include "map/position.h"

class Map;
class Tile;
class Item;
class Creature;
class Spawn;
class wxSpinCtrl;
class wxFlexGridSizer;

/**
 * Base for the item properties dialogs
 * There are two versions, one for otbmv4 maps and one for the old maps.
 * They are declared in old_properties_window / properties_window
 */
class ObjectPropertiesWindowBase : public wxDialog {
public:
	ObjectPropertiesWindowBase(
		wxWindow* parent, wxString title,
		const Map* map, const Tile* tile, Item* item,
		wxPoint position = wxDefaultPosition
	);
	ObjectPropertiesWindowBase(
		wxWindow* parent, wxString title,
		const Map* map, const Tile* tile, Spawn* spawn,
		wxPoint position = wxDefaultPosition
	);
	ObjectPropertiesWindowBase(
		wxWindow* parent, wxString title,
		const Map* map, const Tile* tile, Creature* creature,
		wxPoint position = wxDefaultPosition
	);
	ObjectPropertiesWindowBase(
		wxWindow* parent, wxString title, wxPoint position = wxDefaultPosition
	);

	Item* getItemBeingEdited();
	Creature* getCreatureBeingEdited();
	Spawn* getSpawnBeingEdited();

	~ObjectPropertiesWindowBase() override;

	// BlackTalon: preview ao vivo do draw offset. O dialogo edita uma copia da tile;
	// aqui entra o item ORIGINAL do mapa (ou nulo, e nao ha preview) e a posicao da tile.
	void setDrawOffsetPreviewSource(const Item* original, const Position& position);
	// Limpa o preview e redesenha a tile na posicao original. Idempotente.
	void endDrawOffsetPreview();

protected:
	const Map* edit_map;
	const Tile* edit_tile;
	Item* edit_item;
	Creature* edit_creature;
	Spawn* edit_spawn;

	// BlackTalon: draw offset da instancia do item (px, +x direita, +y baixo).
	// addDrawOffsetFields monta as duas linhas; applyDrawOffsetFields grava no OK.
	void addDrawOffsetFields(wxFlexGridSizer* sizer, wxWindow* parent);
	void applyDrawOffsetFields();
	void onDrawOffsetChanged(wxCommandEvent& event);
	void refreshDrawOffsetPreview();

	wxSpinCtrl* draw_offset_x_field = nullptr;
	wxSpinCtrl* draw_offset_y_field = nullptr;
	const Item* preview_original_item = nullptr;
	Position preview_position;
};

#endif
