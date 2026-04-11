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
#include "lua_api_camera_path.h"
#include "game/camera_paths.h"
#include "map/map.h"
#include "ui/gui.h"
#include "editor/editor.h"
#include "app/settings.h"
#include "ui/map_tab.h"

namespace LuaAPI {

	// Helper: get the current editor or throw
	static Editor* requireEditor() {
		Editor* editor = g_gui.GetCurrentEditor();
		if (!editor) {
			throw sol::error("No map open");
		}
		return editor;
	}

	// Helper: get camera_paths from the current map or throw
	static CameraPaths& requireCameraPaths() {
		Editor* editor = requireEditor();
		Map* map = editor->getMap();
		if (!map) {
			throw sol::error("No map open");
		}
		return map->camera_paths;
	}

	// Helper: apply a modification to camera paths with undo support
	// Takes a lambda that modifies a CameraPaths copy, then applies via snapshot.
	// The lambda may return bool; if it returns false, the snapshot is not applied.
	template <typename Func>
	static auto modifyCameraPaths(Func&& func) -> decltype(func(std::declval<CameraPaths&>())) {
		Editor* editor = requireEditor();
		Map* map = editor->getMap();
		if (!map) {
			throw sol::error("No map open");
		}

		// Copy current state
		CameraPaths temp;
		temp.applySnapshot(map->camera_paths.snapshot());

		// Apply the modification
		if constexpr (std::is_same_v<decltype(func(temp)), bool>) {
			bool result = func(temp);
			if (!result) {
				return false;
			}
			editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_LUA_SCRIPT);
			g_gui.RefreshView();
			return true;
		} else {
			func(temp);
			editor->ApplyCameraPathsSnapshot(temp.snapshot(), ACTION_LUA_SCRIPT);
			g_gui.RefreshView();
		}
	}

	// Helper: validate easing int value is within valid range
	static bool isValidEasing(int e) {
		return e >= 0 && e <= static_cast<int>(CameraEasing::EaseInOutCubic);
	}

	// Helper: serialize a CameraPath to a Lua table
	static sol::table pathToTable(sol::state_view& lua, const CameraPath& path) {
		sol::table pathInfo = lua.create_table();
		pathInfo["name"] = path.name;
		pathInfo["loop"] = path.loop;
		pathInfo["keyframeCount"] = static_cast<int>(path.keyframes.size());

		sol::table color = lua.create_table();
		color["r"] = path.color.r;
		color["g"] = path.color.g;
		color["b"] = path.color.b;
		pathInfo["color"] = color;

		sol::table keyframes = lua.create_table();
		for (size_t k = 0; k < path.keyframes.size(); ++k) {
			sol::table kf = lua.create_table();
			kf["x"] = path.keyframes[k].pos.x;
			kf["y"] = path.keyframes[k].pos.y;
			kf["z"] = path.keyframes[k].pos.z;
			kf["duration"] = path.keyframes[k].duration;
			kf["speed"] = path.keyframes[k].speed;
			kf["zoom"] = path.keyframes[k].zoom;
			kf["easing"] = static_cast<int>(path.keyframes[k].easing);
			keyframes[k + 1] = kf;
		}
		pathInfo["keyframes"] = keyframes;

		return pathInfo;
	}

	void registerCameraPath(sol::state& lua) {
		// Register CameraKeyframe usertype
		lua.new_usertype<CameraKeyframe>(
			"CameraKeyframe",
			sol::constructors<CameraKeyframe()>(),

			"x", sol::property(
				[](const CameraKeyframe& kf) { return kf.pos.x; },
				[](CameraKeyframe& kf, int v) { kf.pos.x = v; }
			),
			"y", sol::property(
				[](const CameraKeyframe& kf) { return kf.pos.y; },
				[](CameraKeyframe& kf, int v) { kf.pos.y = v; }
			),
			"z", sol::property(
				[](const CameraKeyframe& kf) { return kf.pos.z; },
				[](CameraKeyframe& kf, int v) { kf.pos.z = v; }
			),
			"duration", &CameraKeyframe::duration,
			"speed", &CameraKeyframe::speed,
			"zoom", &CameraKeyframe::zoom,
			"easing", sol::property(
				[](const CameraKeyframe& kf) { return static_cast<int>(kf.easing); },
				[](CameraKeyframe& kf, int v) {
					if (isValidEasing(v)) {
						kf.easing = static_cast<CameraEasing>(v);
					}
				}
			),

			sol::meta_function::to_string, [](const CameraKeyframe& kf) {
				return "CameraKeyframe(" + std::to_string(kf.pos.x) + ", " +
					   std::to_string(kf.pos.y) + ", " + std::to_string(kf.pos.z) + ")";
			}
		);

		// Register CameraPath usertype (read-only references from getPaths)
		lua.new_usertype<CameraPath>(
			"CameraPath",
			sol::no_constructor,

			"name", &CameraPath::name,
			"loop", &CameraPath::loop,

			"color", sol::property(
				[](const CameraPath& p, sol::this_state ts) -> sol::table {
					sol::state_view lua(ts);
					sol::table t = lua.create_table();
					t["r"] = p.color.r;
					t["g"] = p.color.g;
					t["b"] = p.color.b;
					return t;
				},
				[](CameraPath& p, sol::table t) {
					if (t["r"].valid()) p.color.r = t["r"].get<uint8_t>();
					if (t["g"].valid()) p.color.g = t["g"].get<uint8_t>();
					if (t["b"].valid()) p.color.b = t["b"].get<uint8_t>();
				}
			),

			"keyframes", sol::property([](const CameraPath& p, sol::this_state ts) -> sol::table {
				sol::state_view lua(ts);
				sol::table t = lua.create_table();
				for (size_t i = 0; i < p.keyframes.size(); ++i) {
					t[i + 1] = p.keyframes[i]; // Copy to Lua (1-based)
				}
				return t;
			}),

			"keyframeCount", sol::property([](const CameraPath& p) -> int {
				return static_cast<int>(p.keyframes.size());
			}),

			sol::meta_function::to_string, [](const CameraPath& p) {
				return "CameraPath(\"" + p.name + "\", " + std::to_string(p.keyframes.size()) + " keyframes)";
			}
		);

		// Create app.cameraPaths table
		sol::table app = lua["app"];
		sol::table cameraPaths = lua.create_table();
		app["cameraPaths"] = cameraPaths;

		// getPaths() - returns table with all CameraPath data as tables
		cameraPaths["getPaths"] = [](sol::this_state ts) -> sol::table {
			sol::state_view lua(ts);
			sol::table result = lua.create_table();

			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return result;
			}

			const auto& paths = editor->getMap()->camera_paths.getPaths();
			for (size_t i = 0; i < paths.size(); ++i) {
				result[i + 1] = pathToTable(lua, paths[i]);
			}
			return result;
		};

		// getPath(name) - returns path info table or nil
		cameraPaths["getPath"] = [](sol::this_state ts, const std::string& name) -> sol::object {
			sol::state_view lua(ts);
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return sol::nil;
			}

			const CameraPath* path = editor->getMap()->camera_paths.getPath(name);
			if (!path) {
				return sol::nil;
			}

			return pathToTable(lua, *path);
		};

		// getActivePath() - returns active path name or nil
		cameraPaths["getActivePath"] = [](sol::this_state ts) -> sol::object {
			sol::state_view lua(ts);
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return sol::nil;
			}

			const std::string& name = editor->getMap()->camera_paths.getActivePathName();
			if (name.empty()) {
				return sol::nil;
			}
			return sol::make_object(lua, name);
		};

		// setActivePath(name) - set the active path (UI selection, no undo)
		cameraPaths["setActivePath"] = [](const std::string& name) {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return;
			}
			editor->getMap()->camera_paths.setActivePath(name);
			g_gui.RefreshView();
		};

		// addPath(name) - creates a new path
		cameraPaths["addPath"] = [](sol::this_state ts, sol::optional<std::string> name) -> sol::object {
			sol::state_view lua(ts);
			Editor* editor = requireEditor();
			Map* map = editor->getMap();
			if (!map) {
				throw sol::error("No map open");
			}

			std::string pathName = name.value_or("");
			if (pathName.empty()) {
				pathName = map->camera_paths.generateUniquePathName("Route");
			}

			modifyCameraPaths([&](CameraPaths& temp) {
				temp.addPath(pathName);
				temp.setActivePath(pathName);
			});

			return sol::make_object(lua, pathName);
		};

		// removePath(name) - removes a path
		cameraPaths["removePath"] = [](const std::string& name) -> bool {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return false;
			}

			return modifyCameraPaths([&](CameraPaths& temp) -> bool {
				if (!temp.getPath(name)) {
					return false;
				}
				temp.removePath(name);
				return true;
			});
		};

		// renamePath(oldName, newName) - renames a path
		cameraPaths["renamePath"] = [](const std::string& oldName, const std::string& newName) -> bool {
			if (newName.empty()) {
				return false;
			}

			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return false;
			}

			return modifyCameraPaths([&](CameraPaths& temp) -> bool {
				CameraPath* path = temp.getPath(oldName);
				if (!path) {
					return false;
				}
				// Don't allow renaming to an existing name
				if (temp.getPath(newName)) {
					return false;
				}
				path->name = newName;
				// If this was the active path, update active name
				if (temp.getActivePathName() == oldName) {
					temp.setActivePath(newName);
				}
				return true;
			});
		};

		// addKeyframe(pathName, props) - adds a keyframe to a path
		cameraPaths["addKeyframe"] = [](const std::string& pathName, sol::table props) -> bool {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return false;
			}

			return modifyCameraPaths([&](CameraPaths& temp) -> bool {
				CameraPath* path = temp.getPath(pathName);
				if (!path) return false;

				CameraKeyframe kf;
				if (props["x"].valid()) kf.pos.x = props["x"].get<int>();
				if (props["y"].valid()) kf.pos.y = props["y"].get<int>();
				if (props["z"].valid()) kf.pos.z = props["z"].get<int>();
				if (props["duration"].valid()) kf.duration = props["duration"].get<double>();
				if (props["speed"].valid()) kf.speed = props["speed"].get<double>();
				if (props["zoom"].valid()) kf.zoom = props["zoom"].get<double>();
				if (props["easing"].valid()) {
					int e = props["easing"].get<int>();
					if (isValidEasing(e)) {
						kf.easing = static_cast<CameraEasing>(e);
					}
				}

				// Insert at specified index (1-based) or append
				if (props["index"].valid()) {
					int idx = props["index"].get<int>() - 1; // Convert to 0-based
					if (idx < 0) idx = 0;
					if (idx > static_cast<int>(path->keyframes.size())) {
						idx = static_cast<int>(path->keyframes.size());
					}
					path->keyframes.insert(path->keyframes.begin() + idx, kf);
				} else {
					path->keyframes.push_back(kf);
				}
				return true;
			});
		};

		// removeKeyframe(pathName, index) - removes keyframe at 1-based index
		cameraPaths["removeKeyframe"] = [](const std::string& pathName, int luaIndex) -> bool {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return false;
			}

			int idx = luaIndex - 1; // Convert to 0-based
			return modifyCameraPaths([&](CameraPaths& temp) -> bool {
				CameraPath* p = temp.getPath(pathName);
				if (!p || idx < 0 || idx >= static_cast<int>(p->keyframes.size())) {
					return false;
				}
				p->keyframes.erase(p->keyframes.begin() + idx);
				return true;
			});
		};

		// clearKeyframes(pathName) - clears all keyframes
		cameraPaths["clearKeyframes"] = [](const std::string& pathName) -> bool {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return false;
			}

			return modifyCameraPaths([&](CameraPaths& temp) -> bool {
				CameraPath* p = temp.getPath(pathName);
				if (!p) {
					return false;
				}
				p->keyframes.clear();
				return true;
			});
		};

		// moveKeyframe(pathName, fromIndex, toIndex) - reorder keyframe (1-based)
		cameraPaths["moveKeyframe"] = [](const std::string& pathName, int fromLua, int toLua) -> bool {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return false;
			}

			int from = fromLua - 1;
			int to = toLua - 1;
			return modifyCameraPaths([&](CameraPaths& temp) -> bool {
				CameraPath* p = temp.getPath(pathName);
				if (!p) return false;

				int size = static_cast<int>(p->keyframes.size());
				if (from < 0 || from >= size || to < 0 || to >= size || from == to) {
					return false;
				}

				CameraKeyframe kf = p->keyframes[from];
				p->keyframes.erase(p->keyframes.begin() + from);
				p->keyframes.insert(p->keyframes.begin() + to, kf);
				return true;
			});
		};

		// updateKeyframe(pathName, index, props) - update keyframe properties (1-based)
		cameraPaths["updateKeyframe"] = [](const std::string& pathName, int luaIndex, sol::table props) -> bool {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return false;
			}

			int idx = luaIndex - 1;
			return modifyCameraPaths([&](CameraPaths& temp) -> bool {
				CameraPath* p = temp.getPath(pathName);
				if (!p || idx < 0 || idx >= static_cast<int>(p->keyframes.size())) {
					return false;
				}

				CameraKeyframe& kf = p->keyframes[idx];
				if (props["x"].valid()) kf.pos.x = props["x"].get<int>();
				if (props["y"].valid()) kf.pos.y = props["y"].get<int>();
				if (props["z"].valid()) kf.pos.z = props["z"].get<int>();
				if (props["duration"].valid()) kf.duration = props["duration"].get<double>();
				if (props["speed"].valid()) kf.speed = props["speed"].get<double>();
				if (props["zoom"].valid()) kf.zoom = props["zoom"].get<double>();
				if (props["easing"].valid()) {
					int e = props["easing"].get<int>();
					if (isValidEasing(e)) {
						kf.easing = static_cast<CameraEasing>(e);
					}
				}
				return true;
			});
		};

		// setShowPaths(bool) - toggle camera path visibility
		cameraPaths["setShowPaths"] = [](bool show) {
			g_settings.setInteger(Config::SHOW_CAMERA_PATHS, show ? 1 : 0);
			g_gui.RefreshView();
		};

		// isShowingPaths() - check if camera paths are visible
		cameraPaths["isShowingPaths"] = []() -> bool {
			return g_settings.getBoolean(Config::SHOW_CAMERA_PATHS);
		};

		// goToKeyframe(pathName, index) - center camera on keyframe (1-based)
		cameraPaths["goToKeyframe"] = [](const std::string& pathName, int luaIndex) -> bool {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return false;
			}

			const CameraPath* path = editor->getMap()->camera_paths.getPath(pathName);
			if (!path) {
				return false;
			}

			int idx = luaIndex - 1;
			if (idx < 0 || idx >= static_cast<int>(path->keyframes.size())) {
				return false;
			}

			const CameraKeyframe& kf = path->keyframes[idx];
			g_gui.SetScreenCenterPosition(kf.pos);
			return true;
		};

		// getActiveKeyframe() - returns 1-based index of active keyframe or nil
		cameraPaths["getActiveKeyframe"] = [](sol::this_state ts) -> sol::object {
			sol::state_view lua(ts);
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return sol::nil;
			}

			int idx = editor->getMap()->camera_paths.getActiveKeyframe();
			if (idx < 0) {
				return sol::nil;
			}
			return sol::make_object(lua, idx + 1); // Convert to 1-based
		};

		// setActiveKeyframe(index) - set active keyframe (1-based, UI selection, no undo)
		cameraPaths["setActiveKeyframe"] = [](int luaIndex) {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return;
			}
			editor->getMap()->camera_paths.setActiveKeyframe(luaIndex - 1); // Convert to 0-based
			g_gui.RefreshView();
		};

		// setPathColor(name, {r, g, b}) - set path color
		cameraPaths["setPathColor"] = [](const std::string& pathName, sol::table color) -> bool {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return false;
			}

			return modifyCameraPaths([&](CameraPaths& temp) -> bool {
				CameraPath* p = temp.getPath(pathName);
				if (!p) {
					return false;
				}
				if (color["r"].valid()) p->color.r = color["r"].get<uint8_t>();
				if (color["g"].valid()) p->color.g = color["g"].get<uint8_t>();
				if (color["b"].valid()) p->color.b = color["b"].get<uint8_t>();
				return true;
			});
		};

		// setPathLoop(name, loop) - set path loop flag
		cameraPaths["setPathLoop"] = [](const std::string& pathName, bool loop) -> bool {
			Editor* editor = g_gui.GetCurrentEditor();
			if (!editor || !editor->getMap()) {
				return false;
			}

			return modifyCameraPaths([&](CameraPaths& temp) -> bool {
				CameraPath* p = temp.getPath(pathName);
				if (!p) {
					return false;
				}
				p->loop = loop;
				return true;
			});
		};

		// Easing constants
		cameraPaths["EASING_LINEAR"] = static_cast<int>(CameraEasing::Linear);
		cameraPaths["EASING_EASE_IN_OUT"] = static_cast<int>(CameraEasing::EaseInOut);
		cameraPaths["EASING_EASE_IN"] = static_cast<int>(CameraEasing::EaseIn);
		cameraPaths["EASING_EASE_OUT"] = static_cast<int>(CameraEasing::EaseOut);
		cameraPaths["EASING_EASE_IN_OUT_CUBIC"] = static_cast<int>(CameraEasing::EaseInOutCubic);
	}

} // namespace LuaAPI
