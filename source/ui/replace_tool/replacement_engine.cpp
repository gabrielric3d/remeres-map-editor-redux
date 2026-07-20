#include "app/main.h"
#include "replacement_engine.h"

ReplacementEngine::ReplacementEngine() : rng(std::random_device {}()) { }

bool ReplacementEngine::ResolveReplacement(uint16_t& resultId, const ReplacementRule& rule) {
	if (rule.targets.empty()) {
		return false;
	}

	// Calculate total probability weight
	int totalProb = 0;
	for (const auto& target : rule.targets) {
		totalProb += target.probability;
	}

	if (totalProb <= 0) {
		return false;
	}

	// Use a single roll from 1 to max(100, totalProb)
	// This ensures that if the UI says 100% total, exactly one target is picked.
	// If total is < 100, the remainder is "no replacement".
	std::uniform_int_distribution<int> dist(1, std::max(100, totalProb));
	int roll = dist(rng);

	int currentSum = 0;
	for (const auto& target : rule.targets) {
		currentSum += target.probability;
		if (roll <= currentSum) {
			resultId = target.id;
			return true;
		}
	}

	return false;
}

#include "editor/editor.h"
#include "editor/action.h"
#include "editor/action_queue.h"
#include "ui/gui.h"
#include "map/tile.h"
#include "map/tile_operations.h"
#include "game/item.h"
#include "game/complexitem.h"
#include "brushes/brush.h"
#include "ui/replace_tool/brush_mapping_service.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>

void ReplacementEngine::ExecuteReplacement(Editor* editor, const std::vector<ReplacementRule>& rules, ReplaceScope scope, const std::vector<Position>* posVec) {
	if (rules.empty() || !editor) {
		return;
	}

	// Item rules are keyed by source id; brush rules cannot be (their fromId is 0),
	// so they live in a separate list that is only consulted when no item rule
	// matched. That ordering is what gives item rules precedence.
	std::map<uint16_t, const ReplacementRule*> ruleMap;
	struct BrushRule {
		const ReplacementRule* rule = nullptr;
		Brush* from = nullptr;
		Brush* to = nullptr;
		int probability = 100;
		bool trash = false;
	};
	std::vector<BrushRule> brushRules;

	for (const auto& rule : rules) {
		if (rule.isBrushRule()) {
			// Pre-resolve the brushes once, outside the tile loop.
			Brush* from = BrushMappingService::FindBrush(rule.fromBrushName);
			if (!from) {
				wxLogWarning("Advanced Replace: source brush '%s' not found; rule skipped.", rule.fromBrushName);
				continue;
			}

			// A brush rule carries exactly one brush target (enforced by the UI).
			const ReplacementTarget* brushTarget = nullptr;
			for (const auto& t : rule.targets) {
				if (t.kind == SlotKind::Brush) {
					brushTarget = &t;
					break;
				}
			}

			BrushRule br;
			br.rule = &rule;
			br.from = from;

			if (brushTarget) {
				br.to = BrushMappingService::FindBrush(brushTarget->brushName);
				if (!br.to) {
					wxLogWarning("Advanced Replace: destination brush '%s' not found; rule skipped.", brushTarget->brushName);
					continue;
				}
				if (!BrushMappingService::AreCompatible(from, br.to)) {
					wxLogWarning("Advanced Replace: brushes '%s' and '%s' are not of the same family; rule skipped.", rule.fromBrushName, brushTarget->brushName);
					continue;
				}
				br.probability = brushTarget->probability > 0 ? brushTarget->probability : 100;
			} else {
				// No brush target: only a REMOVE target makes sense here.
				bool hasTrash = false;
				for (const auto& t : rule.targets) {
					if (t.kind == SlotKind::Item && t.id == TRASH_ITEM_ID) {
						hasTrash = true;
						br.probability = t.probability > 0 ? t.probability : 100;
						break;
					}
				}
				if (!hasTrash) {
					continue; // incomplete rule
				}
				br.trash = true;
			}

			brushRules.push_back(br);
		} else if (rule.fromId != 0) {
			ruleMap[rule.fromId] = &rule;
		}
	}

	if (ruleMap.empty() && brushRules.empty()) {
		return;
	}

	const int mapW = editor->map.getWidth();
	const int mapH = editor->map.getHeight();

	// Every tile we touch is deep-copied once and kept here; a single Change is
	// emitted per position at the end. This is mandatory: two Changes for the
	// same tile inside one Action would make the second silently overwrite the
	// first, losing edits.
	std::map<Position, std::unique_ptr<Tile>> pendingTiles;

	auto acquireTile = [&](const Position& pos, bool createIfMissing) -> Tile* {
		auto it = pendingTiles.find(pos);
		if (it != pendingTiles.end()) {
			return it->second.get();
		}
		Tile* live = editor->map.getTile(pos);
		if (!live) {
			if (!createIfMissing) {
				return nullptr;
			}
			live = editor->map.getOrCreateTile(pos);
			if (!live) {
				return nullptr;
			}
		}
		auto copy = TileOperations::deepCopy(live, editor->map);
		Tile* raw = copy.get();
		pendingTiles.emplace(pos, std::move(copy));
		return raw;
	};

	// Relocations (offset != 0) must be deferred: we cannot place a new item on a
	// tile the scan has not visited yet (that would re-trigger the same rule). The
	// scan only records the moves; they are applied after the whole scope was seen.
	struct PendingMove {
		Position sourcePos;
		int sourceIndex = -1; // index into tile->items; -1 => ground
		Position targetPos;
		uint16_t newId = 0; // TRASH_ITEM_ID => delete the source only, create nothing
	};
	std::vector<PendingMove> pendingMoves;

	// Resolves the replacement id for one item, checking item rules first and
	// falling back to brush rules. outRule is set so the caller can read offsets.
	auto resolveForItem = [&](const Item* item, uint16_t& newId, const ReplacementRule*& outRule) -> bool {
		auto it = ruleMap.find(item->getID());
		if (it != ruleMap.end()) {
			const ReplacementRule* rule = it->second;
			if (!ResolveReplacement(newId, *rule)) {
				return false;
			}
			outRule = rule;
			return true;
		}

		for (const auto& br : brushRules) {
			BrushMappingService::MapResult res;
			if (br.trash) {
				// A REMOVE target only needs to know whether the item belongs to
				// the source brush, so map it onto itself.
				res = BrushMappingService::MapItem(item, br.from, br.from);
			} else {
				res = BrushMappingService::MapItem(item, br.from, br.to);
			}
			if (!res.matched) {
				continue;
			}
			if (br.probability < 100) {
				std::uniform_int_distribution<int> dist(1, 100);
				if (dist(rng) > br.probability) {
					return false;
				}
			}
			if (br.trash) {
				newId = TRASH_ITEM_ID;
				outRule = br.rule;
				return true;
			}
			if (!res.resolved) {
				// Belongs to the brush but has no equivalent: keep the original
				// item instead of leaving a hole (safe failure).
				return false;
			}
			newId = res.newId;
			outRule = br.rule;
			return true;
		}

		return false;
	};

	auto tileProcessor = [&](Tile* liveTile) {
		if (!liveTile) {
			return;
		}
		const Position tilePos = liveTile->getPosition();

		// First pass over the live tile: decide what changes, and whether a copy
		// is needed at all. We only deep-copy tiles that actually change.
		Tile* work = nullptr;
		auto ensureWork = [&]() -> Tile* {
			if (!work) {
				work = acquireTile(tilePos, false);
			}
			return work;
		};

		// Applies a rule to a single item. relocatable is false for items nested
		// inside containers, which can only be replaced in place.
		// itemIndex: -1 for ground, otherwise index into tile->items.
		auto apply = [&](const Item* liveItem, int itemIndex, bool relocatable) -> void {
			uint16_t newId = 0;
			const ReplacementRule* rule = nullptr;
			if (!resolveForItem(liveItem, newId, rule)) {
				return;
			}

			const bool hasOffset = (rule->offsetX != 0 || rule->offsetY != 0);
			if (hasOffset && relocatable) {
				Position target = tilePos + Position(rule->offsetX, rule->offsetY, 0);
				const bool targetOk = target.isValid() && target.x >= 0 && target.y >= 0 && target.x < mapW && target.y < mapH;
				if (targetOk) {
					// Move: the source is removed and the item recreated at the
					// offset target in a later phase. Nothing is touched now, so
					// the recorded index stays valid.
					pendingMoves.push_back({ tilePos, itemIndex, target, newId });
					return;
				}
				// Out-of-bounds target: fall through to in-place so nothing is lost.
			}

			// In-place replacement on the working copy.
			Tile* w = ensureWork();
			if (!w) {
				return;
			}
			Item* target = nullptr;
			if (itemIndex < 0) {
				target = w->ground.get();
			} else if (itemIndex < (int)w->items.size()) {
				target = w->items[itemIndex].get();
			}
			if (target) {
				target->setID(newId == TRASH_ITEM_ID ? 0 : newId);
			}
		};

		// Nested container items can only be replaced in place. We resolve them
		// against the live tile but must edit the copy, so they are walked in
		// lockstep by index path.
		auto applyNested = [&](const Item* liveItem, const std::vector<int>& path) -> void {
			uint16_t newId = 0;
			const ReplacementRule* rule = nullptr;
			if (!resolveForItem(liveItem, newId, rule)) {
				return;
			}
			Tile* w = ensureWork();
			if (!w) {
				return;
			}
			// Walk the same index path on the copy.
			if (path.empty() || path[0] >= (int)w->items.size()) {
				return;
			}
			Item* cur = w->items[path[0]].get();
			for (size_t p = 1; p < path.size() && cur; ++p) {
				Container* c = cur->asContainer();
				if (!c) {
					return;
				}
				auto& v = c->getVector();
				if (path[p] >= (int)v.size()) {
					return;
				}
				cur = v[path[p]].get();
			}
			if (cur) {
				cur->setID(newId == TRASH_ITEM_ID ? 0 : newId);
			}
		};

		if (liveTile->ground) {
			apply(liveTile->ground.get(), -1, true);
		}

		for (size_t i = 0; i < liveTile->items.size(); ++i) {
			Item* item = liveTile->items[i].get();
			apply(item, (int)i, true);

			Container* container = item->asContainer();
			if (!container) {
				continue;
			}
			// Depth-first walk recording the index path of each nested item.
			struct Frame {
				Container* container;
				std::vector<int> path;
			};
			std::vector<Frame> stack;
			stack.push_back({ container, { (int)i } });
			while (!stack.empty()) {
				Frame frame = stack.back();
				stack.pop_back();
				auto& v = frame.container->getVector();
				for (size_t k = 0; k < v.size(); ++k) {
					std::vector<int> childPath = frame.path;
					childPath.push_back((int)k);
					applyNested(v[k].get(), childPath);
					if (Container* c = v[k]->asContainer()) {
						stack.push_back({ c, childPath });
					}
				}
			}
		}
	};

	if (scope == ReplaceScope::Viewport && posVec) {
		for (const auto& pos : *posVec) {
			tileProcessor(editor->map.getTile(pos));
		}
	} else if (scope == ReplaceScope::Selection && !editor->selection.empty()) {
		for (Tile* tile : editor->selection.getTiles()) {
			tileProcessor(tile);
		}
	} else if (scope == ReplaceScope::AllMap) {
		auto allMapTileProcessor = [&tileProcessor](Map&, Tile* tile, long long) {
			tileProcessor(tile);
		};
		foreach_TileOnMap(editor->map, allMapTileProcessor);
	}

	// Apply deferred relocations. Remove every source first (descending index per
	// tile so earlier removals do not invalidate later indices), then create the
	// items at the targets. Both phases go through acquireTile, so a position that
	// is both a source and a target still yields a single Change.
	{
		std::map<Position, std::vector<int>> removalsByPos;
		for (const auto& mv : pendingMoves) {
			removalsByPos[mv.sourcePos].push_back(mv.sourceIndex);
		}
		for (auto& [pos, indices] : removalsByPos) {
			Tile* w = acquireTile(pos, false);
			if (!w) {
				continue;
			}
			std::sort(indices.begin(), indices.end(), std::greater<int>());
			for (int idx : indices) {
				if (idx < 0) {
					w->ground.reset();
				} else if (idx < (int)w->items.size()) {
					w->items.erase(w->items.begin() + idx);
				}
			}
		}
	}
	for (const auto& mv : pendingMoves) {
		if (mv.newId == TRASH_ITEM_ID) {
			continue; // delete-only, no item to create
		}
		Tile* w = acquireTile(mv.targetPos, true);
		if (w) {
			w->addItem(Item::Create(mv.newId));
		}
	}

	if (pendingTiles.empty()) {
		return;
	}

	auto batch = editor->actionQueue->createBatch(ACTION_REPLACE_ITEMS);
	auto action = editor->actionQueue->createAction(batch.get());
	for (auto& [pos, tile] : pendingTiles) {
		TileOperations::update(tile.get());
		action->addChange(std::make_unique<Change>(std::move(tile)));
	}

	if (action->size() == 0) {
		return; // nothing to record, keep the undo history clean
	}

	batch->addAndCommitAction(std::move(action));
	editor->addBatch(std::move(batch));

	g_gui.RefreshView();
}
