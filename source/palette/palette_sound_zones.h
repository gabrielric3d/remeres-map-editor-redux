//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////

// BlackTalon: sound zone palette. Lists the map's ambient sound zones and lets
// the mapper Add / Edit (name + track) / Remove them, and pick one to paint with
// the SoundZoneBrush. Modeled on the waypoint palette but without drag/reorder.

#ifndef RME_PALETTE_SOUND_ZONES_H_
#define RME_PALETTE_SOUND_ZONES_H_

#include <wx/checkbox.h>
#include <wx/listbox.h>

#include <cstdint>
#include <vector>

#include "palette/palette_common.h"

class Map;

class SoundZonePalettePanel : public PalettePanel {
public:
	SoundZonePalettePanel(wxWindow* parent, wxWindowID id = wxID_ANY);
	~SoundZonePalettePanel() override = default;

	wxString GetName() const override;
	PaletteType GetType() const override;

	void SelectFirstBrush() override;
	Brush* GetSelectedBrush() const override;
	int GetSelectedBrushSize() const override;
	bool SelectBrush(const Brush* whatbrush) override;

	void OnUpdate() override;
	void OnSwitchIn() override;

	void SetMap(Map* map);

	// event handlers
	void OnClickZone(wxCommandEvent& event);
	void OnClickAdd(wxCommandEvent& event);
	void OnClickEdit(wxCommandEvent& event);
	void OnClickRemove(wxCommandEvent& event);
	void OnDoubleClickZone(wxCommandEvent& event);
	void OnToggleShow(wxCommandEvent& event);
	void OnClickRecenter(wxCommandEvent& event);

protected:
	void UpdateList(uint32_t select_id = 0);
	uint32_t GetSelectedZoneId() const;

	Map* map;
	wxCheckBox* show_toggle;
	wxListBox* zone_list;
	wxButton* add_button;
	wxButton* edit_button;
	wxButton* remove_button;
	wxButton* recenter_button;

	// zone id for each visible list row (parallel to zone_list rows)
	std::vector<uint32_t> row_ids;
};

#endif
