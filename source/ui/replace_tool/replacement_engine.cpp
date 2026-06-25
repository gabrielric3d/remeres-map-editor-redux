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
#include "ui/gui.h"
#include "map/tile.h"
#include "map/tile_operations.h"
#include "game/item.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>

void ReplacementEngine::ExecuteReplacement(Editor* editor, const std::vector<ReplacementRule>& rules, ReplaceScope scope, const std::vector<Position>* posVec) {
	if (rules.empty()) {
		return;
	}

	std::map<uint16_t, const ReplacementRule*> ruleMap;
	for (const auto& rule : rules) {
		if (rule.fromId != 0) {
			ruleMap[rule.fromId] = &rule;
		}
	}

	// Relocations (offset != 0) must be deferred: we cannot mutate the tile
	// container we are iterating, nor place a new item on a tile the scan has not
	// visited yet (that would re-trigger the same rule). So the scan only records
	// the moves; they are applied after the whole scope has been visited.
	struct PendingMove {
		Tile* sourceTile = nullptr;
		Item* sourceItem = nullptr; // identity, used to erase from the source tile
		Position targetPos;
		uint16_t newId = 0; // TRASH_ITEM_ID => delete the source only, create nothing
	};
	std::vector<PendingMove> pendingMoves;
	std::set<Tile*> dirtyTiles;

	const int mapW = editor->map.getWidth();
	const int mapH = editor->map.getHeight();

	auto tileProcessor = [&](Tile* tile) {
		bool changed = false;
		const Position tilePos = tile->getPosition();

		// Applies a rule to a single item. relocatable is false for items nested
		// inside containers, which can only be replaced in place.
		auto apply = [&](Item* item, bool relocatable) -> bool {
			auto it = ruleMap.find(item->getID());
			if (it == ruleMap.end()) {
				return false;
			}
			const ReplacementRule* rule = it->second;
			uint16_t newId;
			if (!ResolveReplacement(newId, *rule)) {
				return false;
			}

			const bool hasOffset = (rule->offsetX != 0 || rule->offsetY != 0);
			if (hasOffset && relocatable) {
				Position target = tilePos + Position(rule->offsetX, rule->offsetY, 0);
				const bool targetOk = target.isValid() && target.x >= 0 && target.y >= 0 && target.x < mapW && target.y < mapH;
				if (targetOk) {
					// Move: remove the source here, recreate at the offset target later.
					pendingMoves.push_back({ tile, item, target, newId });
					return true;
				}
				// Out-of-bounds target: fall through to in-place so nothing is lost.
			}

			// In-place replacement (legacy behaviour / safe fallback).
			item->setID(newId == TRASH_ITEM_ID ? 0 : newId);
			return true;
		};

		if (tile->ground) {
			changed = apply(tile->ground.get(), true) || changed;
		}

		std::vector<Container*> containers;
		for (const auto& item : tile->items) {
			containers.clear();
			Container* container = item->asContainer();
			changed = apply(item.get(), true) || changed;

			if (container) {
				containers.push_back(container);
				size_t index = 0;
				while (index < containers.size()) {
					container = containers[index++];
					auto& v = container->getVector();
					for (const auto& i : v) {
						Container* c = i->asContainer();
						changed = apply(i.get(), false) || changed; // nested -> in-place only
						if (c) {
							containers.push_back(c);
						}
					}
				}
			}
		}

		if (changed) {
			TileOperations::update(tile);
			tile->modify();
		}
	};

	if (scope == ReplaceScope::Viewport && posVec) {
		// Use Viewport scope
		for (const auto& pos : *posVec) {
			Tile* tile = editor->map.getTile(pos);
			if (tile) {
				tileProcessor(tile);
			}
		}
	} else if (scope == ReplaceScope::Selection && !editor->selection.empty()) {
		// Use Selection scope
		for (Tile* tile : editor->selection.getTiles()) {
			tileProcessor(tile);
		}
	} else if (scope == ReplaceScope::AllMap) {
		// Use All Map scope
		auto allMapTileProcessor = [&tileProcessor](Map&, Tile* tile, long long) {
			tileProcessor(tile);
		};
		foreach_TileOnMap(editor->map, allMapTileProcessor);
	}

	// Apply deferred relocations. Remove every source first so a move that targets
	// a tile we just emptied still behaves predictably, then create at the targets.
	for (const auto& mv : pendingMoves) {
		Tile* src = mv.sourceTile;
		if (src->ground && src->ground.get() == mv.sourceItem) {
			src->ground.reset();
		} else {
			auto& v = src->items;
			auto vit = std::find_if(v.begin(), v.end(), [&](const std::unique_ptr<Item>& p) {
				return p.get() == mv.sourceItem;
			});
			if (vit != v.end()) {
				v.erase(vit);
			}
		}
		dirtyTiles.insert(src);
	}
	for (const auto& mv : pendingMoves) {
		if (mv.newId == TRASH_ITEM_ID) {
			continue; // delete-only, no item to create
		}
		Tile* dst = editor->map.getOrCreateTile(mv.targetPos);
		if (dst) {
			dst->addItem(Item::Create(mv.newId));
			dirtyTiles.insert(dst);
		}
	}
	for (Tile* t : dirtyTiles) {
		TileOperations::update(t);
		t->modify();
	}

	editor->map.doChange();
	g_gui.RefreshView();
}
