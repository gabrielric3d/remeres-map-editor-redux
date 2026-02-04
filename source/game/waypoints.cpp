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

#include "game/waypoints.h"
#include "map/map.h"

void Waypoints::addWaypoint(Waypoint* wp) {
	removeWaypoint(wp->name);
	if (wp->pos != Position()) {
		Tile* t = map.getTile(wp->pos);
		if (!t) {
			map.setTile(wp->pos, t = map.allocator(map.createTileL(wp->pos)));
		}
		t->getLocation()->increaseWaypointCount();
	}
	auto res = waypoints.insert(std::make_pair(as_lower_str(wp->name), wp));
	if (res.second) {
		waypoints_by_pos.emplace(wp->pos, wp);
	}
}

Waypoint* Waypoints::getWaypoint(std::string name) {
	to_lower_str(name);
	WaypointMap::iterator iter = waypoints.find(name);
	if (iter == waypoints.end()) {
		return nullptr;
	}
	return iter->second;
}

Waypoint* Waypoints::getWaypoint(TileLocation* location) {
	if (!location) {
		return nullptr;
	}
	auto it = waypoints_by_pos.find(location->position);
	if (it != waypoints_by_pos.end()) {
		return it->second;
	}
	return nullptr;
}

void Waypoints::removeWaypoint(std::string name) {
	to_lower_str(name);
	WaypointMap::iterator iter = waypoints.find(name);
	if (iter == waypoints.end()) {
		return;
	}

	Waypoint* wp = iter->second;
	auto range = waypoints_by_pos.equal_range(wp->pos);
	for (auto it = range.first; it != range.second; ++it) {
		if (it->second == wp) {
			waypoints_by_pos.erase(it);
			break;
		}
	}

	delete iter->second;
	waypoints.erase(iter);
}
