#ifndef RME_EDITOR_CAVE_WARP_TOOL_H_
#define RME_EDITOR_CAVE_WARP_TOOL_H_

#include "editor/cave_warp.h"
#include "map/position.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class Editor;
struct NVGcontext;
struct RenderView;

// The Cave Warp brush on the map: presets, the active tool, the stroke being
// painted and the redraw it triggers on release (one undo step per stroke).
// While a tool is active the map canvas sends the left button here instead of
// to the brushes and the selection.
class CaveWarpTool {
public:
	static CaveWarpTool& Get();

	// ---- presets: data/presets/cave_warp/<name>.json ----
	std::vector<std::string> GetPresetNames() const;
	bool CreatePreset(const std::string& name, std::string& error);
	bool DeletePreset(const std::string& name);
	bool SelectPreset(const std::string& name);
	const std::string& GetPresetName() const {
		return preset_name;
	}
	const CaveWarp::Preset* GetPreset() const {
		return preset ? &*preset : nullptr;
	}
	// nullptr while the preset has no sample
	const CaveWarp::Model* GetModel() const {
		return model && !model->empty() ? model.get() : nullptr;
	}
	// Adds the selected cave as a sample; new grounds get a class guessed from it.
	bool LearnFromSelection(Editor& editor, std::string& error);
	void RemoveLastSample();
	void SetGroundClass(size_t index, uint8_t cls);
	std::string DescribePreset() const;

	// ---- preview ----
	struct Preview {
		std::vector<std::pair<Position, CaveWarp::Stack>> tiles; // positions from (0,0)
		int width = 0;
		int height = 0;
		int z = 7;
	};
	// A random test cave drawn with the current preset. With `withSelection`, the
	// preset plus the cave selected on that editor (nothing is saved).
	bool GeneratePreview(Editor* withSelection, Preview& out, std::string& error);

	// ---- tool ----
	CaveWarp::Tool GetTool() const {
		return tool;
	}
	bool IsActive() const {
		return tool != CaveWarp::Tool::None;
	}
	bool Activate(CaveWarp::Tool next, std::string& error);
	void Deactivate();

	bool IsCircle() const {
		return circle;
	}
	void SetCircle(bool value) {
		circle = value;
	}
	int GetSize() const {
		return size;
	}
	void SetSize(int value) {
		size = std::max(1, value);
	}
	int GetMargin() const {
		return margin;
	}
	void SetMargin(int value) {
		margin = std::max(1, std::min(4, value));
	}
	bool GetFixShapes() const {
		return fix_shapes;
	}
	void SetFixShapes(bool value) {
		fix_shapes = value;
	}
	bool GetProtectForeign() const {
		return protect_foreign;
	}
	void SetProtectForeign(bool value) {
		protect_foreign = value;
	}

	void OnPress(Editor& editor, const Position& pos);
	void OnDrag(Editor& editor, const Position& pos);
	void OnRelease(Editor& editor, const Position& pos);
	void OnMove(const Editor& editor, const Position& pos);

	// Stroke, cursor footprint and what the last stroke changed. Returns true
	// while the highlight is still fading (the canvas keeps repainting).
	bool DrawOverlay(NVGcontext* vg, const RenderView& view, const Editor* editor) const;

	const std::string& GetStatus() const {
		return status;
	}
	// The dialog follows tool and status changes through this.
	void SetListener(std::function<void()> fn) {
		listener = std::move(fn);
	}

private:
	CaveWarpTool() = default;

	void Notify();
	void SetStatus(std::string text);
	void Recompile();
	bool SavePreset();
	std::string PresetPath(const std::string& name) const;
	static bool BuildSample(Editor& editor, CaveWarp::Preset& preset, std::string& error);
	void Stamp(int x, int y);
	void ApplyStroke(Editor& editor);
	void DecayUsage();

	std::string preset_name;
	std::optional<CaveWarp::Preset> preset;
	std::unique_ptr<CaveWarp::Model> model;
	std::unordered_map<int, float> usage;
	std::mt19937 rng { std::random_device {}() };

	CaveWarp::Tool tool = CaveWarp::Tool::None;
	bool circle = true;
	int size = 3;
	int margin = 2;
	bool fix_shapes = true;
	bool protect_foreign = true;

	struct Stroke {
		bool active = false;
		const Editor* editor = nullptr;
		int z = 0;
		std::unordered_set<uint64_t> keys;
		std::vector<std::pair<int, int>> cells;
		int minx = 0;
		int miny = 0;
		int maxx = 0;
		int maxy = 0;
		int lastx = 0;
		int lasty = 0;
	};
	Stroke stroke;

	const Editor* cursor_editor = nullptr;
	Position cursor;

	struct Highlight {
		const Editor* editor = nullptr;
		int z = 0;
		std::vector<std::pair<int, int>> cells;
		std::chrono::steady_clock::time_point until;
	};
	Highlight highlight;

	std::string status;
	std::function<void()> listener;
};

#endif
