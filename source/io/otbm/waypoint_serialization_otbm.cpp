#include "waypoint_serialization_otbm.h"

#include "map/map.h"
#include <spdlog/spdlog.h>

void WaypointSerializationOTBM::readWaypoints(Map& map, BinaryNode* mapNode) {
	spdlog::debug("Reading OTBM_WAYPOINTS...");
	int order = 0;
	for (BinaryNode* waypointNode = mapNode->getChild(); waypointNode != nullptr; waypointNode = waypointNode->advance()) {
		uint8_t waypointType;
		if (!waypointNode->getByte(waypointType)) {
			spdlog::warn("Invalid waypoint node: failed to read type byte");
			continue;
		}
		if (waypointType != OTBM_WAYPOINT) {
			spdlog::warn("Invalid waypoint node type: {} (expected {})", static_cast<int>(waypointType), static_cast<int>(OTBM_WAYPOINT));
			continue;
		}

		Waypoint wp;
		if (!waypointNode->getString(wp.name)) {
			spdlog::warn("Failed to read waypoint name");
			continue;
		}

		uint16_t x, y;
		uint8_t z;
		if (!waypointNode->getU16(x) || !waypointNode->getU16(y) || !waypointNode->getU8(z)) {
			spdlog::warn("Invalid position for waypoint '{}'", wp.name);
			continue;
		}
		wp.pos = { x, y, z };
		// Preserve the on-disk order so manual ordering round-trips.
		wp.order = order++;

		map.waypoints.addWaypoint(std::make_unique<Waypoint>(std::move(wp)));
	}
}

OTBMWriteResult WaypointSerializationOTBM::writeWaypoints(const Map& map, NodeFileWriteHandle& f, MapVersion mapVersion) {
	if (map.waypoints.begin() == map.waypoints.end()) {
		return OTBMWriteResult::Success;
	}

	OTBMWriteResult WriteResult = OTBMWriteResult::Success;

	if (mapVersion.otbm < MAP_OTBM_2) {
		return OTBMWriteResult::SuccessWithUnsupportedVersion;
	}
	const bool supportWaypoints = mapVersion.otbm >= MAP_OTBM_3;

	if (supportWaypoints) {
		f.addNode(OTBM_WAYPOINTS);
		for (const Waypoint* waypoint : map.waypoints.getOrdered()) {
			f.addNode(OTBM_WAYPOINT);
			f.addString(waypoint->name);
			f.addU16(waypoint->pos.x);
			f.addU16(waypoint->pos.y);
			f.addU8(waypoint->pos.z);
			f.endNode();
		}
		f.endNode();
	} else {
		WriteResult = OTBMWriteResult::SuccessWithUnsupportedVersion;
	}

	return WriteResult;
}
