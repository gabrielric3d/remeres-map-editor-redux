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

#include "brushes/camera/camera_path_brush.h"
#include "map/basemap.h"

CameraPathBrush::CameraPathBrush() :
	Brush() {
}

CameraPathBrush::~CameraPathBrush() = default;

bool CameraPathBrush::canDraw(BaseMap* map, const Position& position) const
{
	return position.isValid();
}

void CameraPathBrush::draw(BaseMap* map, Tile* tile, void* parameter)
{
	ASSERT(false); // Camera path brush drawing is handled via snapshot changes
}

void CameraPathBrush::undraw(BaseMap* map, Tile* tile)
{
	ASSERT(false); // Camera path brush undrawing is handled via snapshot changes
}
