#include "app/main.h"
#include "editor/cave_warp_tool.h"

#include "brushes/ground/ground_brush.h"
#include "editor/action.h"
#include "editor/action_queue.h"
#include "editor/editor.h"
#include "editor/selection.h"
#include "game/item.h"
#include "map/map.h"
#include "map/tile.h"
#include "map/tile_operations.h"
#include "rendering/core/render_view.h"
#include "ui/gui.h"
#include "util/file_system.h"

#include <nanovg.h>
#include <wx/dir.h>
#include <wx/filename.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <format>

using namespace CaveWarp;

namespace {
	constexpr int HIGHLIGHT_MS = 2500;
	constexpr int PREVIEW_W = 40;
	constexpr int PREVIEW_H = 28;

	uint64_t cellKey(int x, int y) {
		return (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32) | static_cast<uint32_t>(x);
	}

	struct ToolStyle {
		const char* verb;
		unsigned char r;
		unsigned char g;
		unsigned char b;
	};

	ToolStyle styleOf(Tool tool) {
		switch (tool) {
			case Tool::Open:
				return { "Opened", 90, 210, 120 };
			case Tool::Close:
				return { "Closed", 220, 90, 60 };
			case Tool::Extra:
				return { "Flooded", 70, 130, 240 };
			case Tool::Dry:
				return { "Dried", 220, 190, 90 };
			case Tool::Reroll:
				return { "Rerolled", 190, 110, 230 };
			default:
				return { "", 255, 255, 255 };
		}
	}

	const char* reasonText(int reason) {
		switch (reason) {
			case ThinWall:
				return "would be a 1-tile wall between two floors";
			case ThinFloor:
				return "would be a 1-tile corridor";
			case Pinch:
				return "floors touching only diagonally";
			case Margin:
				return "margin around the water";
			case ThinLand:
				return "1 tile of land between two waters";
			case ThinExtra:
				return "1-tile wide water";
			case UnknownShape:
				return "shapes the model does not have";
			default:
				return "";
		}
	}

	std::string presetsDirectory() {
		const wxString base = FileSystem::GetDataDirectory() + "/presets";
		if (!wxDirExists(base)) {
			wxMkdir(base);
		}
		const wxString dir = base + "/cave_warp";
		if (!wxDirExists(dir)) {
			wxMkdir(dir);
		}
		return dir.ToStdString();
	}

	// File-system safe preset name (it is also the file name).
	std::string sanitizeName(const std::string& name) {
		std::string out;
		for (unsigned char c : name) {
			if (std::isalnum(c) || c == '-' || c == '_') {
				out += static_cast<char>(c);
			} else if (c == ' ' || c == '.') {
				out += '_';
			}
		}
		while (!out.empty() && out.front() == '_') {
			out.erase(out.begin());
		}
		while (!out.empty() && out.back() == '_') {
			out.pop_back();
		}
		return out;
	}

	// Row runs of a cell list, so the overlay draws one rect per run.
	template <typename Fn>
	void forEachRun(std::vector<std::pair<int, int>> cells, Fn&& fn) {
		std::sort(cells.begin(), cells.end(), [](const auto& a, const auto& b) {
			return a.second != b.second ? a.second < b.second : a.first < b.first;
		});
		size_t i = 0;
		while (i < cells.size()) {
			const int y = cells[i].second;
			const int start = cells[i].first;
			int end = start;
			size_t j = i + 1;
			while (j < cells.size() && cells[j].second == y && cells[j].first <= end + 1) {
				end = std::max(end, cells[j].first);
				++j;
			}
			fn(start, y, end - start + 1);
			i = j;
		}
	}
}

CaveWarpTool& CaveWarpTool::Get() {
	static CaveWarpTool instance;
	return instance;
}

void CaveWarpTool::Notify() {
	if (listener) {
		listener();
	}
}

void CaveWarpTool::SetStatus(std::string text) {
	status = std::move(text);
	Notify();
}

// ============================================================================
// Presets
// ============================================================================

std::string CaveWarpTool::PresetPath(const std::string& name) const {
	return presetsDirectory() + "/" + name + ".json";
}

std::vector<std::string> CaveWarpTool::GetPresetNames() const {
	std::vector<std::string> names;
	wxDir dir(presetsDirectory());
	if (!dir.IsOpened()) {
		return names;
	}
	wxString file;
	bool more = dir.GetFirst(&file, "*.json", wxDIR_FILES);
	while (more) {
		names.push_back(wxFileName(file).GetName().ToStdString());
		more = dir.GetNext(&file);
	}
	std::sort(names.begin(), names.end());
	return names;
}

bool CaveWarpTool::CreatePreset(const std::string& name, std::string& error) {
	const std::string clean = sanitizeName(name);
	if (clean.empty()) {
		error = "Type a name using letters, digits, '-' or '_'.";
		return false;
	}
	if (wxFileExists(PresetPath(clean))) {
		return SelectPreset(clean);
	}
	Deactivate();
	preset_name = clean;
	preset = Preset {};
	if (!SavePreset()) {
		error = status;
		return false;
	}
	Recompile();
	Notify();
	return true;
}

bool CaveWarpTool::DeletePreset(const std::string& name) {
	if (name.empty() || !wxRemoveFile(PresetPath(name))) {
		return false;
	}
	if (name == preset_name) {
		Deactivate();
		preset_name.clear();
		preset.reset();
		model.reset();
	}
	Notify();
	return true;
}

bool CaveWarpTool::SelectPreset(const std::string& name) {
	if (name == preset_name && preset) {
		return true;
	}
	Preset loaded;
	std::string error;
	if (!loaded.load(PresetPath(name), error)) {
		SetStatus(error);
		return false;
	}
	Deactivate();
	preset_name = name;
	preset = std::move(loaded);
	Recompile();
	Notify();
	return true;
}

bool CaveWarpTool::SavePreset() {
	if (!preset) {
		return false;
	}
	std::string error;
	if (!preset->save(PresetPath(preset_name), error)) {
		SetStatus(error);
		return false;
	}
	return true;
}

void CaveWarpTool::Recompile() {
	usage.clear();
	if (!preset) {
		model.reset();
		return;
	}
	model = std::make_unique<Model>();
	model->compile(*preset);
}

bool CaveWarpTool::BuildSample(Editor& editor, Preset& target, std::string& error) {
	const Selection& selection = editor.selection;
	if (selection.empty()) {
		error = "Select a finished cave on the map first (with about 4 tiles of rock around it).";
		return false;
	}
	const Position minPos = selection.minPosition();
	const Position maxPos = selection.maxPosition();
	if (minPos.z != maxPos.z) {
		error = "Select tiles of a single floor.";
		return false;
	}
	const int w = maxPos.x - minPos.x + 1;
	const int h = maxPos.y - minPos.y + 1;
	if (w < 6 || h < 6) {
		error = "The selection is too small (at least 6x6).";
		return false;
	}
	if (w > 256 || h > 256) {
		error = "The selection is too big (at most 256x256).";
		return false;
	}

	Sample sample;
	sample.w = w;
	sample.h = h;
	sample.x = minPos.x;
	sample.y = minPos.y;
	sample.z = minPos.z;
	sample.map = editor.map.getName();
	sample.stacks.reserve(static_cast<size_t>(w) * h);

	std::vector<uint16_t> groundAt(static_cast<size_t>(w) * h, 0);
	std::unordered_map<uint16_t, std::string> brushOf;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const Tile* tile = editor.map.getTile(minPos.x + x, minPos.y + y, minPos.z);
			if (tile && tile->isSelected() && tile->ground) {
				sample.stacks.push_back(Stack::fromTile(tile).key());
				const uint16_t gid = tile->ground->getID();
				groundAt[static_cast<size_t>(y) * w + x] = gid;
				++sample.tiles;
				if (!brushOf.contains(gid)) {
					const GroundBrush* brush = tile->ground->getGroundBrush();
					brushOf[gid] = brush ? brush->getName() : std::string();
				}
			} else {
				sample.stacks.emplace_back("?");
			}
		}
	}
	if (sample.tiles < 36) {
		error = "The selection has too few tiles with ground.";
		return false;
	}

	// Rock = the ground that dominates the edge of the selected area (outside it
	// is rock), plus the variations of the same brush.
	const auto ground = [&](int x, int y) -> uint16_t {
		if (x < 0 || y < 0 || x >= w || y >= h) {
			return 0;
		}
		return groundAt[static_cast<size_t>(y) * w + x];
	};
	std::unordered_map<uint16_t, int> total;
	std::unordered_map<uint16_t, int> edge;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const uint16_t g = ground(x, y);
			if (g == 0) {
				continue;
			}
			++total[g];
			if (!ground(x - 1, y) || !ground(x + 1, y) || !ground(x, y - 1) || !ground(x, y + 1)) {
				++edge[g];
			}
		}
	}
	int edgeMax = 0;
	uint16_t edgeTop = 0;
	for (const auto& [g, n] : edge) {
		if (n > edgeMax) {
			edgeMax = n;
			edgeTop = g;
		}
	}
	const std::string rockBrush = edgeTop ? brushOf[edgeTop] : std::string();

	for (const auto& [g, n] : total) {
		auto it = std::find_if(target.grounds.begin(), target.grounds.end(), [g](const GroundClass& gc) { return gc.id == g; });
		if (it == target.grounds.end()) {
			const auto e = edge.find(g);
			const bool onEdge = edgeMax > 0 && e != edge.end() && e->second >= edgeMax * 0.2;
			const bool sameBrush = !rockBrush.empty() && brushOf[g] == rockBrush;
			target.grounds.push_back({ g, static_cast<uint8_t>(onEdge || sameBrush ? ROCK : FLOOR), 0 });
			it = target.grounds.end() - 1;
		}
		it->count += n;
	}
	std::sort(target.grounds.begin(), target.grounds.end(), [](const GroundClass& a, const GroundClass& b) {
		return a.count > b.count;
	});

	target.samples.push_back(std::move(sample));
	return true;
}

bool CaveWarpTool::LearnFromSelection(Editor& editor, std::string& error) {
	if (!preset) {
		error = "Create a preset first.";
		return false;
	}
	if (!BuildSample(editor, *preset, error)) {
		return false;
	}
	SavePreset();
	Recompile();
	SetStatus("Sample added. Check the class of each ground and the test cave.");
	return true;
}

void CaveWarpTool::RemoveLastSample() {
	if (!preset || preset->samples.empty()) {
		return;
	}
	preset->samples.pop_back();

	// recount and drop the grounds that no sample has any more
	std::unordered_map<uint16_t, int> count;
	for (const Sample& sample : preset->samples) {
		for (const std::string& text : sample.stacks) {
			Stack st;
			if (text != "?" && Stack::parse(text, st) && st.ground != 0) {
				++count[st.ground];
			}
		}
	}
	std::vector<GroundClass> kept;
	for (GroundClass g : preset->grounds) {
		if (const auto it = count.find(g.id); it != count.end()) {
			g.count = it->second;
			kept.push_back(g);
		}
	}
	preset->grounds = std::move(kept);
	SavePreset();
	Recompile();
	SetStatus("Last sample removed.");
}

void CaveWarpTool::SetGroundClass(size_t index, uint8_t cls) {
	if (!preset || index >= preset->grounds.size() || cls >= CLASS_COUNT) {
		return;
	}
	preset->grounds[index].cls = cls;
	SavePreset();
	Recompile();
	Notify();
}

std::string CaveWarpTool::DescribePreset() const {
	if (!preset) {
		return "Create a preset and teach it a finished cave.";
	}
	if (preset->samples.empty()) {
		return "No samples: select a finished cave and click 'Learn from selection'.";
	}
	int tiles = 0;
	for (const Sample& sample : preset->samples) {
		tiles += sample.tiles;
	}
	std::string text = std::format("{} sample(s), {} tiles", preset->samples.size(), tiles);
	if (model) {
		text += std::format(" | rock {}, floor {}, water/swamp {}", model->classCount[ROCK], model->classCount[FLOOR], model->classCount[EXTRA]);
	}
	return text;
}

// ============================================================================
// Preview
// ============================================================================

bool CaveWarpTool::GeneratePreview(Editor* withSelection, Preview& out, std::string& error) {
	out = Preview {};
	const Model* use = GetModel();
	Model temporary;
	int z = preset && !preset->samples.empty() ? preset->samples.front().z : 7;
	if (withSelection) {
		Preset copy = preset ? *preset : Preset {};
		if (!BuildSample(*withSelection, copy, error)) {
			return false;
		}
		temporary.compile(copy);
		use = &temporary;
		z = copy.samples.back().z;
	}
	if (!use || use->empty()) {
		error = "The preset has no sample yet: select a finished cave and click 'Learn from selection'.";
		return false;
	}

	const std::vector<PreviewTile> tiles = generatePreview(*use, PREVIEW_W, PREVIEW_H, rng);
	out.width = PREVIEW_W + 4;
	out.height = PREVIEW_H + 4;
	out.z = z;
	out.tiles.reserve(tiles.size());
	for (const PreviewTile& tile : tiles) {
		out.tiles.emplace_back(Position(tile.x, tile.y, z), use->stacks[tile.sid]);
	}
	return true;
}

// ============================================================================
// Tool
// ============================================================================

bool CaveWarpTool::Activate(Tool next, std::string& error) {
	if (!GetModel()) {
		error = "The preset has no sample yet: teach it a finished cave first.";
		return false;
	}
	tool = next;
	stroke = Stroke {};
	Notify();
	g_gui.RefreshView();
	return true;
}

void CaveWarpTool::Deactivate() {
	if (tool == Tool::None && !stroke.active) {
		return;
	}
	tool = Tool::None;
	stroke = Stroke {};
	cursor_editor = nullptr;
	Notify();
	g_gui.RefreshView();
}

void CaveWarpTool::Stamp(int x, int y) {
	for (const auto& [cx, cy] : brushCells(x, y, size, circle)) {
		if (cx < 0 || cy < 0) {
			continue;
		}
		if (!stroke.keys.insert(cellKey(cx, cy)).second) {
			continue;
		}
		stroke.cells.emplace_back(cx, cy);
		stroke.minx = std::min(stroke.minx, cx);
		stroke.miny = std::min(stroke.miny, cy);
		stroke.maxx = std::max(stroke.maxx, cx);
		stroke.maxy = std::max(stroke.maxy, cy);
	}
}

void CaveWarpTool::OnPress(Editor& editor, const Position& pos) {
	if (!IsActive()) {
		return;
	}
	if (!GetModel()) {
		SetStatus("The preset has no sample yet: teach it a finished cave first.");
		return;
	}
	highlight = Highlight {};
	stroke = Stroke {};
	stroke.active = true;
	stroke.editor = &editor;
	stroke.z = pos.z;
	stroke.minx = INT_MAX;
	stroke.miny = INT_MAX;
	stroke.maxx = INT_MIN;
	stroke.maxy = INT_MIN;
	stroke.lastx = pos.x;
	stroke.lasty = pos.y;
	Stamp(pos.x, pos.y);
	cursor_editor = &editor;
	cursor = pos;
}

void CaveWarpTool::OnDrag(Editor& editor, const Position& pos) {
	cursor_editor = &editor;
	cursor = pos;
	if (!stroke.active || stroke.editor != &editor || pos.z != stroke.z) {
		return;
	}
	for (const auto& [x, y] : linePoints(stroke.lastx, stroke.lasty, pos.x, pos.y)) {
		Stamp(x, y);
	}
	stroke.lastx = pos.x;
	stroke.lasty = pos.y;
}

void CaveWarpTool::OnRelease(Editor& editor, const Position& pos) {
	if (!stroke.active) {
		return;
	}
	OnDrag(editor, pos);
	if (stroke.editor == &editor && !stroke.cells.empty() && IsActive() && GetModel()) {
		ApplyStroke(editor);
	}
	stroke = Stroke {};
}

void CaveWarpTool::OnMove(const Editor& editor, const Position& pos) {
	cursor_editor = &editor;
	cursor = pos;
}

void CaveWarpTool::DecayUsage() {
	for (auto it = usage.begin(); it != usage.end();) {
		it->second *= 0.5f;
		if (it->second < 0.25f) {
			it = usage.erase(it);
		} else {
			++it;
		}
	}
}

void CaveWarpTool::ApplyStroke(Editor& editor) {
	const Model& m = *model;
	const auto started = std::chrono::steady_clock::now();

	const int MG = readMargin(margin);
	const int x1 = stroke.minx - MG;
	const int y1 = stroke.miny - MG;
	const int x2 = stroke.maxx + MG;
	const int y2 = stroke.maxy + MG;
	Target T;
	T.read(m, editor.map, x1, y1, x2, y2, stroke.z, protect_foreign);

	std::vector<int> cells;
	cells.reserve(stroke.cells.size());
	for (const auto& [cx, cy] : stroke.cells) {
		cells.push_back((cy - y1) * T.W + (cx - x1));
	}
	Plan plan = planStroke(m, T, tool, cells, stroke.minx - x1, stroke.miny - y1, stroke.maxx - x1, stroke.maxy - y1, margin, fix_shapes);
	if (plan.painted == 0) {
		SetStatus("Nothing to change here.");
		return;
	}

	DecayUsage();
	SynthOptions opts;
	opts.usage = &usage;
	opts.avoid = tool == Tool::Reroll ? &plan.avoid : nullptr;
	opts.jitter = tool == Tool::Reroll ? 3.0f : 1.0f;
	synthesize(m, T, plan.todo, opts, rng);

	std::vector<int> writes;
	int missing = 0;
	for (int t : plan.todo) {
		const int32_t s = T.sid[t];
		if (s <= 0) {
			++missing;
		} else if (m.stackKeys[s] != T.key[t]) {
			writes.push_back(t);
		}
	}

	highlight = Highlight {};
	highlight.editor = &editor;
	highlight.z = stroke.z;
	highlight.until = std::chrono::steady_clock::now() + std::chrono::milliseconds(HIGHLIGHT_MS);
	if (!writes.empty()) {
		auto batch = editor.actionQueue->createBatch(ACTION_DRAW);
		auto action = editor.actionQueue->createAction(batch.get());
		for (int t : writes) {
			const int mx = x1 + t % T.W;
			const int my = y1 + t / T.W;
			const Position pos(mx, my, stroke.z);
			Tile* existing = editor.map.getTile(pos);
			std::unique_ptr<Tile> tile = existing ? TileOperations::deepCopy(existing, editor.map) : std::make_unique<Tile>(mx, my, stroke.z);
			m.stacks[T.sid[t]].applyTo(*tile);
			action->addChange(std::make_unique<Change>(std::move(tile)));
			highlight.cells.emplace_back(mx, my);
		}
		batch->addAndCommitAction(std::move(action));
		editor.addBatch(std::move(batch));
	}

	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	std::string text = std::format("{} {} tiles.", styleOf(tool).verb, plan.painted);
	if (plan.ruleCount > 0) {
		std::string parts;
		for (int r = 0; r < REASON_COUNT; ++r) {
			if (plan.reasons[r] > 0) {
				parts += std::format("{}{}: {}", parts.empty() ? "" : "; ", plan.reasons[r], reasonText(r));
			}
		}
		text += " Also adjusted " + parts + ".";
	}
	text += std::format(" Redrawn {} tiles ({:.2f} s).", writes.size(), seconds);
	if (missing > 0) {
		text += std::format(" {} without a model (the preset has no tile of that class).", missing);
	}
	SetStatus(std::move(text));
}

// ============================================================================
// Overlay
// ============================================================================

bool CaveWarpTool::DrawOverlay(NVGcontext* vg, const RenderView& view, const Editor* editor) const {
	if (!vg || !editor) {
		return false;
	}
	const auto now = std::chrono::steady_clock::now();
	const bool highlightAlive = highlight.editor == editor && !highlight.cells.empty() && now < highlight.until;
	if (!highlightAlive && !IsActive()) {
		return false;
	}

	const float zoom = view.zoom;
	const float tile = TILE_SIZE / zoom;
	int originX = 0;
	int originY = 0;
	view.getScreenPosition(0, 0, view.floor, originX, originY);
	const auto fill = [&](const std::vector<std::pair<int, int>>& cells, NVGcolor color) {
		nvgBeginPath(vg);
		forEachRun(cells, [&](int x, int y, int run) {
			nvgRect(vg, (originX + x * TILE_SIZE) / zoom, (originY + y * TILE_SIZE) / zoom, run * tile, tile);
		});
		nvgFillColor(vg, color);
		nvgFill(vg);
	};

	nvgSave(vg);
	if (highlightAlive && highlight.z == view.floor) {
		const double left = std::chrono::duration<double, std::milli>(highlight.until - now).count() / HIGHLIGHT_MS;
		fill(highlight.cells, nvgRGBA(235, 150, 40, static_cast<unsigned char>(90 * std::clamp(left, 0.0, 1.0))));
	}
	if (IsActive()) {
		const ToolStyle style = styleOf(tool);
		if (stroke.active && stroke.editor == editor) {
			if (stroke.z == view.floor) {
				fill(stroke.cells, nvgRGBA(style.r, style.g, style.b, 110));
			}
		} else if (cursor_editor == editor && cursor.z == view.floor) {
			fill(brushCells(cursor.x, cursor.y, size, circle), nvgRGBA(style.r, style.g, style.b, 70));
		}
	}
	nvgRestore(vg);
	return highlightAlive;
}
