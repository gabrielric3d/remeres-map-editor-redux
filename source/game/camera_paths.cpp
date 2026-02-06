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

#include "game/camera_paths.h"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {

const CameraPathColor kDefaultColors[] = {
	{255, 96, 96},
	{96, 196, 255},
	{120, 255, 140},
	{255, 200, 96},
	{180, 120, 255},
	{255, 128, 200},
	{96, 240, 200},
};

CameraPathColor PickColor(size_t index)
{
	constexpr size_t kColorCount = sizeof(kDefaultColors) / sizeof(kDefaultColors[0]);
	return kDefaultColors[index % kColorCount];
}

double ApplyEasing(double t, CameraEasing easing)
{
	switch (easing) {
		case CameraEasing::Linear:
			return t;
		case CameraEasing::EaseIn:
			return t * t;
		case CameraEasing::EaseOut:
			return t * (2.0 - t);
		case CameraEasing::EaseInOut:
			return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0;
		case CameraEasing::EaseInOutCubic:
			return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
		default:
			return t;
	}
}

double CatmullRom(double p0, double p1, double p2, double p3, double t)
{
	const double t2 = t * t;
	const double t3 = t2 * t;
	return 0.5 * ((2.0 * p1) +
		(-p0 + p2) * t +
		(2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
		(-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

size_t ClampIndex(int index, size_t maxIndex)
{
	if (index < 0) {
		return 0;
	}
	auto idx = static_cast<size_t>(index);
	return idx > maxIndex ? maxIndex : idx;
}

size_t WrapIndex(int index, size_t count)
{
	if (count == 0) {
		return 0;
	}
	int mod = index % static_cast<int>(count);
	if (mod < 0) {
		mod += static_cast<int>(count);
	}
	return static_cast<size_t>(mod);
}

CameraPathSample EvaluateSegment(const CameraPath& path, size_t segmentIndex, double t, bool loop)
{
	const size_t count = path.keyframes.size();
	const size_t i1 = segmentIndex;
	const size_t i2 = loop ? WrapIndex(static_cast<int>(segmentIndex + 1), count) : ClampIndex(static_cast<int>(segmentIndex + 1), count - 1);
	const size_t i0 = loop ? WrapIndex(static_cast<int>(segmentIndex) - 1, count) : ClampIndex(static_cast<int>(segmentIndex) - 1, count - 1);
	const size_t i3 = loop ? WrapIndex(static_cast<int>(segmentIndex + 2), count) : ClampIndex(static_cast<int>(segmentIndex + 2), count - 1);

	const auto& p0 = path.keyframes[i0];
	const auto& p1 = path.keyframes[i1];
	const auto& p2 = path.keyframes[i2];
	const auto& p3 = path.keyframes[i3];

	const double eased_t = ApplyEasing(t, p1.easing);

	CameraPathSample sample;
	sample.x = CatmullRom(p0.pos.x, p1.pos.x, p2.pos.x, p3.pos.x, eased_t);
	sample.y = CatmullRom(p0.pos.y, p1.pos.y, p2.pos.y, p3.pos.y, eased_t);
	sample.zoom = CatmullRom(p0.zoom, p1.zoom, p2.zoom, p3.zoom, eased_t);
	sample.z = p1.pos.z + (p2.pos.z - p1.pos.z) * eased_t;
	sample.segment_index = segmentIndex;
	sample.segment_t = t;
	return sample;
}

} // namespace

CameraPaths::CameraPaths() :
	active_keyframe(-1) {
}

CameraPath* CameraPaths::addPath(const std::string& name)
{
	CameraPath path;
	path.name = generateUniquePathName(name.empty() ? "Path" : name);
	path.loop = true;
	path.color = PickColor(paths.size());
	paths.push_back(path);
	setActivePath(path.name);
	active_keyframe = -1;
	return &paths.back();
}

bool CameraPaths::removePath(const std::string& name)
{
	for (size_t i = 0; i < paths.size(); ++i) {
		if (paths[i].name == name) {
			paths.erase(paths.begin() + static_cast<long>(i));
			if (active_path == name) {
				if (paths.empty()) {
					active_path.clear();
					active_keyframe = -1;
				} else {
					active_path = paths.front().name;
					active_keyframe = -1;
				}
			}
			return true;
		}
	}
	return false;
}

void CameraPaths::clear()
{
	paths.clear();
	active_path.clear();
	active_keyframe = -1;
}

CameraPath* CameraPaths::getPath(const std::string& name)
{
	for (auto& path : paths) {
		if (path.name == name) {
			return &path;
		}
	}
	return nullptr;
}

const CameraPath* CameraPaths::getPath(const std::string& name) const
{
	for (const auto& path : paths) {
		if (path.name == name) {
			return &path;
		}
	}
	return nullptr;
}

CameraPath* CameraPaths::getActivePath()
{
	if (active_path.empty()) {
		return paths.empty() ? nullptr : &paths.front();
	}
	CameraPath* path = getPath(active_path);
	if (!path && !paths.empty()) {
		active_path = paths.front().name;
		return &paths.front();
	}
	return path;
}

const CameraPath* CameraPaths::getActivePath() const
{
	if (active_path.empty()) {
		return paths.empty() ? nullptr : &paths.front();
	}
	const CameraPath* path = getPath(active_path);
	return path ? path : (paths.empty() ? nullptr : &paths.front());
}

void CameraPaths::setActivePath(const std::string& name)
{
	active_path = name;
}

void CameraPaths::setActiveKeyframe(int index)
{
	active_keyframe = index;
}

CameraPathsSnapshot CameraPaths::snapshot() const
{
	CameraPathsSnapshot snap;
	snap.paths = paths;
	snap.active_path = active_path;
	snap.active_keyframe = active_keyframe;
	return snap;
}

void CameraPaths::applySnapshot(const CameraPathsSnapshot& snap)
{
	paths = snap.paths;
	active_path = snap.active_path;
	active_keyframe = snap.active_keyframe;
}

void CameraPaths::swapSnapshot(CameraPathsSnapshot& snap)
{
	std::swap(paths, snap.paths);
	std::swap(active_path, snap.active_path);
	std::swap(active_keyframe, snap.active_keyframe);
}

std::string CameraPaths::generateUniquePathName(const std::string& base) const
{
	if (getPath(base) == nullptr) {
		return base;
	}

	for (int i = 2; i < 10000; ++i) {
		std::string name = base + " " + i2s(i);
		if (getPath(name) == nullptr) {
			return name;
		}
	}
	return base + " 10000";
}

FileName CameraPaths::BuildSidecarPath(const FileName& mapFile)
{
	FileName sidecar(mapFile);
	if (sidecar.GetFullPath().empty()) {
		return sidecar;
	}
	sidecar.SetExt("camera.json");
	return sidecar;
}

bool CameraPaths::loadFromFile(const FileName& mapFile, wxString* outError)
{
	clear();
	FileName sidecar = BuildSidecarPath(mapFile);
	if (sidecar.GetFullPath().empty() || !sidecar.FileExists()) {
		return false;
	}

	std::ifstream file(nstr(sidecar.GetFullPath()).c_str(), std::ios::in);
	if (!file.is_open()) {
		if (outError) {
			*outError = "Could not open camera path file.";
		}
		return false;
	}

	try {
		nlohmann::json root;
		file >> root;
		if (!root.is_object()) {
			if (outError) {
				*outError = "Camera path file has invalid format.";
			}
			return false;
		}

		const auto& pathsNode = root.value("paths", nlohmann::json::array());
		if (!pathsNode.is_array()) {
			if (outError) {
				*outError = "Camera path file has invalid paths list.";
			}
			return false;
		}

		for (const auto& pathNode : pathsNode) {
			if (!pathNode.is_object()) {
				continue;
			}

			CameraPath path;
			path.name = pathNode.value("name", std::string());
			if (path.name.empty()) {
				path.name = generateUniquePathName("Path");
			} else {
				path.name = generateUniquePathName(path.name);
			}

			path.loop = pathNode.value("loop", true);
			if (pathNode.contains("color") && pathNode["color"].is_array() && pathNode["color"].size() >= 3) {
				path.color.r = static_cast<uint8_t>(pathNode["color"][0].get<int>());
				path.color.g = static_cast<uint8_t>(pathNode["color"][1].get<int>());
				path.color.b = static_cast<uint8_t>(pathNode["color"][2].get<int>());
			} else {
				path.color = PickColor(paths.size());
			}

			const auto& keyframesNode = pathNode.value("keyframes", nlohmann::json::array());
			if (keyframesNode.is_array()) {
				for (const auto& keyNode : keyframesNode) {
					if (!keyNode.is_object()) {
						continue;
					}

					CameraKeyframe key;
					key.pos.x = keyNode.value("x", 0);
					key.pos.y = keyNode.value("y", 0);
					key.pos.z = keyNode.value("z", 7);
					key.duration = keyNode.value("duration", 1.0);
					key.speed = keyNode.value("speed", 0.0);
					key.zoom = keyNode.value("zoom", 1.0);
					key.easing = static_cast<CameraEasing>(keyNode.value("easing", static_cast<int>(CameraEasing::EaseInOut)));
					path.keyframes.push_back(key);
				}
			}

			paths.push_back(path);
		}

		if (!paths.empty()) {
			active_path = paths.front().name;
			active_keyframe = -1;
		}
	} catch (const std::exception& e) {
		if (outError) {
			*outError = "Camera path file parse error: " + wxString(e.what());
		}
		return false;
	}

	return true;
}

bool CameraPaths::saveToFile(const FileName& mapFile, wxString* outError) const
{
	FileName sidecar = BuildSidecarPath(mapFile);
	if (sidecar.GetFullPath().empty()) {
		return false;
	}

	if (paths.empty()) {
		// No paths to save - remove sidecar if it exists
		if (sidecar.FileExists()) {
			wxRemoveFile(sidecar.GetFullPath());
		}
		return true;
	}

	nlohmann::json root;
	root["version"] = 1;
	root["paths"] = nlohmann::json::array();

	for (const auto& path : paths) {
		nlohmann::json pathNode;
		pathNode["name"] = path.name;
		pathNode["loop"] = path.loop;
		pathNode["color"] = { path.color.r, path.color.g, path.color.b };

		nlohmann::json keyframes = nlohmann::json::array();
		for (const auto& key : path.keyframes) {
			nlohmann::json keyNode;
			keyNode["x"] = key.pos.x;
			keyNode["y"] = key.pos.y;
			keyNode["z"] = key.pos.z;
			keyNode["duration"] = key.duration;
			keyNode["speed"] = key.speed;
			keyNode["zoom"] = key.zoom;
			keyNode["easing"] = static_cast<int>(key.easing);
			keyframes.push_back(keyNode);
		}
		pathNode["keyframes"] = keyframes;
		root["paths"].push_back(pathNode);
	}

	std::ofstream file(nstr(sidecar.GetFullPath()).c_str(), std::ios::out | std::ios::trunc);
	if (!file.is_open()) {
		if (outError) {
			*outError = "Could not write camera path file.";
		}
		return false;
	}

	try {
		file << root.dump(2);
	} catch (const std::exception& e) {
		if (outError) {
			*outError = "Camera path write error: " + wxString(e.what());
		}
		return false;
	}

	return true;
}

double GetCameraSegmentDuration(const CameraKeyframe& a, const CameraKeyframe& b)
{
	const double dx = static_cast<double>(b.pos.x - a.pos.x);
	const double dy = static_cast<double>(b.pos.y - a.pos.y);
	const double dist = std::sqrt(dx * dx + dy * dy);
	if (a.speed > 0.0) {
		return dist / std::max(0.0001, a.speed);
	}
	return std::max(0.0, a.duration);
}

double GetCameraPathDuration(const CameraPath& path, bool loop)
{
	if (path.keyframes.size() < 2) {
		return 0.0;
	}

	double total = 0.0;
	const size_t count = path.keyframes.size();
	const size_t segments = loop ? count : (count - 1);
	for (size_t i = 0; i < segments; ++i) {
		const size_t next = loop ? WrapIndex(static_cast<int>(i + 1), count) : (i + 1);
		total += GetCameraSegmentDuration(path.keyframes[i], path.keyframes[next]);
	}
	return total;
}

CameraPathSample SampleCameraPathByTime(const CameraPath& path, double time, bool loop, bool* finished)
{
	CameraPathSample sample;
	if (finished) {
		*finished = false;
	}

	if (path.keyframes.size() < 2) {
		if (finished) {
			*finished = true;
		}
		return sample;
	}

	const double total = GetCameraPathDuration(path, loop);
	if (total <= 0.0) {
		if (finished) {
			*finished = true;
		}
		return sample;
	}

	double localTime = time;
	if (loop) {
		localTime = std::fmod(time, total);
		if (localTime < 0.0) {
			localTime += total;
		}
	} else if (localTime >= total) {
		if (finished) {
			*finished = true;
		}
		localTime = total;
	}

	const size_t count = path.keyframes.size();
	const size_t segments = loop ? count : (count - 1);

	for (size_t i = 0; i < segments; ++i) {
		const size_t next = loop ? WrapIndex(static_cast<int>(i + 1), count) : (i + 1);
		double segDuration = GetCameraSegmentDuration(path.keyframes[i], path.keyframes[next]);
		if (segDuration <= 0.0) {
			continue;
		}

		if (localTime <= segDuration || i == segments - 1) {
			double t = segDuration > 0.0 ? (localTime / segDuration) : 0.0;
			return EvaluateSegment(path, i, t, loop);
		}
		localTime -= segDuration;
	}

	if (finished) {
		*finished = true;
	}
	return sample;
}
