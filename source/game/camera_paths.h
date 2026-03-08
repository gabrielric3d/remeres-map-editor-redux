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

#ifndef RME_CAMERA_PATHS_H_
#define RME_CAMERA_PATHS_H_

#include "app/main.h"
#include "map/position.h"

#include <cstdint>
#include <string>
#include <vector>

enum class CameraEasing {
	Linear = 0,
	EaseInOut,
	EaseIn,
	EaseOut,
	EaseInOutCubic,
};

struct CameraKeyframe {
	Position pos;
	double duration = 1.0;
	double speed = 0.0;
	double zoom = 1.0;
	CameraEasing easing = CameraEasing::EaseInOut;
};

struct CameraPathColor {
	uint8_t r = 255;
	uint8_t g = 160;
	uint8_t b = 80;
};

struct CameraPath {
	std::string name;
	bool loop = false;
	CameraPathColor color;
	std::vector<CameraKeyframe> keyframes;
};

struct CameraPathsSnapshot {
	std::vector<CameraPath> paths;
	std::string active_path;
	int active_keyframe = -1;
};

struct CameraPathSample {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	double zoom = 1.0;
	size_t segment_index = 0;
	double segment_t = 0.0;
};

class CameraPaths {
public:
	CameraPaths();

	CameraPath* addPath(const std::string& name);
	bool removePath(const std::string& name);
	void clear();

	CameraPath* getPath(const std::string& name);
	const CameraPath* getPath(const std::string& name) const;
	CameraPath* getActivePath();
	const CameraPath* getActivePath() const;

	const std::vector<CameraPath>& getPaths() const { return paths; }
	std::vector<CameraPath>& getPaths() { return paths; }

	void setActivePath(const std::string& name);
	const std::string& getActivePathName() const { return active_path; }
	void setActiveKeyframe(int index);
	int getActiveKeyframe() const { return active_keyframe; }

	CameraPathsSnapshot snapshot() const;
	void applySnapshot(const CameraPathsSnapshot& snap);
	void swapSnapshot(CameraPathsSnapshot& snap);

	std::string generateUniquePathName(const std::string& base) const;

	bool loadFromFile(const FileName& mapFile, wxString* outError = nullptr);
	bool saveToFile(const FileName& mapFile, wxString* outError = nullptr) const;
	static FileName BuildSidecarPath(const FileName& mapFile);

private:
	std::vector<CameraPath> paths;
	std::string active_path;
	int active_keyframe;
};

double GetCameraSegmentDuration(const CameraKeyframe& a, const CameraKeyframe& b);
double GetCameraPathDuration(const CameraPath& path, bool loop);
CameraPathSample SampleCameraPathByTime(const CameraPath& path, double time, bool loop, bool* finished = nullptr);

#endif
