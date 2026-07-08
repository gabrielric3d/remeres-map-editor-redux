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

#ifndef _RME_POSITION_CTRL_H_
#define _RME_POSITION_CTRL_H_

#include "ui/numbertextctrl.h"

class wxSpinCtrl;

// Enables pasting a full "x, y, z" position (e.g. "1428, 1232, 8") onto a trio of
// raw wxSpinCtrl controls, for dialogs that use plain spin controls instead of
// PositionCtrl. Pressing Ctrl+V while any of the controls is focused fills all of
// them at once when the clipboard holds a recognizable position. Pass nullptr for
// z_field on 2D (X/Y only) inputs, in which case the pasted Z is ignored.
void EnablePositionPaste(wxSpinCtrl* x_field, wxSpinCtrl* y_field, wxSpinCtrl* z_field = nullptr);

class PositionCtrl : public wxStaticBoxSizer {
public:
	PositionCtrl(wxWindow* parent, const wxString& label, int x, int y, int z, int maxx = MAP_MAX_WIDTH, int maxy = MAP_MAX_HEIGHT, int maxz = MAP_MAX_LAYER);
	~PositionCtrl();

	long GetX() const {
		return x_field->GetIntValue();
	}
	long GetY() const {
		return y_field->GetIntValue();
	}
	long GetZ() const {
		return z_field->GetIntValue();
	}
	Position GetPosition() const;

	void SetX(long value) {
		x_field->SetIntValue(value);
	}
	void SetY(long value) {
		y_field->SetIntValue(value);
	}
	void SetZ(long value) {
		z_field->SetIntValue(value);
	}
	void SetPosition(Position pos);

	bool Enable(bool enable = true);

	void OnClipboardText(wxClipboardTextEvent&);
	void OnCharHook(wxKeyEvent&);

protected:
	NumberTextCtrl* x_field;
	NumberTextCtrl* y_field;
	NumberTextCtrl* z_field;

private:
	int maxWidth, maxHeight;
};

#endif
