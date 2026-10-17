#ifndef RME_EDITOR_CAVE_WARP_H_
#define RME_EDITOR_CAVE_WARP_H_

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class BaseMap;
class Tile;

// Cave Warp: the mapper paints only the SHAPE of a cave (rock / floor /
// water-swamp) and every redrawn tile copies its whole stack (ground, borders
// and details) from a tile of a hand-made model cave whose surroundings match
// ("texture by numbers"): the 3x3 of classes around it must be identical, the
// 9x9 as close as possible, and it should continue the stretch of the model its
// neighbours already copied. Borders, wall rocks and detail clusters then come
// out grouped and placed like in the model, with no hand-written rules.
namespace CaveWarp {

	enum Class : uint8_t {
		ROCK = 0,
		FLOOR = 1,
		EXTRA = 2, // water / swamp: painted apart with its own brush
		UNKNOWN = 3,
	};
	constexpr int CLASS_COUNT = 3;

	enum class Tool {
		None,
		Open,
		Close,
		Extra,
		Dry,
		Reroll,
	};

	// Why the shape fixes touched a tile (status line).
	enum Reason {
		ThinWall,
		ThinFloor,
		Pinch,
		Margin,
		ThinLand,
		ThinExtra,
		UnknownShape,
		REASON_COUNT,
	};

	// Ground + items from bottom to top; subtype 0 = none.
	struct Stack {
		uint16_t ground = 0;
		std::vector<std::pair<uint16_t, uint16_t>> items;

		// "ground|id,id:subtype,..."
		std::string key() const;
		static bool parse(std::string_view text, Stack& out);
		static Stack fromTile(const Tile* tile);
		// Replaces ground and items; house, flags, creature and spawn stay.
		void applyTo(Tile& tile) const;
	};

	struct Sample {
		int w = 0;
		int h = 0;
		int x = 0;
		int y = 0;
		int z = 0;
		std::string map;
		int tiles = 0;
		std::vector<std::string> stacks; // w*h, "?" = outside the selection
	};

	struct GroundClass {
		uint16_t id = 0;
		uint8_t cls = FLOOR;
		int count = 0;
	};

	struct Preset {
		std::vector<Sample> samples;
		std::vector<GroundClass> grounds;

		bool load(const std::string& path, std::string& error);
		bool save(const std::string& path, std::string& error) const;
	};

	// Shapes the model shows. A shape fix only acts on a shape the model never
	// has, because there is nothing to copy its borders from.
	struct Rules {
		bool thinWall = false; // rock with floor on both sides
		bool thinFloor = false; // floor with rock on both sides (1-wide corridor)
		bool pinch = false; // open tiles touching only diagonally
		bool extraTouchRock = false; // water right against the rock
		bool thinLandExtra = false; // 1 tile of land between two waters
		bool thinExtra = false; // 1-wide water
	};

	struct Model {
		int GW = 0;
		int GH = 0;
		int N = 0;
		std::vector<uint8_t> lab;
		std::vector<int32_t> sid; // 0 = no stack
		std::vector<uint32_t> rowCode;
		std::vector<Stack> stacks; // indexed by sid, [0] unused
		std::vector<std::string> stackKeys;
		std::unordered_map<std::string, int32_t> intern;
		std::unordered_map<uint16_t, uint8_t> classOf;
		std::unordered_map<uint32_t, std::vector<int>> buckets; // exact 3x3 -> positions
		std::vector<uint32_t> keyList;
		std::unordered_map<int32_t, std::vector<int>> bySid;
		std::array<std::vector<int>, CLASS_COUNT> byLabel;
		std::unordered_map<int32_t, std::unordered_set<uint64_t>> keep; // sid -> 5x5 shapes it appears in
		mutable std::unordered_map<uint32_t, int64_t> nearCache;
		Rules rules;
		int positions = 0;
		std::array<int, CLASS_COUNT> classCount {};
		int32_t commonRock = 0; // most frequent rock stack

		void compile(const Preset& preset);
		bool empty() const {
			return positions == 0;
		}
		int32_t stackId(const std::string& key) const;
	};

	// The part of the map being redrawn, in local coordinates (index = y*W + x).
	struct Target {
		int W = 0;
		int H = 0;
		int ox = 0;
		int oy = 0;
		int z = 0;
		std::vector<uint8_t> lab; // current class (the brush and the fixes change it)
		std::vector<uint8_t> orig; // class read from the map
		std::vector<int32_t> sid; // map stack as a model id, -1 = a stack the model does not have
		std::vector<std::string> key; // map stack as text
		std::vector<uint8_t> locked; // never redrawn (action/unique id, door, text, house)
		std::vector<uint8_t> foreign; // ground outside the preset: changes only where painted
		std::vector<uint32_t> rowCode;

		void resize(int w, int h);
		void computeRowCodes();
		void read(const Model& model, BaseMap& map, int x1, int y1, int x2, int y2, int floor, bool protectForeign);
	};

	struct Plan {
		std::vector<int> todo;
		std::unordered_map<int, int32_t> avoid; // Reroll: the old stack of each tile
		int painted = 0;
		int ruleCount = 0;
		std::array<int, REASON_COUNT> reasons {};
	};

	struct SynthOptions {
		std::unordered_map<int, float>* usage = nullptr; // how often each model position was copied
		const std::unordered_map<int, int32_t>* avoid = nullptr;
		int K = 12;
		float wCoh = 2.0f;
		float wUse = 1.5f;
		float jitter = 1.0f;
	};

	// Tiles read around a stroke: the redrawn ring, the shape fixes and the 9x9 window.
	int readMargin(int margin);

	// Paints the stroke into T, fixes the shapes the model does not have and
	// decides which tiles to redraw. Leaves T.rowCode ready.
	Plan planStroke(const Model& model, Target& T, Tool tool, const std::vector<int>& cells, int x1, int y1, int x2, int y2, int margin, bool fixShapes);

	// Picks a model stack for every tile in `todo`. Afterwards T.sid[t] > 0, or
	// -1 when the model has no tile of that class.
	void synthesize(const Model& model, Target& T, const std::vector<int>& todo, const SynthOptions& opts, std::mt19937& rng);

	std::vector<std::pair<int, int>> brushCells(int cx, int cy, int size, bool circle);
	std::vector<std::pair<int, int>> linePoints(int x0, int y0, int x1, int y1);
	bool isLockedTile(const Tile* tile);

	// A random test cave drawn with the model, to check what was learned.
	struct PreviewTile {
		int x = 0;
		int y = 0;
		int32_t sid = 0;
	};
	std::vector<PreviewTile> generatePreview(const Model& model, int w, int h, std::mt19937& rng);
}

#endif
