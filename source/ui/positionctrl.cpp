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
#include "ui/positionctrl.h"
#include "ui/numbertextctrl.h"
#include "ui/gui.h"
#include "editor/editor.h"
#include "map/map.h"
#include "map/position.h"
#include <wx/spinctrl.h>

void EnablePositionPaste(wxSpinCtrl* x_field, wxSpinCtrl* y_field, wxSpinCtrl* z_field /* = nullptr */) {
	auto handler = [x_field, y_field, z_field](wxKeyEvent& evt) {
		if (evt.GetKeyCode() == WXK_CONTROL_V) {
			// Validate against the current map bounds when available; wxSpinCtrl
			// clamps to its own range on SetValue regardless.
			int map_width = MAP_MAX_WIDTH;
			int map_height = MAP_MAX_HEIGHT;
			if (const Editor* editor = g_gui.GetCurrentEditor()) {
				map_width = editor->map.getWidth();
				map_height = editor->map.getHeight();
			}
			Position position;
			if (posFromClipboard(position, map_width, map_height)) {
				if (x_field) {
					x_field->SetValue(position.x);
				}
				if (y_field) {
					y_field->SetValue(position.y);
				}
				if (z_field) {
					z_field->SetValue(position.z);
				}
				return; // consume so the raw text isn't pasted into a single field
			}
		}
		evt.Skip();
	};

	if (x_field) {
		x_field->Bind(wxEVT_CHAR, handler);
	}
	if (y_field) {
		y_field->Bind(wxEVT_CHAR, handler);
	}
	if (z_field) {
		z_field->Bind(wxEVT_CHAR, handler);
	}
}

PositionCtrl::PositionCtrl(wxWindow* parent, const wxString& label, int x, int y, int z, int maxx /*= MAP_MAX_WIDTH*/, int maxy /*= MAP_MAX_HEIGHT*/, int maxz /*= MAP_MAX_LAYER*/) :
	wxStaticBoxSizer(wxHORIZONTAL, parent, label) {
	wxWindow* box = GetStaticBox();
	x_field = newd NumberTextCtrl(box, wxID_ANY, x, 0, maxx, wxTE_PROCESS_ENTER, "X", wxDefaultPosition, wxSize(60, 20));
	x_field->SetToolTip("X Coordinate");
	x_field->Bind(wxEVT_TEXT_PASTE, &PositionCtrl::OnClipboardText, this);
	x_field->Bind(wxEVT_CHAR_HOOK, &PositionCtrl::OnCharHook, this);
	Add(x_field, 2, wxEXPAND | wxLEFT | wxBOTTOM, 5);

	y_field = newd NumberTextCtrl(box, wxID_ANY, y, 0, maxy, wxTE_PROCESS_ENTER, "Y", wxDefaultPosition, wxSize(60, 20));
	y_field->SetToolTip("Y Coordinate");
	y_field->Bind(wxEVT_TEXT_PASTE, &PositionCtrl::OnClipboardText, this);
	y_field->Bind(wxEVT_CHAR_HOOK, &PositionCtrl::OnCharHook, this);
	Add(y_field, 2, wxEXPAND | wxLEFT | wxBOTTOM, 5);

	z_field = newd NumberTextCtrl(box, wxID_ANY, z, 0, maxz, wxTE_PROCESS_ENTER, "Z", wxDefaultPosition, wxSize(35, 20));
	z_field->SetToolTip("Z Coordinate (Layer)");
	z_field->Bind(wxEVT_TEXT_PASTE, &PositionCtrl::OnClipboardText, this);
	z_field->Bind(wxEVT_CHAR_HOOK, &PositionCtrl::OnCharHook, this);
	Add(z_field, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

	maxWidth = maxx;
	maxHeight = maxy;
}

PositionCtrl::~PositionCtrl() {
	////
}

Position PositionCtrl::GetPosition() const {
	Position pos;
	pos.x = x_field->GetIntValue();
	pos.y = y_field->GetIntValue();
	pos.z = z_field->GetIntValue();
	return pos;
}

void PositionCtrl::SetPosition(Position pos) {
	x_field->SetIntValue(pos.x);
	y_field->SetIntValue(pos.y);
	z_field->SetIntValue(pos.z);
}

bool PositionCtrl::Enable(bool enable) {
	return (x_field->Enable(enable) && y_field->Enable(enable) && z_field->Enable(enable));
}

void PositionCtrl::OnClipboardText(wxClipboardTextEvent& evt) {
	// Read clipboard text directly instead of using posFromClipboard,
	// because the clipboard may already be locked by the paste operation.
	wxString clipText;
	if (wxTheClipboard->Open()) {
		if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
			wxTextDataObject data;
			wxTheClipboard->GetData(data);
			clipText = data.GetText();
		}
		wxTheClipboard->Close();
	}

	Position position;
	if (!clipText.empty() && posFromString(clipText.ToStdString(), position, maxWidth, maxHeight)) {
		x_field->SetIntValue(position.x);
		y_field->SetIntValue(position.y);
		z_field->SetIntValue(position.z);
	} else {
		evt.Skip();
	}
}

void PositionCtrl::OnCharHook(wxKeyEvent& evt) {
	// Intercept Ctrl+V before the numeric validator strips the separators from a
	// pasted "x, y, z" string. When the clipboard holds a recognizable position,
	// fill all three fields and swallow the paste; otherwise let it proceed.
	if (evt.ControlDown() && !evt.AltDown() && evt.GetKeyCode() == 'V') {
		Position position;
		if (posFromClipboard(position, maxWidth, maxHeight)) {
			x_field->SetIntValue(position.x);
			y_field->SetIntValue(position.y);
			z_field->SetIntValue(position.z);
			return; // consume so the raw text isn't pasted into a single field
		}
	}
	evt.Skip();
}
