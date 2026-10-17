#include "app/main.h"
#include "editor/cave_warp.h"

#include "game/item.h"
#include "map/basemap.h"
#include "map/tile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <charconv>
#include <climits>
#include <cmath>
#include <fstream>
#include <limits>

namespace CaveWarp {

	namespace {
		// Half of the 9x9 window: model and target are read with this margin.
		constexpr int PAD = 4;

		// A row of 9 cells is an 18-bit integer, 2 bits per class. Difference of two
		// rows: xor, fold each cell's 2 bits onto the even bit, count. MASK5/MASK3
		// keep the 5/3 middle cells.
		constexpr uint32_t ALL9 = 0x15555;
		constexpr uint32_t MASK5 = 0x1550;
		constexpr uint32_t MASK3 = 0x540;
		constexpr uint32_t UNKNOWN_ROW = 0x3FFFF;

		int cellCount(uint32_t m) {
			return std::popcount(m);
		}

		bool isOpen(uint8_t c) {
			return c == FLOOR || c == EXTRA;
		}

		std::vector<uint32_t> buildRowCodes(const std::vector<uint8_t>& lab, int W, int H) {
			std::vector<uint32_t> codes(static_cast<size_t>(W) * H, UNKNOWN_ROW);
			for (int y = 0; y < H; ++y) {
				const int base = y * W;
				for (int x = PAD; x < W - PAD; ++x) {
					const int idx = base + x;
					uint32_t code = 0;
					for (int dx = -4; dx <= 4; ++dx) {
						code |= static_cast<uint32_t>(lab[idx + dx]) << (2 * (dx + 4));
					}
					codes[idx] = code;
				}
			}
			return codes;
		}

		// Exact 3x3 (18 bits): top, middle and bottom rows.
		uint32_t key3(const uint32_t* codes, int idx, int W) {
			return ((codes[idx - W] >> 6) & 0x3F) | (((codes[idx] >> 6) & 0x3F) << 6) | (((codes[idx + W] >> 6) & 0x3F) << 12);
		}

		// Exact 5x5 (50 bits).
		uint64_t key5(const uint32_t* codes, int idx, int W) {
			uint64_t k = 0;
			for (int r = -2; r <= 2; ++r) {
				k = (k << 10) | ((codes[idx + r * W] >> 4) & 0x3FF);
			}
			return k;
		}

		// 3x3 straight from the classes (same layout as key3).
		uint32_t labKey3(const std::vector<uint8_t>& lab, int W, int t) {
			uint32_t k = 0;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					k |= static_cast<uint32_t>(lab[t + dy * W + dx]) << (2 * (dx + 1) + 6 * (dy + 1));
				}
			}
			return k;
		}

		bool hasUnknown9(uint32_t k) {
			return (k & (k >> 1) & ALL9) != 0;
		}

		uint32_t center3(uint32_t k) {
			return (k >> 8) & 3;
		}

		bool parseInt(std::string_view text, int& out) {
			if (text.empty()) {
				return false;
			}
			const auto res = std::from_chars(text.data(), text.data() + text.size(), out);
			return res.ec == std::errc() && res.ptr == text.data() + text.size();
		}

		bool isSpecialItem(const Item* item) {
			return item->getActionID() > 0 || item->getUniqueID() > 0 || item->isDoor() || !item->getText().empty();
		}

		Rules detectRules(const Model& m) {
			Rules r;
			const int GW = m.GW;
			const auto& lab = m.lab;
			for (int idx = GW + 1; idx < m.N - GW - 1; ++idx) {
				const uint8_t c = lab[idx];
				if (c == UNKNOWN) {
					continue;
				}
				const uint8_t n = lab[idx - GW], s = lab[idx + GW], e = lab[idx + 1], w = lab[idx - 1];
				if (c == ROCK) {
					if ((isOpen(n) && isOpen(s)) || (isOpen(e) && isOpen(w))) {
						r.thinWall = true;
					}
				} else if ((n == ROCK && s == ROCK) || (e == ROCK && w == ROCK)) {
					r.thinFloor = true;
				}
				if (c == EXTRA) {
					for (int dy = -1; dy <= 1; ++dy) {
						for (int dx = -1; dx <= 1; ++dx) {
							if (lab[idx + dy * GW + dx] == ROCK) {
								r.extraTouchRock = true;
							}
						}
					}
					const bool nv = n != EXTRA && n != UNKNOWN && s != EXTRA && s != UNKNOWN;
					const bool nh = e != EXTRA && e != UNKNOWN && w != EXTRA && w != UNKNOWN;
					if (nv || nh) {
						r.thinExtra = true;
					}
				} else if (c == FLOOR) {
					if ((n == EXTRA && s == EXTRA) || (e == EXTRA && w == EXTRA)) {
						r.thinLandExtra = true;
					}
				}
				// 2x2 with open tiles only on the diagonal
				const uint8_t b = lab[idx + 1], cc = lab[idx + GW], d = lab[idx + GW + 1];
				if (b != UNKNOWN && cc != UNKNOWN && d != UNKNOWN) {
					const bool A = isOpen(c), B = isOpen(b), C = isOpen(cc), D = isOpen(d);
					if (A == D && B == C && A != B) {
						r.pinch = true;
					}
				}
			}
			return r;
		}

		int64_t nearestKey(const Model& model, uint32_t k3) {
			if (auto it = model.nearCache.find(k3); it != model.nearCache.end()) {
				return it->second;
			}
			const uint32_t center = center3(k3);
			int64_t best = -1;
			int bestD = INT_MAX;
			for (uint32_t k : model.keyList) {
				if (center3(k) != center) {
					continue;
				}
				const uint32_t x = k ^ k3;
				const int d = cellCount((x | (x >> 1)) & ALL9);
				if (d < bestD) {
					best = k;
					bestD = d;
				}
			}
			model.nearCache.emplace(k3, best);
			return best;
		}

		bool isKeepable(const Model& model, const Target& T, int t) {
			const int32_t s = T.sid[t];
			if (s <= 0) {
				return false;
			}
			const auto it = model.keep.find(s);
			return it != model.keep.end() && it->second.contains(key5(T.rowCode.data(), t, T.W));
		}

		uint8_t paintedClass(Tool tool, uint8_t cur) {
			switch (tool) {
				case Tool::Open:
					return cur == ROCK ? FLOOR : cur;
				case Tool::Close:
					return ROCK;
				case Tool::Extra:
					return EXTRA;
				case Tool::Dry:
					return cur == EXTRA ? FLOOR : cur;
				default:
					return cur;
			}
		}

		// Fixes what the brush left and the model never shows. Only touches
		// [x1..x2]x[y1..y2] (local, away from the grid edge).
		int applyShapeRules(Target& T, Tool mode, const Rules& rules, int x1, int y1, int x2, int y2, std::array<int, REASON_COUNT>& reasons) {
			const int W = T.W;
			auto& lab = T.lab;
			int changed = 0;
			const auto set = [&](int t, uint8_t c, Reason reason) {
				if (lab[t] != c && !T.locked[t] && !T.foreign[t]) {
					lab[t] = c;
					++changed;
					++reasons[reason];
					return true;
				}
				return false;
			};
			const bool opening = mode == Tool::Open || mode == Tool::Extra;

			for (int pass = 0; pass < 8; ++pass) {
				bool any = false;
				for (int y = y1; y <= y2; ++y) {
					for (int x = x1; x <= x2; ++x) {
						const int t = y * W + x;
						const uint8_t c = lab[t];
						const uint8_t n = lab[t - W], s = lab[t + W], e = lab[t + 1], w = lab[t - 1];

						if (opening) {
							if (c == ROCK && !rules.thinWall && ((isOpen(n) && isOpen(s)) || (isOpen(e) && isOpen(w)))) {
								any = set(t, FLOOR, ThinWall) || any;
							}
						} else if (mode == Tool::Close) {
							if (c != ROCK && !rules.thinFloor && ((n == ROCK && s == ROCK) || (e == ROCK && w == ROCK))) {
								any = set(t, ROCK, ThinFloor) || any;
							}
						}

						if (mode == Tool::Extra) {
							if (c == ROCK && !rules.extraTouchRock) {
								for (int dy = -1; dy <= 1; ++dy) {
									for (int dx = -1; dx <= 1; ++dx) {
										if (lab[t + dy * W + dx] == EXTRA) {
											any = set(t, FLOOR, Margin) || any;
										}
									}
								}
							} else if (c == FLOOR && !rules.thinLandExtra && ((n == EXTRA && s == EXTRA) || (e == EXTRA && w == EXTRA))) {
								any = set(t, EXTRA, ThinLand) || any;
							}
						} else if (mode == Tool::Dry) {
							if (c == FLOOR && !rules.thinLandExtra) {
								// 1 tile of land between two waters: dry one more so it has 2
								if (n == EXTRA && s == EXTRA) {
									any = set(t + W, FLOOR, ThinLand) || any;
								} else if (e == EXTRA && w == EXTRA) {
									any = set(t + 1, FLOOR, ThinLand) || any;
								}
							} else if (c == EXTRA && !rules.thinExtra) {
								if ((n != EXTRA && s != EXTRA) || (e != EXTRA && w != EXTRA)) {
									any = set(t, FLOOR, ThinExtra) || any;
								}
							}
						}

						// open tiles touching only diagonally
						if (!rules.pinch && mode != Tool::Dry && x < x2 && y < y2) {
							const int tb = t + 1, tc = t + W, td = t + W + 1;
							const bool A = isOpen(c), B = isOpen(lab[tb]), C = isOpen(lab[tc]), D = isOpen(lab[td]);
							if (A == D && B == C && A != B) {
								if (mode == Tool::Close) {
									// close the open pair
									if (A) {
										any = set(t, ROCK, Pinch) || any;
										any = set(td, ROCK, Pinch) || any;
									} else {
										any = set(tb, ROCK, Pinch) || any;
										any = set(tc, ROCK, Pinch) || any;
									}
								} else {
									// open the rock pair
									if (A) {
										any = set(tb, FLOOR, Pinch) || any;
										any = set(tc, FLOOR, Pinch) || any;
									} else {
										any = set(t, FLOOR, Pinch) || any;
										any = set(td, FLOOR, Pinch) || any;
									}
								}
							}
						}
					}
				}
				if (!any) {
					break;
				}
			}
			return changed;
		}

		// Generalises the rules above: where a 3x3 the model never shows is left
		// (nothing to copy its borders from), flip the tile around it that removes
		// the most such cases, always in the tool's direction (open only opens,
		// close only closes). The tile itself may flip with no gain (it just must
		// not get worse); since flips go one way only, this always ends.
		int repairUnknownShapes(const Model& model, Target& T, Tool mode, int x1, int y1, int x2, int y2) {
			int flipFrom[CLASS_COUNT] = { -1, -1, -1 };
			switch (mode) {
				case Tool::Open:
				case Tool::Extra:
					flipFrom[ROCK] = FLOOR;
					break;
				case Tool::Close:
					flipFrom[FLOOR] = ROCK;
					flipFrom[EXTRA] = ROCK;
					break;
				case Tool::Dry:
					flipFrom[EXTRA] = FLOOR;
					break;
				default:
					return 0;
			}

			const int W = T.W;
			auto& lab = T.lab;
			const auto known = [&](int t) {
				return model.buckets.contains(labKey3(lab, W, t));
			};
			const auto unknownAround = [&](int t) {
				int n = 0;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						if (!known(t + dy * W + dx)) {
							++n;
						}
					}
				}
				return n;
			};

			int flips = 0;
			for (int pass = 0; pass < 6; ++pass) {
				bool any = false;
				for (int y = y1; y <= y2; ++y) {
					for (int x = x1; x <= x2; ++x) {
						const int t = y * W + x;
						if (known(t)) {
							continue;
						}
						int bestF = -1;
						uint8_t bestC = 0;
						int bestGain = 0;
						for (int dy = -1; dy <= 1; ++dy) {
							for (int dx = -1; dx <= 1; ++dx) {
								const int f = t + dy * W + dx;
								const uint8_t from = lab[f];
								if (from >= CLASS_COUNT || flipFrom[from] < 0 || T.locked[f] || T.foreign[f]) {
									continue;
								}
								const int before = unknownAround(f);
								lab[f] = static_cast<uint8_t>(flipFrom[from]);
								const int gain = before - unknownAround(f);
								lab[f] = from;
								if (gain > bestGain || (f == t && gain == 0 && bestF < 0)) {
									bestF = f;
									bestC = static_cast<uint8_t>(flipFrom[from]);
									bestGain = gain;
								}
							}
						}
						if (bestF >= 0) {
							lab[bestF] = bestC;
							++flips;
							any = true;
						}
					}
				}
				if (!any) {
					break;
				}
			}
			return flips;
		}
	}

	// ========================================================================
	// Stack
	// ========================================================================

	std::string Stack::key() const {
		std::string s = std::to_string(ground);
		s += '|';
		for (size_t i = 0; i < items.size(); ++i) {
			if (i > 0) {
				s += ',';
			}
			s += std::to_string(items[i].first);
			if (items[i].second != 0) {
				s += ':';
				s += std::to_string(items[i].second);
			}
		}
		return s;
	}

	bool Stack::parse(std::string_view text, Stack& out) {
		out = Stack {};
		const size_t bar = text.find('|');
		int value = 0;
		if (bar == std::string_view::npos || !parseInt(text.substr(0, bar), value)) {
			return false;
		}
		out.ground = static_cast<uint16_t>(value);
		std::string_view rest = text.substr(bar + 1);
		while (!rest.empty()) {
			const size_t comma = rest.find(',');
			const std::string_view token = rest.substr(0, comma);
			rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
			const size_t colon = token.find(':');
			int id = 0;
			int sub = 0;
			if (!parseInt(token.substr(0, colon), id)) {
				return false;
			}
			if (colon != std::string_view::npos && !parseInt(token.substr(colon + 1), sub)) {
				return false;
			}
			out.items.emplace_back(static_cast<uint16_t>(id), static_cast<uint16_t>(sub));
		}
		return true;
	}

	Stack Stack::fromTile(const Tile* tile) {
		Stack st;
		if (!tile) {
			return st;
		}
		if (tile->ground) {
			st.ground = tile->ground->getID();
		}
		for (const auto& item : tile->items) {
			if (!item) {
				continue;
			}
			uint16_t sub = 0;
			if (item->isStackable()) {
				const int count = item->getCount();
				if (count > 1) {
					sub = static_cast<uint16_t>(count);
				}
			} else if (item->isSplash() || item->isFluidContainer()) {
				sub = item->getSubtype();
			}
			st.items.emplace_back(item->getID(), sub);
		}
		return st;
	}

	void Stack::applyTo(Tile& tile) const {
		tile.items.clear();
		tile.setGround(nullptr);
		if (ground != 0) {
			// addItem routes a ground item to setGround
			tile.addItem(Item::Create(ground));
		}
		for (const auto& [id, sub] : items) {
			tile.addItem(sub != 0 ? Item::Create(id, sub) : Item::Create(id));
		}
	}

	bool isLockedTile(const Tile* tile) {
		if (!tile) {
			return false;
		}
		if (tile->isHouseTile()) {
			return true;
		}
		if (tile->ground && isSpecialItem(tile->ground.get())) {
			return true;
		}
		for (const auto& item : tile->items) {
			if (item && isSpecialItem(item.get())) {
				return true;
			}
		}
		return false;
	}

	// ========================================================================
	// Preset (JSON)
	// ========================================================================

	bool Preset::load(const std::string& path, std::string& error) {
		samples.clear();
		grounds.clear();
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			error = "Could not open " + path;
			return false;
		}
		const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
		if (j.is_discarded() || !j.is_object()) {
			error = "Invalid JSON in " + path;
			return false;
		}
		try {
			if (const auto it = j.find("grounds"); it != j.end() && it->is_array()) {
				for (const auto& g : *it) {
					GroundClass gc;
					gc.id = static_cast<uint16_t>(g.value("id", 0));
					gc.cls = static_cast<uint8_t>(std::clamp(g.value("class", static_cast<int>(FLOOR)), 0, CLASS_COUNT - 1));
					gc.count = g.value("count", 0);
					if (gc.id != 0) {
						grounds.push_back(gc);
					}
				}
			}
			if (const auto it = j.find("samples"); it != j.end() && it->is_array()) {
				for (const auto& s : *it) {
					Sample sample;
					sample.w = s.value("w", 0);
					sample.h = s.value("h", 0);
					sample.x = s.value("x", 0);
					sample.y = s.value("y", 0);
					sample.z = s.value("z", 7);
					sample.map = s.value("map", std::string());
					sample.tiles = s.value("tiles", 0);
					if (const auto st = s.find("stacks"); st != s.end() && st->is_array()) {
						sample.stacks = st->get<std::vector<std::string>>();
					}
					if (sample.w > 0 && sample.h > 0 && sample.stacks.size() == static_cast<size_t>(sample.w) * sample.h) {
						samples.push_back(std::move(sample));
					}
				}
			}
		} catch (const nlohmann::json::exception& e) {
			error = std::string("Invalid preset ") + path + ": " + e.what();
			samples.clear();
			grounds.clear();
			return false;
		}
		return true;
	}

	bool Preset::save(const std::string& path, std::string& error) const {
		nlohmann::json j;
		j["version"] = 1;
		nlohmann::json gs = nlohmann::json::array();
		for (const GroundClass& g : grounds) {
			gs.push_back({ { "id", static_cast<int>(g.id) }, { "class", static_cast<int>(g.cls) }, { "count", g.count } });
		}
		j["grounds"] = std::move(gs);
		nlohmann::json ss = nlohmann::json::array();
		for (const Sample& s : samples) {
			ss.push_back({ { "w", s.w }, { "h", s.h }, { "x", s.x }, { "y", s.y }, { "z", s.z }, { "map", s.map }, { "tiles", s.tiles }, { "stacks", s.stacks } });
		}
		j["samples"] = std::move(ss);

		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out) {
			error = "Could not write " + path;
			return false;
		}
		out << j.dump();
		return static_cast<bool>(out);
	}

	// ========================================================================
	// Model
	// ========================================================================

	int32_t Model::stackId(const std::string& key) const {
		const auto it = intern.find(key);
		return it != intern.end() ? it->second : -1;
	}

	void Model::compile(const Preset& preset) {
		*this = Model {};
		for (const GroundClass& g : preset.grounds) {
			classOf[g.id] = g.cls;
		}

		int maxW = 0;
		GH = PAD;
		for (const Sample& s : preset.samples) {
			maxW = std::max(maxW, s.w);
			GH += s.h + PAD;
		}
		GW = maxW + 2 * PAD;
		N = GW * GH;
		lab.assign(N, UNKNOWN);
		sid.assign(N, 0);
		stacks.emplace_back();
		stackKeys.emplace_back();

		// Samples stacked vertically, PAD unknown tiles around each one, so the
		// 9x9 window of any sample tile never reaches another sample.
		int oy = PAD;
		for (const Sample& s : preset.samples) {
			for (int y = 0; y < s.h; ++y) {
				for (int x = 0; x < s.w; ++x) {
					const std::string& text = s.stacks[static_cast<size_t>(y) * s.w + x];
					if (text == "?") {
						continue;
					}
					int32_t id = 0;
					if (const auto found = intern.find(text); found != intern.end()) {
						id = found->second;
					} else {
						Stack st;
						if (!Stack::parse(text, st) || st.ground == 0) {
							continue; // no ground: unknown
						}
						id = static_cast<int32_t>(stacks.size());
						stacks.push_back(std::move(st));
						stackKeys.push_back(text);
						intern.emplace(text, id);
					}
					const int idx = (oy + y) * GW + PAD + x;
					const auto cls = classOf.find(stacks[id].ground);
					lab[idx] = cls != classOf.end() ? cls->second : FLOOR;
					sid[idx] = id;
				}
			}
			oy += s.h + PAD;
		}

		rowCode = buildRowCodes(lab, GW, GH);

		std::unordered_map<int32_t, int> rockCount;
		for (int idx = 0; idx < N; ++idx) {
			const uint8_t c = lab[idx];
			if (c == UNKNOWN) {
				continue;
			}
			++classCount[c];
			byLabel[c].push_back(idx);
			const uint32_t k3 = key3(rowCode.data(), idx, GW);
			if (hasUnknown9(k3)) {
				continue;
			}
			auto [bucket, inserted] = buckets.try_emplace(k3);
			if (inserted) {
				keyList.push_back(k3);
			}
			bucket->second.push_back(idx);
			++positions;
			bySid[sid[idx]].push_back(idx);
			keep[sid[idx]].insert(key5(rowCode.data(), idx, GW));
			if (c == ROCK) {
				++rockCount[sid[idx]];
			}
		}

		int bestRock = 0;
		for (const auto& [id, count] : rockCount) {
			if (count > bestRock) {
				bestRock = count;
				commonRock = id;
			}
		}
		rules = detectRules(*this);
	}

	// ========================================================================
	// Target
	// ========================================================================

	void Target::resize(int w, int h) {
		W = w;
		H = h;
		const size_t n = static_cast<size_t>(w) * h;
		lab.assign(n, ROCK);
		orig.assign(n, ROCK);
		sid.assign(n, -1);
		key.assign(n, std::string());
		locked.assign(n, 0);
		foreign.assign(n, 0);
		rowCode.clear();
	}

	void Target::computeRowCodes() {
		rowCode = buildRowCodes(lab, W, H);
	}

	void Target::read(const Model& model, BaseMap& map, int x1, int y1, int x2, int y2, int floor, bool protectForeign) {
		ox = x1;
		oy = y1;
		z = floor;
		resize(x2 - x1 + 1, y2 - y1 + 1);
		for (int y = 0; y < H; ++y) {
			for (int x = 0; x < W; ++x) {
				const int t = y * W + x;
				const int mx = x1 + x;
				const int my = y1 + y;
				const Tile* tile = (mx >= 0 && my >= 0) ? map.getTile(mx, my, z) : nullptr;
				// no ground counts as rock (the background); a ground outside the
				// preset too, but it is protected: it only changes where painted
				uint8_t c = ROCK;
				if (tile && tile->ground) {
					const auto it = model.classOf.find(tile->ground->getID());
					if (it != model.classOf.end()) {
						c = it->second;
					} else if (protectForeign) {
						foreign[t] = 1;
					}
				}
				lab[t] = c;
				orig[t] = c;
				key[t] = Stack::fromTile(tile).key();
				sid[t] = model.stackId(key[t]);
				locked[t] = isLockedTile(tile) ? 1 : 0;
			}
		}
	}

	// ========================================================================
	// Stroke
	// ========================================================================

	int readMargin(int margin) {
		// fixes reach 4 tiles past the stroke, the ring `margin` more, the window PAD
		return margin + PAD + 6;
	}

	Plan planStroke(const Model& model, Target& T, Tool tool, const std::vector<int>& cells, int x1, int y1, int x2, int y2, int margin, bool fixShapes) {
		Plan plan;
		const int W = T.W;

		if (tool == Tool::Reroll) {
			// same shape, another drawing
			for (int t : cells) {
				if (!T.locked[t]) {
					plan.todo.push_back(t);
					plan.avoid[t] = T.sid[t];
				}
			}
			plan.painted = static_cast<int>(plan.todo.size());
			T.computeRowCodes();
			return plan;
		}

		for (int t : cells) {
			if (T.locked[t]) {
				continue;
			}
			const uint8_t cur = T.lab[t];
			const uint8_t next = paintedClass(tool, cur);
			if (next != cur) {
				T.lab[t] = next;
				++plan.painted;
			}
		}
		if (plan.painted == 0) {
			return plan;
		}
		if (fixShapes) {
			plan.ruleCount = applyShapeRules(T, tool, model.rules, x1 - 2, y1 - 2, x2 + 2, y2 + 2, plan.reasons);
			const int repaired = repairUnknownShapes(model, T, tool, x1 - 3, y1 - 3, x2 + 3, y2 + 3);
			plan.ruleCount += repaired;
			plan.reasons[UnknownShape] += repaired;
		}
		T.computeRowCodes();

		// everything that changed class + the ring around it whose stack no longer fits
		std::vector<int> changed;
		for (int t = 0; t < W * T.H; ++t) {
			if (T.lab[t] != T.orig[t]) {
				changed.push_back(t);
			}
		}
		std::vector<uint8_t> seen(T.lab.size(), 0);
		for (int t : changed) {
			seen[t] = 1;
			plan.todo.push_back(t);
		}
		for (int t : changed) {
			for (int dy = -margin; dy <= margin; ++dy) {
				for (int dx = -margin; dx <= margin; ++dx) {
					const int q = t + dy * W + dx;
					if (seen[q]) {
						continue;
					}
					seen[q] = 1;
					if (!T.locked[q] && !T.foreign[q] && !isKeepable(model, T, q)) {
						plan.todo.push_back(q);
					}
				}
			}
		}
		return plan;
	}

	// ========================================================================
	// Synthesis
	// ========================================================================

	void synthesize(const Model& model, Target& T, const std::vector<int>& todo, const SynthOptions& opts, std::mt19937& rng) {
		std::uniform_real_distribution<float> unit(0.0f, 1.0f);
		const auto randomIndex = [&](size_t n) {
			return std::uniform_int_distribution<size_t>(0, n - 1)(rng);
		};
		std::unordered_map<int, float> localUsage;
		std::unordered_map<int, float>& usage = opts.usage ? *opts.usage : localUsage;

		const int GW = model.GW;
		const int TW = T.W;
		const int NS = model.N;
		const uint32_t* SH = model.rowCode.data();
		const uint32_t* TH = T.rowCode.data();
		const size_t TN = T.lab.size();

		std::vector<uint8_t> isTodo(TN, 0);
		for (int t : todo) {
			isTodo[t] = 1;
			T.sid[t] = 0;
		}

		// 5x5 neighbourhood for coherence; the inner ring weighs more
		struct Offset {
			int t;
			int s;
			int w;
			bool inner; // 3x3 ring ("near" is a Windows macro)
		};
		std::vector<Offset> offsets;
		for (int dy = -2; dy <= 2; ++dy) {
			for (int dx = -2; dx <= 2; ++dx) {
				if (dx == 0 && dy == 0) {
					continue;
				}
				const bool inner = std::abs(dx) <= 1 && std::abs(dy) <= 1;
				offsets.push_back({ dy * TW + dx, dy * GW + dx, inner ? 3 : 1, inner });
			}
		}

		// order: from the outside in (starting at what already exists), ties at random
		std::vector<int> dist(TN, INT_MAX);
		std::vector<int> queue;
		queue.reserve(todo.size());
		const int n4[4] = { -1, 1, -TW, TW };
		for (int t : todo) {
			for (int k = 0; k < 4; ++k) {
				if (!isTodo[t + n4[k]]) {
					dist[t] = 1;
					queue.push_back(t);
					break;
				}
			}
		}
		for (size_t head = 0; head < queue.size(); ++head) {
			const int t = queue[head];
			for (int k = 0; k < 4; ++k) {
				const int q = t + n4[k];
				if (isTodo[q] && dist[q] == INT_MAX) {
					dist[q] = dist[t] + 1;
					queue.push_back(q);
				}
			}
		}
		std::vector<float> tie(TN, 0.0f);
		for (int t : todo) {
			tie[t] = unit(rng);
		}
		std::vector<int> order(todo);
		std::sort(order.begin(), order.end(), [&](int a, int b) {
			if (dist[a] != dist[b]) {
				return dist[a] < dist[b];
			}
			return tie[a] < tie[b];
		});

		std::vector<int> src(TN, -1);
		std::vector<int> stamp(NS, -1);

		for (const int t : order) {
			const uint8_t lt = T.lab[t];
			int32_t avoid = INT32_MIN;
			if (opts.avoid) {
				if (const auto it = opts.avoid->find(t); it != opts.avoid->end()) {
					avoid = it->second;
				}
			}
			int best = -1;
			float bestCost = std::numeric_limits<float>::max();

			const auto consider = [&](int s) {
				if (s < 0 || s >= NS || stamp[s] == t) {
					return;
				}
				stamp[s] = t;
				if (model.lab[s] != lt) {
					return;
				}
				const int32_t sidS = model.sid[s];
				if (sidS == 0) {
					return;
				}

				// class cost: 9x9 + 2x 5x5 + 4x 3x3; a different 3x3 costs +100
				float cost = 0.0f;
				int h3 = 0;
				for (int r = -4; r <= 4; ++r) {
					const uint32_t x = TH[t + r * TW] ^ SH[s + r * GW];
					if (x == 0) {
						continue;
					}
					const uint32_t m = (x | (x >> 1)) & ALL9;
					cost += static_cast<float>(cellCount(m));
					if (r >= -2 && r <= 2) {
						const uint32_t m5 = m & MASK5;
						if (m5 != 0) {
							cost += 2.0f * cellCount(m5);
							if (r >= -1 && r <= 1) {
								const uint32_t m3 = m & MASK3;
								if (m3 != 0) {
									const int c3 = cellCount(m3);
									h3 += c3;
									cost += 4.0f * c3;
								}
							}
						}
					}
				}
				if (h3 > 0) {
					cost += 100.0f;
				}

				// coherence with what is already decided around
				int coherence = 0;
				for (const Offset& o : offsets) {
					const int32_t q = T.sid[t + o.t];
					if (q != 0 && q != model.sid[s + o.s]) {
						coherence += o.w;
					}
				}

				float used = 0.0f;
				if (const auto it = usage.find(s); it != usage.end()) {
					used = it->second;
				}
				cost += opts.wCoh * coherence + opts.wUse * used + opts.jitter * unit(rng);
				if (sidS == avoid) {
					cost += 6.0f;
				}
				if (cost < bestCost) {
					bestCost = cost;
					best = s;
				}
			};

			// 1) continue the stretch the neighbours copied
			for (const Offset& o : offsets) {
				const int q = t + o.t;
				if (src[q] >= 0) {
					consider(src[q] - o.s);
				} else if (o.inner && !isTodo[q] && T.sid[q] > 0) {
					if (const auto it = model.bySid.find(T.sid[q]); it != model.bySid.end() && !it->second.empty()) {
						consider(it->second[randomIndex(it->second.size())] - o.s);
						consider(it->second[randomIndex(it->second.size())] - o.s);
					}
				}
			}

			// 2) start over somewhere else with the same 3x3
			const uint32_t k3 = key3(TH, t, TW);
			auto bucket = model.buckets.find(k3);
			if (bucket == model.buckets.end()) {
				const int64_t nk = nearestKey(model, k3);
				if (nk >= 0) {
					bucket = model.buckets.find(static_cast<uint32_t>(nk));
				}
			}
			if (bucket != model.buckets.end()) {
				const std::vector<int>& list = bucket->second;
				if (list.size() <= static_cast<size_t>(2 * opts.K)) {
					for (int s : list) {
						consider(s);
					}
				} else {
					for (int i = 0; i < opts.K; ++i) {
						consider(list[randomIndex(list.size())]);
					}
				}
			}

			// 3) last resort: any tile of the same class
			if (best < 0 && lt < CLASS_COUNT && !model.byLabel[lt].empty()) {
				const std::vector<int>& list = model.byLabel[lt];
				for (int i = 0; i < opts.K; ++i) {
					consider(list[randomIndex(list.size())]);
				}
			}

			if (best >= 0) {
				src[t] = best;
				T.sid[t] = model.sid[best];
				usage[best] += 1.0f;
			} else {
				T.sid[t] = -1;
			}
		}
	}

	// ========================================================================
	// Brush
	// ========================================================================

	std::vector<std::pair<int, int>> brushCells(int cx, int cy, int size, bool circle) {
		std::vector<std::pair<int, int>> cells;
		const int lo = -((size - 1) / 2);
		const int hi = lo + size - 1;
		const double c = (lo + hi) / 2.0;
		const double r2 = (size / 2.0) * (size / 2.0) + 0.25;
		for (int dy = lo; dy <= hi; ++dy) {
			for (int dx = lo; dx <= hi; ++dx) {
				if (circle && size > 2) {
					const double ex = dx - c;
					const double ey = dy - c;
					if (ex * ex + ey * ey > r2) {
						continue;
					}
				}
				cells.emplace_back(cx + dx, cy + dy);
			}
		}
		return cells;
	}

	std::vector<std::pair<int, int>> linePoints(int x0, int y0, int x1, int y1) {
		std::vector<std::pair<int, int>> points;
		const int dx = std::abs(x1 - x0);
		const int dy = -std::abs(y1 - y0);
		const int sx = x0 < x1 ? 1 : -1;
		const int sy = y0 < y1 ? 1 : -1;
		int err = dx + dy;
		while (true) {
			points.emplace_back(x0, y0);
			if (x0 == x1 && y0 == y1) {
				break;
			}
			const int e2 = 2 * err;
			if (e2 >= dy) {
				err += dy;
				x0 += sx;
			}
			if (e2 <= dx) {
				err += dx;
				y0 += sy;
			}
		}
		return points;
	}

	// ========================================================================
	// Preview
	// ========================================================================

	std::vector<PreviewTile> generatePreview(const Model& model, int w, int h, std::mt19937& rng) {
		std::vector<PreviewTile> out;
		if (model.empty()) {
			return out;
		}
		std::uniform_real_distribution<double> unit(0.0, 1.0);
		const auto range = [&](double a, double b) {
			return a + (b - a) * unit(rng);
		};

		// local grid: the drawn area plus a frame of plain rock for the 9x9 window
		const int MG = PAD + 3;
		Target T;
		T.resize(w + 2 * MG, h + 2 * MG);
		const int W = T.W;
		const auto inside = [&](int x, int y) {
			return x >= MG + 2 && y >= MG + 2 && x < MG + w - 2 && y < MG + h - 2;
		};
		const auto carve = [&](double cx, double cy, double rx, double ry, uint8_t cls, bool onlyOpen) {
			for (int y = static_cast<int>(cy - ry) - 1; y <= static_cast<int>(cy + ry) + 1; ++y) {
				for (int x = static_cast<int>(cx - rx) - 1; x <= static_cast<int>(cx + rx) + 1; ++x) {
					if (!inside(x, y)) {
						continue;
					}
					const double ex = (x - cx) / rx;
					const double ey = (y - cy) / ry;
					if (ex * ex + ey * ey <= 1.0 && (!onlyOpen || T.lab[y * W + x] == FLOOR)) {
						T.lab[y * W + x] = cls;
					}
				}
			}
		};

		// rooms joined by winding corridors
		const int rooms = 3 + static_cast<int>(rng() % 3);
		std::vector<std::pair<double, double>> centers;
		double bigR = 0.0;
		std::pair<double, double> bigCenter;
		for (int i = 0; i < rooms; ++i) {
			const double cx = range(MG + 7, MG + w - 8);
			const double cy = range(MG + 6, MG + h - 7);
			const double rx = range(3.0, 6.5);
			const double ry = range(2.5, 5.0);
			carve(cx, cy, rx, ry, FLOOR, false);
			centers.emplace_back(cx, cy);
			if (rx * ry > bigR) {
				bigR = rx * ry;
				bigCenter = { cx, cy };
			}
		}
		for (size_t i = 1; i < centers.size(); ++i) {
			double px = centers[i - 1].first;
			double py = centers[i - 1].second;
			const double tx = centers[i].first;
			const double ty = centers[i].second;
			for (int step = 0; step < 400; ++step) {
				const double dx = tx - px;
				const double dy = ty - py;
				const double len = std::sqrt(dx * dx + dy * dy);
				if (len < 1.0) {
					break;
				}
				px += dx / len + range(-0.7, 0.7);
				py += dy / len + range(-0.7, 0.7);
				const double r = range(1.3, 2.1);
				carve(px, py, r, r, FLOOR, false);
			}
		}

		// round off single bumps and holes
		std::vector<uint8_t> next = T.lab;
		for (int y = MG + 2; y < MG + h - 2; ++y) {
			for (int x = MG + 2; x < MG + w - 2; ++x) {
				const int t = y * W + x;
				int open = 0;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						if ((dx != 0 || dy != 0) && isOpen(T.lab[t + dy * W + dx])) {
							++open;
						}
					}
				}
				if (isOpen(T.lab[t]) && open <= 2) {
					next[t] = ROCK;
				} else if (!isOpen(T.lab[t]) && open >= 6) {
					next[t] = FLOOR;
				}
			}
		}
		T.lab = std::move(next);

		const bool hasExtra = model.classCount[EXTRA] > 0;
		if (hasExtra) {
			carve(bigCenter.first, bigCenter.second, range(1.6, 2.6), range(1.4, 2.2), EXTRA, true);
		}

		// the same shape fixes a stroke gets
		std::array<int, REASON_COUNT> reasons {};
		const int x1 = MG + 1, y1 = MG + 1, x2 = MG + w - 2, y2 = MG + h - 2;
		if (hasExtra) {
			applyShapeRules(T, Tool::Extra, model.rules, x1, y1, x2, y2, reasons);
		}
		applyShapeRules(T, Tool::Open, model.rules, x1, y1, x2, y2, reasons);
		repairUnknownShapes(model, T, Tool::Open, x1, y1, x2, y2);
		if (hasExtra) {
			repairUnknownShapes(model, T, Tool::Dry, x1, y1, x2, y2);
		}
		T.computeRowCodes();

		// the frame stays plain rock, like an untouched map around the cave
		std::fill(T.sid.begin(), T.sid.end(), model.commonRock);
		std::vector<int> todo;
		for (int y = MG; y < MG + h; ++y) {
			for (int x = MG; x < MG + w; ++x) {
				todo.push_back(y * W + x);
			}
		}
		SynthOptions opts;
		synthesize(model, T, todo, opts, rng);

		const int frame = 2;
		for (int y = MG - frame; y < MG + h + frame; ++y) {
			for (int x = MG - frame; x < MG + w + frame; ++x) {
				const int32_t s = T.sid[y * W + x];
				if (s > 0) {
					out.push_back({ x - (MG - frame), y - (MG - frame), s });
				}
			}
		}
		return out;
	}
}
