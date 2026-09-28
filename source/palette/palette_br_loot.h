//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// Battle Royale: the two loot palettes.
//
//   BR Loot Zones  -- list of zones; New (with a tier), Set tier, Delete, Go to;
//                     pick one and paint its tiles with the BRLootZoneBrush.
//   BR Loot Items  -- the items of the loot table, with their sprite; pick one and
//                     click a tile to place it by hand (Ctrl+click takes it off).
//
// Every change is undoable (Editor::ApplyBRLootState), and everything is written to
// "<map>-brloot.json" when the map is saved. Separate from the instance zone
// palette, which belongs to BlackTalon.

#ifndef RME_PALETTE_BR_LOOT_H_
#define RME_PALETTE_BR_LOOT_H_

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <cstdint>
#include <string>
#include <vector>

#include "palette/palette_common.h"
#include "map/position.h"

class Map;
class Editor;

class BRLootZonePalettePanel : public PalettePanel {
public:
	BRLootZonePalettePanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~BRLootZonePalettePanel() override = default;

	wxString GetName() const override;
	PaletteType GetType() const override;

	void SelectFirstBrush() override;
	Brush* GetSelectedBrush() const override;
	int GetSelectedBrushSize() const override;
	bool SelectBrush(const Brush* whatbrush) override;

	void OnUpdate() override;
	void OnSwitchIn() override;

	void SetMap(Map* map);

	// Picks a zone from the map -- the smart brush, the right-click menu, a plain
	// click with no zone picked -- the same way a click on its row does. False for
	// an id this map's table does not know (a tile pasted from another map).
	bool PickZone(uint32_t zone_id);

protected:
	void OnClickZone(wxCommandEvent& event);
	void OnDoubleClickZone(wxCommandEvent& event);
	void OnClickNew(wxCommandEvent& event);
	void OnClickSetTier(wxCommandEvent& event);
	void OnClickDelete(wxCommandEvent& event);
	void OnClickGoto(wxCommandEvent& event);
	void OnToggleShow(wxCommandEvent& event);
	void OnToggleSolid(wxCommandEvent& event);

	void UpdateTierChoices();
	void UpdateList(uint32_t select_id);
	void UpdateSummary();
	uint32_t GetSelectedZoneId() const;
	int GetChosenTier() const;
	void ChooseTier(int tier);
	Editor* GetEditor() const;

	Map* map;
	wxChoice* tier_choice;
	wxListBox* zone_list;
	wxButton* new_button;
	wxButton* tier_button;
	wxButton* delete_button;
	wxButton* goto_button;
	wxCheckBox* show_toggle;
	wxCheckBox* solid_toggle;
	wxStaticText* summary;

	// zone id and tier behind each row of zone_list / tier_choice
	std::vector<uint32_t> row_ids;
	std::vector<wxString> row_texts;
	std::vector<int> tier_values;
	wxString summary_text;
	uint32_t catalog_version;
};

class BRLootItemPalettePanel : public PalettePanel {
public:
	BRLootItemPalettePanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~BRLootItemPalettePanel() override = default;

	wxString GetName() const override;
	PaletteType GetType() const override;

	void SelectFirstBrush() override;
	Brush* GetSelectedBrush() const override;
	int GetSelectedBrushSize() const override;
	bool SelectBrush(const Brush* whatbrush) override;

	void OnUpdate() override;
	void OnSwitchIn() override;

	void SetMap(Map* map);

	// Picks the item placed by hand at this position, the way a click on its row in
	// "Placed On This Map" does: it is loaded into the brush. False when none is there.
	bool PickPlaced(const Position& pos);

protected:
	void OnSelectItem(wxListEvent& event);
	void OnChangeAmount(wxSpinEvent& event);
	void OnClickPlaced(wxCommandEvent& event);
	void OnDoubleClickPlaced(wxCommandEvent& event);
	void OnClickGoto(wxCommandEvent& event);
	void OnClickRemove(wxCommandEvent& event);

	void UpdateCatalog();
	void UpdatePlaced();
	void ApplyToBrush() const;
	bool SelectItemByName(const std::string& name);
	bool LoadPlaced(int row);
	int GetSelectedPlacedRow() const;
	Editor* GetEditor() const;

	Map* map;
	wxListCtrl* item_list;
	wxStaticText* catalog_note;
	wxSpinCtrl* min_spin;
	wxSpinCtrl* max_spin;
	wxSpinCtrl* chance_spin;
	wxListBox* placed_list;
	wxButton* goto_button;
	wxButton* remove_button;

	// catalog name behind each row of item_list; placed item behind each placed row
	std::vector<std::string> item_names;
	std::vector<Position> placed_positions;
	std::vector<wxString> placed_texts;
	uint32_t catalog_version;
};

#endif
